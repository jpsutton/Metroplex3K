#include "core/services.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace m3k { namespace us {

void chartMonthEnd(Chart& c, uint32_t sample, int month, int year) {
  // Spec 14 §6.3 rule 7 [STATIC]: monthly always; yearly on January; decade when year % 10 == 0.
  c.pushMonthly(sample);
  if (month == 1) {
    c.pushYearly(sample);
    if (year % 10 == 0) c.pushDecade(sample);
  }
}

static int64_t roundEven(double v) { return int64_t(std::nearbyint(v)); }

// ======================= Funding curve =======================
double FundingCurve::saturate(double x) const {
  if (M <= 1.0) return 1.0;  // guard for MaxEfficiency = 100 (division by zero in the original)
  const double k = double(4.999999f);
  double A = k * (E + 1.2 / (1.0 - M));
  double den = (A - 1.0 / (1.0 - M)) * x - A;
  if (den == 0.0) return M;
  return M - 1.0 / den;
}

FundedResult applyStationFunding(int upkeep0, const FundingCurve& c, int pct) {
  FundedResult r;
  double x = double(float(pct) * 0.01f);
  r.upkeep = int(roundEven(upkeep0 * x));
  if (r.upkeep > upkeep0) x = c.saturate(x);
  r.x = x;
  return r;
}

FundedResult applyFacilityFunding(int upkeep0, const FundingCurve& c, int pct) {
  FundedResult r;
  double x = double(float(pct) * 0.01f);
  r.upkeep = int(roundEven(upkeep0 * x));
  if (x > 1.0) x = c.saturate(x);
  r.x = x;
  return r;
}

// ======================= Coverage =======================
const int CoverageLayer::kPolicePhases[16][2] = {{4, 8}, {0, 12}, {12, 0}, {8, 12}, {12, 8}, {12, 12}, {0, 4}, {12, 4},
                                                  {4, 12}, {4, 4}, {4, 0}, {8, 8}, {8, 4}, {0, 8}, {8, 0}, {0, 0}};

static StationParams loadStation(const IniFile& f, const char* sec, bool police) {
  IniReader r{&f};
  StationParams p;
  p.radius0 = int(uint8_t(r.num(sec, "OptimalCoverageRadius", 0)));
  p.max0 = int(uint8_t(r.num(sec, "OptimalMaxCoverage", 0)));
  p.min0 = int(uint8_t(r.num(sec, "OptimalMinCoverage", 0)));
  p.upkeep0 = int(r.num(sec, "OptimalMonthlyUpkeep", 0));
  p.maxEfficiencyPct = int(r.num(sec, "MaxEfficiency", 120));
  p.mysticalE = r.real(sec, "MysticalE", 30.0);
  if (police) p.oppressionThreshold = int(r.num(sec, "OppressionThreshold", 0));
  return p;
}
StationParams StationParams::fire(const IniFile& f) { return loadStation(f, "FireStation", false); }
StationParams StationParams::police(const IniFile& f) { return loadStation(f, "PoliceStation", true); }

StampParams stationStamp(const StationParams& p, double x) {
  StampParams s;
  s.R = int(uint8_t(int(p.radius0 * x)));
  s.Max = int(uint8_t(int(p.max0 * x)));
  s.Min = int(uint8_t(int(p.min0 * x)));
  return s;
}

static CoverageParams loadCoverage(const IniFile& f, bool police) {
  IniReader r{&f};
  const char* S = "TuningParameters";
  CoverageParams p;
  int64_t up = r.num(S, "UpdatePeriod", 30);
  p.updatePeriod = up <= 0 ? 30 : int(up);
  p.dispatchRadius = int(uint8_t(r.num(S, "DispatchRadius", 0)));
  p.dispatchEffect = int(uint8_t(r.num(S, "DispatchEffect", 0)));
  p.minAcceptableFunding = int(uint8_t(r.num(S, "MinAcceptableFunding", 0)));
  p.strikeEfficiency = int(uint8_t(r.num(S, "StrikeEfficiency", 100)));
  p.maxStrikeDuration = int(r.num(S, "MaxStrikeDuration", 0));
  if (police) {
    p.maxAcceptable = int(uint8_t(r.num(S, "MaxAcceptableCrime", 255)));
    p.dailyJailReleaseRate = r.num(S, "DailyJailReleaseRate", 0) * 0.001;
    p.convictionRate = r.num(S, "ConvictionRate", 0) * 0.001;
    p.noJailPenalty = r.num(S, "NoJailPenalty", 100) * 0.01;
    p.maxJailOvercrowding = r.num(S, "MaxJailOvercrowding", 100) * 0.01;
  } else {
    p.maxAcceptable = int(uint8_t(r.num(S, "MaxAcceptableFlam", 255)));
  }
  return p;
}
CoverageParams CoverageParams::police(const IniFile& f) { return loadCoverage(f, true); }
CoverageParams CoverageParams::fire(const IniFile& f) { return loadCoverage(f, false); }

int StrikeMachine::step(SimRng& rng, uint32_t chance, bool endNow, uint32_t maxDuration) {
  if (striking) {
    counter++;
    if (counter > maxDuration || endNow) { striking = false; return -1; }
    return 0;
  }
  if (rng.rand(10000) < chance) { striking = true; counter = 0; return +1; }
  return 0;
}

uint32_t departmentStrikeChance(bool striking, bool haveStations, int funding, int minFunding,
                                int avgMetric, int maxAcceptable) {
  // Spec 17 §3.4 / Q-BN [STATIC]: fire uses this same form with MaxAcceptableFlam + flam average.
  if (striking) return 10000;
  if (!haveStations || funding >= minFunding || avgMetric <= maxAcceptable) return 0;
  int d = minFunding - funding;
  return uint32_t(d * d);
}

void CoverageLayer::init(bool police, int W, int H, const StationParams& sp, const CoverageParams& cp) {
  police_ = police; W_ = W; H_ = H; sp_ = sp; cp_ = cp;
  if (cp_.updatePeriod <= 0) cp_.updatePeriod = 30;
  cw_ = (W + 3) / 4; ch_ = (H + 3) / 4;
  acc_.assign(size_t(cw_) * ch_, 0);
  disp_.assign(size_t(cw_) * ch_, 0);
  stations_.clear(); units_.clear(); jails_.clear();
  unjailed = 0; funding_ = 100; upkeepTotal = 0;
  phase = 0; cursorX = 4; cursorZ = police ? 8 : 12;   // new city: police (4, 8), fire (4, 12) [OBSERVED, spec 30 s4]
  strike = StrikeMachine();
  sweepSumD_ = sweepCount_ = sweepZeros_ = 0;
}

void CoverageLayer::refreshCell(int cx, int cz) {
  int64_t A = acc_[cellIdx(cx, cz)];
  if (strike.striking) A = (cp_.strikeEfficiency * A) / 100;
  if (police_ && unjailed > 0) A = roundEven(double(A) * cp_.noJailPenalty);
  disp_[cellIdx(cx, cz)] = uint8_t(clampi(A / 16, 0, 255));
}

void CoverageLayer::stamp(int x0, int z0, int x1, int z1, StampParams s, int sign) {
  if (s.R == 0) return;
  int cxm = x0 + (x1 - x0) / 2, czm = z0 + (z1 - z0) / 2;
  int minCx = cw_, maxCx = -1, minCz = ch_, maxCz = -1;
  for (int x = std::max(0, x0 - s.R); x <= std::min(W_ - 1, x1 + s.R); x++)
    for (int z = std::max(0, z0 - s.R); z <= std::min(H_ - 1, z1 + s.R); z++) {
      int d = int(std::floor(std::sqrt(double((cxm - x) * (cxm - x) + (czm - z) * (czm - z)))));
      if (d > s.R) continue;
      int k = s.Max - ((s.Max - s.Min) * d) / s.R;
      int cx = x / 4, cz = z / 4;
      acc_[cellIdx(cx, cz)] += sign * k;
      minCx = std::min(minCx, cx); maxCx = std::max(maxCx, cx);
      minCz = std::min(minCz, cz); maxCz = std::max(maxCz, cz);
    }
  for (int cx = minCx; cx <= maxCx; cx++)
    for (int cz = minCz; cz <= maxCz; cz++) refreshCell(cx, cz);
}

int CoverageLayer::addStation(int x0, int z0, int x1, int z1) {
  FundedResult f = applyStationFunding(sp_.upkeep0, FundingCurve::make(sp_.maxEfficiencyPct, sp_.mysticalE), funding_);
  Station s{nextId_++, x0, z0, x1, z1, stationStamp(sp_, f.x)};
  stations_.push_back(s);
  stamp(x0, z0, x1, z1, s.st, +1);
  upkeepTotal = f.upkeep * int(stations_.size());
  return s.id;
}

