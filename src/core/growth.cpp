#include "core/growth.h"
#include <algorithm>
#include <iterator>
#include <cmath>
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
int rne(double d) { return int(std::nearbyint(d)); }
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

int SimRand::rand(int64_t n) {
  if (n <= 0) return 0;
  if (n < 0xFFFFFF) return int(next() % uint32_t(n));
  for (;;) { uint32_t r = next(); if (int64_t(r) <= n) return int(r); }
}

// ---------------- configuration ----------------
ZoneDescription ZoneDescription::parse(int zone, const std::string& value) {
  ZoneDescription zd; zd.zone = zone;
  // Airport, seaport and landfill zones have no category list ("14=0xc1f81e7e,3000", "17=0x82348de5,0"): the head still
  // names the developer class, so parse it even when there is no ':'.
  size_t colon = value.find(':');
  auto head = split(colon == std::string::npos ? value : value.substr(0, colon), ',');
  if (head.size() >= 1) zd.developerClass = parseIdNumber(head[0]);
  if (head.size() >= 2) zd.baseAgentType = parseIdNumber(head[1]);
  if (colon == std::string::npos) return zd;
  for (const auto& c : split(value.substr(colon + 1), ';')) {
    if (c.empty()) continue;
    auto f = split(c, ',');
    Category cat;
    if (f.size() == 4) { cat.agentType = parseIdNumber(f[0]); cat.density = toInt(f[1]); cat.maxLV = toInt(f[2]); cat.minLV = toInt(f[3]); }
    else if (f.size() >= 5) { cat.agentType = parseIdNumber(f[0]); cat.cls = f[1].empty() ? 'N' : f[1][0]; cat.density = toInt(f[2]); cat.maxLV = toInt(f[3]); cat.minLV = toInt(f[4]); }
    else continue;
    zd.cats.push_back(cat);
  }
  return zd;
}

void DevRule::addMember(uint32_t cat, const BuildingChoice& b, int weight) {
  CategoryLists& l = lists[cat];
  std::vector<BuildingChoice>& dst = b.isFiller() ? l.filler : (std::max(b.sizeX, b.sizeZ) >= 2 ? l.large : l.small);
  for (int i = 0; i < std::max(1, weight); i++) dst.push_back(b);
}

void GrowthConfig::parseZoneDescriptions(const IniFile& ini) {
  for (const auto& kv : ini.section("ZoneDeveloperDescriptions")) {
    int z = int(parseIdNumber(kv.first));
    zones[z] = ZoneDescription::parse(z, kv.second);
  }
}
std::vector<RuleBinding> GrowthConfig::parseRuleBindings(const IniFile& ini) {
  std::vector<RuleBinding> out;
  for (const auto& kv : ini.section("ZoneDeveloperRules")) {
    auto k = split(kv.first, ',');
    if (k.size() < 2) continue;
    RuleBinding b; b.zone = toInt(k[0]); b.ruleId = parseIdNumber(k[1]);
    for (const auto& t : split(kv.second, ';')) {
      auto f = split(t, ',');
      if (f.size() >= 3) b.families.push_back(toInt(f[2]));
    }
    ruleFor(b.zone, b.ruleId);
    out.push_back(b);
  }
  return out;
}
DevRule& GrowthConfig::ruleFor(int zone, uint32_t id) {
  auto& v = rules[zone];
  for (auto& r : v) if (r.id == id) return r;
  v.emplace_back(); v.back().id = id;
  return v.back();
}
void GrowthConfig::applyRuleParams(const IniFile& ini) {
  for (const auto& kv : ini.section("ZoneDeveloperRules")) {
    uint32_t id = parseIdNumber(kv.first);
    auto f = split(kv.second, ',');
    if (f.size() < 4) continue;
    for (auto& zr : rules) for (auto& r : zr.second) if (r.id == id) {
      r.a = toInt(f[0]); r.b = toInt(f[1]); r.c = toInt(f[2]); r.d = toInt(f[3]); r.hasParams = true;
    }
  }
}
void GrowthConfig::parseTune(const IniFile& ini) {
  auto get = [&](const char* sec, const char* key, int& v) { std::string s; if (ini.get(sec, key, s)) v = toInt(s); };
  auto dev = [&](const char* sec, DeveloperTuning& t) {
    get(sec, "NoServicePercentage", t.noServicePercentage); get(sec, "MaxDistFromTransport", t.maxDistFromTransport);
    get(sec, "ConstructionTimeFactor", t.constructionTimeFactor); get(sec, "HigherLandValueRedevelopmentPct", t.higherLandValuePct);
    get(sec, "LowerLandValueRedevelopmentPct", t.lowerLandValuePct); get(sec, "HigherDensityRedevelopmentPct", t.higherDensityPct);
    get(sec, "AnarchyFactor", t.anarchyFactor);
  };
  dev("ResidentialZoneDeveloper", res); dev("CommercialZoneDeveloper", com); dev("IndustrialZoneDeveloper", ind);
  const char* I = "IndustrialZoneDeveloper";
  get(I, "PctChanceAg", ind.pctChanceAg); get(I, "MinAgAirPollution", ind.minAgAirPollution); get(I, "MinAgWaterPollution", ind.minAgWaterPollution);
  get(I, "MaxAgAirPollution", ind.maxAgAirPollution); get(I, "MaxAgWaterPollution", ind.maxAgWaterPollution);
  get(I, "LawnChemBanOrdEffect", ind.lawnChemBanOrdEffect); get(I, "FarmersMarketOrdEffect", ind.farmersMarketOrdEffect);
  get(I, "MinMinCleanInd", ind.minMinCleanInd); get(I, "MaxMinCleanInd", ind.maxMinCleanInd); get(I, "MinMaxCleanInd", ind.minMaxCleanInd);
  get(I, "MaxMaxCleanInd", ind.maxMaxCleanInd); get(I, "MinCleanIndFlatYear", ind.minCleanIndFlatYear); get(I, "MaxCleanIndFlatYear", ind.maxCleanIndFlatYear);
  get(I, "CleanAirOrdEffect", ind.cleanAirOrd); get(I, "TouristPromotionOrdEffect", ind.touristPromotionOrd);
  get(I, "ConservationCorpsOrdEffect", ind.conservationCorpsOrd); get(I, "IndPolluteFeeOrdEffect", ind.indPolluteFeeOrd);
  get(I, "CleanIndAssocOrdEffect", ind.cleanIndAssocOrd); get(I, "ElectronicsIncentiveOrdEffect", ind.electronicsIncentiveOrd);
  get(I, "AerospaceIncentiveOrdEffect", ind.aerospaceIncentiveOrd); get(I, "ElectronicsJobFairOrdEffect", ind.electronicsJobFairOrd);
  get(I, "BiotechIncentiveOrdEffect", ind.biotechIncentiveOrd); get(I, "PublicAccessCableOrdEffect", ind.publicAccessCableOrd);
  get("LandfillZoneDeveloper", "LandfillDecayRate", landfillDecayRate);
  landfillDecayRate = clampi(landfillDecayRate, 0, 10000);
  applyRuleParams(ini);
}

// ---------------- category selection ----------------
static bool fits(const Category& c, int lv) { return c.minLV <= lv && lv <= c.maxLV; }

int pickCategoryE(const ZoneDescription& zd, int lv, bool watered, char setCls) {
  int best = -1;
  for (size_t i = 0; i < zd.cats.size(); i++) {
    const Category& c = zd.cats[i];
    if (setCls && c.cls != setCls) continue;
    if (!fits(c, lv)) continue;
    if (!watered && c.density > 1) continue;
    if (best < 0 || c.minLV > zd.cats[best].minLV) best = int(i);
  }
  return best;
}
int pickCategoryH(const ZoneDescription& zd, int cur, int lv, bool watered) {
  if (cur < 0) return -1;
  const Category& k = zd.cats[cur];
  int best = -1;
  for (size_t i = 0; i < zd.cats.size(); i++) {
    const Category& c = zd.cats[i];
    if (c.cls != k.cls || !fits(c, lv) || c.minLV <= k.minLV || c.density < k.density) continue;
    if (!watered && c.density > 1) continue;
    if (best < 0 || c.minLV > zd.cats[best].minLV) best = int(i);
  }
  return best;
}
int pickCategoryD(const ZoneDescription& zd, int cur, int lv) {
  if (cur < 0) return -1;
  const Category& k = zd.cats[cur];
  int best = -1;
  for (size_t i = 0; i < zd.cats.size(); i++) {
    const Category& c = zd.cats[i];
    if (c.cls != k.cls || !fits(c, lv) || c.density < k.density + 1) continue;
    if (best < 0 || c.minLV > zd.cats[best].minLV) best = int(i);
  }
  return best;
}
int findCategory(const ZoneDescription& zd, uint32_t agentType) {
  for (size_t i = 0; i < zd.cats.size(); i++) if (zd.cats[i].agentType == agentType) return int(i);
  return -1;
}

// ---------------- lot finding ----------------
namespace {
bool tileOk(const GrowthCity& c, int x, int z, int zone, bool wet, int minLV, uint32_t replaced, int maxDist, bool skipPower) {
  if (x < 0 || z < 0 || x >= c.mapW() || z >= c.mapH()) return false;
  if (c.zoneAt(x, z) != zone || c.watered(x, z) != wet) return false;
  if (!skipPower && !c.powered(x, z)) return false;
  if (c.landValue(x, z) < minLV) return false;
  LotInfo l = c.lotAt(x, z);
  if (l.present) {
    if (!l.removable || l.flagged()) return false;
    uint32_t b = l.baseState();
    bool ok = l.agentType == replaced || l.kind5 || b == 2 || (l.filler() && b != 8 && b != 16);
    if (!ok) return false;
  }
  return c.hasAccess(x, z, maxDist);
}
}  // namespace

Rect findLot(const GrowthCity& c, int x, int z, int minLV, uint32_t replaced, int maxDist, bool skipPower) {
  Rect r{x, z, x, z};
  int zone = c.zoneAt(x, z); bool wet = c.watered(x, z);
  auto colOk = [&](int cx) { for (int zz = r.z0; zz <= r.z1; zz++) if (!tileOk(c, cx, zz, zone, wet, minLV, replaced, maxDist, skipPower)) return false; return true; };
  auto rowOk = [&](int rz) { for (int xx = r.x0; xx <= r.x1; xx++) if (!tileOk(c, xx, rz, zone, wet, minLV, replaced, maxDist, skipPower)) return false; return true; };
  bool progress = true;
  while (progress) {
    progress = false;
    if (colOk(r.x0 - 1)) { r.x0--; progress = true; } else if (colOk(r.x1 + 1)) { r.x1++; progress = true; }
    if (rowOk(r.z0 - 1)) { r.z0--; progress = true; } else if (rowOk(r.z1 + 1)) { r.z1++; progress = true; }
  }
  return r;
}

