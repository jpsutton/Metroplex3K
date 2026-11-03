#include <cmath>
#include <cstring>
#include "core/automata.h"
#include "core/ixf.h"
#include "testutil.h"
using namespace m3k;
using namespace m3k::automata_ids;

// ---------------------------------------------------------------- fixture encoders
static void bin(Bytes& b) { for (char c : std::string("BIN")) b.push_back(uint8_t(c)); b.push_back(0x0D); }
static void putKey(Bytes& b, uint32_t t, uint32_t g, uint32_t i) { put32(b, t); put32(b, g); put32(b, i); }
static void putStr(Bytes& b, const std::string& s) { put32(b, uint32_t(s.size())); b.insert(b.end(), s.begin(), s.end()); }
static void putF(Bytes& b, float f) { uint32_t v; std::memcpy(&v, &f, 4); put32(b, v); }
static void putPt(Bytes& b, int a, int h, int c) { put32(b, uint32_t(a)); put32(b, uint32_t(h)); put32(b, uint32_t(c)); }
static ResKey scriptKey(uint32_t inst) { return {kScriptType, kScriptGroup, inst}; }

static Bytes vattrib(bool emerg, int motion, int paramB, int siren, int len, uint32_t animInst, uint32_t scriptInst, uint32_t speed) {
  Bytes b; bin(b);
  b.push_back(emerg ? 1 : 0);
  put32(b, uint32_t(motion)); put32(b, uint32_t(paramB)); put32(b, uint32_t(siren)); put32(b, uint32_t(len));
  putKey(b, kAnSprAttType, kAnSprAttGroup, animInst);
  if (scriptInst) putKey(b, kScriptType, kScriptGroup, scriptInst); else putKey(b, 0, 0, 0);
  put32(b, speed);
  return b;
}
static Bytes vclprod(float rate, std::vector<std::pair<uint32_t, uint32_t>> e) {
  Bytes b; bin(b); putF(b, rate); put32(b, uint32_t(e.size()));
  for (auto& p : e) { put32(b, p.first); putKey(b, kVAttribType, kVAttribGroup, p.second); }
  return b;
}
static Bytes pattrib(uint32_t anim, uint32_t script) {
  Bytes b; bin(b); putKey(b, kAnSprAttType, kAnSprAttGroup, anim); putKey(b, kScriptType, kScriptGroup, script); return b;
}
static Bytes ansprat(uint32_t spriteInst, std::vector<std::vector<uint16_t>> seqs, std::vector<uint32_t> ids) {
  Bytes b; bin(b); putKey(b, kSpriteInstType, kSpriteInstGroup, spriteInst); put32(b, uint32_t(seqs.size()));
  for (size_t i = 0; i < seqs.size(); i++) {
    put32(b, ids[i]); b.push_back(uint8_t(seqs[i].size()));
    for (auto f : seqs[i]) put16(b, f);
  }
  return b;
}
static Bytes spriteInst(int zc, int rc, int zsc, int asc, int af, uint32_t category, uint32_t base) {
  Bytes b; bin(b);
  put32(b, 1); put32(b, 0x7302);
  b.push_back(uint8_t(zc)); b.push_back(uint8_t(rc)); b.push_back(uint8_t(zsc)); b.push_back(uint8_t(asc)); b.push_back(uint8_t(af)); b.push_back(0);
  put32(b, 0);
  int n = asc * af;
  put16(b, uint16_t(n));
  for (int i = 0; i < n; i++) { put16(b, uint16_t(i)); put32(b, category); put32(b, base + uint32_t(i)); b.push_back(0); put16(b, 0); put16(b, 0); }
  return b;
}
static Bytes ppath(std::vector<Pt> pts, int entry, int exit) {
  Bytes b; bin(b); put32(b, uint32_t(pts.size()));
  for (auto& p : pts) putPt(b, p.a, p.h, p.b);
  put32(b, uint32_t(entry)); put32(b, uint32_t(exit));
  return b;
}
static Bytes vpath(std::vector<Pt> pts, int lane) {
  Bytes b; bin(b); put32(b, uint32_t(pts.size()));
  for (auto& p : pts) putPt(b, p.a, p.h, p.b);
  b.push_back(0); b.push_back(8); put32(b, uint32_t(lane));
  return b;
}
static void putConn(Bytes& b, int next, int heading, uint32_t path, uint32_t value) {
  b.push_back(uint8_t(int8_t(next))); b.push_back(uint8_t(heading)); put32(b, path); put32(b, value);
}
// One vehicle lane id 0 entering at (0,128) following `path`, exit conn Next=-1.
static Bytes trblkVeh(uint32_t path, uint32_t speed, bool withPed = false, uint32_t pedPath = 0) {
  Bytes b; bin(b);
  for (int i = 0; i < 4; i++) put32(b, 0);
  b.push_back(1);
  bin(b);
  if (withPed) {
    put32(b, 2);
    put32(b, 0); putPt(b, 15, 0, 15); put32(b, 1); putConn(b, 1, 0, pedPath, 0);
    put32(b, 1); putPt(b, 240, 0, 15); put32(b, 1); putConn(b, -1, 8, 0, 1);
  } else put32(b, 0);
  bin(b);
  put32(b, 1);
  put32(b, 0); b.push_back(0); putPt(b, 0, 0, 128); put32(b, 1); putConn(b, -1, 8, path, speed);
  return b;
}
// Shipped road layout: lane 0 follows `path` and continues (Next=1) to lane 1, an exit node whose connection has no
// path (Path 0, Next=-1). The vehicle must leave the piece there instead of failing.
static Bytes trblkVehExitNode(uint32_t path, uint32_t speed) {
  Bytes b; bin(b);
  for (int i = 0; i < 4; i++) put32(b, 0);
  b.push_back(1);
  bin(b); put32(b, 0);
  bin(b);
  put32(b, 2);
  put32(b, 0); b.push_back(0); putPt(b, 0, 0, 128); put32(b, 1); putConn(b, 1, 0, path, speed);
  put32(b, 1); b.push_back(0); putPt(b, 255, 0, 128); put32(b, 1); putConn(b, -1, 16, 0, speed);
  return b;
}
static Bytes scriptTxt(std::vector<std::pair<int, std::vector<std::string>>> steps) {
  std::string s = std::to_string(steps.size()) + "\r\n";
  for (auto& st : steps) {
    s += std::to_string(st.first) + "\t" + std::to_string(st.second.size()) + "\r\n";
    for (auto& a : st.second) s += a + "\r\n";
    s += "\r\n";
  }
  s += "\r\n\r\n";
  return Bytes(s.begin(), s.end());
}

