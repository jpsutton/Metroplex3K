#include "core/landvalue.h"
#include "core/ini.h"
#include "testutil.h"
using namespace m3k;
using namespace m3k::growth;

struct LvTestCity : LandValueCity {
  int W, H;
  std::vector<int> zone, air, crimeV;
  std::vector<char> water, rad;
  std::vector<LvOccupant> occ;
  int64_t pop = 0;
  std::set<uint32_t> ords;
  LvTestCity(int w, int h, int z) : W(w), H(h), zone(size_t(w) * h, z), air(size_t(w) * h, 0), crimeV(size_t(w) * h, 0), water(size_t(w) * h, 0), rad(size_t(w) * h, 0) {}
  int mapW() const override { return W; }
  int mapH() const override { return H; }
  int zoneAt(int x, int z) const override { return zone[size_t(x) * H + z]; }
  bool radioactive(int x, int z) const override { return rad[size_t(x) * H + z]; }
  int airPollution(int x, int z) const override { return air[size_t(x) * H + z]; }
  int crime(int x, int z) const override { return crimeV[size_t(x) * H + z]; }
  int altitude(int, int) const override { return 30; }
  int seaLevel() const override { return 30; }
  bool hasWater(int x, int z) const override { return water[size_t(x) * H + z]; }
  int64_t population() const override { return pop; }
  bool ordinanceEnacted(uint32_t id) const override { return ords.count(id) != 0; }
  void occupants(std::vector<LvOccupant>& o) const override { o = occ; }
};

static LandValueTuning flat(int edge = 50) {
  LandValueTuning t;
  t.updatePeriod = 16;
  t.res = LvCentre::parse("50+100p 0:50 10:50");
  t.com = LvCentre::parse("50+100p 0:50 10:50");
  t.ind = LvCentre::parse("50+100p 0:50 10:50");
  (void)edge;
  return t;
}

