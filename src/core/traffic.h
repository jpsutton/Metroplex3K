// Traffic / transit simulation (spec/18-sim-transport.md) and the traffic simulator record
// 206C6E7C:029CA804:0 (version 9). Platform independent.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/ini.h"

namespace m3k {

// ---- network flag bits (section 3.1) ----
enum TrafficFlags : uint16_t {
  TF_ROAD = 0x0001, TF_HIGHWAY = 0x0002, TF_RAIL = 0x0004, TF_SUBWAY = 0x0008, TF_STATION = 0x0010,
  TF_XFER = 0x0020, TF_ACCESS = 0x0040, TF_DAMAGED = 0x0080, TF_PROP_A = 0x0100, TF_SUBWAY_PROP_A = 0x0200,
  TF_ROAD_EXIT = 0x0400, TF_HWY_EXIT = 0x0800, TF_RAIL_EXIT = 0x1000, TF_SUBWAY_EXIT = 0x2000, TF_SPAN = 0x4000,
};
// Station occupant type ids (section 3.1) and the flags OR-ed into their footprint.
constexpr uint32_t kBusStopType = 0x2FB2, kRailStationType = 0x2FC6, kSubwayStationType = 0x3048, kSubwayRailType = 0x3049;
uint16_t stationFlagsForType(uint32_t occupantTypeId);  // 0 when not a station type

// ---- travel modes (section 5.1) ----
enum TripMode : uint8_t {
  TM_START = 0x80, TM_ROAD = 1, TM_HWY = 2, TM_ROAD_ACCESS = 3, TM_RAIL = 4, TM_SUBWAY = 8,
  TM_RAIL_STN = 0x14, TM_SUBWAY_STN = 0x18, TM_XFER_STN = 0x30, TM_BUS = 0x41, TM_BUS_HWY = 0x42,
  TM_BUS_ACCESS = 0x43, TM_BUS_STOP = 0x50,
};

enum TripCategory { CAT_RES = 0, CAT_COM = 1, CAT_IND = 2, CAT_OTHER = 3, CAT_ANY = 3 };
// Category of a building by its occupant-type parent chain (section 4.1); -1 = makes no trips.
int categoryOfTypeChain(const std::vector<uint32_t>& chain);

struct TrafficParams {
  // [TrafficLayer]; defaults per section 2.1.
  uint8_t cellCostRoad = 4, cellCostRoadBus = 3, cellCostHighway = 2, cellCostHighwayBus = 1, cellCostBusStop = 5,
          cellCostRailStation = 5, cellCostSubwayRailStation = 5, cellCostSubwayStation = 5, cellCostRail = 2,
          cellCostSubway = 1;
  uint8_t maxDistRes = 5, maxDistCom = 4, maxDistInd = 6, maxDistDefault = 6;
  uint8_t densityAdder = 1;
  int monthlyDensityDecrement = 25;  // percent retention (value * 0.01)
  uint8_t maxDensity = 255, annoyanceLevel = 150;
  uint8_t divRoad = 32, divRoadBus = 32, divHighway = 32, divHighwayBus = 32, divBusStop = 128, divRailStation = 128,
          divSubwayRailStation = 128, divSubwayStation = 128, divRail = 255, divSubway = 255;
  uint8_t tripThresholdPercent = 25, chanceCountingBase = 30;
  int chanceDensityMultiplier = 50;  // percent (value * 0.01)
  uint8_t useMassTransitChance = 50;
  int maxEfficiencyRoads = 120, maxEfficiencyTransit = 120;  // percent
  float mysticalERoads = 30, mysticalETransit = 30;
  int damageEfficiencyMultiplier = 100, repairEfficiencyMultiplier = 50;  // per mille
  int damagedCostAddToStep = 32;
  int fundingCostRoad = 10, fundingCostHighway = 50, fundingCostRail = 10, fundingCostSubway = 50;  // percent
  int incomeRail = 10, incomeSubway = 10, incomeBus = 1;  // per mille
  int parkingFinesSub = 2, alternateDaySub = 10, carpoolSub = 5, crossingGuardsAdd = 2, touristAdd = 3;  // percent
  uint8_t shuttleRadiusAdd = 1, subsidizedUseAdd = 50, subsidizedFareDecrease = 50;
  uint8_t minFundingForNoStrike = 100;
  int strikeChanceAtZeroFunding = 75;  // percent
  int maxStrikeMonths = 24;
  uint8_t lobotomyDensity = 128;
  // [TrafficTripDestinationThresholds]: from Res/Com/Ind/Mixed, three percentages each.
  uint8_t dest[4][3] = {{10, 40, 100}, {70, 80, 100}, {50, 70, 100}, {33, 66, 100}};

