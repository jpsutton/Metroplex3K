// Power, water and garbage (spec/16-sim-utilities.md) plus the byte-exact power/water records
// (spec/10-save-format.md section 6.14). Layers work on abstract inputs; no world dependency.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <vector>
#include "core/simcommon.h"

namespace m3k { namespace us {

// Months since year 0: 12*year + month (spec 16 section 1.2).
inline int absMonth(int year, int month) { return 12 * year + month; }

// ======================= Power =======================
struct PlantTunables {
  int capacity = 0;        // [PowerPlantCapacities]
  int maxLife = 1;         // MaxLifeSpan (0 -> 1)
  int declineAge = 0;      // DeclineAge (>= maxLife -> maxLife/2)
  int variationPct = 0;    // PercentageVariationInLongevity
  const char* section = "";
};

// Plant agent types in category order (columns of the production table): 12111..12118, 12853.
// Category 9 is the neighbour deal column.
const std::vector<uint32_t>& powerPlantTypes();
int powerCategory(uint32_t agentType);  // -1 if not a plant type

struct PowerParams {
  std::map<uint32_t, PlantTunables> plants;
  int savingConservationPct = 0, savingStairwellPct = 0;
  int minOverwork = 18, maxOverwork = 30;  // shipped values [INFERRED]; INI overrides
  int wteOptimalGarbageCap = 0;            // [WasteToEnergyPowerPlant] OptimalGarbageCap
  int floodPassCap = 600;                  // spec 2.6; only documented, BFS used (see .cpp)
  static PowerParams fromIni(const IniFile& powerLayerIni);
};

struct PlantState {
  int id = 0;
  uint32_t agentType = 0;
  int x = 0, z = 0;
  int capacity = 0, output = 0, load = 0;
  uint16_t overwork = 0;
  int buildAbsMonth = 0;
  int lmax = 1, ld = 0;
  bool warned = false, expired = false;
};

// Output with age (spec 2.4), unsigned arithmetic.
uint32_t plantOutput(uint32_t cap, int age, int lmax, int ld);
// Lifespan randomisation (spec 2.3). Writes lmax/ld.
void rollLifespan(const PlantTunables& t, SimRng& rng, int& lmax, int& ld);

struct PowerDeal {          // one neighbour slot (up to 4)
  int tx = -1, tz = -1;     // connection tile; negative = no connection
  int amount = 0;           // purchased amount for the month (0 = no deal)
  bool reached = false, active = false;
  int64_t unused = 0;       // purchased power reported unused after distribution
};

struct PowerEnv {
  const std::vector<uint8_t>* conductive = nullptr;  // x*H+z, nonzero = line/conductive building/zone
  OrdinanceSet ordinances;
  int nowAbsMonth = 0;
  std::vector<PowerDeal> deals;                      // at most 4 are used
};

struct PlantEvent {
  enum Kind { Explosion, Retired } kind;
  int plantId; uint32_t agentType; int x, z;
};

struct PowerStats {
  int64_t produced = 0, consumed = 0;
  int consumerTiles = 0, poweredConsumerTiles = 0;
  int spareCapacityPct = 0, unpoweredConsumers = 0, ageingPlants = 0;
};

class PowerLayer {
 public:
  PowerLayer() = default;
  PowerLayer(int W, int H, PowerParams p) { init(W, H, std::move(p)); }
  void init(int W, int H, PowerParams p);

  int addPlant(int x, int z, uint32_t agentType, int buildAbsMonth, SimRng& rng);  // returns id (-1 if unknown type)
  void restorePlant(const PlantState& p) { plants_.push_back(p); if (p.id >= nextId_) nextId_ = p.id + 1; }
  bool removePlant(int id);
  const std::vector<PlantState>& plants() const { return plants_; }
  std::vector<PlantState>& plants() { return plants_; }

