#include "core/city.h"
#include "core/occ_defaults.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "core/flora.h"
#include "core/fsutil.h"
#include "core/network_tools.h"
#include "core/records.h"
#include "core/world_network_map.h"

namespace m3k {

namespace {

std::vector<std::string> splitStr(const std::string& s, char d) {
  std::vector<std::string> v;
  std::string cur;
  for (char c : s) {
    if (c == d) { v.push_back(trimSpaces(cur)); cur.clear(); } else cur.push_back(c);
  }
  v.push_back(trimSpaces(cur));
  return v;
}

const ResKey kNeighbourKey{NeighbourDeals::kType, NeighbourDeals::kGroup, NeighbourDeals::kInstance};
const ResKey kBudgetKey{Budget::kType, Budget::kGroup, Budget::kInstance};
const ResKey kOrdKey{Ordinances::kType, Ordinances::kGroup, Ordinances::kInstance};
const ResKey kTrafficKey{kTrafficRecType, kTrafficRecGroup, 0};
const ResKey kCrimeKey{0x206C6E7C, 0x20A7AE7F, 0};
const ResKey kPollutionKey{0x206C6E7C, 0xC0A81498, 0};
const ResKey kPoliceKey{0x206C6E7C, 0x00ABF2EC, 0};
const ResKey kFireKey{0x206C6E7C, 0xA0F42214, 0};

constexpr uint32_t kAgentPolice = 13130, kAgentFire = 13002, kAgentJail = 13881;
constexpr uint32_t kAgentPump = 12810, kAgentTower = 12840, kAgentDesal = 12830, kAgentRecycling = 12851;

int absMonthOf(const Clock& c) { cal::Date d = c.date(); return 12 * d.year + d.month; }

bool isDeveloperType(uint32_t agentType) {
  uint32_t k = agentType / 1000;
  return k == 3 || k == 5 || k == 8;
}

}  // namespace

// ============================================================================ config

void CityIniSet::load(GameData& g) {
  auto L = [&](const char* n, IniFile& f) { g.ini().load(n, f); };
  L("SC3PowerLayer.ini", powerLayer); L("SC3WaterLayer.ini", waterLayer); L("SC3BudgetLayer.ini", budgetLayer);
  L("Advisor-Budget.ini", advisorBudget); L("SC3Tune.ini", tune); L("SC3Crime.ini", crime);
  L("SC3Pollution.ini", pollution); L("SC3Police.ini", police); L("SC3FireLayer.ini", fire);
  L("SC3Aura.ini", aura); L("STTraffic.INI", traffic); L("SC3ResLayer.ini", resLayer);
  L("SC3ComLayer.ini", comLayer); L("SC3IndLayer.ini", indLayer); L("CitySim.ini", citySim);
  L("SC3ZoneLayer.ini", zoneLayer); L("SC3ValveLayer.ini", valveLayer); L("SC3WorldLayer.ini", worldLayer);
  L("SC3LandValue.ini", landValue); L("NeighborDeals.ini", neighbourDeals); L("Ordinances.ini", ordinances);
}

CityConfig::CityConfig() {
  valve.externalJobDemand = 600;
  valve.jobToResFactor = 1;
  valve.bootstrapResDemand = 400;
}

void CityConfig::parseFamilyText(int id, const std::string& text, GameData*) {
  FamilyDef f;
  f.id = id;
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    while (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("LandUse:", 0) == 0) f.landUse = uint32_t(atoi(line.c_str() + 8));
    else if (line.rfind("Key:", 0) == 0) {
      std::string rest = line.substr(4);
      size_t cut = rest.find("  ");
      auto p = splitStr(rest.substr(0, cut == std::string::npos ? rest.size() : cut), ',');
      if (p.size() >= 4) {
        FamilyMember m;
        m.group = uint32_t(strtoul(p[0].c_str(), nullptr, 10));
        m.type = uint32_t(strtoul(p[1].c_str(), nullptr, 10));
        m.instance = uint32_t(strtoul(p[2].c_str(), nullptr, 10));
        m.weight = std::max(1, atoi(p[3].c_str()));
        f.members.push_back(m);
      }
    }
  }
  families[id] = f;
}

void CityConfig::bindFamilies(const std::vector<growth::RuleBinding>& b, GameData* g) {
  bindings = b;
  for (const auto& bind : bindings) {
    growth::DevRule& r = growth.ruleFor(bind.zone, bind.ruleId);
    r.lists.clear();
    for (int fid : bind.families) {
      auto fi = families.find(fid);
      if (fi == families.end()) continue;
      for (const auto& m : fi->second.members) {
        growth::BuildingChoice bc;
        bc.occId = m.instance;
        bc.agentType = fi->second.landUse;
        if (g) {
          const OccAttr* a = g->occupant(m.instance, false, m.type == T_OCC_ATTR_PORT);
          if (!a) continue;
          bc.sizeX = int(a->sizeX); bc.sizeZ = int(a->sizeZ); bc.flagWord = a->buildingFlags; bc.line = a->partIndex;
        }
        bc.family = fid; bc.port = m.type == T_OCC_ATTR_PORT;
        r.addMember(fi->second.landUse, bc, m.weight);
      }
    }
  }
}

void CityConfig::parseRuleFinal(const std::string& text) {
  std::vector<int> tok;
  std::string cur;
  for (char c : text + "\n") {
    if (c == ' ' || c == ',' || c == '\n' || c == '\t' || c == '\r') {
      if (!cur.empty() && (isdigit(static_cast<unsigned char>(cur[0])) || cur[0] == '-')) tok.push_back(atoi(cur.c_str()));
      cur.clear();
    } else cur.push_back(c);
  }
  int key = -1;
  for (size_t i = 0; i < tok.size();) {
    int tag = tok[i++];
    if (tag == 0 || tag == 1 || tag == 2 || tag == 4) {
      if (i >= tok.size()) break;
      int v = tok[i++];
      if (tag == 1) key = v;
    } else if (tag == 3 || tag == 5) {
      if (i + 1 >= tok.size()) break;
      int dir = tok[i++], piece = tok[i++];
      if (tag == 5 && (dir == 255 || dir == 31) && key >= 0 && key < 16) roadFinal[key] = uint32_t(piece);
    }
  }
}

void CityConfig::expandEffects() {
  std::set<uint32_t> types;
  for (const auto& z : growth.zones) for (const auto& c : z.second.cats) types.insert(c.agentType);
  for (const auto& zr : growth.rules)
    for (const auto& r : zr.second)
      for (const auto& l : r.lists) {
        types.insert(l.first);
        for (const auto* v : {&l.second.filler, &l.second.small, &l.second.large}) for (const auto& b : *v) types.insert(b.agentType);
      }
  for (uint32_t t : types) {
    if (valve.agentEffects.count(t)) continue;
    uint32_t cur = t;
    for (int hop = 0; hop < 16; hop++) {
      auto p = parent.find(cur);
      if (p == parent.end()) break;
      cur = p->second;
      auto it = valve.agentEffects.find(cur);
      if (it != valve.agentEffects.end()) { valve.agentEffects[t] = it->second; break; }
    }
  }
}

void CityConfig::applySyntheticDefaults() {
  // Defaults live in the constructor (valve) and in City::init (power, water, stations).
  growth.valveEffects = &valve.agentEffects;
}

void CityConfig::applyDemoData() {
  std::string desc = "[ZoneDeveloperDescriptions]\n", rules = "[ZoneDeveloperRules]\n";
  for (int z : {1, 2, 3}) {
    desc += std::to_string(z) + "=0x619BA64E,3000:3100,1,255,0;3300,1,255,100\n";
    rules += std::to_string(z) + ",0xA22653C0=81F53D09,01F58D3D,4;81F53D09,01F58D3D,11\n";
  }
  for (int z : {5, 6, 7}) {
    desc += std::to_string(z) + "=0x41A3ADC1,5000:5100,1,255,0\n";
    rules += std::to_string(z) + ",0x422C9D52=81F53D09,01F58D3D,7\n";
  }
  for (int z : {9, 10, 11}) {
    desc += std::to_string(z) + "=0xE1A53B30,8000:8100,D,1,255,0\n";
    rules += std::to_string(z) + ",0x422C9D53=81F53D09,01F58D3D,9\n";
  }
  growth.parseZoneDescriptions(IniFile::fromText(desc));
  auto binds = growth.parseRuleBindings(IniFile::fromText(rules));
  parseFamilyText(4, "TXT! r\r\nLandUse: 3300\r\nKey: 1406,545184782,100,1\r\n");
  parseFamilyText(11, "TXT! r\r\nLandUse: 3100\r\nKey: 1406,545184782,101,2\r\n");
  parseFamilyText(7, "TXT! r\r\nLandUse: 5100\r\nKey: 1406,545184782,200,1\r\n");
  parseFamilyText(9, "TXT! r\r\nLandUse: 8100\r\nKey: 1406,545184782,300,1\r\n");
  bindFamilies(binds);
  valve.parseAgentValveEffects(IniFile::fromText(
      "[AgentValveEffects]\n3100=3000:10:0+7000:0:0\n3300=3000:10:0+7000:0:0\n5100=5000:5:0+7000:0:8\n8100=8000:4:0+7000:0:6\n"));
  expandEffects();
  applySyntheticDefaults();
}