// ---------------- landfill ----------------
int LandfillDeveloper::visit(GrowthCity& c, int x, int z) {
  // Spec 15 s7 [STATIC]: landfill tile test, then (1) decay, (2) registration, (3) filling from the store's pending
  // garbage. The tile test: transport within 10 tiles (the s4.2 box with d = 10; a second transport-layer test on the
  // tile is not traced) else code 16; then a tile-origin trip (spec 18 s9.2), failure also code 16; registration only for a corner of a 2 x 2 square of landfill tiles that each have
  // transport within 10, else code 9.
  if (!c.hasAccess(x, z, 10)) return kNdNoAccess;
  if (!c.tileTrip(x, z)) return kNdNoAccess;   // tile-origin trip failed: code 16 (spec 15 s7)
  bool square = false;
  for (int dx : {-1, 1})
    for (int dz : {-1, 1}) {
      bool ok = true;
      for (int ax = 0; ax <= 1 && ok; ax++)
        for (int az = 0; az <= 1 && ok; az++) {
          int tx = x + ax * dx, tz = z + az * dz;
          if (tx < 0 || tz < 0 || tx >= c.mapW() || tz >= c.mapH() || c.zoneAt(tx, tz) != 17 || !c.hasAccess(tx, tz, 10)) ok = false;
        }
      if (ok) square = true;
    }
  int code = square ? 0 : kNdAirportIdle;   // code 9
  std::vector<std::pair<int, int>> changed;
  auto inList = [&](int tx, int tz) { return std::find(active.begin(), active.end(), std::make_pair(tx, tz)) != active.end(); };
  int g = c.landfillGarbage(x, z);
  if (g > 0) {
    if (!inList(x, z)) active.push_back({x, z});
    int d = std::max(1, rne(double(g) * decayRate / 10000.0));
    g = std::max(0, g - d);
    c.setLandfillGarbage(x, z, g);
    if (c.landfillRegistered(x, z)) c.creditLandfillRoom(d);
    if (g == 0) {
      c.clearLandfillTile(x, z);
      active.erase(std::remove(active.begin(), active.end(), std::make_pair(x, z)), active.end());
    }
    changed.push_back({x, z});
  }
  if (square && c.zoneAt(x, z) == 17 && !c.landfillRegistered(x, z)) c.registerLandfill(x, z);
  auto fill = [&](int tx, int tz) {
    int64_t S = c.landfillPending();
    if (S <= 0 || !c.landfillRegistered(tx, tz)) return;
    int cur = c.landfillGarbage(tx, tz);
    int amt = int(std::min<int64_t>(S, 32767 - cur));
    if (amt <= 0) return;
    if (cur == 0) c.placeLandfillPiece(tx, tz);
    c.setLandfillGarbage(tx, tz, cur + amt);
    c.landfillPlaced(amt);
    changed.push_back({tx, tz});
  };
  for (size_t i = 0; i < active.size() && c.landfillPending() > 0; i++) fill(active[i].first, active[i].second);
  // [OPEN] order in which new tiles join the list; spec interim: the visited tile is appended.
  if (c.landfillPending() > 0 && !inList(x, z) && c.landfillRegistered(x, z) && c.landfillGarbage(x, z) < 32767) {
    active.push_back({x, z});
    fill(x, z);
  }
  if (!changed.empty()) c.landfillTilesChanged(changed);
  return code;
}

// ---------------- zone manager: scan state ----------------
void ZoneManager::init(int w, int h, CrtRand& crt) {
  W = w; H = h;
  commit.clear(); failCur.fill(0); failPrev.fill(0); zoneTileCount.fill(0);
  resultPrev.clear(); resultCur.clear(); okCur = okPrev = failCurTotal = failPrevTotal = 0;
  scan = ScanState();
  scan.x0 = 0; scan.x1 = W - 1; scan.z0 = 0; scan.z1 = H - 1; scan.count = W * H;
  scan.list.clear();
  for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) scan.list.push_back(uint16_t(x * 256 + z));
  shuffle(crt);
  scan.index = 0; scan.state = 1;
  takeNext();
  trailing.clear();
}

void ZoneManager::shuffle(CrtRand& crt) {
  size_t n = scan.list.size();
  for (size_t i = 1; i < n; i++) { size_t j = size_t(crt.next()) % (i + 1); std::swap(scan.list[i], scan.list[j]); }
}

void ZoneManager::takeNext() {
  ScanState& s = scan;
  if (s.mode == 0) {
    if (s.index >= int32_t(s.list.size())) { s.state = 2; return; }
    uint16_t e = s.list[size_t(s.index)]; s.index++;
    cursorX = e / 256; cursorZ = e % 256;
    if (s.index == int32_t(s.list.size())) s.state = 2;
  } else {
    s.index++;
    if (s.index > s.count) { s.state = 2; return; }
    int32_t span = std::max(1, s.x1 - s.x0 + 1);
    s.stridePos = s.count ? (s.stridePos + s.strideStep) % s.count : 0;
    cursorX = uint32_t(s.x0 + s.stridePos % span); cursorZ = uint32_t(s.z0 + s.stridePos / span);
  }
}

void ZoneManager::rollover(CrtRand& crt) {
  for (auto& kv : commit) kv.second = 0;
  resultPrev = resultCur;
  for (auto& kv : resultCur) kv.second = 0;
  failPrev = failCur; failCur.fill(0);
  okPrev = okCur; okCur = 0;
  failPrevTotal = failCurTotal; failCurTotal = 0;
  scan.state = 1; scan.index = 0;
  if (int32_t(scan.list.size()) != scan.count) {
    scan.list.clear();
    for (int x = scan.x0; x <= scan.x1; x++) for (int z = scan.z0; z <= scan.z1; z++) scan.list.push_back(uint16_t(x * 256 + z));
  }
  shuffle(crt);
}

void ZoneManager::record(int code, int zone) {
  if (code < 1 || code > 18 || zone < 0 || zone > 22) return;
  resultCur[{uint8_t(code), uint8_t(zone)}]++;
  if (code <= 8) okCur++;
  else if (code >= 9) { failCur[size_t(zone)]++; failCurTotal++; }
}

int ZoneManager::step(GrowthCity& c, SimRand& rng, CrtRand& crt) {
  int code = 0;
  int x = int(cursorX), z = int(cursorZ);
  if (x < 0 || z < 0 || x >= W || z >= H) { x = 0; z = 0; cursorX = cursorZ = 0; }
  if (cfg_) code = visitTile(c, rng, x, z);
  if (scan.state == 2) rollover(crt);
  takeNext();
  return code;
}

void ZoneManager::tick(GrowthCity& c, SimRand& rng, CrtRand& crt) {
  int k = visitsPerTick();
  for (int i = 0; i < k; i++) step(c, rng, crt);
}

int ZoneManager::visitTile(GrowthCity& c, SimRand& rng, int x, int z) {
  int zone = c.zoneAt(x, z);
  if (zone <= 0 || !cfg_) return 0;
  auto it = cfg_->zones.find(zone);
  if (it == cfg_->zones.end()) return 0;
  const ZoneDescription& zd = it->second;
  int code = 0;
  switch (zd.developerClass) {
    case kDevResidential: code = visitRCI(c, rng, x, z, zd, 'R'); break;
    case kDevCommercial: code = visitRCI(c, rng, x, z, zd, 'C'); break;
    case kDevIndustrial: code = visitRCI(c, rng, x, z, zd, 'I'); break;
    case kDevAirport: case kDevSeaport: code = visitPort(c, rng, x, z, zd); break;
    case kDevLandfill: code = landfill.visit(c, x, z); break;
    default: return 0;
  }
  record(code, zone);
  return code;
}

void ZoneManager::adjustCommit(uint32_t agentType, int sx, int sz, uint32_t state, int sign) {
  uint32_t b = state & 0xFF;
  if (b != 1 && b != 4) return;
  if (!cfg_ || !cfg_->valveEffects) return;
  auto it = cfg_->valveEffects->find(agentType);
  if (it == cfg_->valveEffects->end()) return;
  int tiles = int(int8_t(sx * sz));  // signed 8-bit product
  for (int v : {kValveRes, kValveCom, kValveInd}) {
    int e = 0; bool any = false;
    for (const auto& vi : it->second) if (vi.valve == v) { e += vi.supply; any = true; }
    if (!any) continue;
    commit[uint32_t(v)] += sign * tiles * e;
  }
}

void ZoneManager::recountZones(const std::vector<uint8_t>& g) {
  zoneTileCount.fill(0);
  for (uint8_t z : g) if (z >= 1 && z <= 22) zoneTileCount[z]++;
}

// ---------------- placement helpers ----------------
void ZoneManager::removeLot(GrowthCity& c, const LotInfo& lot) {
  c.removeBuilding(lot);
  buildingRemoved(lot.agentType, lot.sizeX, lot.sizeZ, lot.state);
}
int ZoneManager::place(GrowthCity& c, const BuildingChoice& b, int x, int z) {
  std::set<std::pair<int, int>> seen;
  for (int xx = x; xx < x + b.sizeX; xx++) for (int zz = z; zz < z + b.sizeZ; zz++) {
    if (xx < 0 || zz < 0 || xx >= c.mapW() || zz >= c.mapH()) continue;
    LotInfo l = c.lotAt(xx, zz);
    if (l.present && seen.insert({l.ax, l.az}).second) removeLot(c, l);
  }
  c.placeBuilding(b, x, z);
  buildingAdded(b.agentType, b.sizeX, b.sizeZ, 4);
  return 1;
}

bool ZoneManager::startDevelopment(GrowthCity& c, SimRand& rng, int x, int z, const ZoneDescription& zd, int cat, uint32_t replaced, int V, int P, int Max, bool skipPower) {
  if (cat < 0) return false;
  return startDevelopment(c, rng, x, z, zd, zd.cats[size_t(cat)], replaced, V, P, Max, skipPower);
}

bool ZoneManager::startDevelopment(GrowthCity& c, SimRand& rng, int x, int z, const ZoneDescription& zd, const Category& k, uint32_t replaced, int V, int P, int Max, bool skipPower) {
  int maxDist = (zd.developerClass == kDevIndustrial ? cfg_->ind : zd.developerClass == kDevCommercial ? cfg_->com : cfg_->res).maxDistFromTransport;
  if (c.ordinanceEnacted(0xA2BF1DE5)) maxDist++;
  Rect lot = findLot(c, x, z, k.minLV, replaced, maxDist, skipPower);
  auto rit = cfg_->rules.find(zd.zone);
  if (rit == cfg_->rules.end()) return false;
  std::vector<const DevRule*> pool;
  for (const auto& r : rit->second) pool.push_back(&r);
  int tries = std::min<int>(4, int(pool.size()));
  for (int t = 0; t < tries; t++) {
    int i = rng.rand(int(pool.size()));
    const DevRule* r = pool[size_t(i)]; pool.erase(pool.begin() + i);
    if (runRule(c, rng, *r, lot, k, V, P, Max, maxDist) > 0) return true;
  }
  return false;
}

