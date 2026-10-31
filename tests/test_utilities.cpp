#include "core/utilities.h"
#include "testutil.h"

using namespace m3k; using namespace m3k::us;

static const char* kPowerIni =
    "[PowerPlantCapacities]\n12111=100\n12117=50\n12853=30\n"
    "[MiscPowerLayerTunables]\nSavingDueToPowerConservationOrdinance=10\nSavingDueToStairwellLightingOrdinance=5\n"
    "[MiscPowerPlantTunables]\nMinOverworkMonths=18\nMaxOverworkMonths=30\n"
    "[CoalPowerPlant]\nMaxLifeSpan=600\nDeclineAge=700\nPercentageVariationInLongevity=0\n"
    "[SolarPowerPlant]\nMaxLifeSpan=0\nDeclineAge=0\nPercentageVariationInLongevity=20\n"
    "[WasteToEnergyPowerPlant]\nMaxLifeSpan=300\nDeclineAge=100\nOptimalGarbageCap=90\n";

static void testRng() {
  SimRng r(1);
  CHECK(r.rand(0) == 0);
  uint32_t s0 = r.stateS();
  CHECK(r.rand(0) == 0 && r.stateS() == s0);  // state not advanced
  // Documented generator: V = S*0x41C64E6D+0x3039, R = V>>16.
  uint64_t v = uint64_t(1) * 0x41C64E6Du + 0x3039u;
  SimRng a(1);
  CHECK(a.rand(1000) == uint32_t(v >> 16) % 1000);
  // Large-bound branch: result in [0, n] inclusive, equals first raw output <= n.
  SimRng b(1);
  uint32_t big = b.rand(0xFFFFFFFFu);
  CHECK(big == uint32_t(v >> 16));
  SimRng c(5);
  for (int i = 0; i < 1000; i++) CHECK(c.rand(0x01000000u) <= 0x01000000u);
  SimRng d(7);
  for (int i = 0; i < 100; i++) { uint32_t x = d.randInt(18, 30); CHECK(x >= 18 && x < 30); }
  CHECK(SimRng(3).randInt(5, 5) == 5);
  SimRng e(9);
  for (int i = 0; i < 100; i++) { double u = e.randUnit(); CHECK(u >= 0 && u < 1); }
}

static void testPlantMath() {
  CHECK(plantOutput(100, 0, 600, 300) == 100);
  CHECK(plantOutput(100, 300, 600, 300) == 100);
  CHECK(plantOutput(100, 450, 600, 300) == 50);
  CHECK(plantOutput(100, 301, 600, 300) == 100 - (100 * 1 / 300));
  CHECK(plantOutput(100, 600, 600, 300) == 0);
  CHECK(plantOutput(100, 900, 600, 300) == 0);
  PlantTunables t; t.maxLife = 600; t.declineAge = 300; t.variationPct = 0;
  SimRng r(1);
  int lm, ld;
  rollLifespan(t, r, lm, ld);
  CHECK(lm == 600 && ld == 300);
  t.variationPct = 20;
  for (int i = 0; i < 50; i++) {
    rollLifespan(t, r, lm, ld);
    CHECK(lm >= 480 && lm <= 720 && ld >= 240 && ld <= 360 && ld < lm);
  }
  t.maxLife = 10; t.declineAge = 10; t.variationPct = 0;
  rollLifespan(t, r, lm, ld);
  CHECK(lm == 11 && ld == 10);  // Ld >= Lmax => Lmax = Ld+1
}

static void testPowerParams() {
  IniFile f = IniFile::fromText(kPowerIni);
  PowerParams p = PowerParams::fromIni(f);
  CHECK(p.plants.at(12111).capacity == 100);
  CHECK(p.plants.at(12111).maxLife == 600);
  CHECK(p.plants.at(12111).declineAge == 300);  // 700 >= 600 -> 600/2
  CHECK(p.plants.at(12117).maxLife == 1);       // 0 -> 1
  CHECK(p.plants.at(12117).variationPct == 20);
  CHECK(p.plants.at(12112).capacity == 0);      // absent -> 0
  CHECK(p.savingConservationPct == 10 && p.savingStairwellPct == 5);
  CHECK(p.wteOptimalGarbageCap == 90);
  CHECK(powerCategory(12853) == 8 && powerCategory(12111) == 0 && powerCategory(5) == -1);
}