bool CityConfig::load(GameData& g) {
  ini.load(g);
  synthetic = false;
  growth.parseZoneDescriptions(ini.zoneLayer);
  auto binds = growth.parseRuleBindings(ini.zoneLayer);
  growth.parseTune(ini.tune);
  valve.parseAgentValveEffects(ini.valveLayer);
  valve.parseConnectionEffects(ini.valveLayer);
  valve.parseOrdinanceEffects(ini.valveLayer);
  valve.parseWorldLayer(ini.worldLayer);
  valve.parseResLayer(ini.resLayer);
  valve.parseComLayer(ini.comLayer);
  valve.parseIndLayer(ini.indLayer);
  {
    std::string s;
    if (ini.budgetLayer.get("TuningParameters", "TaxationDemandEffect", s)) {
      valve.parseTaxation(s);
      // With the real taxation table the tax modifier f2 gives a new city its first demand (shipped New City: f2 = 0.067), so the
      // synthetic bootstrap is only a fallback for data sets without that table (tests, demos).
      valve.bootstrapResDemand = 0;
    }
  }
  landValue.parse(ini.landValue);
  landValue.parseHierarchy(ini.citySim);
  for (const auto& kv : ini.citySim.section("AgentTypeHierarchy")) parent[parseIdNumber(kv.first)] = parseIdNumber(kv.second);
  valve.parent = parent;
  for (int id = 1; id <= 31; id++) {
    ResourceRef r = g.db().find({0x81F53D09, 0x01F58D3D, uint32_t(id)});
    if (!r) continue;
    uint32_t len = r.declared;
    if (len > r.available) len = uint32_t(r.available);
    parseFamilyText(id, std::string(reinterpret_cast<const char*>(r.data), len));
  }
  bindFamilies(binds, &g);
  Bytes b;
  fs::path f = resolveCI(g.appsDir(), "Res/TilingRules/ROAD_GRND_final.txt");
  if (!f.empty() && readFile(f, b)) parseRuleFinal(std::string(b.begin(), b.end()));
  int found = 0;
  fs::path apps = g.appsDir();
  tiling = TilingRuleSet();
  tiling.load([&](const std::string& name, std::string& out) {
    fs::path p = resolveCI(apps, "Res/TilingRules/" + name);
    Bytes d;
    if (p.empty() || !readFile(p, d)) return false;
    out.assign(d.begin(), d.end());
    found++;
    return true;
  });
  haveTiling = found > 0 && !tiling.nets[NT_ROAD].set.empty();
  expandEffects();
  applySyntheticDefaults();
  return !growth.zones.empty();
}

// ============================================================================ adapters

class GrowthAdapter : public growth::GrowthCity {
 public:
  explicit GrowthAdapter(City& c) : c_(c) {}
  int mapW() const override { return c_.world_->W; }
  int mapH() const override { return c_.world_->H; }
  int zoneAt(int x, int z) const override { return c_.world_->zone[c_.idx(x, z)]; }
  bool powered(int x, int z) const override { return c_.power.powered(x, z); }
  bool watered(int x, int z) const override { return c_.water.tileServiced(x, z); }
  bool radioactive(int x, int z) const override { return c_.radioactiveAt(x, z); }
  // Landfill (spec 15 s7): a landfill tile keeps its garbage in the pollution layer's standing word; the zone-17 developer
  // decays it on each visit (credit back as capacity) and fills tiles from the garbage the disposal stage parked.
  int landfillGarbage(int x, int z) const override { return c_.world_->inTiles(x, z) ? c_.pollution.standingGarbageAt(x, z) : 0; }
  void setLandfillGarbage(int x, int z, int g) override { if (c_.world_->inTiles(x, z)) c_.pollution.setStandingGarbage(x, z, g); }
  bool landfillRegistered(int x, int z) const override { return c_.world_->inTiles(x, z) && c_.pollution.landfillRegistered(x, z); }
  void registerLandfill(int x, int z) override { if (c_.world_->inTiles(x, z)) c_.pollution.registerLandfill(x, z); }
  void creditLandfillRoom(int d) override { c_.pollution.disp.store.D += d; }
  int64_t landfillPending() const override { return c_.pollution.disp.store.S; }
  void landfillPlaced(int a) override {
    auto& d = c_.pollution.disp;
    d.store.S -= a; d.store.D -= a; d.month[us::GarbageDisposal::Landfilled] += a;
  }
  int landValue(int x, int z) const override {
    if (c_.cfg_.synthetic) return 128;
    return std::max(1, c_.landValue.landValueAtTile(x, z));
  }
  bool isTransport(int x, int z) const override { return c_.isRoadTile(x, z); }
  // Access (spec 15 s4.2) [STATIC]: a transport network piece (road, highway, rail; not stops or stations) in the box
  // [x - d - 1, x + d + 1] x [z - d - 1, z + d + 1] around the visited (anchor) tile, i.e. reach d + 1.
  bool hasAccess(int x, int z, int d) const override {
    for (int xx = x - d - 1; xx <= x + d + 1; xx++)
      for (int zz = z - d - 1; zz <= z + d + 1; zz++)
        if (c_.isRoadTile(xx, zz)) return true;
    return false;
  }
  growth::LotInfo lotAt(int x, int z) const override {
    growth::LotInfo l;
    int oi = c_.occupantAt(x, z);
    if (oi < 0) return l;
    const Occupant& o = c_.world_->occupants[size_t(oi)];
    l.present = true; l.ax = o.x; l.az = o.z; l.state = o.state; l.agentType = o.agentType;
    l.sizeX = o.sizeX; l.sizeZ = o.sizeZ;
    if (c_.game_) { const OccAttr* a = c_.game_->occupant(o.occId, false, o.keyType == T_OCC_ATTR_PORT); if (a) l.flagWord = a->buildingFlags; }
    l.removable = isDeveloperType(o.agentType);
    return l;
  }
  growth::ValveReading valve(int id) const override {
    const growth::Valve& v = c_.valves.valve(id);
    return growth::ValveReading{v.V, v.Max, v.Min};
  }
  bool tripFailed(int x, int z) const override {
    return c_.cfg_.useTripFailure && c_.traffic && c_.traffic->tripResult(x, z) == 1;
  }
  int airPollution(int x, int z) const override { return c_.pollution.airAt(x, z); }
  int waterPollution(int x, int z) const override { return c_.pollution.waterAt(x, z); }
  bool ordinanceEnacted(uint32_t id) const override { return c_.ordinances.enacted(id); }
  int year() const override { return c_.clock.date().year; }
  int month() const override { return c_.clock.date().month; }
  int eq() const override { return c_.pop.workingAgeEQ(); }   // Q(20, 65), spec 15 s4.5 [STATIC]
  bool canPlace(const growth::BuildingChoice& b, int x, int z) const override {
    for (int dx = 0; dx < b.sizeX; dx++)
      for (int dz = 0; dz < b.sizeZ; dz++) {
        int tx = x + dx, tz = z + dz;
        if (!c_.inMap(tx, tz) || c_.isRoadTile(tx, tz) || c_.isPowerLine(tx, tz)) return false;
        int oi = c_.occ_[c_.idx(tx, tz)];
        if (oi >= 0 && !isDeveloperType(c_.world_->occupants[size_t(oi)].agentType)) return false;
        if (!c_.flatTile(tx, tz)) return false;
        int lo = std::min(std::min(c_.world_->altAt(tx, tz), c_.world_->altAt(tx + 1, tz)),
                          std::min(c_.world_->altAt(tx, tz + 1), c_.world_->altAt(tx + 1, tz + 1)));
        if (lo < c_.world_->waterAt(tx, tz)) return false;
      }
    return true;
  }
  void placeBuilding(const growth::BuildingChoice& b, int x, int z) override {
    Occupant o;
    o.x = x; o.z = z; o.y = c_.world_->altAt(x, z); o.rot = b.rot >= 0 ? (b.rot & 3) : int(c_.urng_.rand(4));
    o.sizeX = b.sizeX; o.sizeZ = b.sizeZ; o.occId = b.occId; o.keyType = b.port ? T_OCC_ATTR_PORT : T_OCC_ATTR;
    o.agentType = b.agentType; o.state = 4;
    c_.addOccupant(o);
  }
  void removeBuilding(const growth::LotInfo& lot) override {
    int oi = c_.occupantAt(lot.ax, lot.az);
    if (oi < 0) return;
    std::set<int> kill{oi};
    c_.removeOccupantIdx(kill);
  }
  void setLotState(const growth::LotInfo& lot, uint32_t st) override {
    int oi = c_.occupantAt(lot.ax, lot.az);
    if (oi < 0) return;
    c_.world_->occupants[size_t(oi)].state = st;
    c_.occDirty_ = true;
  }

 private:
  City& c_;
};

// Agent type of flora occupants in [AgentIDModifiers] (spec 15 s6.3 calibration note 3).
constexpr uint32_t kFloraAgentType = 300;

class LandValueAdapter : public growth::LandValueCity {
 public:
  explicit LandValueAdapter(City& c) : c_(c) {}
  int mapW() const override { return c_.world_->W; }
  int mapH() const override { return c_.world_->H; }
  int zoneAt(int x, int z) const override { return c_.world_->zone[c_.idx(x, z)]; }
  int airPollution(int x, int z) const override { return c_.pollution.airAt(x, z); }
  int waterPollution(int x, int z) const override { return c_.pollution.waterAt(x, z); }
  int garbage(int x, int z) const override { return c_.pollution.standingGarbageAt(x, z); }
  int crime(int x, int z) const override { return c_.crime.crimeAt(x, z); }
  int altitude(int x, int z) const override { return c_.world_->altAt(std::min(x, c_.world_->W), std::min(z, c_.world_->H)); }
  int waterSurface(int x, int z) const override { return c_.world_->waterAt(std::min(x, c_.world_->W), std::min(z, c_.world_->H)); }
  int seaLevel() const override { return c_.world_->seaLevel; }
  bool radioactive(int x, int z) const override { return c_.radioactiveAt(x, z); }
  // Water tile (spec 15 s6.3 step 1) [STATIC]: at any of the four corner vertices the water surface is >= the ground, so
  // dry land exactly at the sea level counts as water.
  bool hasWater(int x, int z) const override {
    if (!c_.world_->inTiles(x, z)) return false;
    for (int dx = 0; dx <= 1; dx++)
      for (int dz = 0; dz <= 1; dz++)
        if (c_.world_->waterAt(x + dx, z + dz) >= c_.world_->altAt(x + dx, z + dz)) return true;
    return false;
  }
  // Interim for high saved values on all-zone-0 blocks (spec 15 s6.3 step 6g note, [OPEN]): such a block keeps its saved
  // byte until the zone or occupant of one of its tiles changes; this signature detects the change.
  uint32_t blockSignature(int x, int z) const override {
    uint32_t h = 2166136261u;
    for (int dx = 0; dx < 2; dx++)
      for (int dz = 0; dz < 2; dz++) {
        int tx = x + dx, tz = z + dz;
        uint32_t v = 0;
        if (c_.world_->inTiles(tx, tz)) {
          v = c_.world_->zone[c_.idx(tx, tz)];
          int oi = c_.occupantAt(tx, tz);
          if (oi >= 0) v ^= (c_.world_->occupants[size_t(oi)].uid << 8) ^ c_.world_->occupants[size_t(oi)].occId;
        }
        h = (h ^ v) * 16777619u;
      }
    return h;
  }
  // Curves are read at valve 3000 Slast (spec 15 s6.3 cycle start step 1) [STATIC]; no factor 2.
  int64_t population() const override { return c_.valves.v3000.Slast; }
  bool ordinanceEnacted(uint32_t id) const override { return c_.ordinances.enacted(id); }
  // Static occupant set for the modifier maps (spec 15 s6.4 step 2): buildings (occupied: state 1 or 513), network pieces
  // and flora (no lot state, always counted) [INFERRED medium-high for pieces and flora].
  void occupants(std::vector<growth::LvOccupant>& out) const override {
    for (const auto& o : c_.world_->occupants) out.push_back({o.x, o.z, o.sizeX, o.sizeZ, o.agentType, (o.state & ~0x200u) == 1});
    if (c_.game_)
      for (const auto* net : {&c_.world_->surface, &c_.world_->pipes, &c_.world_->subway})
        for (const auto& p : *net)
          if (const OccAttr* a = c_.game_->occupant(p.pieceId, true)) out.push_back({p.x, p.z, 1, 1, a->agentType, true});
    const World& w = *c_.world_;
    for (int x = 0; x < w.W; x++)
      for (int z = 0; z < w.H; z++) {
        uint8_t f = w.flora[size_t(w.tIdx(x, z))];
        if (f && floraCellValid(f) && (f >> 4) != 0) out.push_back({x, z, 1, 1, kFloraAgentType, true});
      }
  }
  void cityCentre(int& cx, int& cz) const override {
    growth::BuildingCentroid bc;
    for (const auto& o : c_.world_->occupants) bc.add(o.x, o.z, o.sizeX, o.sizeZ);
    bc.get(c_.world_->W, c_.world_->H, cx, cz);
  }

