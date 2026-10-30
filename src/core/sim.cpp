#include "core/sim.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace m3k {

static std::vector<std::string> splitStr(const std::string& s, char d) {
  std::vector<std::string> v; std::string cur;
  for (char c : s) { if (c == d) { v.push_back(trimSpaces(cur)); cur.clear(); } else cur.push_back(c); }
  v.push_back(trimSpaces(cur));
  return v;
}

int zoneTypeForTool(Tool t) {
  switch (t) {
    case Tool::ZoneRL: return 1; case Tool::ZoneRM: return 2; case Tool::ZoneRH: return 3;
    case Tool::ZoneCL: return 5; case Tool::ZoneCM: return 6; case Tool::ZoneCH: return 7;
    case Tool::ZoneIL: return 9; case Tool::ZoneIM: return 10; case Tool::ZoneIH: return 11;
    default: return 0;
  }
}
const char* toolName(Tool t) {
  switch (t) {
    case Tool::ZoneRL: return "Residential low"; case Tool::ZoneRM: return "Residential medium"; case Tool::ZoneRH: return "Residential high";
    case Tool::ZoneCL: return "Commercial low"; case Tool::ZoneCM: return "Commercial medium"; case Tool::ZoneCH: return "Commercial high";
    case Tool::ZoneIL: return "Industrial low"; case Tool::ZoneIM: return "Industrial medium"; case Tool::ZoneIH: return "Industrial high";
    case Tool::Dezone: return "Dezone"; case Tool::Road: return "Road"; case Tool::Bulldoze: return "Bulldoze";
    default: return "None";
  }
}

// ---------------- SimData ----------------

void SimData::parseZoneDescriptions(const IniFile& ini) {
  for (const auto& kv : ini.section("ZoneDeveloperDescriptions")) {
    int z = int(parseIdNumber(kv.first));
    size_t colon = kv.second.find(':');
    if (colon == std::string::npos) continue;
    ZoneDesc& zd = zones[z];
    zd.zone = z;
    auto head = splitStr(kv.second.substr(0, colon), ',');
    if (head.size() >= 2) zd.baseType = parseIdNumber(head[1]);
    zd.cats.clear();
    for (const auto& c : splitStr(kv.second.substr(colon + 1), ';')) {
      if (c.empty()) continue;
      auto f = splitStr(c, ',');
      ZoneCategory cat;
      if (f.size() == 4) { cat.agentType = parseIdNumber(f[0]); cat.density = atoi(f[1].c_str()); cat.maxLV = atoi(f[2].c_str()); cat.minLV = atoi(f[3].c_str()); }
      else if (f.size() >= 5) { cat.agentType = parseIdNumber(f[0]); cat.cls = f[1].empty() ? 'N' : f[1][0]; cat.density = atoi(f[2].c_str()); cat.maxLV = atoi(f[3].c_str()); cat.minLV = atoi(f[4].c_str()); }
      else continue;
      zd.cats.push_back(cat);
    }
  }
}

void SimData::parseZoneRules(const IniFile& ini) {
  for (const auto& kv : ini.section("ZoneDeveloperRules")) {
    auto kf = splitStr(kv.first, ',');
    int z = int(parseIdNumber(kf[0]));
    for (const auto& t : splitStr(kv.second, ';')) {
      auto f = splitStr(t, ',');
      if (f.size() >= 3) zones[z].families.insert(atoi(f[2].c_str()));
    }
  }
}

void SimData::parseValveEffects(const IniFile& ini) {
  for (const auto& kv : ini.section("AgentValveEffects")) {
    uint32_t type = parseIdNumber(kv.first);
    std::string v = kv.second;
    size_t sp = v.find_first_of(" \t;");
    if (sp != std::string::npos) v = v.substr(0, sp);
    std::vector<ValveEffect> out;
    for (const auto& item : splitStr(v, '+')) {
      auto f = splitStr(item, ':');
      if (f.size() < 3) continue;
      out.push_back({atoi(f[0].c_str()), atoi(f[1].c_str()), atoi(f[2].c_str())});
    }
    valveEffects[type] = out;
  }
}