bool CoverageLayer::removeStation(int id) {
  for (size_t i = 0; i < stations_.size(); i++)
    if (stations_[i].id == id) {
      const Station s = stations_[i];
      stations_.erase(stations_.begin() + i);
      stamp(s.x0, s.z0, s.x1, s.z1, s.st, -1);
      FundedResult f = applyStationFunding(sp_.upkeep0, FundingCurve::make(sp_.maxEfficiencyPct, sp_.mysticalE), funding_);
      upkeepTotal = f.upkeep * int(stations_.size());
      return true;
    }
  return false;
}

void CoverageLayer::setFunding(int pct) {
  funding_ = clampi(pct, 0, 255);
  FundingCurve c = FundingCurve::make(sp_.maxEfficiencyPct, sp_.mysticalE);
  FundedResult f = applyStationFunding(sp_.upkeep0, c, funding_);
  for (auto& s : stations_) {
    stamp(s.x0, s.z0, s.x1, s.z1, s.st, -1);
    s.st = stationStamp(sp_, f.x);
    stamp(s.x0, s.z0, s.x1, s.z1, s.st, +1);
  }
  upkeepTotal = f.upkeep * int(stations_.size());
}

void CoverageLayer::dispatch(int tx, int tz) {
  while (units_.size() >= stations_.size() + 1 && !units_.empty()) {
    Unit u = units_.front(); units_.erase(units_.begin());
    stamp(u.x0, u.z0, u.x1, u.z1, u.st, -1);
  }
  int x = tx + 1 < W_ ? tx + 1 : tx;
  Unit u{x, tz, x, tz, {cp_.dispatchRadius, cp_.dispatchEffect, cp_.dispatchEffect}};
  units_.push_back(u);
  stamp(u.x0, u.z0, u.x1, u.z1, u.st, +1);
}

int CoverageLayer::addJail(int capacity) {
  Jail j; j.id = nextId_++; j.capacity = capacity;
  jails_.push_back(j);
  redistribute();
  return j.id;
}
bool CoverageLayer::removeJail(int id) {
  for (size_t i = 0; i < jails_.size(); i++)
    if (jails_[i].id == id) {
      unjailed += jails_[i].inmates;
      jails_.erase(jails_.begin() + i);
      redistribute();
      return true;
    }
  return false;
}
void CoverageLayer::addPrisoners(int n) { unjailed += n; redistribute(); }

void CoverageLayer::redistribute() {
  if (jails_.empty()) { unjailed = unjailed * 95 / 100; return; }
  int64_t P = unjailed;
  int64_t sumC = 0;
  for (auto& j : jails_) { P += j.inmates; sumC += j.capacity; }
  double r = sumC > 0 ? std::min(double(P) / double(sumC), cp_.maxJailOvercrowding) : 0.0;
  for (auto& j : jails_) {
    if (P <= 0) { j.inmates = 0; continue; }
    if (r > 1.0) {
      j.inmates = int(roundEven(j.capacity * r));
      P = std::max<int64_t>(0, P - j.inmates);
    } else {
      j.inmates = int(std::min<int64_t>(j.capacity, P));
      P -= j.inmates;
    }
  }
  unjailed = int(P);
}

int CoverageLayer::coverageAt(int x, int z) const { return disp_[cellIdx(x / 4, z / 4)]; }
int CoverageLayer::accumulatorAt(int x, int z) const { return acc_[cellIdx(x / 4, z / 4)]; }

void CoverageLayer::advanceCursor() {
  cursorX += 16;
  if (int(cursorX) < W_) return;
  cursorZ += 16;
  if (int(cursorZ) >= H_) {
    phase++;
    if (phase > 15) {
      phase = 0;
      if (sweepCount_ > 0) {
        avgCoverage = uint32_t(sweepSumD_ / sweepCount_);
        pctCovered = uint32_t(100 - (100 * sweepZeros_) / sweepCount_);
      } else { avgCoverage = 0; pctCovered = 0; }
      sweepSumD_ = sweepCount_ = sweepZeros_ = 0;
    }
    cursorZ = uint32_t(kPolicePhases[phase][1]);
  }
  cursorX = uint32_t(kPolicePhases[phase][0]);
}

void CoverageLayer::tick(const CoverageTickEnv& env, SimRng& rng) {
  // 1. strike
  uint32_t chance = departmentStrikeChance(strike.striking, !stations_.empty(), funding_,
                                           cp_.minAcceptableFunding, env.averageMetric, cp_.maxAcceptable);
  lastStrikeChance = chance;
  bool endNow = funding_ >= 100 || stations_.empty();
  bool wasStriking = strike.striking;
  strike.step(rng, chance, endNow, uint32_t(cp_.maxStrikeDuration));
  bool strikeChanged = wasStriking != strike.striking;
  // 2. jail release
  if (police_)
    for (auto& j : jails_) j.inmates -= int(roundEven(j.inmates * cp_.dailyJailReleaseRate));
  (void)strikeChanged;
  // 3. sweep
  const int64_t N = int64_t(W_) * H_ / (16 * int64_t(cp_.updatePeriod));
  int64_t arrest = 0;
  for (int64_t i = 0; i < N; i++) {
    int ax = int(cursorX), az = int(cursorZ);
    int cx = ax / 4, cz = az / 4;
    if (cx < cw_ && cz < ch_) {
      int64_t A = acc_[cellIdx(cx, cz)];
      if (strike.striking) A = (cp_.strikeEfficiency * A) / 100;
      if (police_ && unjailed > 0) A = roundEven(double(A) * cp_.noJailPenalty);
      int D = clampi(A / 16, 0, 255);
      disp_[cellIdx(cx, cz)] = uint8_t(D);
      bool developed = false;
      if (env.occupied)
        for (int k = 0; k < 16 && !developed; k++) {
          int x = ax + (k % 4), z = az + (k / 4);
          if (x < W_ && z < H_ && env.occupied(x, z)) developed = true;
        }
      if (developed) { sweepCount_++; sweepSumD_ += D; if (D == 0) sweepZeros_++; }
      if (police_ && env.crimeAt && ax < W_ && az < H_) {
        int c = env.crimeAt(ax, az);
        if (c != 0 && D != 0) arrest += std::min(c, D);
      }
    }
    advanceCursor();
  }
  if (police_) {
    int n = int(roundEven(double(arrest) * cp_.convictionRate));
    if (n > 0) addPrisoners(n);
  }
}

Bytes CoverageLayer::serialize() const {
  Bytes b;
  b.push_back(phase);
  put32(b, cursorX); put32(b, cursorZ);
  b.push_back(strike.striking ? 1 : 0);
  put32(b, strike.counter);
  // Running sweep statistics (spec 17 s3.3, Persisted state) [STATIC]: developed cells visited so far, the sum of their
  // D, and how many had D = 0; then the last full sweep's average and percent covered.
  put32(b, uint32_t(sweepCount_));
  put32(b, uint32_t(sweepSumD_));
  put32(b, uint32_t(sweepZeros_));
  put32(b, avgCoverage); put32(b, pctCovered);
  return b;
}

void CoverageLayer::parse(const uint8_t* p, size_t n) {
  if (n != 34) throw FormatError("police/fire record size");
  Reader r(p, n);
  phase = r.u8(); cursorX = r.u32(); cursorZ = r.u32();
  strike.striking = r.u8() != 0;
  strike.counter = r.u32();
  sweepCount_ = r.u32();
  sweepSumD_ = r.u32();
  sweepZeros_ = r.u32();
  avgCoverage = r.u32(); pctCovered = r.u32();
  unjailed = 0;   // the unjailed prisoner count is not saved; it starts at 0 after load (spec 17 s3.3)
}

// ======================= Flammability =======================
FlammabilityParams FlammabilityParams::fromIni(const IniFile& f) {
  IniReader r{&f};
  FlammabilityParams p;
  p.leafBurningBanEffect = int(uint8_t(r.num("FlammabilityLayer", "LeafBurningBanOrdEffect", 0)));
  p.smokeDetectorsEffect = int(uint8_t(r.num("FlammabilityLayer", "SmokeDetectorsOrdEffect", 0)));
  p.noWaterEffect = int(uint8_t(r.num("FlammabilityLayer", "NoWaterEffect", 0)));
  return p;
}

int flammabilityValue(int b, const FlammabilityParams& p, const OrdinanceSet& o, bool noWater) {
  if (b <= 0) return 0;
  int u = 0;
  if (o.active(ord::LeafBurningBan)) u += p.leafBurningBanEffect;
  if (o.active(ord::SmokeDetectors)) u += p.smokeDetectorsEffect;
  if (noWater) u -= p.noWaterEffect;
  if (u >= 100) return 0;
  return clampi(int64_t(b) - (int64_t(b) * u) / 100, 0, 255);
}

int flammabilityBand(int v) {
  if (v <= 0) return 0;
  if (v <= 46) return 1;
  if (v <= 83) return 2;
  if (v <= 121) return 3;
  if (v <= 150) return 4;
  return 5;
}

