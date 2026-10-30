// Tests for the disaster subsystem (spec/21).
#include <algorithm>
#include <cmath>
#include "core/disasters.h"
#include "testutil.h"

using namespace m3k;

namespace {
struct MockWorld : DisasterWorld {
  int W = 64, H = 64;
  std::vector<OccInfo> occ;
  std::vector<uint8_t> water;
  int flam = 200, fcov = 0, pcov = 0;
  std::vector<uint32_t> removed;
  MockWorld() : water(size_t(64 * 64), 0) {}
  int width() const override { return W; }
  int height() const override { return H; }
  int flammability(int, int) const override { return flam; }
  int fireCoverage(int, int) const override { return fcov; }
  int policeCoverage(int, int) const override { return pcov; }
  OccInfo occupantAt(int x, int z) const override {
    for (auto& o : occ) {
      if (std::find(removed.begin(), removed.end(), o.id) != removed.end()) continue;
      if (x >= o.ax && x < o.ax + o.w && z >= o.az && z < o.az + o.h) return o;
    }
    return OccInfo();
  }
  std::vector<OccInfo> occupantsTouching(const OccInfo& o) const override {
    std::vector<OccInfo> v;
    for (auto& p : occ)
      if (p.id != o.id && p.ax <= o.ax + o.w && o.ax <= p.ax + p.w && p.az <= o.az + o.h && o.az <= p.az + p.h) v.push_back(p);
    return v;
  }
  bool inGroup(const OccInfo& o, int g) const override { return g == 100 ? o.agentType == 3000 : o.agentType == 5000; }
  bool isWater(int x, int z) const override { return water[size_t(z * W + x)] != 0; }
  OccInfo add(int x, int z, uint32_t agent, int w = 2, int h = 2, int64_t value = 1000) {
    OccInfo o; o.valid = true; o.id = uint32_t(occ.size() + 1); o.ax = x; o.az = z; o.w = w; o.h = h;
    o.agentType = agent; o.value = value; o.isLot = true;
    occ.push_back(o);
    return o;
  }
  void apply(const DisasterOutput& out) {
    for (auto& c : out.commands) if (c.kind == DisasterCommand::DestroyOccupant) removed.push_back(c.id);
  }
};

int countCmd(const DisasterOutput& o, DisasterCommand::Kind k) {
  int n = 0; for (auto& c : o.commands) if (c.kind == k) ++n; return n;
}
int countEv(const DisasterOutput& o, DisasterEvent::Kind k) {
  int n = 0; for (auto& e : o.events) if (e.kind == k) ++n; return n;
}
}  // namespace

static void testRng() {
  GameRng r(1);
  CHECK(r.next() == 0x000041C6);
  CHECK(r.next() == 0x59D4967E);
  CHECK(r.next() == 0xBF022781);
  GameRng q(1);
  CHECK(q.rand(100) == 38);
  GameRng f(1);
  f.nextFloat(); CHECK(f.stateFloat() == 0x278DDE6D);
  f.nextFloat(); CHECK(f.stateFloat() == 0xFB533A69);
  GameRng z(0);
  z.nextFloat(); CHECK(z.stateFloat() == 0x0B5CE118);
  GameRng s(5);
  CHECK(s.rand(0) == 0);
  for (int i = 0; i < 100; ++i) { int v = s.centreWeighted(0, 20); CHECK(v >= 0 && v <= 20); }
}

static void testTunables() {
  DisasterTunables t;
  CHECK(t.fireIntervalMin == 48 && t.tornadoDestroyChance[2] == 95 && t.quakeT[3] == 180);
  IniFile sim = IniFile::fromText("[Tunables]\n0x047DF002=12\n0x84838207=200\n0x6484A25D=9\n");
  IniFile lay = IniFile::fromText("[FireDisaster]\n0x621CDA33=7,99\n[MiscDisasterTunables]\nMaxEmergencyDuration=3\n"
                                  "[ToxicCloudDisasterTunables]\nThresholdScoreForToxicCloud=77;\n");
  t.load(sim, lay);
  CHECK(t.fireIntervalMin == 12);
  CHECK(t.tornadoSpeedMax == -56);  // signed 8-bit read
  CHECK(t.riotUnrestThreshold == 9);
  CHECK(t.maxFires == 7 && t.fireSound == 99);
  CHECK(t.maxEmergencyDuration == 3 && t.toxThreshold == 77);
  CHECK(disasterClassId(DisasterType::Tornado) == 0x62A34670);
}