  // Reads the INI (all keys optional). Reproduces the TripMaxDistanceToRoadDefault defect.
  static TrafficParams fromIni(const IniFile& ini);
};

// Ordinances affecting traffic (ids in 19-sim-economy.md; section 6.2 / 8.1).
struct TrafficOrdinances {
  bool parkingFines = false, alternateDayDriving = false, carpool = false, crossingGuards = false,
       touristPromotion = false, shuttleService = false, subsidizedTransit = false;
};
constexpr uint32_t kOrdParkingFines = 0x015B4D1E, kOrdAlternateDay = 0xE2BF1E2A, kOrdCarpool = 0xA2BF1DDC,
                   kOrdCrossingGuards = 0xC2BF1DEF, kOrdTourist = 0x62F6E7CF, kOrdSubsidizedTransit = 0xC2D1B025;

// Efficiency curve of section 8.2. fundingPercent F (0..255), maxEff M percent, k = Mystical constant.
float efficiencyCurve(int fundingPercent, int maxEffPercent, float mystical);

// Section 4.7 LCG.
class TrafficRng {
 public:
  explicit TrafficRng(uint32_t seed = 1) : s_(seed) {}
  uint32_t next() { uint64_t v = uint64_t(s_) * 0x41C64E6Du + 0x3039u; s_ = uint32_t(v); return uint32_t(v >> 16); }
  int rand(int n) { return n <= 0 ? 0 : int(next() % uint32_t(n)); }
  uint32_t state() const { return s_; }
 private:
  uint32_t s_;
};

enum StrikeState : uint8_t { STRIKE_THREATENED = 0, STRIKE_STRIKING = 1, STRIKE_NORMAL = 2 };  // persisted values

// ---- persisted record (section "Persisted state") ----
struct TrafficRecord {
  int W = 0, H = 0;
  uint16_t version = 9;
  std::vector<uint8_t> snapSurface, snapHighway, snapSubway;  // W*H, index x*H+z
  std::vector<uint8_t> tripOk, tripTried;                     // one byte (0/1) per tile
  float faresThisMonth = 0, faresLastMonth = 0;
  uint8_t strikeState = STRIKE_NORMAL;
  uint32_t strikeMonths = 0;
  uint32_t history[32] = {};   // 12 monthly + 10 yearly + 10 decade
  uint32_t graphMax = 220;
  std::vector<uint8_t> liveSurface, liveHighway, liveSubway;
  uint32_t tripsAttempted = 0, tripsFailed = 0;
  float failureRatio = 0;
  uint32_t avgTransit = 0, avgRoad = 0;
  void reset(int w, int h);
};
// Throws FormatError. Older versions accepted per the field table; absent fields keep reset values.
TrafficRecord parseTrafficRecord(const uint8_t* p, size_t n, int W, int H);
// Always version 9, flags 2, no markers.
Bytes serializeTrafficRecord(const TrafficRecord& r);
size_t trafficRecordSize(int W, int H);
constexpr uint32_t kTrafficRecType = 0x206C6E7C, kTrafficRecGroup = 0x029CA804;

// ---- trips ----
struct TripRect { int x0 = 0, z0 = 0, x1 = 0, z1 = 0; };  // inclusive
struct TripNode {
  TripRect r;
  uint8_t mode = TM_START, prevMode = TM_START;
  int cost = 0;
  int lastDir = 0;
  bool alt = false;
};
struct TripRequest {
  TripRect origin;
  int originCategory = CAT_RES;  // selects the walking radius
  int dest = CAT_RES;            // destination category (CAT_ANY: any bit counts)
  bool rail = true, bus = true, subway = true;
  int budgetFactor = -1;         // default max(W,H); garbage uses 255
};
struct TripResult {
  bool success = false;
  std::vector<TripNode> path;
  int cost = 0;
  bool leftMap = false;
};
struct SpanInfo { int dir = 0; int len = 1; };  // dir: 0 -x, 1 -y(-z), 2 +x, 3 +y(+z)

struct TripBuilding {
  int x = 0, z = 0, w = 1, h = 1;
  int category = CAT_RES;
  int slot = 0;
  bool functional = true;
  bool makesTrips() const { return category >= 0; }
};

struct MonthlyInput {
  TrafficOrdinances ord;
  int roadTiles = 0, highwayTiles = 0, railTiles = 0, subwayTiles = 0;
  int stationUpkeep = 0;       // sum of OptimalMonthlyUpkeep of station buildings
  double roadCondition = 0;    // X from the network subsystem (0..1)
  int year = 2000, month = 1;
};
struct MonthlyOutput {
  int strikeEvent = 0;         // 0 none, 1 threatened, 2 started, 3 resolved
  int roadConditionPercent = -1;  // new condition message (percent) or -1 when E_R == 1
  int roadUpkeep = 0, transitUpkeep = 0;
};

class TrafficSim {
 public:
  TrafficSim(int W, int H, const TrafficParams& p, uint32_t seed = 1);

