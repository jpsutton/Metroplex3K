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

  // Q-AZ: city pause count = player pause; pending/modal = blocking windows / screens.
  {
    City& c = game.city();
    CHECK(c.cityPauseCount() >= 1);   // start-paused
    const int pauseStart = c.cityPauseCount();
    game.setPaused(false);
    CHECK(c.cityPauseCount() == pauseStart - 1);
    game.setPaused(true);
    CHECK(c.cityPauseCount() == pauseStart);
    const int pend0 = c.cityPendingCount();
    game.openWindow("budget");
    frame(3);
    CHECK(c.cityPauseCount() == pauseStart);          // modal does not raise player pause
    CHECK(c.cityPendingCount() == pend0 + 1);
    CHECK(c.hold());
    in.key[K_ESC] = true; frame(3);
    CHECK(c.cityPendingCount() == pend0);
    // Save/load-style screen while city loaded → pending (not pause).
    game.gotoScreen(Screen::MainMenu);
    frame(2);
    CHECK(c.cityPauseCount() == pauseStart);
    CHECK(c.cityPendingCount() == pend0 + 1);
    game.gotoScreen(Screen::InCity);
    frame(2);
    CHECK(c.cityPendingCount() == pend0);
    game.setPaused(false);   // continue the rest of the test unpaused
  }

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
  {  // issue #33: toggle cheats reach the engine
    City& cc = game.city();
    CHECK(!cc.budget.freePlacement && !cc.budget.debtFloor);
    game.typeCheat("I am weak"); CHECK(cc.budget.freePlacement);
    game.typeCheat("I am weak"); CHECK(!cc.budget.freePlacement);
    game.typeCheat("so sue me"); CHECK(cc.budget.debtFloor);
    game.typeCheat("Salt off"); CHECK(cc.saltOff);
    game.typeCheat("Salt on"); CHECK(!cc.saltOff);
    int sea = game.world().seaLevel, a0 = game.world().alt[0];
    game.typeCheat("Terrain ten up");
    CHECK(game.world().seaLevel == sea + 10 && game.world().alt[0] == std::min(253, std::max(2, a0 + 10)));
    game.typeCheat("Terrain ten down");
  }
  game.runCommand(0xE3166801);
  drag(20, 25, 44, 35);
  frame(2);

  // Tool menu clicks do not crash.
  for (int y = 10; y < 280; y += 20) { click(50, y); frame(1); }
  frame(5);
  CHECK(game.currentScreen() == Screen::InCity || game.currentScreen() == Screen::MainMenu);

  // Leaving the city: Exit asks "Now leaving" (OK / Cancel), then a modified city asks to save (Yes / No);
  // New / Load ask to save only when modified. Enter = first button, Esc = last.
  auto key = [&](int k) { in.key[k] = true; frame(2); };
  game.gotoScreen(Screen::InCity); game.setDirty(true); frame(2);
  game.runCommand(0xA10438E9); frame(2);
  CHECK(!game.wantQuit() && game.currentScreen() == Screen::InCity);
  key(K_ESC);                                       // Cancel: stay
  CHECK(!game.wantQuit() && game.currentScreen() == Screen::InCity);
  game.runCommand(0xE3270FE9); frame(2);
  CHECK(game.currentScreen() == Screen::InCity);    // save prompt up, not yet the New City screen
  key(K_ESC);                                       // No: discard and go on
  CHECK(game.currentScreen() == Screen::NewCity);
  game.gotoScreen(Screen::InCity); game.setDirty(false); frame(2);
  game.runCommand(0x610438A4); frame(2);
  CHECK(game.currentScreen() == Screen::LoadList);  // unmodified: no prompt
  game.gotoScreen(Screen::InCity); game.setDirty(true); frame(2);
  game.runCommand(0xA10438E9); frame(2);
  key(K_ENTER);                                     // OK on "Now leaving"
  CHECK(!game.wantQuit());                          // save prompt follows for a modified city
  key(K_ESC);                                       // No
  CHECK(game.wantQuit());
  TEST_MAIN_END();
}
