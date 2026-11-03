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
#include "core/render.h"

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

// Terrain layer cache on vs off must give identical pixels, including right after world edits.
static int terrainCacheParity(const std::string& game, const std::string& city) {
  Game gm;
  if (!loadCity(gm, game, city, 1280, 800)) return 1;
  World w = gm.world();   // private copy we are free to edit
  GameData data; std::string err;
  if (!data.open(game, err)) { std::fprintf(stderr, "data: %s\n", err.c_str()); return 1; }
  Renderer off, on;
  for (Renderer* r : {&off, &on}) { r->setWorld(&w, &data); r->resize(1280, 800); }
  on.setTerrainLayerCache(true);
  on.setItemListCache(true);
  int mismatches = 0, renders = 0, hits = 0;
  auto compare = [&](const Camera& cam, const RenderOptions& opt, const char* what) {
    off.render(cam, opt); on.render(cam, opt);
    renders++; hits += on.lastStageTimes().terrainLayerHit ? 1 : 0;
    if (off.pixels() != on.pixels()) { mismatches++; std::printf("MISMATCH: %s (zoom %d rot %d)\n", what, cam.zoom, cam.rot); }
  };
  RenderOptions opt;
  for (int z = 0; z <= 4; z++)
    for (int rot = 0; rot < 4; rot++) {
      Camera cam; cam.zoom = z; cam.rot = rot; cam.u = float(w.W) / 2 + float(rot) * 3; cam.v = float(w.H) / 2;
      for (int tick = 0; tick < 3; tick++) { opt.animTick = tick; compare(cam, opt, "repeat with animation tick"); }
    }
  Camera cam; cam.zoom = 2; cam.u = float(w.W) / 2; cam.v = float(w.H) / 2;
  opt.animTick = 0;
  compare(cam, opt, "baseline");
  opt.grid = !opt.grid; compare(cam, opt, "grid toggled"); opt.grid = !opt.grid; compare(cam, opt, "grid restored");
  opt.zonePaint = false; compare(cam, opt, "zonePaint off"); opt.zonePaint = true;
  opt.edgeFaces = false; compare(cam, opt, "edgeFaces off"); opt.edgeFaces = true;
  opt.background = false; compare(cam, opt, "background off"); opt.background = true;
  opt.underground = true; compare(cam, opt, "underground"); opt.underground = false;
  opt.objects = false; compare(cam, opt, "objects off"); opt.objects = true;
  cam.u += 0.25f; compare(cam, opt, "camera nudge");
  // World edits made behind the renderer's back must show up on the very next render.
  for (int x = 100; x < 130; x++) for (int z = 100; z < 130; z++) w.zone[size_t(w.tIdx(x, z))] = 3;
  compare(cam, opt, "zone edit");
  for (int x = 100; x < 110; x++) for (int z = 100; z < 110; z++) w.alt[size_t(w.vIdx(x, z))] = uint8_t(w.alt[size_t(w.vIdx(x, z))] + 4);
  off.invalidateShade(); on.invalidateShade();
  compare(cam, opt, "altitude edit");
  off.setTileTint(120, 120, 7); on.setTileTint(120, 120, 7); compare(cam, opt, "tile tint");
  off.clearTileTints(); on.clearTileTints(); compare(cam, opt, "tint cleared");
  DataOverlay ov; ov.slot = 3; ov.row.assign(size_t(w.W) * w.H, 8);
  opt.overlay = &ov; compare(cam, opt, "overlay"); ov.row[size_t(w.tIdx(120, 120))] = 20; compare(cam, opt, "overlay edit"); opt.overlay = nullptr;
  compare(cam, opt, "overlay removed");
  // Pans: random walks over the map with small and large steps, every zoom and rotation. Small steps take the shift
  // path (cached pixels moved, exposed strips redrawn); large ones fall back to a full redraw.
  int shifts = 0;
  std::mt19937 prng(99);
  opt = RenderOptions();
  for (int z = 0; z <= 4; z++)
    for (int rot = 0; rot < 4; rot++) {
      Camera cam; cam.zoom = z; cam.rot = rot; cam.u = float(w.W) / 2; cam.v = float(w.H) / 2;
      for (int step = 0; step < 60; step++) {
        const float mag = (step % 10 == 9) ? 30.f : 0.6f;   // every tenth step is a long jump
        cam.u = std::clamp(cam.u + (float(prng() % 2001) / 1000.f - 1.f) * mag, 20.f, float(w.W) - 20.f);
        cam.v = std::clamp(cam.v + (float(prng() % 2001) / 1000.f - 1.f) * mag, 20.f, float(w.H) - 20.f);
        opt.animTick = step;
        compare(cam, opt, "pan");
        if (on.lastStageTimes().terrainLayerShift) shifts++;
      }
    }
  std::printf("terrain cache parity: %d renders, %d served from cache, %d via pan shift, %d mismatches\n", renders, hits, shifts, mismatches);
  return mismatches ? 1 : 0;
}

