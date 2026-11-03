// Query panel numbers come from the simulation state (spec 09 6.12): hospital / school / college figures of the population model.
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
// A fresh demo city with an occupant of `agent` at (30,30) and the given population figures, queried with the Ctrl-held query tool.
template <class F> QueryInfo queryAgent(uint32_t agent, F setup) {
  Game game; g = &game; in = Input();
  game.setPersistence(false);
  game.startDemo(64);
  frame(3);
  World& mw = game.mutableWorld();
  for (int x = 0; x <= 64; x++) for (int z = 0; z <= 64; z++) { mw.alt[mw.vIdx(x, z)] = 120; mw.water[mw.vIdx(x, z)] = 60; }
  mw.seaLevel = 60;
  for (auto& f : mw.flora) f = 0;
  setup(game.city().pop);
  Occupant o; o.x = 30; o.z = 30; o.sizeX = 2; o.sizeZ = 2; o.occId = 0xD2; o.keyType = 0x207EDC0E; o.state = 1; o.agentType = agent;
  mw.occupants.clear(); mw.occupants.push_back(o);
  game.city().rebuildIndex(false);
  game.terrainChanged();
  game.setView(32, 32, 2);
  frame(2);
  int sx, sy;
  CHECK(game.tileScreenPos(30, 30, sx, sy));
  in.mx = sx; in.my = sy; in.modCtrl = true; frame(2);
  in.pressed = in.down = true; frame(); in.pressed = false; in.released = true; in.down = false; frame(2);
  return game.queryInfo();
}
}  // namespace

int main() {
  QueryInfo q = queryAgent(13310, [](Population& p) { p.hospitalBeds = 120; p.patients = 90; p.doctors = 40; p.healthRating = 4; });   // hospital
  CHECK(q.cls == QueryClass::Hospital && q.v[0] == 120 && q.v[1] == 90 && q.v[2] == 40);
  CHECK(int(q.band[0]) * 5 / 256 == 0);             // rating 4 = grade A
  q = queryAgent(13010, [](Population& p) { p.schoolDesks = 500; p.schoolAgePop = 420; p.teachers = 25; p.schoolRating = 2; });          // school
  CHECK(q.cls == QueryClass::School && q.v[0] == 500 && q.v[1] == 420 && q.v[2] == 25);
  CHECK(int(q.band[0]) * 5 / 256 == 2);             // rating 2 = grade C
  q = queryAgent(13873, [](Population& p) { p.collegeDesks = 300; p.collegeAgePop = 280; p.professors = 12; p.collegeRating = 0; });     // college
  CHECK(q.cls == QueryClass::College && q.v[0] == 300 && q.v[1] == 280 && q.v[2] == 12);
  CHECK(int(q.band[0]) * 5 / 256 == 4);             // rating 0 = grade F
  TEST_MAIN_END();
}
