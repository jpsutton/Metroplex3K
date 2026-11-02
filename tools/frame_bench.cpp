// Headless frame benchmark: times Renderer::render per zoom, Game::frame in a loaded city, and music
// decode. Prints an FNV-1a hash of each rendered frame so render changes can be checked for pixel identity.
//   frame_bench --game DIR --city FILE [--size WxH] [--frames N]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "core/game.h"
#include "core/render.h"
using namespace m3k;

using SteadyClock = std::chrono::steady_clock;
static double msSince(SteadyClock::time_point t) { return std::chrono::duration<double, std::milli>(SteadyClock::now() - t).count(); }
static uint32_t fnv(const std::vector<uint32_t>& px) {
  uint32_t h = 2166136261u;
  for (uint32_t p : px) for (int i = 0; i < 4; i++) { h ^= (p >> (8 * i)) & 255; h *= 16777619u; }
  return h;
}

int main(int argc, char** argv) {
  std::string game, city;
  int W = 2560, H = 1440, frames = 60;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--game" && i + 1 < argc) game = argv[++i];
    else if (a == "--city" && i + 1 < argc) city = argv[++i];
    else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &W, &H);
    else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
  }
  if (game.empty() || city.empty()) { std::fprintf(stderr, "usage: frame_bench --game DIR --city FILE [--size WxH] [--frames N]\n"); return 1; }

  Game gm; gm.setPersistence(false);
  std::string err;
  if (!gm.init(game, err)) { std::fprintf(stderr, "init: %s\n", err.c_str()); return 1; }
  gm.setIntroEnabled(false);
  Input in;
  gm.requestFile(city);
  for (int i = 0; i < 20 && !gm.world().W; i++) gm.frame(in, W, H, 16);
  if (!gm.world().W) { std::fprintf(stderr, "city did not load\n"); return 1; }
  const World& w = gm.world();
  std::printf("city %dx%d, %zu occupants, %dx%d, %d frames\n", w.W, w.H, w.occupants.size(), W, H, frames);

  // Renderer alone, every zoom, same data the game uses.
  GameData data;
  if (!data.open(game, err)) { std::fprintf(stderr, "data: %s\n", err.c_str()); return 1; }
  Renderer r; r.setWorld(&w, &data); r.resize(W, H);
  RenderOptions opt;
  for (int z = 0; z <= 4; z++) {
    Camera cam; cam.zoom = z; r.centreOnWorld(cam, w.W / 2.f, w.H / 2.f);
    r.render(cam, opt);  // warm sprite caches
    auto t = SteadyClock::now();
    for (int i = 0; i < frames; i++) r.render(cam, opt);
    std::printf("render zoom %d: %7.2f ms/frame  hash %08x\n", z, msSince(t) / frames, fnv(r.pixels()));
  }

  // Whole Game::frame (sim + street sim + render + UI), paused and running.
  for (int run = 0; run < 2; run++) {
    gm.setPaused(run == 0);
    gm.frame(in, W, H, 16);
    const uint64_t ras0 = gm.cityRasterFrames(), hit0 = gm.cityCacheHits();
    auto t = SteadyClock::now();
    for (int i = 0; i < frames; i++) gm.frame(in, W, H, 16);
    const uint64_t ras = gm.cityRasterFrames() - ras0, hit = gm.cityCacheHits() - hit0;
    std::printf("Game::frame %s: %7.2f ms/frame  city raster %llu / cache hit %llu\n",
                run ? "running" : "paused ", msSince(t) / frames,
                (unsigned long long)ras, (unsigned long long)hit);
  }

  // Music decode cost: what playSong does under the audio lock on a cache miss.
  if (AudioSystem* a = gm.audio()) {
    for (auto& m : a->ini().music) {
      if (!m.inSongList) continue;
      auto t = SteadyClock::now();
      bool ok = a->playSong(m.id);
      std::printf("playSong(%d) %s: %7.2f ms\n", m.id, ok ? "ok" : "failed", msSince(t));
      break;
    }
  }
  return 0;
}