void SimData::parseHierarchy(const IniFile& ini) {
  for (const auto& kv : ini.section("AgentTypeHierarchy")) parent[parseIdNumber(kv.first)] = parseIdNumber(kv.second);
}

void SimData::parseFamilyText(int id, const std::string& text) {
  Family f; f.id = id;
  std::istringstream is(text); std::string line;
  while (std::getline(is, line)) {
    while (!line.empty() && (line.back() == '\r')) line.pop_back();
    if (line.rfind("LandUse:", 0) == 0) f.landUse = uint32_t(atoi(line.c_str() + 8));
    else if (line.rfind("Key:", 0) == 0) {
      std::string rest = line.substr(4);
      size_t sp = rest.find_first_of("\t");
      auto p = splitStr(rest.substr(0, rest.find("  ") == std::string::npos ? rest.size() : rest.find("  ")), ',');
      if (p.size() >= 4) {
        FamilyMember m; m.group = uint32_t(strtoul(p[0].c_str(), nullptr, 10)); m.type = uint32_t(strtoul(p[1].c_str(), nullptr, 10));
        m.instance = uint32_t(strtoul(p[2].c_str(), nullptr, 10)); m.weight = std::max(1, atoi(p[3].c_str()));
        f.members.push_back(m); (void)sp;
      }
    }
  }
  families[id] = f;
}

void SimData::parseRuleFinal(const std::string& text) {
  std::vector<int> tok; std::string cur;
  for (char c : text + "\n") {
    if (c == ' ' || c == ',' || c == '\n' || c == '\t') { if (!cur.empty() && (isdigit((unsigned char)cur[0]) || cur[0] == '-')) tok.push_back(atoi(cur.c_str())); cur.clear(); }
    else cur.push_back(c);
  }
  int key = -1; bool inResults = false;
  for (size_t i = 0; i < tok.size();) {
    int tag = tok[i++];
    if (tag == 0 || tag == 1 || tag == 2 || tag == 4) { if (i >= tok.size()) break; int v = tok[i++]; if (tag == 1) { key = v; inResults = false; } if (tag == 4) inResults = true; }
    else if (tag == 3 || tag == 5) { if (i + 1 >= tok.size()) break; int dir = tok[i++], piece = tok[i++]; if (tag == 5 && (dir == 255 || dir == 31) && key >= 0 && key < 16) roadFinal[key] = uint32_t(piece); }
  }
  (void)inResults;
}

bool SimData::load(GameData& g) {
  IniFile ini;
  if (g.ini().load("SC3ZoneLayer.ini", ini)) { parseZoneDescriptions(ini); parseZoneRules(ini); }
  if (g.ini().load("SC3ValveLayer.ini", ini)) parseValveEffects(ini);
  if (g.ini().load("CitySim.ini", ini)) parseHierarchy(ini);
  if (g.ini().load("SC3Tune.ini", ini)) {
    noServicePercentage = atoi(ini.getOr("ResidentialZoneDeveloper", "NoServicePercentage", "20").c_str());
    maxDistFromTransport = atoi(ini.getOr("ResidentialZoneDeveloper", "MaxDistFromTransport", "3").c_str());
    constructionTimeFactor = atoi(ini.getOr("ResidentialZoneDeveloper", "ConstructionTimeFactor", "60").c_str());
  }
  if (g.ini().load("SC3WorldLayer.ini", ini)) {
    jobToResFactor = std::max(1, atoi(ini.getOr("WorldLayer", "JobToResFactor", "1").c_str()));
    externalJobDemand = atoi(ini.getOr("WorldLayer", "ExternalJobDemand", "600").c_str());
  }
  for (int id = 1; id <= 31; id++) {
    ResourceRef r = g.db().find({0x81F53D09, 0x01F58D3D, uint32_t(id)});
    if (!r) continue;
    uint32_t len = r.declared; if (len > r.available) len = uint32_t(r.available);
    parseFamilyText(id, std::string(reinterpret_cast<const char*>(r.data), len));
  }
  fs::path f = resolveCI(g.appsDir(), "Res/TilingRules/ROAD_GRND_final.txt");
  Bytes b;
  if (!f.empty() && readFile(f, b)) parseRuleFinal(std::string(b.begin(), b.end()));
  return !zones.empty();
}

