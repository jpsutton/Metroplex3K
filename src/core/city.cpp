#include "core/city.h"
#include "core/occ_defaults.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <set>
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
  bool insideLoadedAirport(int x, int z) const override {
    return c_.world_->inTiles(x, z) && !c_.loadedAirport_.empty() && c_.loadedAirport_[c_.idx(x, z)];
  }
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
  bool tileTrip(int x, int z) override { return !c_.traffic || c_.traffic->routeTileTrip(x, z); }
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
    l.port = (o.keyType == T_OCC_ATTR_PORT);
    if (c_.game_) {
      const OccAttr* a = c_.game_->occupant(o.occId, false, l.port);
      if (a) { l.flagWord = a->buildingFlags; l.partIndex = a->partIndex; }
    }
    l.removable = isDeveloperType(o.agentType);
    return l;
  }
  growth::ValveReading valve(int id) const override {
    const growth::Valve& v = c_.valves.valve(id);
    return growth::ValveReading{v.V, v.Max, v.Min};
  }
  bool tripFailed(int x, int z) const override {
    // Trip result 1 is the developers' "unfit" input (spec 18 s9.2, 15 s4.2) [STATIC]. On by default since the traffic
    // model matches the original's saves: failure ratios at day ~480 ours/theirs Berlin 0.004/0.008, Moscow 0.001/0.001,
    // London 0.033/0.032, Madison 0.016/0.016, and all six reference cities stay in their 480-day bands (spec 28 s5.1).
    // M3K_TRIPFAIL=0 turns it off for comparison.
    static const char* env = std::getenv("M3K_TRIPFAIL");
    static const bool envOff = env && env[0] == '0';
    return c_.cfg_.useTripFailure && !envOff && c_.traffic && c_.traffic->tripResult(x, z) == 1;
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
  bool waterTile(int x, int z) const override {
    if (!c_.inMap(x, z)) return false;
    const World& w = *c_.world_;
    for (int dx = 0; dx < 2; dx++) for (int dz = 0; dz < 2; dz++) if (w.waterAt(x + dx, z + dz) >= w.altAt(x + dx, z + dz) && w.waterAt(x + dx, z + dz) > 0) return true;
    return false;
  }
  // Deep-water table (spec 15 §5.5 / Q-AX): W×W u16 keyed z·W+x; pass iff entry != 0xFFFF.
  bool deepWater(int x, int z) const override {
    return c_.world_ && c_.world_->deepWaterAt(x, z);
  }
  bool tileOccupied(int x, int z) const override {
    return c_.inMap(x, z) && (c_.occupantAt(x, z) >= 0 || c_.isRoadTile(x, z) || c_.isPowerLine(x, z));
  }
  bool placePier(const growth::BuildingChoice& b, int x, int z) override {
    for (int dx = 0; dx < b.sizeX; dx++) for (int dz = 0; dz < b.sizeZ; dz++) if (!c_.inMap(x + dx, z + dz)) return false;
    Occupant o;
    o.x = x; o.z = z; o.y = c_.world_->altAt(x, z); o.rot = b.rot & 3;
    o.sizeX = b.sizeX; o.sizeZ = b.sizeZ; o.occId = b.occId; o.keyType = b.port ? T_OCC_ATTR_PORT : T_OCC_ATTR;
    o.agentType = b.agentType; o.state = 1;
    c_.addOccupant(o);
    return true;
  }
  bool spacingAdd(int x, int z) override { return c_.spacingAdd(x, z); }
  bool spacingReserveSegment(int x0, int z0, int x1, int z1) override { return c_.spacingReserveSegment(x0, z0, x1, z1); }
  void spacingReleaseSegment(int x0, int z0, int x1, int z1) override { c_.spacingReleaseSegment(x0, z0, x1, z1); }
  bool spacingSegmentBlock(int x, int z) const override { return c_.spacingSegmentBlock(x, z); }
  bool spacingReservePoint(int x, int z) override { return c_.spacingReservePoint(x, z); }
  void spacingReleasePoint(int x, int z) override { c_.spacingReleasePoint(x, z); }
  bool spacingPointNear(int x, int z) const override { return c_.spacingPointNear(x, z); }
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
  wire();
  {  // Airport regions present at load: 4-connected zone-14 regions holding at least one airport part (spec 15 s7 interim:
     // the original adds no part to them in 480-day runs of Madrid, London and Berlin).
    loadedAirport_.assign(size_t(W) * H, 0);
    std::vector<uint8_t> seen(size_t(W) * H, 0);
    for (int x0 = 0; x0 < W; x0++)
      for (int z0 = 0; z0 < H; z0++) {
        if (seen[idx(x0, z0)] || w->zone[idx(x0, z0)] != 14) continue;
        std::vector<int> region{int(idx(x0, z0))}; seen[idx(x0, z0)] = 1;
        bool hasPart = false;
        for (size_t h = 0; h < region.size(); h++) {
          int x = region[h] / H, z = region[h] % H;
          int oi = occupantAt(x, z);
          if (oi >= 0 && w->occupants[size_t(oi)].agentType == 12310) hasPart = true;
          const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          for (auto& d : d4) {
            int nx = x + d[0], nz = z + d[1];
            if (!inMap(nx, nz) || seen[idx(nx, nz)] || w->zone[idx(nx, nz)] != 14) continue;
            seen[idx(nx, nz)] = 1; region.push_back(int(idx(nx, nz)));
          }
        }
        if (hasPart) for (int t : region) loadedAirport_[size_t(t)] = 1;
      }
  }
  // Source-grid offset (see refresh): saved minus restamped, taken once at load when a pollution record exists.
  polBaseline_ = {};
  crimeBaseline_.clear();
  us::PollutionLayer::SourceGrids savedSrc;
  if (havePollution) savedSrc = pollution.sourceGrids();
  const bool haveCrime = w->raw && w->raw->findInner(kCrimeKey);
  const std::vector<int16_t> savedPotential = crime.potential;
  refresh(true);
  if (haveCrime && savedPotential.size() == crime.potential.size()) {
    crimeBaseline_.resize(savedPotential.size());
    for (size_t i = 0; i < savedPotential.size(); i++) { crimeBaseline_[i] = int16_t(savedPotential[i] - crime.potential[i]); crime.potential[i] = savedPotential[i]; }
  }
  if (havePollution) {
    us::PollutionLayer::SourceGrids now = pollution.sourceGrids();
    polBaseline_ = us::PollutionLayer::SourceGrids::diff(savedSrc, now);
    pollution.applySourceOffset(polBaseline_.air, polBaseline_.water, polBaseline_.garbage);
  }
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
    power.rebuildConsumption(powerConsumers());
    std::vector<uint8_t> conductive;
    power.recomputeCoverage(powerEnv(conductive));
  }
}

void City::raisePause(bool announce) {
  if (++cityPauseCount_ == 1) {
    // Game::message sets msgText_ which blocks handleMap; skip announce when starting already-paused.
    if (announce && onMessage) onMessage("simulation paused");
    // Nestable clock pause: always push, even if already busy-paused (monthly/backlog).
    clock.pause();
  }
}
void City::resumePause() {
  if (cityPauseCount_ == 0) return;
  if (--cityPauseCount_ == 0) {
    // Pop the clock pause we pushed; leave monthly/backlog pauses intact.
    if (clock.paused()) clock.resume();
  }
}
void City::raisePending() {
  if (++cityPendingCount_ == 1) clock.pause();
}
void City::clearPending() {
  if (cityPendingCount_ == 0) return;
  if (--cityPendingCount_ == 0 && clock.paused()) clock.resume();
}

void City::wire() {
  sched.attach(clock);
  // Spec 14 §6.3 rule 6: hold = city pause ∨ pending/modal counts (Q-AQ / 7b41478).
  sched.isHold = [this] { return hold(); };
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
  // Spec 14 §6.1 item 3 / Q-BF [STATIC]: dedicated monthly-tier entry after neighbours, before shared pass.
  sched.setHandler(Step::MonthlyWorldLayer, [this] { monthlyWorld(); });
  sched.setHandler(Step::MonthlyRci, [this] { monthlyValves(); });
  sched.setHandler(Step::MonthlyOrdinances, [this] { monthlyOrdinances(); });
  sched.setHandler(Step::MonthlyResidential, [this] { monthlyPopulation(); });
  sched.setHandler(Step::MonthlyTraffic, [this] { monthlyTraffic(); });
  sched.setHandler(Step::MonthlyBudget, [this] { monthlyBudget(); });
  sched.setHandler(Step::MonthlyPower, [this] { monthlyPower(); });
  sched.setHandler(Step::MonthlyWater, [this] { monthlyWater(); });
  sched.setHandler(Step::MonthlyWeather, [this] { monthlyWeather(); });
  // Q-BK: monthly 14–16 run aura → pollution → crime (order from monthlySharedSteps).
  sched.setHandler(Step::MonthlyAura, [this] { monthlyAura(); });
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
  transport_.assign(N, 0); powerLine_.assign(N, 0); pipe_.assign(N, 0); subway_.assign(N, 0);
  for (const auto& p : world_->surface) {
    if (!world_->inTiles(p.x, p.z)) continue;
    uint32_t k = kindOf(p);
    if (k == 3) powerLine_[idx(p.x, p.z)] = 1;
    else if (k == 1 || k == 2 || k == 4) transport_[idx(p.x, p.z)] = uint8_t(k);
  }
  for (const auto& p : world_->pipes) if (world_->inTiles(p.x, p.z)) pipe_[idx(p.x, p.z)] = 1;
  for (const auto& p : world_->subway) if (world_->inTiles(p.x, p.z)) subway_[idx(p.x, p.z)] = 1;
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
  // #28 Placement query + 13 §3.1a class flags: refresh attrs and rebuild occupancy scanners.
  for (auto& o : world_->occupants) {
    o.classFlags = runtimeOccupantFlags(o.agentType);
    if (game_) {
      if (const OccAttr* a = game_->occupant(o.occId, false, o.keyType == T_OCC_ATTR_PORT)) {
        o.caps = a->flags;
        o.buildingFlags = a->buildingFlags;
      }
    }
  }
  placement.rebuild(world_);
}