void FlammabilityLayer::run(const std::function<int(int, int)>& baseAt,
                            const std::function<bool(int, int)>& noWaterAt, const OrdinanceSet& ord) {
  size_t total = size_t(W_) * H_;
  if (!total || W_ <= 0 || H_ <= 0) return;
  size_t n = total / 30;
  for (size_t i = 0; i < n; i++) {
    int x = int(cursorX), z = int(cursorZ);
    uint8_t v = uint8_t(flammabilityValue(baseAt ? baseAt(x, z) : 0, params_, ord,
                                          noWaterAt ? noWaterAt(x, z) : false));
    grid[size_t(x) * H_ + size_t(z)] = v;
    sweepSum += v;
    sweepCount++;
    cursorX++;
    if (int(cursorX) >= W_) {
      cursorX = 0;
      cursorZ++;
      if (int(cursorZ) >= H_) {
        cursorZ = 0;
        avg = uint8_t(sweepCount ? sweepSum / sweepCount : 0);
        sweepSum = 0;
        sweepCount = 0;
      }
    }
  }
}

Bytes FlammabilityLayer::serialize() const {
  Bytes b;
  writeRecHeader(b, 1);
  Bytes g = flipRows(grid, W_, H_);
  b.insert(b.end(), g.begin(), g.end());
  // Trailer: cursor + running sweep stats (spec 17 §4 / Q-BM) then an OPEN tail byte.
  put32(b, cursorX); put32(b, cursorZ); put32(b, sweepSum); put32(b, sweepCount);
  b.push_back(tailByte);
  return b;
}

void FlammabilityLayer::parse(const uint8_t* p, size_t n) {
  Reader r(p, n);
  RecHeader h = readRecHeader(r);
  if (h.version > 1) throw FormatError("flammability version");
  size_t cells = size_t(W_) * H_;
  if (r.remaining() != cells + 17) throw FormatError("flammability size");
  Bytes g = r.bytes(cells);
  grid = flipRows(g, W_, H_);
  cursorX = r.u32(); cursorZ = r.u32(); sweepSum = r.u32(); sweepCount = r.u32();
  tailByte = r.u8();
}

// ======================= Crime =======================
CrimeParams CrimeParams::fromIni(const IniFile& f) {
  IniReader r{&f};
  const char* S = "TuningParameters";
  CrimeParams p;
  int64_t up = r.num(S, "UpdatePeriod", 40);
  p.updatePeriod = uint16_t(up) == 0 ? 1 : int(uint16_t(up));
  int64_t pf = r.num(S, "PoliceFactor", 1);
  p.policeFactor = uint16_t(pf) == 0 ? 1 : int(uint16_t(pf));  // zero guarded (defect in original)
  p.youthCurfew = int16_t(r.num(S, "YouthCurfewOrdEffect", 0));
  p.gambling = int16_t(r.num(S, "GamblingOrdEffect", 0));
  p.jrSports = int16_t(r.num(S, "JrSportsOrdEffect", 0));
  p.neighborhoodWatch = int16_t(r.num(S, "NeighborhoodWatchOrdEffect", 0));
  p.conservationCorps = int16_t(r.num(S, "ConservationCorpsOrdEffect", 0));
  for (const auto& kv : f.section("AgentIDModifiers")) {
    int a[2] = {0, 0};
    if (parseFields(kv.second, "%d,%d", a, 2) < 2) continue;   // radius first, amount second
    CrimeEntry e;
    e.radius = uint8_t(a[0]);
    e.amount = clampi(a[1], -127, 127);
    p.table[parseIdNumber(kv.first)] = e;
  }
  return p;
}

void CrimeLayer::init(int W, int H, const CrimeParams& p, const AgentHierarchy* hier, uint32_t seed) {
  W_ = W; H_ = H; nx_ = W / 2; nz_ = H / 2;
  params_ = p;
  if (params_.updatePeriod <= 0) params_.updatePeriod = 1;
  if (params_.policeFactor <= 0) params_.policeFactor = 1;
  hier_ = hier;
  rnd_ = MsRand(seed);
  crime.assign(size_t(nx_) * nz_, 0);
  potential.assign(size_t(nx_) * nz_, 0);
  table = p.table;
  order.init(nx_, nz_);
  order.shuffle(rnd_);
  order.index = 1;
  if (!order.list.empty()) { cursorX = uint32_t((order.list[0] >> 8) * 2); cursorZ = uint32_t((order.list[0] & 255) * 2); }
  sweepSum = sweepCount = 0; avgCrime = 0;
  chart = Chart::fresh(0, 260);
}

CrimeEntry CrimeLayer::lookup(uint32_t type) {
  auto it = table.find(type);
  if (it != table.end()) return it->second;
  CrimeEntry best; int bestD = 1 << 30;
  if (hier_)
    for (const auto& kv : table) {  // nearest ancestor by parent-step distance (Q-BO); ascending id wins ties
      int d = hier_->distance(type, kv.first);
      if (d >= 0 && d < bestD) { bestD = d; best = kv.second; }
    }
  table[type] = best;   // cached (and saved), even when (0,0)
  return best;
}

void CrimeLayer::stampOccupant(const SimOccupant& o, int sign) {
  if (o.hasState && o.state != 1) return;
  CrimeEntry e = lookup(o.agentType);
  if (e.amount == 0) return;
  int x0 = std::max(0, o.x - e.radius), x1 = std::min(W_ - 1, o.x1() + e.radius);
  int z0 = std::max(0, o.z - e.radius), z1 = std::min(H_ - 1, o.z1() + e.radius);
  for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++) {
      int16_t& P = potential[size_t(x / 2) * nz_ + z / 2];
      P = int16_t(P + sign * e.amount);
    }
}

void CrimeLayer::rebuild(const std::vector<SimOccupant>& occ) {
  std::fill(potential.begin(), potential.end(), 0);
  for (const auto& o : occ) addOccupant(o);
}

int CrimeLayer::cellValue(int cx, int cz, const std::function<int(int, int)>& policeAt, const OrdinanceSet& ord) const {
  int64_t v = potential[size_t(cx) * nz_ + cz];
  for (int i = 0; i < 4; i++) {
    int x = cx * 2 + (i & 1), z = cz * 2 + (i >> 1);
    v -= (policeAt ? policeAt(x, z) : 0) / params_.policeFactor;
  }
  if (ord.active(ord::YouthCurfew)) v += params_.youthCurfew;
  if (ord.active(ord::Gambling)) v += params_.gambling;
  if (ord.active(ord::JrSports)) v += params_.jrSports;
  if (ord.active(ord::NeighborhoodWatch)) v += params_.neighborhoodWatch;
  if (ord.active(ord::ConservationCorps)) v += params_.conservationCorps;
  return clampi(v / 4, 0, 255);
}

void CrimeLayer::run(const std::function<int(int, int)>& policeAt, const OrdinanceSet& ord) {
  int64_t N = int64_t(W_) * H_ / (4 * int64_t(params_.updatePeriod));
  for (int64_t i = 0; i < N; i++) {
    int cx = int(cursorX / 2), cz = int(cursorZ / 2);
    if (cx < nx_ && cz < nz_) {
      int C = cellValue(cx, cz, policeAt, ord);
      crime[size_t(cx) * nz_ + cz] = uint8_t(C);
      if (potential[size_t(cx) * nz_ + cz] > 0) { sweepSum += 4 * C; sweepCount += 4; }
    }
    if (order.index >= int32_t(order.list.size())) {
      avgCrime = uint8_t(sweepCount ? sweepSum / sweepCount : 0);
      sweepSum = sweepCount = 0;
      order.shuffle(rnd_);
      order.index = 0;
    }
    if (order.list.empty()) break;
    uint16_t e = order.list[size_t(order.index++)];
    cursorX = uint32_t((e >> 8) * 2); cursorZ = uint32_t((e & 255) * 2);
  }
}

Bytes CrimeLayer::serialize() const {
  Bytes b;
  put16(b, recVersion); put16(b, streamVersion); put32(b, 0xDEADBEEFu);
  Bytes g = flipRows(crime, nx_, nz_);
  b.insert(b.end(), g.begin(), g.end());
  put32(b, cursorX); put32(b, cursorZ); put32(b, sweepSum); put32(b, sweepCount);
  b.push_back(avgCrime);
  chart.write(b);
  order.write(b);
  std::vector<int16_t> pf = flipRows(potential, nx_, nz_);
  for (auto v : pf) put16(b, uint16_t(v));
  put32(b, uint32_t(table.size()));
  for (const auto& kv : table) { put32(b, kv.first); putS32(b, kv.second.amount); b.push_back(kv.second.radius); }
  return b;
}

