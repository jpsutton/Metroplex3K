#include <algorithm>
#include "core/growth.h"
#include "core/ini.h"
#include "core/records.h"
#include "testutil.h"
using namespace m3k;
using namespace m3k::growth;

struct TestCity : GrowthCity {
  int W, H;
  std::vector<int> zone, lv;
  std::vector<char> pw, wt, rad, road, trip;
  std::vector<LotInfo> lots;
  std::map<int, ValveReading> valves;
  std::set<uint32_t> ords;
  std::map<std::pair<int, int>, int> garbage;
  int pending = 0, returned = 0, credited = 0, runwaySide = -1, runwayCalls = 0, eqv = 100, yr = 1900, mo = 1;
  std::vector<std::pair<int, int>> landfillPieces;
  TestCity(int w, int h) : W(w), H(h), zone(size_t(w) * h, 0), lv(size_t(w) * h, 100), pw(size_t(w) * h, 1), wt(size_t(w) * h, 1),
      rad(size_t(w) * h, 0), road(size_t(w) * h, 0), trip(size_t(w) * h, 0) {
    for (int id : {3000, 5000, 8000, 5001, 8001}) valves[id] = ValveReading{500, 2000, -2000};
  }
  size_t ix(int x, int z) const { return size_t(x) * H + z; }
  void fillZone(int x0, int z0, int x1, int z1, int z) { for (int x = x0; x <= x1; x++) for (int y = z0; y <= z1; y++) zone[ix(x, y)] = z; }
  int mapW() const override { return W; }
  int mapH() const override { return H; }
  int zoneAt(int x, int z) const override { return zone[ix(x, z)]; }
  bool powered(int x, int z) const override { return pw[ix(x, z)]; }
  bool watered(int x, int z) const override { return wt[ix(x, z)]; }
  int landValue(int x, int z) const override { return lv[ix(x, z)]; }
  bool isTransport(int x, int z) const override { return x >= 0 && z >= 0 && x < W && z < H && road[ix(x, z)]; }
  bool hasAccess(int x, int z, int d) const override {
    for (int xx = x - d; xx <= x + d; xx++) for (int zz = z - d; zz <= z + d; zz++) if (isTransport(xx, zz)) return true;
    return false;
  }
  LotInfo lotAt(int x, int z) const override {
    for (const auto& l : lots) if (x >= l.ax && x < l.ax + l.sizeX && z >= l.az && z < l.az + l.sizeZ) return l;
    return LotInfo();
  }
  ValveReading valve(int id) const override { auto it = valves.find(id); return it == valves.end() ? ValveReading() : it->second; }
  bool radioactive(int x, int z) const override { return rad[ix(x, z)]; }
  bool tripFailed(int x, int z) const override { return trip[ix(x, z)]; }
  bool ordinanceEnacted(uint32_t id) const override { return ords.count(id) != 0; }
  int year() const override { return yr; }
  int month() const override { return mo; }
  int eq() const override { return eqv; }
  void placeBuilding(const BuildingChoice& b, int x, int z) override {
    LotInfo l; l.present = true; l.ax = x; l.az = z; l.state = 4; l.agentType = b.agentType; l.sizeX = b.sizeX; l.sizeZ = b.sizeZ; l.flagWord = b.flagWord;
    lots.push_back(l);
  }
  void removeBuilding(const LotInfo& lot) override {
    lots.erase(std::remove_if(lots.begin(), lots.end(), [&](const LotInfo& l) { return l.ax == lot.ax && l.az == lot.az; }), lots.end());
  }
  void setLotState(const LotInfo& lot, uint32_t s) override { for (auto& l : lots) if (l.ax == lot.ax && l.az == lot.az) l.state = s; }
  int layRunway(const Rect&, int side) override { runwaySide = side; runwayCalls++; return 1; }
  int landfillGarbage(int x, int z) const override { auto it = garbage.find({x, z}); return it == garbage.end() ? 0 : it->second; }
  void setLandfillGarbage(int x, int z, int g) override { garbage[{x, z}] = g; }
  void placeLandfillPiece(int x, int z) override { landfillPieces.push_back({x, z}); }
  void clearLandfillTile(int x, int z) override { garbage.erase({x, z}); }
  void creditGarbageCapacity(int d) override { credited += d; }
  int takePendingLandfillGarbage() override { int p = pending; pending = 0; return p; }
  void returnLandfillGarbage(int r) override { returned += r; }
};

