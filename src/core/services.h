// City services (spec/17-sim-services.md): funding curve, police/fire coverage, flammability,
// crime, pollution (+garbage disposal pipeline), aura, health/education facility funding and
// the byte-exact records of spec/10-save-format.md section 6.9, 6.10, 6.15, 6.16.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <vector>
#include "core/simcommon.h"
#include "core/utilities.h"

namespace m3k { namespace us {

// Month-end chart push (spec 14 §6.3 rule 7 / Q-BB) [STATIC]: one monthly sample every monthly
// run; on 1 January also yearly; when the new year is a multiple of 10, also decade.
// Shared by crime (avgCrime), pollution (UI combined air/water %), aura (cityAura+127).
// Traffic uses the same cadence on its own history block (18 §10–§11 / avgRoad).
void chartMonthEnd(Chart& c, uint32_t sample, int month /*1..12*/, int year);

// ======================= Funding curve (spec 17 section 2.2) =======================
struct FundingCurve {
  double M = 1.2;   // max(1.0, MaxEfficiency/100)
  double E = 30.0;  // MysticalE
  static FundingCurve make(int maxEfficiencyPct, double mysticalE) {
    return {std::max(1.0, maxEfficiencyPct / 100.0), mysticalE};
  }
  // Saturating efficiency x' for x > 1 (single precision constants as in the original).
  // M <= 1 (MaxEfficiency = 100 divides by zero in the original) is guarded: returns 1.
  double saturate(double x) const;
};

struct FundedResult { int upkeep = 0; double x = 0; };
// Stations: x replaced only when rounded upkeep exceeds the base upkeep.
FundedResult applyStationFunding(int upkeep0, const FundingCurve& c, int fundingPct);
// Hospitals/schools: x replaced whenever x > 1.
FundedResult applyFacilityFunding(int upkeep0, const FundingCurve& c, int fundingPct);

// ======================= Police / fire coverage =======================
struct StationParams {
  int radius0 = 0, max0 = 0, min0 = 0;   // OptimalCoverageRadius/MaxCoverage/MinCoverage (bytes)
  int upkeep0 = 0;                       // OptimalMonthlyUpkeep
  int maxEfficiencyPct = 120;
  double mysticalE = 30.0;
  int oppressionThreshold = 0;
  static StationParams fire(const IniFile& tune);    // [FireStation]
  static StationParams police(const IniFile& tune);  // [PoliceStation]
};

struct StampParams { int R = 0, Max = 0, Min = 0; };
StampParams stationStamp(const StationParams& p, double x);  // truncating (spec 2.2 step 4)

struct CoverageParams {
  int updatePeriod = 30;                 // 0 -> 30
  int dispatchRadius = 0, dispatchEffect = 0;
  int minAcceptableFunding = 0;
  int maxAcceptable = 255;               // MaxAcceptableCrime (police) / MaxAcceptableFlam (fire)
  int strikeEfficiency = 100;            // percent
  int maxStrikeDuration = 0;
  double dailyJailReleaseRate = 0, convictionRate = 0;   // x0.001
  double noJailPenalty = 1.0;                            // x0.01 (neutral default)
  double maxJailOvercrowding = 1.0;                      // x0.01
  static CoverageParams police(const IniFile& policeIni);
  static CoverageParams fire(const IniFile& fireIni);
};

struct Jail { int id = 0; int capacity = 0; int inmates = 0; };

struct CoverageTickEnv {
  std::function<int(int, int)> crimeAt;      // police: crime 0..255 at tile (may be null)
  std::function<bool(int, int)> occupied;    // per-tile occupancy (null = nothing developed)
  int averageMetric = 0;                     // city average crime (police) / flammability (fire)
};

struct StrikeMachine {
  bool striking = false;
  uint32_t counter = 0;
  // Returns +1 if a strike started, -1 if it ended, 0 otherwise. `chance` is out of 10000.
  int step(SimRng& rng, uint32_t chance, bool endNow, uint32_t maxDuration);
};
// Spec 3.4 chance per tick out of 10000 (police; fire analogous with flammability).
uint32_t departmentStrikeChance(bool striking, bool haveStations, int funding, int minFunding,
                                int avgMetric, int maxAcceptable);

class CoverageLayer {
 public:
  void init(bool police, int W, int H, const StationParams& sp, const CoverageParams& cp);
  bool isPolice() const { return police_; }

