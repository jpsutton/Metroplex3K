#include "core/traffic.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace m3k {

uint16_t stationFlagsForType(uint32_t t) {
  switch (t) {
    case kBusStopType: return 0x0011;
    case kRailStationType: return 0x0014;
    case kSubwayStationType: return 0x0018;
    case kSubwayRailType: return 0x003C;
  }
  return 0;
}

int categoryOfTypeChain(const std::vector<uint32_t>& chain) {
  for (uint32_t id : chain) {
    switch (id) {
      case 3000: return CAT_RES;
      case 5000: case 10000: case 12310: case 12350: case 18000: return CAT_COM;
      case 8000: case 12330: return CAT_IND;
      case 1001: case 11000: case 12005: case 12101: case 13000: case 19000: return CAT_OTHER;
    }
  }
  return -1;
}

// ---------------- params ----------------
namespace {
bool parseUInt(const std::string& s, uint32_t& out) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
  if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
  uint64_t v = 0;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') { v = v * 10 + uint64_t(s[i] - '0'); if (v > 0xFFFFFFFFull) v = 0xFFFFFFFFull; i++; }
  out = uint32_t(v);
  return true;
}
}  // namespace

TrafficParams TrafficParams::fromIni(const IniFile& ini) {
  TrafficParams p;
  const std::string S = "TrafficLayer";
  auto u8 = [&](const char* k, uint8_t& dst) { std::string s; uint32_t v; if (ini.get(S, k, s) && parseUInt(s, v)) dst = uint8_t(v); };
  auto in = [&](const char* k, int& dst) { std::string s; uint32_t v; if (ini.get(S, k, s) && parseUInt(s, v)) dst = int(v); };
  u8("TripCellCostBaseRoad", p.cellCostRoad); u8("TripCellCostBaseRoadBus", p.cellCostRoadBus);
  u8("TripCellCostBaseHighway", p.cellCostHighway); u8("TripCellCostBaseHighwayBus", p.cellCostHighwayBus);
  u8("TripCellCostBaseBusStop", p.cellCostBusStop); u8("TripCellCostBaseRailStation", p.cellCostRailStation);
  u8("TripCellCostBaseSubwayRailStation", p.cellCostSubwayRailStation);
  u8("TripCellCostBaseSubwayStation", p.cellCostSubwayStation);
  u8("TripCellCostBaseRail", p.cellCostRail); u8("TripCellCostBaseSubway", p.cellCostSubway);
  u8("TripMaxDistanceToRoadRes", p.maxDistRes); u8("TripMaxDistanceToRoadCom", p.maxDistCom);
  u8("TripMaxDistanceToRoadInd", p.maxDistInd);
  // Defect (section 2.1): the Default key is stored into the industrial radius; the default radius stays 6.
  u8("TripMaxDistanceToRoadDefault", p.maxDistInd);
  u8("TripDensityAdder", p.densityAdder);
  in("MonthlyDensityDecrement", p.monthlyDensityDecrement);
  u8("TrafficMaxDensity", p.maxDensity); u8("TrafficAnnoyanceLevel", p.annoyanceLevel);
  u8("TrafficDensityTripCostDivisorRoad", p.divRoad); u8("TrafficDensityTripCostDivisorRoadBus", p.divRoadBus);
  u8("TrafficDensityTripCostDivisorHighway", p.divHighway);
  u8("TrafficDensityTripCostDivisorHighwayBus", p.divHighwayBus);
  u8("TrafficDensityTripCostDivisorBusStop", p.divBusStop);
  u8("TrafficDensityTripCostDivisorRailStation", p.divRailStation);
  u8("TrafficDensityTripCostDivisorSubwayRailStation", p.divSubwayRailStation);
  u8("TrafficDensityTripCostDivisorSubwayStation", p.divSubwayStation);
  u8("TrafficDensityTripCostDivisorRail", p.divRail); u8("TrafficDensityTripCostDivisorSubway", p.divSubway);
  u8("TrafficTripThresholdPercent", p.tripThresholdPercent); u8("TripChanceCountingBase", p.chanceCountingBase);
  in("TripChanceDensityMultiplier", p.chanceDensityMultiplier);
  u8("TrafficUseMassTransitChance", p.useMassTransitChance);
  in("MaxEfficiencyRoads", p.maxEfficiencyRoads); in("MaxEfficiencyTransit", p.maxEfficiencyTransit);
  {
    std::string s;
    if (ini.get(S, "MysticalERoads", s)) p.mysticalERoads = float(std::atof(s.c_str()));
    if (ini.get(S, "MysticalETransit", s)) p.mysticalETransit = float(std::atof(s.c_str()));
  }
  in("DamageEfficiencyMultiplier", p.damageEfficiencyMultiplier);
  in("RepairEfficiencyMultiplier", p.repairEfficiencyMultiplier);
  { int v = p.damagedCostAddToStep; in("DamagedCostAddToStep", v); p.damagedCostAddToStep = v; }
  in("OptimalMonthlyFundingCostPerRoad", p.fundingCostRoad);
  in("OptimalMonthlyFundingCostPerHighway", p.fundingCostHighway);
  in("OptimalMonthlyFundingCostPerRail", p.fundingCostRail);
  in("OptimalMonthlyFundingCostPerSubway", p.fundingCostSubway);
  in("PerTileTransitIncomeRail", p.incomeRail); in("PerTileTransitIncomeSubway", p.incomeSubway);
  in("PerTileTransitIncomeBus", p.incomeBus);
  in("ParkingFinesRoadMonthlyDecrementSubtract", p.parkingFinesSub);
  in("AlternateDayCarsRoadHighwayMonthlyDecrementSubtract", p.alternateDaySub);
  in("CarpoolingRoadHighwayMonthlyDecrementSubtract", p.carpoolSub);
  in("CrossingGuardsRoadMonthlyDecrementAdd", p.crossingGuardsAdd);
  in("TouristPromotionRoadRailMonthlyDecrementAdd", p.touristAdd);
  u8("ShuttleServiceDistanceToRoadAdd", p.shuttleRadiusAdd);
  u8("SubsidizedMassTransitUsePercentAdd", p.subsidizedUseAdd);
  u8("SubsidizedMassTransitFaresPercentDecrease", p.subsidizedFareDecrease);
  u8("MinFundingForNoStrike", p.minFundingForNoStrike);
  in("StrikeChanceAtZeroFunding", p.strikeChanceAtZeroFunding);
  in("MaxLengthofStrikeInMonths", p.maxStrikeMonths);
  u8("TrafficSimLobotomyDensity", p.lobotomyDensity);

  const std::string T = "TrafficTripDestinationThresholds";
  const char* keys[4] = {"FromResidential", "FromCommercial", "FromIndustrial", "FromMixedOrAll"};
  for (int i = 0; i < 4; i++) {
    std::string s;
    if (!ini.get(T, keys[i], s)) continue;
    uint32_t v[3];
    int n = 0;
    size_t pos = 0;
    bool ok = true;
    while (pos <= s.size() && ok) {
      size_t e = s.find(',', pos);
      std::string part = s.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
      uint32_t x;
      if (n >= 3 || !parseUInt(part, x)) { ok = false; break; }
      v[n++] = x;
      if (e == std::string::npos) break;
      pos = e + 1;
    }
    if (ok && n == 3) for (int k = 0; k < 3; k++) p.dest[i][k] = uint8_t(v[k]);
  }
  return p;
}

