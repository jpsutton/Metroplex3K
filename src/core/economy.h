// Economy: budget, taxes, funding, bonds, ordinances, neighbour deals, business deals.
// See spec/19-sim-economy.md. Pure state classes with explicit input structs; records are
// byte-exact per spec 19 "Persisted state" and spec 10 section 6.6 / 6.8.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/chart.h"
#include "core/clock.h"
#include "core/ini.h"

namespace m3k {

// ================================================================ agent-type hierarchy helper
// Parent links of CitySim.ini [AgentTypeHierarchy]; used for placement-cost lookup.
class AgentHierarchy {
 public:
  void setParent(uint32_t child, uint32_t parent) { parent_[child] = parent; }
  // Hierarchy distance from t up to ancestor a (0 = same), or -1 when a is not an ancestor.
  int distance(uint32_t t, uint32_t a) const;
  bool isA(uint32_t t, uint32_t a) const { return distance(t, a) >= 0; }
 private:
  std::map<uint32_t, uint32_t> parent_;
};

// ================================================================ configuration
constexpr int kTaxGroups = 3;
constexpr int kMaxTaxRate = 22;
constexpr int kZoneTypes[kTaxGroups] = {3000, 5000, 8000};

struct TaxEffectEntry {
  float a = 0, b = 0;   // already scaled by 0.0001
  int n = 7;            // neutral rate (<= 22)
  uint32_t pop = 0;     // breakpoint P
};

struct BudgetConfig {
  // [Bonds]
  int maxBonds = 10;
  int monthlyPaymentPer1K = 10;
  int maxBondAmt = 25;      // thousands
  int bondLifespan = 10;    // years
  // [TuningParameters]
  float lvToTax[kTaxGroups] = {0.0035f, 0.0045f, 0.0050f};
  int minAverageLV = 35;
  std::vector<TaxEffectEntry> taxEffects;   // sorted by pop
  int tireRecyclingPct = 0;
  std::set<uint32_t> tireRecyclingTypes;
  // [SpecialCosts] by index (0 LowDensityZone ... 12 DemolishRubbleTile); missing = 0
  int64_t specialCost[13] = {};
  // [BuildingPlacementCosts]
  std::map<uint32_t, int64_t> placementCost;
  // Advisor-Budget.ini (loaded separately by loadAdvisor)
  int64_t loseGameLevel = 100000;
  int64_t treasuryLow = 1500;
  int taxGrowthThreshold = 500;      // x0.0001
  int taxDeclineThreshold = -1000;   // x0.0001

  BudgetConfig();
  // `budgetLayer` = SC3BudgetLayer.ini, `advisor` (optional) = Advisor-Budget.ini.
  static BudgetConfig fromIni(const IniFile& budgetLayer, const IniFile* advisor = nullptr);
  // `agent`'s cost per spec 19 s13. `tireOrdEnacted` applies the tire-recycling discount.
  int64_t placementCostFor(uint32_t agentType, const AgentHierarchy& h, bool tireOrdEnacted) const;
  int64_t specialCostFor(int index) const { return (index >= 0 && index < 13) ? specialCost[index] : 0; }
};

// ================================================================ tax->demand table (spec 19 s4.2)
class TaxDemandTable {
 public:
  void setEntries(const std::vector<TaxEffectEntry>& e);
  // Rebuild E[r] for the given population. Reproduces the pop-not-(pop-P_lo) interpolation defect.
  void rebuild(uint32_t population);
  float effect(int rate) const { return e_[rate < 0 ? 0 : (rate > kMaxTaxRate ? kMaxTaxRate : rate)]; }
  static std::array<float, 23> entryTable(const TaxEffectEntry& e);
 private:
  std::vector<TaxEffectEntry> entries_;
  std::array<float, 23> e_{};
};

// Tax revenue for one group (spec 19 s5.1), including the 32-bit unsigned wrap.
int64_t taxRevenue(int rate, int32_t zoneTotal, int avgLV, int minAverageLV, float lvToTax);

// ================================================================ budget
struct TaxGroup {
  uint32_t zoneType = 3000;
  uint8_t rate = 7;
  int64_t hist[12] = {};
  int64_t ytd = 0;
};

// Department IDs [OBSERVED, spec 19 s6.1]: each budget slider set to a distinct value in the original,
// saved, and the funding byte read back. Saved and shown in ascending ID order (see kOrder).
namespace dept {
constexpr uint32_t Education = 0x02922A5D, Health = 0x42922A6C, Fire = 0x62963928, Roads = 0x8290F605,
                   Police = 0xA2963983, Transit = 0xC290F609;
constexpr uint32_t kOrder[6] = {Education, Health, Fire, Roads, Police, Transit};
}

struct Department {
  uint32_t id = 0;
  uint8_t funding = 100;
  int64_t lastCharge = 0;
  int64_t hist[12] = {};
  int64_t ytd = 0;
  // Not persisted: supplied each month by the owning subsystem.
  int64_t fullCost = 0;
  int64_t yearEndCredit = 0;   // [OPEN] meaning; 0 in all shipped data
  int64_t charge() const { return int64_t(funding) * fullCost / 100; }
};

struct Bond {
  uint8_t month = 1;
  uint32_t maturityYear = 0;
  uint32_t principal = 0, totalRepayable = 0, remaining = 0, paid = 0, monthlyPayment = 0;
};

struct MonthEndInput {
  int month = 1, year = 1900;       // the NEW date (first day of the new month)
  uint32_t population = 0;
  int avgLV = 0;                    // 0..255 land-value average
  int32_t zoneTotals[kTaxGroups] = {};   // N_R, N_C, N_I (latched valve totals)
  int64_t ordIncome = 0, ordCost = 0;    // from Ordinances::monthlyMoney
  int64_t dealIncome = 0, dealCost = 0;  // from NeighbourDeals::monthlyMoney (penalties handled by Budget)
  int64_t bizIncome = 0;                 // BusinessDeals::monthlyIncome
  double fares = 0;                      // transit fares of the month just closed
};

struct MonthEndResult {
  bool yearEnd = false;
  bool openBudgetWindow = false;
  int slot = 0;           // month slot just closed (0 = January)
  int budgetYear = 0;
  int64_t taxIncome[kTaxGroups] = {};
};

class Budget {
 public:
  explicit Budget(BudgetConfig cfg = BudgetConfig());