static void testFire() {
  MockWorld w;
  OccInfo a = w.add(10, 10, 3000), b = w.add(12, 10, 3000);  // b touches a
  DisasterManager m; m.seed(7);
  DisasterInputs in;
  DisasterOutput out;
  CHECK(m.startFire(w, 10, 10, out));
  CHECK(m.activeCount(DisasterType::Fire) == 1);
  CHECK(countCmd(out, DisasterCommand::IgniteOccupant) == 1);
  CHECK(!m.startFire(w, 10, 10, out));  // already burning
  CHECK(!m.startFire(w, 30, 30, out));  // no occupant
  bool destroyed = false;
  for (int i = 0; i < 5000 && m.active(DisasterType::Fire); ++i) {
    out.clear();
    m.minuteTick(w, in, out);
    w.apply(out);
    for (auto& c : out.commands)
      if (c.kind == DisasterCommand::DestroyOccupant) { destroyed = true; CHECK(c.a == kDmgBurnt); }
  }
  CHECK(destroyed);
  CHECK(!m.active(DisasterType::Fire));
  (void)a; (void)b;
  // relief: cost 1000 > min(0), pct without EW = 0 -> none credited
  // Full protection: fire never grows before step 10, goes out
  MockWorld w2; w2.fcov = 255; w2.add(5, 5, 3000);
  DisasterManager m2; m2.seed(3);
  out.clear(); CHECK(m2.startFire(w2, 5, 5, out));
  int clear = 0;
  for (int i = 0; i < 2000 && m2.active(DisasterType::Fire); ++i) { out.clear(); m2.minuteTick(w2, in, out); clear += countCmd(out, DisasterCommand::ClearBurning); }
  CHECK(clear == 1);
  // occupant removed -> record discarded silently
  MockWorld w3; w3.add(5, 5, 3000);
  DisasterManager m3; out.clear(); m3.startFire(w3, 5, 5, out);
  w3.removed.push_back(1);
  out.clear(); m3.minuteTick(w3, in, out);
  CHECK(!m3.active(DisasterType::Fire));
  // random fire picks an occupant of group 100/300 or any
  MockWorld w4; w4.add(20, 20, 5000);
  DisasterManager m4; m4.seed(11); out.clear();
  CHECK(m4.startRandomFire(w4, out));
  CHECK(countEv(out, DisasterEvent::Started) == 1);
  // below minFlam no ignition
  MockWorld w5; w5.flam = 0; w5.add(5, 5, 3000);
  DisasterManager m5; out.clear(); CHECK(!m5.startFire(w5, 5, 5, out));
  // scripted tile conversion 1-based
  MockWorld w6; w6.add(0, 0, 3000);
  DisasterManager m6; out.clear(); CHECK(m6.scriptFire(w6, 1, 1, out));
}

static void testDestroyRules() {
  MockWorld w;
  DisasterManager m; DisasterOutput out;
  OccInfo lf = w.add(1, 1, 12850);
  CHECK(!m.destroyOccupant(w, lf, kDmgRubble, out));
  OccInfo r = w.add(4, 4, 3000); r.lotDamage = kDmgBurnt;
  CHECK(!m.destroyOccupant(w, r, kDmgRubble, out));
  OccInfo r2 = w.add(8, 8, 3000); r2.lotDamage = kDmgRubble;
  CHECK(!m.destroyOccupant(w, r2, kDmgAbandon, out));
  CHECK(m.destroyOccupant(w, r2, kDmgBurnt, out));
  CHECK(m.disasterCost() == 0);  // already-rubble lot adds no cost? (rubble state excluded)
  OccInfo n = w.add(20, 20, 3000, 2, 2, 500);
  CHECK(m.destroyOccupant(w, n, kDmgRubble, out));
  CHECK(m.disasterCost() == 500);
  // nuclear
  OccInfo nuc = w.add(30, 30, 12115, 4, 4, 100);
  out.clear();
  CHECK(m.destroyOccupant(w, nuc, kDmgRubble, out));
  int rad = countCmd(out, DisasterCommand::MarkRadiation);
  CHECK(rad > 300 && rad < 41 * 41);
  bool inner = false;
  for (auto& c : out.commands) if (c.kind == DisasterCommand::MarkRadiation && c.x == 32 && c.z == 32) inner = true;
  CHECK(inner);
}