void CrimeLayer::parse(const uint8_t* p, size_t n, int W, int H) {
  W_ = W; H_ = H; nx_ = W / 2; nz_ = H / 2;
  Reader r(p, n);
  recVersion = r.u16(); streamVersion = r.u16();
  if (r.u32() != 0xDEADBEEFu) throw FormatError("crime sentinel");
  size_t cells = size_t(nx_) * nz_;
  crime = flipRows(r.bytes(cells), nx_, nz_);
  cursorX = r.u32(); cursorZ = r.u32();
  if (int(cursorX) >= W) cursorX = 0;
  if (int(cursorZ) >= H) cursorZ = 0;
  sweepSum = r.u32(); sweepCount = r.u32();
  avgCrime = r.u8();
  chart.parse(r);
  order.parse(r);
  std::vector<int16_t> pf(cells);
  for (auto& v : pf) v = r.s16();
  potential = flipRows(pf, nx_, nz_);
  uint32_t k = r.u32();
  table.clear();
  for (uint32_t i = 0; i < k; i++) {
    uint32_t id = r.u32(); CrimeEntry e; e.amount = r.s32(); e.radius = r.u8();
    table[id] = e;
  }
  if (r.remaining() != 0) throw FormatError("crime trailing bytes");
}

// ======================= Pollution =======================
PollutionParams PollutionParams::fromIni(const IniFile& f, const IniFile* tune, const IniFile* power) {
  IniReader r{&f};
  const char* S = "TuningParameters";
  PollutionParams p;
  int64_t up = r.num(S, "UpdatePeriod", 30);
  p.updatePeriod = uint16_t(up) == 0 ? 1 : int(uint16_t(up));
  p.maxAir = int(r.num(S, "MaxAirPolluteForUI", 0));
  p.maxWater = int(r.num(S, "MaxWaterPolluteForUI", 11000));
  p.maxGarbage = int(r.num(S, "MaxGarbagePolluteForUI", 0));
  p.airThreshold = int(r.num(S, "AirPollutedThreshold", 0));
  p.waterThreshold = int(r.num(S, "WaterPollutedThreshold", 0));
  p.garbageThreshold = int(r.num(S, "GarbagePollutedThreshold", 0));
  auto b = [&](const char* k) { return int(uint8_t(r.num(S, k, 0))); };
  p.cleanAir = b("CleanAirOrdEffect"); p.trashPresort = b("TrashPresortOrdEffect");
  p.conservationCorps = b("ConservationCorpsOrdEffect"); p.indPolluteFee = b("IndPolluteFeeOrdEffect");
  p.lawnChemBan = b("LawnChemBanOrdEffect"); p.tireRecycling = b("TireRecyclingOrdEffect");
  p.leafBurningBan = b("LeafBurningBanOrdEffect"); p.backyardComposting = b("BackyardCompostingOrdEffect");
  p.paperReduction = b("PaperReductionOrdEffect"); p.indWasteDisposal = b("IndWasteDisposalOrdEffect");
  p.landfillGasRecovery = b("LandfillGasRecoveryOrdEffect"); p.carSmogging = b("CarSmoggingOrdEffect");
  p.trafficAirFactor = int(r.num(S, "TrafficAirFactor", 0));
  p.garbageAbduction = int(r.num(S, "GarbageAbductionAmount", 0));
  // Spec 16 §4.1 / 17 §7.2.2 / Q-BT [STATIC]: store ×0.001; query UI only (not disposal).
  p.garbageScale = r.num(S, "GarbageScalingFactor", 0) * 0.001;
  for (const auto& kv : f.section("AgentIDModifiers")) {
    int a[5] = {0, 0, 0, 0, 0};
    if (parseFields(kv.second, "%d:%d,%d:%d,%d", a, 5) < 5) continue;
    PollutionEntry e;
    e.air = a[0]; e.rA = uint8_t(a[1]); e.water = a[2]; e.rW = uint8_t(a[3]); e.garbage = a[4];
    p.table[parseIdNumber(kv.first)] = e;
  }
  if (tune) {
    IniReader t{tune};
    p.treatmentCap = int(t.num("WaterTreatment", "OptimalPollutionCap", 0));
    p.treatmentMaxLifespan = int(t.num("WaterTreatment", "MaxLifespan", 0));
    p.treatmentDeclineAge = int(t.num("WaterTreatment", "DeclineAge", 0));
    p.incineratorCap = int(t.num("Incinerator", "OptimalGarbageCap", 0));
    p.recyclingPct = int(t.num("RecyclingCenter", "OptimalPctReduction", 0));
    p.recyclingPopServed = int(t.num("RecyclingCenter", "OptimalPopServed", 0));
    p.recyclingMaxLifespan = int(t.num("RecyclingCenter", "MaxLifespan", 0));
    p.recyclingDeclineAge = int(t.num("RecyclingCenter", "DeclineAge", 0));
  }
  if (power) {
    IniReader t{power};
    p.wteCap = int(t.num("WasteToEnergyPowerPlant", "OptimalGarbageCap", 0));
  }
  return p;
}

void PollutionLayer::init(int W, int H, const PollutionParams& p, const AgentHierarchy* hier, uint32_t seed) {
  W_ = W; H_ = H; nx_ = W / 2; nz_ = H / 2;
  params_ = p;
  if (params_.updatePeriod <= 0) params_.updatePeriod = 1;
  hier_ = hier;
  rnd_ = MsRand(seed);
  size_t cells = size_t(nx_) * nz_;
  state.assign(cells, 0);
  standing.assign(size_t(W) * H, 0);
  waterSource.assign(cells, 0); airSource.assign(cells, 0); garbageSource.assign(cells, 0);
  table = p.table;
  order.init(nx_, nz_);
  order.shuffle(rnd_);
  order.index = 1;
  if (!order.list.empty()) { cursorX = uint32_t((order.list[0] >> 8) * 2); cursorZ = uint32_t((order.list[0] & 255) * 2); }
  disp = GarbageDisposal();
  disp.abductionCap = p.garbageAbduction;
}

int PollutionLayer::queryGarbageScaled(int x, int z) const {
  // Spec 16 §4.1 / 17 §7.2.2 / Q-BT [STATIC]: trunc(G × s) for the pollution query UI line.
  if (x < 0 || z < 0 || x >= W_ || z >= H_) return 0;
  return int(std::trunc(double(standingGarbageAt(x, z)) * params_.garbageScale));
}

PollutionEntry PollutionLayer::lookup(uint32_t type) {
  auto it = table.find(type);
  if (it != table.end()) return it->second;
  PollutionEntry best; int bestD = 1 << 30;
  if (hier_)
    for (const auto& kv : table) {  // nearest ancestor by parent-step distance (Q-BO); ascending id wins ties
      int d = hier_->distance(type, kv.first);
      if (d >= 0 && d < bestD) { bestD = d; best = kv.second; }
    }
  table[type] = best;
  return best;
}

static void eraseTile(std::vector<std::pair<uint32_t, uint32_t>>& v, uint32_t x, uint32_t z) {
  for (size_t i = 0; i < v.size(); i++)
    if (v[i].first == x && v[i].second == z) { v.erase(v.begin() + i); return; }
}

void PollutionLayer::stampOccupant(const SimOccupant& o, int sign) {
  // Facility lists (water treatment, incinerator, WTE, recycling centre).
  std::vector<std::pair<uint32_t, uint32_t>>* lst = nullptr;
  switch (o.agentType) {
    case 12820: lst = &treatmentPlants; break;
    case 12852: lst = &incinerators; break;
    case 12853: lst = &wtePlants; break;
    case 12851: lst = &recyclingCentres; break;
  }
  if (lst) {
    if (sign > 0) lst->emplace_back(uint32_t(o.x), uint32_t(o.z)); else eraseTile(*lst, uint32_t(o.x), uint32_t(o.z));
  }
  PollutionEntry e = lookup(o.agentType);
  int a = sign * e.air, w = sign * e.water, g = sign * e.garbage;
  const int x0 = o.x, z0 = o.z, x1 = o.x1(), z1 = o.z1();
  for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++) {
      if (x < 0 || z < 0 || x >= W_ || z >= H_) continue;
      size_t c = size_t(x / 2) * nz_ + z / 2;
      airSource[c] += a; waterSource[c] += w;
      int v = garbageSource[c] + g;
      garbageSource[c] = int16_t(v < 0 ? 0 : v);
    }
  int R = std::max<int>(e.rA, e.rW);
  if (R == 0) return;
  for (int x = std::max(0, x0 - R); x <= std::min(W_ - 1, x1 + R); x++)
    for (int z = std::max(0, z0 - R); z <= std::min(H_ - 1, z1 + R); z++) {
      if (x >= x0 && x <= x1 && z >= z0 && z <= z1) continue;
      int dx = x < x0 ? x0 - x : (x > x1 ? x - x1 : 0);
      int dz = z < z0 ? z0 - z : (z > z1 ? z - z1 : 0);
      int d;
      if (dx == 0) d = dz; else if (dz == 0) d = dx;
      else d = std::max(dx, dz) + std::min(dx, dz) / 2;
      size_t c = size_t(x / 2) * nz_ + z / 2;
      if (d <= e.rA) airSource[c] += int32_t((int64_t(e.rA - d + 1) * a) / (e.rA + 1));
      if (d <= e.rW) waterSource[c] += int32_t((int64_t(e.rW - d + 1) * w) / (e.rW + 1));
    }
}