  int addStation(int x0, int z0, int x1, int z1);      // uses current funding; returns id
  bool removeStation(int id);
  void setFunding(int pct);                            // restamps every station, updates upkeep
  int funding() const { return funding_; }
  int upkeepTotal = 0;                                  // monthly upkeep of the stations (not saved)

  int addJail(int capacity);                            // police only
  bool removeJail(int id);
  const std::vector<Jail>& jails() const { return jails_; }
  void addPrisoners(int n);
  void setJailInmates(int id, int n) { for (auto& j : jails_) if (j.id == id) j.inmates = std::max(0, n); }
  int unjailed = 0;                                     // unjailed prisoners P (not saved; 0 after load)

  void dispatch(int tx, int ty_z);                      // temporary unit stamp (spec 2.4)

  void tick(const CoverageTickEnv& env, SimRng& rng);   // one daily run (spec 3.3)

  int coverageAt(int x, int z) const;                   // displayed byte D
  int accumulatorAt(int x, int z) const;                // A
  int stationCount() const { return int(stations_.size()); }

  // Sweep state / record (34 bytes, no header).
  uint8_t phase = 0;
  uint32_t cursorX = 4, cursorZ = 8;
  StrikeMachine strike;
  uint32_t lastStrikeChance = 0;   // chance of the last tick (0..10000); the safety advisor reads it / 100 (spec 22 s3.9)
  uint32_t avgCoverage = 0, pctCovered = 0;
  Bytes serialize() const;
  void parse(const uint8_t* p, size_t n);               // throws FormatError unless n == 34

  static const int kPolicePhases[16][2];

 private:
  struct Station { int id; int x0, z0, x1, z1; StampParams st; };
  struct Unit { int x0, z0, x1, z1; StampParams st; };
  void stamp(int x0, int z0, int x1, int z1, StampParams s, int sign);
  void refreshCell(int cx, int cz);
  int cellIdx(int cx, int cz) const { return cx * ch_ + cz; }
  void redistribute();
  void advanceCursor();
  bool police_ = false;
  int W_ = 0, H_ = 0, cw_ = 0, ch_ = 0, nextId_ = 1, funding_ = 100;
  StationParams sp_;
  CoverageParams cp_;
  std::vector<int32_t> acc_;
  std::vector<uint8_t> disp_;
  std::vector<Station> stations_;
  std::vector<Unit> units_;
  std::vector<Jail> jails_;
  int64_t sweepSumD_ = 0, sweepCount_ = 0, sweepZeros_ = 0;
};

// ======================= Flammability =======================
struct FlammabilityParams {
  int leafBurningBanEffect = 0, smokeDetectorsEffect = 0, noWaterEffect = 0;  // bytes, percent
  static FlammabilityParams fromIni(const IniFile& tune);  // [FlammabilityLayer]
};
// Spec 4 tile value. `u` computed from ordinances; returns 0..255.
int flammabilityValue(int base, const FlammabilityParams& p, const OrdinanceSet& o, bool noWater);
int flammabilityBand(int byteValue);  // 0:0 1:1-46 2:47-83 3:84-121 4:122-150 5:>150

class FlammabilityLayer {
 public:
  void init(int W, int H, const FlammabilityParams& p) { W_ = W; H_ = H; params_ = p; grid.assign(size_t(W) * H, 0); cursor = 0; }
  // One daily run: recomputes floor(W*H/30) tiles in index order, continuing from the cursor.
  void run(const std::function<int(int, int)>& baseAt, const std::function<bool(int, int)>& noWaterAt,
           const OrdinanceSet& ord);
  double averageFlammability() const;   // mean of non-zero tiles (fire strike metric) [OPEN]
  std::vector<uint8_t> grid;             // x*H+z
  size_t cursor = 0;
  uint32_t trailer[4] = {0, 0, 0, 0};    // [OPEN] preserved
  uint8_t tailByte = 0;
  Bytes serialize() const;               // 25 + W*H bytes, header version 1
  void parse(const uint8_t* p, size_t n);
 private:
  int W_ = 0, H_ = 0;
  FlammabilityParams params_;
};

// ======================= Crime =======================
struct CrimeEntry { int32_t amount = 0; uint8_t radius = 0; bool operator==(const CrimeEntry& o) const { return amount == o.amount && radius == o.radius; } };

struct CrimeParams {
  int updatePeriod = 40;          // 0 -> 1
  int policeFactor = 1;           // 0 guarded -> 1
  int youthCurfew = 0, gambling = 0, jrSports = 0, neighborhoodWatch = 0, conservationCorps = 0;  // s16
  std::map<uint32_t, CrimeEntry> table;  // [AgentIDModifiers] id=radius,amount
  static CrimeParams fromIni(const IniFile& crimeIni);
};

class CrimeLayer {
 public:
  void init(int W, int H, const CrimeParams& p, const AgentHierarchy* hier, uint32_t shuffleSeed = 1);
  void addOccupant(const SimOccupant& o) { stampOccupant(o, +1); }
  void removeOccupant(const SimOccupant& o) { stampOccupant(o, -1); }
  void rebuild(const std::vector<SimOccupant>& occ);   // clear P and re-add all