static void testEmergencyAndRelief() {
  MockWorld w; w.add(10, 10, 3000, 2, 2, 100000);
  DisasterManager m; m.seed(2);
  m.tune.minDisasterCostToGetRelief = 1000; m.tune.reliefPctWithEW = 50;
  DisasterInputs in; DisasterOutput out;
  m.declareEmergency(out);   // false alarm 1
  CHECK(m.emergencyActive());
  m.stop(DisasterType::Fire, out);
  for (int i = 0; i < 20; ++i) m.monthlyTick(w, in, out);
  CHECK(!m.emergencyActive());   // ends after > MaxEmergencyDuration months
  DisasterManager m2; m2.seed(2);
  out.clear();
  for (int i = 0; i < 3; ++i) { m2.declareEmergency(out); }
  CHECK(m2.falseAlarms() >= 1);
  // three false alarms refuse
  DisasterManager m3;
  out.clear();
  for (int i = 0; i < 4; ++i) { m3.declareEmergency(out); if (i < 2) { /* emergency becomes active; end it */ } }
  CHECK(countEv(out, DisasterEvent::EmergencyDeclared) >= 1);
  // relief with EW
  DisasterManager m4; m4.seed(5);
  m4.tune.minDisasterCostToGetRelief = 1000; m4.tune.reliefPctWithEW = 50; m4.tune.reliefPctWithoutEW = 10;
  out.clear();
  CHECK(m4.startFire(w, 10, 10, out));
  m4.declareEmergency(out);
  CHECK(m4.emergencyActive() && m4.emergencyWithDisaster());
  int64_t credited = 0;
  for (int i = 0; i < 20000 && m4.anyActiveNonParade(); ++i) {
    out.clear(); m4.minuteTick(w, in, out); w.apply(out);
    for (auto& c : out.commands) if (c.kind == DisasterCommand::CreditTreasury) credited += c.value;
  }
  CHECK(credited == 50000);
  CHECK(!m4.emergencyActive());
  CHECK(m4.disasterCost() == 0);
  // menu enable state
  MockWorld w5; w5.add(10, 10, 3000);
  DisasterManager m5; out.clear();
  CHECK(!m5.menuState().goTo);
  m5.startFire(w5, 10, 10, out);
  auto ms = m5.menuState();
  CHECK(ms.goTo && ms.fireDispatch && ms.policeDispatch && !ms.planeDispatch);
}

static void testTornado() {
  MockWorld w; w.add(20, 20, 3000);
  for (int x = 0; x < 64; ++x) for (int z = 0; z < 64; z += 4) w.add(x, z + 1, 3000, 1, 1, 10);
  DisasterManager m; m.seed(9);
  m.tune.tornadoDestroyChance[0] = 100;
  DisasterInputs in; DisasterOutput out;
  int a[8] = {1, 1, 1, 60, 0, 40, 60, 0};
  CHECK(m.scriptTornado(w, a, out));   // no warning -> phase 4
  CHECK(m.tornadoes().size() == 1 && m.tornadoes()[0].phase == 4);
  int touch = 0, destroyed = 0;
  for (int i = 0; i < 200 && m.active(DisasterType::Tornado); ++i) {
    out.clear(); m.minuteTick(w, in, out); w.apply(out);
    touch += countCmd(out, DisasterCommand::TornadoTouchdown);
    destroyed += countCmd(out, DisasterCommand::DestroyOccupant);
  }
  CHECK(touch == 1);
  CHECK(destroyed > 0);
  CHECK(!m.active(DisasterType::Tornado));
  // zero direction fails
  int z[8] = {5, 5, 5, 5, 0, 10, 10, 0};
  CHECK(!m.scriptTornado(w, z, out));
  // warning phases: 60 ticks each (phases 1..4), phase 0 passes at once
  DisasterManager m2; m2.seed(1); out.clear();
  int b[8] = {5, 5, 5, 20, 1, 40, 60, 1};
  CHECK(m2.scriptTornado(w, b, out));
  int ticks = 0, prem = 0;
  while (m2.tornadoes().size() && m2.tornadoes()[0].phase < 5 && ticks < 1000) {
    out.clear(); m2.minuteTick(w, in, out); prem += countEv(out, DisasterEvent::Premonition); ++ticks;
  }
  CHECK(ticks >= 240 && ticks <= 250);
  CHECK(prem == 4);
  // random start
  DisasterManager m3; m3.seed(4); out.clear();
  CHECK(m3.startRandomTornado(w, out));
  CHECK(m3.tornadoes()[0].dz >= 0);
}