void PollutionLayer::rebuild(const std::vector<SimOccupant>& occ) {
  std::fill(airSource.begin(), airSource.end(), 0);
  std::fill(waterSource.begin(), waterSource.end(), 0);
  std::fill(garbageSource.begin(), garbageSource.end(), 0);
  treatmentPlants.clear(); incinerators.clear(); wtePlants.clear(); recyclingCentres.clear();
  for (const auto& o : occ) addOccupant(o);
}

void PollutionLayer::finaliseSweep() {
  // Spec 17 s7.4 [STATIC]: averages over the polluted-cell count (+4 per cell with a > 0, w > 0 or garbage), 0 when the
  // count is 0 or the sum is <= 0.
  avgAir = pollutedCount > 0 && sumAir > 0 ? uint32_t(int64_t(sumAir) / int64_t(pollutedCount)) : 0;
  avgWater = pollutedCount > 0 && sumWater > 0 ? uint32_t(int64_t(sumWater) / int64_t(pollutedCount)) : 0;
  rawWaterLast = rawWaterSum; treatedLast = treatedSum;
  { bool any = false; for (auto v : standing) if (v & 0x7FFF) { any = true; break; } standingAtSweepEnd = any; }
  sumAir = sumWater = 0; rawWaterSum = treatedSum = 0; pollutedCount = 0;
}

void PollutionLayer::processCell(int tx, int tz, const PollutionEnv& env, int64_t& treatLeft, int64_t& /*cells*/) {
  const PollutionParams& P = params_;
  const OrdinanceSet& O = env.ordinances;
  const size_t cell = size_t(tx / 2) * nz_ + tz / 2;
  int64_t a = airSource[cell], w = waterSource[cell], g = garbageSource[cell];
  // 1. city-wide ordinances
  if (O.active(ord::CleanAir)) a = reduceByPercent(a, P.cleanAir);
  if (O.active(ord::TrashPresort)) g = reduceByPercent(g, P.trashPresort);
  if (O.active(ord::ConservationCorps)) w = reduceByPercent(w, P.conservationCorps);
  if (O.active(ord::IndPolluteFee)) { w = reduceByPercent(w, P.indPolluteFee); a = reduceByPercent(a, P.indPolluteFee); }
  if (O.active(ord::LawnChemBan)) w = reduceByPercent(w, P.lawnChemBan);
  if (O.active(ord::TireRecycling)) g = reduceByPercent(g, P.tireRecycling);

  int tiles[4][2] = {{tx, tz}, {tx + 1, tz}, {tx, tz + 1}, {tx + 1, tz + 1}};
  auto zoneOf = [&](int x, int z) -> int {
    if (!env.zone || x >= W_ || z >= H_) return 0;
    return (*env.zone)[size_t(x) * H_ + z];
  };
  // 2. landfill accounting
  int n = 0;
  for (auto& t : tiles) {
    if (t[0] >= W_ || t[1] >= H_) continue;
    size_t ti = size_t(t[0]) * H_ + t[1];
    if (zoneOf(t[0], t[1]) != 17) {
      n++;
      // a non-landfill tile that still carries the landfill flag loses it; 32767 leaves the store's room (spec 17 s7.2.4)
      if (standing[ti] & 0x8000) { standing[ti] &= 0x7FFF; if (landfillTiles) landfillTiles--; disp.store.D -= 32767; }
    }   // registration of zone-17 tiles is done by the landfill developer (spec 15 s7)
  }
  // 3. per-tile terms
  int64_t SA = a, SW = w;
  bool hasGarbage = false;
  for (int i = 0; i < 4; i++) {
    int x = tiles[i][0], z = tiles[i][1];
    if (x >= W_ || z >= H_) continue;
    size_t ti = size_t(x) * H_ + z;
    int zt = zoneOf(x, z);
    if (zt >= 1 && zt <= 2) {
      if (O.active(ord::LeafBurningBan) && n) SA += (int64_t(P.leafBurningBan) * a) / (-100 * n);
      if (O.active(ord::BackyardComposting) && n) g += (g * P.backyardComposting) / (-100 * n);
    } else if (zt >= 5 && zt <= 7) {
      if (O.active(ord::PaperReduction) && n) g += (g * P.paperReduction) / (-100 * n);
    } else if ((zt >= 9 && zt <= 11) || zt == 15) {
      if (O.active(ord::IndWasteDisposal) && n) g += (g * P.indWasteDisposal) / (-100 * n);
    } else if (zt == 17) {
      if (O.active(ord::LandfillGasRecovery) && n != 4) SA += (int64_t(P.landfillGasRecovery) * SA) / (100 * (n - 4));
    }
    int64_t Gt = standing[ti] & 0x7FFF;
    if (Gt > 0) { SW += (8 * Gt) / 32767; hasGarbage = true; }
    int64_t T = env.traffic ? (*env.traffic)[ti] : 0;
    SA += T * P.trafficAirFactor;
    if (O.active(ord::CarSmogging)) SA -= (int64_t(P.carSmogging) * T * P.trafficAirFactor) / 400;
    if (zt != 17 && n) {   // spec 16 s4.3 "Per tile"
      int64_t q = g / n;
      disp.month[GarbageDisposal::Produced] += q;
      if (ratio > 0 && q > 0) {   // recycling ratio r
        double qs = double(q) * double(ratio);
        if ((i == 1 || i == 3) && qs > 0.5) qs -= 0.5;
        int64_t k = std::min<int64_t>(q, int64_t(std::llround(qs)));
        q -= k; disp.month[GarbageDisposal::Recycled] += k;
      }
      q += Gt;
      standing[ti] &= 0x8000;
      int64_t left = disp.dispose(q);
      if (left > 0) {
        LandfillStore& st = disp.store;
        int64_t keep;
        if (left < 32767 && st.V < 0) {   // the tile takes back part of the backlog
          int64_t t = std::min<int64_t>(-int64_t(st.V), g == 0 ? 1 : g / n);
          t = std::min<int64_t>({t, 32767 - left, std::max<int64_t>(0, st.S)});
          st.S -= t; keep = left + t;
        } else if (left < 32768) keep = left;
        else { keep = 32767; st.S += left - 32767; }
        standing[ti] = uint16_t((standing[ti] & 0x8000) | keep);
      }
    }
  }
  // 4. water treatment
  int64_t rawW = SW, treated = 0;
  if (treatLeft > 0 && SW > 0) {
    int64_t t;
    if (rawWaterLast <= 0 || treatedLast < treatLeft) t = treatLeft;
    else t = std::max<int64_t>(1, roundEven(double(treatedLast) / double(rawWaterLast) * double(SW)));
    t = std::min({t, SW, treatLeft});
    SW -= t; treatLeft -= t; treated = t;
  }
  // 5. targets and statistics
  int64_t Atarget = clampi(SA / 4, 0, 32767), Wtarget = clampi(SW / 4, 0, 32767);
  sumAir += int32_t(SA); sumWater += int32_t(SW); rawWaterSum += int32_t(rawW); treatedSum += int32_t(treated);
  if (a > 0 || w > 0 || hasGarbage) pollutedCount += 4;
  // 6. relaxation
  uint32_t& st = state[cell];
  int64_t A = (st >> 15) & 0x7FFF, Wc = st & 0x7FFF;
  int64_t stepA = P.maxAir / 8;
  if (Atarget > A) A = std::min(Atarget, A + stepA); else A = std::max(Atarget, A - stepA);
  if (Wtarget > Wc) Wc = std::min<int64_t>(Wtarget, Wc + P.maxWater / 12);
  else if (Wtarget < Wc) {
    int64_t nw = std::max<int64_t>(Wtarget, Wc - P.maxWater / 120);
    if (nw <= 0) {
      static const int dx[4] = {-1, 0, 1, 0}, dz[4] = {0, -1, 0, 1};
      for (int d = 0; d < 4; d++) {
        int x = tx + dx[d], z = tz + dz[d];
        if (x < 0 || z < 0 || x >= W_ || z >= H_) continue;
        if (waterAt(x, z) > 1) { nw = 1; break; }   // later two read the same (old) cell: original behaviour
      }
    }
    Wc = nw;
  }
  // Keep sea-water bit 30 only. Spec 17 §7.2.1 / Q-BV [STATIC]: bit 31 unused — leave clear.
  st = (st & 0x40000000u) | (uint32_t(A) << 15) | uint32_t(Wc);
}

