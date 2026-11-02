// Headless checks for the async sim (Game::setAsyncSim).
//   async_check --game DIR --city FILE --determinism DAYS
//       Runs the same city with the sim on the main thread and on the worker, stops both after the same number of
//       days and compares a hash of the whole simulated state. Exit 1 on mismatch.
//   async_check --game DIR --city FILE --stress SECONDS [--async 0|1] [--size WxH]
//       Plays a city at top speed for SECONDS of wall time with scripted input (mouse, camera, wheel, clicks, pause
//       and speed keys, window hotkeys), paced like a vsynced host, and prints frame-time statistics.
// Build with -fsanitize=thread to look for data races (see CMake option M3K_TSAN).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "core/clock.h"
#include "core/disasters.h"
#include "core/game.h"

using namespace m3k;
using Clk = std::chrono::steady_clock;

static uint64_t fnv(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }

static uint64_t stateHash(Game& g) {
  const World& w = g.world();
  uint64_t h = 1469598103934665603ull;
  for (const Occupant& o : w.occupants) {
    h = fnv(h, uint64_t(o.x)); h = fnv(h, uint64_t(o.z)); h = fnv(h, o.occId); h = fnv(h, o.state); h = fnv(h, o.agentType);
  }
  for (const NetPiece& p : w.surface) { h = fnv(h, uint64_t(p.x)); h = fnv(h, uint64_t(p.z)); h = fnv(h, p.pieceId); h = fnv(h, uint64_t(p.rot)); }
  for (uint8_t z : w.zone) h = fnv(h, z);
  for (uint8_t f : w.flora) h = fnv(h, f);
  City& c = g.city();
  h = fnv(h, uint64_t(c.budget.funds));
  h = fnv(h, uint64_t(c.population()));
  h = fnv(h, uint64_t(c.clock.jdn()));
  return h;
}

static bool loadCity(Game& gm, const std::string& game, const std::string& city, int W, int H) {
  gm.setPersistence(false);
  std::string err;
  if (!gm.init(game, err)) { std::fprintf(stderr, "init: %s\n", err.c_str()); return false; }
  gm.setIntroEnabled(false);
  Input in;
  gm.requestFile(city);
  for (int i = 0; i < 40 && !gm.world().W; i++) gm.frame(in, W, H, 16);
  return gm.world().W > 0;
}

static int determinism(const std::string& game, const std::string& city, int days) {
  const int W = 1280, H = 800;
  const uint32_t dt = uint32_t(86400000ull / SpeedControl::kFactor[4]) + 2;   // one sim day per frame at speed 4
  auto run = [&](bool async, int64_t stopAtJdn, int64_t* jdnOut, int64_t* startOut = nullptr) {
    Game gm;
    if (!loadCity(gm, game, city, W, H)) return uint64_t(0);
    gm.setAsyncSim(async);
    gm.setPaused(false);
    Input in;
    const int64_t start = gm.city().clock.jdn();
    int guard = 0;
    for (;;) {
      if (stopAtJdn >= 0) { if (gm.city().clock.jdn() >= stopAtJdn) break; }
      else if (gm.city().clock.jdn() >= start + days) break;
      gm.frame(in, W, H, dt);
      gm.syncSim();   // frames are instant here; let the worker finish so each frame advances one day
      if (++guard > days * 2 + 100) { std::fprintf(stderr, "no progress\n"); break; }
    }
    gm.syncSim();   // finish a job that was started after the last check
    if (jdnOut) *jdnOut = gm.city().clock.jdn();
    if (startOut) *startOut = start;
    return stateHash(gm);
  };
  int64_t asyncJdn = 0, syncJdn = 0, startJdn = 0;
  const uint64_t ha = run(true, -1, &asyncJdn, &startJdn);
  const uint64_t hs = run(false, asyncJdn, &syncJdn);
  std::printf("async: jdn %lld hash %016llx\nsync:  jdn %lld hash %016llx\n", (long long)asyncJdn, (unsigned long long)ha,
              (long long)syncJdn, (unsigned long long)hs);
  if (!ha || !hs || asyncJdn != syncJdn || ha != hs) { std::printf("MISMATCH\n"); return 1; }
  std::printf("OK: identical after %d days\n", int(asyncJdn - startJdn));
  return 0;
}