float efficiencyCurve(int F, int maxEffPercent, float K) {
  float f = float(F) / 100.0f;
  if (f <= 1.0f) return f;
  float M = float(maxEffPercent) * 0.01f;
  if (M < 1.0f) M = 1.0f;
  if (M == 1.0f) return 1.0f;
  float inv = 1.0f / (1.0f - M);
  float g = (K + 1.2f * inv) * 4.999999f;
  return M - 1.0f / ((g - inv) * f - g);
}

// ---------------- record ----------------
void TrafficRecord::reset(int w, int h) {
  W = w; H = h;
  size_t G = size_t(w) * h;
  snapSurface.assign(G, 0); snapHighway.assign(G, 0); snapSubway.assign(G, 0);
  tripOk.assign(G, 0); tripTried.assign(G, 0);
  liveSurface.assign(G, 0); liveHighway.assign(G, 0); liveSubway.assign(G, 0);
  faresThisMonth = faresLastMonth = 0;
  strikeState = STRIKE_NORMAL; strikeMonths = 0;
  std::memset(history, 0, sizeof history);
  graphMax = 220;
  tripsAttempted = tripsFailed = 0; failureRatio = 0; avgTransit = avgRoad = 0;
}

size_t trafficRecordSize(int W, int H) {
  size_t G = size_t(W) * H, B = size_t(W) * ((H + 31) / 32) * 4;
  return 8 + 3 * G + 2 * B + 8 + 1 + 4 + 132 + 3 * G + 20;
}

namespace {
void readGrid8(Reader& r, std::vector<uint8_t>& g, int W, int H) {
  for (int row = 0; row < W; row++) {
    int x = W - 1 - row;
    for (int z = 0; z < H; z++) g[size_t(x) * H + z] = r.u8();
  }
}
void readGridBits(Reader& r, std::vector<uint8_t>& g, int W, int H) {
  int words = (H + 31) / 32;
  for (int row = 0; row < W; row++) {
    int x = W - 1 - row;
    for (int w = 0; w < words; w++) {
      uint32_t v = r.u32();
      for (int b = 0; b < 32; b++) {
        int z = w * 32 + b;
        if (z < H) g[size_t(x) * H + z] = (v >> b) & 1;
      }
    }
  }
}
void writeGrid8(Bytes& o, const std::vector<uint8_t>& g, int W, int H) {
  for (int row = 0; row < W; row++) {
    int x = W - 1 - row;
    for (int z = 0; z < H; z++) o.push_back(g[size_t(x) * H + z]);
  }
}
void writeGridBits(Bytes& o, const std::vector<uint8_t>& g, int W, int H) {
  int words = (H + 31) / 32;
  for (int row = 0; row < W; row++) {
    int x = W - 1 - row;
    for (int w = 0; w < words; w++) {
      uint32_t v = 0;
      for (int b = 0; b < 32; b++) {
        int z = w * 32 + b;
        if (z < H && g[size_t(x) * H + z]) v |= 1u << b;
      }
      put32(o, v);
    }
  }
}
void putF32(Bytes& o, float f) { uint32_t v; std::memcpy(&v, &f, 4); put32(o, v); }
}  // namespace

TrafficRecord parseTrafficRecord(const uint8_t* p, size_t n, int W, int H) {
  TrafficRecord t;
  t.reset(W, H);
  Reader r(p, n);
  t.version = r.u16();
  uint8_t fl = r.u8();
  if (fl & 2) r.u8();
  if (r.u32() != 0xDEADBEEF) throw FormatError("traffic record: bad sentinel");
  if (t.version > 9) throw FormatError("traffic record: unsupported version");
  if (fl & 1) { uint32_t l = r.u32(); r.skip(l); }
  readGrid8(r, t.snapSurface, W, H);
  if (t.version >= 2) {
    readGrid8(r, t.snapHighway, W, H);
    readGrid8(r, t.snapSubway, W, H);
    readGridBits(r, t.tripOk, W, H);
    readGridBits(r, t.tripTried, W, H);
  }
  if (t.version >= 3) { t.faresThisMonth = r.f32(); t.faresLastMonth = r.f32(); }
  if (t.version >= 5) { t.strikeState = r.u8(); t.strikeMonths = r.u32(); }
  if (t.version >= 6) {
    for (int i = 0; i < 32; i++) t.history[i] = r.u32();
    t.graphMax = r.u32();
  }
  if (t.version >= 7) {
    readGrid8(r, t.liveSurface, W, H);
    readGrid8(r, t.liveHighway, W, H);
    readGrid8(r, t.liveSubway, W, H);
  }
  if (t.version >= 9) {
    t.tripsAttempted = r.u32(); t.tripsFailed = r.u32(); t.failureRatio = r.f32();
    t.avgTransit = r.u32(); t.avgRoad = r.u32();
  }
  return t;
}