void PollutionLayer::run(const PollutionEnv& env) {
  const PollutionParams& P = params_;
  int64_t wteN = int64_t(wtePlants.size()), incN = int64_t(incinerators.size()), trN = int64_t(treatmentPlants.size());
  // [OBSERVED] OptimalGarbageCap is a per-run (daily) figure: with it the monthly burn of shipped cities is 30x the INI value
  // (WTE 1,750 -> 52,500 per plant, incinerator 1,500 -> 45,000), which is what the saved garbage histories show (Madison
  // 3 WTE burn its whole 104k/month remainder; Liverpool 3 incinerators burn 135,000 = 3 x 45,000 every month).
  int64_t wteBud = wteRb_.next(30 * wteN * P.wteCap * env.wteFunding / 100);
  int64_t incBud = incRb_.next(30 * incN * P.incineratorCap * env.incineratorFunding / 100);
  // Treatment capacity C_T (spec 16 s3.1, 17 s7.2.4) [STATIC]: the sum of the plants' current capacities, taken at the
  // start of every run (per run, not per month; no funding factor). The capacities are set monthly by the city
  // (setTreatmentCaps); until the first such update every listed plant counts at OptimalPollutionCap.
  int64_t trBud = 0;
  if (treatmentCapsSet_) for (int64_t v : treatmentCaps_) trBud += v;
  else trBud = trN * P.treatmentCap;
  burnCapA = uint32_t(incBud); burnCapB = uint32_t(wteBud);
  disp.abductionCap = P.garbageAbduction;
  disp.beginRun(wteBud, incBud, env.exportDeal);
  if (env.importMonthly > 0) {   // imported garbage (deal kind 5) enters first; what is not disposed waits in the store
    int64_t i = env.importMonthly / 30;
    disp.month[GarbageDisposal::Produced] += i; disp.month[GarbageDisposal::Imported] += i;
    disp.store.S += disp.dispose(i);
  }
  int64_t N = int64_t(W_) * H_ / (4 * int64_t(P.updatePeriod));
  int64_t cells = 0;
  int64_t treatLeft = trBud;
  for (int64_t i = 0; i < N; i++) {
    int cx = int(cursorX / 2), cz = int(cursorZ / 2);
    if (cx < nx_ && cz < nz_) processCell(cx * 2, cz * 2, env, treatLeft, cells);
    if (order.index >= int32_t(order.list.size())) {
      finaliseSweep();
      order.shuffle(rnd_);
      order.index = 0;
    }
    if (order.list.empty()) break;
    uint16_t e = order.list[size_t(order.index++)];
    cursorX = uint32_t((e >> 8) * 2); cursorZ = uint32_t((e & 255) * 2);
  }
  if (disp.store.V < 0) {   // the backlog goes through the stages once more (spec 16 s4.3 step 6)
    int64_t b = std::min<int64_t>(-int64_t(disp.store.V), std::max<int64_t>(0, disp.store.S));
    disp.store.S -= b - disp.dispose(b, false);
  }
  uint32_t ts = 0;
  for (auto v : standing) ts += v & 0x7FFF;
  totalStanding = ts;
}

void PollutionLayer::registerLandfill(int x, int z) {
  uint16_t& w = standing[size_t(x) * H_ + z];
  if (w & 0x8000) return;
  w |= 0x8000; landfillTiles++;
  disp.store.D += 32767 - (w & 0x7FFF);
}

float PollutionLayer::recyclingRatio(std::vector<RecyclingCentre> cs, int64_t P) {
  if (cs.empty() || P <= 0) return 0;
  std::stable_sort(cs.begin(), cs.end(), [](const RecyclingCentre& a, const RecyclingCentre& b) { return a.pct > b.pct; });
  double served = 0, w = 0;
  for (const auto& c : cs) {
    if (served >= double(P)) break;
    served += double(c.popServed); w += double(c.pct) * double(c.popServed) / 100.0;
  }
  if (served <= 0) return 0;
  return float(served < double(P) ? w / double(P) : w / served);
}

int64_t PollutionLayer::treatmentCapacity(bool working, int age) const {
  const PollutionParams& P = params_;
  if (!working || (P.treatmentMaxLifespan > 0 && age >= P.treatmentMaxLifespan)) return 0;
  int64_t cap = P.treatmentCap;
  if (age > P.treatmentDeclineAge && P.treatmentMaxLifespan > P.treatmentDeclineAge)
    cap -= int64_t(age - P.treatmentDeclineAge) * cap / (P.treatmentMaxLifespan - P.treatmentDeclineAge);
  return cap;
}

PollutionLayer::RecyclingCentre PollutionLayer::recyclingCentreValues(bool working, int age) const {
  const PollutionParams& P = params_;
  if (!working || (P.recyclingMaxLifespan > 0 && age >= P.recyclingMaxLifespan)) return {0, 0};
  auto aged = [&](int64_t v) {
    if (age <= P.recyclingDeclineAge || P.recyclingMaxLifespan <= P.recyclingDeclineAge) return v;
    return v - int64_t(age - P.recyclingDeclineAge) * v / (P.recyclingMaxLifespan - P.recyclingDeclineAge);
  };
  return {aged(P.recyclingPct), aged(P.recyclingPopServed)};
}

void PollutionLayer::monthEnd(int month, int year, int64_t population, const std::vector<RecyclingCentre>* centres) {
  // UI combined air/water % (spec 17 §7.4 / 14 §6.3 rule 7) [STATIC]:
  // min(100, (⌊avgWater×100÷MaxWaterPolluteForUI⌋ + ⌊avgAir×100÷MaxAirPolluteForUI⌋) ÷ 2), unsigned.
  // Saved samples: Berlin 11, Madison 2, Madrid 19, London 4.
  {
    uint32_t w = params_.maxWater > 0 ? uint32_t(uint64_t(avgWater) * 100 / uint32_t(params_.maxWater)) : 0;
    uint32_t a = params_.maxAir > 0 ? uint32_t(uint64_t(avgAir) * 100 / uint32_t(params_.maxAir)) : 0;
    chartMonthEnd(chart, std::min<uint32_t>(100, (w + a) / 2), month, year);
  }
  disp.monthEnd();
  disp.store.monthly();
  if (centres) ratio = recyclingRatio(*centres, population);
  else ratio = recyclingRatio(std::vector<RecyclingCentre>(recyclingCentres.size(), recyclingCentreValues(true, 0)), population);
}

Bytes PollutionLayer::serialize() const {
  Bytes b;
  put16(b, recVersion); put16(b, streamVersion); put32(b, 0xDEADBEEFu);
  const bool markers = streamVersion >= 3;
  if (markers) putStr(b, "Pollution Layer Start");
  for (auto v : flipRows(state, nx_, nz_)) put32(b, v);
  { uint8_t gs[37]; disp.store.write(gs); b.insert(b.end(), gs, gs + 37); }
  for (auto v : flipRows(standing, W_, H_)) put16(b, v);
  for (int i = 0; i < 7; i++) put64(b, disp.totals[i]);
  put32(b, avgAir); put32(b, avgWater); put16(b, unk16);
  putS32(b, sumAir); putS32(b, sumWater); put32(b, totalStanding); put32(b, pollutedCount);
  chart.write(b);
  for (int i = 0; i < 7; i++) put64(b, disp.month[i]);
  for (int m = 0; m < 12; m++) for (int i = 0; i < 7; i++) put64(b, disp.history[m][i]);
  b.push_back(unk8);
  put32(b, burnCapA); put32(b, burnCapB);
  uint32_t fr; std::memcpy(&fr, &ratio, 4); put32(b, fr);
  put32(b, unk32);
  put32(b, cursorX); put32(b, cursorZ);
  putS32(b, rawWaterSum); putS32(b, rawWaterLast); putS32(b, treatedSum); putS32(b, treatedLast);
  order.write(b);
  put32(b, landfillTiles);
  for (auto v : flipRows(waterSource, nx_, nz_)) putS32(b, v);
  for (auto v : flipRows(airSource, nx_, nz_)) putS32(b, v);
  for (auto v : flipRows(garbageSource, nx_, nz_)) put16(b, uint16_t(v));
  put32(b, uint32_t(table.size()));
  for (const auto& kv : table) {
    put32(b, kv.first); putS32(b, kv.second.air); putS32(b, kv.second.water);
    b.push_back(kv.second.rA); b.push_back(kv.second.rW); putS32(b, kv.second.garbage);
  }
  for (const auto* l : {&treatmentPlants, &incinerators, &wtePlants, &recyclingCentres}) {
    put32(b, uint32_t(l->size()));
    for (const auto& pr : *l) { put32(b, pr.first); put32(b, pr.second); }
  }
  if (markers) putStr(b, "Pollution Layer End");
  return b;
}