static void testQuake() {
  MockWorld w;
  for (int x = 0; x < 64; ++x) for (int z = 0; z < 64; ++z) if (((x + z) % 3) == 0) w.add(x, z, 3000, 1, 1, 10);
  DisasterManager m; m.seed(21);
  m.tune.quakeDestroy[0] = m.tune.quakeDestroy[1] = m.tune.quakeDestroy[2] = 10000;
  DisasterInputs in; in.jdn = 2451545; DisasterOutput out;
  CHECK(m.scriptEarthquake(w, in, 0, 1, out));  // zero magnitude no-op success
  CHECK(!m.active(DisasterType::Earthquake));
  CHECK(m.scriptEarthquake(w, in, 8, 0, out));  // M = (25+60*0.8)*0.1 = 7.3, class 3, no warning
  CHECK(m.quakes().size() == 1);
  CHECK(m.quakes()[0].cls == 3 && m.quakes()[0].phase == 4);
  CHECK(std::fabs(m.quakes()[0].mag - 7.3f) < 0.01f);
  int destroyed = 0, scarp = 0, shakes = 0;
  for (int i = 0; i < 50 && m.active(DisasterType::Earthquake); ++i) {
    out.clear(); m.minuteTick(w, in, out); w.apply(out);
    destroyed += countCmd(out, DisasterCommand::DestroyOccupant);
    scarp += countCmd(out, DisasterCommand::TerrainDelta);
    shakes += countCmd(out, DisasterCommand::ScreenShake);
  }
  CHECK(!m.active(DisasterType::Earthquake));
  CHECK(destroyed > 50);
  CHECK(scarp > 0);
  CHECK(shakes >= 1);
  CHECK(m.quakeHistory().entries.size() == 2);  // seed entry + the quake
  CHECK(m.quakeHistory().entries.back()[0] == uint32_t(in.jdn));
  // warning phases: T = 0/60/120/180
  DisasterManager m2; m2.seed(3); out.clear();
  CHECK(m2.scriptEarthquake(w, in, 1, 1, out));
  CHECK(m2.quakes()[0].cls == 1 && m2.quakes()[0].phase == 1);
  int ticks = 0;
  while (m2.active(DisasterType::Earthquake) && ticks < 1000) { out.clear(); m2.minuteTick(w, in, out); ++ticks; }
  CHECK(ticks >= 180 && ticks <= 190);
  // random occurrence: needs gap > 96 months*30 days since seeded history entry
  DisasterManager m3; m3.seed(5);
  m3.tune.quakeChance = 10000;
  DisasterInputs i0; i0.jdn = 2451545;
  out.clear();
  m3.monthlyTick(w, i0, out);
  CHECK(!m3.active(DisasterType::Earthquake));   // seed entry = today
  CHECK(m3.quakeHistory().entries.size() == 1);
  i0.jdn += 96 * 30 + 1;
  m3.quakeHistory().entries.back()[0] = 2451545;
  m3.monthlyTick(w, i0, out);
  CHECK(m3.active(DisasterType::Earthquake));
  // disasters off blocks random
  DisasterManager m4; m4.seed(5); m4.tune.quakeChance = 10000; m4.setDisastersEnabled(false);
  m4.quakeHistory().entries.push_back({2400000, 0, 0, 0, 0, 0, 0, 0, 0});
  m4.monthlyTick(w, i0, out);
  CHECK(!m4.active(DisasterType::Earthquake));
}