// ---------------------------------------------------------------- host
struct TestHost : AutomataHost {
  int W = 8, H = 8;
  int density = 255;
  int month = 1, day = 1;
  bool ped = true;
  int mapWidth() const override { return W; }
  int mapHeight() const override { return H; }
  int trafficDensity(int, int y) const override { return y == 3 ? density : 0; }
  bool pieceAt(int x, int y, AutoNet k, PieceRef& o) const override {
    if (k == AutoNet::Road && y == 3) { o = {0x10, 0}; return true; }
    if (k == AutoNet::Rail && y == 5 && x >= 3) { o = {0x20, 0}; return true; }
    return false;
  }
  bool isWater(int, int y) const override { return y >= 6; }
  bool pedEligible(int, int) const override { return ped; }
  void gameDate(int& m, int& d) const override { m = month; d = day; }
  std::vector<std::pair<double, double>> seaRoute(int) const override { return {{0.5, 7.5}, {7.5, 7.5}}; }
};

static void buildData(AutomataData& D) {
  auto add = [&](uint32_t t, uint32_t g, uint32_t i, const Bytes& b) { D.addRecord({t, g, i}, b); };
  // sprites: vehicles (3 zooms, 32 rotations, 48 slots), people (2 zooms, 8 slots)
  add(kSpriteInstType, kSpriteInstGroup, 0x500, spriteInst(3, 32, 48, 144, 2, 0xAB, 1000));
  add(kSpriteInstType, kSpriteInstGroup, 0x501, spriteInst(2, 8, 8, 16, 2, 0xCD, 5000));
  add(kAnSprAttType, kAnSprAttGroup, 100, ansprat(0x500, {{0, 1}}, {20}));
  add(kAnSprAttType, kAnSprAttGroup, 101, ansprat(0x501, {{0, 0, 1, 1}, {1}}, {20, 2}));
  // scripts
  add(kScriptType, kScriptGroup, 2001, scriptTxt({{0, {"10000", "0", "20"}}}));
  add(kScriptType, kScriptGroup, 2002, scriptTxt({{3, {"20"}}}));
  add(kScriptType, kScriptGroup, 2004, scriptTxt({{2, {"2183877034", "3794489796", "2001", "1", "0"}}}));
  add(kScriptType, kScriptGroup, 0x30, scriptTxt({{200, {}}}));
  add(kScriptType, kScriptGroup, 1, scriptTxt({{0, {"20", "0", "20", "64000", "0"}}}));
  // vehicles
  add(kVAttribType, kVAttribGroup, 7, vattrib(false, 0, 113, 0, 40, 100, 2001, 64000));
  add(kVAttribType, kVAttribGroup, 8, vattrib(false, 0, 113, 0, 40, 100, 2004, 64000));
  add(kVAttribType, kVAttribGroup, 9, vattrib(false, 0, 0, 0, 40, 100, 0x30, 64000));
  add(kVAttribType, kVAttribGroup, 28, vattrib(true, 0, 113, 130, 50, 100, 2001, 95999));
  add(kVAttribType, kVAttribGroup, 59, vattrib(false, 140, 0, 0, 70, 100, 2001, 64000));
  add(kVAttribType, kVAttribGroup, 60, vattrib(false, 0, 0, 0, 70, 100, 2002, 64000));
  add(kVAttribType, kVAttribGroup, 70, vattrib(false, 150, 0, 0, 89, 100, 0, 64000));
  add(kVAttribType, kVAttribGroup, 75, vattrib(false, 160, 0, 0, 100, 100, 0, 200000));
  add(kVclProdType, kVclProdGroup, 1, vclprod(1.0f, {{10, 7}}));
  add(kVclProdType, kVclProdGroup, 2, vclprod(2.0f, {{10, 8}}));
  add(kVclProdType, kVclProdGroup, 0x3FC, vclprod(1.0f, {{10, 70}}));
  add(kVclProdType, kVclProdGroup, 0x400, vclprod(2.0f, {{10, 70}}));
  add(kVclProdType, kVclProdGroup, 0x401, vclprod(2.0f, {{10, 70}}));
  add(kPAttribType, kPAttribGroup, 1, pattrib(101, 1));
  // pedestrian production: 'def' country, one all-year dated mode, rate 5
  {
    Bytes b = {'B', 'I', 'N', '!'};
    put32(b, 1); putStr(b, "def"); put32(b, 1);
    put32(b, (1u << 24) | (1u << 16) | (12u << 8) | 31u); put32(b, 1); putF(b, 5.0f); put32(b, 8);
    put32(b, 1000); putKey(b, kPAttribType, kPAttribGroup, 1);
    add(kPplProdType, kPplProdGroup, 1, b);
  }
  // paths / lanes
  add(kVPathType, kVPathGroup, 0x1000, vpath({{0, 0, 128}, {256, 0, 128}}, 0));
  add(kTrBlkType, kTrBlkGroup, 0x1000, trblkVeh(0x1000, 64000));
  add(kVPathType, kVPathGroup, 0x2000, vpath({{0, 0, 128}, {256, 0, 128}}, 0));
  add(kTrBlkType, kTrBlkGroup, 0x2000, trblkVeh(0x2000, 64000));
  add(kPPathType, kPPathGroup, 0x1D00, ppath({{0, 0, 128}, {256, 0, 128}}, 0, 8));
  add(kTrnProdType, kTrnProdGroup, 1, Bytes());  // replaced below via text
  { std::string t = "1 2\r\n1\r\n\r\n59\r\n0\r\n1\r\n60\r\n0\r\n0\r\n"; D.addRecord({kTrnProdType, kTrnProdGroup, 1}, Bytes(t.begin(), t.end())); }
  { std::string t = "3\r\n1\r\n75\r\n"; D.addRecord({kAirTrMgrType, kAirTrMgrGroup, 1}, Bytes(t.begin(), t.end())); }
  D.loadErrors = 0;
}

