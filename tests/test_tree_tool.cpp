// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Tree tool (spec 11 4.1 [OBSERVED]): plants on bare land and replaces flora, refused on buildings and networks.
#include "core/game.h"
#include "testutil.h"
using namespace m3k;

namespace {
Game* g = nullptr;
Input in;
const int W = 1280, H = 800;
void frame(int n = 1) {
  for (int i = 0; i < n; i++) { g->frame(in, W, H, 16); in.pressed = in.released = in.rightPressed = false; in.wheel = 0; in.text.clear(); for (auto& k : in.key) k = false; }
}
}  // namespace

int main() {
  Game game; g = &game;
  game.setPersistence(false);
  game.startDemo(64);
  frame(3);
  World& mw = game.mutableWorld();
  for (int x = 0; x <= 64; x++) for (int z = 0; z <= 64; z++) { mw.alt[mw.vIdx(x, z)] = 120; mw.water[mw.vIdx(x, z)] = 60; }
  for (auto& f : mw.flora) f = 0;
  mw.occupants.clear(); mw.surface.clear(); mw.pipes.clear(); mw.subway.clear();
  Occupant o; o.x = 20; o.z = 20; o.sizeX = 2; o.sizeZ = 2; o.occId = 0xD2; o.keyType = 0x207EDC0E; o.state = 1; o.agentType = 13310;
  mw.occupants.push_back(o);
  NetPiece road; road.x = 40; road.z = 40; road.y = 120; road.pieceId = 29; mw.surface.push_back(road);
  game.city().rebuildIndex(true);
  game.terrainChanged();
  frame(2);
  game.runCommand(0x4253EC3F); frame(2);
  CHECK(game.currentTool() == ToolKind::Terrain && game.toolArg() == 100);
  auto plant = [&](int tx, int tz) {
    game.setView(tx, tz, 2); frame(2);
    int sx, sy;
    if (!game.tileScreenPos(tx, tz, sx, sy)) return false;
    in.mx = sx; in.my = sy; frame(2);
    in.pressed = in.down = true; frame(4); in.pressed = false; in.released = true; in.down = false; frame(2); in.released = false;
    return true;
  };
  CHECK(plant(30, 30) && mw.flora[mw.tIdx(30, 30)] != 0);          // bare land
  CHECK(plant(30, 30) && mw.flora[mw.tIdx(30, 30)] != 0);          // a flora tile is allowed again
  CHECK(plant(20, 20) && mw.flora[mw.tIdx(20, 20)] == 0);          // building: refused
  CHECK(plant(40, 40) && mw.flora[mw.tIdx(40, 40)] == 0);          // network piece: refused
  // Terraform clearing (spec 11 4.1): raising over flora clears the trees; a building in the affected area makes it illegal.
  game.runCommand(0x825AD7F6); frame(2);
  CHECK(game.currentTool() == ToolKind::Terrain && game.toolArg() == 1);
  mw.flora[mw.tIdx(50, 50)] = 0x32;
  const int alt50 = mw.altAt(50, 50);
  CHECK(plant(50, 50));
  CHECK(mw.altAt(50, 50) != alt50 && mw.flora[mw.tIdx(50, 50)] == 0);   // raised; the tree is gone
  const int alt20 = mw.altAt(20, 20);
  CHECK(plant(20, 20));
  CHECK(mw.altAt(20, 20) == alt20);                                      // building on the tile: nothing changes
  TEST_MAIN_END();
}