  BudgetConfig cfg;
  int64_t funds = 0;
  TaxGroup tax[kTaxGroups];
  std::vector<Department> depts;     // six known departments registered by default
  int64_t ordCostYTD = 0, ordIncomeYTD = 0;
  int64_t ordCostHist[12] = {}, ordIncomeHist[12] = {};
  std::vector<Bond> bonds;
  int64_t bondYTD = 0;
  uint32_t bondPayHist[12] = {};
  int64_t totalPrincipal = 0;
  int64_t miscYTD = 0, miscHist[12] = {};
  Chart treasuryChart;
  int64_t otherYTD = 0, otherHist[12] = {};
  int64_t dealCostYTD = 0, dealIncomeYTD = 0, bizYTD = 0;
  int64_t dealCostHist[12] = {}, dealIncomeHist[12] = {}, bizHist[12] = {};
  int64_t dealPenaltyAccumulator = 0;
  TaxDemandTable taxDemand;

  // Session flags (not persisted).
  bool freePlacement = false;      // cheat "I am weak" toggles
  bool debtFloor = false;          // cheat "so sue me" toggles
  bool showBudgetOnlyInDebt = false;
  std::function<void()> onFundsChanged;

  // New-city settings (spec 19 s3). Pass <0 funds to keep 0.
  void newCity(int64_t startFunds, int rateR = 7, int rateC = 7, int rateI = 7);
  static int64_t startingFunds(int difficulty);   // 1:50000 2:20000 3:10000

  // Treasury operations (spec 19 s2).
  bool spend(int64_t amount);
  void add(int64_t amount);
  int64_t placementCost(uint32_t agentType, const AgentHierarchy& h, bool tireOrdEnacted) const {
    return freePlacement ? 0 : cfg.placementCostFor(agentType, h, tireOrdEnacted);
  }
  int64_t specialCost(int index) const { return freePlacement ? 0 : cfg.specialCostFor(index); }

  void setTaxRate(int group, int rate);
  Department* dept(uint32_t id);
  const Department* dept(uint32_t id) const;
  void setFunding(uint32_t id, int percent);
  void setDeptFullCost(uint32_t id, int64_t fullCost);
  // Direct miscellaneous income during the month (recorded in the current month's slot;
  // settled with the year-end).
  void addMisc(int64_t amount, int calendarMonth) { miscHist[(calendarMonth - 1 + 12) % 12] += amount; }
  // Monthly tax for a group right now (the "current month" figure of the budget window).
  int64_t currentTax(int group, const MonthEndInput& in) const;