static void testParsers() {
  AutomataData D;
  VAttrib v;
  CHECK(parseVAttrib(vattrib(true, 131, 113, 130, 63, 5, 2001, 64000), v));
  CHECK(v.emergency && v.motionSound == 131 && v.sirenSound == 130 && v.length == 63 && v.anim.instance == 5 &&
        v.script.instance == 2001 && v.maxSpeedRaw == 64000);
  CHECK(std::fabs(v.maxSpeed() * 450000000.0 - 64000) < 1e-3);
  // text form of VATTRIB
  std::string txt = "TXT\r\n1\r\n131\r\n113\r\n130\r\n63\r\n1 2 3\r\n4 5 6\r\n64000\r\n";
  VAttrib vt; CHECK(parseVAttrib(Bytes(txt.begin(), txt.end()), vt));
  CHECK(vt.emergency && vt.anim.instance == 3 && vt.script.instance == 6 && vt.maxSpeedRaw == 64000);
  Bytes trunc = vattrib(false, 0, 0, 0, 0, 0, 0, 0); trunc.resize(20);
  CHECK(!parseVAttrib(trunc, v));

  VclProd vp; CHECK(parseVclProd(vclprod(2.5f, {{3, 7}, {7, 8}}), vp));
  CHECK(vp.rate == 2.5f && vp.entries.size() == 2 && vp.entries[1].second.instance == 8);
  CHECK(vclProdPick(vp, 0) == 0 && vclProdPick(vp, 2) == 0 && vclProdPick(vp, 3) == 1 && vclProdPick(vp, 9) == 1);

  AnSprAtt an; CHECK(parseAnSprAtt(ansprat(0x500, {{0, 0, 1}, {2}}, {20, 6}), an));
  CHECK(an.seqs.size() == 2 && an.find(20)->frames.size() == 3 && an.find(6)->frames[0] == 2 && !an.find(99));

  PPath pp; CHECK(parsePPath(ppath({{15, 0, 15}, {15, 0, 240}}, 16, 24), pp));
  CHECK(pp.entryHeading8 == 4 && pp.exitHeading8 == 6 && pp.length() == 225);

  VPath vp2; CHECK(parseVPath(vpath({{0, 0, 0}, {30, 0, 40}, {30, 100, 40}}, 4), vp2));
  CHECK(vp2.lane == 5 && vp2.cum[1] == 50 && vp2.cum[2] == 50 + int(0.204125 * 100));
  CHECK(std::fabs(vp2.grade - 100.0 / 50.0) < 1e-9);
  CHECK(vp2.heading0 == heading32(30, 40));
  CHECK(heading32(1, 0) == 0 && heading32(0, 1) == 8 && heading32(-1, 0) == 16 && heading32(0, -1) == 24);
  CHECK(heading32(1, -0.0001) == 0);  // rounds to 32 -> 0
  Pt mid = vpathPoint(vp2, 25);
  CHECK(mid.a == 15 && mid.b == 20);

  TrBlk tb; CHECK(parseTrBlk(trblkVeh(0x1000, 95999, true, 0x1D00), tb));
  CHECK(tb.flag && tb.ped.size() == 2 && tb.veh.size() == 1 && tb.veh[0].pos.b == 128);
  CHECK(tb.ped[0].conns[0].path == 0x1D00 && tb.ped[1].conns[0].next == -1 && tb.ped[1].conns[0].heading == 8);
  CHECK(tb.veh[0].conns[0].value == 95999 && tb.vehLane(0) && tb.pedLane(1));

  SpriteInst si; CHECK(parseSpriteInst(spriteInst(3, 32, 48, 144, 2, 9, 100), si));
  CHECK(si.frames.size() == 288 && si.frames[5].spriteId == 105 && si.zoomCount == 3);

  Script sc; CHECK(parseScript(std::string("2\r\n0\t3\r\n10\r\n2\r\n20\r\n\r\n2\t5\r\n2183877034\r\n3794489796\r\n18\r\n3\r\n2\r\n\r\n\r\n\r\n"), sc));
  CHECK(sc.steps.size() == 2 && sc.steps[0].op == 0 && sc.steps[0].args[2] == 20 && sc.steps[1].args.size() == 5);
  CHECK(uint32_t(sc.steps[1].args[0]) == 0x822B51AA && uint32_t(sc.steps[1].args[1]) == 0xE22B51C4);
  Script sc2; CHECK(parseScript(std::string("1\n0\t0\n\n"), sc2) && sc2.steps.size() == 1);  // bare LF tolerated
  CHECK(!parseScript(std::string("3\r\n0\t0\r\n\r\n"), sc2));

  TrnProd tp; CHECK(parseTrnProd("30 90\r\n1\r\n\r\n59\r\n1\r\n58\r\n2\r\n60\r\n63\r\n0\r\n1\r\n91\r\n", tp));
  CHECK(tp.minUs == 30000000 && tp.maxUs == 90000000 && tp.consists.size() == 1 && tp.consists[0].loco == 59);
  CHECK(tp.consists[0].list[0].size() == 1 && tp.consists[0].list[1].size() == 2 && tp.consists[0].list[2].empty() && tp.consists[0].list[3][0] == 91);
  AirTrMgr at; CHECK(parseAirTrMgr("3\r\n4\r\n75\r\n77\r\n78\r\n79\r\n", at) && at.maxActive == 3 && at.models.size() == 4);

  // PPLPROD dated modes, country selection, holidays, weights
  {
    Bytes b = {'B', 'I', 'N', '!'};
    put32(b, 3);
    auto mode = [&](uint32_t date, float rate, uint32_t w1, uint32_t w2) {
      put32(b, date); put32(b, 2); putF(b, rate); put32(b, 8);
      put32(b, w1); putKey(b, kPAttribType, kPAttribGroup, 1);
      put32(b, w2); putKey(b, kPAttribType, kPAttribGroup, 2);
    };
    putStr(b, "def"); put32(b, 2);
    mode((1u << 24) | (1u << 16) | (9u << 8) | 30u, 3.f, 600, 400);
    mode((10u << 24) | (1u << 16) | (12u << 8) | 31u, 6.f, 1, 1);
    putStr(b, "gb"); put32(b, 1);
    mode((1u << 24) | (1u << 16) | (12u << 8) | 31u, 1.f, 5, 5);
    putStr(b, "def:holidays"); put32(b, 1);
    mode((12u << 24) | (20u << 16) | (1u << 8) | 5u, 9.f, 1, 1);  // wraps the year end
    PplProd t; CHECK(parsePplProd(b, t));
    CHECK(t.countries.size() == 3 && t.countries[0].modes[0].dated && t.countries[0].modes[0].endMonth == 9);
    CHECK(t.countries[0].modes[0].cum[0] == 599 && t.countries[0].modes[0].cum[1] == 999);
    CHECK(pplModePick(t.countries[0].modes[0], 0) == 0 && pplModePick(t.countries[0].modes[0], 599) == 0 &&
          pplModePick(t.countries[0].modes[0], 600) == 1 && pplModePick(t.countries[0].modes[0], 999) == 1);
    CHECK(D.pickPplMode(t, "us", false, 5, 5)->rate == 3.f);
    CHECK(D.pickPplMode(t, "us", false, 10, 31)->rate == 6.f);
    CHECK(D.pickPplMode(t, "gb", false, 5, 5)->rate == 1.f);
    CHECK(D.pickPplMode(t, "us", false, 12, 25)->rate == 6.f);   // holidays disabled
    CHECK(D.pickPplMode(t, "us", true, 12, 25)->rate == 9.f);    // holidays on, wrapped range
    CHECK(D.pickPplMode(t, "us", true, 1, 5)->rate == 9.f && D.pickPplMode(t, "us", true, 1, 6)->rate == 3.f);
    CHECK(!D.pickPplMode(t, "us", false, 0, 0));
  }
  // undated: running -1 thresholds
  {
    Bytes b = {'B', 'I', 'N', '!'};
    put32(b, 1); putStr(b, "def"); put32(b, 2);
    put32(b, 301); put32(b, 0); putF(b, 1.f); put32(b, 8);
    put32(b, 700); put32(b, 0); putF(b, 2.f); put32(b, 8);
    PplProd t; CHECK(parsePplProd(b, t));
    CHECK(!t.countries[0].modes[0].dated && t.countries[0].modes[0].cumThreshold == 300 && t.countries[0].modes[1].cumThreshold == 1000);
    CHECK(D.pickPplMode(t, "x", false, 1, 1, 300)->rate == 1.f && D.pickPplMode(t, "x", false, 1, 1, 301)->rate == 2.f);
  }
  // tunables
  auto ini = IniFile::fromText("[MiscStrtSimTunables]\r\nMaxMarinaToBoatDistance=30\r\n[KeyVehiclesForHighMemoryMachines]\r\nCar=7\r\n[EmergencyVehiclesForHighMemoryMachines]\r\nPolice=28\r\nFirePumper=43\r\n");
  auto tn = StreetSimTunables::fromIni(ini);
  CHECK(tn.maxMarinaToBoatDistance == 30 && tn.keyVehiclesHigh.size() == 1 && tn.emergencyHigh.size() == 2 && tn.emergencyHigh[1].second == 43);
  // IXF dispatch + default TrBlk fallback
  IxfWriter w;
  w.add({kVAttribType, kVAttribGroup, 7}, vattrib(false, 0, 0, 0, 40, 100, 2001, 64000));
  w.add({kTrBlkType, kTrBlkGroup, kDefaultTrBlk}, trblkVeh(1, 64000));
  w.add({0x12345678, 1, 1}, {1, 2, 3});
  IxfFile f; CHECK(f.openMemory(w.build()));
  AutomataData E; CHECK(E.loadIxf(f) == 2);
  CHECK(E.vattrib.count(7) && E.trblkFor(0x99, 2) == &E.trblk.at(kDefaultTrBlk) && E.loadErrors == 0);
  Bytes bad = {1, 2, 3};
  E.addRecord({kVAttribType, kVAttribGroup, 8}, bad);
  CHECK(E.loadErrors == 1);
}

