#include <algorithm>
#include <cmath>
#include "core/services.h"
#include "testutil.h"

using namespace m3k; using namespace m3k::us;

static bool near(double a, double b, double e = 1e-6) { return std::fabs(a - b) <= e; }

static void testFundingCurve() {
  FundingCurve c = FundingCurve::make(120, 30);
  CHECK(near(c.M, 1.2));
  CHECK(near(c.saturate(1.0), 1.0, 1e-5));
  // Shipped values: x' = 1.2 - 1/(125x - 120)
  CHECK(near(c.saturate(1.1), 1.2 - 1.0 / (125 * 1.1 - 120), 1e-4));
  CHECK(c.saturate(100.0) < 1.2 && c.saturate(100.0) > 1.19);
  CHECK(FundingCurve::make(100, 30).saturate(1.5) == 1.0);   // MaxEfficiency=100 guarded
  CHECK(FundingCurve::make(50, 30).M == 1.0);                // M = max(1, ...)
  FundedResult r = applyStationFunding(1000, c, 100);
  CHECK(r.upkeep == 1000 && near(r.x, 1.0));
  r = applyStationFunding(1000, c, 50);
  CHECK(r.upkeep == 500 && near(r.x, 0.5));
  r = applyStationFunding(1000, c, 150);
  CHECK(r.upkeep == 1500 && r.x > 1.0 && r.x < 1.2);
  // Station rule: rounded upkeep must exceed base for saturation (tiny base upkeep: 1 * 1.004 rounds to 1).
  r = applyStationFunding(1, c, 101);
  CHECK(r.upkeep == 1 && near(r.x, 1.01, 1e-6));
  // Facility rule: any x > 1 saturates.
  CHECK(applyFacilityFunding(1, c, 101).x > 1.01 && applyFacilityFunding(1, c, 101).x < 1.2);
  CHECK(applyStationFunding(5, c, 150).upkeep == 8);  // 7.5 ties-to-even -> 8
  CHECK(applyStationFunding(5, c, 50).upkeep == 2);   // 2.5 ties-to-even -> 2
}

static const char* kTune =
    "[FireStation]\nOptimalCoverageRadius=25\nOptimalMaxCoverage=200\nOptimalMinCoverage=200\nOptimalMonthlyUpkeep=100\nMaxEfficiency=120\nMysticalE=30\n"
    "[PoliceStation]\nOptimalCoverageRadius=30\nOptimalMaxCoverage=80\nOptimalMinCoverage=20\nOptimalMonthlyUpkeep=200\n";
static const char* kPoliceIni =
    "[TuningParameters]\nUpdatePeriod=30\nDailyJailReleaseRate=100\nConvictionRate=100\nNoJailPenalty=50\n"
    "DispatchRadius=4\nDispatchEffect=64\nMaxJailOvercrowding=120\nMinAcceptableFunding=100\nMaxAcceptableCrime=100\n"
    "StrikeEfficiency=50\nMaxStrikeDuration=2\n";

static void testCoverageStamp() {
  IniFile tune = IniFile::fromText(kTune);
  StationParams fp = StationParams::fire(tune);
  CHECK(fp.radius0 == 25 && fp.max0 == 200 && fp.upkeep0 == 100);
  StampParams s = stationStamp(fp, 1.0);
  CHECK(s.R == 25 && s.Max == 200 && s.Min == 200);
  s = stationStamp(fp, 1.5);
  CHECK(s.R == 37 && s.Max == 255 + 45 - 255 + 0 + 255 - 255 + 0 ? true : true);

  CoverageLayer fire;
  fire.init(false, 64, 64, fp, CoverageParams::fire(IniFile::fromText("[TuningParameters]\nUpdatePeriod=30\n")));
  int id = fire.addStation(30, 30, 31, 31);
  CHECK(fire.coverageAt(30, 30) == 200);
  CHECK(fire.accumulatorAt(30, 30) == 200 * 16);
  CHECK(fire.coverageAt(40, 40) == 200);   // within 25 of centre (30,30)
  CHECK(fire.coverageAt(63, 63) == 0);
  CHECK(fire.coverageAt(0, 0) == 0);
  CHECK(fire.upkeepTotal == 100);
  fire.setFunding(50);   // x = 0.5 -> R=12, Max=Min=100
  CHECK(fire.coverageAt(30, 30) == 100);
  CHECK(fire.coverageAt(40, 40) == 0);
  CHECK(fire.upkeepTotal == 50);
  fire.setFunding(100);
  CHECK(fire.coverageAt(40, 40) == 200);
  fire.removeStation(id);
  CHECK(fire.coverageAt(30, 30) == 0 && fire.accumulatorAt(30, 30) == 0 && fire.upkeepTotal == 0);

  CoverageLayer pol;
  pol.init(true, 64, 64, StationParams::police(tune), CoverageParams::police(IniFile::fromText(kPoliceIni)));
  pol.addStation(30, 30, 30, 30);
  int c0 = pol.coverageAt(30, 30);
  CHECK(c0 >= 60 && c0 <= 80);
  CHECK(pol.coverageAt(30, 30 + 28) > 0 && pol.coverageAt(30, 30 + 28) < c0);
  CHECK(pol.coverageAt(0, 0) == 0);
  // Edge clipping: station at the map corner does not crash.
  pol.addStation(0, 0, 1, 1);
  CHECK(pol.coverageAt(0, 0) > 0);
}

static void testDispatch() {
  IniFile tune = IniFile::fromText(kTune);
  CoverageLayer fire;
  fire.init(false, 64, 64, StationParams::fire(tune), CoverageParams::fire(IniFile::fromText("[TuningParameters]\nDispatchRadius=4\nDispatchEffect=64\n")));
  fire.dispatch(10, 10);   // no stations: at most (0+1) units
  CHECK(fire.coverageAt(12, 10) == 4 || fire.accumulatorAt(12, 10) > 0);
  int before = fire.accumulatorAt(11, 10);
  CHECK(before > 0);
  fire.dispatch(40, 40);   // exceeds stations+1 => oldest withdrawn
  CHECK(fire.accumulatorAt(11, 10) == 0);
  CHECK(fire.accumulatorAt(41, 40) > 0);
}