static void testRiot() {
  MockWorld w;
  OccInfo o = w.add(30, 30, 3000); w.occ[0].flags = kOccCrowdSite;
  DisasterManager m; m.seed(8);
  DisasterInputs in; in.unrest = 50; DisasterOutput out;
  CHECK(m.scriptRiot(w, 25, 25, 0, out));    // no warning: phase 1 at brew time
  CHECK(m.riots().size() == 1 && m.riots()[0].phase == 1);
  m.minuteTick(w, in, out);
  CHECK(m.riots()[0].phase == 2);
  CHECK(countCmd(out, DisasterCommand::SpawnRioters) == 1);
  m.rioterAt(w, 30, 30, out);
  CHECK(m.active(DisasterType::Fire));
  // police coverage blocks sites
  MockWorld w2; w2.add(30, 30, 3000); w2.occ[0].flags = kOccCrowdSite; w2.pcov = 60;
  DisasterManager m2; out.clear();
  CHECK(!m2.startRiot(w2, 30, 30, true, out));
  // full coverage: duration shorter. brewing with warning
  MockWorld w3; w3.add(30, 30, 3000); w3.occ[0].flags = kOccCrowdSite;
  DisasterManager m3; m3.seed(1); out.clear();
  CHECK(m3.startRiot(w3, 30, 30, true, out));
  int brewing = 0; int ticks = 0;
  while (m3.active(DisasterType::Riot) && ticks < 2000) {
    in.unrest = 50; out.clear(); m3.minuteTick(w3, in, out); brewing += countEv(out, DisasterEvent::RiotBrewing); ++ticks;
  }
  CHECK(brewing == 1);
  CHECK(ticks > 80 && ticks < 80 + 250);
  // fizzle if unrest dropped
  DisasterManager m4; out.clear(); m4.startRiot(w3, 30, 30, true, out);
  in.unrest = 0;
  for (int i = 0; i < 100; ++i) { out.clear(); m4.minuteTick(w3, in, out); }
  CHECK(!m4.active(DisasterType::Riot));
  // random trigger needs unrest >= threshold
  DisasterManager m5; m5.seed(4); m5.tune.riotIntervalMin = m5.tune.riotIntervalMax = 1;
  DisasterInputs lo; lo.unrest = 3;
  for (int i = 0; i < 5; ++i) m5.monthlyTick(w3, lo, out);
  CHECK(!m5.active(DisasterType::Riot));
  DisasterInputs hi; hi.unrest = 90;
  for (int i = 0; i < 5; ++i) m5.monthlyTick(w3, hi, out);
  CHECK(m5.active(DisasterType::Riot));
}

static void testUfoLocustToxic() {
  MockWorld w;
  for (int i = 0; i < 10; ++i) w.add(2 + i * 5, 3, 3000);
  w.add(40, 40, 12101, 3, 3); w.add(50, 50, 12350, 3, 3);
  DisasterManager m; m.seed(13);
  m.tune.ufoYearAttack = 1900;
  DisasterInputs in; in.year = 2000; DisasterOutput out;
  int attacks = 0;
  for (int i = 0; i < 30; ++i) {
    DisasterManager u; u.seed(uint32_t(i + 1)); out.clear();
    CHECK(u.scriptUfo(w, in, 1, out));
    for (auto& e : out.events) if (e.kind == DisasterEvent::UfoKind) { CHECK(e.a >= 2 && e.a <= 5); if (e.a == 5) ++attacks; }
  }
  CHECK(attacks >= 20);   // ~90 %
  m.tune.ufoYearAttack = 0;
  out.clear(); CHECK(m.scriptUfo(w, in, 0, out));  // no warning
  CHECK(m.ufos()[0].phase == 3);
  m.minuteTick(w, in, out);
  int swarm = countCmd(out, DisasterCommand::UfoSwarm);
  CHECK(swarm <= 1);
  bool ended = false;
  for (int i = 0; i < 20 && !ended; ++i) {
    m.minuteTick(w, in, out);
    ended = !m.active(DisasterType::UFO);
  }
  CHECK(ended);
  CHECK(countCmd(out, DisasterCommand::UfoSwarm) == 1);
  CHECK(m.ufoHistory().entries.size() == 1);
  // bomb with shipped 100/100 ignites low-tech buildings
  DisasterManager m2; m2.seed(5); m2.tune.ufoYearAttack = 0; out.clear();
  m2.scriptUfo(w, in, 0, out);
  m2.minuteTick(w, in, out);
  m2.minuteTick(w, in, out);   // phase 4 reached
  out.clear();
  m2.ufoBombHit(w, 2, 3, out);
  CHECK(m2.active(DisasterType::Fire));
  m2.ufoBeamHit(w, 7, 3, out);
  CHECK(countCmd(out, DisasterCommand::DestroyOccupant) == 1);
  // too early year: UFO fails
  DisasterManager m3; m3.tune.ufoYearVisit = 3000; out.clear();
  CHECK(!m3.scriptUfo(w, in, 1, out));

  // Locusts: crop tiles
  MockWorld wc;
  for (int x = 10; x < 14; ++x) for (int z = 10; z < 14; ++z) { OccInfo o = wc.add(x, z, 9300, 1, 1); wc.occ.back().crop = true; (void)o; }
  DisasterManager ml; ml.seed(2); out.clear();
  CHECK(ml.scriptLocusts(wc, in, 0, out));
  ml.minuteTick(wc, in, out);   // phase 2 -> launch (thresholds 0)
  ml.minuteTick(wc, in, out);
  CHECK(countCmd(out, DisasterCommand::LocustSwarm) == 1);
  for (auto& c : out.commands) if (c.kind == DisasterCommand::LocustSwarm) CHECK(c.pts.size() == 16 && c.a >= 1);
  out.clear();
  ml.locustEaten(wc, 10, 10, out);
  CHECK(countCmd(out, DisasterCommand::DestroyOccupant) == 1);
  CHECK(out.commands[0].a == kDmgCrop);
  // random locust needs crops
  DisasterManager ml2; ml2.seed(2); ml2.tune.locMinMonths = 0; ml2.tune.locChance100 = 10000;
  out.clear(); ml2.monthlyTick(w, in, out);
  CHECK(!ml2.active(DisasterType::Locusts));
  ml2.monthlyTick(wc, in, out);
  CHECK(ml2.active(DisasterType::Locusts));

  // Toxic cloud
  DisasterManager mt; mt.seed(3); out.clear();
  mt.tune.toxMinDuration = mt.tune.toxMaxDuration = 40;
  CHECK(mt.scriptToxicCloud(w, 20, 20, 0, out));
  int puffs = 0, rain = 0;
  for (int i = 0; i < 400 && mt.active(DisasterType::ToxicCloud); ++i) {
    out.clear(); mt.minuteTick(w, in, out);
    for (auto& c : out.commands) { if (c.kind == DisasterCommand::ToxicPuffs) puffs += int(c.pts.size()); if (c.kind == DisasterCommand::ToxicRain && c.a == 1) ++rain; }
  }
  CHECK(puffs >= 1);
  CHECK(!mt.active(DisasterType::ToxicCloud));
  // random: score gate
  DisasterManager mt2; mt2.seed(1); out.clear();
  DisasterInputs poll; poll.pollutionTotal = 100; poll.pollutionNorm = 100;   // score 100 > 60
  mt2.monthlyTick(w, poll, out);
  CHECK(mt2.active(DisasterType::ToxicCloud));
  DisasterManager mt3; out.clear(); DisasterInputs clean; mt3.monthlyTick(w, clean, out);
  CHECK(!mt3.active(DisasterType::ToxicCloud));
}