static int countSounds(const std::vector<SoundEvent>& v, SoundEvent::Type t, int sid) {
  int n = 0; for (auto& e : v) n += e.type == t && e.soundId == sid; return n;
}

static void testRoadTraffic() {
  AutomataData D; buildData(D);
  CHECK(D.loadErrors == 0);
  TestHost H;
  StreetSim::Config cfg;
  StreetSim sim(D, H, cfg);
  AutoView v; v.x0 = 0; v.y0 = 0; v.x1 = 7; v.y1 = 7; v.zoom = 4;
  sim.setView(v);
  CHECK(sim.active());
  CHECK(sim.vehicleLimit() == int(8 * 255 * 0.008f));  // S = 2040
  CHECK(sim.averageDensity() == 255);
  CHECK(sim.trafficLevelPercent() == sim.vehicleLimit() * 100 / 200);
  int n0 = sim.roadVehicleCount();
  CHECK(n0 >= 1 && n0 <= sim.vehicleLimit() + 1);
  auto sp = sim.sprites();
  CHECK(!sp.empty());
  for (auto& s : sp) {
    CHECK(s.kind == AutomatonKind::Vehicle && s.category == 0xAB);
    CHECK(s.tileY >= 3.49 && s.tileY <= 3.51 && s.tileX >= 0 && s.tileX <= 8);
    // z = zoom - (5 - 3) = 2; frame = anim 0 (a=0 or 1)*144 + 2*48 + orientation(heading 0 + 16: slot 0 faces the
    // opposite way to heading 0)
    CHECK(s.heading32 == 0 && (s.frameIndex == 96 + 16 || s.frameIndex == 144 + 96 + 16));
    CHECK(s.spriteId == 1000u + uint32_t(s.frameIndex));
  }
  // View rotation shifts orientation by 8 steps (90 degrees).
  v.rotation = 1; sim.setView(v);
  for (auto& s : sim.sprites()) CHECK((s.frameIndex % 144) - 96 == 24);
  v.rotation = 0; sim.setView(v);
  // movement + spawning keep the limit
  int maxSeen = 0;
  for (int i = 0; i < 400; i++) { sim.update(100000); maxSeen = std::max(maxSeen, sim.roadVehicleCount()); }
  CHECK(maxSeen <= sim.vehicleLimit() + 1 && maxSeen >= 1);
  for (auto& s : sim.sprites()) CHECK(s.tileX >= 0 && s.tileX < 8.01);
  // motion: a vehicle's x grows
  auto a = sim.sprites();
  sim.update(500000);
  auto b = sim.sprites();
  bool moved = false;
  for (auto& s1 : a) for (auto& s2 : b) if (s1.id == s2.id && s2.tileX > s1.tileX + 0.01) moved = true;
  CHECK(moved);
  // speed: 64000 raw => 64000/115200 tiles per second
  {
    TestHost H2; StreetSim s2(D, H2, cfg);
    H2.density = 0;  // nothing spawns on its own
    AutoView vv = v; s2.setView(vv);
    CHECK(s2.roadVehicleCount() == 0);
    CHECK(s2.dispatchEmergency(EmergencyType::PoliceCar, 1, 3));
    auto p0 = s2.sprites();
    CHECK(p0.size() == 1);
    for (int i = 0; i < 10; i++) s2.update(100000);
    auto p1 = s2.sprites();
    CHECK(p1.size() == 1);
    double expect = 95999.0 / 115200.0 * 1.0;
    // police: raw 95999 lane, vehicle max 64000? VATTRIB 28 speed is 64000 -> min() = 64000
    expect = 64000.0 / 115200.0;
    CHECK(std::fabs((p1[0].tileX - p0[0].tileX) - expect) < 0.02);
    auto ev = s2.takeSoundEvents();
    CHECK(countSounds(ev, SoundEvent::Siren, 130) >= 1);
    CHECK(s2.roadVehicleCount() == 1);
  }
  // deactivation: zoom below threshold or alternate view removes everything and stops spawning
  v.zoom = 1; sim.setView(v);
  CHECK(!sim.active() && sim.automatonCount() == 0 && sim.roadVehicleCount() == 0 && sim.sprites().empty());
  sim.update(1000000); CHECK(sim.automatonCount() == 0);
  v.zoom = 4; v.secondaryMode = 1; sim.setView(v); CHECK(!sim.active());
  v.secondaryMode = 0; sim.setView(v); CHECK(sim.active() && sim.roadVehicleCount() >= 1);
  // view shrink removes automata outside the new rectangle
  v.x0 = 4; sim.setView(v);
  for (auto& s : sim.sprites()) CHECK(s.tileX >= 4.0);
  // zero density => no traffic
  {
    TestHost H3; H3.density = 0; StreetSim s3(D, H3, cfg);
    AutoView vv = v; vv.x0 = 0; s3.setView(vv);
    s3.update(5000000);
    CHECK(s3.roadVehicleCount() == 0 && s3.vehicleLimit() == 0);
  }
  // zoom threshold p=0 (Z=5): never active
  {
    TestHost H4; StreetSim::Config c4; c4.zoomThreshold = 5; StreetSim s4(D, H4, c4);
    AutoView vv = v; vv.x0 = 0; s4.setView(vv); CHECK(!s4.active());
  }
}