void City::applyRoadCondition(int percent) {
  if (!world_ || percent < 0) return;
  auto isRoad = [&](const NetPiece& p) {
    if (!world_->inTiles(p.x, p.z)) return false;
    return cfg_.haveTiling ? cfg_.tiling.inSet(netKindToTable(NK_ROAD), p.pieceId) : transport_[idx(p.x, p.z)] == 1;
  };
  auto potholed = [](uint32_t id) { return id == 15040 || id == 15041 || id == 15042 || id == 9051; };
  auto& pieces = world_->surface;
  int64_t N = 0, D = 0;
  for (const auto& p : pieces) if (isRoad(p)) { N++; if (potholed(p.pieceId)) D++; }
  const int64_t T = int64_t(double(N) * (double(percent) / 100.0));
  if (T == D) return;
  auto gap = [&](int64_t s) { return s < 1 ? int64_t(0) : int64_t(rng_.rand(uint32_t(s))); };
  auto setId = [](NetPiece& p, uint32_t id, bool damaged) {
    p.pieceId = id;
    uint32_t w = p.pieceWord ? p.pieceWord : (0x30000000u | id);
    w = (w & ~0xFFFFu) | (id & 0xFFFFu);
    w = damaged ? (w | (1u << 27)) : (w & ~(1u << 27));
    p.pieceWord = w;
  };
  bool changed = false;
  if (T > D) {   // damage: spaced walk over all road pieces; candidates 29, 35, 11204; stop after k + 1 (original off-by-one)
    const int64_t k = T - D, s = std::max<int64_t>(0, N / k - 1);
    int64_t c = 0, g = gap(s), got = 0;
    std::vector<size_t> picked;
    for (size_t i = 0; i < pieces.size() && got < k + 1; i++) {
      if (!isRoad(pieces[i])) continue;
      if (c < g) { c++; continue; }
      uint32_t id = pieces[i].pieceId;
      if (id == 29 || id == 35 || id == 11204) { picked.push_back(i); got++; g = gap(s); c = 0; }
    }
    for (size_t i : picked) {
      NetPiece& p = pieces[i];
      if (p.pieceId == 29) { setId(p, 15040 + rng_.rand(3), true); changed = true; }
      else if (p.pieceId == 35) { setId(p, 9051, true); changed = true; }
      // 11204: collected but not converted (original defect)
    }
  } else {       // repair: spaced walk over potholed pieces, divisor D, no limit of k
    const int64_t k = D - T, s = std::max<int64_t>(0, D / k - 1);
    int64_t c = 0, g = gap(s);
    for (auto& p : pieces) {
      if (!isRoad(p) || !potholed(p.pieceId)) continue;
      if (c < g) { c++; continue; }
      setId(p, p.pieceId == 9051 ? 35u : 29u, false);
      changed = true;
      g = gap(s); c = 0;
    }
  }
  if (changed) netDirty_ = true;   // rebuild the traffic flags (0x80) and network classification
}

namespace {
std::vector<size_t> spacingNeighCells(World& w, std::vector<uint8_t>& spacing, int x, int z) {
  const int bw = w.W / 4, bh = w.H / 4;
  if (spacing.size() != size_t(bw) * bh) spacing.assign(size_t(bw) * bh, 0);
  std::vector<size_t> cells;
  for (int i = -2; i <= 2; i++)
    for (int j = -2; j <= 2; j++) {
      int tx = x + 4 * i, tz = z + 4 * j;
      if (!w.inTiles(tx, tz)) continue;
      size_t k = size_t(tx / 4) * bh + size_t(tz / 4);
      if (k < spacing.size()) cells.push_back(k);
    }
  return cells;
}
}  // namespace

bool City::spacingAdd(int x, int z) {
  if (!world_) return false;
  std::vector<size_t> cells = spacingNeighCells(*world_, spacing_, x, z);
  for (size_t k : cells) if (((spacing_[k] >> 2) & 15) == 15) return false;
  for (size_t k : cells) spacing_[k] = uint8_t((spacing_[k] & ~0x3C) | ((((spacing_[k] >> 2) & 15) + 1) << 2));
  return true;
}

void City::spacingRem(int x, int z) {
  if (!world_) return;
  // Spec 10 §6.3 [STATIC]: remove decrements the overlap counter on the 5×5-block neighbourhood with no underflow check.
  for (size_t k : spacingNeighCells(*world_, spacing_, x, z))
    spacing_[k] = uint8_t((spacing_[k] & ~0x3C) | ((((spacing_[k] >> 2) - 1) & 15) << 2));
}

namespace {
// Blocks within ±4 tiles (one block / 3×3 neighbourhood) of every tile on an axis-aligned segment (spec 10 §6.3).
std::vector<size_t> spacingSegmentCells(World& w, std::vector<uint8_t>& spacing, int x0, int z0, int x1, int z1) {
  const int bw = w.W / 4, bh = w.H / 4;
  if (spacing.size() != size_t(bw) * bh) spacing.assign(size_t(bw) * bh, 0);
  if (x0 > x1) std::swap(x0, x1);
  if (z0 > z1) std::swap(z0, z1);
  std::set<size_t> uniq;
  auto addAround = [&](int x, int z) {
    for (int i = -1; i <= 1; i++)
      for (int j = -1; j <= 1; j++) {
        int tx = x + 4 * i, tz = z + 4 * j;
        if (!w.inTiles(tx, tz)) continue;
        size_t k = size_t(tx / 4) * bh + size_t(tz / 4);
        if (k < spacing.size()) uniq.insert(k);
      }
  };
  if (z0 == z1) {
    for (int x = x0; x <= x1; x++) addAround(x, z0);
  } else if (x0 == x1) {
    for (int z = z0; z <= z1; z++) addAround(x0, z);
  } else {
    addAround(x0, z0);
    addAround(x1, z1);
  }
  return {uniq.begin(), uniq.end()};
}

std::vector<size_t> spacingPointCells(World& w, std::vector<uint8_t>& spacing, int x, int z) {
  const int bw = w.W / 4, bh = w.H / 4;
  if (spacing.size() != size_t(bw) * bh) spacing.assign(size_t(bw) * bh, 0);
  std::vector<size_t> cells;
  for (int i = -1; i <= 1; i++)
    for (int j = -1; j <= 1; j++) {
      int tx = x + 4 * i, tz = z + 4 * j;
      if (!w.inTiles(tx, tz)) continue;
      size_t k = size_t(tx / 4) * bh + size_t(tz / 4);
      if (k < spacing.size()) cells.push_back(k);
    }
  return cells;
}
}  // namespace

bool City::spacingReserveSegment(int x0, int z0, int x1, int z1) {
  if (!world_) return false;
  auto cells = spacingSegmentCells(*world_, spacing_, x0, z0, x1, z1);
  for (size_t k : cells) if (spacing_[k] & 0x01) return false;
  for (size_t k : cells) spacing_[k] = uint8_t(spacing_[k] | 0x01);
  return true;
}

void City::spacingReleaseSegment(int x0, int z0, int x1, int z1) {
  if (!world_) return;
  for (size_t k : spacingSegmentCells(*world_, spacing_, x0, z0, x1, z1)) spacing_[k] = uint8_t(spacing_[k] & ~0x01);
}

bool City::spacingSegmentBlock(int x, int z) const {
  if (!world_ || !world_->inTiles(x, z) || spacing_.empty()) return false;
  const int bh = world_->H / 4;
  size_t k = size_t(x / 4) * bh + size_t(z / 4);
  return k < spacing_.size() && (spacing_[k] & 0x01) != 0;
}

bool City::spacingReservePoint(int x, int z) {
  if (!world_) return false;
  auto cells = spacingPointCells(*world_, spacing_, x, z);
  for (size_t k : cells) if (spacing_[k] & 0x02) return false;
  for (size_t k : cells) spacing_[k] = uint8_t(spacing_[k] | 0x02);
  return true;
}