namespace {
struct Frontage {
  const GrowthCity& c; Rect r;
  // i-th tile along edge `e` (0: x=x0 column, 1: z=z1 row, 2: x=x1 column, 3: z=z0 row), increasing coordinate
  bool at(int e, int i) const {
    switch (e) {
      case 0: return c.isTransport(r.x0 - 1, r.z0 + i);
      case 1: return c.isTransport(r.x0 + i, r.z1 + 1);
      case 2: return c.isTransport(r.x1 + 1, r.z0 + i);
      default: return c.isTransport(r.x0 + i, r.z0 - 1);
    }
  }
  int len(int e) const { return (e & 1) ? r.w() : r.h(); }
  int count(int e) const { int n = 0; for (int i = 0; i < len(e); i++) n += at(e, i); return n; }
  bool full(int e) const { return count(e) == len(e); }
  // stretch of `n` tiles starting at the low (fromHigh=false) or high end
  bool stretch(int e, bool fromHigh, int n) const {
    int L = len(e); if (n > L) return false;
    for (int i = 0; i < n; i++) if (!at(e, fromHigh ? L - 1 - i : i)) return false;
    return true;
  }
};
// corner k: edges (3,0), (0,1), (1,2), (2,3); returns tile and whether corner is low/high on each axis
struct Corner { int e1, e2; bool hiX, hiZ; };
const Corner kCorners[4] = {{3, 0, false, false}, {0, 1, false, true}, {1, 2, true, true}, {2, 3, true, false}};

bool nearTransport(const GrowthCity& c, int x, int z) {
  return c.isTransport(x - 1, z) || c.isTransport(x + 1, z) || c.isTransport(x, z - 1) || c.isTransport(x, z + 1);
}
}  // namespace