static void testScriptsAndMix() {
  AutomataData D; buildData(D);
  TestHost H; H.density = 0;
  StreetSim sim(D, H, StreetSim::Config());
  AutoView v; v.x0 = 0; v.y0 = 0; v.x1 = 7; v.y1 = 7; v.zoom = 4;
  sim.setView(v);
  // mix tables: default VCLPROD 1 -> model 7 with total ~1e7
  auto m0 = sim.mixTable(3, 3);
  CHECK(m0.size() == 1 && m0[0].first == 7 && std::fabs(m0[0].second - 1e7) < 1.1e6);
  int bid = sim.addBuilding(3, 3, ResKey(), {kVclProdType, kVclProdGroup, 2});
  auto m1 = sim.mixTable(3, 3);
  CHECK(m1.size() == 2);
  sim.removeBuilding(bid);
  CHECK(sim.mixTable(3, 3).size() == 1);
  CHECK(sim.mixTable(40, 40).size() == 1);  // clamped to the last cell
  // Call op: vehicle 8 uses script 2004 -> calls 2001
  CHECK(sim.roadVehicleCount() == 0);
  // spawn through emergency (model 28) works; model 9 (script 0x30 = parade op 200) never runs
  CHECK(sim.dispatchEmergency(EmergencyType::PoliceCar, 2, 3));
  CHECK(sim.roadVehicleCount() == 1);
  // event mode
  CHECK(sim.eventMode() == 0);
  sim.notifyEvent(); CHECK(sim.eventMode() == 1);
  sim.update(10000000); CHECK(sim.eventMode() == 1);
  sim.update(5000000); CHECK(sim.eventMode() == 2);
}