const std::vector<ValveEffect>* SimData::effectsFor(uint32_t t) const {
  for (int hops = 0; hops < 16; hops++) {
    auto it = valveEffects.find(t);
    if (it != valveEffects.end()) return &it->second;
    auto p = parent.find(t);
    if (p == parent.end()) break;
    t = p->second;
  }
  return nullptr;
}

// ---------------- Sim ----------------

uint32_t Sim::rnd() { seed_ = seed_ * 0x41C64E6Du + 0x3039u; return seed_ >> 16; }

void Sim::init(World* w, GameData* g, const SimData* d, uint32_t seed) {
  world_ = w; game_ = g; data_ = d; seed_ = seed;
  rebuildIndex();
  scan_.clear();
  for (int x = 0; x < w->W; x++) for (int z = 0; z < w->H; z++) scan_.push_back(uint16_t(x * 256 + z));
  for (size_t i = scan_.size(); i-- > 1;) std::swap(scan_[i], scan_[rnd() % (i + 1)]);
  scanPos_ = 0;
}

void Sim::rebuildIndex() {
  size_t N = size_t(world_->W) * world_->H;
  occ_.assign(N, -1); road_.assign(N, 0); surf_.assign(N, -1);
  population_ = 0;
  for (size_t i = 0; i < world_->occupants.size(); i++) {
    const Occupant& o = world_->occupants[i];
    for (int dx = 0; dx < o.sizeX; dx++) for (int dz = 0; dz < o.sizeZ; dz++)
      if (world_->inTiles(o.x + dx, o.z + dz)) occ_[world_->tIdx(o.x + dx, o.z + dz)] = int32_t(i);
  }
  for (size_t i = 0; i < world_->surface.size(); i++) {
    const NetPiece& p = world_->surface[i];
    if (world_->inTiles(p.x, p.z)) { road_[world_->tIdx(p.x, p.z)] = 1; surf_[world_->tIdx(p.x, p.z)] = int32_t(i); }
  }
}

int Sim::roadCount() const { return int(world_->surface.size()); }

bool Sim::flat(int x, int z) const {
  int a = world_->altAt(x, z), b = world_->altAt(x + 1, z), c = world_->altAt(x, z + 1), d = world_->altAt(x + 1, z + 1);
  int mx = std::max(std::max(a, b), std::max(c, d)), mn = std::min(std::min(a, b), std::min(c, d));
  return mx - mn <= 2;  // deviation: original requires exactly flat lots
}

bool Sim::tileBuildable(int x, int z) const {
  if (!world_->inTiles(x, z)) return false;
  size_t i = world_->tIdx(x, z);
  if (occ_[i] >= 0 || road_[i]) return false;
  int lo = std::min(std::min(world_->altAt(x, z), world_->altAt(x + 1, z)), std::min(world_->altAt(x, z + 1), world_->altAt(x + 1, z + 1)));
  int w = world_->waterAt(x, z);
  if (lo < w) return false;
  return flat(x, z);
}

bool Sim::hasAccess(int x, int z, int dist) const {
  for (int dx = -dist; dx <= dist; dx++) for (int dz = -dist; dz <= dist; dz++)
    if (isRoadTile(x + dx, z + dz)) return true;
  return false;
}