Bytes serializeTrafficRecord(const TrafficRecord& t) {
  Bytes o;
  o.reserve(trafficRecordSize(t.W, t.H));
  put16(o, 9); o.push_back(2); o.push_back(0); put32(o, 0xDEADBEEF);
  writeGrid8(o, t.snapSurface, t.W, t.H);
  writeGrid8(o, t.snapHighway, t.W, t.H);
  writeGrid8(o, t.snapSubway, t.W, t.H);
  writeGridBits(o, t.tripOk, t.W, t.H);
  writeGridBits(o, t.tripTried, t.W, t.H);
  putF32(o, t.faresThisMonth); putF32(o, t.faresLastMonth);
  o.push_back(t.strikeState); put32(o, t.strikeMonths);
  for (int i = 0; i < 32; i++) put32(o, t.history[i]);
  put32(o, t.graphMax);
  writeGrid8(o, t.liveSurface, t.W, t.H);
  writeGrid8(o, t.liveHighway, t.W, t.H);
  writeGrid8(o, t.liveSubway, t.W, t.H);
  put32(o, t.tripsAttempted); put32(o, t.tripsFailed); putF32(o, t.failureRatio);
  put32(o, t.avgTransit); put32(o, t.avgRoad);
  return o;
}

// ---------------- sim ----------------
TrafficSim::TrafficSim(int W, int H, const TrafficParams& p, uint32_t seed)
    : W_(W), H_(H), p_(p), rng_(seed), flags_(size_t(W) * H, 0), dest_(size_t(W) * H, 0), visit_(size_t(W) * H, 0) {
  rec_.reset(W, H);
  applyOrdinances(TrafficOrdinances());
}

void TrafficSim::loadRecord(const TrafficRecord& r) {
  rec_ = r;
  recomputeEffective();
}

void TrafficSim::orFlags(const TripRect& r, uint16_t f) {
  for (int x = std::max(0, r.x0); x <= std::min(W_ - 1, r.x1); x++)
    for (int z = std::max(0, r.z0); z <= std::min(H_ - 1, r.z1); z++) flags(x, z) |= f;
}

void TrafficSim::clearNetwork(const TripRect& r, uint16_t mask, bool surf, bool hwy, bool sub) {
  for (int x = std::max(0, r.x0); x <= std::min(W_ - 1, r.x1); x++)
    for (int z = std::max(0, r.z0); z <= std::min(H_ - 1, r.z1); z++) {
      size_t k = size_t(x) * H_ + z;
      flags_[k] &= uint16_t(~mask);
      if (surf) rec_.snapSurface[k] = rec_.liveSurface[k] = 0;
      if (hwy) rec_.snapHighway[k] = rec_.liveHighway[k] = 0;
      if (sub) rec_.snapSubway[k] = rec_.liveSubway[k] = 0;
    }
}

void TrafficSim::addStation(uint32_t typeId, const TripRect& r) {
  uint16_t f = stationFlagsForType(typeId);
  if (f) orFlags(r, f);
}
void TrafficSim::removeStation(uint32_t typeId, const TripRect& r) {
  uint16_t f = stationFlagsForType(typeId);
  if (f) clearNetwork(r, f, true, false, false);
}

void TrafficSim::applyOrdinances(const TrafficOrdinances& o) {
  ord_ = o;
  int add = o.shuttleService ? p_.shuttleRadiusAdd : 0;
  radii_[0] = p_.maxDistRes + add; radii_[1] = p_.maxDistCom + add; radii_[2] = p_.maxDistInd + add;
  radii_[3] = p_.maxDistDefault + add;
  recomputeEffective();
}
int TrafficSim::radius(int c) const { return radii_[(c >= 0 && c <= 3) ? c : 3]; }

void TrafficSim::addDestination(const TripRect& r, int cat) {
  if (cat < 0 || cat > 2) return;
  int R = radii_[cat];
  for (int x = std::max(0, r.x0 - R); x <= std::min(W_ - 1, r.x1 + R); x++)
    for (int z = std::max(0, r.z0 - R); z <= std::min(H_ - 1, r.z1 + R); z++) dest_[size_t(x) * H_ + z] |= uint8_t(1 << cat);
}

void TrafficSim::rebuildDestinations(const std::vector<TripBuilding>& all) {
  std::fill(dest_.begin(), dest_.end(), 0);
  for (auto& b : all) {
    if (!b.functional || b.category < 0 || b.category > 2) continue;
    addDestination(TripRect{b.x, b.z, b.x + b.w - 1, b.z + b.h - 1}, b.category);
  }
}

void TrafficSim::permissions(int cat, bool& rail, bool& bus, bool& subway, bool& subject) {
  if (cat == CAT_RES || cat == CAT_COM) { rail = bus = subway = subject = true; }
  else { rail = true; bus = subway = subject = false; }
}

// ---- efficiency / fares ----
float TrafficSim::roadEfficiency() const { return efficiencyCurve(roadFunding_, p_.maxEfficiencyRoads, p_.mysticalERoads); }
float TrafficSim::transitEfficiency() const { return efficiencyCurve(transitFunding_, p_.maxEfficiencyTransit, p_.mysticalETransit); }

void TrafficSim::recomputeEffective() {
  fareRail_ = float(p_.incomeRail) * 0.001f;
  fareSub_ = float(p_.incomeSubway) * 0.001f;
  fareBus_ = float(p_.incomeBus) * 0.001f;
  int pc = p_.useMassTransitChance;
  if (ord_.subsidizedTransit) {
    pc = std::min(pc + int(p_.subsidizedUseAdd), 255);
    float k = float(p_.subsidizedFareDecrease) / 100.0f;
    fareRail_ *= k; fareSub_ *= k; fareBus_ *= k;
  }
  massChance_ = int(float(pc) * transitEfficiency());
}

int TrafficSim::roadUpkeep(const TrafficParams& p, int road, int hwy) {
  return (road * p.fundingCostRoad + 99) / 100 + (hwy * p.fundingCostHighway + 99) / 100;
}
int TrafficSim::transitUpkeep(const TrafficParams& p, int st, int rail, int sub) {
  return st + (rail * p.fundingCostRail + 99) / 100 + (sub * p.fundingCostSubway + 99) / 100;
}