  // One daily run of floor(W*H/(4*UpdatePeriod)) cells (spec 7.1.4).
  void run(const std::function<int(int, int)>& policeAt, const OrdinanceSet& ord);
  int crimeAt(int x, int z) const { return crime[size_t(x / 2) * nz_ + z / 2]; }
  int potentialAt(int cx, int cz) const { return potential[size_t(cx) * nz_ + cz]; }
  int cellValue(int cx, int cz, const std::function<int(int, int)>& policeAt, const OrdinanceSet& ord) const;
  // Crime chart sample = avgCrime (spec 17 §7.1 / 14 §6.3 rule 7) [STATIC].
  void monthEnd(int month, int year) { chartMonthEnd(chart, avgCrime, month, year); }

  uint16_t recVersion = 1, streamVersion = 2;
  std::vector<uint8_t> crime;      // cells, x*nz+z
  std::vector<int16_t> potential;  // cells
  uint32_t cursorX = 0, cursorZ = 0, sweepSum = 0, sweepCount = 0;
  uint8_t avgCrime = 0;
  Chart chart = Chart::fresh(0, 260);
  SweepOrder order;
  std::map<uint32_t, CrimeEntry> table;  // INI entries + cached nearest matches (saved)
  Bytes serialize() const;
  void parse(const uint8_t* p, size_t n, int W, int H);

