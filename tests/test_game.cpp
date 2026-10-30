// Headless end-to-end test: drives Game through scripted Input on synthetic data.
#include <cmath>
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
bool tileXY(int tx, int tz, int& sx, int& sy) { return g->tileScreenPos(tx, tz, sx, sy); }
void click(int x, int y) {
  in.mx = x; in.my = y; in.pressed = in.down = true; frame();
  in.released = true; in.down = false; frame();
}
bool drag(int x0, int z0, int x1, int z1) {
  int ax, ay, bx, by;
  if (!tileXY(x0, z0, ax, ay) || !tileXY(x1, z1, bx, by)) return false;
  in.mx = ax; in.my = ay; frame();
  in.pressed = in.down = true; frame();
  in.mx = bx; in.my = by; frame();
  in.released = true; in.down = false; frame();
  return true;
}
}  // namespace

int main() {
  Game game; g = &game;
  game.setPersistence(false);
  game.startDemo(64);
  frame(3);
  CHECK(game.currentScreen() == Screen::InCity);
  City& city = game.city();
  const World& w = game.world();

  // Flatten a building site so zoning/roads are legal (test setup, not a game action).
  World& mw = game.mutableWorld();
  for (int x = 0; x <= 64; x++) for (int z = 0; z <= 64; z++) { mw.alt[mw.vIdx(x, z)] = 120; mw.water[mw.vIdx(x, z)] = 60; }
  mw.seaLevel = 60;
  for (auto& f : mw.flora) f = 0;
  game.terrainChanged();
  game.setView(32, 32, 2);
  frame(2);

  int64_t funds0 = game.sim().funds;
  // Road (tool via command id, drag on the map).
  game.runCommand(0xC2B5DE77);
  CHECK(drag(20, 30, 44, 30));
  CHECK(city.roadCount() > 0);
  // Zones along the road.
  game.runCommand(0x42E88586);      // residential light
  CHECK(drag(22, 31, 30, 34));
  game.runCommand(0x02E88949);      // commercial light
  CHECK(drag(32, 31, 38, 33));
  game.runCommand(0xC2E8898F);      // industrial light
  CHECK(drag(22, 25, 28, 28));
  int zoned = 0;
  for (auto z : w.zone) if (z) zoned++;
  CHECK(zoned > 20);
  CHECK(game.sim().funds < funds0);

  // Power line + plant via command + plop.
  city.plopAgent(900, 12111, 4, 4, 46, 28, 100);
  game.runCommand(0x42C2F8BF);
  drag(20, 30, 46, 30);
  frame(2);

  // Run the simulation for a couple of years.
  game.setPaused(false);
  for (int i = 0; i < 500; i++) frame(1, 2000);
  CHECK(std::isfinite(double(game.sim().funds)));
  CHECK(game.sim().population() >= 0);
  for (const auto& o : w.occupants) CHECK(o.x >= 0 && o.z >= 0 && o.x < w.W && o.z < w.H);
  CHECK(w.info.year >= 1950);
  CHECK(w.occupants.size() > 1);   // plant + grown buildings
  CHECK(game.sim().population() > 0);

  // Windows open and close without crashing.
  const char* wins[] = {"budget", "ordinances", "neighbours", "deals", "advisors", "props", "map", "graphs", "charts", "prefs", "goals"};
  for (auto n : wins) { game.openWindow(n); frame(3); CHECK(game.windowOpen(n) || std::string(n) == "goals"); in.key[K_ESC] = true; frame(2); }

  // Disasters and cheats.
  for (auto t : allDisasterTypes()) { if (t == DisasterType::Parade) continue; game.startDisaster(t); frame(5, 100); }
  game.typeCheat("fund");
  frame(2);
  game.runCommand(0xE3166801);
  drag(20, 25, 44, 35);
  frame(2);

  // Tool menu clicks do not crash.
  for (int y = 10; y < 280; y += 20) { click(50, y); frame(1); }
  frame(5);
  CHECK(game.currentScreen() == Screen::InCity || game.currentScreen() == Screen::MainMenu);
  TEST_MAIN_END();
}