// ---- trip internals ----
namespace {
int modeBit(uint8_t m) {
  switch (m) {
    case TM_START: return 0; case TM_ROAD: return 1; case TM_HWY: return 2; case TM_ROAD_ACCESS: return 3;
    case TM_RAIL: return 4; case TM_SUBWAY: return 5; case TM_RAIL_STN: return 6; case TM_SUBWAY_STN: return 7;
    case TM_XFER_STN: return 8; case TM_BUS: return 9; case TM_BUS_HWY: return 10; case TM_BUS_ACCESS: return 11;
    case TM_BUS_STOP: return 12;
  }
  return 0;
}
bool isHwyMode(uint8_t m) { return m == TM_HWY || m == TM_ROAD_ACCESS || m == TM_BUS_HWY || m == TM_BUS_ACCESS; }
}  // namespace

struct TrafficSim::Ctx {
  const TripRequest* req = nullptr;
  int budget = 0;
  bool rail = true, bus = true, subway = true;
};

int TrafficSim::densityOf(uint8_t mode, int x, int z) const {
  size_t k = size_t(x) * H_ + z;
  switch (mode) {
    case TM_HWY: case TM_BUS_HWY: return rec_.liveHighway[k];
    case TM_SUBWAY: return rec_.liveSubway[k];
    default: return rec_.liveSurface[k];
  }
}

int TrafficSim::stepCost(uint8_t mode, uint8_t prev, int d, uint16_t f) const {
  auto q = [&](int div) { return div ? d / div : 0; };
  int D = (f & TF_DAMAGED) ? (p_.damagedCostAddToStep & 0xFF) : 0;
  float fac = (f & TF_PROP_A) ? 0.7f : 1.0f;
  float facS = (f & TF_SUBWAY_PROP_A) ? 0.7f : 1.0f;
  auto ce = [](float v) { return int(std::ceil(v)); };
  switch (mode) {
    case TM_ROAD: return ce(float(q(p_.divRoad) + D + p_.cellCostRoad) * fac);
    case TM_HWY: return ce(float(q(p_.divHighway) + D + p_.cellCostHighway) * fac);
    case TM_ROAD_ACCESS: return q(p_.divRoad) + p_.cellCostRoad;
    case TM_RAIL: return ce(float(q(p_.divRail) + p_.cellCostRail) * fac);
    case TM_SUBWAY: return ce(float(q(p_.divSubway) + p_.cellCostSubway) * facS);
    case TM_RAIL_STN:
      if (prev == TM_RAIL) return q(p_.divRail) + p_.cellCostRail;
      return q(p_.divRailStation) + p_.cellCostRailStation;
    case TM_SUBWAY_STN:
      if (prev == TM_SUBWAY) return q(p_.divSubway) + p_.cellCostSubway;
      return q(p_.divSubwayStation) + p_.cellCostSubwayStation;
    case TM_XFER_STN:
      if (prev == TM_RAIL || prev == TM_SUBWAY) return q(p_.divSubway) + p_.cellCostSubway;
      return q(p_.divSubwayRailStation) + p_.cellCostSubwayRailStation;
    case TM_BUS: return ce(float(q(p_.divRoadBus) + D + p_.cellCostRoadBus) * fac);
    case TM_BUS_HWY: return ce(float(q(p_.divHighwayBus) + D + p_.cellCostHighwayBus) * fac);
    case TM_BUS_ACCESS: return q(p_.divRoadBus) + p_.cellCostRoadBus;
    case TM_BUS_STOP:
      if (prev == TM_BUS) return q(p_.divHighway) + p_.cellCostHighway;
      return q(p_.divBusStop) + p_.cellCostBusStop;
  }
  return 0;
}

// Ring/first-step classification (5.2, 5.4). Returns the new mode or 0.
uint8_t TrafficSim::classify(uint8_t from, uint8_t prev, uint16_t f, bool rail, bool bus, bool sub) const {
  const bool road = f & TF_ROAD, railF = f & TF_RAIL, subF = f & TF_SUBWAY, stn = f & TF_STATION, xfer = f & TF_XFER;
  auto fromRoadLike = [&](bool allowBusStop) -> uint8_t {
    if (!stn) return 0;
    if (allowBusStop && bus && road) return TM_BUS_STOP;
    if ((rail || sub) && xfer) return TM_XFER_STN;
    if (rail && railF) return TM_RAIL_STN;
    if (sub) return TM_SUBWAY_STN;
    return 0;
  };
  switch (from) {
    case TM_START: {
      if (!stn) return road ? TM_ROAD : 0;
      return fromRoadLike(true);
    }
    case TM_ROAD:
      if (!stn) return road ? ((f & TF_ACCESS) ? TM_ROAD_ACCESS : TM_ROAD) : 0;
      return fromRoadLike(true);
    case TM_BUS:
      if (!stn) return road ? ((f & TF_ACCESS) ? TM_BUS_ACCESS : TM_BUS) : 0;
      return fromRoadLike(false);
    case TM_RAIL:
      if (!stn) return railF ? TM_RAIL : 0;
      if (xfer) return TM_XFER_STN;
      return railF ? TM_RAIL_STN : 0;
    case TM_SUBWAY:
      if (!stn) return subF ? TM_SUBWAY : 0;
      if (xfer) return TM_XFER_STN;
      if (subF && !road && !railF) return TM_SUBWAY_STN;
      return 0;
    case TM_RAIL_STN: case TM_SUBWAY_STN: case TM_XFER_STN: {
      if (!stn) {
        if (from == TM_RAIL_STN && railF && prev != TM_RAIL) return TM_RAIL;
        if (from == TM_SUBWAY_STN && subF && prev != TM_SUBWAY) return TM_SUBWAY;
        if (from == TM_XFER_STN) {
          if (rail && railF && prev != TM_RAIL) return TM_RAIL;
          if (sub && subF && prev != TM_SUBWAY) return TM_SUBWAY;
        }
        return 0;
      }
      if (road && bus && prev != TM_BUS_STOP) return TM_BUS_STOP;
      return 0;
    }
    case TM_BUS_STOP:
      if (!stn && road && prev != TM_BUS) return TM_BUS;
      return 0;
  }
  return 0;
}