  int width() const { return W_; }
  int height() const { return H_; }
  const TrafficParams& params() const { return p_; }
  TrafficRng& rng() { return rng_; }
  TrafficRecord& rec() { return rec_; }
  const TrafficRecord& rec() const { return rec_; }
  void loadRecord(const TrafficRecord& r);

  // ---- network flags ----
  uint16_t& flags(int x, int z) { return flags_[size_t(x) * H_ + z]; }
  uint16_t flags(int x, int z) const { return flags_[size_t(x) * H_ + z]; }
  void orFlags(const TripRect& r, uint16_t f);
  // Removal rules of section 11: clear flag bits and zero the given layers on the footprint.
  void clearNetwork(const TripRect& r, uint16_t clearMask, bool surf, bool hwy, bool subway);
  void addStation(uint32_t typeId, const TripRect& r);
  void removeStation(uint32_t typeId, const TripRect& r);

  // ---- destination bits ----
  void applyOrdinances(const TrafficOrdinances& o);    // radii
  int radius(int category) const;
  void addDestination(const TripRect& r, int category);
  void rebuildDestinations(const std::vector<TripBuilding>& all);
  bool destBit(int x, int z, int category) const { return (dest_[size_t(x) * H_ + z] >> category) & 1; }

  // ---- callbacks (defaults: none) ----
  // Span of a tile piece with TF_SPAN. When unset, spans fail.
  std::function<bool(int x, int z, SpanInfo&)> spanOf;
  // Highway-like modes: permitted exits for a tile given the last move code; bit0 N(-z) 1 W(-x) 2 S(+z) 3 E(+x).
  std::function<unsigned(int x, int z, int lastDir)> highwayExits;
  // Density layer value of the city at a lot tile (counting test, 4.5); default 0.
  std::function<int(int x, int z)> lotDensity;
  // (C, I) weights of 4.2; default (1,1).
  std::function<void(int& c, int& i)> ciWeights;

  // ---- trips ----
  TripResult routeTrip(const TripRequest& req);
  // Applies a successful counted trip (6.1). Returns fare credited.
  void applyCountedTrip(const TripRect& origin, const TripResult& tr);
  void writeResult(const TripRect& origin, bool ok);
  // One generator firing of a building (4.4-4.6). Returns true when a trip was routed.
  bool fireBuilding(const TripBuilding& b);
  // Weekly Monday run: slot fires iff floor(day/7)==slot. Day is 1-based.
  int runWeekly(int dayOfMonth, const std::vector<TripBuilding>& buildings);
  static bool slotFires(int slot, int dayOfMonth) { return dayOfMonth / 7 == slot; }
  int chooseDestination(int originCategory);
  // Mode permissions for a generator category (4.1 table), before the mass-transit draw.
  static void permissions(int category, bool& rail, bool& bus, bool& subway, bool& subject);