void City::spacingReleasePoint(int x, int z) {
  if (!world_) return;
  for (size_t k : spacingPointCells(*world_, spacing_, x, z)) spacing_[k] = uint8_t(spacing_[k] & ~0x02);
}

bool City::spacingPointNear(int x, int z) const {
  if (!world_ || spacing_.empty()) return false;
  // Const path: same neighbourhood as reserve, read-only.
  const int bh = world_->H / 4;
  for (int i = -1; i <= 1; i++)
    for (int j = -1; j <= 1; j++) {
      int tx = x + 4 * i, tz = z + 4 * j;
      if (!world_->inTiles(tx, tz)) continue;
      size_t k = size_t(tx / 4) * bh + size_t(tz / 4);
      if (k < spacing_.size() && (spacing_[k] & 0x02)) return true;
    }
  return false;
}

bool City::hasRoadPiece(int x, int z) const {
  if (!inMap(x, z)) return false;
  if (traffic && (traffic->flags(x, z) & TF_ROAD)) return true;
  return transport_[idx(x, z)] == uint8_t(NetworkKind::Road);
}

double City::roadCondition() const {
  if (!world_) return 0.0;
  int64_t roads = 0, damaged = 0;
  for (const auto& p : world_->surface) {
    if (!world_->inTiles(p.x, p.z)) continue;
    if (cfg_.haveTiling ? !cfg_.tiling.inSet(netKindToTable(NK_ROAD), p.pieceId) : transport_[idx(p.x, p.z)] != 1) continue;
    roads++;
    if (p.pieceId == 15040 || p.pieceId == 15041 || p.pieceId == 15042 || p.pieceId == 9051) damaged++;
  }
  return double(damaged) / double(roads);   // 0/0 = NaN reproduced
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
  Occupant& placed = world_->occupants[size_t(i)];
  if (!placed.uid) placed.uid = nextUid_++;
  if (!placed.classFlags) placed.classFlags = runtimeOccupantFlags(placed.agentType);
  if (game_) {
    if (const OccAttr* a = game_->occupant(placed.occId, false, placed.keyType == T_OCC_ATTR_PORT)) {
      if (!placed.caps) placed.caps = a->flags;
      if (!placed.buildingFlags) placed.buildingFlags = a->buildingFlags;
    }
  }
  for (int dx = 0; dx < o.sizeX; dx++)
    for (int dz = 0; dz < o.sizeZ; dz++)
      if (inMap(o.x + dx, o.z + dz)) occ_[idx(o.x + dx, o.z + dz)] = i;
  occDirty_ = true;
  placement.rebuild(world_);
  if (onBuildingAdded) {
    int n = 0;
    for (const auto& oc : world_->occupants) n += oc.agentType == o.agentType;
    onBuildingAdded(o.agentType, n, o.x, o.z);
  }
  return i;
}

void City::damageOccupant(int i, uint32_t st) {
  if (i < 0 || i >= int(world_->occupants.size())) return;
  Occupant& o = world_->occupants[size_t(i)];
  const uint32_t b = o.state & ~0x200u;
  if ((b == 1 || b == 4) && st != o.state) zones.buildingRemoved(o.agentType, o.sizeX, o.sizeZ, b);
  o.state = st;
  occDirty_ = true;
}