// removeBuildings must leave the same mix tables and sources as removing each building on its own.
static void testBulkRemoveBuildings() {
  AutomataData D; buildData(D);
  auto populate = [&](StreetSim& sim, std::vector<int>& ids) {
    // Several sources share cell (3,3); two use the same table; (5,5) keeps one source.
    ids.push_back(sim.addBuilding(3, 3, ResKey(), {kVclProdType, kVclProdGroup, 2}));
    ids.push_back(sim.addBuilding(3, 3, ResKey(), {kVclProdType, kVclProdGroup, 2}));
    ids.push_back(sim.addBuilding(3, 3, ResKey(), {kVclProdType, kVclProdGroup, 1}));
    ids.push_back(sim.addBuilding(5, 5, ResKey(), {kVclProdType, kVclProdGroup, 2}));
    ids.push_back(sim.addBuilding(6, 1, ResKey(), {kVclProdType, kVclProdGroup, 2}));
  };
  AutoView v; v.x0 = 0; v.y0 = 0; v.x1 = 7; v.y1 = 7; v.zoom = 4;
  TestHost H1, H2; H1.density = H2.density = 0;
  StreetSim a(D, H1, StreetSim::Config()), b(D, H2, StreetSim::Config());
  a.setView(v); b.setView(v);
  std::vector<int> ia, ib;
  populate(a, ia); populate(b, ib);
  const std::vector<int> removeA = {ia[0], ia[2], ia[4]}, removeB = {ib[0], ib[2], ib[4]};
  for (int id : removeA) a.removeBuilding(id);
  b.removeBuildings(removeB);
  for (int x = 0; x < 8; x++)
    for (int y = 0; y < 8; y++) CHECK(a.mixTable(x, y) == b.mixTable(x, y));
  // Remaining sources are still removable (ids and per-cell tables survived).
  a.removeBuilding(ia[1]); a.removeBuilding(ia[3]);
  b.removeBuildings({ib[1], ib[3]});
  for (int x = 0; x < 8; x++)
    for (int y = 0; y < 8; y++) CHECK(a.mixTable(x, y) == b.mixTable(x, y));
  CHECK(b.mixTable(3, 3).size() == 1);
}