static int stress(const std::string& game, const std::string& city, double seconds, bool async, bool calm, bool disasters, bool screens, int dtMs, int W, int H) {
  Game gm;
  if (!loadCity(gm, game, city, W, H)) return 1;
  gm.setAsyncSim(async);
  gm.setPaused(false);
  std::mt19937 rng(12345);
  auto rnd = [&](int n) { return int(rng() % uint32_t(n)); };
  Input in;
  in.mx = W / 2; in.my = H / 2;
  std::vector<double> frameMs, waitMs, endMs, simMs, rasterMs, gameMs;
  uint64_t days = 0, joins = 0;
  int heldFor = 0, pendingRelease = 0, escIn = 0, screensStep = 0, screensWait = 0;
  const double period = 13.33;
  auto next = Clk::now();
  const auto t0 = Clk::now();
  while (std::chrono::duration<double>(Clk::now() - t0).count() < seconds) {
    in.pressed = in.released = in.rightPressed = false;
    in.wheel = 0; in.keyDowns.clear(); in.text.clear();
    for (bool& k : in.key) k = false;
    in.mx = std::clamp(in.mx + rnd(9) - 4, 0, W - 1);
    in.my = std::clamp(in.my + rnd(9) - 4, 0, H - 1);
    if (heldFor > 0) heldFor--; else { for (bool& k : in.held) k = false; if (rnd(60) == 0) { in.held[rnd(4)] = true; heldFor = 10 + rnd(30); } }
    if (rnd(40) == 0) in.wheel = rnd(2) ? 1 : -1;
    if (calm) { /* watching only: no clicks, keys, pause or window hotkeys */ }
    else if (pendingRelease > 0 && --pendingRelease == 0) { in.released = true; in.down = false; }
    else if (rnd(120) == 0) { in.pressed = true; in.down = true; pendingRelease = 2; in.mx = W / 4 + rnd(W / 2); in.my = H / 4 + rnd(H / 2); }
    if (!calm && rnd(400) == 0) in.keyDowns.push_back({191, 0});    // pause / run
    if (!calm && rnd(300) == 0) in.keyDowns.push_back({rnd(2) ? 188 : 190, 0});   // slower / faster
    if (calm) {}
    else if (escIn > 0 && --escIn == 0) in.key[K_ESC] = true;
    else if (rnd(500) == 0) { in.keyDowns.push_back({'B', 2}); escIn = 20 + rnd(20); }   // Ctrl+B: budget window
    // Leave the city with Esc and come back with a click on "Back to city" (1280x800 layout), like a player. The click
    // flips the screen in the middle of a frame, so that frame ends with the pending-screen latch still raised.
    if (screens) {
      if (screensStep == 0 && rnd(200) == 0) { in.key[K_ESC] = true; screensStep = 1; screensWait = 3 + rnd(6); }
      else if (screensStep == 1 && --screensWait <= 0) { in.mx = 637; in.my = 605; in.pressed = true; in.released = true; in.down = false; screensStep = 0; }   // a fast click: both edges in one frame, like xdotool
    }
    if (disasters && rnd(150) == 0) {
      static const DisasterType kinds[] = {DisasterType::Fire, DisasterType::UFO, DisasterType::Tornado, DisasterType::Earthquake, DisasterType::Locusts};
      gm.startDisaster(kinds[rnd(5)]);
    }
    gm.frame(in, W, H, uint32_t(dtMs));
    const Game::FramePhases& p = gm.lastFramePhases();
    const double ms = std::chrono::duration<double, std::milli>(Clk::now() - (next - std::chrono::microseconds(int(period * 1000)))).count();
    (void)ms;
    frameMs.push_back(p.simMs + p.streetMs + p.rasterMs + p.simWaitMs + p.asyncEndMs);   // sim-related share, refined below
    waitMs.push_back(p.simWaitMs);
    endMs.push_back(p.asyncEndMs);
    simMs.push_back(p.simMs);
    if (p.rastered) rasterMs.push_back(p.rasterMs);
    if (p.simWaitMs > 1.0) joins++;
    if (p.simDay) days++;
    next += std::chrono::microseconds(int(period * 1000));
    std::this_thread::sleep_until(next);
  }
  gm.syncSim();
  auto pct = [](std::vector<double> v, double q) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[std::min(v.size() - 1, size_t(q * double(v.size())))]; };
  auto mx = [](const std::vector<double>& v) { return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end()); };
  std::printf("async=%d frames=%zu days=%llu (%.1f/s) joins>1ms=%llu\n", async ? 1 : 0, frameMs.size(), (unsigned long long)days,
              double(days) / seconds, (unsigned long long)joins);
  std::printf("sim_wait_ms  p50 %.2f p95 %.2f p99 %.2f max %.2f\n", pct(waitMs, .5), pct(waitMs, .95), pct(waitMs, .99), mx(waitMs));
  std::printf("sim_ms       p50 %.2f p95 %.2f p99 %.2f max %.2f\n", pct(simMs, .5), pct(simMs, .95), pct(simMs, .99), mx(simMs));
  std::printf("async_end_ms p50 %.2f p95 %.2f p99 %.2f max %.2f\n", pct(endMs, .5), pct(endMs, .95), pct(endMs, .99), mx(endMs));
  std::printf("raster_ms    p50 %.2f p95 %.2f p99 %.2f max %.2f  (%zu rastered frames)\n", pct(rasterMs, .5), pct(rasterMs, .95), pct(rasterMs, .99), mx(rasterMs), rasterMs.size());
  std::printf("sim-related frame share (sim+street+raster+wait+end) p50 %.2f p95 %.2f p99 %.2f max %.2f\n", pct(frameMs, .5),
              pct(frameMs, .95), pct(frameMs, .99), mx(frameMs));
  return 0;
}

int main(int argc, char** argv) {
  std::string game, city;
  int days = 0, W = 1280, H = 800;
  double seconds = 0;
  bool async = true, calm = false, disasters = false, screens = false;
  int dtMs = 13;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--game" && i + 1 < argc) game = argv[++i];
    else if (a == "--city" && i + 1 < argc) city = argv[++i];
    else if (a == "--determinism" && i + 1 < argc) days = std::atoi(argv[++i]);
    else if (a == "--stress" && i + 1 < argc) seconds = std::atof(argv[++i]);
    else if (a == "--calm") calm = true;
    else if (a == "--disasters") disasters = true;
    else if (a == "--screens") screens = true;
    else if (a == "--dt" && i + 1 < argc) dtMs = std::atoi(argv[++i]);
    else if (a == "--async" && i + 1 < argc) async = std::atoi(argv[++i]) != 0;
    else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &W, &H);
  }
  if (game.empty() || city.empty() || (days <= 0 && seconds <= 0)) {
    std::fprintf(stderr, "usage: async_check --game DIR --city FILE (--determinism DAYS | --stress SECONDS [--async 0|1] [--size WxH])\n");
    return 2;
  }
  return days > 0 ? determinism(game, city, days) : stress(game, city, seconds, async, calm, disasters, screens, dtMs, W, H);
}