// 16x16 map. Row z=5 from x=0..9 conducts. Consumers at (2,5),(5,5),(8,5) etc.
struct PowerFixture {
  int W = 16, H = 16;
  std::vector<uint8_t> cond;
  PowerLayer L;
  SimRng rng{42};
  PowerFixture() : cond(256, 0) {
    PowerParams p = PowerParams::fromIni(IniFile::fromText(kPowerIni));
    L.init(W, H, p);
  }
  void line(int x0, int x1, int z) { for (int x = x0; x <= x1; x++) cond[x * H + z] = 1; }
};

static void testPowerBrownout() {
  PowerFixture f;
  f.line(0, 9, 5);
  f.L.addPlant(0, 5, 12111, 0, f.rng);  // output 100, no neighbour => budget 110
  f.L.setConsumption(2, 5, 40);
  f.L.setConsumption(5, 5, 40);
  f.L.setConsumption(8, 5, 40);   // 120 total > 110 -> brown out at (8,5)
  PowerEnv env; env.conductive = &f.cond; env.nowAbsMonth = 12;
  auto ev = f.L.monthlyUpdate(env, f.rng);
  CHECK(ev.empty());
  CHECK(f.L.powered(0, 5) && f.L.powered(2, 5) && f.L.powered(5, 5));
  // Budget runs out on the third consumer; the tile itself stays powered (spec 2.7 step 8).
  CHECK(f.L.powered(8, 5));
  CHECK(f.L.stats().consumerTiles == 3);
  CHECK(f.L.stats().produced == 100);
  CHECK(f.L.stats().consumed == 110);   // brown-out: consumed += S (incl. margin)
  // Tiles after the failing consumer (row-major: same row, higher x) stay unpowered.
  CHECK(!f.L.powered(9, 5));
  CHECK(f.L.stats().spareCapacityPct == 0);
  CHECK(!f.L.powered(3, 8));  // not on any network
}

static void testPowerOrderAndOrdinance() {
  PowerFixture f;
  // Two rows connected by a vertical link at x=0: z-major scan means row z=2 before z=3.
  f.line(0, 4, 2); f.line(0, 4, 3);
  f.L.addPlant(0, 2, 12111, 0, f.rng);
  f.L.setConsumption(4, 3, 60);   // later in scan (z=3)
  f.L.setConsumption(4, 2, 60);   // earlier (z=2)
  PowerEnv env; env.conductive = &f.cond; env.nowAbsMonth = 1;
  f.L.monthlyUpdate(env, f.rng);
  // Budget 110: first consumer (z=2) ok, second (z=3) fails but is itself powered; nothing after it.
  CHECK(f.L.powered(4, 2));
  CHECK(f.L.powered(4, 3));
  CHECK(f.L.stats().consumed == 110);
  // Conservation ordinance raises budget: 110/0.9 = 122 -> both served.
  env.ordinances.set(ord::PowerConservation, true);
  f.L.monthlyUpdate(env, f.rng);
  CHECK(f.L.powered(4, 3));
  CHECK(f.L.stats().consumed == 110 - int64_t(double(int64_t(110 / 0.9) - 120) * 0.9));
  CHECK(f.L.stats().poweredConsumerTiles == 2);
  // Third consumer with 130 total demand: last in scan order loses power.
  f.L.setConsumption(3, 3, 5);
  f.L.setConsumption(4, 3, 60);
  f.L.setConsumption(4, 2, 60);
  env.ordinances.set(ord::PowerConservation, false);
  f.L.monthlyUpdate(env, f.rng);
  CHECK(f.L.powered(4, 2) && f.L.powered(3, 3));
  CHECK(f.L.powered(4, 3));        // budget runs out exactly at the last tile; tile powered
  CHECK(f.L.stats().poweredConsumerTiles == 3);
}