int ZoneManager::runRule(GrowthCity& c, SimRand& rng, const DevRule& rule, const Rect& lot, const Category& cat, int V, int P, int Max, int maxDist) {
  auto lit = rule.lists.find(cat.agentType);
  if (lit == rule.lists.end()) return 0;
  const CategoryLists& L = lit->second;
  Rect r = lot;
  const int64_t uncommitted = int64_t(V) - P;
  auto pickFrom = [&](const std::vector<BuildingChoice>& v) -> const BuildingChoice* { return v.empty() ? nullptr : &v[size_t(rng.rand(int(v.size())))]; };
  std::vector<char> used;
  auto idx = [&](int x, int z) { return size_t(x - r.x0) * size_t(r.h()) + size_t(z - r.z0); };
  auto inside = [&](int x, int z) { return x >= r.x0 && x <= r.x1 && z >= r.z0 && z <= r.z1; };
  auto canAt = [&](const BuildingChoice& b, int x, int z) {
    if (!inside(x, z) || !inside(x + b.sizeX - 1, z + b.sizeZ - 1)) return false;
    for (int xx = x; xx < x + b.sizeX; xx++) for (int zz = z; zz < z + b.sizeZ; zz++) if (used[idx(xx, zz)]) return false;
    return c.canPlace(b, x, z);
  };
  auto putAt = [&](const BuildingChoice& b, int x, int z) {
    place(c, b, x, z);
    for (int xx = x; xx < x + b.sizeX; xx++) for (int zz = z; zz < z + b.sizeZ; zz++) used[idx(xx, zz)] = 1;
    return 1;
  };
  auto cut = [&](int b) {  // cut to b x b keeping the end with better frontage on each axis
    b = std::min(b, 10);
    if (r.w() > b) { Frontage f{c, r}; if (f.count(2) > f.count(0)) r.x0 = r.x1 - b + 1; else r.x1 = r.x0 + b - 1; }
    if (r.h() > b) { Frontage f{c, r}; if (f.count(1) > f.count(3)) r.z0 = r.z1 - b + 1; else r.z1 = r.z0 + b - 1; }
  };
  int placed = 0;
  switch (rule.id) {
    case 0x630EA608: {  // housing projects (spec 15 s5.5): layout A (r < 33), B (33..65), C (66..99)
      if (uncommitted * 100 / std::max(1, Max) <= 0) return 0;
      const BuildingChoice* b = pickFrom(L.large);
      if (!b) return 0;
      int e = std::max(b->sizeX, b->sizeZ);
      int sel = rng.rand(100);
      used.assign(size_t(r.w()) * r.h(), 0);
      std::vector<BuildingChoice> fill1;
      for (const auto& f : L.filler) if (f.sizeX == 1 && f.sizeZ == 1) fill1.push_back(f);
      auto putFiller = [&](int x, int z) {
        const BuildingChoice* fb = pickFrom(fill1);
        if (fb && canAt(*fb, x, z)) placed += putAt(*fb, x, z);
      };
      if (sel >= 33) {
        // Layouts B and C: a lot side, tried from rand(4) in turn, with room and an entirely fronted frontage strip.
        // Lot frame of side s: `a` runs along the side, `d` away from it (row 0 against the side).
        const bool layoutB = sel < 66;
        const int needAlong = layoutB ? 3 * e : 2 * e + 1, needDepth = layoutB ? e + 1 : e, front = layoutB ? 3 * e : 2 * e;
        auto toXZ = [&](int s, int a, int d, int& x, int& z) {
          switch (s) {
            case 0: x = r.x0 + a; z = r.z0 + d; break;   // low-z side, along +x
            case 1: x = r.x1 - d; z = r.z0 + a; break;   // high-x side, along +z
            case 2: x = r.x1 - a; z = r.z1 - d; break;   // high-z side, along -x
            default: x = r.x0 + d; z = r.z1 - a; break;  // low-x side, along -z
          }
        };
        auto fronted = [&](int s, int a) {  // the tile just outside the side at position a
          int x, z; toXZ(s, a, 0, x, z);
          switch (s) { case 0: z--; break; case 1: x++; break; case 2: z++; break; default: x--; break; }
          return c.isTransport(x, z);
        };
        // Building whose frame square starts at (a, d): its world anchor is the low corner of the mapped square.
        // Orientations are from the layout tables (spec 15 s5.5) [STATIC]: B uses 0 / 1 / 0; C uses 0 / 0.
        auto building = [&](int s, int a, int d, int rot) {
          BuildingChoice bc = *b; bc.rot = rot;
          int xa, za, xb, zb; toXZ(s, a, d, xa, za); toXZ(s, a + e - 1, d + e - 1, xb, zb);
          int x = std::min(xa, xb), z = std::min(za, zb);
          if (canAt(bc, x, z)) placed += putAt(bc, x, z);
        };
        auto filler = [&](int s, int a, int d) { int x, z; toXZ(s, a, d, x, z); putFiller(x, z); };
        int start = rng.rand(4);
        for (int k = 0; k < 4; k++) {
          int s = (start + k) % 4;
          int along = (s & 1) ? r.h() : r.w(), depth = (s & 1) ? r.w() : r.h();
          if (along < needAlong || depth < needDepth) continue;
          bool ok = true;
          for (int a = 0; a < front && ok; a++) ok = fronted(s, a);
          if (!ok) continue;
          if (layoutB) {   // staggered triple: the middle building on the road, the outer two set back behind filler
            for (int a = 0; a < e; a++) filler(s, a, 0);
            building(s, 0, 1, 0);
            building(s, e, 0, 1);
            for (int a = e; a < 2 * e; a++) filler(s, a, e);
            for (int a = 2 * e; a < 3 * e; a++) filler(s, a, 0);
            building(s, 2 * e, 1, 0);
          } else {         // pair with a one-tile filler path between them
            building(s, 0, 0, 0);
            for (int d = 0; d < e; d++) filler(s, e, d);
            building(s, e + 1, 0, 0);
          }
          break;
        }
        return placed;
      }
      if (r.w() < 2 * e || r.h() < 2 * e) return 0;
      Frontage f{c, r};
      int start = rng.rand(4);
      for (int k = 0; k < 4; k++) {
        const Corner& cn = kCorners[(start + k) % 4];
        // stretch of 2e from the corner along both adjoining edges
        auto fromHigh = [&](int edge) {  // is the corner at the high end of this edge's axis?
          return (edge & 1) ? cn.hiX : cn.hiZ;
        };
        if (!f.stretch(cn.e1, fromHigh(cn.e1), 2 * e) || !f.stretch(cn.e2, fromHigh(cn.e2), 2 * e)) continue;
        int ox = cn.hiX ? r.x1 - 2 * e + 1 : r.x0, oz = cn.hiZ ? r.z1 - 2 * e + 1 : r.z0;
        for (int qx = 0; qx < 2; qx++) for (int qz = 0; qz < 2; qz++) {
          bool opposite = (qx != (cn.hiX ? 1 : 0)) && (qz != (cn.hiZ ? 1 : 0));
          int qx0 = ox + qx * e, qz0 = oz + qz * e;
          if (opposite) {
            for (int xx = qx0; xx < qx0 + e; xx++) for (int zz = qz0; zz < qz0 + e; zz++) {
              const BuildingChoice* fb = pickFrom(L.filler);
              if (fb && canAt(*fb, xx, zz)) placed += putAt(*fb, xx, zz);
            }
          } else if (canAt(*b, qx0, qz0)) placed += putAt(*b, qx0, qz0);
        }
        break;
      }
      return placed;
    }
    case 0x61F44687: {  // row houses (spec 15 s5.5) [STATIC]
      if (uncommitted * 50 / std::max(1, Max) < 1) return 0;
      if (r.w() < 6 || r.h() < 6 || (r.w() & 1) || (r.h() & 1)) return 0;
      if (r.w() > 8) r.x1 = r.x0 + 7;
      if (r.h() > 8) r.z1 = r.z0 + 7;
      used.assign(size_t(r.w()) * r.h(), 0);
      // Member lists by building flag bit (attribute 0xCA) and edge; a member may be in several lists.
      std::vector<BuildingChoice> C1, C2, C3, C4, E1, E2, E3, E4, F;
      for (const auto* v : {&L.filler, &L.small, &L.large})
        for (const auto& m : *v) {
          const uint32_t fw = m.flagWord;
          if (m.sizeX == 2 && m.sizeZ == 2) {
            if (fw & 0x1) C1.push_back(m);
            if (fw & 0x2) C2.push_back(m);
            if (fw & 0x4) C3.push_back(m);
            if (fw & 0x8) C4.push_back(m);
            if (fw & 0x20) E1.push_back(m);
            if (fw & 0x80) E2.push_back(m);
            if (fw & 0x10) E3.push_back(m);
            if (fw & 0x40) E4.push_back(m);
          } else if (m.sizeX == 1 && m.sizeZ == 1 && (fw & 0x100)) F.push_back(m);
        }
      const int x0 = r.x0, x1 = r.x1, z0 = r.z0, z1 = r.z1;
      auto fr = [&](int x, int z) { return nearTransport(c, x, z); };
      auto put = [&](const std::vector<BuildingChoice>& lst, int x, int z) {
        const BuildingChoice* m = pickFrom(lst);
        if (m && canAt(*m, x, z)) { placed += putAt(*m, x, z); return true; }
        return false;
      };
      bool c1 = fr(x0 + 1, z0) && fr(x0, z0 + 1) && put(C1, x0, z0);
      bool c2 = fr(x1, z1 - 1) && fr(x1 - 1, z1) && put(C2, x1 - 1, z1 - 1);
      bool c3 = fr(x0 + 1, z1) && fr(x0, z1 - 1) && put(C3, x0, z1 - 1);
      bool c4 = fr(x1, z0 + 1) && fr(x1 - 1, z0) && put(C4, x1 - 1, z0);
      if (!c1 && !c2 && !c3 && !c4) return 0;
      if (c1 && c3) for (int z = z0 + 2; z < z1 - 1; z += 2) put(E1, x0, z);
      if (c1 && c4) for (int x = x0 + 2; x < x1 - 1; x += 2) put(E3, x, z0);
      if (c2 && c3) for (int x = x0 + 2; x < x1 - 1; x += 2) put(E2, x, z1 - 1);
      if (c2 && c4) for (int z = z0 + 2; z < z1 - 1; z += 2) put(E4, x1 - 1, z);
      if (c1 && c2 && c3 && c4)
        for (int x = x0 + 2; x < x1 - 1; x++) for (int z = z0 + 2; z < z1 - 1; z++) put(F, x, z);
      return placed;
    }
    case 0xA2C1EDA3: {  // farm (spec 15 s5.5) [STATIC]
      const int a = std::min(22, std::max(rule.a, 4)), bmax = std::min(22, std::max(rule.b, a));
      const int maxAir = std::min(rule.c, 32767), maxWat = std::min(rule.d, 32767);
      // member lists per family: fields = filler edge 1; farmhouse edge >= 3, outbuilding 2, yard <= 1 (non-filler)
      struct FarmSet { std::vector<BuildingChoice> field, house, out, yard; };
      std::map<int, FarmSet> fams;
      for (const auto* v : {&L.filler, &L.small, &L.large})
        for (const auto& m : *v) {
          const int e = std::max(m.sizeX, m.sizeZ);
          FarmSet& fs = fams[m.family];
          if (m.isFiller()) { if (e == 1) fs.field.push_back(m); }
          else if (e >= 3) fs.house.push_back(m);
          else if (e == 2) fs.out.push_back(m);
          else fs.yard.push_back(m);
        }
      std::vector<const FarmSet*> sets;
      for (auto& kv : fams) if (!kv.second.field.empty() && !kv.second.house.empty() && !kv.second.out.empty() && !kv.second.yard.empty()) sets.push_back(&kv.second);
      if (sets.empty()) return 0;
      // 1. size
      if (r.w() < a || r.h() < a) return 0;
      // 2. surroundings: the ring without its four diagonal corners
      auto zoneOk = [&](int x, int z) {
        if (x < 0 || z < 0 || x >= c.mapW() || z >= c.mapH()) return true;
        int zt = c.zoneAt(x, z);
        return zt == 0 || zt == 1 || zt == 5 || zt == 9 || zt == 22;
      };
      for (int x = r.x0; x <= r.x1; x++) if (!zoneOk(x, r.z0 - 1) || !zoneOk(x, r.z1 + 1)) return 0;
      for (int z = r.z0; z <= r.z1; z++) if (!zoneOk(r.x0 - 1, z) || !zoneOk(r.x1 + 1, z)) return 0;
      // 3. edges 0 row z0, 1 column x1, 2 row z1, 3 column x0, without corner tiles
      int n[4] = {0, 0, 0, 0};
      for (int e = 0; e < 4; e++) {
        const bool row = (e & 1) == 0;
        const int Ln = row ? r.w() : r.h();
        for (int t = 1; t + 1 < Ln; t++) {
          int x = row ? r.x0 + t : (e == 1 ? r.x1 : r.x0), z = row ? (e == 0 ? r.z0 : r.z1) : r.z0 + t;
          if (c.airPollution(x, z) > maxAir || c.waterPollution(x, z) > maxWat) return 0;
          if (c.hasAccess(x, z, maxDist)) n[e]++;
        }
      }
      int frontEdges = 0;
      for (int e = 0; e < 4; e++) if (n[e] > 0) frontEdges++;
      if (frontEdges < 1 || frontEdges > 2) return 0;
      // 4. cut to b
      if (r.w() > bmax) { if (n[1] > 0) r.x0 = r.x1 - bmax + 1; else r.x1 = r.x0 + bmax - 1; }
      if (r.h() > bmax) { if (n[0] > 0) r.z1 = r.z0 + bmax - 1; else r.z0 = r.z1 - bmax + 1; }
      const int W = r.w(), H = r.h();
      used.assign(size_t(W) * H, 0);
      // The family bound to the category being developed, first match (spec 15 s5.5) [STATIC]; our lists for this category
      // hold only its families, taken in ascending family ID.
      const FarmSet& fs = *sets.front();
      // 5. farmstead strip on the side with the most access tiles
      int s = 0;
      for (int e = 1; e < 4; e++) if (n[e] > n[s]) s = e;
      int SX, SZ, xs, zs;
      if (s == 0 || s == 2) {
        SX = std::clamp(W / 2, 3, 8); SZ = std::clamp(H / 3, 3, 4);
        xs = r.x0 + int(rng.rand(std::max(1, W - SX + 1))); zs = s == 0 ? r.z0 : r.z1 - SZ + 1;
      } else {
        SX = std::clamp(W / 3, 3, 4); SZ = std::clamp(H / 2, 3, 8);
        xs = s == 1 ? r.x1 - SX + 1 : r.x0; zs = r.z0 + int(rng.rand(std::max(1, H - SZ + 1)));
      }
      const int xe = xs + SX - 1, ze = zs + SZ - 1;
      auto anchorIn = [&](int e, int& ax, int& az) {
        ax = xs + int(rng.rand(std::max(1, SX - e + 1))); az = zs + int(rng.rand(std::max(1, SZ - e + 1)));
      };
      // 6. farmhouse
      {
        BuildingChoice h = *pickFrom(fs.house); h.rot = 0;
        int ax, az; anchorIn(std::max(h.sizeX, h.sizeZ), ax, az);
        if (canAt(h, ax, az)) placed += putAt(h, ax, az);
      }
      // 7. up to 5 attempts, at most 2 outbuildings
      for (int att = 0, ok = 0; att < 5 && ok < 2; att++) {
        BuildingChoice o = *pickFrom(fs.out); o.rot = 0;
        int ax, az; anchorIn(std::max(o.sizeX, o.sizeZ), ax, az);
        if (canAt(o, ax, az)) { placed += putAt(o, ax, az); ok++; }
      }
      // 8. yard pieces on the free strip tiles
      for (int x = xs; x <= xe; x++)
        for (int z = zs; z <= ze; z++) {
          if (!inside(x, z) || used[idx(x, z)]) continue;
          BuildingChoice y = *pickFrom(fs.yard); y.rot = 0;
          if (canAt(y, x, z)) placed += putAt(y, x, z);
        }
      // 9. fields: one rotation; walk order by rotation; mark every free tile; re-pick 30 % after each, and on used tiles
      const int R = rng.rand(100) <= 50 ? 1 : 0;
      BuildingChoice P = *pickFrom(fs.field);
      auto visit = [&](int x, int z) {
        if (used[idx(x, z)]) { P = *pickFrom(fs.field); return; }
        BuildingChoice f = P; f.rot = R;
        if (c.canPlace(f, x, z)) placed += putAt(f, x, z);
        used[idx(x, z)] = 1;
        if (rng.rand(100) < 30) P = *pickFrom(fs.field);
      };
      if (R == 0) { for (int x = r.x0; x <= r.x1; x++) for (int z = r.z0; z <= r.z1; z++) visit(x, z); }
      else { for (int z = r.z0; z <= r.z1; z++) for (int x = r.x0; x <= r.x1; x++) visit(x, z); }
      return std::max(placed, 1);   // the rule succeeds after the field walk
    }
    case 0xE2D3FD92: {  // seaport yard
      if (uncommitted <= 0) return 0;
      if (r.w() < rule.a || r.h() < rule.a) return 0;
      int b = std::min(rule.b, 10);
      if (r.w() > b) r.x1 = r.x0 + b - 1;
      if (r.h() > b) r.z1 = r.z0 + b - 1;
      used.assign(size_t(r.w()) * r.h(), 0);
      bool any = false;
      for (int x = r.x0; x <= r.x1; x++) for (int z = r.z0; z <= r.z1; z++) {
        if (!c.tileAvailable(x, z)) used[idx(x, z)] = 1; else any = true;
      }
      if (!any) return 0;
      std::vector<BuildingChoice> nonFiller = L.small; nonFiller.insert(nonFiller.end(), L.large.begin(), L.large.end());
      for (int x = r.x0; x <= r.x1; x++) for (int z = r.z0; z <= r.z1; z++) {
        if (used[idx(x, z)] || !c.hasAccess(x, z, maxDist)) continue;
        const BuildingChoice* b2 = rng.rand(100) < rule.d ? pickFrom(nonFiller) : pickFrom(L.filler);
        if (b2 && canAt(*b2, x, z)) placed += putAt(*b2, x, z);
      }
      return placed;
    }
    case 0x81FFC3CE: {  // airport runway: choose side by score; piece laying is delegated
      if (uncommitted <= 0) return 0;
      if (r.w() < 3 || r.h() < 3 || (r.w() < 5 && r.h() < 5)) return 0;
      int best = INT32_MIN, side = 0;
      for (int s = 0; s < 4; s++) {
        int along = (s & 1) ? r.w() : r.h();
        int score = 0;
        if (along < 5) score = -20;
        else {
          std::set<std::pair<int, int>> seen;
          for (int i = 0; i < along; i++) {
            int x = s == 0 ? r.x0 - 1 : s == 2 ? r.x1 + 1 : r.x0 + i, z = (s & 1) ? (s == 1 ? r.z1 + 1 : r.z0 - 1) : r.z0 + i;
            if (x < 0 || z < 0 || x >= c.mapW() || z >= c.mapH()) continue;
            if (c.zoneAt(x, z) == 14) score++;
            LotInfo l = c.lotAt(x, z);
            if (l.present && seen.size() < 10 && seen.insert({l.ax, l.az}).second)
              score += l.agentType == cat.agentType ? 2 * l.sizeX : -l.sizeX;
          }
        }
        if (score > best) { best = score; side = s; }
        else if (score == best && rng.rand(100) > 49) side = s;
      }
      if (best < 0 && ((r.w() == 5 && r.h() == 3) || (r.w() == 3 && r.h() == 5))) return 0;
      // Runway piece sequence (spec 15 s5.5) [STATIC]: one runway set per family, chosen at random; slots by flag word
      // and line number (attribute 0x1F4 [INFERRED]); a slot holds the last matching 1 x 1 member.
      std::map<int, std::vector<BuildingChoice>> sets;
      for (const auto* v : {&L.filler, &L.small, &L.large})
        for (const auto& m : *v) if (m.sizeX == 1 && m.sizeZ == 1) sets[m.family].push_back(m);
      struct RunwaySet { const BuildingChoice* S[10] = {}; std::vector<const BuildingChoice*> F; };
      std::vector<RunwaySet> valid;
      for (const auto& fam : sets) {
        RunwaySet rs;
        const BuildingChoice** S = rs.S;
        std::vector<const BuildingChoice*>& F = rs.F;
        for (const auto& m : fam.second) {
          const uint32_t f = m.flagWord;
          const bool caps = (f & 0xF0) != 0;
          if (m.line == 1) {
            if ((f & 0x800) && !caps) S[0] = &m;
            if ((f & 0x400) && !caps) S[1] = &m;
            if (f & 0x20) S[2] = &m;
            if (f & 0x10) S[3] = &m;
            if (f & 0x80) S[4] = &m;
            if (f & 0x40) S[5] = &m;
          } else if (m.line == 2) {
            if (f & 0x800) S[6] = &m;
            if (f & 0x400) S[7] = &m;
          } else if (m.line == 3) {
            if (f & 0x800) S[8] = &m;
            if (f & 0x400) S[9] = &m;
          }
          if (f & 0x100) F.push_back(&m);
        }
        // Set validation: in each pair one empty slot needs a filled partner flagged 0x200, else the family is no set.
        bool ok = true;
        for (int pr = 0; pr < 10; pr += 2) {
          const BuildingChoice *a = S[pr], *b = S[pr + 1];
          if (!a && !b) ok = false;
          else if (!a || !b) { const BuildingChoice* f = a ? a : b; if (!(f->flagWord & 0x200)) ok = false; }
        }
        if (ok) valid.push_back(rs);
      }
      if (valid.empty()) return 0;
      const RunwaySet& chosen = valid[size_t(rng.rand(int(valid.size())))];
      const BuildingChoice* const* S = chosen.S;
      const std::vector<const BuildingChoice*>& F = chosen.F;
      // Lot frame of side s (Frontage numbering): `a` along the side, `d` inward from it.
      auto toXZ = [&](int a, int d, int& x, int& z) {
        switch (side) {
          case 0: x = r.x0 + d; z = r.z0 + a; break;
          case 1: x = r.x0 + a; z = r.z1 - d; break;
          case 2: x = r.x1 - d; z = r.z0 + a; break;
          default: x = r.x0 + a; z = r.z0 + d; break;
        }
      };
      const int along = (side & 1) ? r.w() : r.h(), D = (side & 1) ? r.h() : r.w();
      const int Ln = std::min(10, along);
      // Spec 15 s5.5 / 10 s6.3 [STATIC]: reserve the strip segment before laying; fail the rule if another runway is near.
      int sx0, sz0, sx1, sz1;
      toXZ(0, 0, sx0, sz0);
      toXZ(Ln - 1, 0, sx1, sz1);
      if (!c.spacingReserveSegment(sx0, sz0, sx1, sz1)) return 0;
      used.assign(size_t(r.w()) * r.h(), 0);
      auto lay = [&](const BuildingChoice* m, int rot, int a, int d) {
        if (!m) return;
        BuildingChoice b = *m; b.rot = rot;
        int x, z; toXZ(a, d, x, z);
        if (canAt(b, x, z)) placed += putAt(b, x, z);
      };
      using Pick = std::pair<const BuildingChoice*, int>;
      auto orAlt = [&](int main, int alt, int rot) -> Pick { return S[main] ? Pick{S[main], 0} : Pick{S[alt], rot}; };
      const Pick l3 = orAlt(8, 9, 3), l2 = orAlt(6, 7, 3);
      for (int a = 0; a < Ln; a++) {
        const bool first = a == 0, last = a == Ln - 1 && Ln > 1;
        Pick pa = first ? orAlt(3, 2, 3) : last ? orAlt(4, 5, 3) : orAlt(0, 1, 1);
        lay(pa.first, pa.second, a, 0);
        int row = 1;
        if (D >= 3) {
          if (first || last) lay(l3.first, l3.second, a, row);
          else lay(F.empty() ? nullptr : F.front(), 0, a, row);
          row++;
        }
        if (D >= 2) lay(l2.first, l2.second, a, row);
      }
      if (placed == 0) c.spacingReleaseSegment(sx0, sz0, sx1, sz1);
      return placed;
    }
    case 0x2207D9EE: {  // airport buildings (spec 15 §5.5) [STATIC]
      if (uncommitted <= 0) return 0;
      struct AirportFamily {
        int family = 0;
        std::vector<BuildingChoice> T, Ev, Eh, Pv, Ph, F;
      };
      std::map<int, AirportFamily> fams;
      for (const auto* v : {&L.filler, &L.small, &L.large}) {
        for (const auto& m : *v) {
          AirportFamily& af = fams[m.family];
          af.family = m.family;
          const uint32_t fw = m.flagWord;
          const int part = m.line;
          const bool edge1 = (m.sizeX == 1 && m.sizeZ == 1);
          const bool edge2 = (m.sizeX == 2 && m.sizeZ == 2);
          if (edge1 && part == 4) af.T.push_back(m);
          if (edge1 && (fw & 0x800) && part == 5) af.Ev.push_back(m);
          if (edge1 && (fw & 0x400) && part == 5) af.Eh.push_back(m);
          if (edge2 && (fw & 0x400) && part == 6) af.Pv.push_back(m);
          if (edge2 && (fw & 0x800) && part == 6) af.Ph.push_back(m);
          if (edge1 && ((fw & 0x100) || part == 0)) af.F.push_back(m);
        }
      }
      std::vector<AirportFamily> registered;
      for (auto& kv : fams) {
        const auto& af = kv.second;
        if (!af.T.empty() && !af.Ev.empty() && !af.Eh.empty() &&
            !af.Pv.empty() && !af.Ph.empty() && !af.F.empty()) {
          registered.push_back(af);
        }
      }
      if (registered.empty()) return 0;

      if (r.w() > 10) r.x1 = r.x0 + 9;
      if (r.h() > 10) r.z1 = r.z0 + 9;
      used.assign(size_t(r.w()) * r.h(), 0);

      auto pickFamily = [&]() -> const AirportFamily& {
        return registered[size_t(rng.rand(int(registered.size())))];
      };

      const BuildingChoice& sample1x1 = registered[0].T[0];

      auto neighborPart = [&](int x, int z, int s) -> int {
        int nx = x, nz = z;
        switch (s) {
          case 0: nx--; break;
          case 1: nz++; break;
          case 2: nx++; break;
          case 3: default: nz--; break;
        }
        if (nx < 0 || nz < 0 || nx >= c.mapW() || nz >= c.mapH()) return 0;
        LotInfo l = c.lotAt(nx, nz);
        return (l.present && l.port) ? l.partIndex : 0;
      };

      auto getEdgeTiles = [&](int s) {
        std::vector<std::pair<int, int>> tiles;
        switch (s) {
          case 0: for (int z = r.z0; z <= r.z1; z++) tiles.push_back({r.x0, z}); break;
          case 1: for (int x = r.x0; x <= r.x1; x++) tiles.push_back({x, r.z1}); break;
          case 2: for (int z = r.z0; z <= r.z1; z++) tiles.push_back({r.x1, z}); break;
          case 3: default: for (int x = r.x0; x <= r.x1; x++) tiles.push_back({x, r.z0}); break;
        }
        return tiles;
      };

      // (1) Terminal on best frontage [STATIC].
      // Sides scanned in order 0–3.
      // Score: 0 if cannot be built on (canAt fails or point-flag near); 1 if can; 2 if can and outward p == 2.
      // First tile with score >= 2 wins (scan stops).
      bool foundStep1 = false;
      int winX = -1, winZ = -1;
      for (int s = 0; s < 4; s++) {
        for (auto& [x, z] : getEdgeTiles(s)) {
          if (!canAt(sample1x1, x, z) || c.spacingPointNear(x, z)) continue;
          int score = (neighborPart(x, z, s) == 2) ? 2 : 1;
          if (score >= 2) {
            winX = x; winZ = z;
            foundStep1 = true;
            break;
          }
        }
        if (foundStep1) break;
      }
      if (foundStep1) {
        const auto& fam = pickFamily();
        const BuildingChoice* b = pickFrom(fam.T);
        if (b && c.spacingReservePoint(winX, winZ)) {
          BuildingChoice bc = *b; bc.rot = 0;
          if (canAt(bc, winX, winZ)) {
            int n = putAt(bc, winX, winZ);
            if (n) return n;
          }
          c.spacingReleasePoint(winX, winZ);
        }
      }

      // (2) Arrangement [STATIC]. If step 1 fails: rand(100) < 55 run (2a), else (2b).
      if (rng.rand(100) < 55) {
        // (2a) Strip along the best part-2 edge.
        int bestSide = -1, maxCount = -1;
        for (int s = 0; s < 4; s++) {
          int cnt = 0;
          for (auto& [x, z] : getEdgeTiles(s)) {
            if (neighborPart(x, z, s) == 2 && canAt(sample1x1, x, z)) cnt++;
          }
          if (cnt > maxCount) {
            maxCount = cnt;
            bestSide = s;
          }
        }
        if (maxCount >= 2) {
          int placedCount = 0;
          for (auto& [x, z] : getEdgeTiles(bestSide)) {
            if (neighborPart(x, z, bestSide) == 2 && canAt(sample1x1, x, z)) {
              const auto& fam = pickFamily();
              const auto& list = (bestSide == 0 || bestSide == 2) ? fam.Ev : fam.Eh;
              const BuildingChoice* b = pickFrom(list);
              if (b) {
                BuildingChoice bc = *b; bc.rot = 0;
                if (canAt(bc, x, z)) {
                  placedCount += putAt(bc, x, z);
                  if (placedCount >= 6) break;
                }
              }
            }
          }
          if (placedCount > 0) return placedCount;
        }
      } else {
        // (2b) 2 x 2 pair on a part-1/2 frontage.
        if (r.w() >= 2 && r.h() >= 2) {
          bool placed2b = false;
          for (int s = 0; s < 4; s++) {
            auto tiles = getEdgeTiles(s);
            if (tiles.size() < 2) continue;
            for (size_t i = 0; i + 1 < tiles.size(); i++) {
              int p0 = neighborPart(tiles[i].first, tiles[i].second, s);
              int p1 = neighborPart(tiles[i + 1].first, tiles[i + 1].second, s);
              if ((p0 == 1 || p0 == 2) && (p1 == 1 || p1 == 2)) {
                int ax = 0, az = 0;
                switch (s) {
                  case 0: ax = r.x0; az = tiles[i].second; break;
                  case 1: ax = tiles[i].first; az = r.z1 - 1; break;
                  case 2: ax = r.x1 - 1; az = tiles[i].second; break;
                  case 3: default: ax = tiles[i].first; az = r.z0; break;
                }
                const auto& fam = pickFamily();
                const auto& list = (s == 0 || s == 2) ? fam.Pv : fam.Ph;
                const BuildingChoice* b = pickFrom(list);
                if (b) {
                  BuildingChoice bc = *b; bc.rot = 0;
                  if (canAt(bc, ax, az)) {
                    int n = putAt(bc, ax, az);
                    if (n) { placed2b = true; return n; }
                  }
                }
                placed2b = true;
                break;
              }
            }
            if (placed2b) break;
          }
        }
      }

      // (3) Random filler [STATIC].
      const auto& fam = pickFamily();
      const BuildingChoice* b3 = pickFrom(fam.F);
      if (b3) {
        int rx = r.x0 + rng.rand(r.w()), rz = r.z0 + rng.rand(r.h());
        BuildingChoice bc = *b3; bc.rot = 0;
        if (canAt(bc, rx, rz)) {
          int n = putAt(bc, rx, rz);
          if (n) return n;
        }
      }
      return 0;
    }
    case 0xE22F4A83: {  // estates (spec 15 s5.5) [STATIC]
      if (uncommitted * 10 / std::max(1, Max) < 1) return 0;
      if (r.w() < 4 || r.h() < 4) return 0;
      if (r.w() > 6) r.x1 = r.x0 + 5;
      if (r.h() > 6) r.z1 = r.z0 + 5;
      // Member lists by flag word; K corners, W walls, G gates indexed X0, X1, Z0, Z1.
      enum { X0 = 0, X1 = 1, Z0 = 2, Z1 = 3 };
      std::vector<BuildingChoice> K[4], Wl[4], Gt[4], lawn, feature, mansion;
      for (const auto* v : {&L.filler, &L.small, &L.large})
        for (const auto& m : *v) {
          const uint32_t f = m.flagWord;
          const int e = std::max(m.sizeX, m.sizeZ);
          if (e == 1 && (f & 0xF)) {
            if (f & 0x1) K[0].push_back(m);
            if (f & 0x2) K[1].push_back(m);
            if (f & 0x4) K[2].push_back(m);
            if (f & 0x8) K[3].push_back(m);
          } else if (f & 0x100) (f & 0x400 ? lawn : feature).push_back(m);
          else if (!(f & 0xF0) && e < 4) mansion.push_back(m);
          else if (e == 1) {
            if (f & 0x20) (f & 0x800 ? Wl[X0] : Gt[X0]).push_back(m);
            if (f & 0x40) (f & 0x800 ? Wl[X1] : Gt[X1]).push_back(m);
            if (f & 0x10) (f & 0x400 ? Wl[Z0] : Gt[Z0]).push_back(m);
            if (f & 0x80) (f & 0x400 ? Wl[Z1] : Gt[Z1]).push_back(m);
          }
        }
      // Mirroring: an empty list takes the opposite list's members, placed rotated 180 degrees.
      auto mirror = [&](std::vector<BuildingChoice>& a, std::vector<BuildingChoice>& b) {
        if (a.empty() == b.empty()) return !a.empty();
        std::vector<BuildingChoice>& dst = a.empty() ? a : b;
        dst = a.empty() ? b : a;
        for (auto& m : dst) m.rot = 2;
        return true;
      };
      bool ok = mirror(K[0], K[1]) & mirror(K[2], K[3]) & mirror(Wl[X0], Wl[X1]) & mirror(Gt[X0], Gt[X1]) &
                mirror(Wl[Z0], Wl[Z1]) & mirror(Gt[Z0], Gt[Z1]);
      if (!ok || mansion.empty() || lawn.empty() || feature.empty()) return 0;
      auto norm = [](std::vector<BuildingChoice>& v) { for (auto& m : v) if (m.rot < 0) m.rot = 0; };
      for (int i = 0; i < 4; i++) { norm(K[i]); norm(Wl[i]); norm(Gt[i]); }
      norm(lawn); norm(feature); norm(mansion);
      used.assign(size_t(r.w()) * r.h(), 0);
      const int x0 = r.x0, x1 = r.x1, z0 = r.z0, z1 = r.z1, W = r.w(), H = r.h();
      auto put = [&](const std::vector<BuildingChoice>& lst, int x, int z) {
        const BuildingChoice* m = pickFrom(lst);
        if (m && canAt(*m, x, z)) placed += putAt(*m, x, z);
      };
      {  // 1. mansion inside the one-tile ring
        const BuildingChoice* m = pickFrom(mansion);
        int e = std::max(m->sizeX, m->sizeZ);
        int mx = x0 + 1 + rng.rand(W - e - 1), mz = z0 + 1 + rng.rand(H - e - 1);
        if (canAt(*m, mx, mz)) placed += putAt(*m, mx, mz);
      }
      // 2. corners that pass the frontage test
      const int cx[4] = {x0, x1, x0, x1}, cz[4] = {z0, z1, z1, z0};
      for (int k = 0; k < 4; k++) if (nearTransport(c, cx[k], cz[k])) put(K[k], cx[k], cz[k]);
      // 3. walls, at most one gate per side
      auto side = [&](int which, bool alongZ, int fixed, int lo, int hi) {
        bool gate = false;
        for (int t = lo + 1; t < hi; t++) {
          int x = alongZ ? fixed : t, z = alongZ ? t : fixed;
          if (!gate && rng.rand(100) < 20) { gate = true; put(Gt[which], x, z); }
          else put(Wl[which], x, z);
        }
      };
      side(X0, true, x0, z0, z1);
      side(Z0, false, z0, x0, x1);
      side(Z1, false, z1, x0, x1);
      side(X1, true, x1, z0, z1);
      // 4. interior: feature 5 %, else lawn
      for (int x = x0 + 1; x < x1; x++)
        for (int z = z0 + 1; z < z1; z++) {
          if (used[idx(x, z)]) continue;
          put(rng.rand(100) < 5 ? feature : lawn, x, z);
        }
      return placed;
    }
    case 0xC2D3FD7E: {  // seaport pier (spec 15 s5.5) [STATIC]; never reports success
      struct PierSet { std::vector<BuildingChoice> Q0, Q1, H0, H1, B0, B1; };
      std::map<int, PierSet> fams;
      for (const auto* v : {&L.filler, &L.small, &L.large})
        for (const auto& m : *v) {
          if (std::max(m.sizeX, m.sizeZ) != 1 || !(m.flagWord & 0x200)) continue;
          const bool f4 = (m.flagWord & 0x400) != 0, f8 = (m.flagWord & 0x800) != 0;
          if (!f4 && !f8) continue;
          PierSet& ps = fams[m.family];
          if (m.line == 7) (f4 ? ps.Q0 : ps.Q1).push_back(m);
          else if (m.line == 8) (f4 ? ps.H0 : ps.H1).push_back(m);
          else if (m.line == 9) (f4 ? ps.B1 : ps.B0).push_back(m);
        }
      std::vector<const PierSet*> sets;
      for (auto& kv : fams)
        if (!kv.second.B0.empty() && !kv.second.B1.empty() && !kv.second.H0.empty() && !kv.second.H1.empty() &&
            (!kv.second.Q0.empty() || !kv.second.Q1.empty()))
          sets.push_back(&kv.second);
      if (sets.empty()) return 0;
      // 1. shore run: sides low-z (0,-1), high-z (0,+1), high-x (+1,0), low-x (-1,0); longest run, strictly longer wins
      static const int SD[4][2] = {{0, -1}, {0, 1}, {1, 0}, {-1, 0}};
      int bestSide = -1, bestStart = 0, bestLen = 0;
      for (int sd = 0; sd < 4; sd++) {
        const int dx = SD[sd][0], dz = SD[sd][1];
        const bool alongX = dz != 0;
        const int Ln = alongX ? r.w() : r.h();
        auto edgeTile = [&](int t, int& x, int& z) {
          if (alongX) { x = r.x0 + t; z = dz < 0 ? r.z0 : r.z1; } else { z = r.z0 + t; x = dx < 0 ? r.x0 : r.x1; }
        };
        int run = 0, runStart = 0;
        for (int t = 0; t < Ln; t++) {
          int x, z; edgeTile(t, x, z);
          bool ok = true;
          for (int k = 1; k <= 5 && ok; k++) {
            int tx = x + k * dx, tz = z + k * dz;
            if (tx < 0 || tz < 0 || tx >= c.mapW() || tz >= c.mapH() || !c.waterTile(tx, tz) || c.tileOccupied(tx, tz)) ok = false;
          }
          if (ok && !c.deepWater(x + 5 * dx, z + 5 * dz)) ok = false;
          if (ok) { if (run == 0) runStart = t; run++; if (run > bestLen) { bestLen = run; bestSide = sd; bestStart = runStart; } }
          else run = 0;
        }
      }
      if (bestSide < 0 || bestLen < 5) return 0;
      const int L = std::min(bestLen, 12);
      // 2. pieces, once per call
      const PierSet& ps = *sets[size_t(rng.rand(int(sets.size())))];
      BuildingChoice shore, head, body;
      auto pick = [&](const std::vector<BuildingChoice>& v, int rot) { BuildingChoice b = v[size_t(rng.rand(int(v.size())))]; b.rot = rot; return b; };
      switch (bestSide) {
        case 0: shore = !ps.Q0.empty() ? pick(ps.Q0, 2) : pick(ps.Q1, 3); head = pick(ps.H0, 2); body = pick(ps.B0, 2); break;
        case 1: shore = !ps.Q0.empty() ? pick(ps.Q0, 0) : pick(ps.Q1, 1); head = pick(ps.H0, 0); body = pick(ps.B0, 0); break;
        case 2: shore = !ps.Q1.empty() ? pick(ps.Q1, 0) : pick(ps.Q0, 1); head = pick(ps.H1, 0); body = pick(ps.B1, 0); break;
        default: shore = !ps.Q1.empty() ? pick(ps.Q1, 2) : pick(ps.Q0, 3); head = pick(ps.H1, 2); body = pick(ps.B1, 2); break;
      }
      // 3. laying
      const int dx = SD[bestSide][0], dz = SD[bestSide][1];
      const bool alongX = dz != 0;
      int g = 5, nPlaced = 0, p = 0;
      bool lastPier = false;
      for (int t = bestStart; t < bestStart + L; t++) {
        int x = alongX ? r.x0 + t : (dx < 0 ? r.x0 : r.x1), z = alongX ? (dz < 0 ? r.z0 : r.z1) : r.z0 + t;
        const int tx = x + dx, tz = z + dz;
        bool pier = false;
        if (g >= 5) pier = int(rng.rand(L)) >= L - nPlaced - 1 + p;
        if (c.tileOccupied(tx, tz) || !c.placePier(pier ? head : shore, tx, tz)) continue;
        nPlaced++;
        if (pier && !lastPier && c.spacingAdd(tx, tz)) {
          for (int k = 2; k <= 5; k++) { int bx = x + k * dx, bz = z + k * dz; if (!c.tileOccupied(bx, bz)) c.placePier(body, bx, bz); }
          g = 0; p++; lastPier = true;
        } else { g++; lastPier = false; }
      }
      return 0;   // the pier rule never reports success: the next rule is still tried
    }
    default: break;
  }
  // Scatter rules (section 5.4)
  if (uncommitted * 100 / std::max(1, Max) < rule.c) return 0;
  int a = std::max(1, rule.a);
  if (r.w() < a || r.h() < a) return 0;
  cut(std::max(a, rule.b));
  used.assign(size_t(r.w()) * r.h(), 0);
  {
    Frontage f{c, r};
    for (int k = 0; k < 4; k++) {
      const Corner& cn = kCorners[k];
      if (!f.full(cn.e1) || !f.full(cn.e2)) continue;
      const BuildingChoice* b = pickFrom(L.large);
      if (!b || b->sizeX > r.w() || b->sizeZ > r.h()) continue;
      int x = cn.hiX ? r.x1 - b->sizeX + 1 : r.x0, z = cn.hiZ ? r.z1 - b->sizeZ + 1 : r.z0;
      if (canAt(*b, x, z)) placed += putAt(*b, x, z);
    }
  }
  for (int z = r.z0; z <= r.z1; z++) for (int x = r.x0; x <= r.x1; x++) {
    if (used[idx(x, z)]) continue;
    int p = nearTransport(c, x, z) ? 100 - rule.d : rule.d;
    const BuildingChoice* b = rng.rand(100) < p ? pickFrom(L.small) : pickFrom(L.filler);
    if (b && canAt(*b, x, z)) placed += putAt(*b, x, z);
  }
  return placed;
}

