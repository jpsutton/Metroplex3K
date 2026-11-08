// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Headless play demo: builds a small city through Game and writes screenshots.
//   game_play_demo [--game DIR] [--size WxH] out_prefix
#include <cstdio>
#include "core/game.h"
#include "png.h"
using namespace m3k;

static Game* g; static Input in; static int W = 1280, H = 800;
static void frame(int n = 1, uint32_t dt = 16) { for (int i = 0; i < n; i++) { g->frame(in, W, H, dt); in.pressed = in.released = in.rightPressed = false; in.wheel = 0; in.text.clear(); for (auto& k : in.key) k = false; } }
static bool drag(int x0, int z0, int x1, int z1) {
  int ax, ay, bx, by; if (!g->tileScreenPos(x0, z0, ax, ay) || !g->tileScreenPos(x1, z1, bx, by)) return false;
  in.mx = ax; in.my = ay; frame(); in.pressed = in.down = true; frame(); in.mx = bx; in.my = by; frame(); in.released = true; in.down = false; frame(); return true;
}
int main(int argc, char** argv) {
  std::string game, prefix = "play";
  for (int i = 1; i < argc; i++) { std::string a = argv[i]; if (a == "--game" && i + 1 < argc) game = argv[++i]; else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &W, &H); else prefix = a; }
  Game gm; g = &gm; gm.setPersistence(false);
  std::string err;
  if (!game.empty() && gm.init(game, err)) { gm.setIntroEnabled(false); } else gm.startDemo(64);
  frame(3);
  World& w = gm.mutableWorld();
  if (game.empty()) { for (int x = 0; x <= w.W; x++) for (int z = 0; z <= w.H; z++) { w.alt[w.vIdx(x, z)] = 120; w.water[w.vIdx(x, z)] = 60; } for (auto& f : w.flora) f = 0; gm.terrainChanged(); }
  gm.setView(32, 32, 2); frame(3);
  gm.runCommand(0xC2B5DE77); drag(20, 30, 44, 30);
  gm.runCommand(0x42E88586); drag(22, 31, 30, 34);
  gm.runCommand(0x02E88949); drag(32, 31, 38, 33);
  gm.runCommand(0xC2E8898F); drag(22, 25, 28, 28);
  gm.city().plopAgent(900, 12111, 4, 4, 46, 28, 100);
  gm.runCommand(0x42C2F8BF); drag(20, 30, 46, 30);
  gm.setPaused(false);
  for (int i = 0; i < 400; i++) frame(1, 2000);
  writePng(prefix + "_city.png", W, H, gm.screen());
  const char* wins[] = {"budget", "ordinances", "neighbours", "advisors", "map", "graphs"};
  for (auto n : wins) { gm.openWindow(n); frame(3); writePng(prefix + "_" + n + ".png", W, H, gm.screen()); in.key[K_ESC] = true; frame(2); }
  std::printf("%s\n", gm.city().debugStats().c_str());
  std::printf("pop %d funds %lld occupants %zu year %d\n", gm.sim().population(), (long long)gm.sim().funds, gm.world().occupants.size(), gm.world().info.year);
}
