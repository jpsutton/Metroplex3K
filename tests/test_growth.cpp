// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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
  std::set<std::pair<int, int>> litter;   // tiles whose standing garbage is at or above the polluted threshold
  int64_t pending = 0;
  int returned = 0, credited = 0, runwaySide = -1, runwayCalls = 0, eqv = 100, yr = 1900, mo = 1;
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
  bool garbagePolluted(int x, int z) const override { return litter.count({x, z}) != 0; }
  bool ordinanceEnacted(uint32_t id) const override { return ords.count(id) != 0; }
  int year() const override { return yr; }
  int month() const override { return mo; }
  int eq() const override { return eqv; }
  void placeBuilding(const BuildingChoice& b, int x, int z) override {
    LotInfo l; l.present = true; l.ax = x; l.az = z; l.state = 4; l.agentType = b.agentType; l.sizeX = b.sizeX; l.sizeZ = b.sizeZ; l.flagWord = b.flagWord;
    l.port = b.port; l.partIndex = b.line;
    lots.push_back(l);
  }
  void removeBuilding(const LotInfo& lot) override {
    lots.erase(std::remove_if(lots.begin(), lots.end(), [&](const LotInfo& l) { return l.ax == lot.ax && l.az == lot.az; }), lots.end());
  }
  void setLotState(const LotInfo& lot, uint32_t s) override { for (auto& l : lots) if (l.ax == lot.ax && l.az == lot.az) l.state = s; }
  int landfillGarbage(int x, int z) const override { auto it = garbage.find({x, z}); return it == garbage.end() ? 0 : it->second; }
  void setLandfillGarbage(int x, int z, int g) override { garbage[{x, z}] = g; }
  void placeLandfillPiece(int x, int z) override { landfillPieces.push_back({x, z}); }
  void clearLandfillTile(int x, int z) override { garbage.erase({x, z}); }
  std::set<std::pair<int, int>> registered;
  int64_t room = 0, placed = 0;
  bool landfillRegistered(int x, int z) const override { return registered.count({x, z}) != 0; }
  void registerLandfill(int x, int z) override { if (registered.insert({x, z}).second) room += 32767 - landfillGarbage(x, z); }
  void creditLandfillRoom(int d) override { credited += d; room += d; }
  int64_t landfillPending() const override { return pending; }
  void landfillPlaced(int a) override { pending -= a; room -= a; placed += a; }
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
    cfg.zones[14] = ZoneDescription::parse(14, "0xC1F81E7E,3000");   // as shipped: no category list
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
    {  // runway set as shipped (category 12310): flag word and line number (attribute 0x1F4)
      const std::pair<uint32_t, int> parts[] = {{0xA10, 1}, {0xA00, 1}, {0xA80, 1}, {0x600, 2}, {0xC00, 3}};
      uint32_t occ = 30;
      for (auto [fw, line] : parts) { BuildingChoice b = bc(occ++, 12310, 1, 1, fw); b.line = line; rA.addMember(12310, b, 1); }
    }
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
    // Filler-lot garbage flag (spec 15 4.3.3): 0x100 follows the garbage test; both set drops it on a state change (original quirk).
    SimRand rngF(5);   // own stream: the later cases depend on the shared one
    auto filler = [&](uint32_t state, bool g) { TestCity c = setup(state, 3100, 60); c.lots[0].flagWord = 0x100; if (g) c.litter.insert({5, 5}); return c; };
    { TestCity c = filler(1, true); zm.visitTile(c, rngF, 5, 5); CHECK(c.lots[0].state == (1 | 0x100)); }          // g set, f clear: re-sync adds the flag
    { TestCity c = filler(1 | 0x100, false); zm.visitTile(c, rngF, 5, 5); CHECK(c.lots[0].state == 1); }          // g clear, f set: flag dropped
    { TestCity c = filler(1 | 0x100, true); zm.visitTile(c, rngF, 5, 5); CHECK(c.lots[0].state == (1 | 0x100)); } // g == f: untouched
    { TestCity c = filler(1 | 0x100, true); c.rad[c.ix(5, 5)] = 1; zm.visitTile(c, rngF, 5, 5); CHECK(c.lots[0].state == 2); }   // quirk: state change with g and f set drops it
    { TestCity c = filler(1, true); c.rad[c.ix(5, 5)] = 1; zm.visitTile(c, rngF, 5, 5); CHECK(c.lots[0].state == (2 | 0x100)); }  // state change carries E
    { TestCity c = filler(1, false); zm.visitTile(c, rngF, 5, 5); CHECK(c.lots[0].state == 1); }
    { TestCity c = setup(1); c.pw[c.ix(5, 5)] = 0; CHECK(zm.visitTile(c, rngF, 5, 5) == kAbNoPower && c.lots[0].state == 2); }
    { TestCity c = setup(1); c.trip[c.ix(5, 5)] = 1; CHECK(zm.visitTile(c, rng, 5, 5) == kAbNoAccess); }
    { TestCity c = setup(1, 3200); c.wt[c.ix(5, 5)] = 0; CHECK(zm.visitTile(c, rng, 5, 5) == kAbNoWater); }   // density 2 without water
    { TestCity c = setup(1, 3100, 10); CHECK(zm.visitTile(c, rng, 5, 5) == kAbLowLV && c.lots[0].state == 2); } // minLV 30 / 2 = 15 > 10 [OBSERVED threshold]
    { TestCity c = setup(1, 3100, 50); CHECK(zm.visitTile(c, rng, 5, 5) != kAbLowLV); }                         // 50 >= 15 (the spec's maxLV/2 = 100 would abandon)
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
    f.cfg.ind.pctChanceAg = 0;   // the shipped default (90) would pick the agricultural set
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
    // a 3 x 2 landfill with a road beside it: every tile is a corner of a 2 x 2 landfill square with transport in reach
    TestCity c(16, 16); c.fillZone(10, 5, 12, 6, 17); c.road[c.ix(9, 5)] = 1; c.pending = 40000; SimRand rng;
    zm.visitTile(c, rng, 10, 5);   // registers (+32767 room), appends itself and fills
    CHECK(c.landfillRegistered(10, 5) && c.landfillGarbage(10, 5) == 32767 && c.pending == 7233 && c.placed == 32767 && c.room == 0);
    CHECK(c.landfillPieces.size() == 1);
    zm.visitTile(c, rng, 11, 5);   // the full tile takes nothing; the visited tile joins the list
    CHECK(c.landfillGarbage(11, 5) == 7233 && c.pending == 0 && c.room == 32767 - 7233 && c.landfillPieces.size() == 2);
    zm.visitTile(c, rng, 10, 5);   // decay: round(32767 * 490 / 10000) = 1606, credited as room (registered)
    CHECK(c.landfillGarbage(10, 5) == 32767 - 1606 && c.credited == 1606);
    c.garbage[{12, 5}] = 1; zm.visitTile(c, rng, 12, 5); CHECK(c.landfillGarbage(12, 5) == 0 && c.garbage.count({12, 5}) == 0);
    // spec 15 s7 test vector: g = 20000 -> d = 980
    c.garbage[{11, 5}] = 20000; int cr = c.credited; zm.visitTile(c, rng, 11, 5);
    CHECK(c.landfillGarbage(11, 5) == 19020 && c.credited - cr == 980);
    // landfill tile test (spec 15 s7): no transport within 10 -> code 16; not a corner of a 2 x 2 landfill square -> 9
    TestCity far(32, 32); far.fillZone(20, 20, 21, 21, 17);
    CHECK(zm.visitTile(far, rng, 20, 20) == kNdNoAccess && !far.landfillRegistered(20, 20));
    TestCity thin(32, 32); thin.fillZone(20, 20, 22, 20, 17); thin.road[thin.ix(19, 20)] = 1;
    CHECK(zm.visitTile(thin, rng, 21, 20) == 9 && !thin.landfillRegistered(21, 20));
  }
  // ---- housing projects layout B, row houses (spec 15 s5.5) ----
  {
    Fixture f; ZoneManager zm; zm.setConfig(&f.cfg);
    DevRule hp; hp.id = 0x630EA608;
    hp.lists[3700].large.push_back(bc(1, 3700, 2, 2));
    hp.lists[3700].filler.push_back(bc(2, 3700, 1, 1, 0x100));
    Category k; k.agentType = 3700;
    // seed search: find a draw sequence that picks layout B (33 <= r < 66) on side 0
    bool sawB = false;
    for (uint32_t seed = 1; seed < 400 && !sawB; seed++) {
      TestCity c(32, 32);
      for (int x = 10; x <= 15; x++) c.road[c.ix(x, 19)] = 1;   // frontage along the low-z side of the lot
      Rect lot{10, 20, 15, 22};
      SimRand probe(seed); probe.rand(1); int sel = probe.rand(100); int side = probe.rand(4);
      if (!(sel >= 33 && sel < 66)) continue;
      SimRand rng(seed);
      int n = zm.runRule(c, rng, hp, lot, k, 2000, 0, 2000, 3);
      if (side != 0 && n == 0) continue;   // other sides have no frontage; the loop moves on to side 0
      sawB = true;
      CHECK(n == 9);
      std::set<std::pair<int, int>> anchors, fill;
      for (const auto& l : c.lots) (l.sizeX == 2 ? anchors : fill).insert({l.ax, l.az});
      CHECK(anchors == (std::set<std::pair<int, int>>{{10, 21}, {12, 20}, {14, 21}}));
      CHECK(fill == (std::set<std::pair<int, int>>{{10, 20}, {11, 20}, {14, 20}, {15, 20}, {12, 22}, {13, 22}}));
    }
    CHECK(sawB);

    DevRule rh; rh.id = 0x61F44687;
    rh.lists[3900].large.push_back(bc(10, 3900, 2, 2, 0xFF));
    rh.lists[3900].filler.push_back(bc(11, 3900, 1, 1, 0x100));
    Category k2; k2.agentType = 3900;
    TestCity c(32, 32);
    for (int x = 9; x <= 18; x++) { c.road[c.ix(x, 9)] = 1; c.road[c.ix(x, 16)] = 1; }
    for (int z = 9; z <= 16; z++) { c.road[c.ix(9, z)] = 1; c.road[c.ix(18, z)] = 1; }
    SimRand rng(3);
    int n = zm.runRule(c, rng, rh, Rect{10, 10, 17, 15}, k2, 2000, 0, 2000, 3);   // 8 x 6 block, roads on all sides
    int big = 0, small = 0;
    for (const auto& l : c.lots) (l.sizeX == 2 ? big : small)++;
    CHECK(big == 4 + 2 + 2 + 1 + 1 && small == 4 * 2 && n == big + small);   // corners, x-edge runs, z-edge runs, courtyard
    TestCity odd(32, 32);
    CHECK(zm.runRule(odd, rng, rh, Rect{10, 10, 16, 15}, k2, 2000, 0, 2000, 3) == 0);   // odd width
  }
  // ---- estates (spec 15 s5.5): shipped category 3500 filing, mirroring, 6 x 6 lot ----
  {
    Fixture f; ZoneManager zm; zm.setConfig(&f.cfg);
    DevRule es; es.id = 0xE22F4A83;
    uint32_t occ = 100;
    for (uint32_t fw : {0x102u, 0x108u, 0x240u, 0x640u, 0x280u, 0xA80u, 0x680u, 0xA40u, 0xD00u, 0x100u, 0x100u, 0x000u, 0x000u})
      es.lists[3500].filler.push_back(bc(occ++, 3500, 1, 1, fw));
    es.lists[3500].large.push_back(bc(occ++, 3500, 3, 3, 0));
    Category k; k.agentType = 3500;
    TestCity c(32, 32);
    for (int x = 9; x <= 16; x++) { c.road[c.ix(x, 9)] = 1; c.road[c.ix(x, 16)] = 1; }
    for (int z = 9; z <= 16; z++) { c.road[c.ix(9, z)] = 1; c.road[c.ix(16, z)] = 1; }
    SimRand rng(9);
    int n = zm.runRule(c, rng, es, Rect{10, 10, 17, 17}, k, 2000, 0, 2000, 3);   // cut to 6 x 6
    CHECK(n == int(c.lots.size()));
    int ring = 0, inner = 0;
    for (const auto& l : c.lots) {
      CHECK(l.ax >= 10 && l.ax <= 15 && l.az >= 10 && l.az <= 15);
      if (l.ax == 10 || l.ax == 15 || l.az == 10 || l.az == 15) ring++; else inner++;
    }
    CHECK(ring == 20);   // 4 corners + 16 wall or gate tiles
    // mansion (1 x 1 or 3 x 3) plus lawn/feature fill the 16 interior tiles
    int innerTiles = 0; for (const auto& l : c.lots) if (!(l.ax == 10 || l.ax == 15 || l.az == 10 || l.az == 15)) innerTiles += l.sizeX * l.sizeZ;
    CHECK(innerTiles == 16 && inner >= 1);
    DevRule none; none.id = 0xE22F4A83; none.lists[3500].filler.push_back(bc(1, 3500, 1, 1, 0xD00));
    TestCity d(32, 32);
    CHECK(zm.runRule(d, rng, none, Rect{10, 10, 15, 15}, k, 2000, 0, 2000, 3) == 0);   // empty pair / mansion list: not registered
  }
  // ---- airport runway ----
  {
    Fixture f; ZoneManager zm; zm.setConfig(&f.cfg); SimRand rng(5);
    TestCity c = makeRes(); c.fillZone(10, 10, 14, 12, 14); for (int x = 9; x <= 15; x++) c.road[c.ix(x, 9)] = 1; c.valves[5001].V = 100;
    zm.visitTile(c, rng, 10, 10);
    // runway along the edge of length 5, three rows deep (A, line 3, line 2): 5 x 3 = 15 pieces
    CHECK(c.lots.size() == 15);
    std::set<uint32_t> occs; for (const auto& l : c.lots) occs.insert(l.agentType);
    CHECK(occs == std::set<uint32_t>{12310});
    // negative score on a 5x3 lot: nothing is built (an adjoining 'other' building strip)
    TestCity d = makeRes(); d.fillZone(10, 10, 14, 12, 14); for (int x = 9; x <= 15; x++) d.road[d.ix(x, 9)] = 1; d.valves[5001].V = 100;
    for (int x = 10; x <= 14; x++) { LotInfo l; l.present = true; l.ax = x; l.az = 13; l.state = 1; l.agentType = 4242; d.lots.push_back(l); }
    for (int x = 10; x <= 14; x++) { LotInfo l; l.present = true; l.ax = x; l.az = 9; l.state = 1; l.agentType = 4242; d.lots.push_back(l); }
    zm.visitTile(d, rng, 10, 10); CHECK(d.lots.size() == 10);
  }
  // ---- airport buildings (spec 15 s5.5) ----
  {
    ZoneManager zm; SimRand rng(7);
    DevRule ab; ab.id = 0x2207D9EE;
    const uint32_t at = 12310;
    auto addB = [&](uint32_t occ, int sx, int sz, uint32_t fw, int line) {
      BuildingChoice b = bc(occ, at, sx, sz, fw); b.line = line; b.family = 1; b.port = true;
      (sx >= 2 || sz >= 2 ? ab.lists[at].large : ab.lists[at].small).push_back(b);
    };
    addB(50, 1, 1, 0, 4);        // T
    addB(51, 1, 1, 0x800, 5);    // Ev
    addB(52, 1, 1, 0x400, 5);    // Eh
    addB(53, 2, 2, 0x400, 6);    // Pv
    addB(54, 2, 2, 0x800, 6);    // Ph
    addB(55, 1, 1, 0x100, 0);    // F
    Category k; k.agentType = at;

    // Step 1: terminal on best frontage when outward neighbour has p = 2
    TestCity c(32, 32);
    // Lot is [10, 10] to [15, 15]. Place an outward neighbour at (12, 9) with p = 2 (side 3, low-z outward -z).
    LotInfo outP2; outP2.present = true; outP2.ax = 12; outP2.az = 9; outP2.port = true; outP2.partIndex = 2;
    c.lots.push_back(outP2);
    int n = zm.runRule(c, rng, ab, Rect{10, 10, 15, 15}, k, 2000, 0, 2000, 3);
    CHECK(n == 1);
    CHECK(c.lots.size() == 2 && c.lots.back().ax == 12 && c.lots.back().az == 10 && c.lots.back().partIndex == 4);

    // If member lists incomplete: family not registered, rule returns 0
    DevRule incomplete; incomplete.id = 0x2207D9EE;
    incomplete.lists[at].small.push_back(bc(50, at, 1, 1, 0));
    TestCity c2(32, 32);
    CHECK(zm.runRule(c2, rng, incomplete, Rect{10, 10, 15, 15}, k, 2000, 0, 2000, 3) == 0);
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