// ---------------- developers ----------------
void ZoneManager::refreshCleanCurve(int year) {
  const IndustrialTuning& t = cfg_->ind;
  auto lerp = [&](int lo, int hi, int flat) {
    if (year < flat && flat != 1900) return lo + (hi - lo) * (year - 1900) / (flat - 1900);
    return hi;
  };
  minClean = clampi(lerp(t.minMinCleanInd, t.maxMinCleanInd, t.minCleanIndFlatYear), 0, 255);
  maxClean = clampi(lerp(t.minMaxCleanInd, t.maxMaxCleanInd, t.maxCleanIndFlatYear), 0, 255);
  curveYear = year;
}

ZoneManager::IndPick ZoneManager::pickIndustrial(GrowthCity& c, SimRand& rng, int x, int z, const ZoneDescription& zd, int lv, bool watered, bool allowAg) {
  const IndustrialTuning& t = cfg_->ind;
  IndPick r;
  int pAg = t.pctChanceAg;
  if (c.ordinanceEnacted(0x62F6E808)) pAg -= t.lawnChemBanOrdEffect;
  if (c.ordinanceEnacted(0x62F6E82B)) pAg += t.farmersMarketOrdEffect;
  bool agOk = c.airPollution(x, z) <= t.minAgAirPollution && c.waterPollution(x, z) <= t.minAgWaterPollution;
  r.agAllowed = zd.hasClass('A') && pAg >= 1 && agOk;
  if (allowAg && r.agAllowed && rng.rand(100) < pAg) { r.idx = pickCategoryE(zd, lv, watered, 'A'); return r; }
  if (cfg_->cleanIndustryOnly) { r.idx = pickCategoryE(zd, lv, watered, 'C'); return r; }
  int e = std::max(c.eq(), 85);
  int clean = std::min(std::max((e - 85) * 100 / 65, minClean), maxClean), dirty = 100 - clean;
  auto on = [&](uint32_t id) { return c.ordinanceEnacted(id); };
  if (on(0xA2BF1E43)) dirty -= t.cleanAirOrd;
  if (on(0x62F6E7CF)) clean += t.touristPromotionOrd;
  if (on(0xA2F6E7DA)) clean += t.conservationCorpsOrd;
  if (on(0x02F6E7E2)) dirty -= t.indPolluteFeeOrd;
  if (on(0xE2F6E7EA)) { clean += t.cleanIndAssocOrd; dirty -= t.cleanIndAssocOrd; }
  if (on(0x02F6E7EF)) clean += t.electronicsIncentiveOrd;
  if (on(0x22F6E7F4)) clean += t.aerospaceIncentiveOrd;
  if (on(0xA2F6E7F9)) clean += t.electronicsJobFairOrd;
  if (on(0xE2F6E7FE)) clean += t.biotechIncentiveOrd;
  if (on(0xC2F6E804)) clean += t.publicAccessCableOrd;
  if (clean >= 1 && rng.rand(100) < clean) r.idx = pickCategoryE(zd, lv, watered, 'C');
  else if (dirty >= 1) r.idx = pickCategoryE(zd, lv, watered, 'D');
  return r;
}

