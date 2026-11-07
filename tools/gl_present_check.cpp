// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Compares the GL presenter against the CPU renderer. Two games run in lockstep on identical scripted input (pan,
// zoom, clicks, zone painting, windows): A draws on the CPU (reference); B draws through GlPresenter, with the city
// composited on the GPU when the presenter supports it. After every frame B's presenter output is read back and
// compared with A's screen.
//   gl_present_check --game DIR --city FILE [--frames N] [--size WxH] [--cityonly] [--basic]
// --cityonly hides the toolbars (F12) so the frame is almost all city: must match exactly. Without it the UI is
// composited over the GPU city; opaque UI is exact, blended UI over the city may differ by rounding.
// --basic keeps the GPU city off (stage 1 present only).
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include "core/game.h"
#include "gl_presenter.h"
using namespace m3k;

// Random sprites with alpha planes, partial levels, 2x magnification, moving and static, over the real city.
// CPU Renderer::render (caches off) vs renderForGpu + the compute compositor; must be identical.
static int synthetic(const std::string& game, const std::string& city, int W, int H, GlPresenter& gl) {
  Game gm; gm.setPersistence(false);
  std::string err;
  if (!gm.init(game, err)) return 1;
  gm.setIntroEnabled(false);
  Input in; gm.requestFile(city);
  for (int i = 0; i < 40 && !gm.world().W; i++) gm.frame(in, W, H, 16);
  World w = gm.world();
  GameData data;
  if (!data.open(game, err)) return 1;
  std::mt19937 rng(4242);
  auto rnd = [&](int n) { return int(rng() % uint32_t(n)); };
  struct Syn { SpriteImage spr; Plane plane; };
  std::vector<std::unique_ptr<Syn>> syn;
  for (int k = 0; k < 60; k++) {
    auto s = std::make_unique<Syn>();
    const int sw = 4 + rnd(70), sh = 4 + rnd(70);
    s->spr.img.w = sw; s->spr.img.h = sh; s->spr.img.px.resize(size_t(sw) * sh);
    for (auto& p : s->spr.img.px) p = rnd(5) == 0 ? 0u : (uint32_t(rnd(256)) | uint32_t(rnd(256)) << 8 | uint32_t(rnd(256)) << 16 | 0xFF000000u);
    s->spr.L = rnd(sw); s->spr.T = rnd(sh); s->spr.R = sw - s->spr.L; s->spr.B = sh - s->spr.T; s->spr.ok = true;
    s->plane.w = sw; s->plane.h = sh; s->plane.v.resize(size_t(sw) * sh);
    for (auto& v : s->plane.v) v = uint8_t(rnd(32));
    syn.push_back(std::move(s));
  }
  auto isSyn = [](uint32_t cat) { return cat >= 0xE0000000u; };
  Renderer cpu, gpu;
  for (Renderer* r : {&cpu, &gpu}) {
    r->setWorld(&w, &data);
    r->resize(W, H);
    r->setSpriteLookup(
        [&](uint32_t c, uint32_t i) -> const SpriteImage* { return isSyn(c) ? &syn[i % syn.size()]->spr : data.sprite(c, i); },
        [&](uint32_t c, uint32_t i) -> const Plane* { return isSyn(c) ? &syn[i % syn.size()]->plane : data.spritePlane(c, i); });
  }
  gpu.setTerrainLayerCache(true); gpu.setItemListCache(true);
  int bad = 0, frames = 0;
  long blended = 0, planes = 0, items = 0;
  std::vector<uint32_t> back;
  for (int z = 0; z <= 4; z++)
    for (int rot = 0; rot < 4; rot++) {
      Camera cam; cam.zoom = z; cam.rot = rot; cam.u = float(w.W) / 2; cam.v = float(w.H) / 2;
      for (int step = 0; step < 6; step++) {
        cam.u += 0.7f; cam.v -= 0.4f;
        RenderScene sc;
        const int n = 800;
        for (int k = 0; k < n; k++) {
          SceneSprite ss;
          ss.category = 0xE0000000u; ss.spriteId = uint32_t(rnd(60));
          const int tx = int(cam.u) - 25 + rnd(50), tz = int(cam.v) - 25 + rnd(50);
          ss.x = (tx < 0 ? 0 : tx) * 256 + rnd(256); ss.z = (tz < 0 ? 0 : tz) * 256 + rnd(256); ss.height = rnd(4) * 256;
          ss.magnify2x = rnd(4) == 0;
          ss.hasAlpha = rnd(3) != 0;
          ss.alphaCategory = 0xE0000000u; ss.alphaId = ss.spriteId;
          ss.globalLevel = rnd(3) == 0 ? 32 : rnd(33);
          ss.moving = rnd(2) == 0;
          ss.footprint = 1 + rnd(2);
          sc.sprites.push_back(ss);
        }
        RenderOptions opt;
        opt.animTick = step;
        cpu.render(cam, opt, &sc);
        GpuCityFrame gf;
        if (!gpu.renderForGpu(cam, opt, &sc, gf)) { std::printf("renderForGpu refused\n"); return 1; }
        FrameBuffer fb;
        if (!gl.beginFrame(W, H, fb)) return 1;
        std::fill(fb.pixels, fb.pixels + size_t(W) * H, 0u);
        if (!gl.submitCity(gf)) { std::printf("submitCity refused\n"); return 1; }
        gl.setCityVisible(true);
        gl.endFrame();
        gl.readback(back);
        frames++;
        blended += gl.stats().blendItems; planes += gl.stats().planeItems; items += gl.stats().items;
        const std::vector<uint32_t>& ref = cpu.pixels();
        long diff = 0;
        for (size_t i = 0; i < back.size(); i++) if ((back[i] | 0xFF000000u) != (ref[i] | 0xFF000000u)) diff++;
        if (diff) { bad++; if (bad <= 5) std::printf("zoom %d rot %d step %d: %ld pixels differ\n", z, rot, step, diff); }
      }
    }
  std::printf("synthetic: %d frames, %d differ; items %ld, blended %ld, with alpha plane %ld\n", frames, bad, items, blended, planes);
  return bad ? 1 : 0;
}