static BuildingChoice bc(uint32_t occ, uint32_t agent, int sx = 1, int sz = 1, uint32_t flags = 0) {
  BuildingChoice b; b.occId = occ; b.agentType = agent; b.sizeX = sx; b.sizeZ = sz; b.flagWord = flags; return b;
}

struct Fixture {
  GrowthConfig cfg;
  std::map<uint32_t, std::vector<ValveItem>> effects;
  Fixture() {
    cfg.zones[1] = ZoneDescription::parse(1, "0x619BA64E,3000:3100,1,200,30;3200,2,255,100");
    cfg.zones[5] = ZoneDescription::parse(5, "0x41A3ADC1,5000:5100,1,200,30");
    cfg.zones[9] = ZoneDescription::parse(9, "0xE1A53B30,8000:8100,D,1,255,10;8200,C,1,255,10;9300,A,1,255,10");
    cfg.zones[14] = ZoneDescription::parse(14, "0xC1F81E7E,12310:12310,1,255,0");
    cfg.zones[17] = ZoneDescription::parse(17, "0x82348DE5,12850:");
    cfg.res.noServicePercentage = 100; cfg.res.constructionTimeFactor = 100000; cfg.res.maxDistFromTransport = 3;
    cfg.res.higherLandValuePct = 100; cfg.res.higherDensityPct = 100; cfg.res.anarchyFactor = 0;
    cfg.com = cfg.res; cfg.ind.noServicePercentage = 100; cfg.ind.constructionTimeFactor = 100000; cfg.ind.maxDistFromTransport = 3;
    effects[3100] = {{3000, 10, 0}}; effects[3200] = {{3000, 20, 0}}; effects[8200] = {{8000, 6, 0}}; effects[8100] = {{8000, 6, 0}};
    cfg.valveEffects = &effects;
    DevRule& r1 = cfg.ruleFor(1, 0x422C9D52); r1.a = 1; r1.b = 10; r1.c = 0; r1.d = 0; r1.hasParams = true;
    r1.addMember(3100, bc(1, 3100), 1); r1.addMember(3200, bc(2, 3200), 1);
    DevRule& r9 = cfg.ruleFor(9, 0x422B55F2); r9.a = 1; r9.b = 10; r9.c = 0; r9.d = 0; r9.hasParams = true;
    r9.addMember(8100, bc(10, 8100), 1); r9.addMember(8200, bc(11, 8200), 1); r9.addMember(9300, bc(12, 9300), 1);
    DevRule& rA = cfg.ruleFor(14, 0x81FFC3CE); rA.a = 1; rA.b = 10; rA.addMember(12310, bc(20, 12310, 1, 1, 0x100), 1);
  }
};

static TestCity makeRes() {
  TestCity c(16, 16);
  for (int z = 0; z < 16; z++) c.road[c.ix(0, z)] = 1;
  return c;
}

