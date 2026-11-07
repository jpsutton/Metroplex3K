// Tests for the disaster effect layer (src/core/disaster_fx.*): flying objects deliver their impacts, and the manager is held open
// until they have. No game data is needed: without sprites nothing is emitted, but the movement and impact logic still runs.
#include <algorithm>
#include "core/disaster_fx.h"
#include "testutil.h"

using namespace m3k;

namespace {
int run(DisasterFx& fx, int ms, std::vector<FxImpact>& got) {
  int steps = 0;
  for (int t = 0; t < ms; t += 50, steps++) {
    fx.update(50);
    for (const FxImpact& i : fx.takeImpacts()) got.push_back(i);
  }
  return steps;
}

void testUfoBombs() {
  DisasterFx fx; fx.seed(7); fx.setMapSize(64, 64);
  DisasterCommand c; c.kind = DisasterCommand::UfoSwarm;
  c.a = 3; c.b = 0; c.c = 5;
  for (int i = 0; i < 7; i++) c.pts.emplace_back(10 + i * 4, 20);
  c.pts2.emplace_back(30, 40);
  for (int i = 0; i < 6; i++) c.pts3.emplace_back(5 + i, 5 + i);
  fx.onCommand(c);
  CHECK(fx.pendingImpacts());
  std::vector<FxImpact> got;
  run(fx, 120000, got);
  int bombs = 0, crops = 0;
  for (auto& i : got) { bombs += i.kind == FxImpact::UfoBomb; crops += i.kind == FxImpact::CropCircle; }
  CHECK(bombs == 7);
  CHECK(crops == 1);
  CHECK(!fx.pendingImpacts());
}

void testLocusts() {
  DisasterFx fx; fx.seed(3); fx.setMapSize(64, 64);
  DisasterCommand c; c.kind = DisasterCommand::LocustSwarm; c.a = 2;
  for (int i = 0; i < 9; i++) c.pts.emplace_back(i, i * 2);
  fx.onCommand(c);
  CHECK(fx.pendingImpacts());
  std::vector<FxImpact> got;
  run(fx, 120000, got);
  int bites = 0;
  for (auto& i : got) bites += i.kind == FxImpact::LocustBite;
  CHECK(bites == 9);
  CHECK(!fx.pendingImpacts() && fx.count() == 0);
}

void testDebris() {
  DisasterFx fx; fx.seed(11); fx.setMapSize(64, 64);
  DisasterCommand c; c.kind = DisasterCommand::DebrisSpawn; c.a = 5000; c.b = 3000000;
  fx.onCommand(c);
  CHECK(fx.pendingImpacts());
  std::vector<FxImpact> got;
  run(fx, 20000, got);
  int hits = 0;
  for (auto& i : got) { hits += i.kind == FxImpact::Debris; CHECK(i.x >= 0 && i.x < 64 && i.z >= 0 && i.z < 64); }
  CHECK(hits == 20);   // class 5000 / 250 pieces
  CHECK(!fx.pendingImpacts());
}

void testOneShots() {
  DisasterFx fx; fx.setMapSize(32, 32);
  DisasterCommand e; e.kind = DisasterCommand::Explosion; e.x = 3; e.z = 4;
  fx.onCommand(e);
  DisasterCommand s; s.kind = DisasterCommand::Splash; s.x = 5; s.z = 5;
  fx.onCommand(s);
  CHECK(fx.count() == 2);
  CHECK(!fx.pendingImpacts());   // explosions never hold a disaster open
  std::vector<FxImpact> got;
  run(fx, 5000, got);
  CHECK(fx.count() == 0 && got.empty());
  // A saucer that explodes on exit disappears at UfoLeave.
  DisasterCommand sw; sw.kind = DisasterCommand::UfoSwarm; sw.a = 2;
  fx.onCommand(sw);
  size_t n = fx.count();
  DisasterCommand lv; lv.kind = DisasterCommand::UfoLeave; lv.a = 2; lv.ints = {1, 0};
  fx.onCommand(lv);
  CHECK(fx.count() == n);   // one saucer replaced by its blast
}

void testManagerHold() {
  // The manager keeps a space-junk instance alive while automata are busy, and ends it afterwards.
  struct W : DisasterWorld { int width() const override { return 32; } int height() const override { return 32; } } w;
  DisasterManager m; m.seed(1);
  DisasterInputs in; in.year = 2000;
  DisasterOutput out;
  CHECK(m.startSpaceJunk(w, in, 0.5f, false, out));
  m.setAutomataBusy(true);
  for (int i = 0; i < 400; i++) { m.minuteTick(w, in, out); out.clear(); }
  CHECK(m.active(DisasterType::SpaceJunk));
  m.setAutomataBusy(false);
  for (int i = 0; i < 400 && m.active(DisasterType::SpaceJunk); i++) { m.minuteTick(w, in, out); out.clear(); }
  CHECK(!m.active(DisasterType::SpaceJunk));
}
}  // namespace

int main() {
  testUfoBombs();
  testLocusts();
  testDebris();
  testOneShots();
  testManagerHold();
  TEST_MAIN_END();
}