bool TrafficSim::arrived(const Ctx& c, const TripNode& n) const {
  uint16_t f = flags(n.r.x0, n.r.z0);
  switch (n.mode) {
    case TM_START: return false;
    case TM_ROAD: case TM_BUS: if (f & TF_ROAD_EXIT) return true; break;
    case TM_HWY: case TM_BUS_HWY: return (f & TF_HWY_EXIT) != 0;
    case TM_RAIL: return (f & TF_RAIL_EXIT) != 0;
    case TM_SUBWAY: return (f & TF_SUBWAY_EXIT) != 0;
    default: break;
  }
  unsigned mask = c.req->dest == CAT_ANY ? 7u : (1u << c.req->dest);
  for (int x = n.r.x0; x <= n.r.x1; x++)
    for (int z = n.r.z0; z <= n.r.z1; z++)
      if (dest_[size_t(x) * H_ + z] & mask) return true;
  return false;
}

bool TrafficSim::nextStep(Ctx& c, std::vector<TripNode>& stack, bool first) {
  (void)first;
  const TripNode cur = stack.back();
  uint8_t scanMode = cur.mode;
  uint8_t prevMode = cur.prevMode;
  if (scanMode == TM_SUBWAY_STN && prevMode == TM_SUBWAY) scanMode = TM_SUBWAY;  // stays on the subway
  const int bit = 1 << modeBit(scanMode);

  struct Cand { int x, z; uint8_t mode; int d; bool stn; uint16_t f; };
  bool have = false;
  Cand best{};
  bool belowChosen = false;
  int bestD = 0, seen = 0;
  bool alt = false;

  auto spanOk = [&](int x, int z, SpanInfo& sp, bool& isSpan) {
    isSpan = false;
    uint16_t f = flags(x, z);
    if ((f & TF_SPAN) && (f & (TF_ROAD | TF_HIGHWAY | TF_RAIL))) {
      isSpan = true;
      if (!spanOf || !spanOf(x, z, sp) || sp.len < 1) return false;
    }
    return true;
  };

  if (isHwyMode(scanMode)) {
    unsigned ex = highwayExits ? highwayExits(cur.r.x0, cur.r.z0, cur.lastDir) : 0xFu;
    static const int ox[4] = {0, -1, 0, 1}, oz[4] = {-1, 0, 1, 0};  // N(-z), W(-x), S(+z), E(+x)
    for (int i = 0; i < 4; i++) {
      if (!((ex >> i) & 1)) continue;
      int x = cur.r.x0 + ox[i], z = cur.r.z0 + oz[i];
      if (x < 0 || z < 0 || x >= W_ || z >= H_) continue;
      size_t k = size_t(x) * H_ + z;
      if (visit_[k] & bit) continue;
      visit_[k] |= uint16_t(bit);
      uint16_t f = flags_[k];
      if (f & TF_STATION) continue;
      bool road = f & TF_ROAD, hwy = f & TF_HIGHWAY, acc = f & TF_ACCESS;
      uint8_t nm = 0;
      int layerMode = TM_ROAD;
      bool bus = scanMode == TM_BUS_HWY || scanMode == TM_BUS_ACCESS;
      switch (scanMode) {
        case TM_HWY: if (hwy) { nm = acc ? TM_ROAD_ACCESS : TM_HWY; layerMode = TM_HWY; } break;
        case TM_ROAD_ACCESS:
          if (road) { nm = acc ? TM_ROAD_ACCESS : TM_ROAD; layerMode = TM_ROAD; }
          else if (hwy) { nm = acc ? TM_ROAD_ACCESS : TM_HWY; layerMode = TM_HWY; }
          break;
        case TM_BUS_HWY: if (hwy) { nm = acc ? TM_BUS_ACCESS : TM_BUS_HWY; layerMode = TM_HWY; } break;
        case TM_BUS_ACCESS:
          if (road) { nm = acc ? TM_BUS_ACCESS : TM_BUS; layerMode = TM_ROAD; }
          else if (hwy) { nm = acc ? TM_BUS_ACCESS : TM_BUS_HWY; layerMode = TM_HWY; }
          break;
      }
      (void)bus;
      if (!nm) continue;
      SpanInfo sp; bool isSpan;
      if (!spanOk(x, z, sp, isSpan)) continue;
      int d = densityOf(uint8_t(layerMode), x, z);
      if (!have) { best = {x, z, nm, d, false, f}; have = true; }
      else if (d < p_.annoyanceLevel) { best = {x, z, nm, d, false, f}; alt = true; }
    }
    if (have) seen = 1;
  } else {
    std::vector<std::pair<int, int>> tiles;
    TripRect g{cur.r.x0 - 1, cur.r.z0 - 1, cur.r.x1 + 1, cur.r.z1 + 1};
    bool firstStep = cur.mode == TM_START && stack.size() == 1;
    if (firstStep) {
      int R = radius(c.req->originCategory);
      g = TripRect{cur.r.x0 - R, cur.r.z0 - R, cur.r.x1 + R, cur.r.z1 + R};
    }
    for (int x = g.x0; x <= g.x1; x++)
      for (int z = g.z0; z <= g.z1; z++) {
        bool cx = (x == g.x0 || x == g.x1), cz = (z == g.z0 || z == g.z1);
        if (cx && cz) continue;  // four corners of the grown rectangle
        if (x >= cur.r.x0 && x <= cur.r.x1 && z >= cur.r.z0 && z <= cur.r.z1) continue;
        if (x < 0 || z < 0 || x >= W_ || z >= H_) continue;
        tiles.push_back({x, z});
      }
    if (firstStep)
      for (size_t i = tiles.size(); i > 1; i--) std::swap(tiles[i - 1], tiles[size_t(rng_.rand(int(i)))]);
    for (auto& t : tiles) {
      size_t k = size_t(t.first) * H_ + t.second;
      if (visit_[k] & bit) continue;
      uint16_t f = flags_[k];
      uint8_t nm = classify(scanMode, prevMode, f, c.rail, c.bus, c.subway);
      SpanInfo sp; bool isSpan;
      if (nm == 0 || !spanOk(t.first, t.second, sp, isSpan)) { visit_[k] |= uint16_t(bit); continue; }
      int d = densityOf(nm, t.first, t.second);
      bool stn = (f & TF_STATION) != 0;
      seen++;
      Cand cd{t.first, t.second, nm, d, stn, f};
      if (d >= p_.annoyanceLevel) {
        if (!have) { best = cd; have = true; }
      } else if (stn) {
        best = cd; have = true; bestD = 255; belowChosen = true;
      } else if (!(belowChosen && d < bestD)) {
        best = cd; have = true; bestD = d; belowChosen = true;
      }
    }
    alt = seen > 1;
  }
  if (!have) return false;

  // build node
  TripNode nn;
  nn.r = TripRect{best.x, best.z, best.x, best.z};
  int mult = 1;
  {
    SpanInfo sp; bool isSpan;
    spanOk(best.x, best.z, sp, isSpan);
    if (isSpan) {
      static const int sx[4] = {-1, 0, 1, 0}, sz[4] = {0, -1, 0, 1};
      int d = sp.dir & 3;
      int ex = std::min(std::max(best.x + sx[d] * sp.len, 0), W_ - 1), ez = std::min(std::max(best.z + sz[d] * sp.len, 0), H_ - 1);
      nn.r = TripRect{std::min(best.x, ex), std::min(best.z, ez), std::max(best.x, ex), std::max(best.z, ez)};
      mult = sp.len;
    }
  }
  nn.mode = best.mode;
  nn.prevMode = cur.mode;
  nn.cost = cur.cost + stepCost(best.mode, cur.mode, best.d, best.f) * mult;
  if (nn.r.z0 > cur.r.z1) nn.lastDir = 1;
  else if (nn.r.x0 > cur.r.x1) nn.lastDir = 0;
  else if (nn.r.z1 < cur.r.z1) nn.lastDir = 3;
  else nn.lastDir = 2;
  nn.alt = alt;
  for (int x = nn.r.x0; x <= nn.r.x1; x++)
    for (int z = nn.r.z0; z <= nn.r.z1; z++) visit_[size_t(x) * H_ + z] |= uint16_t(bit);
  stack.push_back(nn);
  return true;
}

