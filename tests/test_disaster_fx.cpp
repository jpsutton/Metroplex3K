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

int count(const std::vector<FxImpact>& v, FxImpact::Kind k) { int n = 0; for (auto& i : v) n += i.kind == k; return n; }

void testUfo() {
  DisasterFx fx; fx.seed(7); fx.setMapSize(64, 64);
  DisasterCommand c; c.kind = DisasterCommand::UfoSwarm;
  c.a = 3; c.b = 1; c.c = 2;   // passive saucers (no bombs), 2 abduction passes
  for (int i = 0; i < 7; i++) c.pts.emplace_back(10 + i * 4, 20);   // beam targets
  c.pts2.emplace_back(30, 40);                                       // crop circle
  for (int i = 0; i < 6; i++) c.pts3.emplace_back(5 + i, 5 + i);
  fx.onCommand(c);
  CHECK(fx.pendingImpacts());
  std::vector<FxImpact> got;
  run(fx, 600000, got);
  CHECK(count(got, FxImpact::UfoBeam) == 7);
  CHECK(count(got, FxImpact::UfoCrop) == 1);
  CHECK(count(got, FxImpact::UfoAbduction) == 2);
  CHECK(!fx.pendingImpacts());
  // Exit: a saucer flagged to explode disappears, the other fades out over 3 s.
  DisasterFx f2; f2.seed(5); f2.setMapSize(64, 64);
  DisasterCommand sw; sw.kind = DisasterCommand::UfoSwarm; sw.a = 2; sw.b = 1;
  f2.onCommand(sw);
  DisasterCommand lv; lv.kind = DisasterCommand::UfoLeave; lv.a = 2; lv.ints = {1, 0};
  f2.onCommand(lv);
  got.clear();
  run(f2, 300000, got);
  CHECK(f2.count() == 0);
}

void testLocusts() {
  DisasterFx fx; fx.seed(3); fx.setMapSize(64, 64);
  DisasterCommand c; c.kind = DisasterCommand::LocustSwarm; c.a = 2;
  for (int i = 0; i < 9; i++) c.pts.emplace_back(8 + i, 8 + i * 2);
  fx.onCommand(c);
  CHECK(fx.pendingImpacts());
  std::vector<FxImpact> got;
  run(fx, 300000, got);
  CHECK(count(got, FxImpact::LocustEat) == 9);   // one "eat" report per crop
  CHECK(count(got, FxImpact::LocustFlora) > 0);   // the 5 x 5 trail reports tiles for flora damage
  CHECK(!fx.pendingImpacts() && fx.count() == 0);
}

void testDebrisLauncher() {
  struct W : DisasterWorld { int width() const override { return 64; } int height() const override { return 64; } } w;
  DisasterManager m; m.seed(1);
  DisasterInputs in; in.year = 2000; DisasterOutput out;
  CHECK(m.startSpaceJunk(w, in, 0.5f, false, out));   // no warning: the attack window is open
  DisasterFx fx; fx.seed(11); fx.setMapSize(64, 64);
  fx.syncState(m);
  std::vector<FxImpact> got;
  run(fx, 90000, got);
  const int hits = count(got, FxImpact::Debris);
  CHECK(hits >= 10 && hits <= 45);   // one piece per 2.25-4.5 s, each falls for about 9 s
  for (auto& i : got) if (i.kind == FxImpact::Debris) CHECK(i.x >= -1 && i.x < 64 + 4 && i.z >= 0 && i.z < 64);
}

void testTornadoFunnel() {
  struct W : DisasterWorld { int width() const override { return 64; } int height() const override { return 64; } } w;
  DisasterManager m; m.seed(1); m.setTornadoDriven(true);
  DisasterOutput out;
  int a[8] = {10, 32, 60, 32, 0, 60, 500, 0};   // east at 500 tiles/min = 8.3 tiles/s, no warning
  CHECK(m.scriptTornado(w, a, out));
  DisasterInputs in;
  m.minuteTick(w, in, out);   // touches down
  CHECK(m.tornadoes()[0].phase == 5);
  DisasterFx fx; fx.seed(2); fx.setMapSize(64, 64);
  fx.syncState(m);
  std::vector<FxImpact> got;
  run(fx, 12000, got);
  CHECK(count(got, FxImpact::TornadoTile) >= 40);   // one report per new tile
  CHECK(count(got, FxImpact::TornadoLeft) == 1);    // then it leaves the map
  uint32_t uid = m.tornadoes()[0].uid;
  for (auto& i : got) if (i.kind == FxImpact::TornadoTile) CHECK(i.id == uid);
}

void testOneShots() {
  DisasterFx fx; fx.setMapSize(32, 32);
  DisasterCommand e; e.kind = DisasterCommand::Explosion; e.x = 3; e.z = 4; e.b = 0; e.c = 3;
  fx.onCommand(e);
  DisasterCommand s; s.kind = DisasterCommand::Splash; s.x = 5; s.z = 5;
  fx.onCommand(s);
  CHECK(fx.count() == 2);
  CHECK(!fx.pendingImpacts());   // explosions never hold a disaster open
  std::vector<FxImpact> got;
  run(fx, 2000, got);
  CHECK(fx.count() == 0 || fx.count() == 1);   // the splash (1.6 s) is gone; the 13-frame explosion (3.0 s) may remain
  run(fx, 2000, got);
  CHECK(fx.count() == 0 && got.empty());
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
  testUfo();
  testLocusts();
  testDebrisLauncher();
  testTornadoFunnel();
  testOneShots();
  testManagerHold();
  TEST_MAIN_END();
}
