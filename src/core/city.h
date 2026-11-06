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
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "core/clock.h"
#include "core/weather.h"
#include "core/placement_query.h"
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
  bool useTripFailure = true;            // feed TrafficSim trip results into the developers ("unfit", spec 18 s9.2)

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
  // After loading a city / external edits to World. network=false skips network reclassification and
  // netDirty_ for edits that only touch occupants (so the throttled refresh() is not forced every day).
  void rebuildIndex(bool network = true);

  // ---- time ----
  // Advance one calendar day through the clock; runs daily/weekly/monthly steps as dates demand.
  void tickDay();
  int daysElapsed() const { return int(sched.elapsedDays); }

  // ---- player actions (costs charged to budget) ----
  bool paintZoneType(int x0, int z0, int x1, int z1, int zoneType);  // 0 = dezone
  bool plop(uint32_t occId, int x, int z, int cost);
  bool plopAgent(uint32_t occId, uint32_t agentType, int sizeX, int sizeZ, int x, int z, int cost);
  bool buildNetwork(NetworkKind k, const std::vector<std::pair<int, int>>& path);
  // Spec 12 §6.2 drag router (highway dual carriageway, per-tool water/slope). Empty when tiling is off.
  std::vector<std::pair<int, int>> routeNetworkPath(NetworkKind k, int x0, int z0, int x1, int z1) const;
  // Text dump of the routed drag (every tile with carriage, validity, corner heights, water) for the probes.
  std::string describeRoute(NetworkKind k, int x0, int z0, int x1, int z1) const;
  // The last routeNetworkPath result: its valid tiles and the number of tiles of the whole route. A blocked drag still pays for every routed tile
  // (observed), so quoteNetwork / buildNetwork add the invalid tail when handed exactly this path.
  mutable std::vector<std::pair<int, int>> lastRouteValid_;
  mutable int lastRouteTotal_ = 0;
  mutable std::pair<int, int> lastRouteStart_{0, 0}, lastRouteEnd_{0, 0};
  mutable int lastRouteKind_ = 0;
  // Spec 12 §7: highway on-ramp point tool (menu 02B5DEBB). Places the 3×2 piece group.
  bool buildOnRamp(int ax, int az);
  // Spec 12 §6.4 / §8: first yes/no confirm a drag would raise (bridge, then tunnel, then connection).
  enum class NetworkConfirmKind { None, Bridge, Tunnel, Connection };
  struct NetworkConfirm {
    NetworkConfirmKind kind = NetworkConfirmKind::None;
    int price = 0;
    bool powerBridge = false;   // bridge text 0x2D2 vs 0x28B
  };
  NetworkConfirm peekNetworkConfirm(NetworkKind k, const std::vector<std::pair<int, int>>& path) const;
  // When set, ToolHost::confirmConnection returns this value (Game uses it after the yes/no dialog).
  // Nullopt = accept (tests, scripts, and non-interactive hosts).
  std::optional<bool> connectionConfirmOverride;
  bool bulldoze(int x0, int z0, int x1, int z1);

  // ---- quotes (spec 09 5.1): dry-run of the player actions. valid=false: nothing in the area is legal.
  struct Quote { bool valid = false; int64_t cost = 0; };
  Quote quoteZone(int x0, int z0, int x1, int z1, int zoneType) const;
  Quote quotePlop(uint32_t occId, int x, int z) const;
  Quote quoteBulldoze(int x0, int z0, int x1, int z1) const;
  Quote quoteNetwork(NetworkKind k, const std::vector<std::pair<int, int>>& path) const;
  Quote quoteOnRamp(int ax, int az) const;
  // Preview tiles for an on-ramp at the cursor (empty when the plan fails).
  std::vector<std::pair<int, int>> onRampPreview(int ax, int az) const;
  // Footprint of a building (1x1 when unknown).
  void footprint(uint32_t occId, int& sx, int& sz) const;

  // Reason the last player action failed (for the error sound): 0 none/invalid, 1 not enough funds.
  bool lastFailNoMoney = false;

  // ---- queries ----
  // Any surface transport (road / rail / highway). Name is historical; prefer hasRoadPiece for road-only.
  bool isRoadTile(int x, int z) const { return inMap(x, z) && transport_[idx(x, z)]; }
  // Spec 13 §3.1a bit 0x8000: a road (or road-crossing) network piece is present.
  bool hasRoadPiece(int x, int z) const;
  bool isPowerLine(int x, int z) const { return inMap(x, z) && powerLine_[idx(x, z)]; }
  bool isPipe(int x, int z) const { return inMap(x, z) && pipe_[idx(x, z)]; }
  int occupantAt(int x, int z) const { return inMap(x, z) ? occ_[idx(x, z)] : -1; }
  int trafficDensityAt(int x, int z) const;     // live traffic (0..255): strongest of surface, highway and subway at the tile
  int trafficDensityAverage() const;            // mean live density over network tiles that carry any
  const us::AgentHierarchy& agentHierarchy() const { return usHier_; }
  const AgentHierarchy& economyHierarchy() const { return ecoHier_; }   // the hierarchy the budget's placement costs use
  int occupantIndexByUid(uint32_t uid) const {   // -1 when the occupant is gone
    if (!world_ || !uid) return -1;
    for (size_t i = 0; i < world_->occupants.size(); i++) if (world_->occupants[i].uid == uid) return int(i);
    return -1;
  }
  int roadCount() const;
  // Road condition X (spec 12 s10) [STATIC]: potholed road pieces (IDs 15040/15041/15042/9051) / road pieces; NaN with no
  // road pieces (0/0, unguarded in the original).
  double roadCondition() const;
  // New road condition from the traffic model (spec 18 s8.4, 12 s10) [STATIC]: potholes road pieces or repairs them.
  void applyRoadCondition(int percent);
  int population() const { return residents_; }
  bool powered(int x, int z) const { return power.powered(x, z); }
  bool watered(int x, int z) const { return water.tileServiced(x, z); }
  bool usingTilingRules() const { return cfg_.haveTiling; }
  uint32_t kindOf(const NetPiece& p) const;
  // True when the piece id belongs to the tiling set of the network kind (crossings belong to every kind they contain).
  bool pieceInKind(uint32_t pieceId, NetworkKind k) const;
  void configureMap(class WorldNetworkMap& map) const;   // exit function from the occupants' connection matrices   // NetworkKind value of a surface piece, 0 = unknown

  // ---- subsystems (public: state classes) ----
  Clock clock;
  CitySchedule sched;
  Weather weather;   // subsystem #27 (spec 14 §6.1 monthly item 13); no save records
  PlacementQuery placement;  // subsystem #28 (spec 14 §6.1); on-demand only, no save records
  // Spec 14 §2.3 / §6.3 rule 6 / Q-AZ [STATIC]: two nestable runtime city counts. Hold = either non-zero.
  // Pure runtime — not saved, no INI. Busy (monthly / backlog) is separate and does not feed hold.
  // raisePause: ordinary player pause (bottom-bar / hotkey); resume + non-zero speed clear it.
  // raisePending: modal windows, save/load, scenario UI, terrain editor, other blocking paths.
  // `announce` would fire onMessage("simulation paused"); the shell leaves it false — the ticker
  // carries the pause notice (no dialog in the original).
  void raisePause(bool announce = false);
  void resumePause();
  void raisePending();
  void clearPending();
  int cityPauseCount() const { return cityPauseCount_; }
  int cityPendingCount() const { return cityPendingCount_; }
  bool hold() const { return cityPauseCount_ > 0 || cityPendingCount_ > 0; }
  Budget budget;
  Ordinances ordinances;
  NeighbourDeals neighbours;
  BusinessDeals business;
  Population pop;
  growth::ValveLayer valves;
  growth::LandValueLayer landValue;
  // Cheats `Salt on` / `Salt off` (spec 11 4.8 [STATIC]): off clears the sea-water flag of every tile; on recomputes the flags from
  // the terrain (sea edges = more than 60% wet, a walk inward of a quarter of the side). Altitudes and water never change.
  void applySaltCheat(bool on);
  // Cheat `nerdz rool` (spec 27 s5.3): new industry is always clean/high-tech after the agriculture check.
  void setCleanIndustryOnly(bool on) { cfg_.growth.cleanIndustryOnly = on; }
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
  const World* world() const { return world_; }
  void markOccupantsDirty() { occDirty_ = true; }
  int jailCapacity() const;
  void recomputeLandValue();   // recompute every land-value block now (tools / tests)
  void lvCentre(int& cx, int& cz) const;
  std::string lvExplain(int x, int z) const;   // city centre used by the land-value pass (tools)
  void runMonthlyWater() { monthlyWater(); }   // one water update now (tools)
  us::OrdinanceSet crimeOrdinances() const { return usOrdinances(); }   // tools
  std::vector<us::SimOccupant> powerConsumers() const;   // spec 16 s2.6: per-record attribute 0xC9
  // A building burns (16) or becomes rubble (8): the occupant is replaced, so a lot that counted in the committed demand
  // P (state 1 or 4) leaves it (spec 15 s4 "Committed demand") [INFERRED, medium-high].
  void damageOccupant(int index, uint32_t newState);
  void retireAged(size_t occupantIndex, int ageMonths, int maxLifespanMonths);   // recycling / treatment replacement at MaxLifespan
  // A connection tile of saved set `setIndex` (rail, road, pipe, power, highway, subway) to neighbour `neighbour`.
  bool connectionTile(int setIndex, int neighbour, int& tx, int& tz) const;
  std::optional<size_t> evaluateBusinessDeals(const BusinessDeals::Inputs& in) { return business.weeklyEvaluate(in, rng_); }
  // Fall-out marks (disaster MarkRadiation): the tile blocks development and lowers land value. Session-only, not saved.
  void markRadiation(int x, int z) { if (inMap(x, z)) radiation_[idx(x, z)] = 1; }
  bool radioactiveAt(int x, int z) const { return inMap(x, z) && idx(x, z) < radiation_.size() && radiation_[idx(x, z)]; }
  void markNetworkDirty() { netDirty_ = true; }

  // Taxes: all three groups follow `rate` (the simplified UI has one slider).
  void setTaxRate(int rate);

  // Sum of every occupant's footprint tiles, filler lots included: the quantity W behind the world-based ordinance costs (spec 19 s9.5
  // [INFERRED]; Berlin 25,598 gives Power Conservation 11, Smoke Detectors 27, Earthquake 29 as in the original).
  int64_t footprintTiles() const;
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
  // A building occupant was added to the city (player plop, growth, pier): agent type, count of that type afterwards,
  // anchor tile (spec 22 s4.7.1 building news).
  std::function<void(uint32_t agentType, int countAfter, int x, int z)> onBuildingAdded;

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
  void removeOccupantIdx(const std::set<int>& kill, bool network = true);
  void classifyNetwork();
  void rebuildTraffic();
  void setFallbackRoadPiece(int x, int z);
  bool buildNetworkFallback(NetworkKind k, const std::vector<std::pair<int, int>>& path);
  int zoneCost(int zoneType) const;
  int64_t spendableFunds() const { return budget.funds; }

  // schedule handlers
  void dailyPolice(); void dailyFire(); void dailyLandValue(); void dailyZoneDev(); void dailyPollution();
  void dailyCrime(); void dailyFlammability(); void dailyAura(); void weeklyTraffic();
  void monthlyNeighbours(); void monthlyWorld(); void monthlyValves(); void monthlyOrdinances();
  void monthlyPopulation();
  void monthlyTraffic(); void monthlyBudget(); void monthlyPower(); void monthlyWater();
  void monthlyPollution(); void monthlyCrime(); void monthlyAura(); void monthlyWeather();
  void updateTreatmentCaps(bool monthly = false);
 public:
  // Spacing grid 01FD7A8C (spec 10 s6.3): one byte per 4 x 4 block; bits 2-5 = overlap counter. add(): if any block of
  // the 5 x 5-block neighbourhood has 15, refuse; else +1 on all of them. rem(): c ← c−1 on the same 25 blocks (no
  // underflow check — the nibble wraps). Segment bit 0 and point bit 1: airport runway / building clients.
  bool spacingAdd(int x, int z);
  void spacingRem(int x, int z);
  bool spacingReserveSegment(int x0, int z0, int x1, int z1);
  void spacingReleaseSegment(int x0, int z0, int x1, int z1);
  bool spacingSegmentBlock(int x, int z) const;   // test-block: segment flag of the tile's 4×4 block
  bool spacingReservePoint(int x, int z);
  void spacingReleasePoint(int x, int z);
  bool spacingPointNear(int x, int z) const;      // test-point: any block in the 3×3 neighbourhood
 private:

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
  std::map<uint32_t, int> tripSlot_;              // trip week slot per occupant type (spec 18 s4.1)
  std::vector<uint32_t> hwyMatrix_;               // per tile: surface piece's 0x136 connection matrix (spec 18 s5.4)
  std::vector<uint8_t> hwyRot_, hwyPiece_;         // per tile: piece rotation, piece-with-matrix marker
  std::vector<uint16_t> span_;                     // per tile: span record (dir | len << 2) of a surface piece, 0 = none
  std::vector<uint8_t> spacing_;                   // spacing grid cells, index bx * (H/4) + bz (empty: no record)

  std::vector<int32_t> occ_;
  std::vector<uint8_t> transport_, powerLine_, pipe_, subway_;
  bool occDirty_ = true, netDirty_ = true;
  int residents_ = 0;
  uint32_t nextUid_ = 1;
  std::vector<uint8_t> radiation_;
  us::PollutionLayer::SourceGrids polBaseline_;   // saved minus restamped pollution sources at load
  std::vector<int16_t> crimeBaseline_;            // saved minus restamped crime potential at load
  std::vector<uint8_t> loadedAirport_;            // zone-14 tiles of airports that existed at load (spec 15 s7 interim)
  int countResidents() const;
  std::vector<growth::ValveBuilding> valveBuildings() const;
  std::map<std::pair<int, int>, int> plantAt_, waterFacAt_;      // anchor -> layer id
  std::map<std::pair<int, int>, std::pair<int, int>> stationAt_;  // anchor -> (kind 0 police 1 fire 2 jail, id)
  int roadUpkeep_ = 0, transitUpkeep_ = 0;
  int lastTaxRate_ = -1;
  std::set<ResKey> preserved_;   // records that failed to parse: keep raw
  int refreshSkips_ = 0;
  int cityPauseCount_ = 0;
  int cityPendingCount_ = 0;
};

}  // namespace m3k