TripResult TrafficSim::routeTrip(const TripRequest& req) {
  TripResult res;
  Ctx c;
  c.req = &req;
  c.rail = req.rail; c.bus = req.bus; c.subway = req.subway;
  c.budget = int(p_.cellCostRoad) * (req.budgetFactor >= 0 ? req.budgetFactor : std::max(W_, H_));
  std::fill(visit_.begin(), visit_.end(), 0);
  TripNode start;
  start.r = req.origin;
  start.mode = start.prevMode = TM_START;
  for (int x = std::max(0, req.origin.x0); x <= std::min(W_ - 1, req.origin.x1); x++)
    for (int z = std::max(0, req.origin.z0); z <= std::min(H_ - 1, req.origin.z1); z++)
      visit_[size_t(x) * H_ + z] |= uint16_t(1 << modeBit(TM_START));
  std::vector<TripNode> stack{start};
  size_t guard = size_t(W_) * H_ * 16 + 1024;
  while (guard-- > 0) {
    const TripNode& cur = stack.back();
    if (arrived(c, cur)) {
      res.success = true;
      res.path = stack;
      res.cost = cur.cost;
      return res;
    }
    if (cur.cost <= c.budget && nextStep(c, stack, false)) continue;
    // backtrack: a popped node whose "alternatives" flag is set means its parent's scan saw more than
    // one candidate (spec 5.1/5.7: the flag sits on the new node); resume the parent when it yields a step.
    bool resumed = false;
    while (stack.size() > 1) {
      TripNode x = stack.back();
      stack.pop_back();
      if (x.alt && stack.back().cost <= c.budget && nextStep(c, stack, false)) { resumed = true; break; }
    }
    if (!resumed) break;
  }
  return res;
}

void TrafficSim::applyCountedTrip(const TripRect& origin, const TripResult& tr) {
  int w = origin.x1 - origin.x0 + 1, h = origin.z1 - origin.z0 + 1;
  int a = int(int8_t(uint8_t((w * h * int(p_.densityAdder)) & 0xFF)));
  auto add = [&](std::vector<uint8_t>& g, size_t k) {
    int v = int(g[k]) + a;
    v = std::min(std::max(v, 0), int(p_.maxDensity));
    g[k] = uint8_t(v);
  };
  for (auto& n : tr.path) {
    bool s = false, hwy = false, sub = false;
    float fare = 0;
    switch (n.mode) {
      case TM_ROAD: s = true; break;
      case TM_HWY: hwy = true; break;
      case TM_RAIL: case TM_RAIL_STN: s = true; fare = fareRail_; break;
      case TM_SUBWAY: sub = true; fare = fareSub_; break;
      case TM_SUBWAY_STN: s = sub = true; fare = fareSub_; break;
      case TM_XFER_STN: s = sub = true; fare = fareRail_ + fareSub_; break;
      case TM_BUS: case TM_BUS_HWY: fare = fareBus_; break;
      case TM_BUS_STOP: s = true; fare = fareBus_; break;
      default: break;
    }
    // modes 3, 0x43 and the origin credit nothing
    if (n.mode == TM_ROAD) { /* surface only */ }
    for (int x = n.r.x0; x <= n.r.x1; x++)
      for (int z = n.r.z0; z <= n.r.z1; z++) {
        size_t k = size_t(x) * H_ + z;
        if (s) add(rec_.liveSurface, k);
        if (hwy) add(rec_.liveHighway, k);
        if (sub) add(rec_.liveSubway, k);
      }
    rec_.faresThisMonth += fare;
  }
}