  // Per-tile consumption byte, written at an occupant's anchor only.
  void setConsumption(int x, int z, int c) { cons_[size_t(x) * H_ + z] = uint8_t(clampi(c, 0, 255)); }
  void clearConsumption() { std::fill(cons_.begin(), cons_.end(), 0); }
  // Spec 16 open question / spec 29 2.8: use attribute value if set, else
  // densityClass * footprint tiles (one unit per lot tile).
  void rebuildConsumption(const std::vector<SimOccupant>& occ);

  // Monthly step (spec 2.6). Returns explosion/retirement events (explosions already removed).
  std::vector<PlantEvent> monthlyUpdate(const PowerEnv& env, SimRng& rng);

  bool powered(int x, int z) const { return x >= 0 && z >= 0 && x < W_ && z < H_ && powered_[size_t(x) * H_ + z]; }
  const std::vector<uint8_t>& poweredGrid() const { return powered_; }
  uint8_t consumption(int x, int z) const { return cons_[size_t(x) * H_ + z]; }
  const PowerStats& stats() const { return stats_; }
  const PowerParams& params() const { return params_; }
  const std::vector<PowerDeal>& deals() const { return deals_; }

  // ---- record 206C6E7C:E0AFDF68:0 (668 bytes) ----
  int32_t monthIndex = 0;
  int32_t production[12][10] = {};
  int32_t annualTotal[10] = {};
  Chart chart = Chart::fresh(0, 10);
  uint32_t tail = 0;
  Bytes serialize() const;
  void parse(const uint8_t* p, size_t n);   // throws FormatError

 private:
  void distribute(int sx, int sz, const PowerEnv& env, std::vector<uint8_t>& visited,
                  std::vector<int64_t>& catProduced);
  std::vector<int> flood(int sx, int sz, const PowerEnv& env) const;
  int W_ = 0, H_ = 0, nextId_ = 1;
  PowerParams params_;
  std::vector<PlantState> plants_;
  std::vector<uint8_t> cons_, powered_;
  std::vector<PowerDeal> deals_;
  PowerStats stats_;
  bool consPct_ = false, stairPct_ = false;
};

// ======================= Water =======================
enum class WaterKind { Tower, Pump, Desalination };

struct WaterParams {
  int waterFromPipe = 3;                 // 0 -> 3
  int pumpDistanceFactor = 0;            // percent
  int towerCapa = 0, pumpCapa = 0, desaltCapa = 0;
  int towerLifeYears = 0, pumpLifeYears = 0, desaltLifeYears = 0;
  int conservationFactor = 0, meterFactor = 0;   // percent
  int defaultConsumption = 0, defaultLeak = 0;
  std::vector<std::pair<uint32_t, int>> modifiers;  // sorted by id, <= 127 entries
  int maxWaterPolluteForUI = 11000;      // SC3Pollution.ini value, used as m (spec 3.3 step 5; OPEN)
  // [OPEN] spec 16 3.3 step 5: m is the constant (true) or running maximum of the water grid (false).
  bool usePollutionConstantMax = true;
  static WaterParams fromIni(const IniFile& waterLayerIni);
  int consumptionPerTile(uint32_t agentType) const;  // binary search
};

struct WaterFacility {
  int id = 0;
  WaterKind kind = WaterKind::Tower;
  int x = 0, z = 0;
  int buildAbsMonth = 0;
  int lmax = 12, ld = 6;
  bool functional = true, expired = false;
  int output = 0;
  enum Status { Off, Good, Far, NoWater } status = Off;
};

struct WaterDeal { int tx = -1, tz = -1; bool purchase = true; int amount = 0; };

struct WaterEnv {
  int nowAbsMonth = 0;
  const std::vector<uint8_t>* pipes = nullptr;        // x*H+z
  const std::vector<uint8_t>* waterKind = nullptr;    // x*H+z: 0 land, 1 fresh, 2 salt
  const PowerLayer* power = nullptr;                  // powered bits (null = everything powered)
  std::function<int(int, int)> waterPollutionAt;      // 0..32767 at tile; null = 0
  int maxWaterPollutionGrid = 0;                      // used when !usePollutionConstantMax
  OrdinanceSet ordinances;
  const std::vector<SimOccupant>* consumers = nullptr;
  std::vector<WaterDeal> deals;
};

struct WaterStats {
  int64_t supply = 0, used = 0;
  int64_t sale[2] = {0, 0}, purchase[2] = {0, 0};
  uint32_t statA = 0, statB = 0;            // [OPEN] preserved
  uint32_t meanPollution = 0, ageingCount = 0;
};

class WaterLayer {
 public:
  WaterLayer() = default;
  WaterLayer(int W, int H, WaterParams p) { init(W, H, std::move(p)); }
  void init(int W, int H, WaterParams p);