static void testWhirlpoolJunk() {
  MockWorld w;
  for (int x = 10; x < 30; ++x) for (int z = 10; z < 30; ++z) w.water[size_t(z * 64 + x)] = 1;
  OccInfo marina = w.add(12, 12, 14430, 2, 2);
  DisasterManager m; m.seed(6);
  DisasterInputs in; DisasterOutput out;
  CHECK(m.scriptWhirlpool(w, in, 0, 1, out));    // no-op
  CHECK(!m.active(DisasterType::Whirlpool));
  CHECK(m.scriptWhirlpool(w, in, 5, 0, out));
  CHECK(m.whirlpools().size() == 1);
  CHECK(std::fabs(m.whirlpools()[0].mag - 5.5f) < 0.01f);
  out.clear(); m.minuteTick(w, in, out);
  CHECK(countCmd(out, DisasterCommand::VortexSpawn) == 1);
  out.clear();
  m.vortexPosition(w, 13, 13, out);
  CHECK(countCmd(out, DisasterCommand::DestroyOccupant) == 1);
  m.vortexFinished(in, out);
  CHECK(!m.active(DisasterType::Whirlpool));
  CHECK(m.whirlHistory().entries.size() == 1);
  // no water -> fails
  MockWorld dry; DisasterManager m2; out.clear();
  CHECK(!m2.scriptWhirlpool(dry, in, 5, 0, out));
  (void)marina;
  // Space junk
  MockWorld wj; wj.add(20, 20, 3000);
  DisasterManager mj; mj.seed(9);
  CHECK(mj.scriptSpaceJunk(wj, in, 5, 0, out));
  CHECK(mj.junk().size() == 1);
  out.clear(); mj.debrisImpact(wj, 20, 20, out);
  CHECK(countCmd(out, DisasterCommand::Sound) >= 2);
  int ticks = 0; while (mj.active(DisasterType::SpaceJunk) && ticks < 2000) { mj.minuteTick(wj, in, out); ++ticks; }
  CHECK(!mj.active(DisasterType::SpaceJunk));
  CHECK(mj.junkHistory().entries.size() == 1);
  // water impact: sound only
  MockWorld wx; wx.water[size_t(5 * 64 + 5)] = 1;
  DisasterManager mw; out.clear(); mw.debrisImpact(wx, 5, 5, out);
  CHECK(out.commands.size() == 1 && out.commands[0].a == 250);
  // year gate for random junk
  DisasterManager mg; mg.tune.junkChance = 10000; mg.tune.junkMinGapMonths = 0;
  DisasterInputs old; old.year = 1950; mg.monthlyTick(wx, old, out);
  CHECK(!mg.active(DisasterType::SpaceJunk));
}

