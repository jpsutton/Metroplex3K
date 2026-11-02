// Per-frame timing log for the desktop host. Enabled with $M3K_FRAMELOG=FILE or --frame-log FILE.
// Writes one CSV row per loop iteration (microsecond-resolution timings) and prints a percentile
// summary to stderr on exit, so frame-rate questions can be answered from data.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/game.h"

namespace m3k {

class FrameLog {
 public:
  using Clock = std::chrono::steady_clock;

  // One loop iteration, timed by the host. All values in milliseconds.
  struct Sample {
    double loopMs = 0;      // whole duration of this iteration, present/vsync wait and delay included (FPS = 1000 / this)
    double eventsMs = 0;    // SDL event pump + input state
    double gameMs = 0;      // Game::frame (incl. locking the target, excluding present)
    double presentMs = 0;   // presenter present / vsync wait
    double delayMs = 0;     // SDL_Delay when vsync is off
    int w = 0, h = 0;
  };

  ~FrameLog() { close(); }

  bool open(const std::string& path, const std::string& backend) {
    f_ = std::fopen(path.c_str(), "w");
    if (!f_) return false;
    start_ = Clock::now();
    std::fprintf(f_, "# metroplex3k frame log, render=%s\n", backend.c_str());
    std::fprintf(f_, "frame,t_ms,loop_ms,events_ms,game_ms,sim_ms,street_ms,raster_ms,ui_ms,present_ms,delay_ms,"
                     "rastered,sim_day,paused,speed,zoom,w,h\n");
    return true;
  }
  bool active() const { return f_ != nullptr; }

  void add(const Sample& s, const Game::FramePhases& p) {
    if (!f_) return;
    const double t = std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    const double ui = std::max(0.0, s.gameMs - p.simMs - p.streetMs - p.rasterMs);   // HUD, windows, blits, fps overlay
    std::fprintf(f_, "%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d\n",
                 static_cast<unsigned long long>(frames_), t, s.loopMs, s.eventsMs, s.gameMs, p.simMs, p.streetMs,
                 p.rasterMs, ui, s.presentMs, s.delayMs, p.rastered ? 1 : 0, p.simDay ? 1 : 0, p.paused ? 1 : 0,
                 p.speed, p.zoom, s.w, s.h);
    if (++frames_ % 120 == 0) std::fflush(f_);
    loop_.push_back(s.loopMs);
    if (!p.paused && p.rastered) running_.push_back(s.loopMs);
  }

  void close() {
    if (!f_) return;
    std::fclose(f_);
    f_ = nullptr;
    summarize("all frames", loop_);
    summarize("rastered, sim running", running_);
  }

 private:
  static void summarize(const char* label, std::vector<double> v) {
    if (v.empty()) return;
    std::sort(v.begin(), v.end());
    double sum = 0;
    size_t slow33 = 0, slow50 = 0;
    for (double x : v) { sum += x; slow33 += x > 33.4; slow50 += x > 50.0; }
    auto pct = [&](double q) { return v[std::min(v.size() - 1, size_t(q * double(v.size())))]; };
    const double mean = sum / double(v.size());
    std::fprintf(stderr,
                 "frame log [%s]: %zu frames, mean %.2f ms (%.1f FPS), p50 %.2f, p95 %.2f, p99 %.2f, max %.2f ms; "
                 "1%% low %.1f FPS; >33 ms: %zu, >50 ms: %zu\n",
                 label, v.size(), mean, 1000.0 / mean, pct(0.50), pct(0.95), pct(0.99), v.back(),
                 1000.0 / std::max(1e-6, pct(0.99)), slow33, slow50);
  }

  std::FILE* f_ = nullptr;
  Clock::time_point start_;
  uint64_t frames_ = 0;
  std::vector<double> loop_, running_;
};

}  // namespace m3k