  int addFacility(WaterKind k, int x, int z, int buildAbsMonth);  // lmax = 12*L, ld = 6*L
  bool removeFacility(int id);
  std::vector<WaterFacility>& facilities() { return fac_; }
  const std::vector<WaterFacility>& facilities() const { return fac_; }

  // Monthly step (spec 3.3, 3.4). After the call, occupantWatered() has one flag per env.consumers entry.
  void monthlyUpdate(const WaterEnv& env);

  const std::vector<uint8_t>& occupantWatered() const { return watered_; }
  bool tileServiced(int x, int z) const { return x >= 0 && z >= 0 && x < W_ && z < H_ && service_[size_t(x) * H_ + z]; }
  const std::vector<uint8_t>& serviceGrid() const { return service_; }
  WaterStats stats;
  Chart chart = Chart::fresh(0, 100);
  const WaterParams& params() const { return params_; }

  // ---- record 206C6E7C:02BF0033:0 (version 4, 212 + 4n bytes) ----
  Bytes serialize() const;
  void parse(const uint8_t* p, size_t n);   // throws FormatError; replaces service grid and stats

 private:
  int W_ = 0, H_ = 0, nextId_ = 1;
  WaterParams params_;
  std::vector<WaterFacility> fac_;
  std::vector<uint8_t> service_, watered_;
};

// ======================= Garbage =======================
// v -= trunc(v*effect/100) (spec 4.2).
inline int64_t reduceByPercent(int64_t v, int effect) { return v - (v * effect) / 100; }

// Per-run budget derived from a monthly capacity (spec 4.3): the capacity is divided over 30 runs
// with an integer carry so 30 runs sum to exactly the monthly value.
class RunBudget {
 public:
  int64_t next(int64_t monthly) { acc_ += monthly; int64_t o = acc_ / 30; acc_ %= 30; return o; }
 private:
  int64_t acc_ = 0;
};

// Disposal pipeline (spec 4.3): WTE -> incinerators -> landfill -> neighbour export -> abduction
// -> standing garbage. Statistics slots (garbageTotals in the pollution record):
// [0] produced [1] WTE [2] incinerator [3] scaled away [4] landfill [5] exported [6] abducted.
class GarbageDisposal {
 public:
  int64_t wteBudget = 0, incineratorBudget = 0;
  bool exportDeal = false;
  int64_t abductionCap = 0;         // GarbageAbductionAmount
  int64_t abducted = 0;             // cumulative counter (never reset: OPEN)
  int64_t landfillBalance = 0;      // negative = spare room (spec 7.2.4 step 3)
  int64_t stats[7] = {};

  void beginRun(int64_t wte, int64_t inc, bool exportDealPresent) { wteBudget = wte; incineratorBudget = inc; exportDeal = exportDealPresent; }
  // Returns the amount left over (standing garbage).
  int64_t dispose(int64_t amount);
  int64_t landfillRoom() const { return landfillBalance < 0 ? -landfillBalance : 0; }
};

}}  // namespace m3k::us