// Two games run in lockstep on identical input: one with the render caches off, one with them on. Every frame must
// match pixel for pixel. The scripted input pans, zooms, clicks, arms the zone tool and paints, and opens windows.
static int cacheParityGame(const std::string& game, const std::string& city, int frames, int W, int H) {
  Game a, b;
  if (!loadCity(a, game, city, W, H) || !loadCity(b, game, city, W, H)) return 1;
  b.renderer().setTerrainLayerCache(true);
  b.renderer().setItemListCache(true);
  a.setPaused(false); b.setPaused(false);
  std::mt19937 rng(777);
  auto rnd = [&](int n) { return int(rng() % uint32_t(n)); };
  Input in; in.mx = W / 2; in.my = H / 2;
  int heldFor = 0, mismatches = 0, pendingRelease = 0, escIn = 0;
  uint64_t terrainHits = 0;
  for (int f = 0; f < frames; f++) {
    in.pressed = in.released = in.rightPressed = false; in.wheel = 0; in.keyDowns.clear(); in.text.clear();
    for (bool& k : in.key) k = false;
    in.mx = std::clamp(in.mx + rnd(9) - 4, 0, W - 1); in.my = std::clamp(in.my + rnd(9) - 4, 0, H - 1);
    if (heldFor > 0) heldFor--; else { for (bool& k : in.held) k = false; if (rnd(60) == 0) { in.held[rnd(4)] = true; heldFor = 10 + rnd(30); } }
    if (rnd(80) == 0) in.wheel = rnd(2) ? 1 : -1;
    if (pendingRelease > 0 && --pendingRelease == 0) { in.released = true; in.down = false; }
    else if (rnd(90) == 0) { in.pressed = true; in.down = true; pendingRelease = 2 + rnd(3); in.mx = W / 4 + rnd(W / 2); in.my = H / 4 + rnd(H / 2); }
    if (rnd(150) == 0) in.keyDowns.push_back({'A', 0});       // zone tool: following drags paint zones
    if (rnd(200) == 0) in.rightPressed = true;                  // cancel tool
    if (rnd(500) == 0) in.keyDowns.push_back({191, 0});         // pause / run
    if (escIn > 0 && --escIn == 0) in.key[K_ESC] = true;
    else if (rnd(400) == 0) { in.keyDowns.push_back({'B', 2}); escIn = 15; }
    a.frame(in, W, H, 13); b.frame(in, W, H, 13);
    terrainHits += b.renderer().lastStageTimes().terrainLayerHit ? 1 : 0;
    if (a.screen() != b.screen()) {
      if (++mismatches <= 5) std::printf("MISMATCH at frame %d\n", f);
    }
  }
  std::printf("game cache parity: %d frames, terrain layer served %llu, %d mismatches\n", frames, (unsigned long long)terrainHits, mismatches);
  return mismatches ? 1 : 0;
}

