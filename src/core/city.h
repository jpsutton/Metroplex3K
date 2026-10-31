// City: owns every World-attached simulation subsystem and runs them in the order of
// spec/14-sim-clock.md (CitySchedule daily / weekly / monthly cascades). Also loads each layer from the
// city file's inner records (World::raw) and writes them back (City::flush).
//
// Layout: Sim (sim.h) is the thin player-facing facade used by Game; City does the work.
// Zone development / valves / land value are the growth modules (growth.h, valves.h, landvalue.h)
// reached through the GrowthCity / LandValueCity adapters defined in city.cpp.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "core/clock.h"
#include "core/economy.h"
#include "core/gamedata.h"
#include "core/growth.h"
#include "core/landvalue.h"
#include "core/population.h"
#include "core/services.h"
#include "core/simcommon.h"
#include "core/tiling_rules.h"
#include "core/traffic.h"
#include "core/utilities.h"
#include "core/valves.h"
#include "core/world.h"

namespace m3k {

// Network kinds the player can lay (values equal NetKind of tiling_rules.h).
enum class NetworkKind { Road = 1, Rail = 2, Power = 3, Highway = 4, Pipe = 5, Subway = 6 };

// Pieces used by the fallback network placement (no TilingRules files available).
constexpr uint32_t kFallbackPowerPiece = 5000, kFallbackPipePiece = 5001;

// Every INI the city subsystems read. All optional: an empty IniFile yields neutral defaults, then
// CityConfig::applySyntheticDefaults() fills the values the tests / a data-less run need.
struct CityIniSet {
  IniFile powerLayer, waterLayer, budgetLayer, advisorBudget, tune, crime, pollution, police, fire, aura,
      traffic, resLayer, comLayer, indLayer, citySim, zoneLayer, valveLayer, worldLayer, landValue,
      neighbourDeals, ordinances;
  void load(GameData& g);
};

struct CityConfig {
  CityIniSet ini;
  growth::GrowthConfig growth;
  growth::ValveTuning valve;
  growth::LandValueTuning landValue;
  std::map<uint32_t, uint32_t> parent;   // agent type hierarchy child -> parent
  std::map<int, uint32_t> roadFinal;     // fallback road tiling: 4-bit mask -> packed piece (id << 8 | rot)
  TilingRuleSet tiling;                  // network tiling rules (Res/TilingRules)
  bool haveTiling = false;
  bool synthetic = true;                 // true until load() found game data
  bool useTripFailure = false;           // feed TrafficSim trip results into the developers ("unfit")

  CityConfig();
  // Reads all INIs, zone/family data, tiling rules. Returns true if zone data was found.
  bool load(GameData& g);
  // Family text (Res: 81F53D09:01F58D3D:<id>) bound to zone rules, see parseRuleBindings.
  void parseFamilyText(int id, const std::string& text, GameData* g = nullptr);
  void parseRuleFinal(const std::string& text);
  // Zone-rule bindings: zone -> rule -> family ids; families are added to growth.rules lists.
  void bindFamilies(const std::vector<growth::RuleBinding>& bindings, GameData* g = nullptr);
  // Copy valve effects of ancestors to leaf agent types used by categories / families.
  void expandEffects();
  // Fill defaults for data that is only available with a game install.
  void applySyntheticDefaults();
  // Minimal built-in zone/family/valve data so a city can grow without any game files (demo, headless tests).
  void applyDemoData();

  struct FamilyMember { uint32_t type = 0, group = 0, instance = 0; int weight = 1; };
  struct FamilyDef { int id = 0; uint32_t landUse = 0; std::vector<FamilyMember> members; };
  std::map<int, FamilyDef> families;
  std::vector<growth::RuleBinding> bindings;
};

class City {
 public:
  City();
  ~City();
  City(const City&) = delete;
  City& operator=(const City&) = delete;

  // `g` may be null (no game data). `cfg` is copied. Loads every layer from w->raw when present.
  void init(World* w, GameData* g, const CityConfig& cfg, uint32_t seed = 1);
  void rebuildIndex();   // after loading a city / external edits to World

  // ---- time ----
  // Advance one calendar day through the clock; runs daily/weekly/monthly steps as dates demand.
  void tickDay();
  int daysElapsed() const { return int(sched.elapsedDays); }