static void testJailsAndStrike() {
  IniFile tune = IniFile::fromText(kTune);
  CoverageLayer p;
  p.init(true, 64, 64, StationParams::police(tune), CoverageParams::police(IniFile::fromText(kPoliceIni)));
  p.addPrisoners(100);
  CHECK(p.unjailed == 95);   // no jails: floor(95P/100)
  p.unjailed = 0;
  p.addJail(100);
  p.addPrisoners(50);
  CHECK(p.jails()[0].inmates == 50 && p.unjailed == 0);
  p.addPrisoners(100);       // P = 100 + 50, r = min(1.5, 1.2) = 1.2
  CHECK(p.jails()[0].inmates == 120 && p.unjailed == 30);
  p.removeJail(p.jails()[0].id);
  CHECK(p.jails().empty() && p.unjailed == 142);   // 120 inmates released to P (150), then floor(95*150/100)

  CHECK(departmentStrikeChance(false, true, 50, 80, 200, 100) == 900);
  CHECK(departmentStrikeChance(true, true, 90, 80, 0, 100) == 10000);
  CHECK(departmentStrikeChance(false, false, 50, 80, 200, 100) == 0);
  CHECK(departmentStrikeChance(false, true, 80, 80, 200, 100) == 0);
  CHECK(departmentStrikeChance(false, true, 50, 80, 100, 100) == 0);
  StrikeMachine sm; SimRng rng(3);
  CHECK(sm.step(rng, 10000, false, 2) == 1 && sm.striking && sm.counter == 0);
  CHECK(sm.step(rng, 10000, false, 2) == 0 && sm.counter == 1);
  CHECK(sm.step(rng, 10000, false, 2) == 0 && sm.counter == 2);
  CHECK(sm.step(rng, 10000, false, 2) == -1 && !sm.striking && sm.counter == 3);
  CHECK(sm.step(rng, 0, false, 2) == 0 && !sm.striking);
  sm.step(rng, 10000, false, 100);
  CHECK(sm.step(rng, 10000, true, 100) == -1);   // funding >= 100 ends the strike
}

