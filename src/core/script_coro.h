// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A minimal coroutine on a helper thread with strict hand-off: only one of the caller and the helper ever runs, so the code in
// the helper may touch the same (unsynchronised) game objects as the caller. Used by the scenario scripts, whose pop-up
// command must suspend the script at once and resume it after the player's OK (spec 23 4.7.1, observed).
#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace m3k {

class ScriptCoroutine {
 public:
  ~ScriptCoroutine() { abort(); }
  // Run fn on the helper until it finishes or calls yield(); returns then. Not re-entrant; ignored while suspended.
  void run(std::function<void()> fn) {
    if (suspended_ || running_) return;
    join();
    aborted_ = false;
    running_ = true; helperTurn_ = true;
    th_ = std::thread([this, fn = std::move(fn)] {
      { std::lock_guard<std::mutex> g(m_); helperId_ = std::this_thread::get_id(); }   // recorded by the helper itself before fn runs (th_ may not be assigned yet)
      try { fn(); } catch (const Abort&) {}
      std::unique_lock<std::mutex> l(m_);
      running_ = false; suspended_ = false; helperTurn_ = false;
      cv_.notify_all();
    });
    waitForCaller();
  }
  // Resume a suspended helper; returns when it yields again or finishes.
  void resume() {
    if (!suspended_) return;
    {
      std::unique_lock<std::mutex> l(m_);
      suspended_ = false; helperTurn_ = true;
      cv_.notify_all();
    }
    waitForCaller();
    if (!running_) join();
  }
  // Called on the helper: hand control back to the caller until resume(). Does nothing on any other thread.
  void yield() {
    if (!onHelper()) return;
    std::unique_lock<std::mutex> l(m_);
    suspended_ = true; helperTurn_ = false;
    cv_.notify_all();
    cv_.wait(l, [&] { return helperTurn_ || aborted_; });
    if (aborted_) throw Abort();
  }
  bool suspended() const { return suspended_; }
  bool onHelper() const { std::lock_guard<std::mutex> g(m_); return running_ && std::this_thread::get_id() == helperId_; }

 private:
  struct Abort {};
  void waitForCaller() {
    std::unique_lock<std::mutex> l(m_);
    cv_.wait(l, [&] { return !helperTurn_; });
  }
  void join() { if (th_.joinable()) th_.join(); }
  void abort() {
    {
      std::unique_lock<std::mutex> l(m_);
      aborted_ = true; helperTurn_ = true;
      cv_.notify_all();
    }
    join();
  }
  std::thread th_;
  mutable std::mutex m_;
  std::thread::id helperId_;
  std::condition_variable cv_;
  bool helperTurn_ = false, suspended_ = false, running_ = false, aborted_ = false;
};

}  // namespace m3k