  // Bonds (spec 19 s8). Returns false when refused.
  bool issueBond(int64_t requested, int currentMonth, int currentYear);
  int bondCount() const { return int(bonds.size()); }

  // Month-end and year-end processing (spec 19 s7).
  MonthEndResult monthEnd(const MonthEndInput& in);

  // Estimate of the year-end figure for one line (spec 19 s7 trailer); `month` 1..12.
  static int64_t estimate(int64_t ytd, int64_t currentMonthly, int month) {
    return ytd + currentMonthly * (12 - (month - 1));
  }
  // Treasury at or below -loseGameLevel.
  bool lost() const { return funds <= -cfg.loseGameLevel; }

  // Persistence (record T=E11BCC69 G=A11BCC54 I=0).
  static constexpr uint32_t kType = 0xE11BCC69, kGroup = 0xA11BCC54, kInstance = 0;
  void parse(const uint8_t* p, size_t n);
  Bytes serialize() const;

 private:
  static uint32_t chartSample(int64_t funds);
};

// ================================================================ ordinances (spec 19 s9)
namespace ord {
constexpr uint32_t YouthCurfew = 0xC2BF1DC5, LegalGambling = 0xA0D07129, ReadingProgram = 0xE0D07233,
                   YouthSports = 0x815B4CEF, ParkingFines = 0x015B4D1E, HomelessShelter = 0x20D07175,
                   CrossingGuards = 0xC2BF1DEF, Shuttle = 0xA2BF1DE5, WaterConservation = 0x02BF1DFA,
                   EarthquakeRetrofit = 0xC2BF1DD1, StairwellLighting = 0x82E42E30, CleanAir = 0xA2BF1E43,
                   AltDriving = 0xE2BF1E2A, FreeClinics = 0x40D07236, LeafBurningBan = 0x22BF1E35,
                   CprTraining = 0x00D0723D, SmokeDetectors = 0x62BF1DAA, NeighborhoodWatch = 0x62BF1DB9,
                   Carpool = 0xA2BF1DDC, TrashPresort = 0x42BF1E18, CarSmogging = 0xC2BF1E04,
                   SmokingBan = 0xE0D07239, NuclearFree = 0xE0D0722E, SubsidizedTransit = 0xC2D1B025,
                   PowerConservation = 0x82B9999B, TouristPromotion = 0x62F6E7CF,
                   ConservationCorps = 0xA2F6E7DA, IndPollutantFee = 0x02F6E7E2,
                   LandfillGas = 0x22F6E80C, IndWasteDisposal = 0xA2F917CF, CleanIndustry = 0xE2F6E7EA,
                   ElectronicsIncentive = 0x02F6E7EF, Aerospace = 0x22F6E7F4, ElectronicsJobFair = 0xA2F6E7F9,
                   BiotechIncentive = 0xE2F6E7FE, PublicCable = 0xC2F6E804, LawnChemBan = 0x62F6E808,
                   BackyardComposting = 0x22F6E814, PaperReduction = 0x22F6E81B, TireRecycling = 0xC2F6E81F,
                   FarmersMarket = 0x62F6E82B, WaterMeters = 0xC2F90EB1;
}  // namespace ord

enum class OrdMoney { Cost, Income, CostPlusFacility, WorldCost, Free, FixedIncome1 };
enum class OrdFacility { None, PoliceStations, Libraries, RecyclingCenters };

struct OrdinanceDef {
  uint32_t id = 0, category = 0;
  int year = 0;
  uint32_t nameStr = 0;
  int costDivisor = 100;
  OrdMoney money = OrdMoney::Cost;
  OrdFacility facility = OrdFacility::None;
};

struct OrdinanceTriggers {
  int homelessShelterPercent = 10, earthquakePrepBuildings = 300, touristPromotionBuildings = 25,
      aerospaceAvailableYear = 1960;
};

struct OrdinanceMoneyInput {
  uint32_t population = 0;
  int64_t world = -1;            // W; < 0 means "use population / 1000" ([OPEN], spec 29 2.2)
  int policeStations = 0, libraries = 0, recyclingCenters = 0;
};

struct AvailabilityInput {
  int currentYear = 1900;
  int homelessPercent = 0;
  int airports = 0, commercialBuildings = 0, residentialBuildings = 0, industrialBuildings = 0;
  int railStations = 0, busStations = 0, subwayStations = 0, libraries = 0, policeStations = 0;
};

class Ordinances {
 public:
  // Built-in catalogue = the shipped 42 lines of spec 19 s9.5.
  Ordinances();
  // Replace the catalogue by [ListOfOrdinances] and triggers by [ORDINANCE_TRIGGERS] when present.
  // Money kinds are hard-coded per ID (spec: "Money" column), so unknown IDs default to Cost.
  void loadCatalogue(const IniFile& ini);

