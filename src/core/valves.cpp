#include "core/valves.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include "core/records.h"

namespace m3k {
namespace growth {
namespace {

std::vector<std::string> split(const std::string& s, char d) {
  std::vector<std::string> v; std::string cur;
  for (char c : s) { if (c == d) { v.push_back(trimSpaces(cur)); cur.clear(); } else cur.push_back(c); }
  v.push_back(trimSpaces(cur));
  return v;
}
int toInt(const std::string& s) { return int(std::strtol(s.c_str(), nullptr, 10)); }
int64_t clamp64(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
int32_t truncF(float f, int64_t lo, int64_t hi) {
  if (!(f == f)) return 0;
  double d = std::trunc(double(f));
  return int32_t(clamp64(d < -9e18 ? INT64_MIN : (d > 9e18 ? INT64_MAX : int64_t(d)), lo, hi));
}
int32_t roundEven(double d) { return int32_t(std::nearbyint(d)); }

}  // namespace

int curveValue(const std::vector<CurvePoint>& pts, int64_t P) {
  if (pts.empty()) return 0;
  if (pts.size() < 2) return pts[0].y;
  for (size_t i = 0; i + 1 < pts.size(); i++) {
    int64_t lo = int64_t(pts[i].x) * 1000, hi = int64_t(pts[i + 1].x) * 1000;
    if (lo <= P && P < hi) {
      int64_t dy = std::llabs(int64_t(pts[i + 1].y) - pts[i].y);
      return int(pts[i].y + (P - lo) * dy / (hi - lo));
    }
  }
  return pts.back().y;
}

// ---------------- Valve ----------------
void Valve::update() {
  Slast = Sacc; Dlast = Dacc; Sacc = Sbase; Dacc = Dbase;
  int64_t raw = clamp64(int64_t(Dlast) - Slast, Min, Max);
  int64_t raw2 = clamp64(raw + truncF((f1 + f2) * float(Max), INT32_MIN, INT32_MAX), Min, Max);
  int64_t cap = int64_t(Max) - int64_t(capPct) * Max / 100;
  int64_t target = std::max(std::min(raw2, cap), int64_t(Min));
  if (!smooth) V = int32_t(target);
  else {
    int64_t step = (int64_t(Max) - Min) * 7 / 100, delta = target - V;
    if (-step < delta && delta < step) V = int32_t(target);
    else if (delta > 0) V = (target > 0 && V < 0 && int64_t(V) + step < 0) ? 0 : int32_t(int64_t(V) + step);
    else V = (target < 0 && V > 0 && int64_t(V) - step > 0) ? 0 : int32_t(int64_t(V) - step);
  }
  int64_t n = V >= 0 ? (Max ? int64_t(V) * 1000 / Max : 0) : (Min ? -(int64_t(V) * 1000) / Min : 0);
  N = int32_t(n);
}

void Valve::write(Bytes& b) const {
  put32(b, id); put32(b, uint32_t(Slast)); put32(b, uint32_t(Dlast)); put32(b, uint32_t(Sacc)); put32(b, uint32_t(Dacc));
  uint32_t u; std::memcpy(&u, &f1, 4); put32(b, u); std::memcpy(&u, &f2, 4); put32(b, u);
  b.push_back(capPct); put32(b, uint32_t(V)); put32(b, uint32_t(Sbase)); put32(b, uint32_t(Dbase));
}
Valve Valve::read(Reader& r) {
  Valve v; v.id = r.u32(); v.Slast = r.s32(); v.Dlast = r.s32(); v.Sacc = r.s32(); v.Dacc = r.s32();
  v.f1 = r.f32(); v.f2 = r.f32(); v.capPct = r.u8(); v.V = r.s32(); v.Sbase = r.s32(); v.Dbase = r.s32();
  return v;
}

// ---------------- ValveTuning ----------------
std::vector<ValveItem> ValveTuning::parseItems(const std::string& text) {
  std::string v = text;
  size_t sp = v.find_first_of(" \t;");
  if (sp != std::string::npos) v = v.substr(0, sp);
  std::vector<ValveItem> out;
  for (const auto& item : split(v, '+')) {
    auto f = split(item, ':');
    if (f.size() < 3) continue;
    out.push_back({toInt(f[0]), toInt(f[1]), toInt(f[2])});
  }
  return out;
}
void ValveTuning::parseAgentValveEffects(const IniFile& ini) {
  for (const auto& kv : ini.section("AgentValveEffects")) agentEffects[parseIdNumber(kv.first)] = parseItems(kv.second);
}
void ValveTuning::parseConnectionEffects(const IniFile& ini) {
  for (const auto& kv : ini.section("ConnectionValveEffects")) connectionEffects[toInt(kv.first)] = parseItems(kv.second);
}
void ValveTuning::parseOrdinanceEffects(const IniFile& ini) {
  for (const auto& kv : ini.section("OrdinanceEffects")) {
    std::vector<OrdinanceValveEffect> out;
    std::string v = kv.second;
    size_t sp = v.find_first_of(" \t;"); if (sp != std::string::npos) v = v.substr(0, sp);
    for (const auto& item : split(v, '+')) {
      auto f = split(item, ',');
      if (f.size() < 2) continue;
      out.push_back({toInt(f[0]), toInt(f[1])});
    }
    ordinanceEffects[parseIdNumber(kv.first)] = out;
  }
}
void ValveTuning::parseEconomicPhases(const std::string& text) {
  phaseA.clear(); phaseB.clear();
  for (const auto& tok : split(text, ' ')) {
    if (tok.empty()) continue;
    auto f = split(tok, ':');
    if (f.size() < 3) continue;
    phaseA.push_back({toInt(f[0]), toInt(f[1])});
    phaseB.push_back({toInt(f[0]), toInt(f[2])});
  }
  if (phaseA.empty() || phaseA.front().x != 0) { phaseA.insert(phaseA.begin(), {0, 1}); phaseB.insert(phaseB.begin(), {0, 1}); }
}
void ValveTuning::parseWorldMapping(int valveId, const std::string& text) {
  std::vector<Cycle> out;
  for (const auto& tok : split(text, ' ')) {
    if (tok.empty()) continue;
    auto f = split(tok, ':');
    if (f.size() < 2) continue;
    Cycle c{toInt(f[0]), toInt(f[1])};
    if (c.duration == 0) break;
    out.push_back(c);
  }
  worldCycles[valveId] = out;
}
void ValveTuning::parseWorldLayer(const IniFile& ini) {
  std::string s;
  if (ini.get("WorldLayer", "JobToResFactor", s)) jobToResFactor = std::max(1, toInt(s) & 0xFF);
  if (ini.get("WorldLayer", "ExternalJobDemand", s)) externalJobDemand = toInt(s);
  if (ini.get("WorldLayer", "EconomicPhases", s)) parseEconomicPhases(s);
  for (const auto& kv : ini.section("ListOfWorldValveMapping")) parseWorldMapping(toInt(kv.first), kv.second);
}
void ValveTuning::parseResLayer(const IniFile& ini) { std::string s; if (ini.get("TuningParameters", "ExternalFunSupply", s)) externalFunSupply = toInt(s); }
void ValveTuning::parseComLayer(const IniFile& ini) { std::string s; if (ini.get("TuningParameters", "InitialConnectionSupply", s)) comInitialConnection = std::max(0, toInt(s)); }
void ValveTuning::parseIndLayer(const IniFile& ini) {
  std::string s;
  if (ini.get("TuningParameters", "InitialConnectionSupply", s)) indInitialConnection = std::max(0, toInt(s));
  if (ini.get("TuningParameters", "PowerConservationOrdEffect", s)) powerConservationOrdEffect = toInt(s);
  if (ini.get("TuningParameters", "WaterConservationOrdEffect", s)) waterConservationOrdEffect = toInt(s);
}
const std::vector<ValveItem>* ValveTuning::effectsFor(uint32_t t) const {
  // Exact agent type, else the nearest ancestor that has an entry; only one entry applies (spec 15 s3.3) [STATIC].
  uint32_t cur = t;
  for (int hop = 0; hop < 32; hop++) {
    auto it = agentEffects.find(cur);
    if (it != agentEffects.end()) return &it->second;
    auto p = parent.find(cur);
    if (p == parent.end() || p->second == cur) return nullptr;
    cur = p->second;
  }
  return nullptr;
}

void ValveTuning::parseTaxation(const std::string& text) {
  taxTable.clear();
  for (const auto& ent : split(text, ';')) {
    auto f = split(ent, ',');
    if (f.size() < 4) continue;
    float a = float(toInt(f[0])) * 0.0001f, b = float(toInt(f[1])) * 0.0001f;
    int n = std::min(toInt(f[2]), 22);
    TaxBreak tb; tb.pop = toInt(f[3]);
    for (int r = 0; r <= 22; r++) {
      if (r < n) tb.e[r] = float(n - r) * a / float(n);
      else if (r == n) tb.e[r] = 0;
      else tb.e[r] = float(r - n) * b / float(22 - n);
    }
    taxTable.push_back(tb);
  }
  std::sort(taxTable.begin(), taxTable.end(), [](const TaxBreak& x, const TaxBreak& y) { return x.pop < y.pop; });
}
bool ValveTuning::taxEffects(int64_t pop, std::array<float, 23>& out) const {
  int lo = -1;
  for (size_t i = 0; i < taxTable.size(); i++) if (taxTable[i].pop <= pop) lo = int(i);
  if (lo < 0) return false;
  if (taxTable[lo].pop == pop || size_t(lo) + 1 >= taxTable.size()) { out = taxTable[lo].e; return true; }
  const auto& L = taxTable[lo]; const auto& U = taxTable[lo + 1];
  for (int r = 0; r < 23; r++) out[r] = L.e[r] + (U.e[r] - L.e[r]) * float(pop) / float(U.pop - L.pop);  // original defect: pop, not pop - lo
  return true;
}

uint8_t capFromAmenity(const Valve& amenity) {
  int64_t S = amenity.Slast, D = amenity.Dlast;
  if (D == 0) return 0;
  if (S < D) return 100;
  return uint8_t(std::max<int64_t>(0, std::min<int64_t>(100, 100 * D / S)));
}

// ---------------- ValveLayer ----------------
void ValveLayer::init(int w, int h, const ValveTuning& t) {
  W = w; H = h; tune_ = t;
  v3000 = Valve(); v3001 = Valve(); v5000 = Valve(); v5001 = Valve(); v8000 = Valve(); v8001 = Valve(); v7000 = Valve();
  v3000.id = 3000; v3001.id = 3001; v5000.id = 5000; v5001.id = 5001; v8000.id = 8000; v8001.id = 8001; v7000.id = 7000;
  v3001.Sbase = tune_.externalFunSupply;
  v5001.Sbase = std::max(0, tune_.comInitialConnection);
  v8001.Sbase = std::max(0, tune_.indInitialConnection);
  v5001.Sacc = v5001.Sbase; v8001.Sacc = v8001.Sbase; v3001.Sacc = v3001.Sbase;
  applyLimits();
  effectMap.assign(size_t(W) * H, 0); effectAcc = effectMap;
  resUnits = comUnits = indUnits = resAcc = comAcc = indAcc = 0;
  residentialState.assign(1900, 0);
  for (int c = 0; c < 3; c++) {  // chart scales 10000/150/89 (spec 10 6.11)
    uint32_t sc = c == 0 ? 10000 : (c == 1 ? 150 : 89);
    wr32(&residentialState[size_t(c) * 132 + 128], sc);
  }
  world = WorldRec();
  Bytes d = defaultWorldRecord(); parseWorld(d.data(), d.size());
  sumC_ = sumI_ = 0; phaseA_ = phaseB_ = 1;
  setPhase(0);
}
void ValveLayer::applyLimits() {
  for (Valve* v : {&v3000, &v5000, &v8000, &v7000}) v->setLimits(2000, -2000);
  for (Valve* v : {&v3001, &v5001, &v8001}) v->setLimits(INT32_MAX, INT32_MIN);
  // Spec 15 s3.2 "Smoothing" [STATIC]: the smooth flag is set on every valve when it is created and never cleared (not
  // saved). Step = 7% of (Max - Min): 280 for the +-2000 valves, 300,647,710 for the unbounded ones.
  for (Valve* v : {&v3000, &v3001, &v5000, &v5001, &v8000, &v8001, &v7000}) v->smooth = true;
}
Valve& ValveLayer::valve(int id) {
  switch (id) { case 3000: return v3000; case 3001: return v3001; case 5000: return v5000; case 5001: return v5001; case 8000: return v8000; case 8001: return v8001; default: return v7000; }
}
const Valve& ValveLayer::valve(int id) const { return const_cast<ValveLayer*>(this)->valve(id); }

void ValveLayer::setPhase(int64_t pop) {
  if (tune_.phaseA.empty()) { phaseA_ = phaseB_ = 1; return; }
  phaseA_ = curveValue(tune_.phaseA, pop); phaseB_ = curveValue(tune_.phaseB, pop);
  if (phaseA_ < 0) phaseA_ = 0;
  if (phaseB_ < 0) phaseB_ = 0;
}
void ValveLayer::setWorldModifiers(int year, int month) {
  int64_t pos = int64_t(std::max(year, 1900) - 1900) * 12 + (month - 1);
  for (const auto& kv : tune_.worldCycles) {
    int64_t total = 0;
    for (const auto& c : kv.second) total += c.duration;
    if (total <= 0) continue;
    int64_t p = pos % total, acc = 0;
    for (const auto& c : kv.second) {
      if (p < acc + c.duration) { valve(kv.first).f1 = float(c.pct) / 100.0f; break; }
      acc += c.duration;
    }
  }
}
void ValveLayer::setTaxModifiers(int64_t pop, const int rates[3]) {
  std::array<float, 23> e;
  if (!tune_.taxEffects(pop, e)) return;
  v3000.f2 = e[clamp64(rates[0], 0, 22)]; v5000.f2 = e[clamp64(rates[1], 0, 22)]; v8000.f2 = e[clamp64(rates[2], 0, 22)];
}

void ValveLayer::addWorkforce(int64_t n) {
  v7000.Sacc += int32_t(n);
  if (n <= 0) return;
  int64_t a = phaseA_, b = phaseB_;
  if (a + b == 0) a = b = 1;
  int64_t c = n * a / (a + b), i = n * b / (a + b), r = n - c - i;
  if (r > 0) {
    double share = (sumC_ + sumI_) ? double(sumC_) / double(sumC_ + sumI_) : 0.0;
    if (share >= double(a) / double(a + b)) i += r; else c += r;
  }
  v5000.Dacc += int32_t(c); v8000.Dacc += int32_t(i);
  sumC_ += c; sumI_ += i;
}
void ValveLayer::addJobsDemand(int64_t n) {
  v7000.Dacc += int32_t(n);
  v3000.Dacc += int32_t(int64_t(std::max(1, tune_.jobToResFactor)) * n);
}

void ValveLayer::amenityBuildingDemand(const std::vector<ValveBuilding>& bs, int64_t& d5, int64_t& d8) const {
  d5 = d8 = 0;
  for (const auto& b : bs) {
    if (!b.operating) continue;
    const std::vector<ValveItem>* items = tune_.effectsFor(b.agentType);
    if (!items) continue;
    int64_t tiles = int64_t(b.sizeX) * b.sizeZ;
    for (const ValveItem& vi : *items) {
      if (vi.valve == kValveComConn) d5 += tiles * vi.demand * b.serviceMul;
      else if (vi.valve == kValveIndConn) d8 += tiles * vi.demand * b.serviceMul;
    }
  }
}

void ValveLayer::addBuilding(const ValveBuilding& b) {
  if (!b.operating) return;
  const std::vector<ValveItem>* items = tune_.effectsFor(b.agentType);
  if (!items) return;
  int64_t tiles = int64_t(b.sizeX) * b.sizeZ;
  for (const ValveItem& vi : *items) {
    int64_t s = tiles * vi.supply * b.serviceMul, d = tiles * vi.demand * b.serviceMul;
    if (vi.valve == kValveComConn) d = int64_t(std::llround(double(d) * amenityDemandMul[0]));
    else if (vi.valve == kValveIndConn) d = int64_t(std::llround(double(d) * amenityDemandMul[1]));
    bool mapped = vi.valve == 3000 || vi.valve == 5000 || vi.valve == 8000, jobs = vi.valve == 7000;
    if (mapped || jobs) {
      int add = jobs ? vi.demand : vi.supply;
      for (int x = b.x; x < b.x + b.sizeX; x++) for (int z = b.z; z < b.z + b.sizeZ; z++)
        if (x >= 0 && z >= 0 && x < W && z < H) {
          uint8_t& c = effectAcc[size_t(x) * H + z];
          c = uint8_t(clamp64(int64_t(c) + add, 0, 255));
        }
    }
    if (mapped) {   // [OBSERVED] every building with a valve 3000/5000/8000 item counts (saved resUnits = the resident count)
      uint32_t u = uint32_t(tiles * vi.supply);
      (vi.valve == 3000 ? resAcc : vi.valve == 5000 ? comAcc : indAcc) += u;
    }
    if (jobs) {
      if (d) addJobsDemand(d);
      if (s) addWorkforce(s);
    } else {
      Valve& v = valve(vi.valve);
      v.Sacc += int32_t(s); v.Dacc += int32_t(d);
      if (vi.valve == 3000 && s != 0) addWorkforce(roundEven(double(s) * wf_));
    }
  }
}

void ValveLayer::startJobsCycle() {
  sumC_ = sumI_ = 0;
  v7000.Sacc = v7000.Sbase; v7000.Dacc = v7000.Dbase;
  if (tune_.externalJobDemand) addWorkforce(tune_.externalJobDemand);
}
void ValveLayer::finishCycle() {
  effectMap = effectAcc; std::fill(effectAcc.begin(), effectAcc.end(), 0);
  resUnits = resAcc; comUnits = comAcc; indUnits = indAcc; resAcc = comAcc = indAcc = 0;
}

void ValveLayer::monthlyUpdate(const ValveCityInput& in) {
  wf_ = in.workforceFraction;
  setPhase(in.population);
  setWorldModifiers(in.year, in.month);
  int rates[3] = {in.taxRate[0], in.taxRate[1], in.taxRate[2]};
  setTaxModifiers(in.population, rates);
  if (effectAcc.size() != size_t(W) * H) { effectAcc.assign(size_t(W) * H, 0); effectMap.assign(size_t(W) * H, 0); }
  for (const auto& b : in.buildings) addBuilding(b);
  if (in.population <= 0 && tune_.bootstrapResDemand > 0) v3000.Dacc += tune_.bootstrapResDemand;
  for (int k = 0; k < 4; k++) {
    auto it = tune_.connectionEffects.find(k);
    if (it == tune_.connectionEffects.end()) continue;
    for (const auto& vi : it->second) {
      Valve& v = valve(vi.valve);
      v.Sacc += int32_t(int64_t(in.connectionCount[k]) * vi.supply); v.Dacc += int32_t(int64_t(in.connectionCount[k]) * vi.demand);
    }
  }
  for (uint32_t ord : in.enactedOrdinances) {
    auto it = tune_.ordinanceEffects.find(ord);
    if (it == tune_.ordinanceEffects.end()) continue;
    for (const auto& e : it->second) {
      Valve& v = valve(e.valve);
      v.Dacc += truncF(float(v.Dlast) * float(e.permille) / 1000.0f, INT32_MIN, INT32_MAX);
    }
  }
  for (Valve* v : {&v3000, &v3001, &v5000, &v5001, &v8000, &v8001}) v->update();
  v7000.update();
  // Cap rule (spec 15 s3.2) [STATIC]: after the valve update, from Slast/Dlast of the amenity valve (3001, 5001, 8001);
  // takes effect at the next monthly update.
  v3000.capPct = capFromAmenity(v3001); v5000.capPct = capFromAmenity(v5001); v8000.capPct = capFromAmenity(v8001);
  // Industrial ordinance demand cut (spec 15 s3.2) [STATIC]: directly after the industrial cap, applied next cycle.
  if (v8000.Dlast > 0) {
    auto cut = [&](uint32_t ord, int effect) {
      if (in.enactedOrdinances.count(ord)) v8000.Dacc += int32_t(int64_t(effect) * v8000.Dlast / -100);   // truncates toward 0
    };
    cut(0x82B9999B, tune_.powerConservationOrdEffect);
    cut(0x02BF1DFA, tune_.waterConservationOrdEffect);
  }
  finishCycle();
  startJobsCycle();
}

// ---------------- records ----------------
namespace {
void putMarkerStr(Bytes& b, bool on, const char* s) { putMarker(b, on, s); }
}

bool ValveLayer::parseResidential(const uint8_t* p, size_t n) {
  try {
    RecHeader h = parseRecHeader(p, n);
    if (h.version > 3) return false;
    Reader r(p, n); r.seek(h.bodyOffset); skipMarker(r, h);
    if (r.u32() != 2) return false;
    Valve a = Valve::read(r), b = Valve::read(r);
    Bytes st = r.bytes(1900);
    (void)a; v3000 = a; v3001 = b; residentialState = st;
    applyLimits();
    return true;
  } catch (const FormatError&) { return false; }
}
static bool parseTwo(const uint8_t* p, size_t n, int maxVer, Valve& a, Valve& b) {
  try {
    RecHeader h = parseRecHeader(p, n);
    if (h.version > maxVer || h.version < maxVer) return false;  // older layouts unspecified: treat as absent
    Reader r(p, n); r.seek(h.bodyOffset); skipMarker(r, h);
    if (r.u32() != 2) return false;
    a = Valve::read(r); b = Valve::read(r);
    return true;
  } catch (const FormatError&) { return false; }
}
bool ValveLayer::parseCommercial(const uint8_t* p, size_t n) {
  Valve a, b; if (!parseTwo(p, n, 2, a, b)) return false; v5000 = a; v5001 = b; applyLimits(); return true;
}
bool ValveLayer::parseIndustrial(const uint8_t* p, size_t n) {
  Valve a, b; if (!parseTwo(p, n, 2, a, b)) return false; v8000 = a; v8001 = b; applyLimits(); return true;
}
Bytes ValveLayer::writeResidential(bool m) const {
  Bytes d; writeRecHeader(d, 3, m); putMarkerStr(d, m, "Residential Layer Start");
  put32(d, 2); v3000.write(d); v3001.write(d);
  Bytes st = residentialState; st.resize(1900, 0); d.insert(d.end(), st.begin(), st.end());
  putMarkerStr(d, m, "Residential Layer End");
  return d;
}
Bytes ValveLayer::writeCommercial(bool m) const {
  Bytes d; writeRecHeader(d, 2, m); putMarkerStr(d, m, "Commercial Layer Start");
  put32(d, 2); v5000.write(d); v5001.write(d); putMarkerStr(d, m, "Commercial Layer End");
  return d;
}
Bytes ValveLayer::writeIndustrial(bool m) const {
  Bytes d; writeRecHeader(d, 2, m); putMarkerStr(d, m, "Industrial Layer Start");
  put32(d, 2); v8000.write(d); v8001.write(d); putMarkerStr(d, m, "Industrial Layer End");
  return d;
}

// Row order: file rows are x = W-1 .. 0, bytes z = 0..H-1.
bool ValveLayer::parseRci(const uint8_t* p, size_t n) {
  try {
    RecHeader h = parseRecHeader(p, n);
    if (h.version != 2) return false;
    if (W <= 0 || H <= 0) return false;
    Reader r(p, n); r.seek(h.bodyOffset); skipMarker(r, h);
    size_t N = size_t(W) * H;
    std::vector<uint8_t> a(N), b(N);
    for (std::vector<uint8_t>* g : {&a, &b})
      for (int row = 0; row < W; row++) for (int z = 0; z < H; z++) (*g)[size_t(W - 1 - row) * H + z] = r.u8();
    uint32_t u[6]; for (auto& x : u) x = r.u32();
    effectMap = a; effectAcc = b;
    resUnits = u[0]; comUnits = u[1]; indUnits = u[2]; resAcc = u[3]; comAcc = u[4]; indAcc = u[5];
    return true;
  } catch (const FormatError&) { return false; }
}
Bytes ValveLayer::writeRci(bool m) const {
  Bytes d; writeRecHeader(d, 2, m); putMarkerStr(d, m, "RCI Layer Start");
  size_t N = size_t(W) * H;
  for (const std::vector<uint8_t>* g : {&effectMap, &effectAcc})
    for (int row = 0; row < W; row++) for (int z = 0; z < H; z++) d.push_back(g->size() == N ? (*g)[size_t(W - 1 - row) * H + z] : 0);
  for (uint32_t u : {resUnits, comUnits, indUnits, resAcc, comAcc, indAcc}) put32(d, u);
  putMarkerStr(d, m, "RCI Layer End");
  return d;
}

bool ValveLayer::parseWorld(const uint8_t* p, size_t n) {
  try {
    RecHeader h = parseRecHeader(p, n);
    if (h.version > 2 || h.version < 1) return false;
    Reader r(p, n); r.seek(h.bodyOffset); skipMarker(r, h);
    WorldRec w;
    w.hadV0 = h.version >= 2;
    int first = w.hadV0 ? 0 : 1;
    for (int i = first; i < 9; i++) w.v[i] = r.s32();
    for (auto& x : w.phaseParams) x = r.s32();
    w.flag = r.u8();
    for (auto& c : w.charts) c.read(r);
    w.last = r.u32();
    world = w;
    // v0, v1 = jobs valve Slast, Dlast (spec 10 s6.13) [STATIC]; the jobs valve has no record of its own.
    if (w.hadV0) { v7000.Slast = w.v[0]; v7000.Dlast = w.v[1]; }
    return true;
  } catch (const FormatError&) { return false; }
}
Bytes ValveLayer::writeWorld(bool m) const {
  Bytes d; writeRecHeader(d, 2, m); putMarkerStr(d, m, "World Layer Start");
  for (int i = 0; i < 9; i++) put32(d, uint32_t(world.v[i]));
  for (int x : world.phaseParams) put32(d, uint32_t(x));
  d.push_back(world.flag);
  for (const auto& c : world.charts) c.write(d);
  put32(d, world.last);
  putMarkerStr(d, m, "World Layer End");
  return d;
}

int unemploymentPercent(int64_t R, double w, int64_t J) {
  int64_t K = int64_t(std::nearbyint(double(R) * w));   // resident workers, without ExternalJobDemand
  if (R == 0 || K <= J) return 0;
  if (J <= 0) return 100;
  return int((uint64_t(100) * uint64_t(K) - uint64_t(100) * uint64_t(J)) / (uint64_t(2) * uint64_t(K)));
}

void ValveLayer::worldMonthly(double w, int month, int year) {
  world.v[0] = v7000.Slast; world.v[1] = v7000.Dlast; world.v[2] = tune_.externalJobDemand;
  uint32_t u = uint32_t(unemploymentPercent(v3000.Slast, w, v7000.Dlast));
  auto push = [&](int base, int n) { auto& a = world.charts[3].w; for (int i = n - 1; i > 0; i--) a[size_t(base + i)] = a[size_t(base + i - 1)]; a[size_t(base)] = u; };
  push(0, 12);
  if (month == 1) { push(12, 10); if (year % 10 == 0) push(22, 10); }
}

Bytes defaultWorldRecord() {
  // Spec 10 6.13: v2 = 150, scales 100000/10000/10000/100/10000, everything else 0 (terrain-like).
  Bytes d; writeRecHeader(d, 2);
  int32_t v[9] = {0, 0, 150, 0, 0, 0, 0, 0, 0};
  for (int32_t x : v) put32(d, uint32_t(x));
  for (int i = 0; i < 4; i++) put32(d, 0);
  d.push_back(0);
  uint32_t scales[5] = {100000, 10000, 10000, 100, 10000};
  for (uint32_t s : scales) ChartBlock::filled(0, s).write(d);
  put32(d, 0);
  return d;
}

}  // namespace growth
}  // namespace m3k
