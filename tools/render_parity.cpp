// CPU vs GPU terrain hash parity. Renders Berlin (or any city) at zooms 0–4 with objects off so
// the compare is pure terrain+background. Requires OpenGL 4.3 compute (same stack as metroplex3k).
//   render_parity --game DIR --city FILE [--size WxH]
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/game.h"
#include "core/render.h"
#include "gl_compute.h"

using namespace m3k;

static uint32_t fnv(const std::vector<uint32_t>& px) {
  uint32_t h = 2166136261u;
  for (uint32_t p : px) for (int i = 0; i < 4; i++) { h ^= (p >> (8 * i)) & 255; h *= 16777619u; }
  return h;
}

static size_t mismatchCount(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
  size_t n = 0;
  const size_t m = std::min(a.size(), b.size());
  for (size_t i = 0; i < m; i++) if (a[i] != b[i]) n++;
  n += a.size() > m ? a.size() - m : 0;
  n += b.size() > m ? b.size() - m : 0;
  return n;
}

int main(int argc, char** argv) {
  std::string game, city;
  int W = 1280, H = 720;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--game" && i + 1 < argc) game = argv[++i];
    else if (a == "--city" && i + 1 < argc) city = argv[++i];
    else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &W, &H);
  }
  if (game.empty() || city.empty()) {
    std::fprintf(stderr, "usage: render_parity --game DIR --city FILE [--size WxH]\n");
    return 1;
  }

  setenv("M3K_THREADS", "1", 1);

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  GlComputeTerrain gl;
  if (!gl.init()) {
    std::fprintf(stderr, "render_parity: OpenGL 4.3 compute unavailable\n");
    SDL_Quit();
    return 2;
  }

  Game gm;
  gm.setPersistence(false);
  std::string err;
  if (!gm.init(game, err)) { std::fprintf(stderr, "init: %s\n", err.c_str()); return 1; }
  gm.setIntroEnabled(false);
  Input in;
  gm.requestFile(city);
  for (int i = 0; i < 20 && !gm.world().W; i++) gm.frame(in, W, H, 16);
  if (!gm.world().W) { std::fprintf(stderr, "city did not load\n"); return 1; }

  const World& w = gm.world();
  GameData data;
  if (!data.open(game, err)) { std::fprintf(stderr, "data: %s\n", err.c_str()); return 1; }

  RenderOptions opt;
  opt.objects = false;  // sprites stay CPU; parity gate is terrain fill
  opt.edgeFaces = true;
  opt.grid = false;

  Renderer cpu;
  cpu.setWorld(&w, &data);
  cpu.resize(W, H);
  cpu.setBandCount(1);

  Renderer gpu;
  gpu.setWorld(&w, &data);
  gpu.resize(W, H);
  gpu.setBandCount(1);
  gpu.setGpuTerrainWanted(true);
  gpu.setGpuTerrainFn([&gl](uint32_t* fb, int ww, int hh, int zoom, const int8_t* nL, const int8_t* nE,
                            const GpuTerrainTri* tris, int n) {
    return gl.fill(fb, ww, hh, zoom, nL, nE, tris, n);
  });

  int fails = 0;
  std::printf("render_parity %dx%d city %dx%d (terrain only, M3K_THREADS=1)\n", W, H, w.W, w.H);
  for (int z = 0; z <= 4; z++) {
    Camera cam;
    cam.zoom = z;
    cpu.centreOnWorld(cam, w.W / 2.f, w.H / 2.f);
    Camera camG = cam;

    cpu.render(cam, opt);
    std::vector<uint32_t> cpuPx = cpu.pixels();
    const uint32_t hCpu = fnv(cpuPx);

    gpu.render(camG, opt);
    std::vector<uint32_t> gpuPx = gpu.pixels();
    const uint32_t hGpu = fnv(gpuPx);
    const size_t bad = mismatchCount(cpuPx, gpuPx);

    const char* ok = (hCpu == hGpu && bad == 0) ? "OK" : "FAIL";
    if (hCpu != hGpu || bad != 0) fails++;
    std::printf("zoom %d: cpu %08x  gpu %08x  mismatches %zu  %s\n", z, hCpu, hGpu, bad, ok);
  }

  SDL_Quit();
  return fails ? 1 : 0;
}