static void testPowerNeighbourDeal() {
  PowerFixture f;
  f.line(0, 6, 1);
  f.L.addPlant(0, 1, 12111, 0, f.rng);
  f.L.setConsumption(3, 1, 130);
  PowerEnv env; env.conductive = &f.cond; env.nowAbsMonth = 1;
  PowerDeal d; d.tx = 6; d.tz = 1; d.amount = 50;
  env.deals.push_back(d);
  f.L.monthlyUpdate(env, f.rng);
  // With a neighbour connection no 10% margin: S=100, D=50, budget 150 >= 130.
  CHECK(f.L.powered(3, 1));
  CHECK(f.L.stats().consumed == 130);
  CHECK(f.L.deals()[0].unused == 20);
  CHECK(f.L.production[f.L.monthIndex][9] == 50);
  // Unconnected deal slot with amount but separate network: distribution seeded at connection.
  PowerFixture g;
  g.line(0, 3, 1);
  g.line(8, 12, 7);
  g.L.setConsumption(10, 7, 20);
  PowerEnv e2; e2.conductive = &g.cond; e2.nowAbsMonth = 1;
  PowerDeal d2; d2.tx = 12; d2.tz = 7; d2.amount = 25;
  e2.deals.push_back(d2);
  g.L.monthlyUpdate(e2, g.rng);
  CHECK(g.L.powered(10, 7));
}

static void testPowerAgeing() {
  PowerFixture f;
  f.line(0, 4, 1);
  f.L.addPlant(0, 1, 12111, 0, f.rng);
  f.L.setConsumption(2, 1, 10);
  PowerEnv env; env.conductive = &f.cond;
  env.nowAbsMonth = 450;   // between Ld=300 and Lmax=600 -> half output
  f.L.monthlyUpdate(env, f.rng);
  CHECK(f.L.plants()[0].output == 50);
  CHECK(f.L.stats().ageingPlants == 1);
  env.nowAbsMonth = 600;   // age == Lmax: output 0 but not yet retired (strictly greater)
  auto ev = f.L.monthlyUpdate(env, f.rng);
  CHECK(ev.empty() && f.L.plants()[0].output == 0 && !f.L.plants()[0].expired);
  CHECK(f.L.powered(2, 1));   // zero supply: the failing consumer tile itself stays powered (original quirk)
  CHECK(!f.L.powered(3, 1));  // everything after it in scan order is dark
  env.nowAbsMonth = 601;   // retired
  ev = f.L.monthlyUpdate(env, f.rng);
  CHECK(ev.size() == 1 && ev[0].kind == PlantEvent::Retired);
  CHECK(f.L.plants()[0].expired);
}

static void testPowerExplosion() {
  PowerFixture f;
  f.line(0, 4, 1);
  f.L.addPlant(0, 1, 12111, 0, f.rng);
  f.L.setConsumption(2, 1, 200);   // demand > output => overworked
  PowerEnv env; env.conductive = &f.cond; env.nowAbsMonth = 1;
  int explodedAt = -1;
  for (int m = 1; m <= 40; m++) {
    auto ev = f.L.monthlyUpdate(env, f.rng);
    if (!ev.empty() && ev[0].kind == PlantEvent::Explosion) { explodedAt = m; break; }
  }
  // Cannot explode before 19 overloaded months have accumulated; certain by month 31.
  CHECK(explodedAt >= 20 && explodedAt <= 31);
  CHECK(f.L.plants().empty());
  // No overwork when supply is enough.
  PowerFixture g;
  g.line(0, 4, 1);
  g.L.addPlant(0, 1, 12111, 0, g.rng);
  g.L.setConsumption(2, 1, 10);
  PowerEnv e2; e2.conductive = &g.cond; e2.nowAbsMonth = 1;
  for (int m = 0; m < 60; m++) CHECK(g.L.monthlyUpdate(e2, g.rng).empty());
  CHECK(g.L.plants()[0].overwork == 0);
}

static void testDefaultConsumption() {
  PowerFixture f;
  std::vector<SimOccupant> occ(2);
  occ[0].x = 3; occ[0].z = 4; occ[0].sizeX = 2; occ[0].sizeZ = 3; occ[0].densityClass = 2;
  occ[1].x = 7; occ[1].z = 7; occ[1].powerUse = 9;
  f.L.rebuildConsumption(occ);
  CHECK(f.L.consumption(3, 4) == 12);
  CHECK(f.L.consumption(4, 4) == 0);
  CHECK(f.L.consumption(7, 7) == 9);
}