void City::removeOccupantIdx(const std::set<int>& killIn) {
  std::set<int> kill = killIn;
  // Spec 10 §6.3 [STATIC]: spacing-grid removal handler for airport/seaport parts (attr 0x1F4).
  std::vector<std::pair<int, int>> pierRem;
  std::vector<std::array<int, 4>> runwaySeg;   // x0,z0,x1,z1
  std::vector<std::pair<int, int>> pointRem;
  if (game_ && world_) {
    auto partOf = [&](const Occupant& o) -> int {
      const OccAttr* a = game_->occupant(o.occId, false, o.keyType == T_OCC_ATTR_PORT);
      return a ? a->partIndex : 0;
    };
    auto partOk = [&](int oi, auto pred) {
      if (oi < 0 || oi >= int(world_->occupants.size())) return false;
      return pred(partOf(world_->occupants[size_t(oi)]));
    };
    const int d4[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    auto flood = [&](int seed, auto pred, std::set<int>& seen) {
      std::vector<int> comp;
      if (!partOk(seed, pred) || !seen.insert(seed).second) return comp;
      std::vector<int> q{seed};
      for (size_t qi = 0; qi < q.size(); qi++) {
        const Occupant& o = world_->occupants[size_t(q[qi])];
        comp.push_back(q[qi]);
        for (auto& dd : d4) {
          int nj = occupantAt(o.x + dd[0], o.z + dd[1]);
          if (!partOk(nj, pred) || !seen.insert(nj).second) continue;
          q.push_back(nj);
        }
      }
      return comp;
    };
    std::set<int> seenPier, seenRunway;
    for (int seed : killIn) {
      // Pier parts 7–9: remove the connected pier and *remove* one overlap count at the head (line 8).
      if (partOk(seed, [](int p) { return p >= 7 && p <= 9; }) && !seenPier.count(seed)) {
        auto comp = flood(seed, [](int p) { return p >= 7 && p <= 9; }, seenPier);
        int hx = world_->occupants[size_t(seed)].x, hz = world_->occupants[size_t(seed)].z;
        for (int oi : comp) {
          kill.insert(oi);
          if (partOf(world_->occupants[size_t(oi)]) == 8) {
            hx = world_->occupants[size_t(oi)].x;
            hz = world_->occupants[size_t(oi)].z;
          }
        }
        pierRem.push_back({hx, hz});
      }
      // Runway part index 1: remove the rest of its runway line (parts 1–3) and release the strip segment.
      if (partOk(seed, [](int p) { return p == 1; }) && !seenRunway.count(seed)) {
        auto comp = flood(seed, [](int p) { return p >= 1 && p <= 3; }, seenRunway);
        int x0 = INT32_MAX, z0 = INT32_MAX, x1 = INT32_MIN, z1 = INT32_MIN;
        bool any1 = false;
        for (int oi : comp) {
          kill.insert(oi);
          const Occupant& o = world_->occupants[size_t(oi)];
          if (partOf(o) != 1) continue;
          any1 = true;
          x0 = std::min(x0, o.x); z0 = std::min(z0, o.z);
          x1 = std::max(x1, o.x); z1 = std::max(z1, o.z);
        }
        if (any1) {
          // Strip endpoints: the row-A extent is axis-aligned; collapse the thin axis to a line.
          if (x1 - x0 >= z1 - z0) z1 = z0 = (z0 + z1) / 2;
          else x1 = x0 = (x0 + x1) / 2;
          runwaySeg.push_back({x0, z0, x1, z1});
        }
      }
      // Airport building part index 4: release the point at its anchor.
      if (partOk(seed, [](int p) { return p == 4; })) {
        const Occupant& o = world_->occupants[size_t(seed)];
        pointRem.push_back({o.x, o.z});
      }
    }
  }
  for (auto [hx, hz] : pierRem) spacingRem(hx, hz);
  for (auto& s : runwaySeg) spacingReleaseSegment(s[0], s[1], s[2], s[3]);
  for (auto [x, z] : pointRem) spacingReleasePoint(x, z);

  for (auto it = kill.rbegin(); it != kill.rend(); ++it) {
    const Occupant o = world_->occupants[size_t(*it)];
    // Spec 18 §11 [STATIC]: a station clears its flags and zeroes surface density on the footprint;
    // any other non-transport building clears the lot's trip-tried / trip-ok bits.
    if (traffic) {
      if (stationFlagsForType(o.agentType))
        traffic->removeStation(o.agentType, TripRect{o.x, o.z, o.x + o.sizeX - 1, o.z + o.sizeZ - 1});
      else
        traffic->clearLot(TripRect{o.x, o.z, o.x + o.sizeX - 1, o.z + o.sizeZ - 1});
    }
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


namespace {
// Orthogonal on-ramp pieces [DATA Sacramento]: 3 columns × 2 carriageways.
// Column i=0..2, carriage c=0..1 → 15130 + 4*i + c. Reverse approach swaps column and carriage.
// Spec 12 §7 called this a "3-tile lot"; the shipped cities place six 1×1 network pieces (agent 17202).
void placeOrthogonalOnRamp(WorldNetworkMap& map, const OnRampPlan& plan) {
  if (!plan.ok || plan.diagonal || plan.footprint.size() != 3) return;
  const int ax = plan.footprint[1].first, az = plan.footprint[1].second;
  // Highway side: unit step from the middle footprint tile toward a neighbouring highway piece.
  int hx = 0, hz = 0;
  for (int s : {-1, 1}) {
    if (plan.orientation == 0) {  // footprint along z → highway along ±x
      auto o = map.occupantAt(ax + s, az, LAYER_SURFACE);
      if (o.present && (o.id == 73 || o.id == 11258 || o.id == 15051 || o.id == 15098)) { hx = s; break; }
    } else {
      auto o = map.occupantAt(ax, az + s, LAYER_SURFACE);
      if (o.present && (o.id == 73 || o.id == 11258 || o.id == 15051 || o.id == 15098)) { hz = s; break; }
    }
  }
  if (hx == 0 && hz == 0) {
    // Fall back to the side recorded by planOnRamp's search (perpendicular to footprint).
    if (plan.orientation == 0) hx = 1; else hz = 1;
  }
  // Approach direction along the footprint: +1 if footprint[2] is the "far" end used by rot=2 in Sacramento.
  const int dx = plan.footprint[2].first - plan.footprint[0].first;
  const int dz = plan.footprint[2].second - plan.footprint[0].second;
  const bool forward = (dx + dz) > 0;   // [INFERRED] rot 2 ↔ positive footprint axis
  const int rot = forward ? 2 : 0;
  auto altAt = [&](int x, int z) {
    int c[4]; map.tileCorners(x, z, c); return *std::min_element(c, c + 4);
  };
  for (int i = 0; i < 3; i++) {
    const int col = forward ? i : 2 - i;
    const int x0 = plan.footprint[size_t(i)].first, z0 = plan.footprint[size_t(i)].second;
    for (int c = 0; c < 2; c++) {
      const int car = forward ? c : 1 - c;
      const uint32_t id = 15130u + uint32_t(4 * col + car);
      const int x = x0 + c * hx, z = z0 + c * hz;
      if (!map.onMap(x, z)) continue;
      map.placePiece(x, z, LAYER_SURFACE, id, rot, altAt(x, z));
    }
  }
}
}  // namespace

std::vector<std::pair<int, int>> City::routeNetworkPath(NetworkKind k, int x0, int z0, int x1, int z1) const {
  std::vector<std::pair<int, int>> path;
  if (!world_ || !cfg_.haveTiling) return path;
  Tool tool = k == NetworkKind::Road ? TOOL_ROAD : k == NetworkKind::Rail ? TOOL_RAIL : k == NetworkKind::Power ? TOOL_POWER
            : k == NetworkKind::Pipe ? TOOL_PIPE : k == NetworkKind::Highway ? TOOL_HIGHWAY : TOOL_SUBWAY;
  WorldNetworkMap map(*const_cast<World*>(world_), flagsFromRuleSets(cfg_.tiling));
  RoutedPath rp = ::m3k::routePath(map, tool, {x0, z0}, {x1, z1});
  path.reserve(rp.tiles.size());
  for (const auto& t : rp.tiles) if (t.valid) path.push_back({t.x, t.z});
  return path;
}

City::Quote City::quoteOnRamp(int ax, int az) const {
  Quote q;
  if (!world_ || !cfg_.haveTiling) return q;
  WorldNetworkMap map(*const_cast<World*>(world_), flagsFromRuleSets(cfg_.tiling));
  OnRampPlan plan = planOnRamp(map, ax, az);
  if (!plan.ok) return q;
  PlacementCosts pc = makeCosts(budget, cfg_);
  int cost = pc.cost(toolParams(TOOL_ONRAMP).costKey);
  if (cost == 0 && budget.cfg.placementCost.empty() && !budget.freePlacement) cost = 50;
  // Point-tool charge: one tile × highway key 16001 (spec 12 §7 / commitPath).
  q.valid = true; q.cost = cost;
  return q;
}

std::vector<std::pair<int, int>> City::onRampPreview(int ax, int az) const {
  std::vector<std::pair<int, int>> out;
  if (!world_ || !cfg_.haveTiling) return out;
  WorldNetworkMap map(*const_cast<World*>(world_), flagsFromRuleSets(cfg_.tiling));
  OnRampPlan plan = planOnRamp(map, ax, az);
  if (!plan.ok) return out;
  // Show the three footprint tiles; dual-carriageway partners are implied.
  for (auto& t : plan.footprint) out.push_back(t);
  return out;
}

bool City::buildOnRamp(int ax, int az) {
  lastFailNoMoney = false;
  if (!world_ || !cfg_.haveTiling) return false;
  WorldNetworkMap map(*world_, flagsFromRuleSets(cfg_.tiling));
  PlacementCosts costs = makeCosts(budget, cfg_);
  struct Host : ToolHost {
    City& c;
    explicit Host(City& cc) : c(cc) {}
    long funds() const override { return long(c.budget.funds); }
    void charge(int a) override { c.budget.funds -= a; }
    void sound(int id) override { if (c.onSound) c.onSound(id); }
    void message(const std::string& m) override { if (c.onMessage) c.onMessage(m); }
    WorldNetworkMap* map = nullptr;
    bool buildOnRamp(const OnRampPlan& plan) override {
      if (!plan.ok || !map) return false;
      if (plan.diagonal) {
        // Diagonal ramp piece pick is still [OPEN] in the spec; refuse rather than guess IDs.
        return false;
      }
      placeOrthogonalOnRamp(*map, plan);
      return true;
    }
  } host(*this);
  host.map = &map;
  RoutedPath rp = routePath(map, TOOL_ONRAMP, {ax, az}, {ax, az});
  CommitResult r = commitPath(map, cfg_.tiling, costs, host, TOOL_ONRAMP, rp);
  if (r.status == COMMIT_OK) { rebuildIndex(); return true; }
  if (r.status == COMMIT_NO_MONEY) lastFailNoMoney = true;
  return false;
}

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

City::NetworkConfirm City::peekNetworkConfirm(NetworkKind k, const std::vector<std::pair<int, int>>& path) const {
  NetworkConfirm out;
  if (!world_ || !cfg_.haveTiling || path.empty()) return out;
  Tool tool = k == NetworkKind::Road ? TOOL_ROAD : k == NetworkKind::Rail ? TOOL_RAIL : k == NetworkKind::Power ? TOOL_POWER
            : k == NetworkKind::Pipe ? TOOL_PIPE : k == NetworkKind::Highway ? TOOL_HIGHWAY : TOOL_SUBWAY;
  WorldNetworkMap map(*const_cast<World*>(world_), flagsFromRuleSets(cfg_.tiling));
  PlacementCosts costs = makeCosts(budget, cfg_);
  struct Host : ToolHost {
    const City& c;
    explicit Host(const City& cc) : c(cc) {}
    long funds() const override { return long(c.budget.funds); }
    void charge(int) override {}
    bool neighbourExists(int edge) const override {
      return edge >= 0 && edge < 4 && c.neighbours.neighbours[edge].exists;
    }
  } host(*this);
  static const int setOfKind[7] = {-1, 1, 0, 3, 4, 2, 5};
  for (int nk = 1; nk <= 6; nk++)
    for (uint32_t p : neighbours.conn[setOfKind[nk]]) map.setConnection(nk, int(p >> 16), int(p & 0xFFFF), true);
  // Same straight-run split as buildNetwork; commit order is bridge → tunnel → connection (spec 12 §6.2.6).
  std::vector<std::pair<std::pair<int, int>, std::pair<int, int>>> segs;
  size_t s = 0;
  for (size_t i = 2; i < path.size(); i++) {
    int dx0 = path[i - 1].first - path[i - 2].first, dz0 = path[i - 1].second - path[i - 2].second;
    int dx1 = path[i].first - path[i - 1].first, dz1 = path[i].second - path[i - 1].second;
    if (dx0 != dx1 || dz0 != dz1) { segs.push_back({path[s], path[i - 1]}); s = i - 1; }
  }
  segs.push_back({path[s], path.back()});
  const int nk = int(k);
  const NetworkRules& nr = cfg_.tiling.nets[netKindToTable(nk)];
  const int tileCost = costs.cost(toolParams(tool).costKey);
  for (auto& sg : segs) {
    RoutedPath rp = routePath(map, tool, sg.first, sg.second);
    std::vector<PathTile> tiles = toPathTiles(rp, true);
    int dragPrice = int(tiles.size()) * tileCost;
    if (tiles.size() >= 4 && !nr.bridges.empty() &&
        (nk == NK_ROAD || nk == NK_RAIL || nk == NK_POWER || nk == NK_HIGHWAY)) {
      int passes = nk == NK_HIGHWAY ? 2 : 1, total = 0;
      bool any = false;
      for (int pass = 0; pass < passes; pass++) {
        std::vector<PathTile> sub;
        for (auto& t : tiles) if (nk != NK_HIGHWAY || t.carriage == pass) sub.push_back(t);
        BridgePlan bp = planBridge(map, nr, nk, sub, costs, pass);
        if (!bp.ok) continue;
        total += bp.price;
        any = true;
      }
      if (any && total <= host.funds()) {
        out.kind = NetworkConfirmKind::Bridge;
        out.price = total;
        out.powerBridge = (nk == NK_POWER);
        return out;
      }
    }
    if (nk == NK_ROAD || nk == NK_RAIL || nk == NK_HIGHWAY) {
      TunnelPlan tp = planTunnel(map, nk, tiles, costs);
      if (tp.ok && tp.price <= host.funds()) {
        out.kind = NetworkConfirmKind::Tunnel;
        out.price = tp.price;
        return out;
      }
    }
    ConnectionOfferPlan pl = planConnectionOffer(map, host, costs, nk, tiles, dragPrice);
    if (pl.ok) {
      out.kind = NetworkConfirmKind::Connection;
      out.price = pl.price;
      return out;
    }
  }
  return out;
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
    bool neighbourExists(int edge) const override {
      return edge >= 0 && edge < 4 && c.neighbours.neighbours[edge].exists;
    }
    bool confirmConnection(int, int) override {
      return c.connectionConfirmOverride.value_or(true);
    }
  } host(*this);
  // Neighbour connections (spec 12 s8): the map starts from the saved connection sets, and connections made by this drag are
  // written back (set + the neighbour's count for that network, one per accepted offer). Game sets
  // connectionConfirmOverride after the yes/no dialog (029541F4:0x252 / 0x251); nullopt auto-accepts.
  static const int setOfKind[7] = {-1, 1, 0, 3, 4, 2, 5};   // NK kind -> saved set index (rail, road, pipe, power, highway, subway)
  for (int nk = 1; nk <= 6; nk++)
    for (uint32_t p : neighbours.conn[setOfKind[nk]]) map.setConnection(nk, int(p >> 16), int(p & 0xFFFF), true);
  const auto before = map.connections();
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
  {  // write back new connections
    std::set<int> kindsAdded[4];
    for (const auto& t : map.connections()) {
      if (before.count(t)) continue;
      const int nk = std::get<0>(t), x = std::get<1>(t), z = std::get<2>(t);
      if (nk < 1 || nk > 6) continue;
      neighbours.conn[setOfKind[nk]].insert((uint32_t(x) << 16) | uint32_t(z));
      int edge = edgeNeighbour(x, z, world_->W, world_->H);
      if (edge >= 0 && edge < 4) kindsAdded[edge].insert(nk);
    }
    for (int e = 0; e < 4; e++)
      for (int nk : kindsAdded[e]) {
        Neighbour& nb = neighbours.neighbours[e];
        switch (nk) {
          case 1: nb.nRoad++; break; case 2: nb.nRail++; break; case 3: nb.nPower++; break;
          case 4: nb.nHighway++; break; case 5: nb.nPipe++; break; case 6: nb.nSubway++; break;
        }
      }
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
  // Bulldozing a network piece removes every connection of every kind at that tile (spec 12 s8 "Removal") [STATIC].
  for (auto& kv : netTiles)
    for (auto& t : kv.second)
      for (auto& set : neighbours.conn) set.erase((uint32_t(t.first) << 16) | uint32_t(t.second));
  // Spec 18 §11 [STATIC]: each removed piece clears its flag bits and zeroes its own network layer's
  // live + snapshot density on the footprint (span 0x4000 is never cleared here).
  if (traffic) {
    for (auto& kv : netTiles) {
      for (auto& t : kv.second) {
        TripRect r{t.first, t.second, t.first, t.second};
        switch (kv.first) {
          case 1:  // road
            traffic->clearNetwork(r, uint16_t(TF_ROAD | TF_DAMAGED | TF_PROP_A | TF_ROAD_EXIT), true, false, false);
            break;
          case 2:  // rail
            traffic->clearNetwork(r, uint16_t(TF_RAIL | TF_PROP_A | TF_RAIL_EXIT), true, false, false);
            break;
          case 4:  // highway (on-ramp access bit cleared with the highway piece)
            traffic->clearNetwork(r, uint16_t(TF_HIGHWAY | TF_ACCESS | TF_DAMAGED | TF_PROP_A | TF_HWY_EXIT), false, true,
                                  false);
            break;
          case 5:  // subway (when tracked as a transport kind)
            traffic->clearNetwork(r, uint16_t(TF_SUBWAY | TF_SUBWAY_PROP_A | TF_SUBWAY_EXIT), false, false, true);
            break;
          default: break;
        }
      }
    }
  }
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

std::vector<us::SimOccupant> City::powerConsumers() const {
  // Per-tile power consumption (spec 12 s9.1, 16 s2.6) [STATIC]: attribute 0xC9 of the occupant's OWN record (keyed per
  // record, not per agent type), 0 when absent; no density, lot-size or funding factor. Occupied buildings and
  // power-line pieces. (The occupant list holds only lot occupants, all of which
  // descend from 100; records without 0xC9 contribute 0.)
  std::vector<us::SimOccupant> v;
  if (!game_) return v;
  for (const auto& o : world_->occupants) {
    // Only occupied lots (state 1 / 513) consume [DATA, clean side: Σ 0xC9 over state-1 lots equals the shipped consumed
    // total in Berlin 158,275 vs 158,254, Craterville 49,344 vs 49,192, London 0.9 x 127,580 vs 114,858, Madison 38,328 vs
    // 38,303; with abandoned and under-construction lots included every city is 2-12% over].
    if ((o.state & ~0x200u) != 1) continue;
    const OccAttr* a = game_->occupant(o.occId, false, o.keyType == T_OCC_ATTR_PORT);
    int use = a && a->powerAttr > 0 ? a->powerAttr : 0;
    if (use <= 0) continue;
    us::SimOccupant s;
    s.id = o.occId; s.agentType = o.agentType; s.x = o.x; s.z = o.z; s.sizeX = o.sizeX; s.sizeZ = o.sizeZ;
    s.hasState = true; s.state = o.state; s.powerUse = use;
    v.push_back(s);
  }
  for (const auto& p : world_->surface) {
    if (!isPowerLine(p.x, p.z)) continue;
    const OccAttr* a = game_->occupant(p.pieceId, true);
    if (!a || a->powerAttr <= 0) continue;
    us::SimOccupant s; s.agentType = a->agentType; s.x = p.x; s.z = p.z; s.powerUse = a->powerAttr;
    v.push_back(s);
  }
  return v;
}

std::vector<us::SimOccupant> City::simOccupants(bool operatingOnly) const {
  std::vector<us::SimOccupant> v;
  for (const auto& o : world_->occupants) {
    if (operatingOnly && (o.state & ~0x200u) != 1) continue;
    us::SimOccupant s;
    s.id = o.occId; s.agentType = o.agentType; s.group = o.agentType;
    s.x = o.x; s.z = o.z; s.sizeX = o.sizeX; s.sizeZ = o.sizeZ;
    s.hasState = true; s.state = o.state;
    s.occupantKind = occupantKind(o.agentType);
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
    const uint32_t lot = o.state & ~0x200u;
    // Spec 16 §2.6 / Q-BW [STATIC]: expired plants stay on the map at lot state 8; keep them in the layer.
    if (us::powerCategory(o.agentType) >= 0 && (lot == 1 || lot == 8)) {
      plants.insert(key);
      if (!plantAt_.count(key)) {
        // A loaded plant keeps its build date (date pair) and its saved per-plant properties (spec 10 s6.2.5, 16
        // persisted state): 2351FAF8 overwork months, 2351FAF9 maximum life, 2351FAFA decline age, 2351FAFB expired.
        int built = now;
        if (o.hasDate && o.dateYear > 0) built = std::min(now, 12 * o.dateYear + std::max(0, std::min(11, (std::max(1, o.dateDay) - 1) * 12 / 366)) + 1);
        int id = power.addPlant(o.x, o.z, o.agentType, built, urng_);
        if (id >= 0) {
          plantAt_[key] = id;
          for (auto& p : power.plants()) {
            if (p.id != id) continue;
            for (const auto& kv : o.intProps) {
              switch (kv.first) {
                case 0x2351FAF8: p.overwork = uint16_t(kv.second & 0xFFFF); break;
                case 0x2351FAF9: if (kv.second > 0) p.lmax = int(kv.second); break;
                case 0x2351FAFA: if (kv.second > 0) p.ld = int(kv.second); break;
                case 0x2351FAFB: p.expired = kv.second != 0; break;
                default: break;
              }
            }
            if (lot == 8) p.expired = true;   // Q-BW: destroyed lot ⇒ expired
            p.output = int(us::plantOutput(uint32_t(p.capacity), std::max(0, now - built), p.lmax, p.ld));
          }
        }
      }
      continue;
    }
    if (lot != 1) continue;
    if (o.agentType == kAgentPump || o.agentType == kAgentTower || o.agentType == kAgentDesal) {
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
        if (o.agentType == kAgentJail) {
          // A loaded jail keeps its inmates (property 0352FBBD; spec 10 s6.2.5, 17 s3.3 persisted state).
          int id = police.addJail(jailCapacity());
          for (const auto& kv : o.intProps) if (kv.first == 0x0352FBBD) police.setJailInmates(id, int(kv.second));
          stationAt_[key] = {2, id};
        }
        else if (usHier_.isAncestorOrSelf(o.agentType, kAgentPolice)) stationAt_[key] = {0, police.addStation(o.x, o.z, o.x + o.sizeX - 1, o.z + o.sizeZ - 1)};
        else stationAt_[key] = {1, fire.addStation(o.x, o.z, o.x + o.sizeX - 1, o.z + o.sizeZ - 1)};
      }
    }
  }
  // Water facilities can also be expired at lot state 8 (Q-BW); keep them tracked like plants.
  for (const auto& o : world_->occupants) {
    if ((o.state & ~0x200u) != 8) continue;
    if (o.agentType != kAgentPump && o.agentType != kAgentTower && o.agentType != kAgentDesal) continue;
    std::pair<int, int> key{o.x, o.z};
    wf.insert(key);
    if (!waterFacAt_.count(key)) {
      us::WaterKind kind = o.agentType == kAgentPump ? us::WaterKind::Pump : o.agentType == kAgentTower ? us::WaterKind::Tower : us::WaterKind::Desalination;
      int built = now;
      if (o.hasDate && o.dateYear > 0) built = 12 * o.dateYear + std::max(0, std::min(11, (std::max(1, o.dateDay) - 1) * 12 / 366)) + 1;
      int id = water.addFacility(kind, o.x, o.z, std::min(built, now));
      waterFacAt_[key] = id;
      for (auto& f : water.facilities()) if (f.id == id) f.expired = true;
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
  const int W = world_->W, H = world_->H;
  hwyMatrix_.assign(size_t(W) * H, 0); hwyRot_.assign(size_t(W) * H, 0); hwyPiece_.assign(size_t(W) * H, 0);
  span_.assign(size_t(W) * H, 0);
  if (cfg_.haveTiling && game_) {
    // Network flags from the surface pieces (spec 18 s3.1) [STATIC]: road/highway/rail by tiling-set membership (a piece
    // may be in more than one set: on-ramps are highway-set members with or without road); 0x0040 on the on-ramp pieces
    // 15130-15141; 0x0100 when bit 0 of the piece's attribute 0x12D is set. Spans (0x4000) are not restored on load
    // (spec: OPEN), so loaded cities have none.
    for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) traffic->flags(x, z) = 0;
    for (const auto& p : world_->surface) {
      if (!inMap(p.x, p.z)) continue;
      uint16_t f = 0;
      if (cfg_.tiling.inSet(netKindToTable(NK_ROAD), p.pieceId)) f |= TF_ROAD;
      if (cfg_.tiling.inSet(netKindToTable(NK_HIGHWAY), p.pieceId)) f |= TF_HIGHWAY;
      if (cfg_.tiling.inSet(netKindToTable(NK_RAIL), p.pieceId)) f |= TF_RAIL;
      if (!f) continue;
      if (p.pieceId >= 15130 && p.pieceId <= 15141) f |= TF_ACCESS;
      const OccAttr* a = game_->occupant(p.pieceId, true);
      if (a && (a->netAttr12D & 1)) f |= TF_PROP_A;
      // Piece word (spec 10 s6.2.6) [STATIC]: bit 16 span flag, bits 17-18 direction (0 -x, 1 -z, 2 +x, 3 +z), bits 19-26
      // length; bit 27 damaged (traffic 0x80). Saved and restored with the piece, so spans come back at load.
      if ((p.pieceWord >> 16) & 1) {
        f |= TF_SPAN;
        span_[size_t(p.x) * H + p.z] = uint16_t(((p.pieceWord >> 17) & 3) | (((p.pieceWord >> 19) & 0xFF) << 2));
      }
      if ((p.pieceWord >> 27) & 1) f |= TF_DAMAGED;
      traffic->flags(p.x, p.z) |= f;
      size_t i = size_t(p.x) * H + p.z;
      if (a && a->hasNetMatrix) { hwyMatrix_[i] = a->netMatrix[0]; hwyRot_[i] = uint8_t(p.rot & 3); hwyPiece_[i] = 1; }
    }
    for (const auto& p : world_->subway) {
      if (!inMap(p.x, p.z)) continue;
      uint16_t f = TF_SUBWAY;
      const OccAttr* a = game_->occupant(p.pieceId, true);
      if (a && (a->netAttr12D & 1)) f |= TF_SUBWAY_PROP_A;
      traffic->flags(p.x, p.z) |= f;
    }
    // Highway-like modes (spec 18 s5.4) [STATIC]: exits from the piece's 0x136 matrix, half chosen by the city-wide
    // boolean B, row = last move d + 4 * rotation r. B's meaning is OPEN; M3K_HWY_B=0 selects bits 16-31.
    static const uint8_t rows[16][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {8, 9, 10, 11}, {12, 13, 14, 15}, {15, 12, 13, 14}, {3, 0, 1, 2},
                                        {7, 4, 5, 6}, {11, 8, 9, 10}, {10, 11, 8, 9}, {14, 15, 12, 13}, {2, 3, 0, 1}, {6, 7, 4, 5},
                                        {5, 6, 7, 4}, {9, 10, 11, 8}, {13, 14, 15, 12}, {1, 2, 3, 0}};
    const char* bEnv = std::getenv("M3K_HWY_B");
    const bool B = !(bEnv && bEnv[0] == '0');
    traffic->highwayExits = [this, B, H](int x, int z, int d) -> unsigned {
      size_t i = size_t(x) * H + z;
      if (i >= hwyPiece_.size() || !hwyPiece_[i]) return 0;
      uint32_t half = B ? (hwyMatrix_[i] & 0xFFFF) : (hwyMatrix_[i] >> 16);
      const uint8_t* b = rows[(d & 3) + 4 * hwyRot_[i]];
      // spec order (x-1, y-1, x+1, y+1) -> our exit bits (bit0 N(-z), 1 W(-x), 2 S(+z), 3 E(+x))
      unsigned ex = 0;
      if ((half >> b[0]) & 1) ex |= 2;   // x-1
      if ((half >> b[1]) & 1) ex |= 1;   // z-1
      if ((half >> b[2]) & 1) ex |= 8;   // x+1
      if ((half >> b[3]) & 1) ex |= 4;   // z+1
      return ex;
    };
  } else {
    for (int x = 0; x < W; x++)
      for (int z = 0; z < H; z++) {
        uint8_t k = transport_[idx(x, z)];
        traffic->flags(x, z) = k == 1 ? TF_ROAD : k == 4 ? TF_HIGHWAY : k == 2 ? TF_RAIL : 0;
      }
    for (const auto& p : world_->subway)
      if (inMap(p.x, p.z)) traffic->flags(p.x, p.z) |= TF_SUBWAY;
  }
  traffic->spanOf = [this, H](int x, int z, SpanInfo& sp) -> bool {
    size_t i = size_t(x) * H + z;
    if (i >= span_.size() || !span_[i]) return false;
    sp.dir = span_[i] & 3; sp.len = span_[i] >> 2;
    return sp.len >= 1;
  };
  // Counting-test density d (spec 18 s4.5) [STATIC]: the RCI development layer's valve-effect map (layer 0x80F1E6D3,
  // last completed cycle) at the lot's first tile.
  // Trips from landfill tiles and pieces succeed (spec 18 s9.2 interim [INFERRED from DATA]: every Berlin landfill tile is
  // "ok" in every save of the original, including tiles with no road in reach).
  traffic->alwaysReaches = [this](int x, int z) { return inMap(x, z) && world_->zone[idx(x, z)] == 17; };
  // (C, I) weights of the commercial-or-industrial destination draw (spec 18 s4.2) [STATIC]: the EconomicPhases weights
  // (a, b) at the current population, held by the world jobs valve (15 s3.5). An earlier revision used job totals.
  traffic->ciWeights = [this](int& C, int& I) { C = valves.phaseA(); I = valves.phaseB(); };
  if (residents_ > 0) valves.setPhase(residents_);   // recomputed at load, not only at the month end (spec 18 s4.2)
  traffic->lotDensity = [this, H](int x, int z) -> int {
    size_t i = size_t(x) * H + z;
    return i < valves.effectMap.size() ? int(valves.effectMap[i]) : 0;
  };
  {  // walking radii follow the enacted ordinances from load on, not from the first month end (spec 18 s4.3, s11)
    TrafficOrdinances o;
    o.parkingFines = ordinances.enacted(kOrdParkingFines); o.alternateDayDriving = ordinances.enacted(kOrdAlternateDay);
    o.carpool = ordinances.enacted(kOrdCarpool); o.crossingGuards = ordinances.enacted(kOrdCrossingGuards);
    o.touristPromotion = ordinances.enacted(kOrdTourist); o.subsidizedTransit = ordinances.enacted(kOrdSubsidizedTransit);
    o.shuttleService = ordinances.enacted(kOrdShuttle);
    traffic->applyOrdinances(o);
    // Road and transit funding from the budget at load (spec 18 s8.1 "recomputed after load"): the mass-transit chance
    // depends on the transit efficiency, which was 100 % until the first month end.
    if (const Department* r = budget.dept(dept::Roads)) traffic->setRoadFunding(r->funding);
    if (const Department* tr = budget.dept(dept::Transit)) traffic->setTransitFunding(tr->funding);
  }
  // Station occupants (spec 18 s3.1) [STATIC]: bus stops, rail, subway and subway-rail stations OR their flags into every
  // tile of the footprint.
  for (const auto& o : world_->occupants)
    if (uint16_t sf = stationFlagsForType(o.agentType))
      for (int x = o.x; x < o.x + o.sizeX; x++)
        for (int z = o.z; z < o.z + o.sizeZ; z++)
          if (inMap(x, z)) traffic->flags(x, z) |= sf;
  // Neighbour connections absorb every trip that reaches them (spec 18 s3 flags 0x0400-0x1000, s5.8) [STATIC].
  // Saved connection sets: rail, road, pipe, power, highway, subway.
  const uint16_t exitFlag[6] = {TF_RAIL_EXIT, TF_ROAD_EXIT, 0, 0, TF_HWY_EXIT, TF_SUBWAY_EXIT};
  for (int k = 0; k < 6; k++) {
    if (!exitFlag[k]) continue;
    for (uint32_t p : neighbours.conn[k]) {
      int x = int(p >> 16), z = int(p & 0xFFFF);
      if (inMap(x, z)) traffic->flags(x, z) |= exitFlag[k];
    }
  }
  tripBuildings_.clear();
  // Generators (spec 18 4.1) [STATIC]: category by the occupant's parent chain; week slot dealt per building *type* from one
  // counter 0,1,2,3,... over the trip-making types in registry order = CitySim.ini [AgentTypes] by ascending unsigned ID.
  if (tripSlot_.empty()) {
    std::vector<uint32_t> reg;
    for (const auto& kv : cfg_.ini.citySim.section("AgentTypes")) reg.push_back(parseIdNumber(kv.first));
    if (reg.empty()) reg = usHier_.types();
    std::sort(reg.begin(), reg.end());
    reg.erase(std::unique(reg.begin(), reg.end()), reg.end());
    int next = 0;
    for (uint32_t ty : reg)
      if (categoryOfTypeChain(usHier_.chain(ty)) >= 0) tripSlot_[ty] = (next++) & 3;
  }
  for (const auto& o : world_->occupants) {
    TripBuilding b;
    b.x = o.x; b.z = o.z; b.w = o.sizeX; b.h = o.sizeZ;
    b.category = categoryOfTypeChain(usHier_.chain(o.agentType));
    auto it = tripSlot_.find(o.agentType);
    b.slot = it != tripSlot_.end() ? it->second : 0;
    b.functional = (o.state & ~0x200u) == 1;
    if (b.makesTrips()) tripBuildings_.push_back(b);
  }
  traffic->rebuildDestinations(tripBuildings_);
  // Spec 18 §11 [STATIC]: removing a network piece zeroes that layer's live and snapshot density on its
  // footprint. rebuildTraffic is the common path after bulldoze/retile; clear any layer that no longer
  // has a piece (road/rail/station share surface; highway and subway are their own). TF_SPAN is left alone.
  auto& tr = traffic->rec();
  for (int x = 0; x < W; x++)
    for (int z = 0; z < H; z++) {
      size_t k = size_t(x) * H + z;
      uint16_t f = traffic->flags(x, z);
      if (!(f & (TF_ROAD | TF_RAIL | TF_STATION | TF_XFER))) tr.snapSurface[k] = tr.liveSurface[k] = 0;
      if (!(f & TF_HIGHWAY)) tr.snapHighway[k] = tr.liveHighway[k] = 0;
      if (!(f & TF_SUBWAY)) tr.snapSubway[k] = tr.liveSubway[k] = 0;
    }
}

void City::refresh(bool force) {
  if (!netDirty_ && !occDirty_) return;
  if (!force && !netDirty_ && ++refreshSkips_ < 7) return;
  refreshSkips_ = 0;
  if (netDirty_) classifyNetwork();
  auto all = simOccupants(false);
  crime.rebuild(all);
  // Crime potential: as for the pollution sources below, the saved P is kept (spec 17 s7.1, "Otherwise the saved P is
  // kept"); later changes are applied on top of it.
  if (!crimeBaseline_.empty())
    for (size_t i = 0; i < crime.potential.size() && i < crimeBaseline_.size(); i++) crime.potential[i] = int16_t(crime.potential[i] + crimeBaseline_[i]);
  // Pollution sources: the saved grids are used as loaded (spec 17 s7.2.3: rebuilt only when the INI table changed);
  // later changes are applied on top. We restamp everything and add back the load-time difference between the saved
  // grids and a full restamp, which keeps the original's accumulated sources (Berlin: our restamp is 9% above the
  // saved air total, with cells up to 3.5x the saved value, which pushed air pollution and so land value off).
  pollution.rebuild(all);
  if (!polBaseline_.air.empty()) pollution.applySourceOffset(polBaseline_.air, polBaseline_.water, polBaseline_.garbage);
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
  // Traffic air term (spec 17 7.2.4 step 3, 18 7.1) [STATIC]: T_t is the snapshot tile value with mask 0x03, i.e. the
  // surface layer on road-flag tiles (not rail or station tiles), max with the highway layer on highway tiles.
  // DATA agrees: on Berlin's saves this input leaves the original's saved air nearly fixed over two sweeps.
  std::vector<uint8_t> airTraffic;
  if (traffic) {
    const int W = world_->W, H = world_->H;
    airTraffic.assign(size_t(W) * H, 0);
    for (int x = 0; x < W; x++)
      for (int z = 0; z < H; z++) airTraffic[size_t(x) * H + z] = uint8_t(traffic->tileValue(x, z, 0x03));
    env.traffic = &airTraffic;
  }
  env.ordinances = usOrdinances();
  if (!pollution.treatmentCapsSet()) updateTreatmentCaps();   // first run after load or new city
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
  // Spec 17 §8.4 / Q-BH / Q-BR [STATIC]: empty or kind 1 counts; network (2) and flora (5) skip the tile.
  env.tileCounts = [this](int x, int z) {
    if (!inMap(x, z)) return false;
    const size_t i = idx(x, z);
    // Network tile pieces report kind 2 (roads/rail/hwy/pipe/power/subway).
    if (transport_[i] || powerLine_[i] || pipe_[i] || (i < subway_.size() && subway_[i])) return false;
    // Flora (trees) report kind 5 — stored on the flora layer, not as Occupants.
    if (i < world_->flora.size() && world_->flora[i]) return false;
    int oi = occupantAt(x, z);
    if (oi < 0) return true;
    return occupantKind(world_->occupants[size_t(oi)].agentType) == kOccKindBuilding;
  };
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

void City::monthlyWorld() {
  // Spec 14 §6.1 item 3 / Q-BF [STATIC]: world-layer monthly (economic phase + world charts).
  // Phase/cycle modifiers also refresh at the start of monthlyUpdate (item 4); chart sample here
  // runs on the dedicated world schedule entry before the shared RCI pass.
  cal::Date d = clock.date();
  valves.setPhase(residents_);
  valves.setWorldModifiers(d.year, d.month);
  valves.worldMonthly(pop.workforceFraction(), d.month, d.year);
}

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
  // Connection counts per kind (road, rail, highway, subway) are the neighbours' connection counters (nRoad, ...), not
  // the number of connection tiles [DATA: Berlin, the 5001 and 8001 supplies of the original's day-40 save are
  // reproduced exactly (1,078,500 and 1,030,800); counting tiles gave +226,000 and +170,000].
  for (const auto& n : neighbours.neighbours) {
    in.connectionCount[0] += int(n.nRoad); in.connectionCount[1] += int(n.nRail);
    in.connectionCount[2] += int(n.nHighway); in.connectionCount[3] += int(n.nSubway);
  }
  for (const auto& kv : ordinances.catalogue()) if (ordinances.enacted(kv.first)) in.enactedOrdinances.insert(kv.first);
  in.workforceFraction = pop.workforceFraction();
  in.population = residents_;
  cal::Date d = clock.date();
  in.year = d.year; in.month = d.month;
  for (int g = 0; g < kTaxGroups; g++) in.taxRate[g] = budget.tax[g].rate;
  valves.monthlyUpdate(in);
  // City population = valve 3000 Slast (spec 20 s2) [DATA 13/13: equals the newest population-chart sample].
  residents_ = valves.v3000.Slast;
  // World chart sample moved to monthlyWorld (dedicated item 3 / Q-BF).
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
  in.roadCondition = roadCondition();
  in.ord.parkingFines = ordinances.enacted(kOrdParkingFines);
  in.ord.alternateDayDriving = ordinances.enacted(kOrdAlternateDay);
  in.ord.carpool = ordinances.enacted(kOrdCarpool);
  in.ord.crossingGuards = ordinances.enacted(kOrdCrossingGuards);
  in.ord.touristPromotion = ordinances.enacted(kOrdTourist);
  in.ord.subsidizedTransit = ordinances.enacted(kOrdSubsidizedTransit);
  in.ord.shuttleService = ordinances.enacted(kOrdShuttle);   // walking radii + ShuttleServiceDistanceToRoadAdd (spec 18 s4.3)
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
  if (out.roadConditionPercent >= 0) applyRoadCondition(out.roadConditionPercent);   // spec 18 s8.4 -> 12 s10
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
  // Conductors (spec 12 s9.1) [STATIC]: buildings, power lines and every tile zoned anything but 0 or 17 (landfill),
  // each expanded by 2 tiles.
  for (size_t i = 0; i < src.size(); i++) src[i] = powerLine_[i] || occ_[i] >= 0 || (world_->zone[i] != 0 && world_->zone[i] != 17);
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
  // Neighbour slots (spec 16 s2.6 step 2, s2.7 steps 3-4) [STATIC]: one per neighbour with a power-line connection,
  // seeded at a connection tile; a buy deal (kind 0) gives the slot 1,000,000,000 (unlimited), otherwise 0. A component
  // reaching any connection tile has an active slot and loses the 10% margin, deal or not (Madrid: the main network's
  // 204,000 capacity is exhausted at the end of row 235, the original's shipped pump cut).
  // [OPEN on our side] sell deals (kind 1) are not modelled by the power layer yet.
  for (int n = 0; n < 4; n++) {
    us::PowerDeal pd;
    if (!connectionTile(3, n, pd.tx, pd.tz)) continue;
    for (const auto& d : neighbours.active)
      if (!d.terminated() && d.dealType == 0 && d.neighbourIndex == n) pd.amount = us::kPowerBuySlot;
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
  power.rebuildConsumption(powerConsumers());
  std::vector<uint8_t> conductive;
  us::PowerEnv env = powerEnv(conductive);
  auto events = power.monthlyUpdate(env, urng_);
  {  // The deal entry's first s32 holds the reported draw (spec 19 s10.4, 10 s6.6); units are never overwritten.
    int64_t drawn = 0;
    for (const auto& d : power.deals()) if (d.active && d.amount > 0) drawn += std::max<int64_t>(0, d.amount - d.unused);
    for (auto& d : neighbours.active)
      if (!d.terminated() && d.dealType == 0) { d.penalty = int32_t(std::min<int64_t>(drawn, INT32_MAX)); break; }
  }
  // The draw (slot - left) goes to the production table's deal column. The deal's saved units are NOT overwritten:
  // Moscow keeps units 12,600 in every save of a 480-day run of the original (spec 28 s5.1) [OBSERVED].
  for (const auto& e : events) {
    int oi = occupantAt(e.x, e.z);
    if (e.kind == us::PlantEvent::Explosion) {
      if (oi >= 0) damageOccupant(oi, 8);
      plantAt_.erase({e.x, e.z});
      if (onMessage) onMessage("A power plant exploded from overwork!");
      continue;
    }
    // Spec 16 §2.6 step 4 / Q-BW [STATIC]: retire in place — same agent type, lot state 8, expired flag.
    // Lmax/Ld stay on the plant; do not remove plantAt_ (expired plants keep producing O = 0).
    if (e.kind == us::PlantEvent::Retired && oi >= 0) {
      Occupant& o = world_->occupants[size_t(oi)];
      damageOccupant(oi, 8);
      bool found = false;
      for (auto& kv : o.intProps)
        if (kv.first == 0x2351FAFB) { kv.second = 1; found = true; break; }
      if (!found) {
        o.intProps.push_back({0x2351FAFB, 1});
        std::sort(o.intProps.begin(), o.intProps.end());
      }
    }
  }
}

void City::monthlyWater() {
  // Pipe mask (spec 16 s3.4 step 3): pipe pieces plus every tile of every water facility's lot, whatever its state or
  // output [STATIC]. Berlin: non-producing pumps at (114,167), (117,146) and (119,142) link two pipe networks.
  std::vector<uint8_t> pipes(pipe_);
  for (const auto& f : water.facilities()) if (occupantAt(f.x, f.z) < 0) pipes[idx(f.x, f.z)] = 1;
  for (const auto& o : world_->occupants) {
    if (o.agentType != kAgentPump && o.agentType != kAgentTower && o.agentType != kAgentDesal) continue;
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
  // Buildings with a needs-water mark: in practice lot states 1 and 4 (spec 16 s3.4 "Needs-water marks").
  auto consumers = simOccupants(false);
  consumers.erase(std::remove_if(consumers.begin(), consumers.end(), [](const us::SimOccupant& o) {
                    const uint32_t b = o.state & ~0x200u; return b != 1 && b != 4; }), consumers.end());
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
  // Spec 16 §3.3 / Q-BW [STATIC]: newly retired water facilities → lot state 8, same agent type.
  for (const auto& f : water.facilities()) {
    if (!f.expired) continue;
    int oi = occupantAt(f.x, f.z);
    if (oi < 0) continue;
    if ((world_->occupants[size_t(oi)].state & ~0x200u) == 8) continue;
    damageOccupant(oi, 8);
  }
}

void City::monthlyPollution() {
  cal::Date d = clock.date();
  // Garbage facilities whose last trip failed route a fresh trip (spec 16 s4.3, 18 s9.2) [STATIC]. Applied to every
  // occupant of the garbage family 12005, landfill tiles (12850) included [INFERRED from DATA: Berlin's 401 landfill lots
  // all hold result "ok" in the original's saves, while the weekly trips alone fail on interior tiles].
  if (traffic)
    for (const auto& o : world_->occupants)
      if ((o.state & ~0x200u) == 1 && traffic->tripResult(o.x, o.z) == 1 && usHier_.isAncestorOrSelf(o.agentType, 12005))
        traffic->routeFacilityTrip(TripRect{o.x, o.z, o.x + o.sizeX - 1, o.z + o.sizeZ - 1});
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
  updateTreatmentCaps();
}
// Water-treatment plants' capacities (spec 16 s3.1) [STATIC]: set monthly; working = lot occupied (state 1) and the plant
// tile powered and watered; age from the build date as for recycling centres.
void City::updateTreatmentCaps() {
  std::vector<int64_t> caps;
  const int now = absMonthOf(clock);
  for (const auto& o : world_->occupants) {
    if (o.agentType != 12820) continue;   // water treatment plant (spec 17 s7.2.3 facility list)
    int built = now;
    if (o.hasDate && o.dateYear > 0) built = 12 * o.dateYear + std::max(0, std::min(11, (std::max(1, o.dateDay) - 1) * 12 / 366)) + 1;
    bool working = (o.state & ~0x200u) == 1 && power.powered(o.x, o.z) && water.tileServiced(o.x, o.z);
    caps.push_back(pollution.treatmentCapacity(working, std::max(0, now - std::min(built, now))));
  }
  pollution.setTreatmentCaps(std::move(caps));
}
void City::monthlyCrime() {
  cal::Date d = clock.date();
  crime.monthEnd(d.month, d.year);
}
void City::monthlyAura() {
  // Spec 14 §6.1 monthly item 16 / 17 §8.5 [STATIC]: mayor-rating chart sample (cityAura+127).
  cal::Date d = clock.date();
  aura.monthEnd(d.month, d.year);
}
void City::monthlyWeather() {
  // Spec 14 §6.1 monthly item 13 / Q-AT: season + temperature update (#27 Weather).
  cal::Date d = clock.date();
  weather.monthEnd(d.month, &rng_);
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
  // Station stamps use the department's saved funding from the start (spec 17 §2.2), not 100 % until the first month end:
  // Berlin is saved at 103 % police funding, and its saved crime matches only the over-funded R/Max/Min.
  if (const Department* p = budget.dept(dept::Police)) police.setFunding(p->funding);
  if (const Department* f = budget.dept(dept::Fire)) fire.setFunding(f->funding);
  tryLoad(keys::Flammability, [&](const uint8_t* p, size_t n) { flammability.parse(p, n); return true; });
  {  // spacing grid (spec 10 s6.3): 8-byte header, then (W/4)(H/4) cells, rows last-first
    spacing_.clear();
    if (const InnerRecord* r = raw.findInner(keys::Spacing)) {
      const int bw = W / 4, bh = H / 4;
      if (r->data.size() >= 8 + size_t(bw) * bh) {
        spacing_.assign(size_t(bw) * bh, 0);
        for (int bx = 0; bx < bw; bx++)
          for (int bz = 0; bz < bh; bz++) spacing_[size_t(bx) * bh + bz] = r->data[8 + size_t(bw - 1 - bx) * bh + bz];
      }
    }
  }
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
  if (!spacing_.empty())
    if (const InnerRecord* r = st.findInner(keys::Spacing)) {
      Bytes d = r->data;
      const int bw = w.W / 4, bh = w.H / 4;
      if (d.size() >= 8 + size_t(bw) * bh)
        for (int bx = 0; bx < bw; bx++)
          for (int bz = 0; bz < bh; bz++) d[8 + size_t(bw - 1 - bx) * bh + bz] = spacing_[size_t(bx) * bh + bz];
      st.setInner(keys::Spacing, std::move(d));
    }
}

}  // namespace m3k