 private:
  City& c_;
};

// ============================================================================ City basics

City::City() {}
City::~City() {}

namespace {
us::PowerParams syntheticPower(us::PowerParams p) {
  bool any = false;
  for (auto& kv : p.plants) if (kv.second.capacity > 0) any = true;
  if (any) return p;
  static const int cap[9] = {6000, 4000, 8000, 10000, 20000, 30000, 2000, 1500, 2500};
  const auto& types = us::powerPlantTypes();
  for (size_t i = 0; i < types.size(); i++) {
    us::PlantTunables& t = p.plants[types[i]];
    t.capacity = cap[i]; t.maxLife = 600; t.declineAge = 480; t.variationPct = 0;
  }
  return p;
}
us::WaterParams syntheticWater(us::WaterParams p) {
  if (p.towerCapa || p.pumpCapa || p.desaltCapa) return p;
  p.towerCapa = 4000; p.pumpCapa = 8000; p.desaltCapa = 8000;
  p.towerLifeYears = p.pumpLifeYears = p.desaltLifeYears = 50;
  p.pumpDistanceFactor = 50;
  if (p.defaultConsumption == 0) p.defaultConsumption = 1;
  return p;
}
}  // namespace

void City::init(World* w, GameData* g, const CityConfig& cfg, uint32_t seed) {
  world_ = w; game_ = g; cfg_ = cfg; seed_ = seed;
  cfg_.growth.valveEffects = &cfg_.valve.agentEffects;
  const int W = w->W, H = w->H;
  rng_ = SimRng(seed); urng_ = us::SimRng(seed);
  zoneRng_ = growth::SimRand(seed); zoneCrt_ = growth::CrtRand(seed | 1u);
  usHier_ = us::AgentHierarchy(); usHier_.loadIni(cfg_.ini.citySim);
  ecoHier_ = AgentHierarchy();
  for (const auto& kv : cfg_.parent) { usHier_.set(kv.first, kv.second); ecoHier_.setParent(kv.first, kv.second); }

  clock = Clock();
  sched = CitySchedule();
  clock.setFoundingYear(w->info.foundingYear);
  clock.setJdn(cal::jdnFromDayOfYear(std::max(1, w->info.dayOfYear), w->info.year));
  sched.elapsedDays = 0;

  budget = Budget(BudgetConfig::fromIni(cfg_.ini.budgetLayer, &cfg_.ini.advisorBudget));
  budget.newCity(20000);
  ordinances = Ordinances();
  if (cfg_.ini.ordinances.has("ListOfOrdinances")) ordinances.loadCatalogue(cfg_.ini.ordinances);
  ordinances.newCity();
  neighbours = NeighbourDeals();
  neighbours.cfg = DealConfig::fromIni(cfg_.ini.neighbourDeals);
  neighbours.newCity(rng_, W, H, [w](int x, int z) { return w->inTiles(x, z) && w->waterAt(x, z) <= w->altAt(x, z); });
  business = cfg_.ini.advisorBudget.has("BUSINESS_DEALS") ? BusinessDeals::fromIni(cfg_.ini.advisorBudget) : BusinessDeals();
  pop = Population(PopulationConfig::fromIni(cfg_.ini.resLayer), seed);
  pop.newCity();

  valves = growth::ValveLayer(); valves.init(W, H, cfg_.valve);
  landValue = growth::LandValueLayer(); landValue.init(W, H, cfg_.landValue);
  zones = growth::ZoneManager(); zones.setConfig(&cfg_.growth); zones.init(W, H, zoneCrt_);

  power = us::PowerLayer(); power.init(W, H, syntheticPower(us::PowerParams::fromIni(cfg_.ini.powerLayer)));
  water = us::WaterLayer(); water.init(W, H, syntheticWater(us::WaterParams::fromIni(cfg_.ini.waterLayer)));
  us::StationParams sp = us::StationParams::police(cfg_.ini.tune), sf = us::StationParams::fire(cfg_.ini.tune);
  if (cfg_.synthetic) {
    if (sp.radius0 == 0 && sp.max0 == 0) { sp.radius0 = 30; sp.max0 = 80; sp.min0 = 20; sp.upkeep0 = 100; }
    if (sf.radius0 == 0 && sf.max0 == 0) { sf.radius0 = 25; sf.max0 = 200; sf.min0 = 200; sf.upkeep0 = 100; }
  }
  police = us::CoverageLayer(); police.init(true, W, H, sp, us::CoverageParams::police(cfg_.ini.police));
  fire = us::CoverageLayer(); fire.init(false, W, H, sf, us::CoverageParams::fire(cfg_.ini.fire));
  crime = us::CrimeLayer(); crime.init(W, H, us::CrimeParams::fromIni(cfg_.ini.crime), &usHier_, seed);
  pollution = us::PollutionLayer();
  pollution.init(W, H, us::PollutionParams::fromIni(cfg_.ini.pollution, &cfg_.ini.tune, &cfg_.ini.powerLayer), &usHier_, seed);
  aura = us::AuraLayer(); aura.init(W, H, us::AuraParams::fromIni(cfg_.ini.aura), &usHier_);
  flammability = us::FlammabilityLayer(); flammability.init(W, H, us::FlammabilityParams::fromIni(cfg_.ini.tune));
  traffic = std::make_unique<TrafficSim>(W, H, TrafficParams::fromIni(cfg_.ini.traffic), seed);

  radiation_.assign(size_t(W) * H, 0);
  plantAt_.clear(); waterFacAt_.clear(); stationAt_.clear(); preserved_.clear();
  residents_ = 0; lastTaxRate_ = -1; roadUpkeep_ = transitUpkeep_ = 0; refreshSkips_ = 0;
  growthView_ = std::make_unique<GrowthAdapter>(*this);
  lvView_ = std::make_unique<LandValueAdapter>(*this);

  rebuildIndex();
  const bool haveLv = w->raw && w->raw->findInner(keys::LandValue);
  const bool havePollution = w->raw && w->raw->findInner(kPollutionKey);
  loadRecords();
  if (!havePollution && w->seaVertex.size() == w->alt.size())   // generated terrain: sea surfaces flag their cells (spec 11 s5)
    for (int x = 0; x <= W; x++)
      for (int z = 0; z <= H; z++)
        if (w->seaVertex[size_t(w->vIdx(x, z))]) pollution.markSeaVertex(x, z);
  if (!haveLv) landValue.recomputeAll(*lvView_);
  else landValue.pinUnzonedBlocks(*lvView_);
  wire();
  refresh(true);
  // City population = valve 3000 Slast (spec 20 s2) [DATA]; a city without a completed valve cycle counts its buildings.
  residents_ = valves.v3000.Slast > 0 ? valves.v3000.Slast : countResidents();
  {  // Spec 15 s3.3 interim [OPEN, narrowed]: carry the saved 5001/8001 demand ratio (saved Dlast / building sum).
    int64_t d5 = 0, d8 = 0;
    valves.amenityBuildingDemand(valveBuildings(), d5, d8);
    auto ratio = [](int32_t saved, int64_t sum) { return saved > 0 && sum > 0 ? std::clamp(double(saved) / double(sum), 0.5, 4.0) : 1.0; };
    valves.amenityDemandMul[0] = ratio(valves.v5001.Dlast, d5);
    valves.amenityDemandMul[1] = ratio(valves.v8001.Dlast, d8);
  }
  // Full monthly costs are normally supplied by the owning subsystem at the first month end, and health and education
  // have no modelled upkeep yet (issue #44). Until then reuse the loaded city's last monthly charge so the budget
  // window's year-end estimate and the next settlement start from the saved level.
  for (auto& d : budget.depts)
    if (d.fullCost == 0 && d.lastCharge > 0 && d.funding > 0) d.fullCost = (d.lastCharge * 100 + d.funding - 1) / d.funding;
  // Business deals are not stored in the save: a deal is in force while its business building stands (spec 19 s11).
  for (const auto& d : business.defs)
    for (const auto& o : world_->occupants)
      if (o.agentType == d.buildingType) { business.inForce.push_back(d.buildingType); break; }
  if (!power.plants().empty()) {   // loaded city: coverage must be valid before the first month end
    auto consumers = simOccupants(true);
    consumers.erase(std::remove_if(consumers.begin(), consumers.end(), [](const us::SimOccupant& o) { return us::powerCategory(o.agentType) >= 0; }), consumers.end());
    power.rebuildConsumption(consumers);
    std::vector<uint8_t> conductive;
    power.recomputeCoverage(powerEnv(conductive));
  }
}