static void testPowerRecord() {
  PowerLayer a(128, 128, PowerParams());
  a.monthIndex = 5;
  for (int m = 0; m < 12; m++) for (int c = 0; c < 10; c++) a.production[m][c] = m * 100 + c;
  for (int c = 0; c < 10; c++) { int s = 0; for (int m = 0; m < 12; m++) s += a.production[m][c]; a.annualTotal[c] = s; }
  a.chart = Chart::fresh(3, 10);
  a.chart.monthly[0] = 77; a.chart.recomputeAutoMax();
  Bytes b = a.serialize();
  CHECK(b.size() == 668);
  CHECK(b[0] == 1 && b[1] == 0 && b[2] == 2 && b[3] == 0 && b[4] == 0xEF && b[5] == 0xBE && b[6] == 0xAD && b[7] == 0xDE);
  PowerLayer c(128, 128, PowerParams());
  c.parse(b.data(), b.size());
  CHECK(c.monthIndex == 5 && c.production[3][4] == 304 && c.annualTotal[2] == a.annualTotal[2]);
  CHECK(c.chart == a.chart && c.chart.max == 77);
  CHECK(c.serialize() == b);
  bool threw = false;
  try { c.parse(b.data(), b.size() - 1); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
}

// ---------------- Water ----------------
static const char* kWaterIni =
    "[TuningParameters]\nWaterFromPipe=2\nWaterPumpDistanceFactor=50\nWaterTowerCapa=1000\nWaterPumpCapa=2000\n"
    "DesaltPlantCapa=3000\nWaterTowerLife=10\nWaterPumpLife=20\nDesaltPlantLife=15\nWaterConservationFactor=20\n"
    "WaterMeterFactor=10\nDefaultConsumption=3\nDefaultLeak=1\n"
    "[AgentIDModifiers]\n4300=7 high density\n3100=2\n";

static void testWaterParams() {
  WaterParams p = WaterParams::fromIni(IniFile::fromText(kWaterIni));
  CHECK(p.waterFromPipe == 2 && p.pumpCapa == 2000 && p.towerLifeYears == 10);
  CHECK(p.consumptionPerTile(3100) == 2);
  CHECK(p.consumptionPerTile(4300) == 7);
  CHECK(p.consumptionPerTile(9999) == 3);
  WaterParams q = WaterParams::fromIni(IniFile::fromText("[TuningParameters]\nWaterFromPipe=0\n"));
  CHECK(q.waterFromPipe == 3);
  // sorted modifiers, capped at 127
  std::string t = "[AgentIDModifiers]\n";
  for (int i = 200; i > 0; i--) t += std::to_string(i) + "=1\n";
  WaterParams r = WaterParams::fromIni(IniFile::fromText(t));
  CHECK(r.modifiers.size() == 127);
  for (size_t i = 1; i < r.modifiers.size(); i++) CHECK(r.modifiers[i - 1].first < r.modifiers[i].first);
}

struct WaterFixture {
  int W = 32, H = 32;
  WaterLayer L;
  std::vector<uint8_t> pipes, kind;
  WaterFixture() : pipes(1024, 0), kind(1024, 0) { L.init(W, H, WaterParams::fromIni(IniFile::fromText(kWaterIni))); }
  void pipeLine(int x0, int x1, int z) { for (int x = x0; x <= x1; x++) pipes[x * H + z] = 1; }
};

static void testWaterPumpRings() {
  WaterFixture f;
  int pump = f.L.addFacility(WaterKind::Pump, 10, 10, 0); (void)pump;
  WaterEnv env; env.nowAbsMonth = 1; env.pipes = &f.pipes; env.waterKind = &f.kind;
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 0 && f.L.facilities()[0].status == WaterFacility::NoWater);
  f.kind[12 * 32 + 10] = 1;  // fresh water at distance 2
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 1000 && f.L.facilities()[0].status == WaterFacility::Far);
  f.kind[11 * 32 + 11] = 1;  // distance 1 (diagonal counts)
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 2000 && f.L.facilities()[0].status == WaterFacility::Good);
  // Salt water (cell sea-water flag set) does not feed pumps (spec 16 s3.3).
  std::fill(f.kind.begin(), f.kind.end(), 0);
  f.kind[11 * 32 + 11] = 2;
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 0 && f.L.facilities()[0].status == WaterFacility::NoWater);
  // No water at all => 0.
  std::fill(f.kind.begin(), f.kind.end(), 0);
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 0);
  // Unpowered => 0.
  f.kind[11 * 32 + 11] = 1;
  PowerLayer pw(32, 32, PowerParams());
  env.power = &pw;
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 0);
}

