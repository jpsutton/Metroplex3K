// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Headless demo: generate terrain (or load a city) and write PNGs. Usage:
//   render_demo [--game DIR] [--city FILE] [--seed N] [--zoom Z] [--rot R] [--size 128|192|256] out.png
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "core/render.h"
#include "core/terrain_gen.h"
#include "png.h"
using namespace m3k;

int main(int argc, char** argv) {
  std::string game, city, out = "out.png";
  int seed = 1, zoom = 2, rot = 0, size = 128;
  bool grid = false, under = false, overlay = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--game" && i + 1 < argc) game = argv[++i];
    else if (a == "--city" && i + 1 < argc) city = argv[++i];
    else if (a == "--seed" && i + 1 < argc) seed = atoi(argv[++i]);
    else if (a == "--zoom" && i + 1 < argc) zoom = atoi(argv[++i]);
    else if (a == "--rot" && i + 1 < argc) rot = atoi(argv[++i]);
    else if (a == "--grid") grid = true;
    else if (a == "--under") under = true;
    else if (a == "--overlay") overlay = true;
    else if (a == "--size" && i + 1 < argc) size = atoi(argv[++i]);
    else out = a;
  }
  GameData data; GameData* dp = nullptr;
  if (!game.empty()) { std::string err; if (data.open(game, err)) dp = &data; else std::cerr << err << "\n"; }
  World w;
  if (!city.empty()) {
    Bytes b; if (!readFile(city, b)) { std::cerr << "cannot read city\n"; return 1; }
    std::vector<std::string> warn;
    w = loadCity(b, &warn);
    for (auto& s : warn) std::cerr << "warn: " << s << "\n";
  } else {
    TerrainGenOptions o; o.seed = uint32_t(seed);
    generateTerrain(w, size, size, o);
  }
  Renderer r; r.setWorld(&w, dp); r.resize(1280, 800);
  Camera cam; cam.zoom = zoom; cam.rot = rot;
  r.centreOnWorld(cam, w.W / 2.f, w.H / 2.f);
  RenderOptions opt; opt.grid = grid; opt.underground = under;
  DataOverlay ov;
  if (overlay) {
    std::vector<uint8_t> vals(size_t(w.W) * w.H);
    for (int x = 0; x < w.W; x++) for (int z = 0; z < w.H; z++) vals[w.tIdx(x, z)] = uint8_t((x + z) % 256);
    ov = makeDataOverlay(DataLayer::LandValue, vals, w.W, w.H);
    opt.overlay = &ov;
  }
  RenderScene scene; TilePreview tp; tp.x0 = w.W / 2; tp.z0 = w.H / 2; tp.x1 = tp.x0 + 3; tp.z1 = tp.z0 + 2; scene.previews.push_back(tp);
  r.render(cam, opt, overlay ? &scene : nullptr);
  writePng(out, r.width(), r.height(), r.pixels());
  std::cout << "wrote " << out << " (" << w.W << "x" << w.H << ", sea " << int(w.seaLevel) << ")\n";
}
