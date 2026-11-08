// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Small persistent thread pool for data-parallel loops (renderer bands). Standard library only.
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace m3k {

class WorkerPool {
 public:
  // threads = total workers including the calling thread; <= 1 runs everything inline.
  explicit WorkerPool(int threads);
  ~WorkerPool();
  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  // Calls fn(i) for every i in [0, n), spread over the workers and the calling thread; returns when all
  // calls are done. Concurrent run() calls from different threads are serialised.
  void run(int n, const std::function<void(int)>& fn);
  int threads() const { return int(workers_.size()) + 1; }

  // Process-wide pool: $M3K_THREADS if set (1 = single-threaded), else the hardware thread count, capped at 16.
  static WorkerPool& shared();

 private:
  void workerLoop();
  void drain();
  std::vector<std::thread> workers_;
  std::mutex runMu_;  // one run() at a time
  std::mutex mu_;
  std::condition_variable wake_, done_;
  const std::function<void(int)>* fn_ = nullptr;
  int n_ = 0;
  std::atomic<int> next_{0};
  int busy_ = 0;          // workers still inside the current job
  uint64_t gen_ = 0;      // job counter; a change wakes the workers
  bool quit_ = false;
};

}  // namespace m3k