void City::wire() {
  sched.attach(clock);
  sched.disastersEnabled = false;
  sched.setHandler(Step::DailyPolice, [this] { dailyPolice(); });
  sched.setHandler(Step::DailyFire, [this] { dailyFire(); });
  sched.setHandler(Step::DailyLandValue, [this] { dailyLandValue(); });
  sched.setHandler(Step::DailyZoneDev, [this] { dailyZoneDev(); });
  sched.setHandler(Step::DailyPollution, [this] { dailyPollution(); });
  sched.setHandler(Step::DailyCrime, [this] { dailyCrime(); });
  sched.setHandler(Step::DailyFlammability, [this] { dailyFlammability(); });
  sched.setHandler(Step::DailyAura, [this] { dailyAura(); });
  sched.setHandler(Step::WeeklyStreetSim, [this] { weeklyTraffic(); });
  sched.setHandler(Step::MonthlyNeighbours, [this] { monthlyNeighbours(); });
  sched.setHandler(Step::MonthlyRci, [this] { monthlyValves(); });
  sched.setHandler(Step::MonthlyOrdinances, [this] { monthlyOrdinances(); });
  sched.setHandler(Step::MonthlyResidential, [this] { monthlyPopulation(); });
  sched.setHandler(Step::MonthlyTraffic, [this] { monthlyTraffic(); });
  sched.setHandler(Step::MonthlyBudget, [this] { monthlyBudget(); });
  sched.setHandler(Step::MonthlyPower, [this] { monthlyPower(); });
  sched.setHandler(Step::MonthlyWater, [this] { monthlyWater(); });
  sched.setHandler(Step::MonthlyPollution, [this] { monthlyPollution(); });
  sched.setHandler(Step::MonthlyCrime, [this] { monthlyCrime(); });
  sched.onDay = [this] {
    cal::Date d = clock.date();
    world_->info.year = d.year;
    world_->info.dayOfYear = cal::dayOfYear(clock.jdn());
  };
  sched.onMonth = [this] { if (onMonthEnd) { cal::Date d = clock.date(); onMonthEnd(d.month, d.year); } };
}

void City::tickDay() {
  bool monthStart = cal::fromJdn(clock.jdn() + 1).day == 1;
  refresh(monthStart);
  clock.dayTick();
}

void City::setTaxRate(int rate) {
  rate = std::max(0, std::min(kMaxTaxRate, rate));
  if (rate == lastTaxRate_) return;
  for (int g = 0; g < kTaxGroups; g++) budget.setTaxRate(g, rate);
  lastTaxRate_ = rate;
}

// ============================================================================ index / network classification

uint32_t City::kindOf(const NetPiece& p) const {
  if (!cfg_.haveTiling) return p.pieceId == kFallbackPowerPiece ? 3u : 1u;
  static const int order[] = {NK_ROAD, NK_HIGHWAY, NK_RAIL, NK_POWER};
  for (int k : order) if (cfg_.tiling.inSet(netKindToTable(k), p.pieceId)) return uint32_t(k);
  return 1u;
}

void City::classifyNetwork() {
  size_t N = size_t(world_->W) * world_->H;
  transport_.assign(N, 0); powerLine_.assign(N, 0); pipe_.assign(N, 0);
  for (const auto& p : world_->surface) {
    if (!world_->inTiles(p.x, p.z)) continue;
    uint32_t k = kindOf(p);
    if (k == 3) powerLine_[idx(p.x, p.z)] = 1;
    else if (k == 1 || k == 2 || k == 4) transport_[idx(p.x, p.z)] = uint8_t(k);
  }
  for (const auto& p : world_->pipes) if (world_->inTiles(p.x, p.z)) pipe_[idx(p.x, p.z)] = 1;
}

void City::rebuildIndex() {
  size_t N = size_t(world_->W) * world_->H;
  for (auto& o : world_->occupants) if (!o.uid) o.uid = nextUid_++;
  occ_.assign(N, -1);
  for (size_t i = 0; i < world_->occupants.size(); i++) {
    const Occupant& o = world_->occupants[i];
    for (int dx = 0; dx < o.sizeX; dx++)
      for (int dz = 0; dz < o.sizeZ; dz++)
        if (world_->inTiles(o.x + dx, o.z + dz)) occ_[idx(o.x + dx, o.z + dz)] = int32_t(i);
  }
  classifyNetwork();
  occDirty_ = netDirty_ = true;
}

int City::roadCount() const {
  int n = 0;
  for (uint8_t t : transport_) if (t) n++;
  return n;
}

bool City::flatTile(int x, int z) const {
  int a = world_->altAt(x, z), b = world_->altAt(x + 1, z), c = world_->altAt(x, z + 1), d = world_->altAt(x + 1, z + 1);
  int mx = std::max(std::max(a, b), std::max(c, d)), mn = std::min(std::min(a, b), std::min(c, d));
  return mx - mn <= 2;  // deviation: the original requires exactly flat lots
}

bool City::tileBuildable(int x, int z) const {
  if (!inMap(x, z)) return false;
  size_t i = idx(x, z);
  if (occ_[i] >= 0 || transport_[i] || powerLine_[i]) return false;
  int lo = std::min(std::min(world_->altAt(x, z), world_->altAt(x + 1, z)), std::min(world_->altAt(x, z + 1), world_->altAt(x + 1, z + 1)));
  if (lo < world_->waterAt(x, z)) return false;
  return flatTile(x, z);
}

int City::addOccupant(const Occupant& o) {
  world_->occupants.push_back(o);
  int i = int(world_->occupants.size()) - 1;
  if (!world_->occupants[size_t(i)].uid) world_->occupants[size_t(i)].uid = nextUid_++;
  for (int dx = 0; dx < o.sizeX; dx++)
    for (int dz = 0; dz < o.sizeZ; dz++)
      if (inMap(o.x + dx, o.z + dz)) occ_[idx(o.x + dx, o.z + dz)] = i;
  occDirty_ = true;
  return i;
}

void City::removeOccupantIdx(const std::set<int>& kill) {
  for (auto it = kill.rbegin(); it != kill.rend(); ++it) {
    const Occupant o = world_->occupants[size_t(*it)];
    for (int dx = 0; dx < o.sizeX; dx++)
      for (int dz = 0; dz < o.sizeZ; dz++)
        if (inMap(o.x + dx, o.z + dz) && world_->zone[idx(o.x + dx, o.z + dz)] == 22) world_->zone[idx(o.x + dx, o.z + dz)] = 0;
    world_->occupants.erase(world_->occupants.begin() + *it);
  }
  rebuildIndex();
}

// ============================================================================ player actions
namespace { PlacementCosts makeCosts(const Budget& b, const CityConfig& cfg); }


int City::zoneCost(int zt) const {
  if (budget.freePlacement) return 0;
  if (zt == 0) { int64_t c = budget.specialCost(6); return int(c > 0 ? c : 1); }
  int d = (zt >= 1 && zt <= 3) ? zt - 1 : (zt >= 5 && zt <= 7) ? zt - 5 : (zt >= 9 && zt <= 11) ? zt - 9 : 0;
  int64_t c = budget.specialCost(std::min(2, d));
  return int(c > 0 ? c : 5);
}

void City::footprint(uint32_t id, int& sx, int& sz) const {
  sx = sz = 1;
  if (!game_) return;
  if (const OccAttr* a = game_->occupant(id)) { sx = std::max(1, int(a->sizeX)); sz = std::max(1, int(a->sizeZ)); }
}

City::Quote City::quoteZone(int x0, int z0, int x1, int z1, int zt) const {
  if (x0 > x1) std::swap(x0, x1);
  if (z0 > z1) std::swap(z0, z1);
  Quote q; int cost = zoneCost(zt);
  for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++) {
      if (!inMap(x, z) || world_->zone[idx(x, z)] == zt) continue;
      if (zt && !tileBuildable(x, z)) continue;
      q.valid = true; q.cost += cost;
    }
  return q;
}

City::Quote City::quotePlop(uint32_t id, int x, int z) const {
  Quote q; int sx = 1, sz = 1; int64_t cost = 0;
  if (game_) {
    const OccAttr* a = game_->occupant(id);
    if (!a) return q;
    sx = int(a->sizeX); sz = int(a->sizeZ);
    cost = budget.placementCost(a->agentType, ecoHier_, false);
  }
  for (int dx = 0; dx < sx; dx++)
    for (int dz = 0; dz < sz; dz++)
      if (!tileBuildable(x + dx, z + dz)) return q;
  q.valid = true; q.cost = cost;
  return q;
}

City::Quote City::quoteBulldoze(int x0, int z0, int x1, int z1) const {
  if (x0 > x1) std::swap(x0, x1);
  if (z0 > z1) std::swap(z0, z1);
  Quote q; std::set<int> kill; int tiles = 0;
  for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++) {
      if (!inMap(x, z)) continue;
      size_t i = idx(x, z);
      if (occ_[i] >= 0) kill.insert(occ_[i]);
      if (transport_[i] || powerLine_[i]) tiles++;
      if (world_->flora[i]) q.valid = true;
    }
  if (!kill.empty() || tiles) { q.valid = true; q.cost = int64_t(kill.size()) + tiles; }
  return q;
}

City::Quote City::quoteNetwork(NetworkKind k, const std::vector<std::pair<int, int>>& path) const {
  Quote q;
  Tool tool = k == NetworkKind::Road ? TOOL_ROAD : k == NetworkKind::Rail ? TOOL_RAIL : k == NetworkKind::Power ? TOOL_POWER
            : k == NetworkKind::Pipe ? TOOL_PIPE : k == NetworkKind::Highway ? TOOL_HIGHWAY : TOOL_SUBWAY;
  PlacementCosts pc = makeCosts(budget, cfg_);
  int cost = pc.cost(toolParams(tool).costKey);
  if (cost == 0 && budget.cfg.placementCost.empty() && !budget.freePlacement) cost = 10;
  for (auto [x, z] : path) {
    if (!inMap(x, z)) continue;
    size_t i = idx(x, z);
    if (k == NetworkKind::Pipe ? pipe_[i] != 0 : k == NetworkKind::Power ? powerLine_[i] != 0 : transport_[i] != 0) continue;
    if (k != NetworkKind::Pipe && k != NetworkKind::Subway && occ_[i] >= 0) continue;
    int lo = std::min(std::min(world_->altAt(x, z), world_->altAt(x + 1, z)), std::min(world_->altAt(x, z + 1), world_->altAt(x + 1, z + 1)));
    bool water = lo < world_->waterAt(x, z);
    if (water && cfg_.haveTiling == false) continue;
    q.valid = true; q.cost += cost;
  }
  return q;
}