// Is a terrain render at camera B equal to the render at camera A shifted by the origin difference? (Pixel-exact
// translation is what a scrolling terrain cache would rely on.)
static int translationProbe(const std::string& game, const std::string& city) {
  Game gm;
  if (!loadCity(gm, game, city, 1280, 800)) return 1;
  World w = gm.world();
  GameData data; std::string err;
  if (!data.open(game, err)) return 1;
  Renderer r; r.setWorld(&w, &data);
  const int W = 1280, H = 800;
  r.resize(W, H);
  RenderOptions opt; opt.objects = false;
  std::mt19937 rng(5);
  long total = 0, bad = 0; int pairs = 0, badPairs = 0;
  for (int z = 0; z <= 4; z++)
    for (int rot = 0; rot < 4; rot++)
      for (int trial = 0; trial < 12; trial++) {
        Camera a; a.zoom = z; a.rot = rot; a.u = 40.f + float(rng() % 1500) / 10.f; a.v = 40.f + float(rng() % 1500) / 10.f;
        Camera b = a; b.u += (float(rng() % 600) - 300.f) / 100.f; b.v += (float(rng() % 600) - 300.f) / 100.f;
        r.render(a, opt); const std::vector<uint32_t> A = r.pixels();
        r.render(b, opt); const std::vector<uint32_t>& B = r.pixels();
        // origins: recompute through the public projector (screen position of a fixed world point)
        int ax, ay, bx, by;
        r.projectSub(a, 100 * 256, 100 * 256, 0, ax, ay);
        r.projectSub(b, 100 * 256, 100 * 256, 0, bx, by);
        const int dx = bx - ax, dy = by - ay;   // B(x + dx, y + dy) should equal A(x, y)
        if (std::abs(dx) >= W || std::abs(dy) >= H) continue;
        long bp = 0, tp = 0;
        for (int y = std::max(0, -dy); y < std::min(H, H - dy); y++)
          for (int x = std::max(0, -dx); x < std::min(W, W - dx); x++) {
            tp++;
            if (A[size_t(y) * W + x] != B[size_t(y + dy) * W + x + dx]) bp++;
          }
        pairs++; total += tp; bad += bp; if (bp) badPairs++;
      }
  std::printf("translation probe: %d camera pairs, %ld pixels compared, %ld differ (%d pairs with any difference)\n", pairs, total, bad, badPairs);
  return bad ? 1 : 0;
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

static int stress(const std::string& game, const std::string& city, double seconds, bool async, bool calm, bool disasters, bool screens, bool terrainCache, bool pan, int dtMs, int W, int H) {
  Game gm;
  if (!loadCity(gm, game, city, W, H)) return 1;
  gm.setAsyncSim(async);
  gm.renderer().setTerrainLayerCache(terrainCache);
  gm.renderer().setItemListCache(terrainCache);
  gm.setPaused(false);
  std::mt19937 rng(12345);
  uint64_t layerHits = 0, layerShifts = 0, totalFrames = 0;
  auto rnd = [&](int n) { return int(rng() % uint32_t(n)); };
  Input in;
  in.mx = W / 2; in.my = H / 2;
  std::vector<double> frameMs, waitMs, endMs, simMs, rasterMs, gameMs, stSetup, stBands, stBg, stTerrain, stItems, stTail, hitRaster, hitSetup, hitBands, missRaster, hitKey, hitBuild;
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
    if (pan) { for (bool& k : in.held) k = false; in.held[(totalFrames / 90) % 4] = true; heldFor = 0; }   // steady scrolling, direction changes every 90 frames
    if (!pan && rnd(40) == 0) in.wheel = rnd(2) ? 1 : -1;
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
    gm.frame(in, W, H, uint32_t(dtMs)); totalFrames++;
    const Game::FramePhases& p = gm.lastFramePhases();
    const double ms = std::chrono::duration<double, std::milli>(Clk::now() - (next - std::chrono::microseconds(int(period * 1000)))).count();
    (void)ms;
    frameMs.push_back(p.simMs + p.streetMs + p.rasterMs + p.simWaitMs + p.asyncEndMs);   // sim-related share, refined below
    waitMs.push_back(p.simWaitMs);
    endMs.push_back(p.asyncEndMs);
    simMs.push_back(p.simMs);
    if (p.rastered) {
      rasterMs.push_back(p.rasterMs);
      const Renderer::StageTimes& st = gm.renderer().lastStageTimes();
      layerHits += st.terrainLayerHit ? 1 : 0; layerShifts += st.terrainLayerShift ? 1 : 0;
      if (st.terrainLayerHit) { hitRaster.push_back(p.rasterMs); hitSetup.push_back(st.setupMs); hitBands.push_back(st.bandsWallMs); hitKey.push_back(st.layerKeyMs); hitBuild.push_back(st.buildItemsMs); } else missRaster.push_back(p.rasterMs);
      stSetup.push_back(st.setupMs); stBands.push_back(st.bandsWallMs); stBg.push_back(st.bgCpuMs);
      stTerrain.push_back(st.terrainCpuMs); stItems.push_back(st.itemsCpuMs); stTail.push_back(st.tailMs);
    }
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
  std::printf("terrain layer cache: %s, served %llu, pan-shifted %llu, of %zu rastered frames\n", terrainCache ? "on" : "off", (unsigned long long)layerHits, (unsigned long long)layerShifts, rasterMs.size());
  if (terrainCache) std::printf("cache hit frames: raster p50 %.2f p95 %.2f (setup %.2f [key hash %.2f, buildItems %.2f] + bands %.2f); miss frames: raster p50 %.2f p95 %.2f\n", pct(hitRaster, .5), pct(hitRaster, .95), pct(hitSetup, .5), pct(hitKey, .5), pct(hitBuild, .5), pct(hitBands, .5), pct(missRaster, .5), pct(missRaster, .95));
  std::printf("raster_ms    p50 %.2f p95 %.2f p99 %.2f max %.2f  (%zu rastered frames)\n", pct(rasterMs, .5), pct(rasterMs, .95), pct(rasterMs, .99), mx(rasterMs), rasterMs.size());
  std::printf("stages p50: setup(serial) %.2f  bands(wall) %.2f  | band cpu-time: bg %.2f terrain %.2f items(sprites) %.2f  | tail(serial) %.2f ms\n",
              pct(stSetup, .5), pct(stBands, .5), pct(stBg, .5), pct(stTerrain, .5), pct(stItems, .5), pct(stTail, .5));
  std::printf("stages p95: setup(serial) %.2f  bands(wall) %.2f  | band cpu-time: bg %.2f terrain %.2f items(sprites) %.2f  | tail(serial) %.2f ms\n",
              pct(stSetup, .95), pct(stBands, .95), pct(stBg, .95), pct(stTerrain, .95), pct(stItems, .95), pct(stTail, .95));
  std::printf("sim-related frame share (sim+street+raster+wait+end) p50 %.2f p95 %.2f p99 %.2f max %.2f\n", pct(frameMs, .5),
              pct(frameMs, .95), pct(frameMs, .99), mx(frameMs));
  return 0;
}

int main(int argc, char** argv) {
  std::string game, city;
  int days = 0, W = 1280, H = 800;
  bool cacheParity = false, terrainCache = false;
  int gameParityFrames = 0;
  bool probe = false, pan = false;
  double seconds = 0;
  bool async = true, calm = false, disasters = false, screens = false;
  int dtMs = 13;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--game" && i + 1 < argc) game = argv[++i];
    else if (a == "--city" && i + 1 < argc) city = argv[++i];
    else if (a == "--determinism" && i + 1 < argc) days = std::atoi(argv[++i]);
    else if (a == "--stress" && i + 1 < argc) seconds = std::atof(argv[++i]);
    else if (a == "--terrain-cache-parity") cacheParity = true;
    else if (a == "--terrain-cache") terrainCache = true;
    else if (a == "--cache-parity-game" && i + 1 < argc) gameParityFrames = std::atoi(argv[++i]);
    else if (a == "--translation-probe") probe = true;
    else if (a == "--pan") pan = true;
    else if (a == "--calm") calm = true;
    else if (a == "--disasters") disasters = true;
    else if (a == "--screens") screens = true;
    else if (a == "--dt" && i + 1 < argc) dtMs = std::atoi(argv[++i]);
    else if (a == "--async" && i + 1 < argc) async = std::atoi(argv[++i]) != 0;
    else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &W, &H);
  }
  if (!game.empty() && !city.empty() && probe) return translationProbe(game, city);
  if (!game.empty() && !city.empty() && cacheParity) return terrainCacheParity(game, city);
  if (!game.empty() && !city.empty() && gameParityFrames > 0) return cacheParityGame(game, city, gameParityFrames, W, H);
  if (game.empty() || city.empty() || (days <= 0 && seconds <= 0)) {
    std::fprintf(stderr, "usage: async_check --game DIR --city FILE (--terrain-cache-parity | --cache-parity-game FRAMES | --determinism DAYS | --stress SECONDS [--async 0|1] [--size WxH])\n");
    return 2;
  }
  return days > 0 ? determinism(game, city, days) : stress(game, city, seconds, async, calm, disasters, screens, terrainCache, pan, dtMs, W, H);
}