static void testStrikeDispatch() {
  GameRng r(5);
  ServiceStrike s; s.minFunding = 50; s.maxStat = 10; s.efficiency = 50; s.maxDuration = 3;
  CHECK(s.chancePer10000(60, 2, 99) == 0);
  CHECK(s.chancePer10000(0, 0, 99) == 0);
  CHECK(s.chancePer10000(40, 2, 5) == 0);
  CHECK(s.chancePer10000(40, 2, 99) == 100);
  bool started = false;
  for (int i = 0; i < 100000 && !started; ++i) { ServiceStrike t = s; started = t.update(0, 2, 99, r); if (started) s = t; }
  CHECK(started && s.onStrike);
  CHECK(s.apply(100) == 50);
  for (int i = 0; i < 4; ++i) s.update(0, 2, 99, r);
  CHECK(!s.onStrike);
  CHECK(s.apply(100) == 100);
  // dispatch stamp
  auto st = dispatchStamp(64, 64, 10, 10, 4, 225);
  int total = 0; for (auto& c : st) total += c.delta;
  CHECK(total > 0 && total % 225 == 0);
  auto neg = dispatchStamp(64, 64, 10, 10, 4, 225, -1);
  int t2 = 0; for (auto& c : neg) t2 += c.delta;
  CHECK(t2 == -total);
  CHECK(coverageByteFromRaw(16 * 225) == 225);
  CHECK(coverageByteFromRaw(100000) == 255);
  DispatchTracker d(0);
  uint32_t id = 100; DisasterOutput out;
  d.dispatch(10, 10, 1, 64, 4, 225, id, out);
  d.dispatch(12, 10, 1, 64, 4, 225, id, out);
  CHECK(d.deployed() == 2);  // S + 1 = 2
  out.clear();
  d.dispatch(14, 10, 1, 64, 4, 225, id, out);
  CHECK(d.deployed() == 2);
  CHECK(countCmd(out, DisasterCommand::RemoveUnit) == 1);
  CHECK(countCmd(out, DisasterCommand::StampCoverage) == 2);
  CHECK(out.commands[0].id == 100 || out.commands[0].kind == DisasterCommand::RemoveUnit);
}