bool City::paintZoneType(int x0, int z0, int x1, int z1, int zt) {
  lastFailNoMoney = false;
  if (x0 > x1) std::swap(x0, x1);
  if (z0 > z1) std::swap(z0, z1);
  bool any = false;
  int cost = zoneCost(zt);
  for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++) {
      if (!inMap(x, z)) continue;
      size_t i = idx(x, z);
      if (world_->zone[i] == zt) continue;
      if (zt && !tileBuildable(x, z)) continue;
      if (budget.funds < cost) { lastFailNoMoney = true; return any; }
      budget.funds -= cost;
      world_->zone[i] = uint8_t(zt);
      any = true;
    }
  return any;
}

bool City::plop(uint32_t id, int x, int z, int cost) {
  int sx = 1, sz = 1;
  uint32_t agent = 0;
  if (game_) {
    const OccAttr* a = game_->occupant(id);
    if (!a) return false;
    sx = int(a->sizeX); sz = int(a->sizeZ); agent = a->agentType;
    int64_t c = budget.placementCost(agent, ecoHier_, false);
    if (c > 0) cost = int(c);
  }
  return plopAgent(id, agent, sx, sz, x, z, cost);
}

bool City::plopAgent(uint32_t id, uint32_t agent, int sx, int sz, int x, int z, int cost) {
  lastFailNoMoney = false;
  for (int dx = 0; dx < sx; dx++)
    for (int dz = 0; dz < sz; dz++)
      if (!tileBuildable(x + dx, z + dz)) return false;
  if (budget.funds < cost) { lastFailNoMoney = true; return false; }
  budget.funds -= cost;
  Occupant o;
  o.x = x; o.z = z; o.y = world_->altAt(x, z); o.sizeX = sx; o.sizeZ = sz; o.occId = id;
  o.keyType = T_OCC_ATTR; o.agentType = agent; o.state = 1;
  addOccupant(o);
  zones.buildingAdded(agent, sx, sz, 1);
  syncFacilities();
  {  // spec 10 6.2.5: observed per-class values for a new building
    Occupant& placed = world_->occupants.back();
    int lmax = 0, ld = 0;
    auto pit = plantAt_.find({x, z});
    if (pit != plantAt_.end())
      for (const auto& p : power.plants()) if (p.id == pit->second) { lmax = p.lmax; ld = p.ld; }
    cal::Date d = clock.date();
    applyNewBuildingDefaults(placed, d.year, cal::dayOfYear(clock.jdn()), lmax, ld);
  }
  return true;
}

void City::setFallbackRoadPiece(int x, int z) {
  int mask = 0;
  if (isRoadTile(x - 1, z)) mask |= 1;
  if (isRoadTile(x, z - 1)) mask |= 2;
  if (isRoadTile(x + 1, z)) mask |= 4;
  if (isRoadTile(x, z + 1)) mask |= 8;
  uint32_t packed;
  auto it = cfg_.roadFinal.find(mask);
  if (it != cfg_.roadFinal.end()) packed = it->second;
  else packed = (mask == 5 ? (29u << 8) | 1 : mask == 10 ? (29u << 8) : (43u << 8));
  NetPiece* p = nullptr;
  for (auto& q : world_->surface) if (q.x == x && q.z == z && q.pieceId != kFallbackPowerPiece) { p = &q; break; }
  if (!p) { world_->surface.push_back({}); p = &world_->surface.back(); }
  p->x = x; p->z = z; p->y = world_->altAt(x, z);
  p->pieceId = packed >> 8; p->rot = int(packed & 3); p->pieceWord = 0x30000000u | p->pieceId;
}

namespace {
PlacementCosts makeCosts(const Budget& b, const CityConfig& cfg) {
  PlacementCosts pc;
  for (const auto& kv : b.cfg.placementCost) pc.byType[kv.first] = int(kv.second);
  pc.parent = cfg.parent;
  pc.demolish = int(b.cfg.specialCostFor(7));
  pc.demolishRubble = int(b.cfg.specialCostFor(12));
  if (b.freePlacement) { pc.byType.clear(); pc.demolish = pc.demolishRubble = 0; return pc; }
  if (cfg.synthetic) {
    static const std::pair<Tool, int> defs[] = {{TOOL_ROAD, 10}, {TOOL_RAIL, 20}, {TOOL_POWER, 5}, {TOOL_PIPE, 3},
                                                {TOOL_HIGHWAY, 50}, {TOOL_SUBWAY, 30}};
    for (const auto& d : defs) {
      uint32_t key = toolParams(d.first).costKey;
      if (pc.cost(key) == 0) pc.byType[key] = d.second;
    }
  }
  return pc;
}
}  // namespace