static void testWaterDesalAndPollution() {
  WaterFixture f;
  f.L.addFacility(WaterKind::Desalination, 10, 10, 0);
  WaterEnv env; env.nowAbsMonth = 1; env.pipes = &f.pipes; env.waterKind = &f.kind;
  f.kind[13 * 32 + 13] = 2;   // corner of box (x+3,z+3)
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 3000);
  f.kind[13 * 32 + 13] = 0; f.kind[14 * 32 + 10] = 2;  // outside the ring
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 0);
  // Pollution scaling (m = 11000): p=5500 halves, p>=m zeroes.
  f.kind[13 * 32 + 13] = 2;
  env.waterPollutionAt = [](int, int) { return 5500; };
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 1500);
  CHECK(f.L.stats.meanPollution == 5500);
  env.waterPollutionAt = [](int, int) { return 11000; };
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 0);
}

static void testWaterAgeing() {
  WaterFixture f;
  f.L.addFacility(WaterKind::Tower, 5, 5, 0);   // L=10y => Lmax 120, Ld 60
  WaterEnv env; env.pipes = &f.pipes; env.waterKind = &f.kind;
  env.nowAbsMonth = 90;   // half way through decline
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 500 && f.L.stats.ageingCount == 1);
  env.nowAbsMonth = 120;  // age >= Lmax (greater OR equal) -> retired
  f.L.monthlyUpdate(env);
  CHECK(f.L.facilities()[0].output == 0 && f.L.facilities()[0].expired);
}

static void testWaterDistribution() {
  WaterFixture f;
  f.pipeLine(2, 20, 10);
  f.L.addFacility(WaterKind::Tower, 2, 10, 0);   // 1000 water
  std::vector<SimOccupant> cons;
  auto add = [&](int x, int z, uint32_t type, int sx, int sz) {
    SimOccupant o; o.x = x; o.z = z; o.agentType = type; o.sizeX = sx; o.sizeZ = sz; cons.push_back(o);
  };
  add(4, 11, 3100, 1, 1);     // within 2 tiles of pipe: cost 2
  add(10, 12, 4300, 2, 2);    // cost 28, 2 tiles from pipe (z=12 is distance 2)
  add(10, 14, 3100, 1, 1);    // 4 tiles from pipe: dry
  add(25, 10, 3100, 1, 1);    // beyond pipe end + 2: dry
  WaterEnv env; env.nowAbsMonth = 1; env.pipes = &f.pipes; env.waterKind = &f.kind; env.consumers = &cons;
  f.L.monthlyUpdate(env);
  const auto& w = f.L.occupantWatered();
  CHECK(w.size() == 4);
  CHECK(w[0] && w[1] && !w[2] && !w[3]);
  CHECK(f.L.stats.supply == 1000 && f.L.stats.used == 30);
  CHECK(f.L.tileServiced(10, 12) && !f.L.tileServiced(10, 14));
  // Shortage: conservation ordinance lets supply go further; tiny supply waters only nearest first.
  WaterParams p = WaterParams::fromIni(IniFile::fromText(kWaterIni));
  p.towerCapa = 2;
  WaterLayer s(32, 32, p);
  s.addFacility(WaterKind::Tower, 2, 10, 0);
  s.monthlyUpdate(env);
  CHECK(s.occupantWatered()[0] == 1 && s.occupantWatered()[1] == 0);   // nearest watered first, budget hits 0, spread stops
  // Ordinances: k = 1 - 0.2 - 0.1.
  env.ordinances.set(ord::WaterConservation, true);
  env.ordinances.set(ord::WaterMeters, true);
  f.L.monthlyUpdate(env);
  CHECK(f.L.stats.used == 30);
  // Purchase deal => unlimited supply.
  WaterLayer u(32, 32, p);
  u.addFacility(WaterKind::Tower, 2, 10, 0);
  env.ordinances = OrdinanceSet();
  WaterDeal d; d.tx = 20; d.tz = 10; d.purchase = true; d.amount = 1000;
  env.deals.push_back(d);
  u.monthlyUpdate(env);
  CHECK(u.occupantWatered()[0] && u.occupantWatered()[1]);
}