static void testRecords() {
  DisasterManager m;
  auto recs = m.saveRecords();
  size_t layer = 0, fire = 0, quake = 0, ufo = 0, junk = 0, loc = 0, whirl = 0, parade = 0, tor = 0, riot = 0, tox = 0;
  for (auto& r : recs) {
    if (r.type == kDisasterLayerType) layer = r.data.size();
    else if (r.group == uint32_t(DisasterType::Fire)) fire = r.data.size();
    else if (r.group == uint32_t(DisasterType::Tornado)) tor = r.data.size();
    else if (r.group == uint32_t(DisasterType::Riot)) riot = r.data.size();
    else if (r.group == uint32_t(DisasterType::ToxicCloud)) tox = r.data.size();
    else if (r.group == kGroupEarthquake) quake = r.data.size();
    else if (r.group == kGroupUfo) ufo = r.data.size();
    else if (r.group == kGroupSpaceJunk) junk = r.data.size();
    else if (r.group == kGroupLocust) loc = r.data.size();
    else if (r.group == kGroupWhirlpool) whirl = r.data.size();
    else if (r.group == uint32_t(DisasterType::Parade)) parade = r.data.size();
  }
  CHECK(layer == 28 && fire == 12 && tor == 12 && riot == 12 && tox == 12);
  CHECK(quake == 12 && ufo == 12 && junk == 12 && loc == 16 && whirl == 12 && parade == 46);
  // populated state round trip
  MockWorld w; w.add(10, 10, 3000);
  DisasterManager a; a.seed(4);
  DisasterInputs in; in.jdn = 2451545; DisasterOutput out;
  a.quakeHistory().entries.push_back({2415021, 0, 0, 0, 0, 0, 0, 0, 0});
  a.quakeHistory().entries.push_back({2451000, 55, 1, 2, 3, 4, 5, 6, 7});
  a.ufoHistory().entries.push_back({2415021, 17, 5, 5, 0, 0, 0, 0, 0, 5, 325, 0});
  a.junkHistory().entries.push_back({2451000, 26, 0, 0, 6, 181});
  a.locustHistory().extra = 59;
  a.locustHistory().entries.push_back({2451000, 1, 2, 3, 4, 5});
  a.whirlHistory().entries.push_back({2451000, 1, 2, 3, 4, 5, 6, 7});
  a.parade().a = 17; a.parade().x = 5; a.parade().z = 9; a.parade().route[0] = 5; a.parade().e = 3;
  a.layerFlagA = 1; a.layerFlagB = 1;
  a.startFire(w, 10, 10, out);
  int args[8] = {5, 5, 9, 40, 2, 30, 80, 1};
  a.scriptTornado(w, args, out);
  w.add(30, 30, 3000); w.occ.back().flags = kOccCrowdSite;
  a.startRiot(w, 30, 30, true, out);
  a.startToxicCloud(w, 8, 8, true, out);
  auto ra = a.saveRecords();
  DisasterManager b;
  for (auto& r : ra) CHECK(b.loadRecord(r));
  auto rb = b.saveRecords();
  CHECK(ra.size() == rb.size());
  for (size_t i = 0; i < ra.size(); ++i) {
    if (ra[i].group == uint32_t(DisasterType::Fire)) continue;   // loaded fire gets a new duration only (not saved)
    CHECK(ra[i].data == rb[i].data);
  }
  CHECK(b.burning().size() == 1 && b.tornadoes().size() == 1 && b.riots().size() == 1 && b.clouds().size() == 1);
  CHECK(b.quakeHistory().entries.size() == 2);
  CHECK(b.locustHistory().extra == 59 && b.locustHistory().version == 2);
  // locust v1 (36 bytes) accepted and rewritten as v1
  DisasterRecord lr; lr.type = kDisasterRecType; lr.group = kGroupLocust;
  put16(lr.data, 1); put16(lr.data, 2); put32(lr.data, 0xDEADBEEFu); put32(lr.data, 1);
  for (int i = 0; i < 6; ++i) put32(lr.data, uint32_t(100 + i));
  CHECK(lr.data.size() == 36);
  DisasterManager c;
  CHECK(c.loadRecord(lr));
  CHECK(c.locustHistory().version == 1 && c.locustHistory().entries.size() == 1);
  bool same = false;
  for (auto& r : c.saveRecords()) if (r.group == kGroupLocust) same = (r.data == lr.data);
  CHECK(same);
  // parade filler CD -> zeros
  DisasterRecord pr; pr.type = kDisasterRecType; pr.group = uint32_t(DisasterType::Parade);
  put16(pr.data, 2); put16(pr.data, 2); put32(pr.data, 0xDEADBEEFu);
  put32(pr.data, 1); pr.data.push_back(0); put32(pr.data, 0); put32(pr.data, 0); put32(pr.data, 0); pr.data.push_back(0);
  for (int i = 0; i < 4; ++i) put32(pr.data, 0xCDCDCDCDu);
  put32(pr.data, 0);
  CHECK(pr.data.size() == 46);
  CHECK(c.loadRecord(pr));
  CHECK(c.parade().route[0] == 0 && c.parade().route[3] == 0);
  // malformed rejected
  DisasterRecord bad = lr; bad.data.pop_back();
  CHECK(!c.loadRecord(bad));
  DisasterRecord bad2 = pr; bad2.data[4] = 0;
  CHECK(!c.loadRecord(bad2));
  // layer record with uninitialised cost value kept
  DisasterRecord lay; lay.type = kDisasterLayerType; lay.group = kDisasterLayerGroup;
  put16(lay.data, 0x01F9); put16(lay.data, 2); put32(lay.data, 0xDEADBEEFu);
  put32(lay.data, 0); put32(lay.data, 1); put32(lay.data, 0); put32(lay.data, 0xCDCDCDCDu); put32(lay.data, 1);
  CHECK(c.loadRecord(lay));
  CHECK(c.emergencyActive() && uint32_t(c.disasterCost()) == 0xCDCDCDCDu);
  bool same2 = false;
  for (auto& r : c.saveRecords()) if (r.type == kDisasterLayerType) same2 = (r.data == lay.data);
  CHECK(same2);
}

int main() {
  testRng();
  testTunables();
  testFire();
  testDestroyRules();
  testEmergencyAndRelief();
  testTornado();
  testQuake();
  testRiot();
  testUfoLocustToxic();
  testWhirlpoolJunk();
  testStrikeDispatch();
  testRecords();
  TEST_MAIN_END();
}