int ZoneManager::visitRCI(GrowthCity& c, SimRand& rng, int x, int z, const ZoneDescription& zd, char fam) {
  const bool I = fam == 'I';
  const DeveloperTuning& tn = fam == 'R' ? cfg_->res : fam == 'C' ? static_cast<const DeveloperTuning&>(cfg_->com) : static_cast<const DeveloperTuning&>(cfg_->ind);
  const int vid = fam == 'R' ? kValveRes : fam == 'C' ? kValveCom : kValveInd;
  ValveReading vr = c.valve(vid);
  const int V = vr.V, Max = vr.Max, Min = vr.Min, P = committed(vid);
  LotInfo lot = c.lotAt(x, z);
  if (lot.present && (lot.ax != x || lot.az != z)) return 0;
  const bool pw = c.powered(x, z), wt = c.watered(x, z), blocked = c.radioactive(x, z);
  const int maxDist = tn.maxDistFromTransport + (c.ordinanceEnacted(0xA2BF1DE5) ? 1 : 0);
  const bool acc = c.hasAccess(x, z, maxDist);
  const int LV = c.landValue(x, z);
  if (I && (curveYear < 0 || (c.month() == 1 && curveYear != c.year()))) refreshCleanCurve(c.year());

  auto chooseCat = [&]() {
    if (!I) return pickCategoryE(zd, LV, wt);
    IndPick p = pickIndustrial(c, rng, x, z, zd, LV, wt, true);
    if (p.idx < 0 && p.agAllowed) p = pickIndustrial(c, rng, x, z, zd, LV, wt, false);
    return p.idx;
  };

  if (!lot.present) {  // empty zoned tile
    if (V < 1 || V - P < 1) return (!I && V < 0) ? kNdNegDemand : 0;
    if (blocked) return kNdRadioactive;
    if (!pw) return kNdNoPower;
    if (!acc) return kNdNoAccess;
    if (zd.cats.empty()) return 0;
    int lo = 1 << 30, hi = -1;
    for (const auto& k : zd.cats) { lo = std::min(lo, k.minLV); hi = std::max(hi, k.maxLV); }
    if (LV < lo) return kNdLVBelow;
    if (LV > hi) return kNdLVAbove;
    int cat = chooseCat();
    if (cat < 0) return I ? (wt ? kNdLVAbove : kNdNoCategory) : (wt ? kNdNoCategoryWatered : kNdNoCategory);
    startDevelopment(c, rng, x, z, zd, cat, 0, V, P, Max, false);
    return 0;
  }

  const uint32_t base = lot.baseState();
  if (base != 1 && base != 2 && base != 4) return 0;
  const int tiles = std::max(1, lot.tiles());
  const int cat = findCategory(zd, lot.agentType);
  const uint32_t keep = lot.state & 0x200;
  const bool farm = I && lot.agentType == 9300;
  const bool field = farm && (lot.flagWord & 0x100) && (lot.flagWord & 0x400);

  if (base == 4) {
    if (cat < 0 && !lot.filler()) {
      if (V > 0 && V - P > 0 && !lot.flagged() && lot.removable) { removeLot(c, lot); return 0; }
    }
    if (pw && acc && !blocked && rng.rand(int64_t(tiles) * 100) < tn.constructionTimeFactor) c.setLotState(lot, 1 | keep);
    return 0;
  }
  if (base == 2) {
    if (pw && acc && !c.tripFailed(lot.ax, lot.az) && !blocked && V > 0 && V - P > 0) {
      // Re-occupation (2 -> 1) adds the lot back to the committed demand P (spec 15 s4 "Committed demand") [STATIC].
      if (lot.flagged()) { c.setLotState(lot, 1 | keep); buildingAdded(lot.agentType, lot.sizeX, lot.sizeZ, 1); return 0; }
      int n = chooseCat();
      if (n >= 0 && n == cat) { c.setLotState(lot, 1 | keep); buildingAdded(lot.agentType, lot.sizeX, lot.sizeZ, 1); }
      else if (n >= 0) startDevelopment(c, rng, x, z, zd, n, lot.agentType, V, P, Max, false);
    }
    return 0;
  }
  // occupied
  auto abandon = [&](int code) {
    if (field) { if (lot.removable && !lot.flagged()) removeLot(c, lot); }
    else {
      // Abandonment (1 -> 2) takes the lot's amount out of the committed demand P: every developer state change replaces
      // the occupant, and P counts states 1 and 4 only (spec 15 s4 "Committed demand") [STATIC].
      c.setLotState(lot, 2 | keep);
      buildingRemoved(lot.agentType, lot.sizeX, lot.sizeZ, 1);
    }
    return code;
  };
  if (blocked) return abandon(kAbRadioactive);
  const bool unfit = c.tripFailed(lot.ax, lot.az);
  const int density = cat >= 0 ? zd.cats[size_t(cat)].density : 0;
  bool svcMissing = !pw || !acc || unfit || (!wt && density >= 2);
  if (field) svcMissing = false;
  if (svcMissing) {
    if (rng.rand(100) < tn.noServicePercentage / tiles) return abandon(!pw ? kAbNoPower : (pw && acc && !unfit) ? kAbNoWater : kAbNoAccess);
    return 0;
  }
  if (I && farm && (c.airPollution(x, z) > cfg_->ind.maxAgAirPollution || c.waterPollution(x, z) > cfg_->ind.maxAgWaterPollution)) return abandon(kAbFarmPollution);
  if (V < 0 && !field && rng.rand(-int64_t(Min)) < -(int64_t(V - P) / tiles)) return abandon(kAbNegDemand);
  // Spec 15 section 4.3 steps 4-5 [STATIC]: the lot keeps its land value while floor(minLV / 2) <= LV (all three
  // developers compare the category's minimum, the fourth number of [ZoneDeveloperDescriptions]).
  const bool adequate = cat < 0 || zd.cats[size_t(cat)].minLV / 2 <= LV;
  if (!adequate) return abandon(kAbLowLV);  // rule L never finds a category (original defect)
  const bool flagged = lot.flagged();
  int hi = (!flagged && V > 0 && cat >= 0) ? pickCategoryH(zd, cat, LV, wt) : -1;
  if (hi >= 0) {
    if (rng.rand(100) < tn.higherLandValuePct) startDevelopment(c, rng, x, z, zd, hi, lot.agentType, V, P, Max, false);
    return 0;
  }
  if (wt && !flagged) {
    const int X = fam == 'R' ? V : V - P;
    if (V > 0 && cat >= 0 && rng.rand(Max / tiles) < X) {
      int d = pickCategoryD(zd, cat, LV);
      if (d >= 0 && rng.rand(100) < tn.higherDensityPct) startDevelopment(c, rng, x, z, zd, d, lot.agentType, V, P, Max, false);
      return 0;
    }
    if (V > 0 && cat >= 0 && rng.rand(10000) < tn.anarchyFactor) startDevelopment(c, rng, x, z, zd, cat, lot.agentType, V, P, Max, false);
  }
  return 0;
}

