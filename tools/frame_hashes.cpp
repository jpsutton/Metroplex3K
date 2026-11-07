// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Prints an FNV-1a hash of every screen frame of a scripted, deterministic run (sync sim, fixed dt, seeded input:
// pan, zoom, clicks, zone painting, windows). Build it on two commits and diff the output to check that a render
// change left every pixel of every frame alone, moving vehicles included.
//   frame_hashes --game DIR --city FILE [--frames N] [--size WxH]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include "core/game.h"
using namespace m3k;

int main(int argc, char** argv) {
  std::string game, city;
  int frames = 1500, W = 1280, H = 800;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--game" && i + 1 < argc) game = argv[++i];
    else if (a == "--city" && i + 1 < argc) city = argv[++i];
    else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &W, &H);
  }
  if (game.empty() || city.empty()) { std::fprintf(stderr, "usage: frame_hashes --game DIR --city FILE [--frames N] [--size WxH]\n"); return 2; }
  Game gm; gm.setPersistence(false);
  std::string err;
  if (!gm.init(game, err)) { std::fprintf(stderr, "init: %s\n", err.c_str()); return 1; }
  gm.setIntroEnabled(false);
  Input in;
  gm.requestFile(city);
  for (int i = 0; i < 40 && !gm.world().W; i++) gm.frame(in, W, H, 16);
  if (!gm.world().W) return 1;
  if (const char* e = std::getenv("M3K_TERRAIN_CACHE"); e && *e && *e != '0') gm.renderer().setTerrainLayerCache(true);
  if (const char* e = std::getenv("M3K_ITEM_CACHE"); e && *e && *e != '0') gm.renderer().setItemListCache(true);
  gm.setPaused(false);
  std::mt19937 rng(777);
  auto rnd = [&](int n) { return int(rng() % uint32_t(n)); };
  in.mx = W / 2; in.my = H / 2;
  int heldFor = 0, pendingRelease = 0, escIn = 0;
  for (int f = 0; f < frames; f++) {
    in.pressed = in.released = in.rightPressed = false; in.wheel = 0; in.keyDowns.clear(); in.text.clear();
    for (bool& k : in.key) k = false;
    in.mx = std::clamp(in.mx + rnd(9) - 4, 0, W - 1); in.my = std::clamp(in.my + rnd(9) - 4, 0, H - 1);
    if (heldFor > 0) heldFor--; else { for (bool& k : in.held) k = false; if (rnd(40) == 0) { in.held[rnd(4)] = true; heldFor = 10 + rnd(30); } }
    if (rnd(80) == 0) in.wheel = rnd(2) ? 1 : -1;
    if (pendingRelease > 0 && --pendingRelease == 0) { in.released = true; in.down = false; }
    else if (rnd(90) == 0) { in.pressed = true; in.down = true; pendingRelease = 2 + rnd(3); in.mx = W / 4 + rnd(W / 2); in.my = H / 4 + rnd(H / 2); }
    if (rnd(150) == 0) in.keyDowns.push_back({'A', 0});
    if (rnd(200) == 0) in.rightPressed = true;
    if (rnd(500) == 0) in.keyDowns.push_back({191, 0});
    if (escIn > 0 && --escIn == 0) in.key[K_ESC] = true;
    else if (rnd(400) == 0) { in.keyDowns.push_back({'B', 2}); escIn = 15; }
    gm.frame(in, W, H, 13);
    uint64_t h = 1469598103934665603ull;
    for (uint32_t p : gm.screen()) h = (h ^ p) * 1099511628211ull;
    std::printf("%d %016llx\n", f, (unsigned long long)h);
  }
  return 0;
}