static void testWaterRecord() {
  WaterFixture f;
  Bytes b0 = f.L.serialize();
  CHECK(b0.size() == 212);     // n = 0
  f.L.monthlyUpdate(WaterEnv());
  WaterLayer a(64, 64, WaterParams());
  // manual service bits
  WaterEnv env;
  std::vector<uint8_t> pipes(4096, 0);
  for (int x = 0; x < 20; x++) pipes[x * 64 + 30] = 1;
  WaterParams p; p.waterFromPipe = 1; p.towerCapa = 100; p.towerLifeYears = 10;
  WaterLayer c(64, 64, p);
  c.addFacility(WaterKind::Tower, 0, 30, 0);
  env.pipes = &pipes;
  c.monthlyUpdate(env);
  c.stats.statA = 123; c.stats.statB = 456; c.stats.sale[1] = 9; c.stats.purchase[0] = 8;
  Bytes b = c.serialize();
  CHECK((b.size() - 212) % 4 == 0 && b.size() > 212);
  CHECK(b[0] == 4);
  WaterLayer d(64, 64, WaterParams());
  d.parse(b.data(), b.size());
  CHECK(d.serviceGrid() == c.serviceGrid());
  CHECK(d.stats.statA == 123 && d.stats.sale[1] == 9 && d.stats.purchase[0] == 8);
  CHECK(d.serialize() == b);
  // Trimmed: first/last stored words are non-zero.
  uint32_t first = rd32(b.data() + 8), n = rd32(b.data() + 12);
  CHECK(n > 0 && rd32(b.data() + 16) != 0 && rd32(b.data() + 16 + 4 * (n - 1)) != 0);
  (void)first;
  // Out-of-range bitmap rejected.
  Bytes bad = b; wr32(bad.data() + 8, 100000);
  bool threw = false;
  try { d.parse(bad.data(), bad.size()); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
}

// ---------------- Garbage ----------------
static void testGarbage() {
  GarbageDisposal g;
  g.abductionCap = 10;
  g.beginRun(5, 7, false);
  g.store.V = 4;                    // 4 available landfill room: routed into the store's pending amount S
  CHECK(g.dispose(30) == 30 - 5 - 7 - 4 - 10);
  CHECK(g.month[5] == 5 && g.month[4] == 7 && g.month[6] == 0 && g.month[2] == 0 && g.store.S == 4 && g.abducted == 10);
  // Abduction is capped per month and resets at the month end.
  g.beginRun(0, 0, false); g.store.V = 0;
  CHECK(g.dispose(3) == 3);
  g.month[0] = 100;
  g.monthEnd();
  CHECK(g.abducted == 0 && g.history[0][0] == 90 && g.history[0][5] == 5 && g.totals[4] == 7 && g.month[5] == 0);
  g.beginRun(0, 0, false); g.store.V = 0;
  CHECK(g.dispose(3) == 0 && g.abducted == 3);
  // Export swallows everything left after burners and landfill.
  GarbageDisposal e; e.beginRun(2, 0, true);
  CHECK(e.dispose(100) == 0 && e.month[2] == 98 && e.abducted == 0);
  // Landfill store: V = D - S at the monthly update; 37-byte block round trip.
  LandfillStore st; st.D = 1000; st.S = 300; st.monthly();
  CHECK(st.V == 700 && st.Dlast == 1000 && st.Slast == 300);
  uint8_t blk[37]; st.f1 = 0.5f; st.capPct = 7; st.Dbase = -3; st.write(blk);
  LandfillStore st2; st2.parse(blk);
  CHECK(st2.S == 300 && st2.D == 1000 && st2.V == 700 && st2.f1 == 0.5f && st2.capPct == 7 && st2.Dbase == -3);
  CHECK(reduceByPercent(100, 25) == 75 && reduceByPercent(7, 50) == 4);
  RunBudget rb;
  int64_t tot = 0;
  for (int i = 0; i < 30; i++) tot += rb.next(100);
  CHECK(tot == 100);
}

static void testChart() {
  Chart c = Chart::fresh(127, 260);
  c.pushMonthly(5); c.pushMonthly(6);
  CHECK(c.monthly[0] == 6 && c.monthly[1] == 5 && c.monthly[2] == 127);
  Bytes b; c.write(b);
  CHECK(b.size() == 132);
  Reader r(b); Chart d; d.parse(r);
  CHECK(d == c);
}

int main() {
  testRng();
  testPlantMath();
  testPowerParams();
  testPowerBrownout();
  testPowerOrderAndOrdinance();
  testPowerNeighbourDeal();
  testPowerAgeing();
  testPowerExplosion();
  testDefaultConsumption();
  testPowerRecord();
  testWaterParams();
  testWaterPumpRings();
  testWaterDesalAndPollution();
  testWaterAgeing();
  testWaterDistribution();
  testWaterRecord();
  testGarbage();
  testChart();
  TEST_MAIN_END();
}
