// Simplified city simulation: zoning, road network, demand valves, zone developers, economy.
// Data-driven from the user's game files (spec/15-sim-growth.md, 12-networks.md). Deviations from
// the original are listed in STATUS.md.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "core/gamedata.h"
#include "core/world.h"

namespace m3k {

struct ZoneCategory { uint32_t agentType = 0; char cls = 'N'; int density = 1, maxLV = 255, minLV = 0; };
struct ZoneDesc { int zone = 0; uint32_t baseType = 0; std::vector<ZoneCategory> cats; std::set<int> families; };
struct FamilyMember { uint32_t type = 0, group = 0, instance = 0; int weight = 1; };
struct Family { int id = 0; uint32_t landUse = 0; std::vector<FamilyMember> members; };
struct ValveEffect { int valve = 0, supply = 0, demand = 0; };

// Everything the simulation reads from game data. Fill it with `load` or the parse helpers (tests).
struct SimData {
  std::map<int, ZoneDesc> zones;
  std::map<int, Family> families;
  std::map<uint32_t, std::vector<ValveEffect>> valveEffects;
  std::map<uint32_t, uint32_t> parent;  // agent type hierarchy child -> parent
  std::map<int, uint32_t> roadFinal;    // 4-bit mask -> packed piece (id << 8 | rot)
  std::map<std::string, int> constructionSites;  // "size:agentType" -> occupant id (state 4 appearance)
  int noServicePercentage = 20, maxDistFromTransport = 3, constructionTimeFactor = 60;
  int jobToResFactor = 1, externalJobDemand = 600;
  int phaseCommercial = 1, phaseIndustrial = 1;

  bool load(GameData& g);
  void parseZoneDescriptions(const IniFile& ini);
  void parseZoneRules(const IniFile& ini);
  void parseValveEffects(const IniFile& ini);
  void parseHierarchy(const IniFile& ini);
  void parseFamilyText(int id, const std::string& text);
  void parseRuleFinal(const std::string& text);  // TilingRules *_final.txt
  const std::vector<ValveEffect>* effectsFor(uint32_t agentType) const;
};

enum class Tool { None, ZoneRL, ZoneRM, ZoneRH, ZoneCL, ZoneCM, ZoneCH, ZoneIL, ZoneIM, ZoneIH, Dezone, Road, Bulldoze };
int zoneTypeForTool(Tool t);
const char* toolName(Tool t);

struct Valve { int supply = 0, demand = 0, value = 0; };

class Sim {
 public:
  void init(World* w, GameData* g, const SimData* d, uint32_t seed = 1);
  void rebuildIndex();  // after loading a city / external changes

  // Player actions (return false if rejected). Costs are charged to funds.
  bool paintZone(int x0, int z0, int x1, int z1, Tool t);
  bool paintZoneType(int x0, int z0, int x1, int z1, int zoneType);  // 0 = dezone
  // Place a single building by occupant id (civic/landmark plop). Returns false if blocked/unaffordable.
  bool plop(uint32_t occupantId, int x, int z, int cost);
  int occupantSizeX(uint32_t occId) const;
  bool buildRoad(const std::vector<std::pair<int, int>>& path);
  bool bulldoze(int x0, int z0, int x1, int z1);

  void tickDay();
  void tickMonth();

  // Queries
  bool isRoadTile(int x, int z) const { return world_->inTiles(x, z) && road_[world_->tIdx(x, z)]; }
  int occupantAt(int x, int z) const { return world_->inTiles(x, z) ? occ_[world_->tIdx(x, z)] : -1; }
  int population() const { return population_; }
  int64_t funds = 20000;
  int day = 0;  // days since start
  int taxRate = 7;
  Valve valveR, valveC, valveI;
  int roadCount() const;

 private:
  uint32_t rnd();
  int rnd(int n) { return n <= 0 ? 0 : int(rnd() % uint32_t(n)); }
  bool flat(int x, int z) const;
  bool tileBuildable(int x, int z) const;
  bool hasAccess(int x, int z, int dist) const;
  void visitTile(int x, int z);
  bool startDevelopment(int x, int z, const ZoneDesc& zd, const ZoneCategory& cat);
  const ZoneCategory* pickCategory(const ZoneDesc& zd, int lv, char cls = 0) const;
  void removeOccupant(int idx);
  int addOccupant(const Occupant& o);
  void retile(int x, int z);
  void setRoadPiece(int x, int z);
  uint32_t valveFor(int zone) const;

  World* world_ = nullptr;
  GameData* game_ = nullptr;
  const SimData* data_ = nullptr;
  uint32_t seed_ = 1;
  std::vector<int32_t> occ_;     // tile -> occupant index or -1
  std::vector<uint8_t> road_;    // tile -> road present
  std::vector<int32_t> surf_;    // tile -> index into world.surface or -1
  std::vector<uint8_t> dead_;    // tombstones for world.occupants
  std::vector<uint16_t> scan_;   // shuffled tile visit order
  size_t scanPos_ = 0;
  int population_ = 0;
  int committedR_ = 0, committedC_ = 0, committedI_ = 0;
};

}  // namespace m3k