  // ---- player actions (costs charged to budget) ----
  bool paintZoneType(int x0, int z0, int x1, int z1, int zoneType);  // 0 = dezone
  bool plop(uint32_t occId, int x, int z, int cost);
  bool plopAgent(uint32_t occId, uint32_t agentType, int sizeX, int sizeZ, int x, int z, int cost);
  bool buildNetwork(NetworkKind k, const std::vector<std::pair<int, int>>& path);
  bool bulldoze(int x0, int z0, int x1, int z1);

  // ---- quotes (spec 09 5.1): dry-run of the player actions. valid=false: nothing in the area is legal.
  struct Quote { bool valid = false; int64_t cost = 0; };
  Quote quoteZone(int x0, int z0, int x1, int z1, int zoneType) const;
  Quote quotePlop(uint32_t occId, int x, int z) const;
  Quote quoteBulldoze(int x0, int z0, int x1, int z1) const;
  Quote quoteNetwork(NetworkKind k, const std::vector<std::pair<int, int>>& path) const;
  // Footprint of a building (1x1 when unknown).
  void footprint(uint32_t occId, int& sx, int& sz) const;

  // Reason the last player action failed (for the error sound): 0 none/invalid, 1 not enough funds.
  bool lastFailNoMoney = false;

  // ---- queries ----
  bool isRoadTile(int x, int z) const { return inMap(x, z) && transport_[idx(x, z)]; }
  bool isPowerLine(int x, int z) const { return inMap(x, z) && powerLine_[idx(x, z)]; }
  bool isPipe(int x, int z) const { return inMap(x, z) && pipe_[idx(x, z)]; }
  int occupantAt(int x, int z) const { return inMap(x, z) ? occ_[idx(x, z)] : -1; }
  int trafficDensityAt(int x, int z) const;     // live traffic (0..255): strongest of surface, highway and subway at the tile
  int trafficDensityAverage() const;            // mean live density over network tiles that carry any
  const us::AgentHierarchy& agentHierarchy() const { return usHier_; }
  int occupantIndexByUid(uint32_t uid) const {   // -1 when the occupant is gone
    if (!world_ || !uid) return -1;
    for (size_t i = 0; i < world_->occupants.size(); i++) if (world_->occupants[i].uid == uid) return int(i);
    return -1;
  }
  int roadCount() const;
  int population() const { return residents_; }
  bool powered(int x, int z) const { return power.powered(x, z); }
  bool watered(int x, int z) const { return water.tileServiced(x, z); }
  bool usingTilingRules() const { return cfg_.haveTiling; }
  uint32_t kindOf(const NetPiece& p) const;   // NetworkKind value of a surface piece, 0 = unknown

  // ---- subsystems (public: state classes) ----
  Clock clock;
  CitySchedule sched;
  Budget budget;
  Ordinances ordinances;
  NeighbourDeals neighbours;
  BusinessDeals business;
  Population pop;
  growth::ValveLayer valves;
  growth::LandValueLayer landValue;
  bool saltOff = false;   // cheat `Salt off`: every water body counts as fresh
  growth::ZoneManager zones;
  us::PowerLayer power;
  us::WaterLayer water;
  us::CoverageLayer police, fire;
  us::CrimeLayer crime;
  us::PollutionLayer pollution;
  us::AuraLayer aura;
  us::FlammabilityLayer flammability;
  std::unique_ptr<TrafficSim> traffic;
  const CityConfig& config() const { return cfg_; }
  GameData* gameData() const { return game_; }
  World* world() { return world_; }
  void markOccupantsDirty() { occDirty_ = true; }
  int jailCapacity() const;
  void recomputeLandValue();   // recompute every land-value block now (tools / tests)
  void lvCentre(int& cx, int& cz) const;
  std::string lvExplain(int x, int z) const;   // city centre used by the land-value pass (tools)
  void runMonthlyWater() { monthlyWater(); }   // one water update now (tools)
  // A building burns (16) or becomes rubble (8): the occupant is replaced, so a lot that counted in the committed demand
  // P (state 1 or 4) leaves it (spec 15 s4 "Committed demand") [INFERRED, medium-high].
  void damageOccupant(int index, uint32_t newState);
  // A connection tile of saved set `setIndex` (rail, road, pipe, power, highway, subway) to neighbour `neighbour`.
  bool connectionTile(int setIndex, int neighbour, int& tx, int& tz) const;
  std::optional<size_t> evaluateBusinessDeals(const BusinessDeals::Inputs& in) { return business.weeklyEvaluate(in, rng_); }
  // Fall-out marks (disaster MarkRadiation): the tile blocks development and lowers land value. Session-only, not saved.
  void markRadiation(int x, int z) { if (inMap(x, z)) radiation_[idx(x, z)] = 1; }
  bool radioactiveAt(int x, int z) const { return inMap(x, z) && idx(x, z) < radiation_.size() && radiation_[idx(x, z)]; }
  void markNetworkDirty() { netDirty_ = true; }