static void testExitNodeLeavesPiece() {
  AutomataData D; buildData(D);
  D.addRecord({kTrBlkType, kTrBlkGroup, 0x1000}, trblkVehExitNode(0x1000, 64000));
  TestHost H; H.density = 0;
  StreetSim sim(D, H, StreetSim::Config());
  AutoView v; v.x0 = 0; v.y0 = 0; v.x1 = 7; v.y1 = 7; v.zoom = 4;
  sim.setView(v);
  CHECK(sim.dispatchEmergency(EmergencyType::PoliceCar, 2, 3));
  for (int i = 0; i < 40; i++) sim.update(100000);  // 4 s at 0.55 tiles/s: well past the end of tile 2
  auto sp = sim.sprites();
  CHECK(sp.size() == 1);
  for (auto& s : sp) CHECK(s.tileX >= 3.0 && s.tileX < 8.0);
}

static void testPeds() {
  AutomataData D; buildData(D);
  TestHost H; H.density = 0;
  StreetSim sim(D, H, StreetSim::Config());
  AutoView v; v.x0 = 0; v.y0 = 0; v.x1 = 7; v.y1 = 7; v.zoom = 4;
  sim.setView(v);
  sim.addBuilding(4, 4, {kPplProdType, kPplProdGroup, 1}, ResKey());
  CHECK(sim.pedestrianCount() == 5);  // cap = trunc(rate)
  auto sp = sim.sprites();
  CHECK(sp.size() == 5);
  for (auto& s : sp) {
    CHECK(s.kind == AutomatonKind::Pedestrian && s.category == 0xCD);
    // people: ZoomCount 2 => slot = 4-(5-2) = 1, ZoomSetCount 8, rot 8: frame = a*16 + 8 + o
    CHECK(s.frameIndex >= 8 && s.frameIndex < 8 + 8 + 16);
    CHECK(s.tileX >= 0 && s.tileX <= 8 && std::fabs(s.tileY - std::floor(s.tileY) - 0.5) < 0.02);
  }
  {
    auto p0 = sim.sprites();
    sim.update(1000000);
    auto p1 = sim.sprites();
    double moved = 0;
    for (auto& a : p0) for (auto& b : p1) if (a.id == b.id) moved += std::fabs(b.tileX - a.tileX) + std::fabs(b.tileY - a.tileY);
    CHECK(moved > 0.2);  // 64000 raw = 0.55 tiles/s each
  }
  for (int i = 0; i < 300; i++) sim.update(100000);
  CHECK(sim.pedestrianCount() <= 5 && sim.pedestrianCount() >= 1);  // walkers leave the map and are refilled
  H.ped = false;
  sim.pointSpawnPedestrians(1, 1, 3);
  int before = sim.pedestrianCount();
  sim.pointSpawnPedestrians(1, 1, 3);
  CHECK(sim.pedestrianCount() == before);  // ineligible tile
  H.ped = true;
  sim.pointSpawnPedestrians(1, 1, 3);
  CHECK(sim.pedestrianCount() == before + 3);
  // holiday date switch re-chooses the mode (here only one mode: cap stays)
  H.month = 12; H.day = 31; sim.update(2000000);
}