void TrafficSim::writeResult(const TripRect& o, bool ok) {
  for (int x = std::max(0, o.x0); x <= std::min(W_ - 1, o.x1); x++)
    for (int z = std::max(0, o.z0); z <= std::min(H_ - 1, o.z1); z++) {
      rec_.tripTried[size_t(x) * H_ + z] = 1;
      rec_.tripOk[size_t(x) * H_ + z] = ok ? 1 : 0;
    }
}

int TrafficSim::chooseDestination(int cat) {
  int r = rng_.rand(100);
  auto weighted = [&]() {
    int C = 1, I = 1;
    if (ciWeights) ciWeights(C, I);
    if (C < 0) C = 0;
    if (I < 0) I = 0;
    if (C + I <= 0) return int(CAT_COM);
    int s = rng_.rand(C + I);
    return s > C ? int(CAT_IND) : int(CAT_COM);
  };
  switch (cat) {
    case CAT_RES: return r < p_.dest[0][0] ? int(CAT_RES) : weighted();
    case CAT_COM: return r < p_.dest[1][0] ? int(CAT_RES) : (r < p_.dest[1][1] ? int(CAT_COM) : int(CAT_IND));
    case CAT_IND: return r < p_.dest[2][0] ? int(CAT_RES) : (r < p_.dest[2][1] ? int(CAT_COM) : int(CAT_IND));
    default: return r < p_.dest[3][0] ? int(CAT_RES) : weighted();
  }
}

bool TrafficSim::fireBuilding(const TripBuilding& b) {
  if (b.category < 0) return false;
  if (b.x < 0 || b.z < 0 || b.x >= W_ || b.z >= H_) return false;
  TripRect origin{b.x, b.z, b.x + b.w - 1, b.z + b.h - 1};
  size_t first = size_t(b.x) * H_ + b.z;
  int r = rng_.rand(100);
  bool counted, firstTimePath = false;
  if (r > p_.tripThresholdPercent) {
    if (rec_.tripTried[first]) return false;
    counted = false;
    firstTimePath = true;
  } else {
    counted = true;
    if (b.functional) {
      int d = lotDensity ? lotDensity(b.x, b.z) : 0;
      int q = rng_.rand(100);
      int lim = int(float(p_.chanceCountingBase) + float(d) * float(p_.chanceDensityMultiplier) / 100.0f);
      counted = q <= lim;
    }
  }
  TripRequest req;
  req.origin = origin;
  req.originCategory = b.category;
  req.dest = chooseDestination(b.category);
  bool subject;
  permissions(b.category, req.rail, req.bus, req.subway, subject);
  if (subject) {
    int m = rng_.rand(100);
    bool allowed = firstTimePath ? m < massChance_ : m <= massChance_;
    if (!allowed) req.rail = req.bus = req.subway = false;
  }
  TripResult tr = routeTrip(req);
  rec_.tripsAttempted++;
  if (!tr.success) rec_.tripsFailed++;
  writeResult(origin, tr.success);
  if (tr.success && counted) applyCountedTrip(origin, tr);
  return true;
}

int TrafficSim::runWeekly(int day, const std::vector<TripBuilding>& bs) {
  int n = 0;
  for (auto& b : bs)
    if (b.category >= 0 && slotFires(b.slot, day) && fireBuilding(b)) n++;
  return n;
}

int TrafficSim::tripResult(int x, int z) const {
  size_t k = size_t(x) * H_ + z;
  if (!rec_.tripTried[k]) return 2;
  return rec_.tripOk[k] ? 0 : 1;
}
int TrafficSim::tripResultArea(const TripRect& r) const {
  for (int x = std::max(0, r.x0); x <= std::min(W_ - 1, r.x1); x++)
    for (int z = std::max(0, r.z0); z <= std::min(H_ - 1, r.z1); z++)
      if (tripResult(x, z) == 1) return 1;
  return 0;
}
void TrafficSim::clearLot(const TripRect& r) {
  for (int x = std::max(0, r.x0); x <= std::min(W_ - 1, r.x1); x++)
    for (int z = std::max(0, r.z0); z <= std::min(H_ - 1, r.z1); z++) {
      rec_.tripTried[size_t(x) * H_ + z] = 0;
      rec_.tripOk[size_t(x) * H_ + z] = 0;
    }
}

// ---- queries (7.1) ----
static int tileVal(const TrafficSim& s, int x, int z, unsigned mask, int init, bool defect, bool& has) {
  uint16_t f = s.flags(x, z);
  int v = 0;
  has = false;
  bool surf = (f & (TF_ROAD | TF_RAIL | TF_STATION | TF_XFER)) && (mask & (0x01 | 0x04 | 0x10 | 0x20));
  bool hwy = (f & TF_HIGHWAY) && (mask & 0x02);
  bool sub = (f & TF_SUBWAY) && (mask & 0x08);
  if (surf) { v = s.snapSurface(x, z); has = true; }
  if (hwy) { has = true; if (s.snapHighway(x, z) > v) v = s.snapHighway(x, z); }
  if (sub) { has = true; int c = defect ? init : v; if (s.snapSubway(x, z) > c) v = s.snapSubway(x, z); }
  return v;
}

int TrafficSim::tileValue(int x, int z, unsigned mask) const {
  bool has;
  return tileVal(*this, x, z, mask, 0, false, has);
}

int TrafficSim::areaAverage(const TripRect& r, unsigned mask, int init) const {
  long sum = 0, n = 0;
  for (int x = r.x0; x <= r.x1; x++)
    for (int z = r.z0; z <= r.z1; z++) {
      n++;
      if (x < 0 || z < 0 || x >= W_ || z >= H_) continue;
      bool has;
      sum += tileVal(*this, x, z, mask, init, true, has);
    }
  return n ? int(uint8_t(sum / n)) : 0;
}

void TrafficSim::areaSumCount(const TripRect& r, unsigned mask, int& sum, int& count) const {
  sum = count = 0;
  for (int x = std::max(0, r.x0); x <= std::min(W_ - 1, r.x1); x++)
    for (int z = std::max(0, r.z0); z <= std::min(H_ - 1, r.z1); z++) {
      bool has;
      int v = tileVal(*this, x, z, mask, 0, false, has);
      if (has) { sum += v; count++; }
    }
}