  // Taxes: all three groups follow `rate` (the simplified UI has one slider).
  void setTaxRate(int rate);

  void flush() { if (world_) flush(*world_); }

  // ---- persistence ----
  // Writes every live layer record into w.raw (created when absent). Layers whose stored record could
  // not be parsed keep their raw record. Call before saveCity().
  void flush(World& w);

  // ---- hooks ----
  std::function<void(int month, int year)> onMonthEnd;
  std::function<void(const std::string&)> onMessage;
  // Sound event ids (AUDIO.INI ids) raised by tools; Game maps them to AudioSystem::request.
  std::function<void(int soundId)> onSound;

  // Multi-line human-readable diagnostics: valves, buildings per class/state, power, zone-manager results.
  std::string debugStats() const;

  int64_t lastPowerProduced() const { return power.stats().produced; }

 private:
  friend class GrowthAdapter;
  friend class LandValueAdapter;
  bool inMap(int x, int z) const { return world_ && world_->inTiles(x, z); }
  size_t idx(int x, int z) const { return world_->tIdx(x, z); }
  void wire();
  void initLayers(bool fromRecords);
  void loadRecords();
  void refresh(bool force);  // rebuild occupant-derived layer state when dirty (throttled unless forced)
  void syncFacilities();   // plants, water facilities, police/fire stations from occupants
  std::vector<us::SimOccupant> simOccupants(bool operatingOnly) const;
  us::OrdinanceSet usOrdinances() const;
  us::PowerEnv powerEnv(std::vector<uint8_t>& conductive) const;
  bool tileBuildable(int x, int z) const;
  bool flatTile(int x, int z) const;
  int addOccupant(const Occupant& o);
  void removeOccupantIdx(const std::set<int>& kill);
  void classifyNetwork();
  void rebuildTraffic();
  void setFallbackRoadPiece(int x, int z);
  bool buildNetworkFallback(NetworkKind k, const std::vector<std::pair<int, int>>& path);
  int zoneCost(int zoneType) const;
  int64_t spendableFunds() const { return budget.funds; }

  // schedule handlers
  void dailyPolice(); void dailyFire(); void dailyLandValue(); void dailyZoneDev(); void dailyPollution();
  void dailyCrime(); void dailyFlammability(); void dailyAura(); void weeklyTraffic();
  void monthlyNeighbours(); void monthlyValves(); void monthlyOrdinances(); void monthlyPopulation();
  void monthlyTraffic(); void monthlyBudget(); void monthlyPower(); void monthlyWater();
  void monthlyPollution(); void monthlyCrime();

  World* world_ = nullptr;
  GameData* game_ = nullptr;
  CityConfig cfg_;
  uint32_t seed_ = 1;
  std::unique_ptr<class GrowthAdapter> growthView_;
  std::unique_ptr<class LandValueAdapter> lvView_;
  growth::SimRand zoneRng_;
  growth::CrtRand zoneCrt_;
  SimRng rng_{1};
  us::SimRng urng_{1};
  us::AgentHierarchy usHier_;
  AgentHierarchy ecoHier_;
  std::vector<TripBuilding> tripBuildings_;

  std::vector<int32_t> occ_;
  std::vector<uint8_t> transport_, powerLine_, pipe_;
  bool occDirty_ = true, netDirty_ = true;
  int residents_ = 0;
  uint32_t nextUid_ = 1;
  std::vector<uint8_t> radiation_;
  int countResidents() const;
  std::vector<growth::ValveBuilding> valveBuildings() const;
  std::map<std::pair<int, int>, int> plantAt_, waterFacAt_;      // anchor -> layer id
  std::map<std::pair<int, int>, std::pair<int, int>> stationAt_;  // anchor -> (kind 0 police 1 fire 2 jail, id)
  int roadUpkeep_ = 0, transitUpkeep_ = 0;
  int lastTaxRate_ = -1;
  std::set<ResKey> preserved_;   // records that failed to parse: keep raw
  int refreshSkips_ = 0;
};

}  // namespace m3k