  // ---- results & queries ----
  int tripResult(int x, int z) const;                 // 2 none, 0 ok, 1 failed
  int tripResultArea(const TripRect& r) const;        // 1 if any tile failed
  void clearLot(const TripRect& r);                   // building removed
  int tileValue(int x, int z, unsigned netMask) const;
  int areaAverage(const TripRect& r, unsigned netMask, int initial = 0) const;
  void areaSumCount(const TripRect& r, unsigned netMask, int& sum, int& count) const;
  uint8_t liveSurface(int x, int z) const { return rec_.liveSurface[size_t(x) * H_ + z]; }
  uint8_t liveHighway(int x, int z) const { return rec_.liveHighway[size_t(x) * H_ + z]; }
  uint8_t liveSubway(int x, int z) const { return rec_.liveSubway[size_t(x) * H_ + z]; }
  uint8_t snapSurface(int x, int z) const { return rec_.snapSurface[size_t(x) * H_ + z]; }
  uint8_t snapHighway(int x, int z) const { return rec_.snapHighway[size_t(x) * H_ + z]; }
  uint8_t snapSubway(int x, int z) const { return rec_.snapSubway[size_t(x) * H_ + z]; }

  // ---- funding / strikes / monthly ----
  void setRoadFunding(int f) { roadFunding_ = f; }
  void setTransitFunding(int f) { transitFunding_ = f; recomputeEffective(); }
  int roadFunding() const { return roadFunding_; }
  int transitFunding() const { return transitFunding_; }
  float roadEfficiency() const;
  float transitEfficiency() const;
  // Section 8.3 upkeep.
  static int roadUpkeep(const TrafficParams& p, int roadTiles, int highwayTiles);
  static int transitUpkeep(const TrafficParams& p, int stationUpkeep, int railTiles, int subwayTiles);
  void recomputeEffective();  // 8.1 (uses last applied ordinances)
  int massTransitChance() const { return massChance_; }
  float fareRail() const { return fareRail_; }
  float fareSubway() const { return fareSub_; }
  float fareBus() const { return fareBus_; }
  // Month boundary in the exact order of section 11.
  MonthlyOutput monthly(const MonthlyInput& in);
  // Statistics of section 10 (computed inside monthly(); exposed for tests).
  void computeStats(int roadTiles, int highwayTiles, int railTiles, int subwayTiles);
  // 8.5 strike machine alone. Returns event (see MonthlyOutput).
  int strikeStep(int upkeep);
  // 8.4 road condition message; -1 when E_R == 1.
  int roadConditionMessage(double X, int month_unused = 0) const;

 private:
  struct Ctx;
  int densityOf(uint8_t mode, int x, int z) const;
  bool nextStep(Ctx& c, std::vector<TripNode>& stack, bool first);
  bool arrived(const Ctx& c, const TripNode& n) const;
  int stepCost(uint8_t mode, uint8_t prev, int d, uint16_t f) const;
  uint8_t classify(uint8_t fromMode, uint8_t prevMode, uint16_t f, bool rail, bool bus, bool subway) const;
  void decay(const MonthlyInput& in);

  int W_, H_;
  TrafficParams p_;
  TrafficRng rng_;
  TrafficRecord rec_;
  std::vector<uint16_t> flags_;
  std::vector<uint8_t> dest_;  // bit0 res, bit1 com, bit2 ind
  std::vector<uint16_t> visit_;
  int radii_[4];
  TrafficOrdinances ord_;
  int roadFunding_ = 100, transitFunding_ = 100;
  int massChance_ = 50;
  float fareRail_ = 0, fareSub_ = 0, fareBus_ = 0;
  int tripTotalCost_ = 0;
};

}  // namespace m3k