// ---- monthly ----
void TrafficSim::decay(const MonthlyInput& in) {
  float base = float(p_.monthlyDensityDecrement) * 0.01f;
  float rR = base, rH = base, rO = base;
  const auto& o = in.ord;
  if (o.parkingFines) rR -= float(p_.parkingFinesSub) / 100.0f;
  if (o.alternateDayDriving) { rR -= float(p_.alternateDaySub) / 100.0f; rH -= float(p_.alternateDaySub) / 100.0f; }
  if (o.carpool) { rR -= float(p_.carpoolSub) / 100.0f; rH -= float(p_.carpoolSub) / 100.0f; }
  if (o.crossingGuards) rR += float(p_.crossingGuardsAdd) / 100.0f;
  if (o.touristPromotion) { float t = float(p_.touristAdd) / 100.0f; rR += t; rH += t; rO += t; }
  for (size_t k = 0; k < rec_.liveSurface.size(); k++) {
    uint16_t f = flags_[k];
    float fac = ((f & TF_ROAD) && !(f & TF_STATION)) ? rR : rO;
    auto cl = [&](float v) { return uint8_t(std::min(std::max(int(v), 0), 255)); };
    rec_.liveSurface[k] = rec_.snapSurface[k] = cl(float(rec_.liveSurface[k]) * fac);
    rec_.liveHighway[k] = rec_.snapHighway[k] = cl(float(rec_.liveHighway[k]) * rH);
    rec_.liveSubway[k] = rec_.snapSubway[k] = cl(float(rec_.liveSubway[k]) * rO);
  }
}

void TrafficSim::computeStats(int road, int hwy, int rail, int sub) {
  long sumRoad = 0, sumTransit = 0, stnSurf = 0;
  for (size_t k = 0; k < flags_.size(); k++) {
    uint16_t f = flags_[k];
    int s = rec_.liveSurface[k], h = rec_.liveHighway[k], u = rec_.liveSubway[k];
    sumRoad += ((f & TF_ROAD) && !(f & TF_STATION) && s >= h) ? s : h;
    bool fromSurf = ((f & TF_STATION) || (f & TF_RAIL)) && s > u;
    sumTransit += fromSurf ? s : u;
    if (fromSurf && (f & TF_STATION)) stnSurf++;
  }
  long dr = long(road) + hwy;
  rec_.avgRoad = dr ? uint32_t(sumRoad / dr) : 0;
  long dt = long(rail) + sub + stnSurf;
  rec_.avgTransit = dt ? uint32_t(sumTransit / dt) : 0;
  if (rec_.tripsFailed > 0 && rec_.tripsAttempted > 0) rec_.failureRatio = float(rec_.tripsFailed) / float(rec_.tripsAttempted);
  rec_.tripsAttempted = rec_.tripsFailed = 0;
  // history: index 0 = most recent (INFERRED)
  for (int i = 11; i > 0; i--) rec_.history[i] = rec_.history[i - 1];
  rec_.history[0] = rec_.avgRoad;
}

int TrafficSim::strikeStep(int upkeep) {
  int F = transitFunding_;
  int S = 0;
  if (F < p_.minFundingForNoStrike && upkeep > 0)
    S = int(float(p_.minFundingForNoStrike - F) * float(p_.strikeChanceAtZeroFunding) / 100.0f);
  switch (rec_.strikeState) {
    case STRIKE_NORMAL:
      if (F <= 99 && upkeep > 0 && rng_.rand(100) <= S) { rec_.strikeState = STRIKE_THREATENED; return 1; }
      return 0;
    case STRIKE_THREATENED:
      if (F >= 100 || upkeep <= 0) { rec_.strikeState = STRIKE_NORMAL; rec_.strikeMonths = 0; return 3; }
      rec_.strikeState = STRIKE_STRIKING; rec_.strikeMonths = 0;
      return 2;
    case STRIKE_STRIKING:
      if (F < 100 && int(rec_.strikeMonths) <= p_.maxStrikeMonths && upkeep > 0) { rec_.strikeMonths++; return 0; }
      rec_.strikeState = STRIKE_NORMAL; rec_.strikeMonths = 0;
      return 3;
  }
  return 0;
}

int TrafficSim::roadConditionMessage(double X, int) const {
  float E = roadEfficiency();
  if (E == 1.0f) return -1;
  double v;
  if (E < 1.0f) {
    v = std::min(1.0, X + double(1.0f - E) * p_.damageEfficiencyMultiplier / 1000.0);
  } else {
    double M = std::max(1.0, p_.maxEfficiencyRoads * 0.01);
    if (M <= 1.0) return -1;
    v = std::max(0.0, X - (double(E - 1.0f) / (M - 1.0)) * p_.repairEfficiencyMultiplier / 1000.0);
  }
  return int(v * 100.0);
}

MonthlyOutput TrafficSim::monthly(const MonthlyInput& in) {
  MonthlyOutput out;
  out.roadUpkeep = roadUpkeep(p_, in.roadTiles, in.highwayTiles);
  out.transitUpkeep = transitUpkeep(p_, in.stationUpkeep, in.railTiles, in.subwayTiles);
  decay(in);                                                           // 1
  computeStats(in.roadTiles, in.highwayTiles, in.railTiles, in.subwayTiles);
  if (in.month == 1) { for (int i = 21; i > 12; i--) rec_.history[i] = rec_.history[i - 1]; rec_.history[12] = rec_.avgRoad;
    if (in.year % 10 == 0) { for (int i = 31; i > 22; i--) rec_.history[i] = rec_.history[i - 1]; rec_.history[22] = rec_.avgRoad; } }
  out.strikeEvent = strikeStep(out.transitUpkeep);                     // 2
  applyOrdinances(in.ord);                                             // 3
  out.roadConditionPercent = roadConditionMessage(in.roadCondition);  // 4
  rec_.faresLastMonth = rec_.faresThisMonth;                           // 5
  rec_.faresThisMonth = 0;
  return out;
}

}  // namespace m3k