void PollutionLayer::parse(const uint8_t* p, size_t n, int W, int H) {
  W_ = W; H_ = H; nx_ = W / 2; nz_ = H / 2;
  Reader r(p, n);
  recVersion = r.u16(); streamVersion = r.u16();
  if (r.u32() != 0xDEADBEEFu) throw FormatError("pollution sentinel");
  const bool markers = streamVersion >= 3;
  if (markers) expectStr(r, "Pollution Layer Start");
  const size_t cells = size_t(nx_) * nz_, tiles = size_t(W) * H;
  std::vector<uint32_t> sf(cells); for (auto& v : sf) v = r.u32();
  state = flipRows(sf, nx_, nz_);
  // Spec 17 §7.2.1 / Q-BV [STATIC]: bit 31 unused in normal play; clear on load.
  for (auto& v : state) v &= ~0x80000000u;
  { Bytes gs = r.bytes(37); disp.store.parse(gs.data()); }
  std::vector<uint16_t> st(tiles); for (auto& v : st) v = r.u16();
  standing = flipRows(st, W, H);
  for (int i = 0; i < 7; i++) disp.totals[i] = rd64(r);
  avgAir = r.u32(); avgWater = r.u32(); unk16 = r.u16();
  sumAir = r.s32(); sumWater = r.s32(); totalStanding = r.u32(); pollutedCount = r.u32();
  standingAtSweepEnd = totalStanding != 0;
  chart.parse(r);
  for (int i = 0; i < 7; i++) disp.month[i] = rd64(r);
  for (int m = 0; m < 12; m++) for (int i = 0; i < 7; i++) disp.history[m][i] = rd64(r);
  unk8 = r.u8();
  burnCapA = r.u32(); burnCapB = r.u32();
  { uint32_t fr = r.u32(); std::memcpy(&ratio, &fr, 4); }
  unk32 = r.u32();
  cursorX = r.u32(); cursorZ = r.u32();
  rawWaterSum = r.s32(); rawWaterLast = r.s32(); treatedSum = r.s32(); treatedLast = r.s32();
  order.parse(r);
  landfillTiles = r.u32();
  std::vector<int32_t> ws(cells), as(cells); std::vector<int16_t> gs16(cells);
  for (auto& v : ws) v = r.s32();
  for (auto& v : as) v = r.s32();
  for (auto& v : gs16) v = r.s16();
  waterSource = flipRows(ws, nx_, nz_); airSource = flipRows(as, nx_, nz_); garbageSource = flipRows(gs16, nx_, nz_);
  uint32_t k = r.u32();
  table.clear();
  for (uint32_t i = 0; i < k; i++) {
    uint32_t id = r.u32(); PollutionEntry e;
    e.air = r.s32(); e.water = r.s32(); e.rA = r.u8(); e.rW = r.u8(); e.garbage = r.s32();
    table[id] = e;
  }
  for (auto* l : {&treatmentPlants, &incinerators, &wtePlants, &recyclingCentres}) {
    uint32_t c = r.u32();
    if (c > r.remaining() / 8) throw FormatError("pollution list");
    l->clear();
    for (uint32_t i = 0; i < c; i++) { uint32_t x = r.u32(), z = r.u32(); l->emplace_back(x, z); }
  }
  if (markers) expectStr(r, "Pollution Layer End");
  if (r.remaining() != 0) throw FormatError("pollution trailing bytes");
}

// ======================= Aura =======================
const int AuraLayer::kPhases[16][2] = {{4, 8}, {0, 12}, {4, 0}, {8, 12}, {12, 8}, {0, 8}, {0, 4}, {12, 4},
                                       {4, 12}, {4, 4}, {12, 0}, {8, 8}, {8, 4}, {12, 12}, {8, 0}, {0, 0}};

AuraParams AuraParams::fromIni(const IniFile& f) {
  IniReader r{&f};
  const char* S = "TuningParameters";
  AuraParams p;
  auto q = [&](const char* k, uint32_t d = 0) { return r.quirky(S, k, d); };
  uint16_t up = uint16_t(q("UpdatePeriod", 50));
  p.updatePeriod = up == 0 ? 1 : up;
  p.optimalPolice = uint8_t(q("OptimalPoliceCoverage"));
  p.optimalPoliceEffect = int8_t(q("OptimalPoliceCoverageEffect"));
  p.oppressiveEffect = int8_t(q("OppressivePoliceCoverageEffect"));
  p.inadequateEffect = int8_t(q("InadequatePoliceCoverageEffect"));
  p.crimeFactor = int8_t(q("CrimeFactor", uint32_t(-1)));
  if (p.crimeFactor == 0) p.crimeFactor = -1;
  p.fireFactor = int8_t(q("FireCoverageFactor"));
  p.airFactor = int16_t(q("AirPollutionFactor"));
  p.waterFactor = int16_t(q("WaterPollutionFactor"));
  p.garbageEffect = int8_t(q("StandingGarbageFactor"));
  p.radiationEffect = int8_t(q("RadiationFactor"));
  p.highEQFactor = int8_t(q("HighEQFactor")); p.lowEQFactor = int8_t(q("LowEQFactor"));
  p.highEQThreshold = uint8_t(q("HighEQThreshold")); p.lowEQThreshold = uint8_t(q("LowEQThreshold"));
  p.highLEFactor = int8_t(q("HighLEFactor")); p.lowLEFactor = int8_t(q("LowLEFactor"));
  p.highLEThreshold = uint8_t(q("HighLEThreshold")); p.lowLEThreshold = uint8_t(q("LowLEThreshold"));
  p.parkingFine = int8_t(q("ParkingFineOrdEffect"));
  p.altDriving = int8_t(q("AltDrivingOrdEffect"));
  p.nukeFree = int8_t(q("NukeFreeOrdEffect"));
  for (const auto& kv : f.section("AgentIDModifiers")) {
    uint32_t id = parseQuirky(kv.first);
    size_t comma = kv.second.find(',');
    if (comma == std::string::npos) continue;
    Mod m;
    m.radius = uint8_t(parseQuirky(kv.second.substr(0, comma)));   // text before the first comma
    m.effect = int8_t(parseQuirky(kv.second.substr(comma + 1)));   // includes trailing comment text
    if (m.effect == 0) continue;
    p.table[id] = m;
  }
  return p;
}

void AuraLayer::init(int W, int H, const AuraParams& p, const AgentHierarchy* hier) {
  W_ = W; H_ = H; cw_ = W / 4; ch_ = H / 4;
  params_ = p;
  if (params_.updatePeriod <= 0) params_.updatePeriod = 1;
  hier_ = hier;
  grid.assign(size_t(cw_) * ch_, 0);
  mod_.assign(size_t(cw_) * ch_, 0);
  cache_.clear();
  phase = 0; cursorX = 4; cursorZ = 8; sweepSum = 0; sweepCount = 0; cityAura = 0;
  chart = Chart::fresh(127, 260);
}

AuraParams::Mod AuraLayer::lookup(uint32_t type) {
  auto it = params_.table.find(type);
  if (it != params_.table.end()) return it->second;
  auto c = cache_.find(type);
  if (c != cache_.end()) return c->second;
  AuraParams::Mod m;
  if (hier_)
    for (const auto& kv : params_.table)   // lowest-numbered ancestor, regardless of distance
      if (hier_->isAncestorOrSelf(type, kv.first)) { m = kv.second; break; }
  cache_[type] = m;
  return m;
}

void AuraLayer::stampOccupant(const SimOccupant& o, int sign) {
  if (o.hasState && o.state != 1) return;
  AuraParams::Mod m = lookup(o.agentType);
  if (m.effect == 0) return;
  int x0 = std::max(0, o.x - m.radius), x1 = std::min(W_ - 1, o.x1() + m.radius);
  int z0 = std::max(0, o.z - m.radius), z1 = std::min(H_ - 1, o.z1() + m.radius);
  for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++) {
      int16_t& v = mod_[size_t(x / 4) * ch_ + z / 4];
      v = int16_t(v + sign * m.effect);
    }
}

void AuraLayer::rebuild(const std::vector<SimOccupant>& occ) {
  std::fill(mod_.begin(), mod_.end(), 0);
  for (const auto& o : occ) addOccupant(o);
}

void AuraLayer::advance() {
  cursorX += 16;
  if (int(cursorX) < W_) return;
  cursorZ += 16;
  if (int(cursorZ) >= H_) {
    phase++;
    if (phase > 15) {
      phase = 0;
      cityAura = int8_t(sweepCount ? int64_t(sweepSum) / int64_t(sweepCount) : 0);
      sweepSum = 0; sweepCount = 0;
    }
    cursorZ = uint32_t(kPhases[phase][1]);
  }
  cursorX = uint32_t(kPhases[phase][0]);
}