  OrdinanceTriggers triggers;
  const std::map<uint32_t, OrdinanceDef>& catalogue() const { return defs_; }
  const OrdinanceDef* def(uint32_t id) const;

  // New-city state: year-0 ordinances available, nothing enacted.
  void newCity();
  bool available(uint32_t id) const;
  bool enacted(uint32_t id) const;
  bool setAvailable(uint32_t id, bool v);
  bool enact(uint32_t id);            // only when available
  bool repeal(uint32_t id);
  void makeAllAvailable(std::vector<uint32_t>* announced = nullptr);   // cheat

  struct Money { int64_t income = 0, cost = 0; };
  // Monthly value of one ordinance (positive = income, negative = cost); 0 when not enacted+available.
  int64_t monthlyValue(uint32_t id, const OrdinanceMoneyInput& in) const;
  Money monthlyMoney(const OrdinanceMoneyInput& in) const;

  // Monthly availability scan (10% chance, at most one ordinance). Returns the new ID if any.
  std::optional<uint32_t> availabilityScan(SimRng& rng, const AvailabilityInput& in);
  bool conditionHolds(const OrdinanceDef& d, const AvailabilityInput& in) const;

  // Record T=41193C3A G=E1193C2A I=0 (ascending ID order).
  static constexpr uint32_t kType = 0x41193C3A, kGroup = 0xE1193C2A, kInstance = 0;
  void parse(const uint8_t* p, size_t n);
  Bytes serialize() const;

 private:
  struct State { bool enacted = false, available = false; };
  std::map<uint32_t, OrdinanceDef> defs_;
  std::map<uint32_t, State> state_;
};

// ================================================================ neighbour deals (spec 19 s10)
constexpr int kDealTypes = 6;
constexpr int kNeighbours = 4;
// Connection kinds (spec 12 s8): 1 road, 2 rail, 3 power, 4 highway, 5 pipe, 6 subway.
enum ConnKind { ConnRoad = 1, ConnRail, ConnPower, ConnHighway, ConnPipe, ConnSubway };

struct DealConfig {
  uint32_t baseRate[kDealTypes] = {10, 12, 10, 12, 10, 12};   // simoleons per Base units per month
  uint32_t rateVariable = 3;                                   // DEAL_RATE_VARIABLE
  uint32_t penaltyMultiplier = 3;                              // DEAL_PENALTY_MULTIPLIER
  uint32_t basePower = 1000, baseWater = 1000, baseGarbage = 100;
  uint32_t powerUnitsPer1K = 100, waterUnitsPer1K = 100, garbageUnitsPer1K = 100;
  uint32_t percentPopForBuy = 20, percentPopForSell = 20;
  uint32_t minPowerBuy = 50, minWaterBuy = 50, minGarbageExport = 50;
  static DealConfig fromIni(const IniFile& ini);
  uint32_t base(int type) const { return type < 2 ? basePower : type < 4 ? baseWater : baseGarbage; }
  uint32_t unitsPer1K(int type) const { return type < 2 ? powerUnitsPer1K : type < 4 ? waterUnitsPer1K : garbageUnitsPer1K; }
  uint32_t minCost(int type) const { return type < 2 ? minPowerBuy : type < 4 ? minWaterBuy : minGarbageExport; }
};

struct Neighbour {
  std::string cityName, mayorName;
  uint32_t population = 0, populationCap = 4000000;
  bool exists = true;
  uint32_t nRoad = 0, nRail = 0, nPower = 0, nPipe = 0, nHighway = 0, nSubway = 0;
  bool dealMade[kDealTypes] = {};
};

struct Deal {
  std::string neighbourName, text;
  int32_t penalty = 0;
  int32_t monthlyAmount = 0;
  uint8_t neighbourIndex = 0, dealType = 0;
  int32_t monthCounter = 0;    // age in months; -1 = terminated
  int32_t quantity = 0;        // units
  int32_t rate = 0;
  uint32_t sizeOrTerm = 0;     // k of spec 10.2 step 2
  uint32_t year = 0;
  bool fromSave = false;       // loaded deal: its saved monthly amount is kept (the spec formula does not reproduce shipped saves)
  bool terminated() const { return monthCounter < 0; }
};

struct OfferContext {
  uint32_t cityPopulation = 0;
  int currentYear = 1900;
  uint32_t referenceRate = 0;        // neighbour's reference rate for the commodity
  uint32_t availableToSell = 0;      // quantity the player currently has to sell (sell types)
  bool hasSeaport = false;
  std::function<bool(int type, int neighbour)> powered;   // sell power: any connection tile powered
  std::function<bool(int type, int neighbour)> watered;   // sell water
};

class NeighbourDeals {
 public:
  DealConfig cfg;
  Neighbour neighbours[kNeighbours];
  std::vector<Deal> active, pending;
  std::set<uint32_t> conn[6];     // saved order: rail, road, pipe, power, highway, subway
  bool newDealsEnabled = true;    // scenario commands