 private:
  CrimeEntry lookup(uint32_t type);
  void stampOccupant(const SimOccupant& o, int sign);
  int W_ = 0, H_ = 0, nx_ = 0, nz_ = 0;
  CrimeParams params_;
  const AgentHierarchy* hier_ = nullptr;
  MsRand rnd_;
};

// ======================= Pollution =======================
struct PollutionEntry {
  int32_t air = 0, water = 0; uint8_t rA = 0, rW = 0; int32_t garbage = 0;
  bool operator==(const PollutionEntry& o) const { return air == o.air && water == o.water && rA == o.rA && rW == o.rW && garbage == o.garbage; }
};

struct PollutionParams {
  int updatePeriod = 30;          // 0 -> 1
  int maxAir = 0, maxWater = 11000, maxGarbage = 0;
  int airThreshold = 0, waterThreshold = 0, garbageThreshold = 0;
  int cleanAir = 0, trashPresort = 0, conservationCorps = 0, indPolluteFee = 0, lawnChemBan = 0,
      tireRecycling = 0, leafBurningBan = 0, backyardComposting = 0, paperReduction = 0,
      indWasteDisposal = 0, landfillGasRecovery = 0, carSmogging = 0;   // unsigned bytes
  int trafficAirFactor = 0;
  int garbageAbduction = 0;
  double garbageScale = 0;        // GarbageScalingFactor x 0.001
  // Facility monthly capacities (spec 16 4.1, 29 2.7). Per facility, at 100% funding.
  int treatmentCap = 0;           // SC3Tune [WaterTreatment] OptimalPollutionCap
  int incineratorCap = 0;         // SC3Tune [Incinerator] OptimalGarbageCap
  int wteCap = 0;                 // SC3PowerLayer [WasteToEnergyPowerPlant] OptimalGarbageCap
  // SC3Tune [RecyclingCenter] OptimalPctReduction / OptimalPopServed: inputs of the recycling ratio (spec 16 s4.3).
  int recyclingPct = 0, recyclingPopServed = 0;
  int recyclingMaxLifespan = 0, recyclingDeclineAge = 0;   // months
  int treatmentMaxLifespan = 0, treatmentDeclineAge = 0;   // months, SC3Tune [WaterTreatment]
  std::map<uint32_t, PollutionEntry> table;
  // pollution = SC3Pollution.ini; tune = SC3Tune.ini; power = SC3PowerLayer.ini (any may be null)
  static PollutionParams fromIni(const IniFile& pollution, const IniFile* tune = nullptr, const IniFile* power = nullptr);
};

struct PollutionEnv {
  const std::vector<uint8_t>* zone = nullptr;      // zone type per tile (x*H+z); 17 = landfill
  const std::vector<uint8_t>* traffic = nullptr;   // 0..255 per tile
  OrdinanceSet ordinances;
  bool exportDeal = false;
  int64_t importMonthly = 0;                       // neighbour garbage import (paid), per month
  int incineratorFunding = 100, wteFunding = 100;  // percent
};

class PollutionLayer {
 public:
  void init(int W, int H, const PollutionParams& p, const AgentHierarchy* hier, uint32_t shuffleSeed = 1);
  void addOccupant(const SimOccupant& o) { stampOccupant(o, +1); }
  void removeOccupant(const SimOccupant& o) { stampOccupant(o, -1); }
  void rebuild(const std::vector<SimOccupant>& occ);
  struct SourceGrids {
    std::vector<int32_t> air, water; std::vector<int16_t> garbage;
    static SourceGrids diff(const SourceGrids& a, const SourceGrids& b) {
      SourceGrids d; d.air.resize(a.air.size()); d.water.resize(a.water.size()); d.garbage.resize(a.garbage.size());
      for (size_t i = 0; i < a.air.size() && i < b.air.size(); i++) d.air[i] = a.air[i] - b.air[i];
      for (size_t i = 0; i < a.water.size() && i < b.water.size(); i++) d.water[i] = a.water[i] - b.water[i];
      for (size_t i = 0; i < a.garbage.size() && i < b.garbage.size(); i++) d.garbage[i] = int16_t(a.garbage[i] - b.garbage[i]);
      return d;
    }
  };
  SourceGrids sourceGrids() const { return {airSource, waterSource, garbageSource}; }
  void applySourceOffset(const std::vector<int32_t>& a, const std::vector<int32_t>& w, const std::vector<int16_t>& g) {
    for (size_t i = 0; i < airSource.size() && i < a.size(); i++) airSource[i] += a[i];
    for (size_t i = 0; i < waterSource.size() && i < w.size(); i++) waterSource[i] += w[i];
    for (size_t i = 0; i < garbageSource.size() && i < g.size(); i++) garbageSource[i] = int16_t(std::max(0, garbageSource[i] + g[i]));
  }

  void run(const PollutionEnv& env);   // one daily run (spec 7.2.4)