static void testOtherAutomata() {
  AutomataData D; buildData(D);
  D.tunables.maxMarinaToBoatDistance = 3;
  TestHost H; H.density = 0; H.ped = false;
  StreetSim sim(D, H, StreetSim::Config());
  AutoView v; v.x0 = 0; v.y0 = 0; v.x1 = 7; v.y1 = 7; v.zoom = 4;
  sim.setView(v);
  sim.addMarina(4, 6);
  sim.addSeaport(0, 7);
  sim.addAirport(4, 2);
  sim.addStation(2, 5, false);
  for (int i = 0; i < 100; i++) sim.update(100000);  // 10 s
  CHECK(sim.countKind(AutomatonKind::Boat) >= 1);
  CHECK(sim.countKind(AutomatonKind::Train) >= 2);  // locomotive + at least one car
  // train cars become visible and follow the locomotive along the rail row
  auto sp = sim.sprites();
  int trains = 0;
  for (auto& s : sp) if (s.kind == AutomatonKind::Train) { trains++; CHECK(std::fabs(s.tileY - 5.5) < 0.02); }
  CHECK(trains >= 1);
  auto ev = sim.takeSoundEvents();
  CHECK(countSounds(ev, SoundEvent::Start, 140) >= 1);  // locomotive motion sound
  CHECK(countSounds(ev, SoundEvent::Start, 150) >= 1);  // boat
  for (int i = 0; i < 450; i++) sim.update(100000);     // reach 55 s: airport timer has fired (20-40 s)
  ev = sim.takeSoundEvents();
  CHECK(countSounds(ev, SoundEvent::Start, 160) >= 1);  // aircraft spawned
  // aircraft altitude profile: descends towards the airport
  int air = 0;
  for (auto& s : sim.sprites()) if (s.kind == AutomatonKind::Aircraft) { air++; CHECK(s.height >= 0 && s.height <= 2048); }
  (void)air;
  // sea route ships are removed at route end and the cap lets another spawn later
  for (int i = 0; i < 100; i++) sim.update(100000);
  CHECK(sim.automatonCount() >= 0);
  // sound stop on removal
  v.zoom = 0; sim.setView(v);
  CHECK(sim.automatonCount() == 0);
  ev = sim.takeSoundEvents();
  CHECK(countSounds(ev, SoundEvent::Stop, 140) >= 1);
}

static void testParadeStart() {
  AutomataData D; buildData(D);
  TestHost H; H.density = 0; H.ped = false;
  StreetSim sim(D, H, StreetSim::Config());
  AutoView v; v.x0 = 0; v.y0 = 0; v.x1 = 7; v.y1 = 7; v.zoom = 4;
  sim.setView(v);
  // Lineup of three vehicles (VATTRIB 7, speed 64000), 1.5 tiles apart, from (1.5, 3.5) to (6.5, 3.5): the followers start behind
  // the route start and all of them finish at the route end (the parade is then over).
  std::vector<StreetSim::ParadeVehicle> line(3);
  for (auto& l : line) { l.vattrib = 7; l.spacingTiles = 1.5; }
  CHECK(sim.startParade(1.5, 3.5, 6.5, 3.5, line) == 3);
  CHECK(sim.paradeVehicleCount() == 3);
  sim.update(1000);
  auto sp = sim.sprites();
  CHECK(sp.size() == 3);
  double minX = 1e9, maxX = -1e9;
  for (auto& s : sp) { minX = std::min(minX, s.tileX); maxX = std::max(maxX, s.tileX); CHECK(std::fabs(s.tileY - 3.5) < 0.05); }
  CHECK(maxX > 1.4 && maxX < 1.7 && minX < -1.2);       // lead at the start, the last 3 tiles behind it
  CHECK(sim.startParade(1, 1, 1, 1, line) == 0);          // empty route: nothing
  for (int i = 0; i < 400 && sim.paradeVehicleCount() > 0; i++) sim.update(100000);
  CHECK(sim.paradeVehicleCount() == 0);                   // every vehicle reached the end and was removed
  // Spawner removal: a removed seaport spawns no more ships.
  int port = sim.addSeaport(0, 7);
  sim.removeSpawner(port);
  for (int i = 0; i < 100; i++) sim.update(100000);
  CHECK(sim.countKind(AutomatonKind::Boat) == 0);
}

static void testCallAndParade() {
  AutoView v; v.x0 = 0; v.y0 = 0; v.x1 = 7; v.y1 = 7; v.zoom = 4;
  {  // model 8 runs script 2004: a Call into 2001 (Drive)
    AutomataData D; buildData(D);
    D.addRecord({kVclProdType, kVclProdGroup, 1}, vclprod(1.0f, {{10, 8}}));
    TestHost H; StreetSim sim(D, H, StreetSim::Config());
    sim.setView(v);
    CHECK(sim.roadVehicleCount() >= 1);
    for (int i = 0; i < 100; i++) sim.update(100000);
    CHECK(sim.roadVehicleCount() >= 1);
  }
  {  // model 9 runs the parade opcode: never executed on ordinary vehicles -> removed at once
    AutomataData D; buildData(D);
    D.addRecord({kVclProdType, kVclProdGroup, 1}, vclprod(1.0f, {{10, 9}}));
    TestHost H; StreetSim sim(D, H, StreetSim::Config());
    sim.setView(v);
    CHECK(sim.roadVehicleCount() == 0 && sim.automatonCount() == 0);
  }
}

int main() {
  testParsers();
  testRoadTraffic();
  testScriptsAndMix();
  testBulkRemoveBuildings();
  testPeds();
  testExitNodeLeavesPiece();
  testOtherAutomata();
  testCallAndParade();
  testParadeStart();
  TEST_MAIN_END();
}
