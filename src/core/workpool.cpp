// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/workpool.h"

#include <algorithm>
#include <cstdlib>

namespace m3k {

WorkerPool::WorkerPool(int threads) {
  for (int i = 1; i < threads; i++) workers_.emplace_back([this] { workerLoop(); });
}

WorkerPool::~WorkerPool() {
  { std::lock_guard<std::mutex> l(mu_); quit_ = true; }
  wake_.notify_all();
  for (auto& t : workers_) t.join();
}

WorkerPool& WorkerPool::shared() {
  static WorkerPool pool([] {
    if (const char* e = std::getenv("M3K_THREADS"); e && *e) return std::max(1, std::atoi(e));
    return std::clamp(int(std::thread::hardware_concurrency()), 1, 16);
  }());
  return pool;
}

void WorkerPool::drain() {
  for (int i; (i = next_.fetch_add(1)) < n_;) (*fn_)(i);
}

void WorkerPool::workerLoop() {
  uint64_t seen = 0;
  for (;;) {
    {
      std::unique_lock<std::mutex> l(mu_);
      wake_.wait(l, [&] { return quit_ || gen_ != seen; });
      if (quit_) return;
      seen = gen_;
    }
    drain();
    std::lock_guard<std::mutex> l(mu_);
    if (--busy_ == 0) done_.notify_one();
  }
}

void WorkerPool::run(int n, const std::function<void(int)>& fn) {
  if (n <= 0) return;
  if (workers_.empty() || n == 1) { for (int i = 0; i < n; i++) fn(i); return; }
  std::lock_guard<std::mutex> one(runMu_);
  {
    std::lock_guard<std::mutex> l(mu_);
    fn_ = &fn; n_ = n; next_ = 0; busy_ = int(workers_.size()); gen_++;
  }
  wake_.notify_all();
  drain();
  std::unique_lock<std::mutex> l(mu_);
  done_.wait(l, [&] { return busy_ == 0; });
  fn_ = nullptr;
}

}  // namespace m3k