int main() {
  CHECK(lvZoneClass(1) == kLvRes && lvZoneClass(14) == kLvCom && lvZoneClass(15) == kLvInd && lvZoneClass(22) == kLvCom && lvZoneClass(4) == kLvNone);
  { LvCentre c = LvCentre::parse("20+80p 0:100 5:150"); CHECK(c.edge == 20 && c.radiusPct == 80 && c.pts.size() == 2 && c.pts[1].y == 150); }
  { LvFactor f = LandValueTuning::parseTriple("400,-500,300", true); CHECK(f.t == 144 && f.b == -0.5f && f.c == 0.3f);
    LvFactor g = LandValueTuning::parseTriple("400,-500,300", false); CHECK(g.t == 255);
    LvAgentMod m = LandValueTuning::parseMod("50:R,-20:c,7:A,7:A trailing"); CHECK(m.r == 50 && m.c == -20 && m.all == 14); }
  { IniFile ini = IniFile::fromText("[TuningParameters]\nUpdatePeriod=0\nRadPolluteFactor=0,-800,0\nWaterEffectRadius=500\nWaterEffect=40\nHillEffect=20\nResCityCenterEffect=10+100p 0:80 10:120\n[AgentIDModifiers]\n3100=30:R\n");
    LandValueTuning t; t.parse(ini); CHECK(t.updatePeriod == 1 && t.radB == -0.8f && t.waterRadiusBlocks == 126 && t.res.edge == 10 && t.agentMods.at(3100).r == 30); }

  // Flat city: centre tile, base 50 + 1
  {
    LvTestCity c(64, 64, 1);
    LandValueLayer lv; lv.init(64, 64, flat());
    lv.recomputeAll(c);
    CHECK(lv.landValueAtTile(10, 10) == 51 && lv.landValueAtTile(63, 63) == 51);
    CHECK(lv.centreValue(kLvRes) == 50);
  }
  // Water bonus: Rw = 2 blocks, WaterEffect 40
  {
    LvTestCity c(64, 64, 1); c.water[0] = 1;
    LandValueTuning t = flat(); t.waterEffect = 40; t.waterRadiusBlocks = 2;
    LandValueLayer lv; lv.init(64, 64, t); lv.recomputeAll(c);
    CHECK(lv.landValueAtTile(0, 0) == 71);   // dist 0: W=0.4, B=round(51*1.4)=71
    CHECK(lv.landValueAtTile(2, 0) == 64);   // dist 1: Wb=(2-1+1)*40/3=26 -> round(51*1.26)=64
    CHECK(lv.landValueAtTile(4, 4) == 58);   // dist 2: Wb=13 -> round(51*1.13)=58
  }
  // Pollution: air above threshold with negative slope lowers value; low pollution raises it
  {
    LvTestCity c(64, 64, 1);
    for (auto& a : c.air) a = 0;
    LandValueTuning t = flat(); t.air.t = 100; t.air.b = -0.5f; t.air.c = 0.1f;
    LandValueLayer lv; lv.init(64, 64, t); lv.recomputeAll(c);
    // v=0 < tA: p = 100*0.1/100 = 0.1 -> T = round(1.1*51) = 56
    CHECK(lv.landValueAtTile(10, 10) == 56);
    for (auto& a : c.air) a = 32767; lv.recomputeAll(c);
    // v>tA: p = (32767-100)*-0.5/(32767-100) = -0.5 -> T = round(0.5*51)=26 (25.5 -> even 26)
    CHECK(lv.landValueAtTile(10, 10) == 26);
    // radioactive tile
    LvTestCity d(64, 64, 1); d.rad[size_t(10) * 64 + 10] = 1; LandValueTuning t2 = flat(); t2.radB = -2.0f;
    LandValueLayer l2; l2.init(64, 64, t2); l2.recomputeAll(d);
    // one tile: T = round((1-2)*51) = -51; others 51: sum = 102 -> /4 = 25
    CHECK(l2.landValueAtTile(10, 10) == 25);
  }
  // Unzoned: base 1 -> LV 1 (T=1.. block value clamps >= 1)
  { LvTestCity c(64, 64, 0); LandValueLayer lv; lv.init(64, 64, flat()); lv.recomputeAll(c); CHECK(lv.landValueAtTile(10, 10) == 1); CHECK(lv.cityAverage() == 0); }
  // City-centre distance: edge 20, base slope
  {
    LvTestCity c(64, 64, 1);
    LandValueTuning t = flat(); t.res = LvCentre::parse("20+100p 0:50 10:50");
    LandValueLayer lv; lv.init(64, 64, t); lv.recomputeAll(c);
    CHECK(lv.landValueAtTile(32, 32) == 51);          // dc 0
    CHECK(lv.landValueAtTile(42, 32) == 46);          // dc 10..11 radius 64: round(-4.69) = -5 -> 46
  }
  // Agent modifiers + smoothing: a +100 'A' building in a zone-0 block
  {
    LvTestCity c(64, 64, 0); c.zone[size_t(20) * 64 + 20] = 0;
    LandValueTuning t = flat(); t.agentMods[7000] = LvAgentMod{100, 0, 0, 0};
    c.occ.push_back({20, 20, 1, 1, 7000, true});
    LandValueLayer lv; lv.init(64, 64, t); lv.recomputeAll(c);
    // cell (5,5) value 100 smoothed twice: 100/9 = 11, then (11*4+...)/..; unzoned base 1 * (1 + m) -> still clamps to >=1
    CHECK(lv.landValueAtTile(20, 20) == 1);
    c.occ[0].occupied = false; lv.recomputeAll(c);
  }
  // Scan: 16 update calls = one full cycle, phase wraps and statistics roll
  {
    LvTestCity c(64, 64, 1);
    LandValueLayer lv; lv.init(64, 64, flat());
    for (int i = 0; i < 16; i++) lv.update(c);
    CHECK(lv.phase == 0 && lv.scanX == 4 && lv.scanY == 4);
    for (size_t i = 0; i < lv.lv.size(); i++) if (lv.lv[i] != 51) { CHECK(lv.lv[i] == 51); break; }
    CHECK(lv.count == 0 && lv.sum == 0);
    CHECK(lv.prevSum == 51 * 4096 && lv.avgLV == 51);
  }
  // Record round trip (size 8 + 1024 + 22 + 128 + 4)
  {
    LvTestCity c(64, 64, 1);
    LandValueLayer lv; lv.init(64, 64, flat()); lv.recomputeAll(c);
    lv.lv[5] = 77; lv.history[3] = 9; lv.avgLV = 12; lv.phase = 3; lv.scanX = 6; lv.scanY = 16; lv.sum = -4; lv.count = 5; lv.prevSum = 100;
    Bytes b = lv.write(); CHECK(b.size() == 8 + 32 * 32 + 22 + 128 + 4);
    LandValueLayer o; o.init(64, 64, flat()); CHECK(o.parse(b.data(), b.size()));
    CHECK(o.lv == lv.lv && o.phase == 3 && o.history[3] == 9 && o.sum == -4 && o.write() == b);
    CHECK(!o.parse(b.data(), 20));
    LandValueLayer p192; p192.init(192, 192, LandValueTuning()); CHECK(p192.write().size() == 9378);
    LandValueLayer p256; p256.init(256, 256, LandValueTuning()); CHECK(p256.write().size() == 16546);
  }
  TEST_MAIN_END();
}