  int airAt(int x, int z) const { return (state[size_t(x / 2) * nz_ + z / 2] >> 15) & 0x7FFF; }
  int waterAt(int x, int z) const { return state[size_t(x / 2) * nz_ + z / 2] & 0x7FFF; }
  // Sea-water (salt) flag of the tile's cell: state bit 30, set by the terrain (spec 11 section 5, 17 7.2.1).
  bool seaWaterAt(int x, int z) const {
    size_t i = size_t(x / 2) * nz_ + z / 2;
    return x >= 0 && z >= 0 && i < state.size() && (state[i] >> 30 & 1);
  }
  int standingGarbageAt(int x, int z) const { return standing[size_t(x) * H_ + z] & 0x7FFF; }
  void setStandingGarbage(int x, int z, int g) { uint16_t& w = standing[size_t(x) * H_ + z]; w = uint16_t((w & 0x8000) | (g & 0x7FFF)); }
  bool airPolluted(int x, int z) const { return airAt(x, z) >= params_.airThreshold; }
  const PollutionParams& params() const { return params_; }
  bool waterPolluted(int x, int z) const { return waterAt(x, z) >= params_.waterThreshold; }
  bool garbagePolluted(int x, int z) const { return standingGarbageAt(x, z) >= params_.garbageThreshold; }
  // Recycling ratio r (spec 16 s4.3) [STATIC]: centres sorted by reduction percent, walked while served < P.
  struct RecyclingCentre { int64_t pct = 0, popServed = 0; };
  // A centre's monthly values (spec 16 s4.3) [STATIC]: 0 when not working (expired, age >= MaxLifespan, unpowered, lot
  // state not 1, failed trips), else the INI values reduced linearly after DeclineAge. No funding scaling.
  RecyclingCentre recyclingCentreValues(bool working, int ageMonths) const;
  // A treatment plant's monthly capacity (spec 16 s3.1) [STATIC]: 0 unless working (lot occupied, tile powered and
  // watered) and younger than MaxLifespan; OptimalPollutionCap reduced linearly after DeclineAge. No funding factor.
  int64_t treatmentCapacity(bool working, int ageMonths) const;
  void setTreatmentCaps(std::vector<int64_t> caps) { treatmentCaps_ = std::move(caps); treatmentCapsSet_ = true; }
  bool treatmentCapsSet() const { return treatmentCapsSet_; }
  // Month end: chart, garbage counters and history (spec 16 s4.5), landfill store update (s4.4) and the recycling
  // ratio for `population` (s4.3) from `centres` (null: every listed centre new and working).
  void monthEnd(int month, int year, int64_t population = 0, const std::vector<RecyclingCentre>* centres = nullptr);
  static float recyclingRatio(std::vector<RecyclingCentre> centres, int64_t population);
  // Landfill registration (spec 15 s7, spec 17 s7.2.5): sets bit 15 of the standing word, adds 32767 - g of room.
  // Sea-water (salt) flag of the tile's 2 x 2 cell: bit 30 of the cell state (spec 17 s7.2.1) [STATIC].
  bool seaFlagAt(int x, int z) const { return (state[size_t(x / 2) * nz_ + z / 2] & 0x40000000u) != 0; }
  // The terrain writing a sea surface at vertex (x, z) flags the cells of the up to four tiles sharing it (spec 11 s5).
  void markSeaVertex(int x, int z) {
    for (int dx = -1; dx <= 0; dx++) for (int dz = -1; dz <= 0; dz++) {
      int tx = x + dx, tz = z + dz;
      if (tx >= 0 && tz >= 0 && tx < W_ && tz < H_) state[size_t(tx / 2) * nz_ + tz / 2] |= 0x40000000u;
    }
  }
  bool landfillRegistered(int x, int z) const { return (standing[size_t(x) * H_ + z] & 0x8000) != 0; }
  void registerLandfill(int x, int z);