void AuraLayer::run(const AuraEnv& env) {
  const AuraParams& P = params_;
  const OrdinanceSet& O = env.ordinances;
  float sHi = P.optimalPolice < 255 ? float(P.oppressiveEffect - P.optimalPoliceEffect) / float(255 - P.optimalPolice) : 0.f;
  float sLo = P.optimalPolice > 0 ? float(P.optimalPoliceEffect - P.inadequateEffect) / float(P.optimalPolice) : 0.f;
  int ordSum = 0;
  if (O.active(ord::ParkingFine)) ordSum += P.parkingFine;
  if (O.active(ord::AltDriving)) ordSum += P.altDriving;
  if (O.active(ord::NukeFree)) ordSum += P.nukeFree;
  // City-wide EQ / LE terms (same for every counted tile).
  int64_t eqTerm = 0, leTerm = 0;
  int q = env.eq;
  if (q >= P.highEQThreshold) { if (150 - P.highEQThreshold != 0) eqTerm = (int64_t(q - P.highEQThreshold) * P.highEQFactor) / (150 - P.highEQThreshold); }
  else if (q <= P.lowEQThreshold) { if (P.lowEQThreshold != 0) eqTerm = (int64_t(P.lowEQThreshold - q) * P.lowEQFactor) / P.lowEQThreshold; }
  int L = env.lifeExpectancy;
  if (L >= P.highLEThreshold) { if (89 - P.highLEThreshold != 0) leTerm = (int64_t(L - P.highLEThreshold) * P.highLEFactor) / (89 - P.highLEThreshold); }
  else if (L <= P.lowLEThreshold) { if (P.lowLEThreshold != 0) leTerm = (int64_t(P.lowLEThreshold - L) * P.lowLEFactor) / P.lowLEThreshold; }

  const int64_t N = int64_t(W_) * H_ / (16 * int64_t(P.updatePeriod));
  for (int64_t it = 0; it < N; it++) {
    int cx = int(cursorX), cz = int(cursorZ);
    if (cx / 4 < cw_ && cz / 4 < ch_) {
      int64_t S = mod_[size_t(cx / 4) * ch_ + cz / 4];
      int n = 0;
      for (int i = 0; i < 16; i++) {
        int x = cx + (i % 4), z = cz + (i / 4);
        if (x >= W_ || z >= H_) continue;
        if (env.tileCounts && !env.tileCounts(x, z)) continue;
        int p = env.policeAt ? env.policeAt(x, z) : 0;
        float v;
        if (p > P.optimalPolice) v = float(p - P.optimalPolice) * sHi + float(P.optimalPoliceEffect);
        else v = float(P.optimalPoliceEffect) - float(P.optimalPolice - p) * sLo;
        S += int64_t(std::nearbyint(v));
        int f = env.fireAt ? env.fireAt(x, z) : 0;
        if (P.fireFactor != 0) S += f / P.fireFactor;
        int c = env.crimeAt ? env.crimeAt(x, z) : 0;
        S += c / P.crimeFactor;
        int air = env.airAt ? env.airAt(x, z) : 0, water = env.waterAt ? env.waterAt(x, z) : 0;
        if (P.airFactor != 0) S += air / P.airFactor;
        if (P.waterFactor != 0) S += water / P.waterFactor;
        if (env.garbageAt && env.garbageAt(x, z) != 0) S += P.garbageEffect;
        if (env.irradiated && env.irradiated(x, z)) S += P.radiationEffect;
        S += eqTerm + leTerm + ordSum;
        n++;
      }
      int A = n == 0 ? 0 : clampi(S / n, -127, 127);
      grid[size_t(cx / 4) * ch_ + cz / 4] = int8_t(A);
      sweepSum += A * n; sweepCount += uint32_t(n);
    }
    advance();
  }
}

Bytes AuraLayer::serialize() const {
  Bytes b;
  for (auto v : flipRows(grid, cw_, ch_)) b.push_back(uint8_t(v));
  b.push_back(phase);
  put32(b, cursorX); put32(b, cursorZ);
  putS32(b, sweepSum); put32(b, sweepCount);
  b.push_back(uint8_t(cityAura));
  chart.write(b);
  return b;
}

void AuraLayer::parse(const uint8_t* p, size_t n, int W, int H) {
  W_ = W; H_ = H; cw_ = W / 4; ch_ = H / 4;
  size_t C = size_t(cw_) * ch_;
  if (n != C + 150) throw FormatError("aura record size");
  Reader r(p, n);
  Bytes g = r.bytes(C);
  std::vector<int8_t> gs(C);
  for (size_t i = 0; i < C; i++) gs[i] = int8_t(g[i]);
  grid = flipRows(gs, cw_, ch_);
  phase = r.u8(); if (phase > 15) phase = 0;
  cursorX = r.u32(); if (int(cursorX) >= W) cursorX = 0;
  cursorZ = r.u32(); if (int(cursorZ) >= H) cursorZ = 0;
  sweepSum = r.s32(); sweepCount = r.u32();
  cityAura = int8_t(r.u8());
  chart.parse(r);
  if (mod_.size() != C) mod_.assign(C, 0);
}

// ======================= Health / education =======================
HospitalParams HospitalParams::fromIni(const IniFile& f) {
  IniReader r{&f};
  HospitalParams p;
  p.beds = uint32_t(r.num("Hospital", "OptimalBedCount", 0));
  p.doctors = uint32_t(r.num("Hospital", "OptimalDoctorCount", 0));
  p.hqEffect = std::max(0.001, r.num("Hospital", "OptimalHQEffect", 1) * 0.001);
  p.upkeep0 = int(r.num("Hospital", "OptimalMonthlyUpkeep", 0));
  p.maxEfficiencyPct = int(r.num("Hospital", "MaxEfficiency", 120));
  p.mysticalE = r.real("Hospital", "MysticalE", 30.0);
  return p;
}

HospitalFunding hospitalFunding(const HospitalParams& p, int pct) {
  FundedResult f = applyFacilityFunding(p.upkeep0, FundingCurve::make(p.maxEfficiencyPct, p.mysticalE), pct);
  HospitalFunding h;
  h.upkeep = f.upkeep; h.x = f.x;
  h.doctors = int(roundEven(p.doctors * f.x));
  h.effect = p.hqEffect * f.x;
  h.beds = p.beds;
  return h;
}

static SchoolParams loadSchool(const IniFile& f, const char* sec, const char* staffKey) {
  IniReader r{&f};
  SchoolParams p;
  p.desks = uint32_t(r.num(sec, "OptimalDeskCount", 0));
  p.staff = uint32_t(r.num(sec, staffKey, 0));
  p.minAge = int(uint8_t(r.num(sec, "MinAgeServed", 0)));
  p.maxAge = std::min(89, int(uint8_t(r.num(sec, "MaxAgeServed", 0))));
  p.eqEffect = std::max(0.001, r.num(sec, "OptimalEQEffect", 1) * 0.001);
  p.upkeep0 = int(r.num(sec, "OptimalMonthlyUpkeep", 0));
  p.maxEfficiencyPct = int(r.num(sec, "MaxEfficiency", 120));
  p.mysticalE = r.real(sec, "MysticalE", 30.0);
  return p;
}
SchoolParams SchoolParams::school(const IniFile& f) { return loadSchool(f, "School", "OptimalTeacherCount"); }
SchoolParams SchoolParams::college(const IniFile& f) { return loadSchool(f, "College", "OptimalProfCount"); }

SchoolFunding schoolFunding(const SchoolParams& p, int pct) {
  FundedResult f = applyFacilityFunding(p.upkeep0, FundingCurve::make(p.maxEfficiencyPct, p.mysticalE), pct);
  SchoolFunding s;
  s.upkeep = f.upkeep; s.x = f.x;
  s.staff = int(roundEven(p.staff * f.x));
  if (s.staff == 0 && f.x > 0) s.staff = 1;
  s.effect = p.eqEffect * f.x;
  return s;
}

static LibraryParams loadLib(const IniFile& f, const char* sec) {
  IniReader r{&f};
  LibraryParams p;
  p.popServed = uint32_t(r.num(sec, "PopServed", 0));
  p.minAge = int(uint8_t(r.num(sec, "MinAgeServed", 0)));
  p.maxAge = int(uint8_t(r.num(sec, "MaxAgeServed", 0)));
  p.eqEffect = std::max(0.001, r.num(sec, "EQEffect", 1) * 0.001);
  p.upkeep = int(r.num(sec, "MonthlyUpkeep", 0));
  return p;
}
LibraryParams LibraryParams::library(const IniFile& f) { return loadLib(f, "Library"); }
LibraryParams LibraryParams::museum(const IniFile& f) { return loadLib(f, "Museum"); }

double ageCoverage(double st, double people, double seats) {
  double d = std::max(people, seats);
  return d > 0 ? st / d : 0.0;
}
int64_t hospitalPatients(int p1, int p2, double pf, double hr, int64_t pop) {
  return int64_t((double((p1 + p2) / 2) * pf + hr) * double(pop));
}
double healthQuotient(double bt, double patients, double beds) {
  double d = std::max(patients, beds);
  return d > 0 ? bt / d : 0.0;
}
int ratingFromQuotient(double g, double optimalYearly) {
  double d = optimalYearly / 12.0;
  if (d <= 0) return 0;
  return clampi(int64_t(std::nearbyint(4.0 * g / d - 0.4)), 0, 4);
}
uint32_t monthlyStrikeChance(bool have, bool ratingLow, int funding, int minFunding) {
  if (!have || !ratingLow || funding >= minFunding) return 0;
  int d = minFunding - funding;
  return uint32_t(d * d);
}
double strikeMultiplier(int e, bool fix) { return fix ? e / 100.0 : double(e); }

}}  // namespace m3k::us