bool Sim::paintZone(int x0, int z0, int x1, int z1, Tool t) {
  int zt = (t == Tool::Dezone) ? 0 : zoneTypeForTool(t);
  if (t != Tool::Dezone && zt == 0) return false;
  if (x0 > x1) std::swap(x0, x1);
  if (z0 > z1) std::swap(z0, z1);
  bool any = false;
  for (int x = x0; x <= x1; x++) for (int z = z0; z <= z1; z++) {
    if (!world_->inTiles(x, z)) continue;
    size_t i = world_->tIdx(x, z);
    if (world_->zone[i] == zt) continue;
    if (zt && !tileBuildable(x, z) && occ_[i] < 0) continue;
    if (zt && occ_[i] >= 0) continue;
    if (funds < 5) return any;
    funds -= (zt ? 5 : 1); world_->zone[i] = uint8_t(zt); any = true;
  }
  return any;
}

void Sim::setRoadPiece(int x, int z) {
  size_t i = world_->tIdx(x, z);
  int mask = 0;
  if (isRoadTile(x - 1, z)) mask |= 1;
  if (isRoadTile(x, z - 1)) mask |= 2;
  if (isRoadTile(x + 1, z)) mask |= 4;
  if (isRoadTile(x, z + 1)) mask |= 8;
  uint32_t packed;
  auto it = data_->roadFinal.find(mask);
  if (it != data_->roadFinal.end()) packed = it->second;
  else packed = (mask == 5 ? (29u << 8) | 1 : mask == 10 ? (29u << 8) : (43u << 8));
  NetPiece* p;
  if (surf_[i] >= 0) p = &world_->surface[surf_[i]];
  else { world_->surface.push_back({}); surf_[i] = int32_t(world_->surface.size() - 1); p = &world_->surface.back(); }
  p->x = x; p->z = z; p->y = world_->altAt(x, z);
  p->pieceId = packed >> 8; p->rot = int(packed & 3); p->pieceWord = 0x30000000u | p->pieceId;
  road_[i] = 1;
}