  // ---- record state (spec 17 7.4) ----
  uint16_t recVersion = 3, streamVersion = 2;
  std::vector<uint32_t> state;               // cells x*nz+z
  std::vector<uint16_t> standing;            // tiles x*H+z
  GarbageDisposal disp;                      // counters, history, totals and the landfill store (garbageStore)
  uint32_t avgAir = 0, avgWater = 0;
  uint16_t unk16 = 0;
  int32_t sumAir = 0, sumWater = 0;
  uint32_t totalStanding = 0, pollutedCount = 0;
  // Standing garbage on the map latched when the last sweep finalised (spec 22 "Garbage metrics": the environment
  // advisor's accumulating flag A). Not saved: after a load it starts as the loaded total.
  bool standingAtSweepEnd = false;
  Chart chart = Chart::fresh(0, 260);
  uint8_t unk8 = 0;
  uint32_t burnCapA = 0, burnCapB = 0;
  float ratio = 0; uint32_t unk32 = 0;
  uint32_t cursorX = 0, cursorZ = 0;
  int32_t rawWaterSum = 0, rawWaterLast = 0, treatedSum = 0, treatedLast = 0;
  SweepOrder order;
  uint32_t landfillTiles = 0;
  std::vector<int32_t> waterSource, airSource;   // cells
  std::vector<int16_t> garbageSource;            // cells
  std::map<uint32_t, PollutionEntry> table;
  std::vector<std::pair<uint32_t, uint32_t>> treatmentPlants, incinerators, wtePlants, recyclingCentres;

  Bytes serialize() const;
  void parse(const uint8_t* p, size_t n, int W, int H);

 private:
  PollutionEntry lookup(uint32_t type);
  void stampOccupant(const SimOccupant& o, int sign);
  void processCell(int tx, int tz, const PollutionEnv& env, int64_t& treatLeft, int64_t& sweepCells);
  void finaliseSweep();
  int W_ = 0, H_ = 0, nx_ = 0, nz_ = 0;
  PollutionParams params_;
  const AgentHierarchy* hier_ = nullptr;
  MsRand rnd_;
  RunBudget wteRb_, incRb_, recRb_;
  std::vector<int64_t> treatmentCaps_;
  bool treatmentCapsSet_ = false;
  int64_t recBudget_ = 0;
};

// ======================= Aura =======================
struct AuraParams {
  int updatePeriod = 50;               // u16, 0 -> 1
  uint8_t optimalPolice = 0;           // P0
  int8_t optimalPoliceEffect = 0, oppressiveEffect = 0, inadequateEffect = 0;  // E0, Ehi, Elo
  int8_t crimeFactor = -1;             // 0 -> -1
  int8_t fireFactor = 0;               // 0 guarded (original divides by zero)
  int16_t airFactor = 0, waterFactor = 0;
  int8_t garbageEffect = 0, radiationEffect = 0;
  int8_t highEQFactor = 0, lowEQFactor = 0; uint8_t highEQThreshold = 0, lowEQThreshold = 0;
  int8_t highLEFactor = 0, lowLEFactor = 0; uint8_t highLEThreshold = 0, lowLEThreshold = 0;
  int8_t parkingFine = 0, altDriving = 0, nukeFree = 0;
  struct Mod { uint8_t radius = 0; int8_t effect = 0; };
  std::map<uint32_t, Mod> table;
  static AuraParams fromIni(const IniFile& auraIni);   // uses the quirky number parser
};

struct AuraEnv {
  std::function<int(int, int)> policeAt, fireAt, crimeAt, airAt, waterAt, garbageAt;
  std::function<bool(int, int)> irradiated;
  std::function<bool(int, int)> tileCounts;   // occupant absent or building kind 1; null = true
  int eq = 0, lifeExpectancy = 0;
  OrdinanceSet ordinances;
};

class AuraLayer {
 public:
  void init(int W, int H, const AuraParams& p, const AgentHierarchy* hier);
  void addOccupant(const SimOccupant& o) { stampOccupant(o, +1); }
  void removeOccupant(const SimOccupant& o) { stampOccupant(o, -1); }
  void rebuild(const std::vector<SimOccupant>& occ);
  void run(const AuraEnv& env);                         // one daily run
  int aura(int x, int z) const { return grid[size_t(x / 4) * ch_ + z / 4]; }
  // Aura chart sample = cityAura + 127 (spec 17 §8.5 / 14 §6.3 rule 7) [STATIC].
  void monthEnd(int month, int year) { chartMonthEnd(chart, uint32_t(int(cityAura) + 127), month, year); }