int ZoneManager::visitPort(GrowthCity& c, SimRand& rng, int x, int z, const ZoneDescription& zd) {
  // Airport (zone 14) per-tile step, spec 15 s7 [STATIC]; S = the second service test, INFERRED to be water service.
  const bool airport = zd.developerClass == kDevAirport;
  const bool pw = c.powered(x, z), S = c.watered(x, z), blocked = c.radioactive(x, z);
  const bool acc = c.hasAccess(x, z, 6);   // the s4.2 access box with d = 6
  // Airport and seaport zones have no category list; the rules' families are filed under the port agent types
  // (12310 airport, 12330 seaport; spec 15 s5.5), which is the key of DevRule::lists.
  Category k;
  k.agentType = airport ? 12310 : 12330;
  k.minLV = 0; k.maxLV = 255;
  LotInfo lot = c.lotAt(x, z);
  if (airport) {
    if (lot.present) {
      if (lot.ax != x || lot.az != z) return 0;
      const uint32_t b = lot.baseState();
      if ((b == 2 || b == 4) && S && pw && acc && !blocked) c.setLotState(lot, 1 | (lot.state & 0x200));
      return 0;
    }
    if (blocked) return kNdRadioactive;
    if (S) {
      if (!pw) return kNdNoPower;
      if (!acc) return kNdNoAccess;
      // Airport rules run with a fixed demand of 2000 of 2000: valve 5001 does not gate airport growth.
      if (c.insideLoadedAirport(x, z)) return kNdAirportIdle;   // spec 15 s7 interim: no part is ever added there
      return startDevelopment(c, rng, x, z, zd, k, 0, 2000, 0, 2000, true) ? 0 : kNdAirportIdle;
    }
    if (!pw) return kNdNoPower;
    if (!acc) return kNdNoAccess;
    return kNdNoCategory;
  }
  // Seaport (zone 15): rules receive valve 8001's V and Max (spec 15 s3.1); code 17 when a serviceable empty tile gets
  // nothing [STATIC]. The rest of the seaport step mirrors the airport [INFERRED].
  ValveReading vr = c.valve(cfg_->seaportValve);
  int P = committed(cfg_->seaportValve);
  if (lot.present) {
    if (lot.ax != x || lot.az != z) return 0;
    const uint32_t b = lot.baseState();
    if ((b == 2 || b == 4) && pw && acc && !blocked) c.setLotState(lot, 1 | (lot.state & 0x200));
    return 0;
  }
  if (blocked) return kNdRadioactive;
  if (!pw) return kNdNoPower;
  if (!acc) return kNdNoAccess;
  if (vr.V < 1 || vr.V - P < 1) return 0;
  return startDevelopment(c, rng, x, z, zd, k, 0, vr.V, P, vr.Max, true) ? 0 : kNdSeaportIdle;
}