int main(int argc, char** argv) {
  std::string game, city;
  int frames = 600, W = 1280, H = 800;
  bool cityOnly = false, basic = false, syntheticMode = false, fps = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--game" && i + 1 < argc) game = argv[++i];
    else if (a == "--city" && i + 1 < argc) city = argv[++i];
    else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &W, &H);
    else if (a == "--cityonly") cityOnly = true;
    else if (a == "--basic") basic = true;
    else if (a == "--synthetic") syntheticMode = true;
    else if (a == "--fps") fps = true;
  }
  if (game.empty() || city.empty()) { std::fprintf(stderr, "usage: gl_present_check --game DIR --city FILE\n"); return 2; }
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
  SDL_Window* win = SDL_CreateWindow("check", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  GlPresenter gl;
  if (!win || !gl.init(win)) { std::fprintf(stderr, "no GL context\n"); return 3; }
  gl.setGpuCityEnabled(!basic);
  if (syntheticMode) return synthetic(game, city, W, H, gl);
  std::printf("GPU city: %s\n", gl.hasGpuCity() && !basic ? "on" : "off");
  Game a, b;
  std::string err;
  for (Game* g : {&a, &b}) {
    g->setPersistence(false);
    if (!g->init(game, err)) return 1;
    g->setIntroEnabled(false);
  }
  if (fps) { a.mutableEngineOptions().showFps = true; b.mutableEngineOptions().showFps = true; }
  b.renderer().setTerrainLayerCache(true);
  b.renderer().setItemListCache(true);
  Input in;
  for (Game* g : {&a, &b}) {
    g->requestFile(city);
    for (int i = 0; i < 40 && !g->world().W; i++) g->frame(in, W, H, 16);
    g->setPaused(false);
  }
  std::mt19937 rng(321);
  auto rnd = [&](int n) { return int(rng() % uint32_t(n)); };
  in.mx = W / 2; in.my = H / 2;
  int heldFor = 0, pendingRelease = 0, escIn = 0, badFrames = 0, gpuFrames = 0;
  long badPixels = 0, totalPixels = 0;
  int worstDiff = 0;
  long blendItemsSeen = 0, planeItemsSeen = 0, itemsSeen = 0;
  std::vector<uint32_t> back;
  for (int f = 0; f < frames; f++) {
    in.pressed = in.released = in.rightPressed = false; in.wheel = 0; in.keyDowns.clear(); in.text.clear();
    for (bool& k : in.key) k = false;
    if (cityOnly && f == 3) in.keyDowns.push_back({123, 0});   // F12: hide the toolbars
    in.mx = std::clamp(in.mx + rnd(9) - 4, 0, W - 1); in.my = std::clamp(in.my + rnd(9) - 4, 0, H - 1);
    if (heldFor > 0) heldFor--; else { for (bool& k : in.held) k = false; if (rnd(50) == 0) { in.held[rnd(4)] = true; heldFor = 10 + rnd(30); } }
    if (rnd(80) == 0) in.wheel = rnd(2) ? 1 : -1;
    if (!cityOnly) {
      if (pendingRelease > 0 && --pendingRelease == 0) { in.released = true; in.down = false; }
      else if (rnd(90) == 0) { in.pressed = true; in.down = true; pendingRelease = 2 + rnd(3); in.mx = W / 4 + rnd(W / 2); in.my = H / 4 + rnd(H / 2); }
      if (rnd(150) == 0) in.keyDowns.push_back({'A', 0});
      if (rnd(200) == 0) in.rightPressed = true;
      if (escIn > 0 && --escIn == 0) in.key[K_ESC] = true;
      else if (rnd(400) == 0) { in.keyDowns.push_back({'B', 2}); escIn = 15; }
    }
    a.frame(in, W, H, 13);
    FrameBuffer fb;
    if (!gl.beginFrame(W, H, fb)) { std::fprintf(stderr, "beginFrame failed\n"); return 1; }
    b.frame(in, 13, fb);
    gl.endFrame();
    if (!gl.readback(back)) { std::fprintf(stderr, "readback failed\n"); return 1; }
    const std::vector<uint32_t>& ref = a.screen();
    long bad = 0;
    int worst = 0;
    for (size_t i = 0; i < back.size(); i++) {
      const uint32_t x = back[i] | 0xFF000000u, y = ref[i] | 0xFF000000u;
      if (x == y) continue;
      bad++;
      for (int s = 0; s < 24; s += 8) worst = std::max(worst, std::abs(int((x >> s) & 255) - int((y >> s) & 255)));
    }
    totalPixels += long(back.size());
    badPixels += bad;
    worstDiff = std::max(worstDiff, worst);
    if (bad) { badFrames++; if (badFrames <= 5) std::printf("frame %d: %ld pixels differ, worst channel diff %d\n", f, bad, worst); }
    if (b.lastFramePhases().rastered && gl.stats().items > 0) { gpuFrames++; blendItemsSeen += gl.stats().blendItems; planeItemsSeen += gl.stats().planeItems; itemsSeen += gl.stats().items; }
  }
  std::printf("gl present: %d frames (%d with GPU city work), %d frames differ, %ld of %ld pixels differ, worst channel diff %d\n",
              frames, gpuFrames, badFrames, badPixels, totalPixels, worstDiff);
  std::printf("items composited: %ld, blended (m2<32 or alpha plane): %ld, with alpha plane: %ld\n", itemsSeen, blendItemsSeen, planeItemsSeen);
  return (cityOnly || basic) ? (badPixels ? 1 : 0) : (worstDiff > 3 ? 1 : 0);
}
