#include "core/utilities.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <deque>

namespace m3k { namespace us {

// ======================= Power =======================
const std::vector<uint32_t>& powerPlantTypes() {
  static const std::vector<uint32_t> t = {12111, 12112, 12113, 12114, 12115, 12116, 12117, 12118, 12853};
  return t;
}
int powerCategory(uint32_t a) {
  const auto& t = powerPlantTypes();
  for (size_t i = 0; i < t.size(); i++) if (t[i] == a) return int(i);
  return -1;
}

static const char* plantSection(uint32_t a) {
  switch (a) {
    case 12111: return "CoalPowerPlant";
    case 12112: return "OilPowerPlant";
    case 12113: return "GasPowerPlant";
    case 12114: return "MicroWavePowerPlant";
    case 12115: return "NuclearPowerPlant";
    case 12116: return "FusionPowerPlant";
    case 12117: return "SolarPowerPlant";
    case 12118: return "WindPowerPlant";
    case 12853: return "WasteToEnergyPowerPlant";
  }
  return "";
}

PowerParams PowerParams::fromIni(const IniFile& f) {
  PowerParams p;
  IniReader r{&f};
  for (uint32_t t : powerPlantTypes()) {
    PlantTunables pt;
    pt.section = plantSection(t);
    pt.capacity = int(r.num("PowerPlantCapacities", std::to_string(t), 0));
    int64_t ml = r.num(pt.section, "MaxLifeSpan", 0);
    pt.maxLife = ml == 0 ? 1 : int(ml);
    pt.declineAge = int(r.num(pt.section, "DeclineAge", 0));
    if (pt.declineAge >= pt.maxLife) pt.declineAge = pt.maxLife / 2;
    pt.variationPct = int(r.num(pt.section, "PercentageVariationInLongevity", 0));
    p.plants[t] = pt;
  }
  p.savingConservationPct = int(r.num("MiscPowerLayerTunables", "SavingDueToPowerConservationOrdinance", 0));
  p.savingStairwellPct = int(r.num("MiscPowerLayerTunables", "SavingDueToStairwellLightingOrdinance", 0));
  p.minOverwork = int(r.num("MiscPowerPlantTunables", "MinOverworkMonths", p.minOverwork));
  p.maxOverwork = int(r.num("MiscPowerPlantTunables", "MaxOverworkMonths", p.maxOverwork));
  p.wteOptimalGarbageCap = int(r.num("WasteToEnergyPowerPlant", "OptimalGarbageCap", 0));
  return p;
}

uint32_t plantOutput(uint32_t cap, int age, int lmax, int ld) {
  if (age >= lmax) return 0;
  if (age > ld) {
    uint64_t dec = uint64_t(cap) * uint64_t(age - ld) / uint64_t(lmax - ld);
    return cap - uint32_t(dec);
  }
  return cap;
}

void rollLifespan(const PlantTunables& t, SimRng& rng, int& lmax, int& ld) {
  if (t.variationPct == 0) {
    lmax = t.maxLife; ld = t.declineAge;
  } else {
    double v = t.variationPct / 100.0;
    double f = rng.randReal(1.0 - v, 1.0 + v);
    lmax = int(t.maxLife * f);
    ld = int(t.declineAge * f);
  }
  if (ld >= lmax) lmax = ld + 1;
}

void PowerLayer::init(int W, int H, PowerParams p) {
  W_ = W; H_ = H; params_ = std::move(p);
  cons_.assign(size_t(W) * H, 0);
  powered_.assign(size_t(W) * H, 0);
  plants_.clear();
  stats_ = PowerStats();
}

int PowerLayer::addPlant(int x, int z, uint32_t agentType, int buildAbsMonth, SimRng& rng) {
  auto it = params_.plants.find(agentType);
  if (it == params_.plants.end()) return -1;
  PlantState p;
  p.id = nextId_++;
  p.agentType = agentType; p.x = x; p.z = z;
  p.capacity = it->second.capacity;
  p.buildAbsMonth = buildAbsMonth;
  rollLifespan(it->second, rng, p.lmax, p.ld);
  p.output = int(plantOutput(uint32_t(p.capacity), 0, p.lmax, p.ld));
  plants_.push_back(p);
  return p.id;
}

bool PowerLayer::removePlant(int id) {
  for (size_t i = 0; i < plants_.size(); i++)
    if (plants_[i].id == id) { plants_.erase(plants_.begin() + i); return true; }
  return false;
}

void PowerLayer::rebuildConsumption(const std::vector<SimOccupant>& occ) {
  clearConsumption();
  for (const auto& o : occ) {
    if (o.x < 0 || o.z < 0 || o.x >= W_ || o.z >= H_) continue;
    int c = o.powerUse >= 0 ? o.powerUse : o.densityClass * o.sizeX * o.sizeZ;
    setConsumption(o.x, o.z, c);
  }
}

// Connected component by 4-neighbour flood over the conductive mask. The original expands row-wise
// with a 600-pass cap; a plain flood is equivalent unless a network needs > 600 passes (not modelled).
std::vector<int> PowerLayer::flood(int sx, int sz, const PowerEnv& env) const {
  std::vector<int> out;
  std::vector<uint8_t> seen(size_t(W_) * H_, 0);
  std::deque<int> q;
  int s = sx * H_ + sz;
  seen[s] = 1; q.push_back(s);
  while (!q.empty()) {
    int c = q.front(); q.pop_front();
    out.push_back(c);
    int x = c / H_, z = c % H_;
    static const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
    for (int d = 0; d < 4; d++) {
      int nx = x + dx[d], nz = z + dz[d];
      if (nx < 0 || nz < 0 || nx >= W_ || nz >= H_) continue;
      int n = nx * H_ + nz;
      if (seen[n] || !env.conductive || !(*env.conductive)[n]) continue;
      seen[n] = 1; q.push_back(n);
    }
  }
  return out;
}

void PowerLayer::distribute(int sx, int sz, const PowerEnv& env, std::vector<uint8_t>& visited,
                            std::vector<int64_t>& catProduced) {
  std::vector<int> K = flood(sx, sz, env);
  for (int t : K) visited[t] = 1;
  std::vector<uint8_t> inK(size_t(W_) * H_, 0);
  for (int t : K) inK[t] = 1;

  int64_t S = 0;
  std::vector<PlantState*> here;
  for (auto& p : plants_) {
    if (inK[p.x * H_ + p.z]) {
      here.push_back(&p);
      S += p.output;
      int c = powerCategory(p.agentType);
      if (c >= 0) catProduced[c] += p.output;
    }
  }
  stats_.produced += S;

  std::vector<PowerDeal*> active;
  for (auto& d : deals_) {
    if (d.tx >= 0 && d.tz >= 0 && d.tx < W_ && d.tz < H_ && inK[d.tx * H_ + d.tz]) {
      d.reached = true; d.active = true; active.push_back(&d);
    }
  }
  int64_t rawS = S;
  if (active.empty()) S = int64_t(1.1 * double(S));
  int64_t D = 0;
  for (auto* d : active) D += d->amount;

  double k = 1.0;
  if (consPct_) k -= params_.savingConservationPct / 100.0;
  if (stairPct_) k -= params_.savingStairwellPct / 100.0;

  // Sorted scan order: z ascending, then x ascending.
  std::sort(K.begin(), K.end(), [&](int a, int b) {
    int az = a % H_, bz = b % H_;
    return az != bz ? az < bz : a < b;  // a<b same z => x ascending (index = x*H+z)
  });

  int64_t demandSum = 0;
  for (int t : K) demandSum += cons_[t];

  int64_t B = k > 0 ? int64_t(double(S + D) / k) : INT64_MAX / 4;
  bool brown = false;
  for (int t : K) {
    int c = cons_[t];
    if (c == 0) { powered_[t] = 1; continue; }
    powered_[t] = 1;
    stats_.poweredConsumerTiles++;
    if (c <= B) { B -= c; continue; }
    brown = true;
    B = 0;
    for (auto* d : active) d->amount = 0;
    stats_.consumed += S + D;
    break;
  }
  if (!brown) {
    int64_t left = int64_t(double(B) * k);
    stats_.consumed += S + D - left;
    int64_t rem = left;
    for (auto* d : active) {
      int64_t u = std::min<int64_t>(rem, d->amount);
      d->unused += u; rem -= u;
    }
  }
  for (auto* d : active) catProduced[9] += std::max<int64_t>(0, int64_t(d->amount) - d->unused);   // power drawn from neighbours

  // Overwork bookkeeping (load assignment is INFERRED: demand shared in proportion to output).
  int64_t demand = std::max<int64_t>(0, int64_t(double(demandSum) * k) - D);
  for (auto* p : here) {
    int64_t load = rawS > 0 ? demand * p->output / rawS : demand / int64_t(here.size());
    p->load = int(load);
    if (p->load > p->output) p->overwork++;
    else p->overwork = 0;
  }
}

std::vector<PlantEvent> PowerLayer::monthlyUpdate(const PowerEnv& env, SimRng& rng) {
  std::vector<PlantEvent> events;
  consPct_ = env.ordinances.active(ord::PowerConservation);
  stairPct_ = env.ordinances.active(ord::StairwellLighting);
  deals_.clear();
  for (size_t i = 0; i < env.deals.size() && i < 4; i++) { deals_.push_back(env.deals[i]); deals_.back().reached = false; deals_.back().active = false; deals_.back().unused = 0; }

  // Rolling production history: next slot is cleared and removed from the annual totals.
  monthIndex = (monthIndex + 1) % 12;
  for (int c = 0; c < 10; c++) { annualTotal[c] -= production[monthIndex][c]; production[monthIndex][c] = 0; }

  stats_ = PowerStats();
  std::fill(powered_.begin(), powered_.end(), 0);
  for (auto c : cons_) if (c) stats_.consumerTiles++;

  // Per-plant step.
  std::vector<int> explode;
  for (auto& p : plants_) {
    int age = env.nowAbsMonth - p.buildAbsMonth;
    if (p.expired) { p.output = int(plantOutput(uint32_t(p.capacity), age, p.lmax, p.ld)); continue; }
    uint32_t r = rng.randInt(uint32_t(params_.minOverwork), uint32_t(params_.maxOverwork));
    if (int64_t(r) < int64_t(p.overwork)) {
      explode.push_back(p.id);
      events.push_back({PlantEvent::Explosion, p.id, p.agentType, p.x, p.z});
      continue;
    }
    if (age > p.lmax) {
      p.expired = true;
      events.push_back({PlantEvent::Retired, p.id, p.agentType, p.x, p.z});
    }
    p.output = int(plantOutput(uint32_t(p.capacity), age, p.lmax, p.ld));
  }
  for (int id : explode) removePlant(id);

  for (auto& p : plants_) {
    int age = env.nowAbsMonth - p.buildAbsMonth;
    if (!p.expired && age >= p.ld) stats_.ageingPlants++;
  }

  std::vector<uint8_t> visited(size_t(W_) * H_, 0);
  std::vector<int64_t> cat(10, 0);
  for (const auto& p : plants_) {
    if (visited[p.x * H_ + p.z]) continue;
    distribute(p.x, p.z, env, visited, cat);
  }
  for (auto& d : deals_) {
    if (d.amount != 0 && !d.reached && d.tx >= 0 && d.tz >= 0 && d.tx < W_ && d.tz < H_ &&
        !visited[d.tx * H_ + d.tz])
      distribute(d.tx, d.tz, env, visited, cat);
  }

  for (int c = 0; c < 10; c++) {
    production[monthIndex][c] = int32_t(cat[c]);
    annualTotal[c] += int32_t(cat[c]);
  }

  if (stats_.produced > stats_.consumed)
    stats_.spareCapacityPct = int((100 * stats_.produced - 100 * stats_.consumed) / stats_.produced);
  else stats_.spareCapacityPct = 0;
  stats_.unpoweredConsumers = std::max(0, stats_.consumerTiles - stats_.poweredConsumerTiles);

  // power % graph sample: consumption as a percentage of production, clamped 0..100 [OPEN].
  uint32_t pct = stats_.produced > 0 ? uint32_t(std::min<int64_t>(100, 100 * stats_.consumed / stats_.produced)) : 0;
  chart.pushMonthly(pct);
  chart.recomputeAutoMax();
  return events;
}

void PowerLayer::recomputeCoverage(const PowerEnv& env) {
  consPct_ = env.ordinances.active(ord::PowerConservation);
  stairPct_ = env.ordinances.active(ord::StairwellLighting);
  stats_ = PowerStats();
  std::fill(powered_.begin(), powered_.end(), 0);
  for (auto c : cons_) if (c) stats_.consumerTiles++;
  std::vector<uint8_t> visited(size_t(W_) * H_, 0);
  std::vector<int64_t> cat(10, 0);
  for (const auto& p : plants_) {
    if (visited[p.x * H_ + p.z]) continue;
    distribute(p.x, p.z, env, visited, cat);
  }
  stats_.unpoweredConsumers = std::max(0, stats_.consumerTiles - stats_.poweredConsumerTiles);
}

Bytes PowerLayer::serialize() const {
  Bytes b;
  writeRecHeader(b, 1);
  putS32(b, monthIndex);
  for (int m = 0; m < 12; m++) for (int c = 0; c < 10; c++) putS32(b, production[m][c]);
  for (int c = 0; c < 10; c++) putS32(b, annualTotal[c]);
  chart.write(b);
  put32(b, tail);
  return b;
}

void PowerLayer::parse(const uint8_t* p, size_t n) {
  Reader r(p, n);
  RecHeader h = readRecHeader(r);
  if (h.version > 1) throw FormatError("power layer version");
  monthIndex = r.s32();
  for (int m = 0; m < 12; m++) for (int c = 0; c < 10; c++) production[m][c] = r.s32();
  for (int c = 0; c < 10; c++) annualTotal[c] = r.s32();
  chart.parse(r);
  tail = r.u32();
  if (r.remaining() != 0) throw FormatError("power layer trailing bytes");
}

// ======================= Water =======================
WaterParams WaterParams::fromIni(const IniFile& f) {
  WaterParams p;
  IniReader r{&f};
  const char* S = "TuningParameters";
  p.waterFromPipe = int(uint16_t(r.num(S, "WaterFromPipe", 0)));
  if (p.waterFromPipe == 0) p.waterFromPipe = 3;
  p.pumpDistanceFactor = int(r.num(S, "WaterPumpDistanceFactor", 0));
  p.towerCapa = int(r.num(S, "WaterTowerCapa", 0));
  p.pumpCapa = int(r.num(S, "WaterPumpCapa", 0));
  p.desaltCapa = int(r.num(S, "DesaltPlantCapa", 0));
  p.towerLifeYears = int(r.num(S, "WaterTowerLife", 0));
  p.pumpLifeYears = int(r.num(S, "WaterPumpLife", 0));
  p.desaltLifeYears = int(r.num(S, "DesaltPlantLife", 0));
  p.conservationFactor = int(r.num(S, "WaterConservationFactor", 0));
  p.meterFactor = int(r.num(S, "WaterMeterFactor", 0));
  p.defaultConsumption = int(r.num(S, "DefaultConsumption", 0));
  p.defaultLeak = int(r.num(S, "DefaultLeak", 0));
  for (const auto& kv : f.section("AgentIDModifiers")) {
    if (p.modifiers.size() >= 127) break;
    p.modifiers.emplace_back(parseIdNumber(kv.first), int(parseDecimal(kv.second, 0)));
  }
  std::stable_sort(p.modifiers.begin(), p.modifiers.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
  return p;
}

int WaterParams::consumptionPerTile(uint32_t a) const {
  auto it = std::lower_bound(modifiers.begin(), modifiers.end(), a,
                             [](const std::pair<uint32_t, int>& e, uint32_t v) { return e.first < v; });
  if (it != modifiers.end() && it->first == a) return it->second;
  return defaultConsumption;
}

void WaterLayer::init(int W, int H, WaterParams p) {
  W_ = W; H_ = H; params_ = std::move(p);
  service_.assign(size_t(W) * H, 0);
  fac_.clear();
  stats = WaterStats();
}

int WaterLayer::addFacility(WaterKind k, int x, int z, int buildAbsMonth) {
  WaterFacility f;
  f.id = nextId_++;
  f.kind = k; f.x = x; f.z = z; f.buildAbsMonth = buildAbsMonth;
  int L = k == WaterKind::Tower ? params_.towerLifeYears : k == WaterKind::Pump ? params_.pumpLifeYears : params_.desaltLifeYears;
  f.lmax = 12 * L; f.ld = 6 * L;
  fac_.push_back(f);
  return f.id;
}

bool WaterLayer::removeFacility(int id) {
  for (size_t i = 0; i < fac_.size(); i++)
    if (fac_[i].id == id) { fac_.erase(fac_.begin() + i); return true; }
  return false;
}

static bool ringAny(int W, int H, int x0, int z0, int x1, int z1, const std::vector<uint8_t>* kind, int want) {
  if (!kind) return false;
  for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++) {
      if (x != x0 && x != x1 && z != z0 && z != z1) continue;
      if (x < 0 || z < 0 || x >= W || z >= H) continue;
      if ((*kind)[size_t(x) * H + z] & want) return true;
    }
  return false;
}

void WaterLayer::monthlyUpdate(const WaterEnv& env) {
  const int W = W_, H = H_;
  double k = 1.0;
  if (env.ordinances.active(ord::WaterConservation)) k -= params_.conservationFactor / 100.0;
  if (env.ordinances.active(ord::WaterMeters)) k -= params_.meterFactor / 100.0;

  stats.supply = stats.used = 0;
  stats.sale[0] = stats.sale[1] = stats.purchase[0] = stats.purchase[1] = 0;
  stats.ageingCount = 0;
  std::fill(service_.begin(), service_.end(), 0);
  const size_t nCons = env.consumers ? env.consumers->size() : 0;
  watered_.assign(nCons, 0);

  // ---- 3.3 facility outputs ----
  int64_t polSum = 0; int polN = 0;
  int gridMax = env.maxWaterPollutionGrid;
  int m = params_.usePollutionConstantMax ? params_.maxWaterPolluteForUI : gridMax;
  for (auto& f : fac_) {
    f.output = 0; f.status = WaterFacility::Off;
    int age = env.nowAbsMonth - f.buildAbsMonth;
    if (!f.functional || f.expired) continue;
    if (age >= f.lmax) { f.expired = true; continue; }
    if (env.power && !env.power->powered(f.x, f.z)) continue;
    int cap = f.kind == WaterKind::Tower ? params_.towerCapa : f.kind == WaterKind::Pump ? params_.pumpCapa : params_.desaltCapa;
    int64_t out = 0;
    if (f.kind == WaterKind::Tower) { out = cap; f.status = WaterFacility::Good; }
    else if (f.kind == WaterKind::Pump) {
      // Pumps need fresh water: a water tile whose cell has no sea-water flag (spec 16 s3.3) [STATIC].
      if (ringAny(W, H, f.x - 1, f.z - 1, f.x + 1, f.z + 1, env.waterKind, 1)) { out = cap; f.status = WaterFacility::Good; }
      else if (ringAny(W, H, f.x - 2, f.z - 2, f.x + 2, f.z + 2, env.waterKind, 1)) {
        out = int64_t(cap) * params_.pumpDistanceFactor / 100; f.status = WaterFacility::Far;
      } else f.status = WaterFacility::NoWater;
    } else {
      if (ringAny(W, H, f.x - 1, f.z - 1, f.x + 3, f.z + 3, env.waterKind, 2)) { out = cap; f.status = WaterFacility::Good; }
      else f.status = WaterFacility::NoWater;
    }
    int p = env.waterPollutionAt ? env.waterPollutionAt(f.x, f.z) : 0;
    polSum += p; polN++;
    if (m != 0) {
      if (p >= m) out = 0;
      else out = int64_t(m - p) * out / m;
    }
    if (age > f.ld && f.lmax > f.ld) {
      out -= int64_t(age - f.ld) * out / (f.lmax - f.ld);
      stats.ageingCount++;
    }
    f.output = int(out);
  }
  stats.meanPollution = polN ? uint32_t(polSum / polN) : 0;

  // ---- 3.4 distribution (spec 16 s3.4) ----
  // Needs-water marks [STATIC]: one per building on its lot CENTRE tile; water facilities get none. Copied once for the
  // month, so a building is charged at most once across components.
  std::vector<int> markAt(size_t(W) * H, -1);
  std::vector<uint8_t> marked(nCons, 0);
  int marks = 0;
  for (size_t i = 0; i < nCons; i++) {
    const auto& o = (*env.consumers)[i];
    if (o.agentType == 12810 || o.agentType == 12840 || o.agentType == 12830) continue;
    int cx = (o.x + o.x1()) / 2, cz = (o.z + o.z1()) / 2;
    if (cx < 0 || cz < 0 || cx >= W || cz >= H) continue;
    if (markAt[size_t(cx) * H + cz] < 0) markAt[size_t(cx) * H + cz] = int(i);
    marked[i] = 1; marks++;
  }
  auto inMask = [&](int x, int z) { return x >= 0 && z >= 0 && x < W && z < H && env.pipes && (*env.pipes)[size_t(x) * H + z]; };
  std::vector<uint8_t> reachedFac(fac_.size(), 0);
  const int r = params_.waterFromPipe;
  for (;;) {
    // 3. Component: first facility in list order with output != 0 not yet reached this month.
    size_t seedIdx = fac_.size();
    for (size_t fi = 0; fi < fac_.size(); fi++) if (fac_[fi].output > 0 && !reachedFac[fi]) { seedIdx = fi; break; }
    if (seedIdx == fac_.size()) break;
    // Flood of the pipe mask from the seed tile: each pass grows one tile left/right, then one tile up/down; at most 500
    // passes [STATIC]. Every tile expands horizontally and vertically exactly once.
    std::vector<uint8_t> filled(size_t(W) * H, 0);
    const int sx = fac_[seedIdx].x, sz = fac_[seedIdx].z;
    std::vector<int> needH{sx * H + sz}, needV{sx * H + sz};
    filled[size_t(sx) * H + sz] = 1;
    for (int pass = 0; pass < 500; pass++) {
      bool added = false;
      std::vector<int> nh, nv;
      for (int t : needH) {
        int x = t / H, z = t % H;
        for (int nx : {x - 1, x + 1}) {
          if (!inMask(nx, z) || filled[size_t(nx) * H + z]) continue;
          filled[size_t(nx) * H + z] = 1; added = true; nh.push_back(nx * H + z); needV.push_back(nx * H + z);
        }
      }
      for (int t : needV) {
        int x = t / H, z = t % H;
        for (int nz : {z - 1, z + 1}) {
          if (!inMask(x, nz) || filled[size_t(x) * H + nz]) continue;
          filled[size_t(x) * H + nz] = 1; added = true; nv.push_back(x * H + nz); nh.push_back(x * H + nz);
        }
      }
      needH.swap(nh);
      needV.swap(nv);
      if (!added) break;
    }
    // S starts at 0 and sums EVERY producing facility inside the flood, including ones counted earlier [STATIC].
    int64_t S = 0;
    std::vector<int> wetSeeds;
    for (size_t gi = 0; gi < fac_.size(); gi++) {
      const auto& g = fac_[gi];
      if (g.output > 0 && filled[size_t(g.x) * H + g.z]) { reachedFac[gi] = 1; S += g.output; wetSeeds.push_back(g.x * H + g.z); }
    }
    bool unlimited = false;
    int64_t sold = 0;
    for (const auto& d : env.deals) {
      if (d.tx < 0 || d.tz < 0 || d.tx >= W || d.tz >= H || !filled[size_t(d.tx) * H + d.tz]) continue;
      if (d.purchase) unlimited = true; else sold += d.amount;
    }
    // 5. Serviceable = the filled set dilated by WaterFromPipe (Chebyshev square).
    std::vector<uint8_t> serv(size_t(W) * H, 0);
    for (int x = 0; x < W; x++)
      for (int z = 0; z < H; z++) {
        if (!filled[size_t(x) * H + z]) continue;
        for (int xx = std::max(0, x - r); xx <= std::min(W - 1, x + r); xx++)
          for (int zz = std::max(0, z - r); zz <= std::min(H - 1, z + r); zz++) serv[size_t(xx) * H + zz] = 1;
      }
    // 6. Budget for this component alone.
    int64_t budget = unlimited ? INT64_MAX / 4 : (k > 0 ? int64_t(double(S) / k) : INT64_MAX / 4);
    if (!unlimited) budget = std::max<int64_t>(0, budget - sold);
    stats.sale[0] += sold;
    int64_t consumed = 0;
    bool stop = false;
    auto charge = [&](int t) {
      int oi = markAt[t];
      if (oi < 0 || !marked[size_t(oi)]) return;
      const auto& o = (*env.consumers)[size_t(oi)];
      int64_t c = int64_t(params_.consumptionPerTile(o.agentType)) * o.sizeX * o.sizeZ;
      if (c <= 0) { marked[size_t(oi)] = 0; watered_[size_t(oi)] = 1; return; }
      if (budget <= c) { budget = 0; stop = true; return; }
      budget -= c; consumed += c;
      marked[size_t(oi)] = 0; watered_[size_t(oi)] = 1;
      if (budget < 1) stop = true;
    };
    // Spreading front: the wet mask starts at the counted facilities' tiles, active rows = their rows; each iteration a
    // down sweep then an up sweep, each growing the wet mask one tile horizontally and vertically inside the serviceable
    // mask and the active rows, which then widen by one row each way [STATIC; growth inside a sweep INFERRED].
    std::vector<uint8_t> wet(size_t(W) * H, 0);
    int zlo = H, zhi = -1;
    std::vector<int> front;
    for (int t : wetSeeds) if (!wet[size_t(t)]) { wet[size_t(t)] = 1; front.push_back(t); zlo = std::min(zlo, t % H); zhi = std::max(zhi, t % H); }
    for (int t : front) { service_[size_t(t)] = 1; if (!stop) charge(t); }
    int idleSweeps = 0;
    for (int sweep = 0; !stop && !front.empty() && idleSweeps < 2; sweep++) {
      const bool down = (sweep % 2) == 0;
      std::vector<int> next, keep;
      for (int t : front) {
        int x = t / H, z = t % H;
        bool held = false;   // a neighbour waits for the active row range to widen
        const int nb[4][2] = {{x - 1, z}, {x + 1, z}, {x, z - 1}, {x, z + 1}};
        for (const auto& n : nb) {
          if (n[0] < 0 || n[1] < 0 || n[0] >= W || n[1] >= H) continue;
          size_t ni = size_t(n[0]) * H + n[1];
          if (wet[ni] || !serv[ni]) continue;
          if (n[1] < zlo || n[1] > zhi) { held = true; continue; }
          wet[ni] = 1; next.push_back(int(ni));
        }
        if (held) keep.push_back(t);
      }
      idleSweeps = next.empty() ? idleSweeps + 1 : 0;
      // Row order of the sweep; within a row x ascending. New tiles go wet a 32-tile row word at a time: when the budget
      // runs out the rest of that word stays wet but uncharged.
      std::sort(next.begin(), next.end(), [&](int a2, int b2) {
        int az = a2 % H, bz = b2 % H;
        if (az != bz) return down ? az < bz : az > bz;
        return a2 < b2;
      });
      int stopWordZ = -1, stopWordX = -1;
      for (int t : next) {
        int x = t / H, z = t % H;
        if (stop) {
          if (z == stopWordZ && x / 32 == stopWordX) service_[size_t(t)] = 1;
          continue;
        }
        service_[size_t(t)] = 1;
        charge(t);
        if (stop) { stopWordZ = z; stopWordX = x / 32; }
      }
      front.swap(next);
      front.insert(front.end(), keep.begin(), keep.end());
      zlo = std::max(0, zlo - 1); zhi = std::min(H - 1, zhi + 1);
    }
    // 7. Leftover and statistics; a purchase component draws its excess from the neighbour (step 4).
    int64_t L;
    if (unlimited) {
      if (consumed > S) { stats.purchase[0] += consumed - S; S = consumed; L = 0; }
      else L = S - consumed;
    } else {
      L = k > 0 ? int64_t(double(budget) * k) : 0;
    }
    stats.supply += S;
    stats.used += S - L;
  }
  // Counts (spec 16 s3.4 "Counts") [STATIC]: marks left = buildings without water; needWater = all marks, or 100 when
  // none is left and at least one building needs water.
  {
    int left = 0;
    for (size_t i = 0; i < nCons; i++) left += marked[i];
    stats.statA = uint32_t(left);
    if (marks > 0) stats.statB = left == 0 ? 100u : uint32_t(marks);
  }

  uint32_t pct = stats.supply > 0 ? uint32_t(std::min<int64_t>(100, 100 * stats.used / stats.supply)) : 0;
  chart.pushMonthly(pct);
  chart.max = 100;
}

// Bit index of tile (x, z) in the saved water-service bitmap: z-major, bit = z * W + x [DATA: with this order Madison's
// saved bitmap leaves 5 of 5,615 occupied R/C/I lots dry, which equals our own distribution after one month; the
// earlier (W-1-x) * H + z order left 1,828 dry].
size_t WaterLayer::waterBitIndex(int x, int z) const { return size_t(z) * size_t(W_) + size_t(x); }

Bytes WaterLayer::serialize() const {
  const size_t total = (size_t(W_) * H_ + 31) / 32;
  std::vector<uint32_t> words(total, 0);
  for (int x = 0; x < W_; x++)
    for (int z = 0; z < H_; z++)
      if (service_[size_t(x) * H_ + z]) {
        size_t idx = waterBitIndex(x, z);
        words[idx / 32] |= 1u << (idx % 32);
      }
  size_t first = 0, last = total;
  while (first < total && words[first] == 0) first++;
  while (last > first && words[last - 1] == 0) last--;
  uint32_t n = uint32_t(last - first);
  Bytes b;
  writeRecHeader(b, 4);
  put32(b, n ? uint32_t(first) : 0);
  put32(b, n);
  for (size_t i = first; i < last; i++) put32(b, words[i]);
  chart.write(b);
  put64(b, stats.supply); put64(b, stats.used);
  put64(b, stats.sale[0]); put64(b, stats.sale[1]);
  put64(b, stats.purchase[0]); put64(b, stats.purchase[1]);
  put32(b, stats.statA); put32(b, stats.statB);
  put32(b, stats.meanPollution); put32(b, stats.ageingCount);
  return b;
}

void WaterLayer::parse(const uint8_t* p, size_t len) {
  Reader r(p, len);
  RecHeader h = readRecHeader(r);
  if (h.version > 4) throw FormatError("water layer version");
  uint32_t first = r.u32(), n = r.u32();
  const size_t total = (size_t(W_) * H_ + 31) / 32;
  if (size_t(first) + n > total) throw FormatError("water bitmap range");
  std::vector<uint32_t> words(total, 0);
  for (uint32_t i = 0; i < n; i++) words[first + i] = r.u32();
  service_.assign(size_t(W_) * H_, 0);
  for (int x = 0; x < W_; x++)
    for (int z = 0; z < H_; z++) {
      size_t idx = waterBitIndex(x, z);
      if (words[idx / 32] >> (idx % 32) & 1u) service_[size_t(x) * H_ + z] = 1;
    }
  chart.parse(r);
  stats.supply = rd64(r); stats.used = rd64(r);
  stats.sale[0] = rd64(r); stats.sale[1] = rd64(r);
  stats.purchase[0] = rd64(r); stats.purchase[1] = rd64(r);
  stats.statA = r.u32(); stats.statB = r.u32();
  stats.meanPollution = r.u32(); stats.ageingCount = r.u32();
  if (r.remaining() != 0) throw FormatError("water layer trailing bytes");
}

// ======================= Garbage =======================
int64_t GarbageDisposal::dispose(int64_t amount, bool landfill) {
  int64_t rem = amount;
  if (rem <= 0) return 0;
  auto take = [&](int64_t& budget, int slot) {
    int64_t t = std::min(rem, std::max<int64_t>(0, budget));
    budget -= t; rem -= t; month[slot] += t;
  };
  take(wteBudget, Wte);
  take(incineratorBudget, Incinerated);
  if (rem > 0 && landfill && store.V > 0) {   // routed to the store; placed on tiles (and counted) by the landfill developer
    int64_t t = std::min<int64_t>(rem, store.V);
    store.S += t; rem -= t;
  }
  if (rem > 0 && exportDeal) { month[Exported] += rem; rem = 0; }
  if (rem > 0) {
    int64_t room = abductionCap - abducted;
    if (room > 0) { int64_t t = std::min(rem, room); abducted += t; rem -= t; }
  }
  return rem;
}

void GarbageDisposal::monthEnd() {
  for (int m = 11; m > 0; m--) for (int i = 0; i < 7; i++) history[m][i] = history[m - 1][i];
  for (int i = 0; i < 7; i++) history[0][i] = month[i];
  history[0][Produced] = month[Produced] - abducted;
  for (int i = 0; i < 7; i++) { totals[i] = 0; for (int m = 0; m < 12; m++) totals[i] += history[m][i]; }
  for (auto& v : month) v = 0;
  abducted = 0;
}

void LandfillStore::monthly() {
  Slast = int32_t(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, S)));
  Dlast = int32_t(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, D)));
  V = int32_t(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, D - S)));
}
void LandfillStore::parse(const uint8_t* p) {
  auto s32 = [&](int o) { int32_t v; std::memcpy(&v, p + o, 4); return v; };
  Slast = s32(0); Dlast = s32(4); S = s32(8); D = s32(12);
  std::memcpy(&f1, p + 16, 4); std::memcpy(&f2, p + 20, 4);
  capPct = p[24]; V = s32(25); Sbase = s32(29); Dbase = s32(33);
}
void LandfillStore::write(uint8_t* p) const {
  auto put = [&](int o, int64_t v) { int32_t x = int32_t(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, v))); std::memcpy(p + o, &x, 4); };
  put(0, Slast); put(4, Dlast); put(8, S); put(12, D);
  std::memcpy(p + 16, &f1, 4); std::memcpy(p + 20, &f2, 4);
  p[24] = capPct; put(25, V); put(29, Sbase); put(33, Dbase);
}

}}  // namespace m3k::us