// ---------------- persistence ----------------
bool ZoneManager::parse(const uint8_t* p, size_t n, int w, int h, std::vector<uint8_t>* zoneGrid) {
  try {
    if (w <= 0 || h <= 0) return false;
    Reader r(p, n);
    size_t N = size_t(w) * h;
    std::vector<uint8_t> g(N);
    for (int row = 0; row < w; row++) for (int z = 0; z < h; z++) g[size_t(w - 1 - row) * h + z] = r.u8();
    ZoneManager m; m.cfg_ = cfg_;
    m.W = w; m.H = h;
    uint32_t nv = r.u32();
    if (nv > 4096) return false;
    for (uint32_t i = 0; i < nv; i++) { uint32_t id = r.u32(); m.commit[id] = r.s32(); }
    for (auto& v : m.failCur) v = r.u32();
    for (auto& v : m.failPrev) v = r.u32();
    for (ResultMap* rm : {&m.resultPrev, &m.resultCur}) {
      uint32_t k = r.u32();
      if (k > 100000) return false;
      for (uint32_t i = 0; i < k; i++) { uint8_t code = r.u8(), zone = r.u8(); (*rm)[{code, zone}] = r.u32(); }
    }
    bool haveCounts = r.remaining() >= 92;
    if (haveCounts) for (auto& v : m.zoneTileCount) v = r.u32();
    ScanState& s = m.scan;
    s.state = int8_t(r.u8()); s.mode = r.u32(); s.listValid = r.u8();
    s.x0 = r.s32(); s.x1 = r.s32(); s.z0 = r.s32(); s.z1 = r.s32(); s.count = r.s32();
    uint32_t ln = r.u32();
    if (size_t(ln) * 2 > r.remaining()) return false;
    s.list.assign(ln, 0);
    for (uint32_t i = 0; i < ln; i++) s.list[ln - 1 - i] = r.u16();  // stored reversed
    s.index = r.s32(); s.stridePos = r.s32(); s.strideStep = r.s32();
    m.cursorX = r.u32(); m.cursorZ = r.u32(); m.okCur = r.u32(); m.okPrev = r.u32(); m.failCurTotal = r.u32(); m.failPrevTotal = r.u32();
    if (m.cursorX >= uint32_t(w) || m.cursorZ >= uint32_t(h)) m.cursorX = m.cursorZ = 0;
    if (!haveCounts) m.recountZones(g);
    m.trailing = r.bytes(r.remaining());
    m.landfill = landfill; m.minClean = minClean; m.maxClean = maxClean; m.curveYear = -1;
    *this = m;
    if (zoneGrid) *zoneGrid = g;
    return true;
  } catch (const FormatError&) { return false; }
}

Bytes ZoneManager::write(const std::vector<uint8_t>& g) const {
  Bytes d;
  size_t N = size_t(W) * H;
  for (int row = 0; row < W; row++) for (int z = 0; z < H; z++) d.push_back(g.size() == N ? g[size_t(W - 1 - row) * H + z] : 0);
  put32(d, uint32_t(commit.size()));
  for (const auto& kv : commit) { put32(d, kv.first); put32(d, uint32_t(kv.second)); }
  for (auto v : failCur) put32(d, v);
  for (auto v : failPrev) put32(d, v);
  for (const ResultMap* rm : {&resultPrev, &resultCur}) {
    put32(d, uint32_t(rm->size()));
    for (const auto& kv : *rm) { d.push_back(kv.first.first); d.push_back(kv.first.second); put32(d, kv.second); }
  }
  for (auto v : zoneTileCount) put32(d, v);
  d.push_back(uint8_t(scan.state)); put32(d, scan.mode); d.push_back(scan.listValid);
  put32(d, uint32_t(scan.x0)); put32(d, uint32_t(scan.x1)); put32(d, uint32_t(scan.z0)); put32(d, uint32_t(scan.z1)); put32(d, uint32_t(scan.count));
  put32(d, uint32_t(scan.list.size()));
  for (size_t i = scan.list.size(); i-- > 0;) put16(d, scan.list[i]);
  put32(d, uint32_t(scan.index)); put32(d, uint32_t(scan.stridePos)); put32(d, uint32_t(scan.strideStep));
  put32(d, cursorX); put32(d, cursorZ); put32(d, okCur); put32(d, okPrev); put32(d, failCurTotal); put32(d, failPrevTotal);
  d.insert(d.end(), trailing.begin(), trailing.end());
  return d;
}

std::vector<std::pair<ResKey, Bytes>> ZoneManager::developerClassRecords() const {
  std::vector<std::pair<ResKey, Bytes>> out;
  if (!cfg_) return out;
  for (const auto& kv : cfg_->zones) {
    Bytes b; put32(b, kv.second.developerClass);
    out.push_back({keys::zoneDeveloperClass(kv.first), b});
  }
  return out;
}
bool ZoneManager::acceptDeveloperClass(int zone, const uint8_t* p, size_t n) const {
  if (!cfg_ || n < 4) return false;
  auto it = cfg_->zones.find(zone);
  return it != cfg_->zones.end() && it->second.developerClass == rd32(p);
}

}  // namespace growth
}  // namespace m3k
