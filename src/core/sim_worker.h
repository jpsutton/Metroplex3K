// One background thread that runs one job at a time (the sim day tick), plus a mailbox that lets the
// job hand work back to the owner thread (anything that touches owner-only state, such as the UI).
//
// Owner side:  start(job), busy(), wait(), service()
// Worker side: callOnOwner(fn) (blocks until the owner ran fn), postToOwner(fn) (fire and forget)
//
// Memory ordering: everything a job wrote is visible to the owner after busy() returned false or
// wait() returned; everything the owner wrote before start() is visible to the job. Calls made from
// a thread other than the worker run inline, so code that is shared with a synchronous path needs no
// branches.
#pragma once
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace m3k {

class SimWorker {
 public:
  SimWorker() = default;
  ~SimWorker() { stop(); }
  SimWorker(const SimWorker&) = delete;
  SimWorker& operator=(const SimWorker&) = delete;

  // Start `job` on the worker thread. Precondition: not busy().
  void start(std::function<void()> job) {
    std::unique_lock<std::mutex> lk(mu_);
    if (!thread_.joinable()) thread_ = std::thread([this] { loop(); });
    job_ = std::move(job);
    running_ = true;
    error_ = nullptr;
    cv_.notify_all();
  }

  // True while a job has started and not finished.
  bool busy() const {
    std::lock_guard<std::mutex> lk(mu_);
    return running_;
  }

  // Owner: run requests the job queued so far. Does not block.
  void service() {
    std::deque<Request> batch;
    {
      std::lock_guard<std::mutex> lk(mu_);
      batch.swap(requests_);
    }
    for (Request& r : batch) {
      r.fn();
      if (r.done) {
        std::lock_guard<std::mutex> lk(mu_);
        *r.done = true;
        cv_.notify_all();
      }
    }
  }

  // Owner: block until the job finished, running its requests meanwhile. Rethrows a job exception.
  void wait() {
    for (;;) {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait(lk, [this] { return !running_ || !requests_.empty(); });
      if (requests_.empty()) break;
      lk.unlock();
      service();
    }
    service();   // requests posted just before the job ended
    std::lock_guard<std::mutex> lk(mu_);
    if (error_) { auto e = error_; error_ = nullptr; std::rethrow_exception(e); }
  }

  // True when called on the worker thread.
  bool onWorker() const {
    std::lock_guard<std::mutex> lk(mu_);
    return std::this_thread::get_id() == workerId_;
  }

  // Worker: run fn on the owner thread and wait for it. Inline when not on the worker thread.
  void callOnOwner(std::function<void()> fn) {
    if (!onWorker()) { fn(); return; }
    bool done = false;
    std::unique_lock<std::mutex> lk(mu_);
    requests_.push_back(Request{std::move(fn), &done});
    cv_.notify_all();
    cv_.wait(lk, [&] { return done; });
  }

  // Worker: queue fn for the owner thread and continue. Inline when not on the worker thread.
  void postToOwner(std::function<void()> fn) {
    if (!onWorker()) { fn(); return; }
    std::lock_guard<std::mutex> lk(mu_);
    requests_.push_back(Request{std::move(fn), nullptr});
    cv_.notify_all();
  }

 private:
  struct Request {
    std::function<void()> fn;
    bool* done;   // set when the owner ran fn; null for fire-and-forget
  };

  void loop() {
    std::unique_lock<std::mutex> lk(mu_);
    workerId_ = std::this_thread::get_id();
    for (;;) {
      cv_.wait(lk, [this] { return quit_ || (running_ && job_); });
      if (quit_) return;
      auto job = std::move(job_);
      job_ = nullptr;
      lk.unlock();
      std::exception_ptr err;
      try { job(); } catch (...) { err = std::current_exception(); }
      lk.lock();
      error_ = err;
      running_ = false;
      cv_.notify_all();
    }
  }

  void stop() {
    {
      std::unique_lock<std::mutex> lk(mu_);
      quit_ = true;
      cv_.notify_all();
    }
    if (thread_.joinable()) thread_.join();
  }

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::thread thread_;
  std::thread::id workerId_;
  std::function<void()> job_;
  std::deque<Request> requests_;
  std::exception_ptr error_;
  bool running_ = false, quit_ = false;
};

}  // namespace m3k
