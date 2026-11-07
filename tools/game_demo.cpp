// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Headless UI smoke test: drives Game with scripted input and writes PNG screenshots.
//   game_demo [--game DIR] out_prefix
#include <iostream>
#include "core/game.h"
#include "png.h"
using namespace m3k;

int main(int argc, char** argv) {
  std::string game, prefix = "shot";
  for (int i = 1; i < argc; i++) { std::string a = argv[i]; if (a == "--game" && i + 1 < argc) game = argv[++i]; else prefix = a; }
  Game g; std::string err;
  g.init(game, err);
  Input in; in.mx = 640; in.my = 400;
  auto step = [&](int n = 2) { for (int i = 0; i < n; i++) { g.frame(in, 1280, 800, 16); in.pressed = in.released = false; in.text.clear(); for (auto& k : in.key) k = false; } };
  step();
  writePng(prefix + "_1.png", 1280, 800, g.screen());
  // Click "Explore generated terrain" (NeedData screen) at its known position.
  in.mx = 640 - 300 + 170 + 50; in.my = 400 - 150 + 200 + 10; in.pressed = true; in.down = true; step(1);
  in.released = true; in.down = false; step(2);
  in.mx = 5; in.my = 20; in.pressed = in.down = true; step(1); in.released = true; in.down = false; step(2);
  writePng(prefix + "_2.png", 1280, 800, g.screen());
  std::cout << "ok\n";
}