bool City::buildNetworkFallback(NetworkKind k, const std::vector<std::pair<int, int>>& path) {
  PlacementCosts pc = makeCosts(budget, cfg_);
  Tool tool = k == NetworkKind::Power ? TOOL_POWER : k == NetworkKind::Pipe ? TOOL_PIPE : TOOL_ROAD;
  int cost = pc.cost(toolParams(tool).costKey);
  if (cost == 0 && budget.cfg.placementCost.empty() && !budget.freePlacement) cost = 10;
  std::vector<std::pair<int, int>> placed;
  for (auto [x, z] : path) {
    if (!inMap(x, z)) continue;
    size_t i = idx(x, z);
    if (occ_[i] >= 0) continue;
    int lo = std::min(std::min(world_->altAt(x, z), world_->altAt(x + 1, z)), std::min(world_->altAt(x, z + 1), world_->altAt(x + 1, z + 1)));
    if (lo < world_->waterAt(x, z) || !flatTile(x, z)) continue;
    if (k == NetworkKind::Pipe) {
      if (pipe_[i]) continue;
      if (budget.funds < cost) { lastFailNoMoney = true; break; }
      budget.funds -= cost;
      NetPiece p; p.x = x; p.z = z; p.y = world_->altAt(x, z); p.pieceId = kFallbackPipePiece; p.pieceWord = 0x30000000u | p.pieceId;
      world_->pipes.push_back(p); pipe_[i] = 1; placed.push_back({x, z});
      continue;
    }
    // Power lines may cross roads (and roads cross lines): both pieces share the tile.
    if (k == NetworkKind::Power ? powerLine_[i] != 0 : transport_[i] != 0) continue;
    if (budget.funds < cost) { lastFailNoMoney = true; break; }
    budget.funds -= cost;
    if (k == NetworkKind::Power) {
      NetPiece p; p.x = x; p.z = z; p.y = world_->altAt(x, z); p.pieceId = kFallbackPowerPiece; p.pieceWord = 0x30000000u | p.pieceId;
      world_->surface.push_back(p); powerLine_[i] = 1;
    } else {
      transport_[i] = 1;
      world_->surface.push_back({});  // filled by setFallbackRoadPiece
      NetPiece& p = world_->surface.back(); p.x = x; p.z = z;
    }
    placed.push_back({x, z});
  }
  if (placed.empty()) return false;
  if (k == NetworkKind::Road) {
    for (auto [x, z] : placed) setFallbackRoadPiece(x, z);
    const int d[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (auto [x, z] : placed)
      for (auto& dd : d) if (isRoadTile(x + dd[0], z + dd[1])) setFallbackRoadPiece(x + dd[0], z + dd[1]);
  }
  netDirty_ = true;
  return true;
}

bool City::buildNetwork(NetworkKind k, const std::vector<std::pair<int, int>>& path) {
  lastFailNoMoney = false;
  if (path.empty()) return false;
  if (!cfg_.haveTiling) return buildNetworkFallback(k, path);
  Tool tool = k == NetworkKind::Road ? TOOL_ROAD : k == NetworkKind::Rail ? TOOL_RAIL : k == NetworkKind::Power ? TOOL_POWER
            : k == NetworkKind::Pipe ? TOOL_PIPE : k == NetworkKind::Highway ? TOOL_HIGHWAY : TOOL_SUBWAY;
  WorldNetworkMap map(*world_, flagsFromRuleSets(cfg_.tiling));
  PlacementCosts costs = makeCosts(budget, cfg_);
  struct Host : ToolHost {
    City& c;
    explicit Host(City& cc) : c(cc) {}
    long funds() const override { return long(c.budget.funds); }
    void charge(int a) override { c.budget.funds -= a; }
    void sound(int id) override { if (c.onSound) c.onSound(id); }
    void message(const std::string& m) override { if (c.onMessage) c.onMessage(m); }
  } host(*this);
  // Split into straight runs.
  std::vector<std::pair<std::pair<int, int>, std::pair<int, int>>> segs;
  size_t s = 0;
  for (size_t i = 2; i < path.size(); i++) {
    int dx0 = path[i - 1].first - path[i - 2].first, dz0 = path[i - 1].second - path[i - 2].second;
    int dx1 = path[i].first - path[i - 1].first, dz1 = path[i].second - path[i - 1].second;
    if (dx0 != dx1 || dz0 != dz1) { segs.push_back({path[s], path[i - 1]}); s = i - 1; }
  }
  segs.push_back({path[s], path.back()});
  bool any = false;
  for (auto& sg : segs) {
    RoutedPath rp = routePath(map, tool, sg.first, sg.second);
    CommitResult r = commitPath(map, cfg_.tiling, costs, host, tool, rp);
    if (r.status == COMMIT_OK) any = true;
  }
  if (any) rebuildIndex();
  return any;
}

bool City::bulldoze(int x0, int z0, int x1, int z1) {
  if (x0 > x1) std::swap(x0, x1);
  if (z0 > z1) std::swap(z0, z1);
  bool any = false;
  std::set<int> kill;
  std::map<uint32_t, std::vector<std::pair<int, int>>> netTiles;   // NetKind -> tiles
  int count = 0;
  for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++) {
      if (!inMap(x, z)) continue;
      size_t i = idx(x, z);
      if (occ_[i] >= 0) kill.insert(occ_[i]);
      if (transport_[i]) netTiles[transport_[i]].push_back({x, z});
      if (powerLine_[i]) netTiles[3].push_back({x, z});
      if (world_->flora[i]) { world_->flora[i] = 0; any = true; }
    }
  for (auto& kv : netTiles) count += int(kv.second.size());
  if (kill.empty() && netTiles.empty()) return any;
  budget.funds -= int64_t(kill.size()) + count;
  for (int oi : kill) {
    const Occupant& o = world_->occupants[size_t(oi)];
    zones.buildingRemoved(o.agentType, o.sizeX, o.sizeZ, o.state);
  }
  if (!netTiles.empty()) {
    if (cfg_.haveTiling) {
      WorldNetworkMap map(*world_, flagsFromRuleSets(cfg_.tiling));
      for (auto& kv : netTiles) demolish(map, cfg_.tiling, int(kv.first), kv.second);
    } else {
      std::set<std::pair<int, int>> rm;
      for (auto& kv : netTiles) for (auto& t : kv.second) rm.insert(t);
      auto& v = world_->surface;
      v.erase(std::remove_if(v.begin(), v.end(), [&](const NetPiece& p) { return rm.count({p.x, p.z}) != 0; }), v.end());
    }
  }
  if (!kill.empty()) removeOccupantIdx(kill); else rebuildIndex();
  if (!cfg_.haveTiling) {
    auto it = netTiles.find(1);
    if (it != netTiles.end()) {
      const int d[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
      for (auto [x, z] : it->second)
        for (auto& dd : d) if (isRoadTile(x + dd[0], z + dd[1])) setFallbackRoadPiece(x + dd[0], z + dd[1]);
    }
  }
  syncFacilities();
  return true;
}

// ============================================================================ derived layer state

std::vector<us::SimOccupant> City::simOccupants(bool operatingOnly) const {
  std::vector<us::SimOccupant> v;
  for (const auto& o : world_->occupants) {
    if (operatingOnly && (o.state & ~0x200u) != 1) continue;
    us::SimOccupant s;
    s.id = o.occId; s.agentType = o.agentType; s.group = o.agentType;
    s.x = o.x; s.z = o.z; s.sizeX = o.sizeX; s.sizeZ = o.sizeZ;
    s.hasState = true; s.state = o.state;
    if (game_) if (const OccAttr* a = game_->occupant(o.occId)) if (a->powerAttr >= 0) s.powerUse = a->powerAttr;   // [OBSERVED] attribute 0xC9 is the power use (Σ over a shipped city = 96% of its saved production table)
    v.push_back(s);
  }
  return v;
}

us::OrdinanceSet City::usOrdinances() const {
  us::OrdinanceSet s;
  for (const auto& kv : ordinances.catalogue()) if (ordinances.enacted(kv.first)) s.enacted.insert(kv.first);
  return s;
}

void City::syncFacilities() {
  const int now = absMonthOf(clock);
  std::set<std::pair<int, int>> plants, wf, st;
  for (const auto& o : world_->occupants) {
    std::pair<int, int> key{o.x, o.z};
    if ((o.state & ~0x200u) != 1) continue;
    if (us::powerCategory(o.agentType) >= 0) {
      plants.insert(key);
      if (!plantAt_.count(key)) { int id = power.addPlant(o.x, o.z, o.agentType, now, urng_); if (id >= 0) plantAt_[key] = id; }
    } else if (o.agentType == kAgentPump || o.agentType == kAgentTower || o.agentType == kAgentDesal) {
      wf.insert(key);
      if (!waterFacAt_.count(key)) {
        us::WaterKind kind = o.agentType == kAgentPump ? us::WaterKind::Pump : o.agentType == kAgentTower ? us::WaterKind::Tower : us::WaterKind::Desalination;
        // Build date (year, day of year) from the occupant's date pair when the save has one; otherwise it is new.
        int built = now;
        if (o.hasDate && o.dateYear > 0) built = 12 * o.dateYear + std::max(0, std::min(11, (std::max(1, o.dateDay) - 1) * 12 / 366)) + 1;
        waterFacAt_[key] = water.addFacility(kind, o.x, o.z, std::min(built, now));
      }
    } else if (usHier_.isAncestorOrSelf(o.agentType, kAgentPolice) || usHier_.isAncestorOrSelf(o.agentType, kAgentFire) ||
               o.agentType == kAgentJail) {
      st.insert(key);
      if (!stationAt_.count(key)) {
        if (o.agentType == kAgentJail) stationAt_[key] = {2, police.addJail(jailCapacity())};
        else if (usHier_.isAncestorOrSelf(o.agentType, kAgentPolice)) stationAt_[key] = {0, police.addStation(o.x, o.z, o.x + o.sizeX - 1, o.z + o.sizeZ - 1)};
        else stationAt_[key] = {1, fire.addStation(o.x, o.z, o.x + o.sizeX - 1, o.z + o.sizeZ - 1)};
      }
    }
  }
  for (auto it = plantAt_.begin(); it != plantAt_.end();) {
    if (!plants.count(it->first)) { power.removePlant(it->second); it = plantAt_.erase(it); } else ++it;
  }
  for (auto it = waterFacAt_.begin(); it != waterFacAt_.end();) {
    if (!wf.count(it->first)) { water.removeFacility(it->second); it = waterFacAt_.erase(it); } else ++it;
  }
  for (auto it = stationAt_.begin(); it != stationAt_.end();) {
    if (!st.count(it->first)) {
      auto [kind, id] = it->second;
      if (kind == 0) police.removeStation(id); else if (kind == 1) fire.removeStation(id); else police.removeJail(id);
      it = stationAt_.erase(it);
    } else ++it;
  }
}

void City::rebuildTraffic() {
  if (!traffic) return;
  for (int x = 0; x < world_->W; x++)
    for (int z = 0; z < world_->H; z++) {
      uint8_t k = transport_[idx(x, z)];
      traffic->flags(x, z) = k == 1 ? TF_ROAD : k == 4 ? TF_HIGHWAY : k == 2 ? TF_RAIL : 0;
    }
  tripBuildings_.clear();
  int i = 0;
  for (const auto& o : world_->occupants) {
    TripBuilding b;
    b.x = o.x; b.z = o.z; b.w = o.sizeX; b.h = o.sizeZ;
    uint32_t k = o.agentType / 1000;
    b.category = k == 3 ? CAT_RES : k == 5 ? CAT_COM : k == 8 ? CAT_IND : -1;
    b.slot = (i++) % 5;
    b.functional = (o.state & ~0x200u) == 1;
    if (b.makesTrips()) tripBuildings_.push_back(b);
  }
  traffic->rebuildDestinations(tripBuildings_);
}

void City::refresh(bool force) {
  if (!netDirty_ && !occDirty_) return;
  if (!force && !netDirty_ && ++refreshSkips_ < 7) return;
  refreshSkips_ = 0;
  if (netDirty_) classifyNetwork();
  auto all = simOccupants(false);
  crime.rebuild(all);
  pollution.rebuild(all);
  aura.rebuild(all);
  syncFacilities();
  rebuildTraffic();
  netDirty_ = occDirty_ = false;
}

// ============================================================================ daily handlers

void City::dailyPolice() {
  us::CoverageTickEnv env;
  env.crimeAt = [this](int x, int z) { return crime.crimeAt(x, z); };
  env.occupied = [this](int x, int z) { return occupantAt(x, z) >= 0; };
  env.averageMetric = crime.avgCrime;
  police.tick(env, urng_);
}
void City::dailyFire() {
  us::CoverageTickEnv env;
  env.occupied = [this](int x, int z) { return occupantAt(x, z) >= 0; };
  env.averageMetric = int(flammability.averageFlammability());
  fire.tick(env, urng_);
}
int City::trafficDensityAt(int x, int z) const {
  if (!traffic || !inMap(x, z)) return 0;
  return std::max({int(traffic->liveSurface(x, z)), int(traffic->liveHighway(x, z)), int(traffic->liveSubway(x, z))});
}
int City::trafficDensityAverage() const {
  if (!traffic || !world_) return 0;
  int64_t sum = 0, n = 0;
  for (int x = 0; x < world_->W; x++) for (int z = 0; z < world_->H; z++) { int d = trafficDensityAt(x, z); if (d) { sum += d; n++; } }
  return n ? int(sum / n) : 0;
}
int City::jailCapacity() const {
  us::IniReader t{&cfg_.ini.tune};
  int c = int(t.num("Jail", "InmateCapacity", 0));
  return c > 0 ? c : 100;
}
void City::recomputeLandValue() { landValue.recomputeAll(*lvView_); }
void City::lvCentre(int& cx, int& cz) const { lvView_->cityCentre(cx, cz); }
std::string City::lvExplain(int x, int z) const { return landValue.explainBlock(*lvView_, x, z); }
void City::dailyLandValue() { landValue.update(*lvView_); }
void City::dailyZoneDev() { zones.tick(*growthView_, zoneRng_, zoneCrt_); }
void City::dailyPollution() {
  us::PollutionEnv env;
  env.zone = &world_->zone;
  env.ordinances = usOrdinances();
  env.exportDeal = neighbours.dealOfType(4);   // garbage export deal: the whole remainder leaves the city
  for (const auto& d : neighbours.active)      // garbage import deal (the player is paid): enters the pipeline first
    if (!d.terminated() && d.dealType == 5) env.importMonthly += d.quantity;
  pollution.run(env);
}
void City::dailyCrime() {
  crime.run([this](int x, int z) { return police.coverageAt(x, z); }, usOrdinances());
}
void City::dailyFlammability() {
  flammability.run([this](int x, int z) { return occupantAt(x, z) >= 0 ? 30 : 0; },
                   [this](int x, int z) { return !water.tileServiced(x, z); }, usOrdinances());
}
void City::dailyAura() {
  us::AuraEnv env;
  env.policeAt = [this](int x, int z) { return police.coverageAt(x, z); };
  env.fireAt = [this](int x, int z) { return fire.coverageAt(x, z); };
  env.crimeAt = [this](int x, int z) { return crime.crimeAt(x, z); };
  env.airAt = [this](int x, int z) { return pollution.airAt(x, z); };
  env.waterAt = [this](int x, int z) { return pollution.waterAt(x, z); };
  env.garbageAt = [this](int x, int z) { return pollution.standingGarbageAt(x, z); };
  env.eq = pop.cityEQ();
  env.lifeExpectancy = pop.cityLifeExpectancy();
  env.ordinances = usOrdinances();
  aura.run(env);
}
void City::weeklyTraffic() {
  if (traffic) traffic->runWeekly(clock.date().day, tripBuildings_);
}

// ============================================================================ monthly handlers

std::string City::debugStats() const {
  std::ostringstream o;
  auto V = [&](const char* n, int id) {
    const growth::Valve& v = valves.valve(id);
    o << n << "(V=" << v.V << " S=" << v.Slast << " D=" << v.Dlast << " P=" << zones.committed(id) << ") ";
  };
  o << "valves: "; V("R", 3000); V("C", 5000); V("I", 8000); V("jobs", 7000); o << "\n";
  int cnt[3][5] = {};
  int poweredN = 0;
  for (const auto& oc : world_->occupants) {
    uint32_t k = oc.agentType / 1000;
    int c = k == 3 ? 0 : k == 5 ? 1 : k == 8 ? 2 : -1;
    if (c < 0) continue;
    int st = (oc.state & ~0x200u) == 1 ? 0 : (oc.state & ~0x200u) == 2 ? 1 : (oc.state & ~0x200u) == 4 ? 2 : 3;
    cnt[c][st]++;
    if (power.powered(oc.x, oc.z)) poweredN++;
  }
  const char* nm[3] = {"R", "C", "I"};
  for (int c = 0; c < 3; c++)
    o << nm[c] << " bld: occ=" << cnt[c][0] << " aband=" << cnt[c][1] << " constr=" << cnt[c][2] << " other=" << cnt[c][3] << "  ";
  o << "poweredBld=" << poweredN << " residents=" << residents_ << " funds=" << budget.funds
    << " powerProduced=" << power.stats().produced << "\n";
  o << "zone results(prev pass): ";
  for (const auto& kv : zones.resultPrev) o << int(kv.first.first) << ":" << int(kv.first.second) << "=" << kv.second << " ";
  o << "\ncur: ";
  for (const auto& kv : zones.resultCur) o << int(kv.first.first) << ":" << int(kv.first.second) << "=" << kv.second << " ";
  o << "\n";
  return o.str();
}

void City::monthlyNeighbours() { neighbours.monthly(clock.date().month, rng_); }

int City::countResidents() const {
  int residents = 0;
  for (const auto& o : world_->occupants) {
    if ((o.state & ~0x200u) != 1) continue;
    if (const auto* items = cfg_.valve.effectsFor(o.agentType))
      for (const auto& vi : *items) if (vi.valve == growth::kValveRes) residents += vi.supply * o.sizeX * o.sizeZ;
  }
  return residents;
}

std::vector<growth::ValveBuilding> City::valveBuildings() const {
  std::vector<growth::ValveBuilding> out;
  out.reserve(world_->occupants.size());
  for (const auto& o : world_->occupants) {
    growth::ValveBuilding b;
    b.agentType = o.agentType; b.x = o.x; b.z = o.z; b.sizeX = o.sizeX; b.sizeZ = o.sizeZ;
    b.operating = (o.state & ~0x200u) == 1;   // states 1 and 513 count (spec 15 s3.3) [STATIC]
    out.push_back(b);
  }
  return out;
}

void City::monthlyValves() {
  growth::ValveCityInput in;
  in.buildings = valveBuildings();
  in.connectionCount[0] = int(neighbours.conn[1].size());   // saved order: rail, road, pipe, power, highway, subway
  in.connectionCount[1] = int(neighbours.conn[0].size());
  in.connectionCount[2] = int(neighbours.conn[4].size());
  in.connectionCount[3] = int(neighbours.conn[5].size());
  for (const auto& kv : ordinances.catalogue()) if (ordinances.enacted(kv.first)) in.enactedOrdinances.insert(kv.first);
  in.workforceFraction = pop.workforceFraction();
  in.population = residents_;
  cal::Date d = clock.date();
  in.year = d.year; in.month = d.month;
  for (int g = 0; g < kTaxGroups; g++) in.taxRate[g] = budget.tax[g].rate;
  valves.monthlyUpdate(in);
  // City population = valve 3000 Slast (spec 20 s2) [DATA 13/13: equals the newest population-chart sample].
  residents_ = valves.v3000.Slast;
  valves.worldMonthly(in.workforceFraction, d.month, d.year);
}

void City::monthlyOrdinances() {
  AvailabilityInput in;
  in.currentYear = clock.date().year;
  for (const auto& o : world_->occupants) {
    uint32_t k = o.agentType / 1000;
    if (k == 3) in.residentialBuildings++; else if (k == 5) in.commercialBuildings++; else if (k == 8) in.industrialBuildings++;
  }
  ordinances.availabilityScan(rng_, in);
}

void City::monthlyPopulation() {
  PopulationInput in;
  cal::Date d = clock.date();
  in.population = uint32_t(std::max(0, residents_));
  in.month = d.month; in.year = d.year;
  in.pollution1 = pollution.avgAir; in.pollution2 = pollution.avgWater;
  if (const Department* e = budget.dept(dept::Education)) in.educationFunding = e->funding;
  if (const Department* h = budget.dept(dept::Health)) in.healthFunding = h->funding;
  in.ordinanceEnacted = [this](uint32_t id) { return ordinances.enacted(id); };
  pop.monthly(in);
}

void City::monthlyTraffic() {
  if (!traffic) return;
  int roads = 0, hwy = 0, rail = 0;
  for (uint8_t t : transport_) { if (t == 1) roads++; else if (t == 4) hwy++; else if (t == 2) rail++; }
  MonthlyInput in;
  in.ord.parkingFines = ordinances.enacted(kOrdParkingFines);
  in.ord.alternateDayDriving = ordinances.enacted(kOrdAlternateDay);
  in.ord.carpool = ordinances.enacted(kOrdCarpool);
  in.ord.crossingGuards = ordinances.enacted(kOrdCrossingGuards);
  in.ord.touristPromotion = ordinances.enacted(kOrdTourist);
  in.ord.subsidizedTransit = ordinances.enacted(kOrdSubsidizedTransit);
  in.roadTiles = roads; in.highwayTiles = hwy; in.railTiles = rail;
  {  // transit upkeep (spec 18 s8): station buildings' OptimalMonthlyUpkeep plus subway tiles (saved Transit charges: Berlin 1,351)
    std::set<std::pair<int, int>> subTiles;
    for (const auto& p : world_->subway) subTiles.insert({p.x, p.z});
    in.subwayTiles = int(subTiles.size());
    us::IniReader t{&cfg_.ini.tune};
    const int64_t bus = t.num("BusStop", "OptimalMonthlyUpkeep", 0), railStn = t.num("RailStation", "OptimalMonthlyUpkeep", 0),
                  subRail = t.num("SubwayRailStation", "OptimalMonthlyUpkeep", 0), subStn = t.num("SubwayStation", "OptimalMonthlyUpkeep", 0);
    int64_t up = 0;
    for (const auto& o : world_->occupants) {
      if ((o.state & ~0x200u) != 1) continue;
      switch (o.agentType) { case 12210: up += bus; break; case 12230: up += railStn; break; case 12361: up += subRail; break; case 12360: up += subStn; break; default: break; }
    }
    in.stationUpkeep = int(up);
  }
  cal::Date d = clock.date();
  in.year = d.year; in.month = d.month;
  if (const Department* r = budget.dept(dept::Roads)) traffic->setRoadFunding(r->funding);
  if (const Department* t = budget.dept(dept::Transit)) traffic->setTransitFunding(t->funding);
  MonthlyOutput out = traffic->monthly(in);
  roadUpkeep_ = out.roadUpkeep; transitUpkeep_ = out.transitUpkeep;
}

void City::monthlyBudget() {
  budget.setDeptFullCost(dept::Roads, roadUpkeep_);
  budget.setDeptFullCost(dept::Transit, transitUpkeep_);
  {  // police department cost = station upkeep + a fixed monthly upkeep per jail (SC3Tune [Jail] MonthlyUpkeep = 75; matches the saved
     // charges: Berlin 12 jails, charge 1,483 vs station upkeep 540)
    int jails = 0;
    for (const auto& kv : stationAt_) if (kv.second.first == 2) jails++;
    us::IniReader t{&cfg_.ini.tune};
    budget.setDeptFullCost(dept::Police, int64_t(police.upkeepTotal) + int64_t(jails) * t.num("Jail", "MonthlyUpkeep", 0));
  }
  budget.setDeptFullCost(dept::Fire, fire.upkeepTotal);
  if (const Department* p = budget.dept(dept::Police)) if (p->funding != police.funding()) police.setFunding(p->funding);
  if (const Department* f = budget.dept(dept::Fire)) if (f->funding != fire.funding()) fire.setFunding(f->funding);
  MonthEndInput in;
  cal::Date d = clock.date();
  in.month = d.month; in.year = d.year;
  in.population = uint32_t(std::max(0, residents_));
  in.avgLV = cfg_.synthetic ? 128 : landValue.cityAverage();
  in.zoneTotals[0] = int32_t(valves.resUnits); in.zoneTotals[1] = int32_t(valves.comUnits); in.zoneTotals[2] = int32_t(valves.indUnits);
  OrdinanceMoneyInput om;
  om.population = in.population;
  Ordinances::Money om2 = ordinances.monthlyMoney(om);
  in.ordIncome = om2.income; in.ordCost = om2.cost;
  NeighbourDeals::Money dm = neighbours.monthlyMoney();
  in.dealIncome = dm.income; in.dealCost = dm.cost;
  in.bizIncome = business.monthlyIncome();
  in.fares = traffic ? double(traffic->rec().faresLastMonth) : 0.0;
  budget.monthEnd(in);
}

us::PowerEnv City::powerEnv(std::vector<uint8_t>& conductive) const {
  // spec 12 section 9.1: every building footprint and power-line piece conducts to anything within 2 tiles.
  const int Wt = world_->W, Ht = world_->H;
  std::vector<uint8_t> src(size_t(Wt) * Ht, 0);
  for (size_t i = 0; i < src.size(); i++) src[i] = powerLine_[i] || occ_[i] >= 0;
  std::vector<uint8_t> dx(src.size(), 0);
  conductive.assign(src.size(), 0);
  for (int x = 0; x < Wt; x++)
    for (int z = 0; z < Ht; z++)
      if (src[idx(x, z)])
        for (int d = std::max(0, x - 2); d <= std::min(Wt - 1, x + 2); d++) dx[idx(d, z)] = 1;
  for (int x = 0; x < Wt; x++)
    for (int z = 0; z < Ht; z++)
      if (dx[idx(x, z)])
        for (int d = std::max(0, z - 2); d <= std::min(Ht - 1, z + 2); d++) conductive[idx(x, d)] = 1;
  us::PowerEnv env;
  env.conductive = &conductive;
  env.ordinances = usOrdinances();
  env.nowAbsMonth = absMonthOf(clock);
  // Neighbour power deals (spec 16 s2.6 step 2) [STATIC]: one slot per neighbour; a buy deal (kind 0) gives its slot
  // 1,000,000,000 (unlimited) and is seeded at a power-line connection tile to that neighbour (step 6).
  // [OPEN on our side] sell deals (kind 1) are not modelled by the power layer yet.
  for (const auto& d : neighbours.active) {
    if (d.terminated() || d.dealType != 0 || env.deals.size() >= 4) continue;
    us::PowerDeal pd; pd.amount = us::kPowerBuySlot;
    connectionTile(3, d.neighbourIndex, pd.tx, pd.tz);
    env.deals.push_back(pd);
  }
  return env;
}

bool City::connectionTile(int setIndex, int neighbour, int& tx, int& tz) const {
  tx = tz = -1;
  if (setIndex < 0 || setIndex >= 6) return false;
  for (uint32_t p : neighbours.conn[setIndex]) {
    int x = int(p >> 16), z = int(p & 0xFFFF);
    if (NeighbourDeals::edgeNeighbour(x, z, world_->W, world_->H) == neighbour) { tx = x; tz = z; return true; }
  }
  return false;
}

void City::monthlyPower() {
  syncFacilities();
  auto consumers = simOccupants(true);
  consumers.erase(std::remove_if(consumers.begin(), consumers.end(), [](const us::SimOccupant& o) { return us::powerCategory(o.agentType) >= 0; }), consumers.end());
  power.rebuildConsumption(consumers);
  std::vector<uint8_t> conductive;
  us::PowerEnv env = powerEnv(conductive);
  auto events = power.monthlyUpdate(env, urng_);
  {  // Reported draw (spec 16 s2.7 step 9): the buy deal's units follow what was drawn from neighbours (spec 19 s10.4).
    int64_t drawn = 0;
    for (const auto& d : power.deals()) if (d.active && d.amount > 0) drawn += std::max<int64_t>(0, d.amount - d.unused);
    for (auto& d : neighbours.active)
      if (!d.terminated() && d.dealType == 0) { d.quantity = int32_t(std::min<int64_t>(drawn, INT32_MAX)); break; }
  }
  for (const auto& e : events) {
    if (e.kind != us::PlantEvent::Explosion) continue;
    int oi = occupantAt(e.x, e.z);
    if (oi >= 0) { world_->occupants[size_t(oi)].state = 8; occDirty_ = true; }
    plantAt_.erase({e.x, e.z});
    if (onMessage) onMessage("A power plant exploded from overwork!");
  }
}

void City::monthlyWater() {
  std::vector<uint8_t> pipes(pipe_);
  for (const auto& f : water.facilities()) {
    int oi = occupantAt(f.x, f.z);
    if (oi < 0) { pipes[idx(f.x, f.z)] = 1; continue; }
    const Occupant& o = world_->occupants[size_t(oi)];
    for (int dx = 0; dx < o.sizeX; dx++)
      for (int dz = 0; dz < o.sizeZ; dz++)
        if (inMap(o.x + dx, o.z + dz)) pipes[idx(o.x + dx, o.z + dz)] = 1;
  }
  // Water tiles for the pump ring test (spec 16 s3.3): a tile is water when any of its four corners is ([OPEN] any or all
  // corners; spec 11 "some corners under water" for shoreline tiles). Salt = the stored sea-water flag of the tile's
  // pollution cell (bit 30), not the shape of the water body [STATIC]; the `Salt off` cheat ignores it. 1 = fresh, 2 = salt.
  std::vector<uint8_t> kind(size_t(world_->W) * world_->H, 0);
  for (int x = 0; x < world_->W; x++)
    for (int z = 0; z < world_->H; z++) {
      bool wet = false;
      for (int dx = 0; dx <= 1 && !wet; dx++)
        for (int dz = 0; dz <= 1; dz++)
          if (world_->waterAt(x + dx, z + dz) >= world_->altAt(x + dx, z + dz)) { wet = true; break; }
      if (wet) kind[idx(x, z)] = (!saltOff && pollution.seaFlagAt(x, z)) ? 2 : 1;
    }
  auto consumers = simOccupants(true);
  us::WaterEnv env;
  env.nowAbsMonth = absMonthOf(clock);
  env.pipes = &pipes;
  env.waterKind = &kind;
  env.power = &power;
  env.waterPollutionAt = [this](int x, int z) { return pollution.waterAt(x, z); };
  env.ordinances = usOrdinances();
  env.consumers = &consumers;
  // Neighbour water deals (kinds 2 buy, 3 sell; spec 16 s3.4 step 4) at a pipe connection tile to that neighbour.
  for (const auto& d : neighbours.active) {
    if (d.terminated() || (d.dealType != 2 && d.dealType != 3)) continue;
    us::WaterDeal wd; wd.purchase = d.dealType == 2; wd.amount = d.quantity;
    if (connectionTile(2, d.neighbourIndex, wd.tx, wd.tz)) env.deals.push_back(wd);
  }
  water.monthlyUpdate(env);
}

void City::monthlyPollution() {
  cal::Date d = clock.date();
  // Recycling centres' monthly values (spec 16 s4.3): working = occupied, powered and the lot's last trip not failed;
  // age from the build date.
  std::vector<us::PollutionLayer::RecyclingCentre> centres;
  const int now = absMonthOf(clock);
  for (const auto& o : world_->occupants) {
    if (o.agentType != kAgentRecycling) continue;
    int built = now;
    if (o.hasDate && o.dateYear > 0) built = 12 * o.dateYear + std::max(0, std::min(11, (std::max(1, o.dateDay) - 1) * 12 / 366)) + 1;
    bool working = (o.state & ~0x200u) == 1 && power.powered(o.x, o.z) && !(traffic && traffic->tripResult(o.x, o.z) == 1);
    centres.push_back(pollution.recyclingCentreValues(working, std::max(0, now - std::min(built, now))));
  }
  pollution.monthEnd(d.month, d.year, residents_, &centres);
}
void City::monthlyCrime() {
  cal::Date d = clock.date();
  crime.monthEnd(d.month, d.year);
  aura.monthEnd(d.month, d.year);
}

// ============================================================================ persistence

void City::loadRecords() {
  if (!world_->raw) return;
  const RawRecordStore& raw = *world_->raw;
  auto tryLoad = [&](const ResKey& k, const std::function<bool(const uint8_t*, size_t)>& fn) {
    const InnerRecord* r = raw.findInner(k);
    if (!r) return false;
    bool ok = false;
    try { ok = fn(r->data.data(), r->data.size()); } catch (const FormatError&) { ok = false; }
    if (!ok) preserved_.insert(k);
    return ok;
  };
  const int W = world_->W, H = world_->H;
  tryLoad(kBudgetKey, [&](const uint8_t* p, size_t n) { budget.parse(p, n); return true; });
  tryLoad(kOrdKey, [&](const uint8_t* p, size_t n) { ordinances.parse(p, n); return true; });
  tryLoad(kNeighbourKey, [&](const uint8_t* p, size_t n) { neighbours.parse(p, n); return true; });
  if (tryLoad(keys::ResLayer, [&](const uint8_t* p, size_t n) { return valves.parseResidential(p, n); })) {
    try { pop.parseState(valves.residentialState.data(), valves.residentialState.size()); } catch (const FormatError&) {}
  }
  tryLoad(keys::ComLayer, [&](const uint8_t* p, size_t n) { return valves.parseCommercial(p, n); });
  tryLoad(keys::IndLayer, [&](const uint8_t* p, size_t n) { return valves.parseIndustrial(p, n); });
  tryLoad(keys::RciLayer, [&](const uint8_t* p, size_t n) { return valves.parseRci(p, n); });
  tryLoad(keys::WorldLayer, [&](const uint8_t* p, size_t n) { return valves.parseWorld(p, n); });
  tryLoad(keys::LandValue, [&](const uint8_t* p, size_t n) { return landValue.parse(p, n); });
  tryLoad(keys::ZoneMgr, [&](const uint8_t* p, size_t n) { std::vector<uint8_t> g; return zones.parse(p, n, W, H, &g); });
  tryLoad(keys::PowerLayer, [&](const uint8_t* p, size_t n) { power.parse(p, n); return true; });
  tryLoad(keys::WaterLayer, [&](const uint8_t* p, size_t n) { water.parse(p, n); return true; });
  tryLoad(kTrafficKey, [&](const uint8_t* p, size_t n) { traffic->loadRecord(parseTrafficRecord(p, n, W, H)); return true; });
  tryLoad(keys::Aura, [&](const uint8_t* p, size_t n) { aura.parse(p, n, W, H); return true; });
  tryLoad(kPollutionKey, [&](const uint8_t* p, size_t n) { pollution.parse(p, n, W, H); return true; });
  tryLoad(kCrimeKey, [&](const uint8_t* p, size_t n) { crime.parse(p, n, W, H); return true; });
  tryLoad(kPoliceKey, [&](const uint8_t* p, size_t n) { police.parse(p, n); return true; });
  tryLoad(kFireKey, [&](const uint8_t* p, size_t n) { fire.parse(p, n); return true; });
  tryLoad(keys::Flammability, [&](const uint8_t* p, size_t n) { flammability.parse(p, n); return true; });
}

void City::flush(World& w) {
  if (!w.raw) w.raw = std::make_shared<RawRecordStore>();
  RawRecordStore& st = *w.raw;
  auto put = [&](const ResKey& k, Bytes b) { if (!preserved_.count(k)) st.setInner(k, std::move(b)); };
  put(kBudgetKey, budget.serialize());
  put(kOrdKey, ordinances.serialize());
  put(kNeighbourKey, neighbours.serialize());
  valves.residentialState = pop.serializeState();
  put(keys::ResLayer, valves.writeResidential());
  put(keys::ComLayer, valves.writeCommercial());
  put(keys::IndLayer, valves.writeIndustrial());
  put(keys::RciLayer, valves.writeRci());
  put(keys::WorldLayer, valves.writeWorld());
  put(keys::LandValue, landValue.write());
  zones.recountZones(w.zone);
  put(keys::ZoneMgr, zones.write(w.zone));
  for (auto& kv : zones.developerClassRecords()) put(kv.first, kv.second);
  put(keys::PowerLayer, power.serialize());
  put(keys::WaterLayer, water.serialize());
  if (traffic) put(kTrafficKey, serializeTrafficRecord(traffic->rec()));
  put(keys::Aura, aura.serialize());
  put(kPollutionKey, pollution.serialize());
  put(kCrimeKey, crime.serialize());
  put(kPoliceKey, police.serialize());
  put(kFireKey, fire.serialize());
  put(keys::Flammability, flammability.serialize());
}

}  // namespace m3k