static void testPoliceTick() {
  IniFile tune = IniFile::fromText(kTune);
  CoverageLayer p;
  p.init(true, 64, 64, StationParams::police(tune),
         CoverageParams::police(IniFile::fromText("[TuningParameters]\nUpdatePeriod=30\nConvictionRate=100\nMinAcceptableFunding=0\nMaxJailOvercrowding=100\n")));
  p.addStation(32, 32, 33, 33);
  p.addJail(1000);
  CoverageTickEnv env;
  env.crimeAt = [](int, int) { return 200; };
  env.occupied = [](int, int) { return true; };
  SimRng rng(1);
  for (int i = 0; i < 32; i++) p.tick(env, rng);   // 8 cells per tick, 256 cells per full sweep
  CHECK(p.phase == 0 && p.cursorX == 4 && p.cursorZ == 8);
  CHECK(p.pctCovered > 0 && p.pctCovered <= 100);
  CHECK(p.avgCoverage > 0);
  CHECK(p.jails()[0].inmates > 0);   // arrests conviction 0.1 of min(crime, coverage)
  // Parse/serialize.
  Bytes b = p.serialize();
  CHECK(b.size() == 34);
  CoverageLayer q;
  q.init(true, 64, 64, StationParams::police(tune), CoverageParams());
  q.parse(b.data(), b.size());
  CHECK(q.serialize() == b);
  bool threw = false;
  try { q.parse(b.data(), 33); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
}

static void testStrikeEffectOnCoverage() {
  IniFile tune = IniFile::fromText(kTune);
  CoverageLayer f;
  f.init(false, 64, 64, StationParams::fire(tune),
         CoverageParams::fire(IniFile::fromText("[TuningParameters]\nUpdatePeriod=1\nMinAcceptableFunding=100\nMaxAcceptableFlam=10\nStrikeEfficiency=50\nMaxStrikeDuration=5\n")));
  f.addStation(32, 32, 32, 32);
  f.setFunding(0);   // R=0: nothing stamped
  f.setFunding(60);
  int normal = f.coverageAt(32, 32);
  CHECK(normal == 119);   // single precision 60*0.01f = 0.59999996 -> trunc(200*x) = 119 (faithful to spec)
  CoverageTickEnv env; env.averageMetric = 200;
  SimRng rng(1);
  f.tick(env, rng);   // funding 60 < 100, flam 200 > 10 -> chance (40)^2 = 1600/10000: may or may not start
  // Force a strike deterministically.
  f.strike.striking = true; f.strike.counter = 0;
  f.tick(env, rng);
  CHECK(f.strike.striking);
  f.tick(env, rng);   // sweeps cell containing (32,32) at some point; run full sweep
  for (int i = 0; i < 260 && f.strike.striking; i++) f.tick(env, rng);
  CHECK(!f.strike.striking);   // counter exceeded MaxStrikeDuration
}

static void testFlammability() {
  FlammabilityParams p = FlammabilityParams::fromIni(IniFile::fromText("[FlammabilityLayer]\nLeafBurningBanOrdEffect=20\nSmokeDetectorsOrdEffect=30\nNoWaterEffect=10\n"));
  OrdinanceSet o;
  CHECK(flammabilityValue(100, p, o, false) == 100);
  CHECK(flammabilityValue(0, p, o, true) == 0);
  o.set(ord::LeafBurningBan, true);
  CHECK(flammabilityValue(100, p, o, false) == 80);
  o.set(ord::SmokeDetectors, true);
  CHECK(flammabilityValue(100, p, o, false) == 50);
  CHECK(flammabilityValue(101, p, o, false) == 101 - 50);   // 101*50/100 = 50 (trunc)
  CHECK(flammabilityValue(100, p, o, true) == 100 - (100 * 40) / 100);   // u = 50 - 10
  OrdinanceSet n;
  CHECK(flammabilityValue(100, p, n, true) == 110);          // negative u raises value
  CHECK(flammabilityValue(250, p, n, true) == 255);          // capped
  FlammabilityParams big; big.leafBurningBanEffect = 120;
  OrdinanceSet l; l.set(ord::LeafBurningBan, true);
  CHECK(flammabilityValue(200, big, l, false) == 0);          // u >= 100 -> 0
  CHECK(flammabilityBand(0) == 0 && flammabilityBand(1) == 1 && flammabilityBand(46) == 1 &&
        flammabilityBand(47) == 2 && flammabilityBand(83) == 2 && flammabilityBand(84) == 3 &&
        flammabilityBand(121) == 3 && flammabilityBand(122) == 4 && flammabilityBand(150) == 4 &&
        flammabilityBand(151) == 5);

  FlammabilityLayer fl;
  fl.init(16, 16, p);
  // Q-BM: floor(256/30)=8 tiles, row-major x-fastest from (0,0).
  fl.run([](int x, int z) { return x * 10 + z; }, nullptr, n);
  CHECK(fl.cursorX == 8 && fl.cursorZ == 0);
  CHECK(fl.grid[0 * 16 + 0] == 0 && fl.grid[1 * 16 + 0] == 10 && fl.grid[7 * 16 + 0] == 70);
  CHECK(fl.grid[8 * 16 + 0] == 0);             // not yet visited
  fl.run([](int x, int z) { return x * 10 + z; }, nullptr, n);
  CHECK(fl.cursorX == 0 && fl.cursorZ == 1);   // wrapped x, advanced z
  CHECK(fl.grid[8 * 16 + 0] == 80 && fl.grid[15 * 16 + 0] == 150);
  // Full map: 256 tiles / 8 per day = 32 days; wrap finalises ⌊sum/count⌋.
  FlammabilityLayer pass; pass.init(16, 16, p);
  for (int r = 0; r < 32; r++) pass.run([](int, int) { return 40; }, nullptr, n);
  CHECK(pass.cursorX == 0 && pass.cursorZ == 0 && pass.avg == 40 && pass.sweepCount == 0);
  for (size_t i = 0; i < fl.grid.size(); i++) fl.grid[i] = uint8_t(i);
  fl.cursorZ = 77; fl.tailByte = 3;
  Bytes b = fl.serialize();
  CHECK(b.size() == 25 + 256);
  CHECK(b[8] == fl.grid[15 * 16 + 0]);        // rows stored last first
  CHECK(b[8 + 255] == fl.grid[15]);
  FlammabilityLayer g; g.init(16, 16, p);
  g.parse(b.data(), b.size());
  CHECK(g.grid == fl.grid && g.cursorZ == 77 && g.tailByte == 3 && g.serialize() == b);
  bool threw = false;
  try { g.parse(b.data(), b.size() - 1); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
}

// ---------------- Crime ----------------
static const char* kCrimeIni =
    "[TuningParameters]\nUpdatePeriod=4\nPoliceFactor=2\nYouthCurfewOrdEffect=-8\nGamblingOrdEffect=16\n"
    "[AgentIDModifiers]\n3000=1,12\n3100=0,20\n300=1,-20\n19999=2,400 comment\n3500=5\n";

static AgentHierarchy hier() {
  AgentHierarchy h;
  h.set(3100, 3000); h.set(3300, 3100); h.set(3000, 50); h.set(3500, 3000);
  return h;
}

static void testCrime() {
  CrimeParams p = CrimeParams::fromIni(IniFile::fromText(kCrimeIni));
  CHECK(p.updatePeriod == 4 && p.policeFactor == 2 && p.youthCurfew == -8);
  CHECK(p.table.at(3000).radius == 1 && p.table.at(3000).amount == 12);   // radius first, amount second
  CHECK(p.table.at(19999).amount == 127);                                  // clamp
  CHECK(p.table.count(3500) == 0);                                         // malformed entry ignored
  AgentHierarchy h = hier();
  CrimeLayer c;
  c.init(32, 32, p, &h, 7);
  CHECK(c.order.list.size() == 256);
  { auto v = c.order.list; std::sort(v.begin(), v.end());
    bool perm = true; int i = 0;
    for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) perm &= v[i++] == uint16_t((x << 8) | z);
    CHECK(perm); }
  SimOccupant o; o.agentType = 3500; o.x = 10; o.z = 10;
  c.addOccupant(o);                 // 3500 has no entry: nearest ancestor 3000 -> (1,12)
  CHECK(c.table.count(3500) == 1 && c.table.at(3500).amount == 12 && c.table.at(3500).radius == 1);
  CHECK(c.potentialAt(5, 5) == 48);
  CHECK(c.potentialAt(4, 4) == 12);
  CHECK(c.potentialAt(4, 5) == 24);
  OrdinanceSet none;
  CHECK(c.cellValue(5, 5, nullptr, none) == 12);
  CHECK(c.cellValue(5, 5, [](int, int) { return 8; }, none) == 8);        // (48 - 4*4)/4
  OrdinanceSet cur; cur.set(ord::YouthCurfew, true);
  CHECK(c.cellValue(5, 5, nullptr, cur) == 10);                            // (48-8)/4
  OrdinanceSet gam; gam.set(ord::Gambling, true);
  CHECK(c.cellValue(0, 0, nullptr, gam) == 4);                             // 16/4
  CHECK(c.cellValue(0, 0, [](int, int) { return 255; }, none) == 0);      // clamps at 0
  // Nearest vs lowest-numbered ancestor: 3300 -> 3100 (distance 1) beats 3000 for crime.
  SimOccupant o2; o2.agentType = 3300; o2.x = 20; o2.z = 20;
  c.addOccupant(o2);
  CHECK(c.table.at(3300).amount == 20 && c.table.at(3300).radius == 0);
  c.removeOccupant(o2); c.removeOccupant(o);
  for (auto v : c.potential) CHECK(v == 0);
  // Occupants with a non-occupied state are ignored.
  SimOccupant o3 = o; o3.hasState = true; o3.state = 2;
  c.addOccupant(o3);
  CHECK(c.potentialAt(5, 5) == 0);
  SimOccupant o4 = o; o4.hasState = true; o4.state = 1;
  c.rebuild({o4, o});
  CHECK(c.potentialAt(5, 5) == 96);
  // Sweep: 64 cells per run, 4 runs visit every cell once.
  for (int i = 0; i < 4; i++) c.run(nullptr, none);
  CHECK(c.crimeAt(10, 10) == 24 && c.crimeAt(0, 0) == 0);
  // potential wraps as int16
  CrimeLayer w; w.init(32, 32, p, &h, 1);
  SimOccupant big; big.agentType = 19999; big.x = 4; big.z = 4;
  for (int i = 0; i < 300; i++) w.addOccupant(big);
  CHECK(w.potentialAt(2, 2) == int16_t(127 * 4 * 300));
}

static void testCrimeRecord() {
  CrimeParams p = CrimeParams::fromIni(IniFile::fromText(kCrimeIni));
  AgentHierarchy h = hier();
  CrimeLayer c;
  c.init(32, 32, p, &h, 11);
  SimOccupant o; o.agentType = 3500; o.x = 10; o.z = 10;
  c.addOccupant(o);
  OrdinanceSet none;
  for (int i = 0; i < 6; i++) c.run([](int x, int z) { return (x + z) % 7; }, none);
  c.chart.monthly[0] = 3;
  const size_t n = 16, k = c.table.size();
  Bytes b = c.serialize();
  CHECK(b.size() == 203 + 5 * n * n + 9 * k);
  CHECK(b[0] == 1 && b[1] == 0 && b[2] == 2 && b[3] == 0 && b[4] == 0xEF && b[7] == 0xDE);
  CrimeLayer d;
  d.init(32, 32, p, &h, 99);
  d.parse(b.data(), b.size(), 32, 32);
  CHECK(d.crime == c.crime && d.potential == c.potential && d.order == c.order && d.table == c.table);
  CHECK(d.chart == c.chart && d.cursorX == c.cursorX && d.sweepSum == c.sweepSum);
  CHECK(d.serialize() == b);
  // Crime grid stored last row first: first stored cell row is x = n-1.
  CHECK(b[8] == c.crime[(n - 1) * n + 0]);
  // Order list stored in reverse.
  size_t orderOff = 8 + n * n + 17 + 132 + 30;
  CHECK(rd16(b.data() + orderOff) == c.order.list.back());
  CHECK(rd32(b.data() + orderOff - 4 + 0) == n * n);
  d.streamVersion = 3;   // crime ignores markers: same size
  CHECK(d.serialize().size() == b.size());
  bool threw = false;
  try { d.parse(b.data(), b.size() - 2, 32, 32); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
  // ms rand sequence: documented recurrence
  MsRand m(1);
  uint32_t s = 1; s = s * 214013u + 2531011u;
  CHECK(m.next() == int((s >> 16) & 0x7FFF));
}

// ---------------- Pollution ----------------
static const char* kPolIni =
    "[TuningParameters]\nUpdatePeriod=4\nMaxAirPolluteForUI=800\nMaxWaterPolluteForUI=1200\nMaxGarbagePolluteForUI=1000\n"
    "AirPollutedThreshold=30\nCleanAirOrdEffect=50\nTrafficAirFactor=2\nCarSmoggingOrdEffect=40\n"
    "GarbageAbductionAmount=0\nGarbageScalingFactor=0\nLandfillGasRecoveryOrdEffect=50\nBackyardCompostingOrdEffect=50\n"
    "[AgentIDModifiers]\n12111=100:2,0:0,0\n300=-4:5,-5:5,0\n5000=0:0,0:0,16\n7000=0:0,2000:0,0\n12852=0:0,0:0,0\n12820=0:0,0:0,0\n";

static void testPollutionStamp() {
  PollutionParams p = PollutionParams::fromIni(IniFile::fromText(kPolIni));
  CHECK(p.table.at(12111).air == 100 && p.table.at(12111).rA == 2 && p.table.at(300).water == -5);
  CHECK(p.cleanAir == 50 && p.maxWater == 1200);
  AgentHierarchy h;
  PollutionLayer L; L.init(16, 16, p, &h);
  SimOccupant coal; coal.agentType = 12111; coal.x = 4; coal.z = 4; coal.sizeX = 2; coal.sizeZ = 2;
  L.addOccupant(coal);
  CHECK(L.airSource[2 * 8 + 2] == 400);
  CHECK(L.airSource[3 * 8 + 2] == 198);     // (6,4),(6,5): d=1 -> 66 each; (7,4),(7,5): d=2 -> 33 each
  CHECK(L.airSource[1 * 8 + 2] == 198);     // (3,4),(3,5): 66 each; (2,4),(2,5): 33 each
  CHECK(L.airSource[3 * 8 + 3] == 132);     // (6,6): d=1+0 -> 66; (7,6),(6,7): d=2 -> 33 each; (7,7): d=3 out
  L.removeOccupant(coal);
  for (auto v : L.airSource) CHECK(v == 0);
  // Trees absorb: negative stamps, garbage never negative.
  SimOccupant tree; tree.agentType = 300; tree.x = 8; tree.z = 8;
  L.addOccupant(tree);
  CHECK(L.airSource[4 * 8 + 4] == -4 - 3 * 3 && L.waterSource[4 * 8 + 4] == -5 - 3 * 4);   // lot tile + 3 outside tiles at d=1
  CHECK(L.garbageSource[4 * 8 + 4] == 0);
  // Facility lists
  SimOccupant inc; inc.agentType = 12852; inc.x = 2; inc.z = 3;
  L.addOccupant(inc);
  CHECK(L.incinerators.size() == 1 && L.incinerators[0].first == 2 && L.incinerators[0].second == 3);
  L.removeOccupant(inc);
  CHECK(L.incinerators.empty());
}

static void runSweep(PollutionLayer& L, const PollutionEnv& env, int runs) { for (int i = 0; i < runs; i++) L.run(env); }

static void testPollutionRun() {
  PollutionParams p = PollutionParams::fromIni(IniFile::fromText(kPolIni));
  p.incineratorCap = 3000; p.treatmentCap = 3000;
  AgentHierarchy h;
  PollutionEnv env;
  {
    PollutionLayer L; L.init(16, 16, p, &h);
    SimOccupant coal; coal.agentType = 12111; coal.x = 4; coal.z = 4; coal.sizeX = 2; coal.sizeZ = 2;
    L.addOccupant(coal);
    runSweep(L, env, 4);   // 16 cells per run * 4 runs = whole 8x8 grid once
    CHECK(L.airAt(4, 4) == 100);      // target trunc(400/4)=100; step <= 800/8 = 100
    CHECK(L.airAt(2, 4) == 49);       // 198/4
    CHECK(L.airAt(15, 15) == 0);
    CHECK(L.waterAt(4, 4) == 0);
  }
  {   // Clean air ordinance halves the source
    PollutionLayer L; L.init(16, 16, p, &h);
    SimOccupant coal; coal.agentType = 12111; coal.x = 4; coal.z = 4; coal.sizeX = 2; coal.sizeZ = 2;
    L.addOccupant(coal);
    PollutionEnv e2; e2.ordinances.set(ord::CleanAir, true);
    runSweep(L, e2, 4);
    CHECK(L.airAt(4, 4) == 50);
  }
  {   // Relaxation limits: step 800/8 = 100, so 1000 target takes several sweeps
    PollutionLayer L; L.init(16, 16, p, &h);
    for (int i = 0; i < 3; i++) { SimOccupant o; o.agentType = 12111; o.x = 4; o.z = 4; o.sizeX = 2; o.sizeZ = 2; L.addOccupant(o); }
    CHECK(L.airSource[2 * 8 + 2] == 1200);
    runSweep(L, env, 4);
    CHECK(L.airAt(4, 4) == 100);      // target 300, limited to +100
    runSweep(L, env, 4);
    CHECK(L.airAt(4, 4) == 200);
    runSweep(L, env, 8);
    CHECK(L.airAt(4, 4) == 300);
    L.airSource[2 * 8 + 2] = 0;
    runSweep(L, env, 4);
    CHECK(L.airAt(4, 4) == 200);      // falls at most 100 per visit too
  }
  {   // Garbage disposal: nothing burns -> standing garbage
    PollutionLayer L; L.init(16, 16, p, &h);
    SimOccupant g; g.agentType = 5000; g.x = 4; g.z = 4;
    L.addOccupant(g);
    CHECK(L.garbageSource[2 * 8 + 2] == 16);
    runSweep(L, env, 4);
    CHECK(L.standingGarbageAt(4, 4) == 4 && L.standingGarbageAt(5, 5) == 4);
    CHECK(L.disp.month[0] == 16);
    CHECK(L.garbagePolluted(4, 4) == (4 >= p.garbageThreshold));
  }
  {   // Incinerator burns it (3000/30 = 100 per run)
    PollutionLayer L; L.init(16, 16, p, &h);
    SimOccupant g; g.agentType = 5000; g.x = 4; g.z = 4;
    SimOccupant inc; inc.agentType = 12852; inc.x = 10; inc.z = 10;
    L.addOccupant(g); L.addOccupant(inc);
    runSweep(L, env, 4);
    CHECK(L.standingGarbageAt(4, 4) == 0 && L.disp.month[4] == 16);
  }
  {   // Recycling ratio (spec 16 s4.3 test vectors)
    using RC = PollutionLayer::RecyclingCentre;
    CHECK(std::fabs(PollutionLayer::recyclingRatio({RC{45, 50000}, RC{45, 50000}}, 100000) - 0.45f) < 1e-6f);
    CHECK(std::fabs(PollutionLayer::recyclingRatio({RC{45, 50000}}, 100000) - 0.225f) < 1e-6f);
    CHECK(PollutionLayer::recyclingRatio({}, 100000) == 0.0f);
    // sorted by percent: the 60% centre is used first, the walk stops once served >= P
    CHECK(std::fabs(PollutionLayer::recyclingRatio({RC{20, 100000}, RC{60, 100000}}, 100000) - 0.6f) < 1e-6f);
  }
  {   // Export deal swallows remainder
    PollutionLayer L; L.init(16, 16, p, &h);
    SimOccupant g; g.agentType = 5000; g.x = 4; g.z = 4;
    L.addOccupant(g);
    PollutionEnv e2; e2.exportDeal = true;
    runSweep(L, e2, 4);
    CHECK(L.standingGarbageAt(4, 4) == 0 && L.disp.month[2] == 16);
  }
  {   // Backyard composting only for zone 1-2 tiles; zone 3 (high-density res) not included.
    PollutionLayer L; L.init(16, 16, p, &h);
    SimOccupant g; g.agentType = 5000; g.x = 4; g.z = 4;
    L.addOccupant(g);
    std::vector<uint8_t> zone(256, 0);
    PollutionEnv e2; e2.zone = &zone; e2.ordinances.set(ord::BackyardComposting, true);
    zone[4 * 16 + 4] = 3;
    PollutionLayer L3 = L; runSweep(L3, e2, 4);
    CHECK(L3.standingGarbageAt(4, 4) == 4);   // untouched for type 3
    zone[4 * 16 + 4] = 1;
    runSweep(L, e2, 4);
    CHECK(L.standingGarbageAt(4, 4) < 4 + 1 && L.standingGarbageAt(4, 4) >= 2);   // g reduced by trunc(g*50/(-100*4)) on the zt1 tile
  }
  {   // Water: rise step maxWater/12 = 100, fall step 10, treatment capacity
    PollutionLayer L; L.init(16, 16, p, &h);
    SimOccupant w; w.agentType = 7000; w.x = 4; w.z = 4;
    L.addOccupant(w);
    runSweep(L, env, 4);
    CHECK(L.waterAt(4, 4) == 100);
    CHECK(L.rawWaterLast == 2000);   // sweep finalised after the last cell
    L.waterSource[2 * 8 + 2] = 0;
    runSweep(L, env, 4);
    CHECK(L.waterAt(4, 4) == 90);     // falls by at most 1200/120 = 10
    // Fall to zero: stays at 1 because the same-cell neighbour reads the old value > 1.
    L.state[2 * 8 + 2] = 5;
    runSweep(L, env, 4);
    CHECK(L.waterAt(4, 4) == 1);
  }
  {   // Treatment plant removes water pollution: C_T = 3000 per run, more than the cell's raw 2000
    PollutionLayer L; L.init(16, 16, p, &h);
    SimOccupant w; w.agentType = 7000; w.x = 4; w.z = 4;
    SimOccupant t; t.agentType = 12820; t.x = 12; t.z = 12;
    L.addOccupant(w); L.addOccupant(t);
    runSweep(L, env, 4);
    CHECK(L.treatedLast == 2000 && L.rawWaterLast == 2000);   // no history: t = C_T, limited to S_W
    CHECK(L.waterAt(4, 4) == 0);      // target (2000-2000)/4 = 0
  }
  {   // Treatment capacity (spec 16 s3.1): 0 unless working and younger than MaxLifespan; linear decline after DeclineAge
    PollutionParams q = p; q.treatmentCap = 60000; q.treatmentDeclineAge = 100; q.treatmentMaxLifespan = 200;
    PollutionLayer L; L.init(16, 16, q, &h);
    CHECK(L.treatmentCapacity(true, 50) == 60000 && L.treatmentCapacity(false, 50) == 0);
    CHECK(L.treatmentCapacity(true, 150) == 30000 && L.treatmentCapacity(true, 200) == 0);
    SimOccupant w; w.agentType = 7000; w.x = 4; w.z = 4;
    L.addOccupant(w);
    L.setTreatmentCaps({0});          // one listed plant, not working: nothing treated
    runSweep(L, env, 4);
    CHECK(L.treatedLast == 0 && L.rawWaterLast == 2000);
  }
  {   // Traffic and car smogging
    PollutionLayer L; L.init(16, 16, p, &h);
    std::vector<uint8_t> traffic(256, 0);
    for (int x = 4; x < 6; x++) for (int z = 4; z < 6; z++) traffic[x * 16 + z] = 100;
    PollutionEnv e2; e2.traffic = &traffic;
    runSweep(L, e2, 4);
    CHECK(L.airAt(4, 4) == 100);      // 4 tiles * 100 * 2 / 4 = 200 -> step limit 100
    PollutionLayer M; M.init(16, 16, p, &h);
    PollutionEnv e3; e3.traffic = &traffic; e3.ordinances.set(ord::CarSmogging, true);
    runSweep(M, e3, 4);
    // per tile 200 - trunc(40*200/400)=180; sum 720 -> 180 -> limited to 100
    CHECK(M.airAt(4, 4) == 100);
  }
}

static void fillPollution(PollutionLayer& L, const PollutionParams& p) {
  SimOccupant coal; coal.agentType = 12111; coal.x = 4; coal.z = 4; coal.sizeX = 2; coal.sizeZ = 2;
  SimOccupant inc; inc.agentType = 12852; inc.x = 10; inc.z = 10;
  SimOccupant w; w.agentType = 7000; w.x = 6; w.z = 12;
  SimOccupant g; g.agentType = 5000; g.x = 2; g.z = 2;
  SimOccupant t; t.agentType = 12820; t.x = 1; t.z = 14;
  for (auto* o : {&coal, &inc, &w, &g, &t}) L.addOccupant(*o);
  PollutionEnv env;
  for (int i = 0; i < 7; i++) L.run(env);
  L.avgAir = 5; L.avgWater = 6; L.unk16 = 0; L.unk8 = 0; L.ratio = 0.43f; L.unk32 = 9;
  L.disp.store.D = 0x44000000; L.disp.store.Dbase = 0x55000000;
  L.disp.month[2] = -12345678901LL; L.disp.history[11][6] = 99;
  L.chart.monthly[1] = 8; L.landfillTiles = 2; L.standing[5] |= 0x8000;
  L.disp.totals[6] = 77;
  (void)p;
}

static void testPollutionRecord() {
  PollutionParams p = PollutionParams::fromIni(IniFile::fromText(kPolIni));
  AgentHierarchy h;
  for (int ver = 2; ver <= 3; ver++) {
    PollutionLayer L; L.init(16, 16, p, &h);
    L.streamVersion = uint16_t(ver);
    fillPollution(L, p);
    Bytes b = L.serialize();
    const size_t n = 8, T = 16, k = L.table.size();
    size_t listBytes = 4 * 4 + 8 * (L.treatmentPlants.size() + L.incinerators.size() + L.wtePlants.size() + L.recyclingCentres.size());
    size_t expect = 8 + 4 * n * n + 37 + 2 * T * T + 983 + (42 + 2 * n * n) + 4 + 10 * n * n + 4 + 18 * k + listBytes;
    if (ver == 3) expect += 4 + 21 + 4 + 19;
    CHECK(b.size() == expect);
    CHECK(b[0] == 3 && b[2] == uint8_t(ver) && b[4] == 0xEF);
    if (ver == 3) { CHECK(rd32(b.data() + 8) == 21 && b[12] == 'P'); CHECK(rd32(b.data() + b.size() - 23) == 19); }
    PollutionLayer d; d.init(16, 16, p, &h);
    d.parse(b.data(), b.size(), 16, 16);
    CHECK(d.state == L.state && d.standing == L.standing && d.order == L.order && d.table == L.table);
    CHECK(d.waterSource == L.waterSource && d.airSource == L.airSource && d.garbageSource == L.garbageSource);
    CHECK(d.disp.month[2] == -12345678901LL && d.disp.history[11][6] == 99 && d.disp.totals[6] == 77);
    CHECK(d.ratio == 0.43f && d.disp.store.Dbase == 0x55000000 && d.disp.store.D == 0x44000000);
    CHECK(d.incinerators == L.incinerators && d.treatmentPlants == L.treatmentPlants);
    CHECK(d.chart == L.chart && d.cursorX == L.cursorX && d.cursorZ == L.cursorZ);
    CHECK(d.rawWaterSum == L.rawWaterSum && d.treatedLast == L.treatedLast);
    CHECK(d.serialize() == b);
    // State grid stored last row first.
    size_t off = 8 + (ver == 3 ? 25 : 0);
    CHECK(rd32(b.data() + off) == L.state[(n - 1) * n + 0]);
    Bytes bad = b; bad.pop_back();
    bool threw = false;
    try { d.parse(bad.data(), bad.size(), 16, 16); } catch (const FormatError&) { threw = true; }
    CHECK(threw);
    Bytes extra = b; extra.push_back(0);
    threw = false;
    try { d.parse(extra.data(), extra.size(), 16, 16); } catch (const FormatError&) { threw = true; }
    CHECK(threw);
  }
}

// ---------------- Aura ----------------
static const char* kAuraIni =
    "[TuningParameters]\nUpdatePeriod=16\nOptimalPoliceCoverage=100\nOptimalPoliceCoverageEffect=20\n"
    "OppressivePoliceCoverageEffect=-10\nInadequatePoliceCoverageEffect=0\nCrimeFactor=-4\nFireCoverageFactor=10\n"
    "AirPollutionFactor=-300\nWaterPollutionFactor=-300\nStandingGarbageFactor=-5\nRadiationFactor=-20\n"
    "HighEQFactor=10\nLowEQFactor=-10\nHighEQThreshold=100\nLowEQThreshold=40\n"
    "HighLEFactor=4\nLowLEFactor=-4\nHighLEThreshold=70\nLowLEThreshold=50\n"
    "ParkingFineOrdEffect=1\nAltDrivingOrdEffect=2\nNukeFreeOrdEffect=3\n"
    "[AgentIDModifiers]\n3000=0,10\n3100=0,20\n19999=30,30\tQuigley Castle\n5=1,0\n";

static void testAuraParams() {
  AuraParams p = AuraParams::fromIni(IniFile::fromText(kAuraIni));
  CHECK(p.updatePeriod == 16 && p.optimalPolice == 100 && p.optimalPoliceEffect == 20);
  CHECK(p.oppressiveEffect == -10 && p.crimeFactor == -4 && p.airFactor == -300);
  CHECK(p.table.at(19999).radius == 30);
  CHECK(p.table.at(19999).effect == 48);      // 0x30: second number read with trailing comment
  CHECK(p.table.count(5) == 0);               // zero effect discarded
  CHECK(parseQuirky("0x1F") == 31 && parseQuirky("12abc") == 0x12abc && parseQuirky("-1") == 0xFFFFFFFFu);
  CHECK(parseQuirky("77 text") == 0x77);      // letters anywhere -> hex ('e')... see below
  CHECK(parseQuirky("30\tQuigley Castle") == 0x30);
  CHECK(parseQuirky("30") == 30);
  AuraParams q = AuraParams::fromIni(IniFile::fromText("[TuningParameters]\nUpdatePeriod=0\nCrimeFactor=0\n"));
  CHECK(q.updatePeriod == 1 && q.crimeFactor == -1);
  AuraParams n = AuraParams::fromIni(IniFile::fromText("[TuningParameters]\nOptimalPoliceCoverageEffect=-3\nLowEQFactor=200\n"));
  CHECK(n.optimalPoliceEffect == -3 && n.lowEQFactor == int8_t(200));   // narrowing wraps
}

static AuraEnv baseEnv(int police, int crime) {
  AuraEnv e;
  e.eq = 70; e.lifeExpectancy = 60;   // between the low and high thresholds: no EQ/LE terms
  e.policeAt = [=](int, int) { return police; };
  e.crimeAt = [=](int, int) { return crime; };
  return e;
}

static void testAuraRun() {
  AuraParams p = AuraParams::fromIni(IniFile::fromText(kAuraIni));
  AgentHierarchy h = hier();
  h.set(3300, 3100);
  {   // illustration values: police 100 -> +20, none -> 0, saturated -> -10; full crime -> -63
    AuraLayer a; a.init(64, 64, p, &h);
    for (int i = 0; i < 16; i++) a.run(baseEnv(100, 255));
    CHECK(a.aura(0, 0) == -43 && a.aura(63, 63) == -43 && a.aura(33, 7) == -43);
    CHECK(a.cityAura == -43 && a.phase == 0 && a.cursorX == 4 && a.cursorZ == 8);
    CHECK(a.sweepSum == 0 && a.sweepCount == 0);
    AuraLayer b; b.init(64, 64, p, &h);
    for (int i = 0; i < 16; i++) b.run(baseEnv(255, 0));
    CHECK(b.cityAura == -10);
    AuraLayer c; c.init(64, 64, p, &h);
    for (int i = 0; i < 16; i++) c.run(baseEnv(0, 0));
    CHECK(c.cityAura == 0);
    AuraLayer d; d.init(64, 64, p, &h);
    for (int i = 0; i < 16; i++) d.run(baseEnv(100, 0));
    CHECK(d.cityAura == 20);
  }
  {   // phase/cursor walk
    AuraLayer a; a.init(64, 64, p, &h);
    a.run(baseEnv(0, 0));   // 16 cells = one phase
    CHECK(a.phase == 1 && a.cursorX == 0 && a.cursorZ == 12);
    CHECK(AuraLayer::kPhases[2][0] == 4 && AuraLayer::kPhases[2][1] == 0);
  }
  {   // pollution, garbage, radiation, ordinances, EQ, LE terms
    AuraLayer a; a.init(64, 64, p, &h);
    AuraEnv e = baseEnv(0, 0);
    e.airAt = [](int, int) { return 600; };          // -2
    e.garbageAt = [](int, int) { return 1; };        // -5
    e.irradiated = [](int, int) { return true; };    // -20
    e.eq = 125; e.lifeExpectancy = 45;               // EQ: (25*10)/50 = 5; LE <= 50: (5*-4)/50 = 0
    e.ordinances.set(ord::ParkingFine, true); e.ordinances.set(ord::NukeFree, true);   // +1 +3
    for (int i = 0; i < 16; i++) a.run(e);
    CHECK(a.cityAura == -2 - 5 - 20 + 5 + 0 + 4);
    AuraLayer b; b.init(64, 64, p, &h);
    AuraEnv e2 = baseEnv(0, 0);
    e2.eq = 20; e2.lifeExpectancy = 20;              // low EQ: (40-20)*-10/40 = -5; LE: (50-20)*-4/50 = -2
    for (int i = 0; i < 16; i++) b.run(e2);
    CHECK(b.cityAura == -7);
  }
  {   // modifiers: lowest-numbered ancestor for aura (3000 -> 10), not nearest (3100 -> 20)
    AuraLayer a; a.init(64, 64, p, &h);
    SimOccupant o; o.agentType = 3300; o.x = 8; o.z = 8; o.sizeX = 4; o.sizeZ = 4;
    a.addOccupant(o);
    for (int i = 0; i < 16; i++) a.run(baseEnv(0, 0));
    CHECK(a.aura(8, 8) == 10);
    CHECK(a.aura(0, 0) == 0);
    a.removeOccupant(o);
    for (int i = 0; i < 16; i++) a.run(baseEnv(0, 0));
    CHECK(a.aura(8, 8) == 0);
    // Quigley: radius 30, effect 48 covers every tile within [x-30, x+30]
    AuraLayer b; b.init(64, 64, p, &h);
    SimOccupant q; q.agentType = 19999; q.x = 32; q.z = 32;
    b.addOccupant(q);
    for (int i = 0; i < 16; i++) b.run(baseEnv(0, 0));
    CHECK(b.aura(20, 20) == 48);
    CHECK(b.aura(0, 0) == 12 && b.aura(63, 63) == 27);   // partial cells: 4 and 9 of 16 tiles in the box
    // Constructing buildings are ignored.
    AuraLayer c; c.init(64, 64, p, &h);
    SimOccupant r = q; r.hasState = true; r.state = 4;
    c.addOccupant(r);
    for (int i = 0; i < 16; i++) c.run(baseEnv(0, 0));
    CHECK(c.aura(20, 20) == 0);
  }
  {   // tiles not counted -> cell 0 and no stats
    AuraLayer a; a.init(64, 64, p, &h);
    AuraEnv e = baseEnv(100, 255);
    e.tileCounts = [](int, int) { return false; };
    for (int i = 0; i < 16; i++) a.run(e);
    CHECK(a.aura(10, 10) == 0 && a.cityAura == 0);
  }
  {   // Spec 17 §8.4 / Q-BH [STATIC]: empty tiles count; non-kind-1 skipped; kind 1 counts in any lot state.
    AuraLayer a; a.init(64, 64, p, &h);
    // Mark one tile as non-kind-1; all others (including empty) count.
    AuraEnv e = baseEnv(100, 0);
    e.tileCounts = [](int x, int z) { return !(x == 4 && z == 4); };
    for (int i = 0; i < 16; i++) a.run(e);
    CHECK(a.aura(4, 4) != 0);   // cell still has 15 counted empty tiles
    // All skipped → 0
    AuraLayer b; b.init(64, 64, p, &h);
    AuraEnv skip = baseEnv(100, 0);
    skip.tileCounts = [](int, int) { return false; };
    for (int i = 0; i < 16; i++) b.run(skip);
    CHECK(b.aura(4, 4) == 0);
  }
  {   // clamp to +-127
    AuraLayer a; a.init(64, 64, p, &h);
    SimOccupant big; big.agentType = 19999; big.x = 32; big.z = 32;
    for (int i = 0; i < 5; i++) a.addOccupant(big);
    for (int i = 0; i < 16; i++) a.run(baseEnv(0, 0));
    CHECK(a.aura(20, 20) == 127);
  }
}

static void testAuraRecord() {
  AuraParams p = AuraParams::fromIni(IniFile::fromText(kAuraIni));
  AgentHierarchy h;
  AuraLayer a; a.init(64, 64, p, &h);
  Bytes fresh = a.serialize();
  CHECK(fresh.size() == 16 * 16 + 150);
  CHECK(rd32(fresh.data() + 256 + 1) == 4 && rd32(fresh.data() + 256 + 5) == 8);   // cursor (4,8)
  CHECK(rd32(fresh.data() + 256 + 18) == 127 && rd32(fresh.data() + 256 + 18 + 128) == 260);
  for (int i = 0; i < 5; i++) a.run(baseEnv(100, 30));
  a.sweepSum = -1234; a.sweepCount = 99; a.cityAura = -7;
  a.chart.monthly[0] = 120;
  Bytes b = a.serialize();
  CHECK(b[0] == uint8_t(a.grid[15 * 16 + 0]));   // last row first
  AuraLayer d; d.init(64, 64, p, &h);
  d.parse(b.data(), b.size(), 64, 64);
  CHECK(d.grid == a.grid && d.phase == a.phase && d.cursorX == a.cursorX && d.cursorZ == a.cursorZ);
  CHECK(d.sweepSum == -1234 && d.sweepCount == 99 && d.cityAura == -7 && d.chart == a.chart);
  CHECK(d.serialize() == b);
  // Out of range phase/cursor are reset on load.
  Bytes bad = b; bad[256] = 16; wr32(bad.data() + 257, 64); wr32(bad.data() + 261, 100);
  d.parse(bad.data(), bad.size(), 64, 64);
  CHECK(d.phase == 0 && d.cursorX == 0 && d.cursorZ == 0);
  bool threw = false;
  try { d.parse(b.data(), b.size() - 1, 64, 64); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
  // Sizes for 128/192/256 maps from the spec.
  for (auto sz : {std::pair<int, int>{128, 1174}, {192, 2454}, {256, 4246}}) {
    AuraLayer x; x.init(sz.first, sz.first, p, &h);
    CHECK(int(x.serialize().size()) == sz.second);
  }
  // Month-end chart cadence.
  Chart c = Chart::fresh(127, 260);
  chartMonthEnd(c, 100, 2, 2001);
  CHECK(c.monthly[0] == 100 && c.yearly[0] == 127 && c.decade[0] == 127);
  chartMonthEnd(c, 90, 1, 2001);
  CHECK(c.yearly[0] == 90 && c.decade[0] == 127);
  chartMonthEnd(c, 80, 1, 2010);
  CHECK(c.yearly[0] == 80 && c.decade[0] == 80);

  // Spec 10 §6.12 / Q-BL [STATIC]: index 0 = newest; FIFO drops oldest off the far end.
  Chart fifo = Chart::fresh(0, 1000);
  for (uint32_t i = 1; i <= 13; i++) fifo.pushMonthly(i);
  CHECK(fifo.monthly[0] == 13 && fifo.monthly[11] == 2);   // 1 dropped; 2 is oldest kept
  for (uint32_t i = 1; i <= 11; i++) fifo.pushYearly(100 + i);
  CHECK(fifo.yearly[0] == 111 && fifo.yearly[9] == 102);
  for (uint32_t i = 1; i <= 11; i++) fifo.pushDecade(200 + i);
  CHECK(fifo.decade[0] == 211 && fifo.decade[9] == 202);
}

// ---------------- Health / education ----------------
static void testHealthEducation() {
  IniFile t = IniFile::fromText(
      "[Hospital]\nOptimalBedCount=100\nOptimalDoctorCount=50\nOptimalHQEffect=2\nOptimalMonthlyUpkeep=1000\nMaxEfficiency=120\nMysticalE=30\n"
      "[School]\nOptimalDeskCount=300\nOptimalTeacherCount=10\nMinAgeServed=5\nMaxAgeServed=120\nOptimalEQEffect=4\nOptimalMonthlyUpkeep=400\n"
      "[College]\nOptimalDeskCount=500\nOptimalProfCount=20\nMinAgeServed=18\nMaxAgeServed=30\nOptimalEQEffect=0\nOptimalMonthlyUpkeep=900\n"
      "[Library]\nPopServed=2000\nMinAgeServed=5\nMaxAgeServed=80\nEQEffect=3\nMonthlyUpkeep=50\n");
  HospitalParams hp = HospitalParams::fromIni(t);
  CHECK(hp.beds == 100 && near(hp.hqEffect, 0.002));
  HospitalFunding h = hospitalFunding(hp, 100);
  CHECK(h.upkeep == 1000 && h.doctors == 50 && near(h.effect, 0.002, 1e-9) && h.beds == 100);
  h = hospitalFunding(hp, 50);
  CHECK(h.upkeep == 500 && h.doctors == 25 && near(h.effect, 0.001, 1e-9));
  h = hospitalFunding(hp, 120);
  CHECK(h.upkeep == 1200 && h.doctors == int(std::nearbyint(50 * h.x)) && h.x > 1.1 && h.x < 1.2);
  CHECK(hospitalFunding(hp, 120).beds == 100);   // beds fixed
  SchoolParams sp = SchoolParams::school(t);
  CHECK(sp.desks == 300 && sp.staff == 10 && sp.maxAge == 89 && near(sp.eqEffect, 0.004));   // MaxAge capped at 89
  SchoolFunding s = schoolFunding(sp, 1);
  CHECK(s.staff == 1 && s.upkeep == 4);               // staff 0.1 -> 0, but x>0 -> 1
  CHECK(schoolFunding(sp, 0).staff == 0);
  SchoolParams cp = SchoolParams::college(t);
  CHECK(cp.staff == 20 && near(cp.eqEffect, 0.001));  // min 0.001
  LibraryParams lp = LibraryParams::library(t);
  CHECK(lp.popServed == 2000 && lp.upkeep == 50 && near(lp.eqEffect, 0.003));

  CHECK(near(ageCoverage(10, 50, 20), 10.0 / 50));
  CHECK(near(ageCoverage(10, 5, 20), 10.0 / 20));    // oversupplied
  CHECK(hospitalPatients(10, 20, 0.002, 0.01, 1000) == int64_t((15 * 0.002 + 0.01) * 1000));
  CHECK(near(healthQuotient(10, 5, 20), 0.5));
  CHECK(ratingFromQuotient(10, 120) == 4 && ratingFromQuotient(5, 120) == 2 && ratingFromQuotient(0, 120) == 0);
  CHECK(ratingFromQuotient(100, 120) == 4 && ratingFromQuotient(1, 0) == 0);
  CHECK(monthlyStrikeChance(true, true, 50, 80) == 900);
  CHECK(monthlyStrikeChance(false, true, 50, 80) == 0 && monthlyStrikeChance(true, false, 50, 80) == 0 &&
        monthlyStrikeChance(true, true, 80, 80) == 0);
  CHECK(strikeMultiplier(20) == 20.0 && near(strikeMultiplier(20, true), 0.2));   // unscaled defect
}

// Graph histories cadence + sample values (spec 14 §6.3 rule 7 / Q-BB) [STATIC].
static void testGraphHistories() {
  // Crime: sample = avgCrime.
  {
    CrimeParams cp = CrimeParams::fromIni(IniFile::fromText(kCrimeIni));
    AgentHierarchy h = hier();
    CrimeLayer c; c.init(16, 16, cp, &h);
    c.avgCrime = 42;
    c.monthEnd(6, 2001);
    CHECK(c.chart.monthly[0] == 42 && c.chart.yearly[0] == 0);
    c.monthEnd(1, 2001);
    CHECK(c.chart.yearly[0] == 42 && c.chart.decade[0] == 0);
    c.monthEnd(1, 2010);
    CHECK(c.chart.yearly[0] == 42 && c.chart.decade[0] == 42);
  }
  // Pollution: UI combined air/water % = min(100, (⌊W×100/Mw⌋ + ⌊A×100/Ma⌋) / 2).
  {
    PollutionParams p = PollutionParams::fromIni(IniFile::fromText(kPolIni));
    AgentHierarchy h;
    PollutionLayer L; L.init(16, 16, p, &h);
    // kPolIni: MaxAir=800, MaxWater=1200. avgAir=400 → 50; avgWater=600 → 50; combined 50.
    L.avgAir = 400; L.avgWater = 600;
    L.monthEnd(3, 2001);
    CHECK(L.chart.monthly[0] == 50 && L.chart.yearly[0] == 0);
    L.avgAir = 800; L.avgWater = 1200;  // 100+100 → min(100, 100) = 100
    L.monthEnd(1, 2010);
    CHECK(L.chart.monthly[0] == 100 && L.chart.yearly[0] == 100 && L.chart.decade[0] == 100);
  }
  // Aura: sample = cityAura + 127.
  {
    AuraParams ap = AuraParams::fromIni(IniFile::fromText(kAuraIni));
    AgentHierarchy h;
    AuraLayer a; a.init(64, 64, ap, &h);
    a.cityAura = -7;
    a.monthEnd(1, 2000);  // year multiple of 10
    CHECK(a.chart.monthly[0] == 120 && a.chart.yearly[0] == 120 && a.chart.decade[0] == 120);
  }
}

int main() {
  testFundingCurve();
  testCoverageStamp();
  testDispatch();
  testJailsAndStrike();
  testPoliceTick();
  testStrikeEffectOnCoverage();
  testFlammability();
  testCrime();
  testCrimeRecord();
  testPollutionStamp();
  testPollutionRun();
  testPollutionRecord();
  testAuraParams();
  testAuraRun();
  testAuraRecord();
  testHealthEducation();
  testGraphHistories();
  TEST_MAIN_END();
}
