// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Renders the skinned chrome using synthetic art to PNG. Usage: skin_demo [outdir]  -> skin_<h>.png
// With a real install: skin_demo outdir GAMEDIR uses the real art.
#include <iostream>
#include "core/gamedata.h"
#include "core/skin.h"
#include "../tests/skin_fixture.h"
#include "png.h"
using namespace m3k;

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  GameData gd; ResourceDb synth;
  ResourceDb* db = &synth;
  if (argc > 2) { std::string err; if (gd.open(argv[2], err)) db = &gd.db(); else std::cerr << err << "\n"; }
  if (db == &synth) synth.mountMemory(skinfix::build(), "skin");
  Fonts fonts;
  struct Mode { int w, h; } modes[] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 900}};
  for (auto m : modes) {
    Skin s;
    if (!s.load(*db, m.h, m.w)) { std::cout << "no art for " << m.h << "\n"; continue; }
    std::vector<uint32_t> fb(size_t(m.w) * m.h);
    for (size_t i = 0; i < fb.size(); i++) fb[i] = (((i % m.w) / 32 + (i / m.w) / 32) & 1) ? rgb(0x4A, 0x70, 0x48) : rgb(0x52, 0x7A, 0x50);
    Ui ui; ui.fonts = &fonts;
    Input in; in.mx = m.w - 96 + 40; in.my = 120;
    ui.begin(&fb, m.w, m.h, in);
    Rect col = s.columnRect(m.w, m.h);
    s.drawRootPanel(ui, col.x, col.y, 2, 1);
    // sub-menu 1 open: panel + its buttons
    s.drawSubPanel(ui, col.x, col.y, 1);
    for (size_t k = 0; k < s.layout().subIds[0].size(); k++) {
      bool c; Rect r = s.subButtonRect(1, int(k), col.x, col.y);
      s.imageButtonCells(ui, s.toolStrip(s.layout().subIds[0][k]), 4, r, "", k == 2, true, c);
    }
    BottomBarPlace pl;
    s.drawBottomBar(ui, &pl);
    s.drawRciIndicator(ui, m.w - 96 - 41 - 4, m.h - 88, 0.8, -0.4, 1.5);
    bool c; s.button3(ui, s.get("button"), {20, 20, 160, 33}, "Button", true, false, c);
    s.drawCheck(ui, s.get("check"), 20, 70, true); s.drawCheck(ui, s.get("radio"), 50, 70, false);
    std::string out = dir + "/skin_" + std::to_string(m.h) + ".png";
    writePng(out, m.w, m.h, fb);
    std::cout << "wrote " << out << "\n";
  }
}