  // New city: population 2000+rand(13000), cap 4M, names from `nameOf(instance)` (default
  // "Neighbour N"/"Mayor N"), `isLand(edgeX, edgeZ)` false clears exists.
  void newCity(SimRng& rng, int w, int h, const std::function<bool(int, int)>& isLand = {},
               const std::function<std::string(uint32_t)>& nameOf = {});

  // Neighbour index for an edge tile (corners explicit), -1 if the tile is not on an edge.
  static int edgeNeighbour(int x, int z, int w, int h);
  // Register a network piece on an edge tile; returns neighbour index or -1.
  int registerConnection(ConnKind kind, int x, int z, int w, int h);
  int connectionCount(int neighbour, ConnKind kind) const;

  // Monthly step (neighbours subsystem): January growth, then age += 1 for all non-terminated deals
  // and removal of terminated ones. `month` is the month of the run.
  void monthly(int month, SimRng& rng);
  static uint32_t growPopulation(uint32_t pop, uint32_t cap, SimRng& rng);   // one year, spec 10.6

  // Budget month-end hook: recompute monthlyAmount for every active deal.
  struct Money { int64_t income = 0, cost = 0; };
  Money monthlyMoney();
  static int32_t amountFor(const DealConfig& c, const Deal& d);

  // Offer generation (spec 10.2/10.6). Returns false when no offer is made.
  bool dealOfType(int type) const;
  int chooseNeighbour(int type, const OfferContext& ctx, SimRng& rng) const;
  bool connectionOk(int type, int neighbour, const OfferContext& ctx) const;
  std::optional<Deal> makeOffer(int type, const OfferContext& ctx, SimRng& rng);   // also queues in `pending`
  bool accept(size_t pendingIndex);        // moves to active, age 0, sets dealMade
  // Cancel the active deal of `type`. Returns the penalty to add to the budget's accumulator.
  int64_t cancel(int type);
  // Utilities report the current quantity for a deal type (spec 10.4).
  void reportQuantity(int type, int32_t quantity);

  // Record C2910E7D:82937B60:0 (no header, no markers).
  static constexpr uint32_t kType = 0xC2910E7D, kGroup = 0x82937B60, kInstance = 0;
  void parse(const uint8_t* p, size_t n);
  Bytes serialize() const;
};

// ================================================================ business deals (spec 19 s11)
struct BusinessDealDef {
  std::string name;
  uint32_t buildingType = 0;
  int year = 0, value = 0;
  uint32_t messageId = 0;
  bool enabled = true;
};

class BusinessDeals {
 public:
  int monthsTillBusinessDeal = 12;   // counted in WEEKS (advisor cycles)
  int64_t treasuryLow = 1500;
  std::vector<BusinessDealDef> defs;
  static BusinessDeals fromIni(const IniFile& advisorBudget);
  BusinessDeals();

  int lowTreasuryCounter = 0;
  std::optional<size_t> pendingOffer;
  std::vector<uint32_t> inForce;      // building types of accepted deals

  struct Inputs {
    int64_t funds = 0;
    uint32_t population = 0;
    int currentYear = 1900;
    std::function<bool(uint32_t buildingType)> cityHasBusiness;
    std::function<bool()> casinoAllowed;   // [OPEN] default: allowed
  };
  // Weekly advisor evaluation. Returns the offered deal index if a new offer was raised.
  std::optional<size_t> weeklyEvaluate(const Inputs& in, SimRng& rng);
  bool accept();                          // pending offer -> in force
  void decline() { pendingOffer.reset(); }
  void remove(uint32_t buildingType);
  int64_t monthlyIncome() const;
};

}  // namespace m3k
