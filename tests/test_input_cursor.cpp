// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Headless test: map cursor follows held modifier keys and the right-button drag-scroll.
#include <cstring>
#include "core/game.h"
#include "testutil.h"
using namespace m3k;

namespace {
Game* g = nullptr;
Input in;
const int W = 1280, H = 800;

void frame(int n = 1, uint32_t dt = 16) {
  for (int i = 0; i < n; i++) {
    g->frame(in, W, H, dt);
    in.pressed = in.released = in.rightPressed = false; in.wheel = 0; in.text.clear();
    for (auto& k : in.key) k = false;
  }
}
}  // namespace

int main() {
  Game game; g = &game;
  game.setPersistence(false);
  game.startDemo(64);
  frame(3);
  CHECK(game.currentScreen() == Screen::InCity);
  game.setView(32, 32, 2);
  in.mx = W / 2; in.my = H / 2;
  frame(3);
  CHECK(game.cursorName() == "CENTER CURSOR");

  // Right-button drag: the OS cursor is hidden; the anchor marker and the PAN arrow are drawn into the frame.
  CHECK(game.panCursor().empty());
  in.rightPressed = in.rightDown = true; frame();
  CHECK(game.cursorName() == "BLANK" && game.panCursor() == "EARLY MOVER CURSOR");
  in.mx = W / 2 + 60; frame();
  CHECK(game.panCursor() == "PAN RIGHT ARROW");
  in.my = H / 2 - 60; frame();
  CHECK(game.panCursor() == "PAN UP RIGHT ARROW");
  in.rightDown = false; frame();
  CHECK(game.cursorName() == "CENTER CURSOR" && game.panCursor().empty());
  in.mx = W / 2; in.my = H / 2; frame();

  // Holding Alt = bulldozer cursor, holding Ctrl = query cursor; releasing restores the cursor and the tool.
  in.modAlt = true; frame();
  CHECK(game.cursorName() == "DEMOLISH CLEAR CURSOR");
  CHECK(game.currentTool() == ToolKind::Demolish);
  in.modAlt = false; frame();
  CHECK(game.cursorName() == "CENTER CURSOR");
  CHECK(game.currentTool() == ToolKind::None);
  in.modCtrl = true; frame();
  CHECK(game.cursorName() == "QUERY CURSOR");
  CHECK(game.currentTool() == ToolKind::Query);
  in.modCtrl = false; frame();
  CHECK(game.cursorName() == "CENTER CURSOR");
  CHECK(game.currentTool() == ToolKind::None);

  // Query highlights the whole property under the pointer, not just the tile.
  {
    World& mw = game.mutableWorld();
    for (int x = 0; x <= 64; x++) for (int z = 0; z <= 64; z++) { mw.alt[mw.vIdx(x, z)] = 120; mw.water[mw.vIdx(x, z)] = 60; }
    mw.seaLevel = 60;
    for (auto& f : mw.flora) f = 0;
    Occupant o; o.x = 30; o.z = 30; o.sizeX = 3; o.sizeZ = 2; o.occId = 0xD2; o.keyType = 0x207EDC0E; o.state = 1;
    mw.occupants.push_back(o);
    game.city().rebuildIndex(false);
    game.terrainChanged();
    game.setView(32, 32, 2);   // the drag-scroll above moved the view
    frame(2);
    int sx, sy;
    CHECK(game.tileScreenPos(31, 30, sx, sy));
    in.mx = sx; in.my = sy; in.modCtrl = true; frame(2);
    CHECK(game.scene().previews.empty() && game.scene().tints.size() == 1);
    if (!game.scene().tints.empty()) {
      const SpriteTint& t = game.scene().tints[0];
      CHECK(t.x0 == 30 && t.z0 == 30 && t.x1 == 32 && t.z1 == 31 && !t.ground);   // whole building, sprite only
      CHECK(t.r == 0 && t.g == 130 && t.b == 176);
    }
    int ex, ey;
    CHECK(game.tileScreenPos(10, 10, ex, ey));
    in.mx = ex; in.my = ey; frame(2);
    CHECK(game.scene().tints.size() == 1);
    if (!game.scene().tints.empty()) {
      const SpriteTint& t = game.scene().tints[0];
      CHECK(t.x0 == t.x1 && t.z0 == t.z1 && t.ground);   // empty ground: one tile, diamond included
    }
    in.modCtrl = false; frame(2);
    in.mx = W / 2; in.my = H / 2; frame();
  }

  // A selected tool comes back after the modifier is released.
  game.runCommand(0x42E88586);   // Q hotkey: residential low zoning
  frame();
  const std::string zoneCur = game.cursorName();
  in.modAlt = true; frame();
  CHECK(game.cursorName() == "DEMOLISH CLEAR CURSOR");
  in.modAlt = false; frame();
  CHECK(game.currentTool() == ToolKind::Zone);
  CHECK(game.cursorName() == zoneCur);

  // Right-drag while a modifier is held still shows the pan cursor.
  in.modAlt = true; in.rightPressed = in.rightDown = true; frame();
  in.mx = W / 2 - 60; frame();
  CHECK(game.panCursor() == "PAN LEFT ARROW");
  in.rightDown = false; in.modAlt = false; frame(2);
  TEST_MAIN_END();
}