bool Sim::buildRoad(const std::vector<std::pair<int, int>>& path) {
  bool any = false;
  std::vector<std::pair<int, int>> placed;
  for (auto [x, z] : path) {
    if (!world_->inTiles(x, z)) continue;
    size_t i = world_->tIdx(x, z);
    if (road_[i]) continue;
    if (occ_[i] >= 0) continue;
    int lo = std::min(std::min(world_->altAt(x, z), world_->altAt(x + 1, z)), std::min(world_->altAt(x, z + 1), world_->altAt(x + 1, z + 1)));
    if (lo < world_->waterAt(x, z) || !flat(x, z)) continue;
    if (funds < 10) break;
    funds -= 10;
    world_->zone[i] = world_->zone[i];  // zone under road is kept; developers skip road tiles
    road_[i] = 1; surf_[i] = -2;        // provisional
    placed.push_back({x, z}); any = true;
  }
  for (auto [x, z] : placed) surf_[world_->tIdx(x, z)] = -1;
  for (auto [x, z] : placed) setRoadPiece(x, z);
  for (auto [x, z] : placed) {
    const int d[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (auto& dd : d) if (isRoadTile(x + dd[0], z + dd[1]) && surf_[world_->tIdx(x + dd[0], z + dd[1])] >= 0) setRoadPiece(x + dd[0], z + dd[1]);
  }
  return any;
}

bool Sim::bulldoze(int x0, int z0, int x1, int z1) {
  if (x0 > x1) std::swap(x0, x1);
  if (z0 > z1) std::swap(z0, z1);
  bool any = false;
  std::set<int> kill;
  std::vector<std::pair<int, int>> roads;
  for (int x = x0; x <= x1; x++) for (int z = z0; z <= z1; z++) {
    if (!world_->inTiles(x, z)) continue;
    size_t i = world_->tIdx(x, z);
    if (occ_[i] >= 0) kill.insert(occ_[i]);
    if (road_[i]) roads.push_back({x, z});
    if (world_->flora[i]) { world_->flora[i] = 0; any = true; }
  }
  if (kill.empty() && roads.empty()) return any;
  funds -= int64_t(kill.size()) * 1 + int64_t(roads.size());
  for (auto it = kill.rbegin(); it != kill.rend(); ++it) world_->occupants.erase(world_->occupants.begin() + *it);
  std::set<int> rk;
  for (auto [x, z] : roads) if (surf_[world_->tIdx(x, z)] >= 0) rk.insert(surf_[world_->tIdx(x, z)]);
  for (auto it = rk.rbegin(); it != rk.rend(); ++it) world_->surface.erase(world_->surface.begin() + *it);
  rebuildIndex();
  for (auto [x, z] : roads) {
    const int d[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (auto& dd : d) if (isRoadTile(x + dd[0], z + dd[1])) setRoadPiece(x + dd[0], z + dd[1]);
  }
  return true;
}

uint32_t Sim::valveFor(int zone) const { return zone >= 1 && zone <= 3 ? 3000 : zone >= 5 && zone <= 7 ? 5000 : zone >= 9 && zone <= 11 ? 8000 : 0; }

const ZoneCategory* Sim::pickCategory(const ZoneDesc& zd, int lv, char cls) const {
  int dens = zd.zone <= 3 ? zd.zone : zd.zone <= 7 ? zd.zone - 4 : zd.zone - 8;
  const ZoneCategory* best = nullptr;
  for (const auto& c : zd.cats) {
    if (c.density > dens || lv < c.minLV || lv > c.maxLV) continue;
    if (cls && c.cls != cls) continue;
    if (c.cls == 'A') continue;  // agriculture not modelled
    if (!best || c.minLV > best->minLV) best = &c;
  }
  return best;
}

int Sim::addOccupant(const Occupant& o) {
  world_->occupants.push_back(o);
  int idx = int(world_->occupants.size()) - 1;
  for (int dx = 0; dx < o.sizeX; dx++) for (int dz = 0; dz < o.sizeZ; dz++) occ_[world_->tIdx(o.x + dx, o.z + dz)] = idx;
  return idx;
}

bool Sim::startDevelopment(int x, int z, const ZoneDesc& zd, const ZoneCategory& cat) {
  struct Cand { const FamilyMember* m; int sx, sz; };
  std::vector<Cand> cands;
  for (int fid : zd.families) {
    auto fi = data_->families.find(fid);
    if (fi == data_->families.end() || fi->second.landUse != cat.agentType) continue;
    for (const auto& m : fi->second.members) {
      int sx = 1, sz = 1;
      if (game_) {
        const OccAttr* a = game_->occupant(m.instance, false, m.type == T_OCC_ATTR_PORT);
        if (!a) continue;
        if (a->filler()) continue;
        sx = int(a->sizeX); sz = int(a->sizeZ);
      }
      bool fits = true;
      for (int dx = 0; dx < sx && fits; dx++) for (int dz = 0; dz < sz && fits; dz++) {
        int tx = x + dx, tz = z + dz;
        if (!tileBuildable(tx, tz) || world_->zone[world_->tIdx(tx, tz)] != world_->zone[world_->tIdx(x, z)]) fits = false;
      }
      if (!fits) continue;
      for (int w = 0; w < m.weight; w++) cands.push_back({&m, sx, sz});
    }
  }
  if (cands.empty()) return false;
  // Prefer larger lots when available (greedy lot growth in the original).
  int maxArea = 0;
  for (auto& c : cands) maxArea = std::max(maxArea, c.sx * c.sz);
  std::vector<Cand> big;
  for (auto& c : cands) if (c.sx * c.sz == maxArea || rnd(3) == 0) big.push_back(c);
  const Cand& c = big[rnd(int(big.size()))];
  Occupant o;
  o.x = x; o.z = z; o.y = world_->altAt(x, z); o.rot = rnd(4); o.sizeX = c.sx; o.sizeZ = c.sz;
  o.occId = c.m->instance; o.keyType = c.m->type; o.agentType = cat.agentType; o.state = 4;
  addOccupant(o);
  if (auto* ef = data_->effectsFor(cat.agentType)) {
    for (auto& e : *ef) {
      int add = c.sx * c.sz * e.supply;
      if (e.valve == 3000) committedR_ += add; else if (e.valve == 5000) committedC_ += add; else if (e.valve == 8000) committedI_ += add;
    }
  }
  return true;
}

void Sim::visitTile(int x, int z) {
  size_t i = world_->tIdx(x, z);
  int zt = world_->zone[i];
  int vk = int(valveFor(zt));
  if (!vk) return;
  Valve& V = vk == 3000 ? valveR : vk == 5000 ? valveC : valveI;
  int P = vk == 3000 ? committedR_ : vk == 5000 ? committedC_ : committedI_;
  int oi = occ_[i];
  if (oi < 0) {
    if (road_[i]) return;
    auto zi = data_->zones.find(zt);
    if (zi == data_->zones.end()) return;
    if (V.value < 1 || V.value - P < 1) return;
    if (!hasAccess(x, z, data_->maxDistFromTransport)) return;
    const int lv = 128;  // deviation: land value layer not modelled
    char cls = 0;
    if (vk == 8000) cls = rnd(100) < 50 ? 'C' : 'D';
    const ZoneCategory* cat = pickCategory(zi->second, lv, cls);
    if (!cat && vk == 8000) cat = pickCategory(zi->second, lv, cls == 'C' ? 'D' : 'C');
    if (!cat) return;
    startDevelopment(x, z, zi->second, *cat);
    return;
  }
  Occupant& o = world_->occupants[oi];
  if (o.x != x || o.z != z) return;  // anchor tile only
  int tiles = o.sizeX * o.sizeZ;
  bool access = hasAccess(x, z, data_->maxDistFromTransport);
  if (o.state == 4) {
    if (access && rnd(tiles * 100) < data_->constructionTimeFactor) o.state = 1;
  } else if (o.state == 1) {
    if (!access) { if (rnd(100) < data_->noServicePercentage / tiles) o.state = 2; }
    else if (V.value < 0 && rnd(2000) < -((V.value - P) / tiles)) o.state = 2;
  } else if (o.state == 2) {
    if (access && V.value > 0 && V.value - P > 0) o.state = 1;
  }
}

void Sim::tickDay() {
  day++;
  int K = world_->W * world_->H / 30;
  for (int k = 0; k < K; k++) {
    if (scanPos_ >= scan_.size()) {
      for (size_t i = scan_.size(); i-- > 1;) std::swap(scan_[i], scan_[rnd() % (i + 1)]);
      scanPos_ = 0; committedR_ = committedC_ = committedI_ = 0;
    }
    uint16_t e = scan_[scanPos_++];
    int x = e >> 8, z = e & 255;
    if (world_->inTiles(x, z)) visitTile(x, z);
  }
}

void Sim::tickMonth() {
  int resS = 0, comS = 0, indS = 0, jobsD = 0;
  for (const auto& o : world_->occupants) {
    if (o.state != 1) continue;
    const auto* ef = data_->effectsFor(o.agentType);
    if (!ef) continue;
    int tiles = o.sizeX * o.sizeZ;
    for (const auto& e : *ef) {
      switch (e.valve) {
        case 3000: resS += tiles * e.supply; break;
        case 5000: comS += tiles * e.supply; break;
        case 8000: indS += tiles * e.supply; break;
        case 7000: jobsD += tiles * e.demand; break;
        default: break;
      }
    }
  }
  population_ = resS;
  int a = data_->phaseCommercial, b = data_->phaseIndustrial;
  int workforce = int(resS * 0.6) + data_->externalJobDemand;
  auto clampv = [](int v) { return std::max(-2000, std::min(2000, v)); };
  valveR.supply = resS; valveR.demand = data_->jobToResFactor * jobsD + 400;  // +400: bootstrap (deviation)
  valveC.supply = comS; valveC.demand = workforce * a / (a + b);
  valveI.supply = indS; valveI.demand = workforce * b / (a + b);
  for (Valve* v : {&valveR, &valveC, &valveI}) v->value = clampv(v->demand - v->supply);
  committedR_ = committedC_ = committedI_ = 0;
  // Budget (simplified): taxes minus road upkeep.
  funds += int64_t(population_) * taxRate / 10 - roadCount() / 2;
  day += 0;
}

}  // namespace m3k