  std::vector<int8_t> grid;          // cells, x*ch+z
  uint8_t phase = 0;
  uint32_t cursorX = 4, cursorZ = 8;
  int32_t sweepSum = 0;
  uint32_t sweepCount = 0;
  int8_t cityAura = 0;
  Chart chart = Chart::fresh(127, 260);
  Bytes serialize() const;                               // (W/4)(H/4) + 150 bytes
  void parse(const uint8_t* p, size_t n, int W, int H);
  static const int kPhases[16][2];

 private:
  AuraParams::Mod lookup(uint32_t type);
  void stampOccupant(const SimOccupant& o, int sign);
  void advance();
  int W_ = 0, H_ = 0, cw_ = 0, ch_ = 0;
  AuraParams params_;
  const AgentHierarchy* hier_ = nullptr;
  std::vector<int16_t> mod_;
  std::map<uint32_t, AuraParams::Mod> cache_;
};

// ======================= Health / education facility funding =======================
struct HospitalParams {
  uint32_t beds = 0, doctors = 0; double hqEffect = 0.001; int upkeep0 = 0;
  int maxEfficiencyPct = 120; double mysticalE = 30;
  static HospitalParams fromIni(const IniFile& tune);   // [Hospital]
};
struct HospitalFunding { int upkeep = 0; double x = 0; int doctors = 0; double effect = 0; uint32_t beds = 0; };
HospitalFunding hospitalFunding(const HospitalParams& p, int fundingPct);

struct SchoolParams {   // [School] / [College]
  uint32_t desks = 0, staff = 0; int minAge = 0, maxAge = 0; double eqEffect = 0.001; int upkeep0 = 0;
  int maxEfficiencyPct = 120; double mysticalE = 30;
  static SchoolParams school(const IniFile& tune);
  static SchoolParams college(const IniFile& tune);
};
struct SchoolFunding { int upkeep = 0; double x = 0; int staff = 0; double effect = 0; };
SchoolFunding schoolFunding(const SchoolParams& p, int fundingPct);

struct LibraryParams {  // [Library] / [Museum]: fixed, funding independent
  uint32_t popServed = 0; int minAge = 0, maxAge = 0; double eqEffect = 0.001; int upkeep = 0;
  static LibraryParams library(const IniFile& tune);
  static LibraryParams museum(const IniFile& tune);
};

// Coverage of one age (spec 17 6.3): sum(seats*e) / max(people, seats).
double ageCoverage(double seatsTimesEffect, double people, double seats);
// Patients (spec 17 5.2) and hospital quotient x_H = sum(beds*e)/max(patients, beds).
int64_t hospitalPatients(int p1, int p2, double pollutionFactor, double hospitalizationRate, int64_t population);
double healthQuotient(double bedsTimesEffect, double patients, double beds);
// Ratings (spec 17 5.3, 6.4): clamp(round(4*g/(optimalYearly/12) - 0.4), 0, 4).
int ratingFromQuotient(double g, double optimalYearly);
// Strike chance per month out of 10000 (spec 5.3/6.4): need facility, low rating, low funding.
uint32_t monthlyStrikeChance(bool haveFacility, bool ratingLow, int funding, int minFunding);
// Original defect: strike efficiency multiplies unscaled (20 means x20). fixDefect divides by 100.
double strikeMultiplier(int efficiency, bool fixDefect = false);

}}  // namespace m3k::us