int main() {
  const std::pair<uint8_t, uint8_t> K11(1, 1);
  // ---- category rules ----
  {
    ZoneDescription zd = ZoneDescription::parse(1, "0x619BA64E,3000:3100,1,200,30;3200,2,255,100;3300,1,120,60;3400,3,255,150");
    CHECK(zd.developerClass == 0x619BA64E && zd.baseAgentType == 3000 && zd.cats.size() == 4);
    CHECK(pickCategoryE(zd, 70, true) == 2);   // 3100 (min 30) and 3300 (min 60) fit: greatest minLV
    CHECK(pickCategoryE(zd, 110, true) == 1);  // 3100, 3200(min 100) fit -> 3200 (density 2 needs water)
    CHECK(pickCategoryE(zd, 110, false) == 2); // unwatered: density 1 only (3100, 3300 fit)
    CHECK(pickCategoryE(zd, 20, true) == -1);
    CHECK(pickCategoryH(zd, 0, 110, true) == 1);  // higher minLV, density >= current
    CHECK(pickCategoryH(zd, 1, 110, true) == -1);
    CHECK(pickCategoryD(zd, 0, 200) == 3);        // density >= 2, greatest minLV: 3400 (150)
    CHECK(pickCategoryD(zd, 3, 200) == -1);
    ZoneDescription tie = ZoneDescription::parse(5, "0x41A3ADC1,5000:5100,1,200,30;5200,1,200,30");
    CHECK(pickCategoryE(tie, 100, true) == 0);    // tie keeps first
    ZoneDescription ind = ZoneDescription::parse(9, "0xE1A53B30,8000:8100,D,1,255,10;8200,C,2,255,20");
    CHECK(ind.cats[1].cls == 'C' && ind.cats[1].density == 2 && ind.cats[1].minLV == 20 && pickCategoryE(ind, 100, true, 'D') == 0);
  }
  // ---- lot finding ----
  {
    TestCity c = makeRes();
    c.fillZone(1, 1, 3, 3, 1);
    Rect r = findLot(c, 2, 2, 30, 0, 3, false);
    CHECK(r.x0 == 1 && r.x1 == 3 && r.z0 == 1 && r.z1 == 3);
    LotInfo blocker; blocker.present = true; blocker.ax = 3; blocker.az = 1; blocker.state = 1; blocker.agentType = 9999; c.lots.push_back(blocker);
    r = findLot(c, 2, 2, 30, 0, 3, false);
    CHECK(r.x1 == 2);                      // blocked column x=3
    c.lots[0].state = 2;                   // abandoned buildings can be replaced
    r = findLot(c, 2, 2, 30, 0, 3, false); CHECK(r.x1 == 3);
    for (int z = 1; z <= 3; z++) c.pw[c.ix(1, z)] = 0; r = findLot(c, 2, 2, 30, 0, 3, false); CHECK(r.x0 == 2);  // unpowered column left
    r = findLot(c, 2, 2, 30, 0, 3, true); CHECK(r.x0 == 1);                          // power test skipped
    c.lv[c.ix(2, 3)] = 10; r = findLot(c, 2, 2, 30, 0, 3, true); CHECK(r.z1 == 2);    // land value below minLV
  }
  // ---- empty tile result codes and development ----
  {
    Fixture f; SimRand rng(7);
    ZoneManager zm; zm.setConfig(&f.cfg);
    {
      TestCity c = makeRes(); c.fillZone(1, 1, 1, 3, 1);
      c.lv.assign(c.lv.size(), 90);
      CHECK(zm.visitTile(c, rng, 1, 1) == 0 && c.lots.size() == 3);       // three small buildings along the road
      for (auto& l : c.lots) CHECK(l.state == 4 && l.agentType == 3100);
      CHECK(zm.committed(3000) == 30);                                      // 3 tiles * supply 10
    }
    auto code = [&](TestCity& c, int x, int z) { return zm.visitTile(c, rng, x, z); };
    { TestCity c = makeRes(); c.fillZone(1, 1, 1, 1, 1); c.valves[3000].V = -5; CHECK(code(c, 1, 1) == 13); c.valves[3000].V = 0; CHECK(code(c, 1, 1) == 0);
      c.valves[3000].V = 500; zm.commit[3000] = 500; CHECK(code(c, 1, 1) == 0 && c.lots.empty()); zm.commit[3000] = 0;  // V - P < 1
      c.rad[c.ix(1, 1)] = 1; CHECK(code(c, 1, 1) == 10); c.rad[c.ix(1, 1)] = 0;
      c.pw[c.ix(1, 1)] = 0; CHECK(code(c, 1, 1) == 14); c.pw[c.ix(1, 1)] = 1;
      c.road[c.ix(0, 0)] = 0; for (int z = 0; z < 16; z++) c.road[c.ix(0, z)] = 0; CHECK(code(c, 1, 1) == 16);
      c.road[c.ix(0, 1)] = 1; c.lv[c.ix(1, 1)] = 10; CHECK(code(c, 1, 1) == 11);
      c.lv[c.ix(1, 1)] = 250; CHECK(code(c, 1, 1) == 0 && c.lots.size() == 1);   // 3200 minLV 100 maxLV 255 fits (watered)
    }
    { TestCity c = makeRes(); c.fillZone(1, 1, 1, 1, 1); c.lv[c.ix(1, 1)] = 210; CHECK(code(c, 1, 1) == 0 && c.lots.size() == 1 && c.lots[0].agentType == 3200); }
    { Fixture g; g.cfg.zones[1] = ZoneDescription::parse(1, "0x619BA64E,3000:3200,2,255,100");
      ZoneManager z2; z2.setConfig(&g.cfg); TestCity c = makeRes(); c.fillZone(1, 1, 1, 1, 1); c.lv[c.ix(1, 1)] = 150; c.wt[c.ix(1, 1)] = 0;
      CHECK(z2.visitTile(c, rng, 1, 1) == 15); c.wt[c.ix(1, 1)] = 1; c.lv[c.ix(1, 1)] = 50; CHECK(z2.visitTile(c, rng, 1, 1) == 11);
      g.cfg.zones[1].cats[0].minLV = 0; g.cfg.zones[1].cats[0].maxLV = 255; g.cfg.zones[1].cats[0].density = 5;  // always fits...
      ZoneManager z3; z3.setConfig(&g.cfg); c.wt[c.ix(1, 1)] = 0; c.lv[c.ix(1, 1)] = 150; CHECK(z3.visitTile(c, rng, 1, 1) == 15);  // dense needs water
      Fixture k; k.cfg.zones[1] = ZoneDescription::parse(1, "0x619BA64E,3000:3200,2,255,100;3300,2,90,40");
      ZoneManager z5; z5.setConfig(&k.cfg); TestCity e = makeRes(); e.fillZone(1, 1, 1, 1, 1); e.lv[e.ix(1, 1)] = 95; CHECK(z5.visitTile(e, rng, 1, 1) == 18);  // hole between categories, watered
    }
  }
  // ---- occupied / construction / abandoned lots ----
  {
    Fixture f; SimRand rng(3);
    ZoneManager zm; zm.setConfig(&f.cfg);
    auto setup = [&](uint32_t state, uint32_t agent = 3100, int lvv = 150) {
      TestCity c = makeRes(); c.fillZone(5, 5, 5, 5, 1);
      LotInfo l; l.present = true; l.ax = 5; l.az = 5; l.state = state; l.agentType = agent; c.lots.push_back(l);
      c.lv[c.ix(5, 5)] = lvv; c.road[c.ix(4, 5)] = 1; return c; };
    { TestCity c = setup(1); c.rad[c.ix(5, 5)] = 1; CHECK(zm.visitTile(c, rng, 5, 5) == kAbRadioactive && c.lots[0].state == 2); }
    { TestCity c = setup(1); c.pw[c.ix(5, 5)] = 0; CHECK(zm.visitTile(c, rng, 5, 5) == kAbNoPower && c.lots[0].state == 2); }
    { TestCity c = setup(1); c.trip[c.ix(5, 5)] = 1; CHECK(zm.visitTile(c, rng, 5, 5) == kAbNoAccess); }
    { TestCity c = setup(1, 3200); c.wt[c.ix(5, 5)] = 0; CHECK(zm.visitTile(c, rng, 5, 5) == kAbNoWater); }   // density 2 without water
    { TestCity c = setup(1, 3100, 50); CHECK(zm.visitTile(c, rng, 5, 5) == kAbLowLV && c.lots[0].state == 2); } // 200/2 = 100 > 50
    { TestCity c = setup(1, 3100, 90); c.valves[3000].V = -2000; int hit = 0;
      for (int i = 0; i < 30 && !hit; i++) { c.lots[0].state = 1; hit = zm.visitTile(c, rng, 5, 5) == kAbNegDemand; } CHECK(hit); }
    { TestCity c = setup(4); CHECK(zm.visitTile(c, rng, 5, 5) == 0 && c.lots[0].state == 1); }   // construction completes
    { TestCity c = setup(4); c.pw[c.ix(5, 5)] = 0; zm.visitTile(c, rng, 5, 5); CHECK(c.lots[0].state == 4); }
    { TestCity c = setup(4, 7777); zm.visitTile(c, rng, 5, 5); CHECK(c.lots.empty()); }          // no category: removed (V>0)
    { TestCity c = setup(4 | 0x200, 7777); zm.visitTile(c, rng, 5, 5); CHECK(c.lots.size() == 1 && c.lots[0].state == (1 | 0x200)); }  // flagged keeps
    { TestCity c = setup(2, 3100, 60); CHECK(zm.visitTile(c, rng, 5, 5) == 0 && c.lots[0].state == 1); }  // re-occupied, same category
    { TestCity c = setup(2 | 0x200, 7777, 60); zm.visitTile(c, rng, 5, 5); CHECK(c.lots[0].state == (1 | 0x200)); }
    { TestCity c = setup(2, 3100, 150); zm.visitTile(c, rng, 5, 5); CHECK(c.lots.size() == 1 && c.lots[0].agentType == 3200 && c.lots[0].state == 4); }  // redevelop
    { TestCity c = setup(2, 3100, 60); c.valves[3000].V = 0; zm.visitTile(c, rng, 5, 5); CHECK(c.lots[0].state == 2); }
    { TestCity c = setup(1, 3100, 150);  // higher land value redevelopment (pct 100)
      zm.visitTile(c, rng, 5, 5); CHECK(c.lots.size() == 1 && c.lots[0].agentType == 3200 && c.lots[0].state == 4);
}
    { TestCity c = setup(1, 3200, 150); f.cfg.res.anarchyFactor = 10000;   // anarchy: rebuilt with same category
      zm.visitTile(c, rng, 5, 5); CHECK(c.lots.size() == 1 && c.lots[0].agentType == 3200 && c.lots[0].state == 4); f.cfg.res.anarchyFactor = 0; }
    { TestCity c = setup(1, 3200, 150); zm.visitTile(c, rng, 5, 5); CHECK(c.lots[0].state == 1 && c.lots.size() == 1); }  // nothing to do
    { TestCity c = setup(8); CHECK(zm.visitTile(c, rng, 5, 5) == 0 && c.lots[0].state == 8); }  // rubble ignored
    { TestCity c = setup(1, 3100, 150); LotInfo big; big.present = true; big.ax = 5; big.az = 5; big.sizeX = 2; big.sizeZ = 2; big.state = 1; big.agentType = 3100; c.lots[0] = big;
      CHECK(zm.visitTile(c, rng, 6, 5) == 0 && c.lots[0].state == 1); }  // non-anchor tile ignored
  }
  // ---- industrial set choice ----
  {
    Fixture f; SimRand rng(11);
    ZoneManager zm; zm.setConfig(&f.cfg);
    auto make = [&]() { TestCity c = makeRes(); c.fillZone(1, 1, 1, 1, 9); return c; };
    f.cfg.ind.minMinCleanInd = f.cfg.ind.maxMinCleanInd = 100; f.cfg.ind.minMaxCleanInd = f.cfg.ind.maxMaxCleanInd = 100;
    { TestCity c = make(); zm.visitTile(c, rng, 1, 1); CHECK(c.lots.size() == 1 && c.lots[0].agentType == 8200); CHECK(zm.minClean == 100 && zm.maxClean == 100); }
    f.cfg.ind.minMinCleanInd = f.cfg.ind.maxMinCleanInd = 0; f.cfg.ind.minMaxCleanInd = f.cfg.ind.maxMaxCleanInd = 0; zm.curveYear = -1;
    { TestCity c = make(); zm.visitTile(c, rng, 1, 1); CHECK(c.lots.size() == 1 && c.lots[0].agentType == 8100); }
    f.cfg.ind.pctChanceAg = 100;
    { TestCity c = make(); zm.visitTile(c, rng, 1, 1); CHECK(c.lots.size() == 1 && c.lots[0].agentType == 9300); }
    f.cfg.ind.minAgAirPollution = -1;  // tile no longer qualifies for agriculture
    { TestCity c = make(); zm.visitTile(c, rng, 1, 1); CHECK(c.lots.size() == 1 && c.lots[0].agentType == 8100); }
    f.cfg.ind.minAgAirPollution = 0; f.cfg.ind.pctChanceAg = 0;
    { TestCity c = make(); c.valves[8000].V = 0; CHECK(zm.visitTile(c, rng, 1, 1) == 0); c.valves[8000].V = -9; CHECK(zm.visitTile(c, rng, 1, 1) == 0); }  // no code 13 for industry
    // year curve: MinMinClean 10 -> MaxMinClean 50 between 1900 and 1950
    f.cfg.ind.minMinCleanInd = 10; f.cfg.ind.maxMinCleanInd = 50; f.cfg.ind.minCleanIndFlatYear = 1950;
    f.cfg.ind.minMaxCleanInd = 20; f.cfg.ind.maxMaxCleanInd = 90; f.cfg.ind.maxCleanIndFlatYear = 2000;
    { TestCity c = make(); c.yr = 1925; c.mo = 1; zm.curveYear = -1; zm.visitTile(c, rng, 1, 1); CHECK(zm.minClean == 30 && zm.maxClean == 20 + 70 * 25 / 100); }
    { TestCity c = make(); c.yr = 2010; zm.curveYear = -1; zm.visitTile(c, rng, 1, 1); CHECK(zm.minClean == 50 && zm.maxClean == 90); }
    // farm pollution abandon [7], farm field (flags 0x500) removed instead of abandoned
    f.cfg.ind.maxAgAirPollution = -1;
    { TestCity c = make(); LotInfo l; l.present = true; l.ax = 1; l.az = 1; l.state = 1; l.agentType = 9300; c.lots.push_back(l);
      CHECK(zm.visitTile(c, rng, 1, 1) == kAbFarmPollution && c.lots[0].state == 2); }
    { TestCity c = make(); LotInfo l; l.present = true; l.ax = 1; l.az = 1; l.state = 1; l.agentType = 9300; l.flagWord = 0x500; c.lots.push_back(l);
      CHECK(zm.visitTile(c, rng, 1, 1) == kAbFarmPollution && c.lots.empty()); }
  }
  // ---- scan order, passes, record ----
  {
    CHECK(ZoneManager().visitsPerTick() == 0);
    for (auto sz : {128, 192, 256}) { ZoneManager zm; CrtRand crt; zm.init(sz, sz, crt); int K = zm.visitsPerTick(); CHECK(K == (sz == 128 ? 546 : sz == 192 ? 1228 : 2184));
      int ticks = (sz * sz + K - 1) / K; CHECK(ticks == 31); }
    ZoneManager zm; CrtRand crt; zm.init(16, 16, crt);
    CHECK(zm.scan.list.size() == 256 && zm.scan.index == 1 && zm.scan.state == 1);
    { std::vector<uint16_t> s = zm.scan.list; std::sort(s.begin(), s.end());
      bool perm = true; for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) if (s[size_t(x * 16 + z)] != x * 256 + z) perm = false; CHECK(perm);
      bool ident = true; for (size_t i = 0; i < 256; i++) if (zm.scan.list[i] != uint16_t((i / 16) * 256 + i % 16)) ident = false; CHECK(!ident); }
    CHECK(zm.cursorX * 256 + zm.cursorZ == zm.scan.list[0]);
    { ZoneManager b; CrtRand crt2; b.init(16, 16, crt2); CHECK(b.scan.list == zm.scan.list); }  // same CRT seed, same shuffle
    zm.commit[3000] = 5; zm.commit[8000] = -4; zm.resultCur[K11] = 3; zm.failCur[9] = 2; zm.okCur = 4; zm.failCurTotal = 2;
    TestCity c(16, 16); SimRand rng; std::vector<uint16_t> first = zm.scan.list;
    for (int i = 0; i < 255; i++) zm.step(c, rng, crt);
    CHECK(zm.scan.state == 2 && zm.scan.index == 256);                        // last element taken
    CHECK(zm.cursorX * 256 + zm.cursorZ == zm.scan.list[255]);
    zm.step(c, rng, crt);                                                      // processes last, rolls over, takes first of new pass
    CHECK(zm.scan.state == 1 && zm.scan.index == 1 && zm.commit[3000] == 0 && zm.commit[8000] == 0);
    CHECK(zm.resultPrev[K11] == 3 && zm.resultCur[K11] == 0 && zm.failPrev[9] == 2 && zm.failCur[9] == 0 && zm.okPrev == 4 && zm.failPrevTotal == 2 && zm.okCur == 0);
    CHECK(zm.scan.list != first);                                              // reshuffled
    // record round trip
    std::vector<uint8_t> grid(256, 0); grid[5] = 3; grid[100] = 17;
    zm.zoneTileCount[3] = 1; zm.zoneTileCount[17] = 1;
    Bytes b = zm.write(grid);
    ZoneManager o; std::vector<uint8_t> g2;
    CHECK(o.parse(b.data(), b.size(), 16, 16, &g2)); CHECK(g2 == grid && o.write(g2) == b);
    CHECK(o.scan.list == zm.scan.list && o.commit == zm.commit && o.resultPrev == zm.resultPrev && o.cursorX == zm.cursorX);
    // new-city size: 3N + 354 here (91 empty-key table would make it 3N + 900, spec 6.4)
    ZoneManager n; CrtRand c3; n.init(16, 16, c3); CHECK(n.write(std::vector<uint8_t>(256, 0)).size() == 3 * 256 + 354);
    ZoneManager bad; CHECK(!bad.parse(b.data(), 100, 16, 16, nullptr));
    // developer class records
    Fixture f; zm.setConfig(&f.cfg); auto recs = zm.developerClassRecords();
    bool found = false; for (auto& r : recs) if (r.first.instance == 2 && rd32(r.second.data()) == 0x619BA64E) found = true; CHECK(found);
    Bytes cls; put32(cls, 0x619BA64E); CHECK(zm.acceptDeveloperClass(1, cls.data(), 4) && !zm.acceptDeveloperClass(5, cls.data(), 4));
  }
  // ---- committed demand ----
  {
    Fixture f; ZoneManager zm; zm.setConfig(&f.cfg);
    zm.buildingAdded(3200, 2, 2, 4); CHECK(zm.committed(3000) == 80);
    zm.buildingAdded(3200, 2, 2, 2); CHECK(zm.committed(3000) == 80);       // abandoned: not counted
    zm.buildingRemoved(3200, 2, 2, 1); CHECK(zm.committed(3000) == 0);
    zm.buildingRemoved(3100, 1, 1, 1); CHECK(zm.committed(3000) == -10);    // can go negative
    zm.buildingAdded(3200, 12, 12, 1); CHECK(zm.committed(3000) == -10 + int(int8_t(144)) * 20);  // signed 8-bit footprint product
  }
  // ---- landfill ----
  {
    Fixture f; ZoneManager zm; zm.setConfig(&f.cfg);
    TestCity c(16, 16); c.fillZone(10, 5, 12, 5, 17); c.pending = 40000; SimRand rng;
    zm.visitTile(c, rng, 10, 5);
    CHECK(c.landfillGarbage(10, 5) == 32767 && c.landfillGarbage(11, 5) == 7233 && c.landfillPieces.size() == 2 && c.returned == 0);
    zm.visitTile(c, rng, 10, 5);   // decay: round(32767 * 490 / 10000) = 1606
    CHECK(c.landfillGarbage(10, 5) == 32767 - 1606 && c.credited == 1606);
    c.garbage[{12, 5}] = 1; zm.visitTile(c, rng, 12, 5); CHECK(c.landfillGarbage(12, 5) == 0 && c.garbage.count({12, 5}) == 0);
    c.pending = 3 * 32767; zm.visitTile(c, rng, 10, 5); CHECK(c.returned > 0);  // runs out of zone-17 tiles
  }
  // ---- airport runway ----
  {
    Fixture f; ZoneManager zm; zm.setConfig(&f.cfg); SimRand rng(5);
    TestCity c = makeRes(); c.fillZone(10, 10, 14, 12, 14); for (int x = 9; x <= 15; x++) c.road[c.ix(x, 9)] = 1; c.valves[5001].V = 100;
    zm.visitTile(c, rng, 10, 10);
    CHECK(c.runwayCalls == 1 && (c.runwaySide == 1 || c.runwaySide == 3));   // runway along an edge of length 5
    // negative score on a 5x3 lot: nothing is built (an adjoining 'other' building strip)
    TestCity d = makeRes(); d.fillZone(10, 10, 14, 12, 14); for (int x = 9; x <= 15; x++) d.road[d.ix(x, 9)] = 1; d.valves[5001].V = 100;
    for (int x = 10; x <= 14; x++) { LotInfo l; l.present = true; l.ax = x; l.az = 13; l.state = 1; l.agentType = 4242; d.lots.push_back(l); }
    for (int x = 10; x <= 14; x++) { LotInfo l; l.present = true; l.ax = x; l.az = 9; l.state = 1; l.agentType = 4242; d.lots.push_back(l); }
    zm.visitTile(d, rng, 10, 10); CHECK(d.runwayCalls == 0);
  }
  // ---- config parsing ----
  {
    IniFile zl = IniFile::fromText("[ZoneDeveloperDescriptions]\n1=0x619BA64E,3000:3100,1,200,30\n[ZoneDeveloperRules]\n1,0xA22653C0=81F53D09,01F58D3D,11;81F53D09,01F58D3D,4\n");
    IniFile tune = IniFile::fromText("[ResidentialZoneDeveloper]\nNoServicePercentage=33\nAnarchyFactor=9\n[IndustrialZoneDeveloper]\nPctChanceAg=12\nMinMinCleanInd=7\n"
        "[LandfillZoneDeveloper]\nLandfillDecayRate=99999\n[ZoneDeveloperRules]\n0xA22653C0=2,6,10,40\n");
    GrowthConfig g; g.parseZoneDescriptions(zl); auto b = g.parseRuleBindings(zl); g.parseTune(tune);
    CHECK(g.zones.at(1).cats.size() == 1 && b.size() == 1 && b[0].families.size() == 2 && b[0].families[1] == 4);
    CHECK(g.res.noServicePercentage == 33 && g.res.anarchyFactor == 9 && g.ind.pctChanceAg == 12 && g.ind.minMinCleanInd == 7 && g.landfillDecayRate == 10000);
    const DevRule& r = g.rules.at(1)[0]; CHECK(r.id == 0xA22653C0 && r.a == 2 && r.b == 6 && r.c == 10 && r.d == 40 && r.hasParams);
    DevRule dr; dr.addMember(3100, bc(1, 3100, 1, 1), 3); dr.addMember(3100, bc(2, 3100, 2, 2), 2); dr.addMember(3100, bc(3, 3100, 1, 1, 0x100), 1);
    CHECK(dr.lists[3100].small.size() == 3 && dr.lists[3100].large.size() == 2 && dr.lists[3100].filler.size() == 1);
  }
  TEST_MAIN_END();
}
