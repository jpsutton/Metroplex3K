#include "core/economy.h"

#include <algorithm>
#include <cmath>

#include "core/tune.h"

namespace m3k {

namespace {

int64_t rdS64(Reader& r) {
  uint32_t lo = r.u32();
  uint32_t hi = r.u32();
  return int64_t((uint64_t(hi) << 32) | lo);
}
void putS64(Bytes& b, int64_t v) {
  put32(b, uint32_t(uint64_t(v)));
  put32(b, uint32_t(uint64_t(v) >> 32));
}
std::string rdString(Reader& r) {
  uint32_t n = r.u32();
  if (n > r.remaining()) throw FormatError("string overruns record");
  return r.str(n);
}
void putString(Bytes& b, const std::string& s) {
  put32(b, uint32_t(s.size()));
  b.insert(b.end(), s.begin(), s.end());
}
std::vector<std::string> splitChar(const std::string& s, char c) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    size_t p = s.find(c, start);
    out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
    if (p == std::string::npos) break;
    start = p + 1;
  }
  return out;
}

}  // namespace

// ================================================================ AgentHierarchy
int AgentHierarchy::distance(uint32_t t, uint32_t a) const {
  uint32_t cur = t;
  for (int d = 0; d < 64; d++) {
    if (cur == a) return d;
    auto it = parent_.find(cur);
    if (it == parent_.end()) return -1;
    cur = it->second;
  }
  return -1;
}

// ================================================================ BudgetConfig
BudgetConfig::BudgetConfig() { taxEffects.push_back(TaxEffectEntry{0.3f, -0.7f, 7, 0}); }

BudgetConfig BudgetConfig::fromIni(const IniFile& ini, const IniFile* advisor) {
  BudgetConfig c;
  using namespace tune;
  c.maxBonds = int(getInt(ini, "Bonds", "MaxBonds", c.maxBonds));
  c.monthlyPaymentPer1K = int(getInt(ini, "Bonds", "MonthlyPaymentPer1K", c.monthlyPaymentPer1K));
  c.maxBondAmt = int(getInt(ini, "Bonds", "MaxBondAmt", c.maxBondAmt));
  c.bondLifespan = int(getInt(ini, "Bonds", "BondLifespan", c.bondLifespan));
  static const char* kLv[3] = {"LVToResTaxTransmogrifier", "LVToComTaxTransmogrifier", "LVToIndTaxTransmogrifier"};
  for (int k = 0; k < 3; k++) {
    std::string s;
    int64_t v;
    if (ini.get("TuningParameters", kLv[k], s) && parseInt(s, v)) c.lvToTax[k] = float(v) * 0.0001f;
  }
  c.minAverageLV = int(std::clamp<int64_t>(getInt(ini, "TuningParameters", "MinAverageLV", c.minAverageLV), 0, 255));

  // Only the [TuningParameters] copy of TaxationDemandEffect is read ([Admin] is dead data).
  std::string s;
  if (ini.get("TuningParameters", "TaxationDemandEffect", s)) {
    std::vector<TaxEffectEntry> es;
    for (auto& ent : splitChar(s, ';')) {
      auto f = splitChar(ent, ',');
      if (f.size() < 4) continue;
      int64_t a, b, n, p;
      if (!parseInt(f[0], a) || !parseInt(f[1], b) || !parseInt(f[2], n) || !parseInt(f[3], p)) continue;
      TaxEffectEntry e;
      e.a = float(a) * 0.0001f;
      e.b = float(b) * 0.0001f;
      e.n = int(std::clamp<int64_t>(n, 0, kMaxTaxRate));
      e.pop = uint32_t(p);
      es.push_back(e);
    }
    if (!es.empty()) {
      std::stable_sort(es.begin(), es.end(), [](const TaxEffectEntry& x, const TaxEffectEntry& y) { return x.pop < y.pop; });
      c.taxEffects = es;
    }
  }
  if (ini.get("TuningParameters", "TireRecyclingOrdEffect", s)) {
    size_t colon = s.find(':');
    if (colon != std::string::npos) {
      int64_t pct;
      if (parseInt(s.substr(0, colon), pct)) {
        c.tireRecyclingPct = pct > 100 ? 0 : int(std::max<int64_t>(pct, 0));   // pct must be <= 100
        for (auto& t : splitChar(s.substr(colon + 1), ',')) {
          std::string tt = trimSpaces(t);
          if (!tt.empty()) c.tireRecyclingTypes.insert(parseIdNumber(tt));
        }
      }
    }
  }
  static const char* kSpecial[13] = {"LowDensityZone", "MedDensityZone", "HighDensityZone", "LandfillZone",
                                     "AirportZone", "SeaportZone", "Dezone", "Demolish", "LevelTerrain",
                                     "RaiseTerrain", "LowerTerrain", "DecorativeWater", "DemolishRubbleTile"};
  for (int i = 0; i < 13; i++) c.specialCost[i] = getInt(ini, "SpecialCosts", kSpecial[i], 0);
  for (auto& kv : ini.section("BuildingPlacementCosts")) {
    int64_t v;
    std::string val = trimSpaces(kv.second);
    // first integer after '=': take the leading token, the rest is a comment
    size_t sp = val.find_first_of(" \t");
    if (sp != std::string::npos) val = val.substr(0, sp);
    if (parseInt(val, v)) c.placementCost[parseIdNumber(kv.first)] = v;
  }
  if (advisor) {
    c.loseGameLevel = getInt(*advisor, "TREASURY_NUMBERS", "LOSE_GAME_LEVEL", c.loseGameLevel);
    c.treasuryLow = getInt(*advisor, "TREASURY_NUMBERS", "TREASURY_LOW", c.treasuryLow);
    c.taxGrowthThreshold = int(getInt(*advisor, "TAX_RATE_CUTOFFS", "TaxGrowthThreshold", c.taxGrowthThreshold));
    c.taxDeclineThreshold = int(getInt(*advisor, "TAX_RATE_CUTOFFS", "TaxDeclineThreshold", c.taxDeclineThreshold));
  }
  return c;
}

int64_t BudgetConfig::placementCostFor(uint32_t t, const AgentHierarchy& h, bool tireOrd) const {
  int best = -1;
  int64_t cost = 0;
  for (auto& kv : placementCost) {   // ascending key order: ties keep the first
    int d = h.distance(t, kv.first);
    if (d >= 0 && (best < 0 || d < best)) { best = d; cost = kv.second; }
  }
  if (best < 0) return 0;
  if (tireOrd && tireRecyclingTypes.count(t)) cost -= cost * tireRecyclingPct / 100;
  return cost;
}

// ================================================================ TaxDemandTable
std::array<float, 23> TaxDemandTable::entryTable(const TaxEffectEntry& e) {
  std::array<float, 23> t{};
  int n = std::clamp(e.n, 0, kMaxTaxRate);
  for (int r = 0; r <= kMaxTaxRate; r++) {
    if (r < n) t[r] = float(n - r) * e.a / float(n);
    else if (r == n) t[r] = 0.0f;
    else t[r] = float(r - n) * e.b / float(kMaxTaxRate - n);
  }
  return t;
}

void TaxDemandTable::setEntries(const std::vector<TaxEffectEntry>& e) {
  entries_ = e;
  std::stable_sort(entries_.begin(), entries_.end(),
                   [](const TaxEffectEntry& x, const TaxEffectEntry& y) { return x.pop < y.pop; });
}

void TaxDemandTable::rebuild(uint32_t pop) {
  int lo = -1;
  for (size_t i = 0; i < entries_.size(); i++)
    if (entries_[i].pop <= pop) lo = int(i);
  if (lo < 0) return;   // keep previous table
  auto tlo = entryTable(entries_[lo]);
  if (entries_[lo].pop == pop || lo + 1 >= int(entries_.size())) { e_ = tlo; return; }
  auto thi = entryTable(entries_[lo + 1]);
  float span = float(entries_[lo + 1].pop - entries_[lo].pop);
  for (int r = 0; r <= kMaxTaxRate; r++)
    e_[r] = tlo[r] + (thi[r] - tlo[r]) * float(pop) / span;   // original defect: pop, not pop - P_lo
}

int64_t taxRevenue(int rate, int32_t N, int avgLV, int minLV, float T) {
  int32_t a = int32_t(int64_t(rate) * N / 100);
  int lp = avgLV;
  if (N > 0 && avgLV < minLV) lp = minLV;
  uint32_t prod = uint32_t(a) * uint32_t(lp);
  return int64_t(float(prod) * T);
}

// ================================================================ Budget
Budget::Budget(BudgetConfig c) : cfg(std::move(c)) {
  for (int k = 0; k < kTaxGroups; k++) tax[k].zoneType = kZoneTypes[k];
  for (uint32_t id : dept::kOrder) {
    Department d;
    d.id = id;
    depts.push_back(d);
  }
  treasuryChart = Chart::make(100, 200, true);   // new city: 32 samples of 100, scale 200 [OBSERVED, spec 30 s6]
  taxDemand.setEntries(cfg.taxEffects);
}

int64_t Budget::startingFunds(int difficulty) {
  switch (difficulty) {
    case 1: return 50000;
    case 2: return 20000;
    case 3: return 10000;
  }
  return 0;
}

void Budget::newCity(int64_t startFunds, int rR, int rC, int rI) {
  funds = startFunds < 0 ? 0 : startFunds;
  int r[3] = {rR, rC, rI};
  for (int k = 0; k < 3; k++) tax[k].rate = uint8_t(std::clamp(r[k], 0, kMaxTaxRate));
  if (onFundsChanged) onFundsChanged();
}

bool Budget::spend(int64_t amount) {
  if (funds - amount < 0) return false;
  funds -= amount;
  if (onFundsChanged) onFundsChanged();
  return true;
}

void Budget::add(int64_t amount) {
  funds += amount;
  if (onFundsChanged) onFundsChanged();
}

void Budget::setTaxRate(int g, int rate) {
  if (g < 0 || g >= kTaxGroups) return;
  tax[g].rate = uint8_t(std::clamp(rate, 0, kMaxTaxRate));
}

Department* Budget::dept(uint32_t id) {
  for (auto& d : depts) if (d.id == id) return &d;
  return nullptr;
}
const Department* Budget::dept(uint32_t id) const {
  for (auto& d : depts) if (d.id == id) return &d;
  return nullptr;
}
void Budget::setFunding(uint32_t id, int pct) {
  if (auto* d = dept(id)) d->funding = uint8_t(std::clamp(pct, 0, 255));
}
void Budget::setDeptFullCost(uint32_t id, int64_t c) {
  if (auto* d = dept(id)) d->fullCost = c;
}

int64_t Budget::currentTax(int g, const MonthEndInput& in) const {
  return taxRevenue(tax[g].rate, in.zoneTotals[g], in.avgLV, cfg.minAverageLV, cfg.lvToTax[g]);
}

bool Budget::issueBond(int64_t x, int month, int year) {
  if (x < 0) return false;
  int64_t principal = x / 5000 * 5000;
  int64_t maxSingle = int64_t(cfg.maxBondAmt) * 1000;
  if (int(bonds.size()) >= cfg.maxBonds || principal == 0 || principal > maxSingle ||
      totalPrincipal + principal > maxSingle * cfg.maxBonds)
    return false;
  Bond b;
  b.month = uint8_t(month);
  b.maturityYear = uint32_t(year + cfg.bondLifespan);
  b.principal = uint32_t(principal);
  b.monthlyPayment = uint32_t(int64_t(cfg.monthlyPaymentPer1K) * principal / 1000);
  b.totalRepayable = b.monthlyPayment * uint32_t(cfg.bondLifespan) * 12u;
  b.remaining = b.totalRepayable;
  b.paid = 0;
  totalPrincipal += principal;
  bonds.push_back(b);
  add(principal);
  return true;
}

uint32_t Budget::chartSample(int64_t f) {
  return uint32_t((std::max<int64_t>(f, -100000) + 100000) / 1000);
}

MonthEndResult Budget::monthEnd(const MonthEndInput& in) {
  MonthEndResult res;
  const int m = in.month == 1 ? 11 : in.month - 2;
  const int budgetYear = in.month == 1 ? in.year - 1 : in.year;
  res.slot = m;
  res.budgetYear = budgetYear;

  // 1. tax->demand table
  taxDemand.setEntries(cfg.taxEffects);
  taxDemand.rebuild(in.population);
  // 2. taxes
  for (int k = 0; k < kTaxGroups; k++) {
    int64_t t = currentTax(k, in);
    res.taxIncome[k] = t;
    tax[k].hist[m] = t;
    tax[k].ytd += t;
  }
  // 3. ordinances
  ordIncomeHist[m] = in.ordIncome; ordIncomeYTD += in.ordIncome;
  ordCostHist[m] = in.ordCost;     ordCostYTD += in.ordCost;
  // 4. neighbour deals (+ penalty accumulator, read and cleared)
  dealIncomeHist[m] = in.dealIncome; dealIncomeYTD += in.dealIncome;
  int64_t dc = in.dealCost + dealPenaltyAccumulator;
  dealPenaltyAccumulator = 0;
  dealCostHist[m] = dc; dealCostYTD += dc;
  // 5. business deals
  bizHist[m] = in.bizIncome; bizYTD += in.bizIncome;
  // 6. transit fares (truncated toward zero)
  int64_t fares = int64_t(in.fares);
  otherHist[m] = fares; otherYTD += fares;
  // 7. misc income; clear next slot
  miscYTD += miscHist[m];
  miscHist[(m + 1) % 12] = 0;
  // 8. departments
  for (auto& d : depts) {
    int64_t c = d.charge();
    d.lastCharge = c;
    d.hist[m] = c;
    d.ytd += c;
  }
  // 9. bonds
  int64_t paidThisMonth = 0;
  for (size_t i = 0; i < bonds.size();) {
    Bond& b = bonds[i];
    uint32_t pay = b.monthlyPayment;
    bondYTD += pay;
    paidThisMonth += pay;
    b.remaining = pay > b.remaining ? 0 : b.remaining - pay;
    b.paid += pay;
    // The stored month is 1-based and compared directly with the 0-based slot (original behaviour).
    bool retire = b.remaining == 0 || (int(b.month) == m && int(b.maturityYear) == budgetYear);
    if (retire) {
      totalPrincipal -= b.principal;
      bonds.erase(bonds.begin() + i);
    } else {
      i++;
    }
  }
  bondPayHist[m] = uint32_t(paidThisMonth);
  // 10. year-end
  if (m == 11) {
    res.yearEnd = true;
    funds += tax[0].ytd + tax[1].ytd + tax[2].ytd + ordIncomeYTD + dealIncomeYTD + bizYTD + otherYTD + miscYTD;
    for (auto& d : depts) {
      funds += d.yearEndCredit;
      funds -= d.ytd;
    }
    funds -= ordCostYTD + dealCostYTD + bondYTD;
    for (auto& t : tax) t.ytd = 0;
    for (auto& d : depts) d.ytd = 0;
    ordCostYTD = ordIncomeYTD = bondYTD = miscYTD = otherYTD = dealCostYTD = dealIncomeYTD = bizYTD = 0;
  }
  // 11. debt floor
  if (debtFloor && funds < -99999) funds = -99999;
  // 12. treasury chart
  treasuryChart.pushMonthly(chartSample(funds));
  if (m == 11) {
    treasuryChart.pushYearly(chartSample(funds));
    if ((budgetYear + 1) % 10 == 0) treasuryChart.pushDecade(chartSample(funds));
  }
  // 13. year-end pop-up
  if (m == 11 && funds > -100000 && (!showBudgetOnlyInDebt || funds < 0)) res.openBudgetWindow = true;
  // 14. funds changed
  if (onFundsChanged) onFundsChanged();
  return res;
}

// ---- budget record
void Budget::parse(const uint8_t* p, size_t n) {
  Reader r(p, n);
  int64_t f = rdS64(r);
  uint32_t ntax = r.u32();
  std::vector<TaxGroup> tg;
  for (uint32_t i = 0; i < ntax; i++) {
    TaxGroup g;
    g.zoneType = r.u32();
    g.rate = r.u8();
    for (auto& h : g.hist) h = rdS64(r);
    g.ytd = rdS64(r);
    tg.push_back(g);
  }
  uint32_t ndept = r.u32();
  std::vector<Department> loaded;
  for (uint32_t i = 0; i < ndept; i++) {
    Department d;
    d.id = r.u32();
    d.funding = r.u8();
    d.lastCharge = rdS64(r);
    for (auto& h : d.hist) h = rdS64(r);
    d.ytd = rdS64(r);
    loaded.push_back(d);
  }
  int64_t ocy = rdS64(r), oiy = rdS64(r);
  int64_t och[12], oih[12];
  for (int i = 0; i < 12; i++) { och[i] = rdS64(r); oih[i] = rdS64(r); }
  uint32_t nb = r.u32();
  std::vector<Bond> bl;
  for (uint32_t i = 0; i < nb; i++) {
    Bond b;
    b.month = r.u8();
    b.maturityYear = r.u32();
    b.principal = r.u32();
    b.totalRepayable = r.u32();
    b.remaining = r.u32();
    b.paid = r.u32();
    b.monthlyPayment = r.u32();
    if (int(bl.size()) < cfg.maxBonds) bl.push_back(b);   // records beyond MaxBonds are read and discarded
  }
  int64_t byt = rdS64(r);
  uint32_t bph[12];
  for (auto& v : bph) v = r.u32();
  int64_t tp = rdS64(r);
  int64_t my = rdS64(r);
  int64_t mh[12];
  for (auto& v : mh) v = rdS64(r);
  Chart ch = Chart::parse(r.ptr(), r.remaining());
  r.skip(Chart::kBytes);
  ch.autoScale = true;
  int64_t oty = rdS64(r);
  int64_t oth[12];
  for (auto& v : oth) v = rdS64(r);
  int64_t dcy = rdS64(r), diy = rdS64(r), by = rdS64(r);
  int64_t dch[12], dih[12], bh[12];
  for (int i = 0; i < 12; i++) { dch[i] = rdS64(r); dih[i] = rdS64(r); bh[i] = rdS64(r); }
  int64_t pen = rdS64(r);

  // Commit.
  funds = f;
  for (size_t i = 0; i < tg.size() && i < size_t(kTaxGroups); i++) tax[i] = tg[i];
  for (auto& d : loaded) {
    if (Department* dst = dept(d.id)) {   // unknown department IDs are skipped
      dst->funding = d.funding; dst->lastCharge = d.lastCharge; dst->ytd = d.ytd;
      std::copy(d.hist, d.hist + 12, dst->hist);
    }
  }
  ordCostYTD = ocy; ordIncomeYTD = oiy;
  std::copy(och, och + 12, ordCostHist); std::copy(oih, oih + 12, ordIncomeHist);
  bonds = bl;
  bondYTD = byt;
  std::copy(bph, bph + 12, bondPayHist);
  totalPrincipal = tp; miscYTD = my;
  std::copy(mh, mh + 12, miscHist);
  treasuryChart = ch;
  otherYTD = oty;
  std::copy(oth, oth + 12, otherHist);
  dealCostYTD = dcy; dealIncomeYTD = diy; bizYTD = by;
  std::copy(dch, dch + 12, dealCostHist); std::copy(dih, dih + 12, dealIncomeHist); std::copy(bh, bh + 12, bizHist);
  dealPenaltyAccumulator = pen;
  // Session flags are re-derived, never loaded.
  freePlacement = false;
  debtFloor = false;
}

Bytes Budget::serialize() const {
  Bytes b;
  putS64(b, funds);
  put32(b, kTaxGroups);
  for (auto& g : tax) {
    put32(b, g.zoneType);
    b.push_back(g.rate);
    for (auto h : g.hist) putS64(b, h);
    putS64(b, g.ytd);
  }
  put32(b, uint32_t(depts.size()));
  for (auto& d : depts) {
    put32(b, d.id);
    b.push_back(d.funding);
    putS64(b, d.lastCharge);
    for (auto h : d.hist) putS64(b, h);
    putS64(b, d.ytd);
  }
  putS64(b, ordCostYTD);
  putS64(b, ordIncomeYTD);
  for (int i = 0; i < 12; i++) { putS64(b, ordCostHist[i]); putS64(b, ordIncomeHist[i]); }
  put32(b, uint32_t(bonds.size()));
  for (auto& bd : bonds) {
    b.push_back(bd.month);
    put32(b, bd.maturityYear);
    put32(b, bd.principal);
    put32(b, bd.totalRepayable);
    put32(b, bd.remaining);
    put32(b, bd.paid);
    put32(b, bd.monthlyPayment);
  }
  putS64(b, bondYTD);
  for (auto v : bondPayHist) put32(b, v);
  putS64(b, totalPrincipal);
  putS64(b, miscYTD);
  for (auto v : miscHist) putS64(b, v);
  treasuryChart.serialize(b);
  putS64(b, otherYTD);
  for (auto v : otherHist) putS64(b, v);
  putS64(b, dealCostYTD);
  putS64(b, dealIncomeYTD);
  putS64(b, bizYTD);
  for (int i = 0; i < 12; i++) { putS64(b, dealCostHist[i]); putS64(b, dealIncomeHist[i]); putS64(b, bizHist[i]); }
  putS64(b, dealPenaltyAccumulator);
  return b;
}

// ================================================================ Ordinances
namespace {
struct OrdRow { uint32_t id, cat; int year; uint32_t str; int d; OrdMoney money; OrdFacility fac; };
using M = OrdMoney;
using F = OrdFacility;
const OrdRow kOrdRows[] = {
    {0xC2BF1DC5, 0x828D0A2F, 9999, 120, 8000, M::CostPlusFacility, F::PoliceStations},
    {0xA0D07129, 0x628D0C45, 0, 121, 1050, M::Income, F::None},
    {0xE0D07233, 0xC28D0F40, 0, 122, 8000, M::CostPlusFacility, F::Libraries},
    {0x815B4CEF, 0xC28D0F40, 0, 123, 14000, M::Cost, F::None},
    {0x015B4D1E, 0x028D0FC5, 0, 124, 1550, M::Income, F::None},
    {0x20D07175, 0xC28D0B6E, 9999, 125, 7250, M::Cost, F::None},
    {0xC2BF1DEF, 0x828D0A2F, 0, 128, 2007, M::Cost, F::None},
    {0xA2BF1DE5, 0x028D0FC5, 9999, 129, 1259, M::Cost, F::None},
    {0x02BF1DFA, 0x022E288E, 1960, 130, 4125, M::Cost, F::None},
    {0xC2BF1DD1, 0xC28D0B6E, 9999, 131, 900, M::WorldCost, F::None},
    {0x82E42E30, 0x022E288E, 1930, 132, 8593, M::Cost, F::None},
    {0xA2BF1E43, 0x828D04EB, 1950, 133, 4587, M::Cost, F::None},
    {0xE2BF1E2A, 0x028D0FC5, 1950, 134, 10443, M::Cost, F::None},
    {0x40D07236, 0xC28D0F40, 1960, 135, 10006, M::Cost, F::None},
    {0x22BF1E35, 0x828D04EB, 1960, 136, 9999, M::Free, F::None},
    {0x00D0723D, 0xC28D0F40, 1970, 137, 33057, M::Cost, F::None},
    {0x62BF1DAA, 0x828D0A2F, 1970, 138, 959, M::WorldCost, F::None},
    {0x62BF1DB9, 0x828D0A2F, 1970, 139, 18613, M::Cost, F::None},
    {0xA2BF1DDC, 0x028D0FC5, 1980, 141, 1531, M::Cost, F::None},
    {0x42BF1E18, 0x828D04EB, 1980, 142, 13768, M::CostPlusFacility, F::RecyclingCenters},
    {0xC2BF1E04, 0x828D04EB, 1980, 143, 12583, M::Cost, F::None},
    {0xE0D07239, 0xC28D0F40, 1990, 144, 1645, M::Cost, F::None},
    {0xE0D0722E, 0xC28D0F40, 1990, 145, 39187, M::Cost, F::None},
    {0xC2D1B025, 0x028D0FC5, 9999, 146, 1000, M::Free, F::None},
    {0x82B9999B, 0x022E288E, 1970, 147, 2364, M::WorldCost, F::None},
    {0x62F6E7CF, 0xC28D0B6E, 9999, 148, 3521, M::Cost, F::None},
    {0xA2F6E7DA, 0xC28D0B6E, 1915, 149, 3011, M::Cost, F::None},
    {0x02F6E7E2, 0xC28D0B6E, 1950, 150, 42659, M::Income, F::None},
    {0x22F6E80C, 0x828D04EB, 1990, 151, 28703, M::Cost, F::None},
    {0xA2F917CF, 0x828D04EB, 1950, 152, 20828, M::Income, F::None},
    {0xE2F6E7EA, 0xC28D0B6E, 1930, 153, 2546, M::Cost, F::None},
    {0x02F6E7EF, 0xC28D0B6E, 1950, 154, 3345, M::Cost, F::None},
    {0x22F6E7F4, 0xC28D0B6E, 9999, 155, 3593, M::Cost, F::None},
    {0xA2F6E7F9, 0xC28D0B6E, 1970, 156, 4256, M::Cost, F::None},
    {0xE2F6E7FE, 0xC28D0B6E, 1980, 157, 3542, M::Cost, F::None},
    {0xC2F6E804, 0xC28D0B6E, 1980, 158, 3452, M::Cost, F::None},
    {0x62F6E808, 0x828D04EB, 1970, 159, 17612, M::Cost, F::None},
    {0x22F6E814, 0x828D04EB, 1970, 160, 45349, M::Cost, F::None},
    {0x22F6E81B, 0x828D04EB, 1960, 161, 53517, M::Cost, F::None},
    {0xC2F6E81F, 0x828D04EB, 1940, 162, 39315, M::Cost, F::None},
    {0x62F6E82B, 0xC28D0B6E, 0, 163, 9999, M::Free, F::None},
    {0xC2F90EB1, 0x022E288E, 1930, 164, 16521, M::Cost, F::None},
};
// Hard-coded money kinds that exist in the original but not in the shipped INI (inert).
bool hardMoney(uint32_t id, OrdMoney& m, OrdFacility& f) {
  for (auto& r : kOrdRows) if (r.id == id) { m = r.money; f = r.fac; return true; }
  if (id == 0x62BF1E21 || id == 0x82BF1E0E) { m = M::FixedIncome1; f = F::None; return true; }
  if (id == 0xA2BF1F60) { m = M::CostPlusFacility; f = F::PoliceStations; return true; }
  return false;
}
}  // namespace

Ordinances::Ordinances() {
  for (auto& r : kOrdRows) {
    OrdinanceDef d;
    d.id = r.id; d.category = r.cat; d.year = r.year; d.nameStr = r.str; d.costDivisor = r.d;
    d.money = r.money; d.facility = r.fac;
    defs_[d.id] = d;
    state_[d.id] = State{};
  }
}

void Ordinances::loadCatalogue(const IniFile& ini) {
  using namespace tune;
  triggers.homelessShelterPercent = int(getInt(ini, "ORDINANCE_TRIGGERS", "HOMELESS_SHELTER_PERCENT", triggers.homelessShelterPercent));
  triggers.earthquakePrepBuildings = int(getInt(ini, "ORDINANCE_TRIGGERS", "EARTHQUAKE_PREP_BUILDINGS", triggers.earthquakePrepBuildings));
  triggers.touristPromotionBuildings = int(getInt(ini, "ORDINANCE_TRIGGERS", "TOURIST_PROMOTION_BUILDINGS", triggers.touristPromotionBuildings));
  triggers.aerospaceAvailableYear = int(getInt(ini, "ORDINANCE_TRIGGERS", "AEROSPACE_AVAILABLE_YEAR", triggers.aerospaceAvailableYear));
  if (!ini.has("ListOfOrdinances")) return;
  std::map<uint32_t, OrdinanceDef> nd;
  for (auto& kv : ini.section("ListOfOrdinances")) {
    uint32_t id = parseIdNumber(trimSpaces(kv.first));
    if (id == 0) continue;
    auto f = splitChar(kv.second, ';');
    if (f.size() < 4) continue;
    OrdinanceDef d;
    d.id = id;
    d.category = parseIdNumber(trimSpaces(f[0]));
    if (d.category == 0) continue;
    d.year = int(parseIdNumber(trimSpaces(f[1])));
    d.nameStr = parseIdNumber(trimSpaces(f[2]));
    d.costDivisor = int(parseIdNumber(trimSpaces(f[3])));
    if (d.costDivisor == 0) d.costDivisor = 100;
    OrdMoney m = M::Cost; OrdFacility fac = F::None;
    hardMoney(id, m, fac);
    d.money = m; d.facility = fac;
    nd[id] = d;
  }
  defs_ = nd;
  std::map<uint32_t, State> ns;
  for (auto& kv : defs_) {
    auto it = state_.find(kv.first);
    ns[kv.first] = it != state_.end() ? it->second : State{};
  }
  state_ = ns;
}

const OrdinanceDef* Ordinances::def(uint32_t id) const {
  auto it = defs_.find(id);
  return it == defs_.end() ? nullptr : &it->second;
}

void Ordinances::newCity() {
  for (auto& kv : defs_) {
    State s;
    s.available = kv.second.year == 0;
    state_[kv.first] = s;
  }
}
bool Ordinances::available(uint32_t id) const { auto it = state_.find(id); return it != state_.end() && it->second.available; }
bool Ordinances::enacted(uint32_t id) const { auto it = state_.find(id); return it != state_.end() && it->second.enacted; }
bool Ordinances::setAvailable(uint32_t id, bool v) {
  auto it = state_.find(id);
  if (it == state_.end()) return false;
  it->second.available = v;
  return true;
}
bool Ordinances::enact(uint32_t id) {
  auto it = state_.find(id);
  if (it == state_.end() || !it->second.available) return false;
  it->second.enacted = true;
  return true;
}
bool Ordinances::repeal(uint32_t id) {
  auto it = state_.find(id);
  if (it == state_.end()) return false;
  it->second.enacted = false;
  return true;
}
void Ordinances::makeAllAvailable(std::vector<uint32_t>* announced) {
  for (auto& kv : state_) {
    kv.second.available = true;
    if (announced) announced->push_back(kv.first);
  }
}

int64_t Ordinances::monthlyValue(uint32_t id, const OrdinanceMoneyInput& in) const {
  return enacted(id) ? ifEnactedValue(id, in) : 0;
}

// The monthly value the ordinance would have if it were enacted (the Ordinances window shows it for rows not enacted).
int64_t Ordinances::ifEnactedValue(uint32_t id, const OrdinanceMoneyInput& in) const {
  const OrdinanceDef* d = def(id);
  if (!d || !available(id)) return 0;
  int64_t D = d->costDivisor <= 0 ? 100 : d->costDivisor;
  int64_t P = in.population;
  int64_t W = in.world >= 0 ? in.world : P / 1000;   // [OPEN] spec 29 2.2 default
  switch (d->money) {
    case OrdMoney::Cost: return -(P / D + 1);
    case OrdMoney::Income: return P / D;
    case OrdMoney::CostPlusFacility: {
      int64_t n = 0;
      if (d->facility == OrdFacility::PoliceStations) n = in.policeStations;
      else if (d->facility == OrdFacility::Libraries) n = in.libraries;
      else if (d->facility == OrdFacility::RecyclingCenters) n = in.recyclingCenters;
      return -(P / D + 1 + n);
    }
    case OrdMoney::WorldCost: return -(W / D + 1);
    case OrdMoney::Free: return 0;
    case OrdMoney::FixedIncome1: return 1;
  }
  return 0;
}

Ordinances::Money Ordinances::monthlyMoney(const OrdinanceMoneyInput& in) const {
  Money m;
  for (auto& kv : defs_) {
    int64_t v = monthlyValue(kv.first, in);
    if (v > 0) m.income += v;
    else if (v < 0) m.cost += -v;
  }
  return m;
}

bool Ordinances::conditionHolds(const OrdinanceDef& d, const AvailabilityInput& in) const {
  if (d.year != 9999) return in.currentYear > d.year;
  int stations = in.busStations + in.subwayStations;
  switch (d.id) {
    case ord::HomelessShelter: return in.homelessPercent > triggers.homelessShelterPercent;
    case ord::Aerospace: return in.airports >= 1 && in.currentYear > triggers.aerospaceAvailableYear;
    case ord::TouristPromotion: return in.commercialBuildings > triggers.touristPromotionBuildings;
    case ord::Shuttle: return in.railStations + stations > 2;
    case ord::SubsidizedTransit: return stations > 2;
    case ord::YouthCurfew: return in.policeStations > 0;
    case ord::EarthquakeRetrofit: {
      int count = in.residentialBuildings + in.commercialBuildings + in.industrialBuildings + in.railStations +
                  in.subwayStations + in.libraries + in.policeStations + in.airports + in.busStations;
      return count > triggers.earthquakePrepBuildings;
    }
  }
  return false;
}

std::optional<uint32_t> Ordinances::availabilityScan(SimRng& rng, const AvailabilityInput& in) {
  if (rng.rand(100) >= 10) return std::nullopt;
  for (auto& kv : defs_) {   // ascending ID order
    if (available(kv.first)) continue;
    if (conditionHolds(kv.second, in)) {
      state_[kv.first].available = true;
      return kv.first;
    }
  }
  return std::nullopt;
}

void Ordinances::parse(const uint8_t* p, size_t n) {
  Reader r(p, n);
  uint32_t count = r.u32();
  for (uint32_t i = 0; i < count; i++) {
    uint32_t id = r.u32();
    uint8_t en = r.u8();
    uint8_t av = r.u8();
    auto it = state_.find(id);
    if (it == state_.end()) continue;   // not in the catalogue: ignored
    it->second.enacted = en != 0;
    it->second.available = av != 0;
  }
}

Bytes Ordinances::serialize() const {
  Bytes b;
  put32(b, uint32_t(state_.size()));
  for (auto& kv : state_) {
    put32(b, kv.first);
    b.push_back(kv.second.enacted ? 1 : 0);
    b.push_back(kv.second.available ? 1 : 0);
  }
  return b;
}

// ================================================================ NeighbourDeals
DealConfig DealConfig::fromIni(const IniFile& ini) {
  DealConfig c;
  using namespace tune;
  auto any = [&](const char* key, int64_t dflt) {
    for (const char* sec : {"DEAL_RATE_CONSTANTS", "BASE_DEAL_QUANTITIES", "MINIMUM_DEAL_COSTS", "DEAL_PENALTIES"}) {
      std::string s;
      int64_t v;
      if (ini.get(sec, key, s) && parseInt(s, v)) return v;
    }
    return dflt;
  };
  static const char* kRate[6] = {"POWER_BUY_RATE", "POWER_SELL_RATE", "WATER_BUY_RATE", "WATER_SELL_RATE",
                                 "GARBAGE_EXPORT_RATE", "GARBAGE_IMPORT_RATE"};
  for (int i = 0; i < 6; i++) c.baseRate[i] = uint32_t(any(kRate[i], c.baseRate[i]));
  c.rateVariable = uint32_t(any("DEAL_RATE_VARIABLE", c.rateVariable));
  c.penaltyMultiplier = uint32_t(any("DEAL_PENALTY_MULTIPLIER", c.penaltyMultiplier));
  c.basePower = uint32_t(any("BASE_POWER", c.basePower));
  c.baseWater = uint32_t(any("BASE_WATER", c.baseWater));
  c.baseGarbage = uint32_t(any("BASE_GARBAGE", c.baseGarbage));
  c.powerUnitsPer1K = uint32_t(any("PowerUnitsPer1K", c.powerUnitsPer1K));
  c.waterUnitsPer1K = uint32_t(any("WaterUnitsPer1K", c.waterUnitsPer1K));
  c.garbageUnitsPer1K = uint32_t(any("GarbageUnitsPer1K", c.garbageUnitsPer1K));
  c.percentPopForBuy = uint32_t(any("PercentPopForBuy", c.percentPopForBuy));
  c.percentPopForSell = uint32_t(any("PercentPopForSell", c.percentPopForSell));
  c.minPowerBuy = uint32_t(any("MIN_POWER_BUY", c.minPowerBuy));
  c.minWaterBuy = uint32_t(any("MIN_WATER_BUY", c.minWaterBuy));
  c.minGarbageExport = uint32_t(any("MIN_GARBAGE_EXPORT", c.minGarbageExport));
  return c;
}

int NeighbourDeals::edgeNeighbour(int x, int z, int w, int h) {
  if (x == 0 && z == 0) return 0;
  if (x == w - 1 && z == 0) return 1;
  if (x == w - 1 && z == h - 1) return 2;
  if (x == 0 && z == h - 1) return 3;
  if (x == 0) return 0;
  if (z == 0) return 1;
  if (x == w - 1) return 2;
  if (z == h - 1) return 3;
  return -1;
}

namespace {
int connIndex(ConnKind k) {   // saved order: rail, road, pipe, power, highway, subway
  switch (k) {
    case ConnRail: return 0;
    case ConnRoad: return 1;
    case ConnPipe: return 2;
    case ConnPower: return 3;
    case ConnHighway: return 4;
    case ConnSubway: return 5;
  }
  return 1;
}
uint32_t& countRef(Neighbour& n, ConnKind k) {
  switch (k) {
    case ConnRoad: return n.nRoad;
    case ConnRail: return n.nRail;
    case ConnPower: return n.nPower;
    case ConnPipe: return n.nPipe;
    case ConnHighway: return n.nHighway;
    case ConnSubway: return n.nSubway;
  }
  return n.nRoad;
}
}  // namespace

int NeighbourDeals::registerConnection(ConnKind kind, int x, int z, int w, int h) {
  int n = edgeNeighbour(x, z, w, h);
  if (n < 0) return -1;
  uint32_t packed = (uint32_t(x) << 16) | uint32_t(z);
  if (conn[connIndex(kind)].insert(packed).second) countRef(neighbours[n], kind)++;
  return n;
}

int NeighbourDeals::connectionCount(int n, ConnKind kind) const {
  Neighbour tmp = neighbours[n];
  return int(countRef(tmp, kind));
}

void NeighbourDeals::newCity(SimRng& rng, int w, int h, const std::function<bool(int, int)>& isLand,
                             const std::function<std::string(uint32_t)>& nameOf) {
  active.clear();
  pending.clear();
  for (auto& s : conn) s.clear();
  const int mx[4][2] = {{0, h / 2}, {w / 2, 0}, {w - 1, h / 2}, {w / 2, h - 1}};
  uint32_t usedCity[4], usedMayor[4];
  auto nm = [&](uint32_t inst, const char* pre) { return nameOf ? nameOf(inst) : std::string(pre) + std::to_string(inst); };
  for (int i = 0; i < kNeighbours; i++) {
    Neighbour n;
    n.population = 2000u + rng.rand(13000);
    n.populationCap = 4000000;
    n.exists = isLand ? isLand(mx[i][0], mx[i][1]) : true;
    uint32_t ci, mi;
    for (;;) {
      ci = 0x29 + rng.rand(36);
      bool dup = false;
      for (int j = 0; j < i; j++) dup |= usedCity[j] == ci;
      if (!dup) break;
    }
    for (;;) {
      mi = 2 + rng.rand(36);
      bool dup = false;
      for (int j = 0; j < i; j++) dup |= usedMayor[j] == mi;
      if (!dup) break;
    }
    usedCity[i] = ci;
    usedMayor[i] = mi;
    n.cityName = nm(ci, "City ");
    n.mayorName = nm(mi, "Mayor ");
    neighbours[i] = n;
  }
}

uint32_t NeighbourDeals::growPopulation(uint32_t pop, uint32_t cap, SimRng& rng) {
  for (int i = 0; i < 12; i++) pop = pop + rng.rand(3800) - 1200u;   // unsigned wrap is intended
  uint32_t q = rng.rand(1000);
  if (q < 15) pop /= 2;
  while (pop < 1000) pop += 500;
  if (pop > cap) {
    uint32_t k = (pop - cap + 9999u) / 10000u;   // same result as repeated -10000 while > cap
    pop -= k * 10000u;
  }
  return pop;
}

void NeighbourDeals::monthly(int month, SimRng& rng) {
  if (month == 1)
    for (auto& n : neighbours) n.population = growPopulation(n.population, n.populationCap, rng);
  for (auto& d : active)
    if (d.monthCounter >= 0) d.monthCounter++;
  for (auto it = active.begin(); it != active.end();) {
    if (it->terminated()) {
      if (it->neighbourIndex < kNeighbours && it->dealType < kDealTypes)
        neighbours[it->neighbourIndex].dealMade[it->dealType] = false;   // [INFERRED] frees the slot
      it = active.erase(it);
    } else {
      ++it;
    }
  }
}

int32_t NeighbourDeals::amountFor(const DealConfig& c, const Deal& d) {
  uint32_t base = c.base(d.dealType);
  if (base == 0) base = 1;
  uint32_t amount = uint32_t(uint64_t(uint32_t(d.quantity)) * uint32_t(d.rate) / base);
  if ((d.dealType & 1) == 0 && amount < c.minCost(d.dealType)) amount = c.minCost(d.dealType);
  return int32_t(amount);
}

NeighbourDeals::Money NeighbourDeals::monthlyMoney() {
  Money m;
  for (auto& d : active) {
    if (d.terminated() || d.dealType >= kDealTypes) continue;
    // [OBSERVED] shipped saves: units * rate / Base reproduces neither sell deals (Berlin 26,775 * 20 / 1000 = 535 vs saved
    // 1,071) nor buy deals (the saved rate field equals the amount), so a loaded deal keeps its saved amount.
    if (!(d.fromSave && d.monthlyAmount > 0)) d.monthlyAmount = amountFor(cfg, d);
    if (d.dealType & 1) m.income += d.monthlyAmount; else m.cost += d.monthlyAmount;
  }
  return m;
}

bool NeighbourDeals::dealOfType(int type) const {
  for (auto& d : active) if (d.dealType == type && !d.terminated()) return true;
  return false;
}

bool NeighbourDeals::connectionOk(int type, int n, const OfferContext& ctx) const {
  const Neighbour& nb = neighbours[n];
  switch (type) {
    case 0: return nb.exists && nb.nPower >= 1;
    case 1: return nb.exists && nb.nPower >= 1 && ctx.powered && ctx.powered(type, n);
    case 2: return nb.exists && nb.nPipe >= 1;
    case 3: return nb.exists && nb.nPipe >= 1 && ctx.watered && ctx.watered(type, n);
    case 4:
    case 5: return ctx.hasSeaport || (nb.exists && nb.nRoad + nb.nRail + nb.nHighway >= 1);
  }
  return false;
}

int NeighbourDeals::chooseNeighbour(int type, const OfferContext& ctx, SimRng& rng) const {
  if (dealOfType(type)) return -1;
  int c = type / 2;   // commodity
  int r = int(rng.rand(4));
  for (int i = 1; i <= 4; i++) {
    int n = (r + i) % 4;
    if (neighbours[n].dealMade[c * 2] || neighbours[n].dealMade[c * 2 + 1]) continue;
    if (connectionOk(type, n, ctx)) return n;
  }
  return -1;
}

std::optional<Deal> NeighbourDeals::makeOffer(int type, const OfferContext& ctx, SimRng& rng) {
  if (type < 0 || type >= kDealTypes || !newDealsEnabled) return std::nullopt;
  for (auto& p : pending) if (p.dealType == type) return std::nullopt;
  int n = chooseNeighbour(type, ctx, rng);
  if (n < 0) return std::nullopt;
  const bool buy = (type & 1) == 0;
  uint32_t rate = cfg.baseRate[type] + rng.rand(cfg.rateVariable);
  bool raised = false;
  if (buy) {
    for (auto& d : active)
      if (d.dealType == type + 1 && !d.terminated() && uint32_t(d.rate) > rate) { rate = uint32_t(d.rate); raised = true; }
    if (rate <= ctx.referenceRate) return std::nullopt;
  } else {
    if (rate < ctx.referenceRate) { rate = ctx.referenceRate; raised = true; }
  }
  uint64_t k;
  if (buy) k = uint64_t(ctx.cityPopulation) * cfg.percentPopForBuy / 100000;
  else k = uint64_t(neighbours[n].population) * cfg.percentPopForSell / 100000;
  if (k < 1) k = 1;
  uint32_t base = std::max<uint32_t>(cfg.base(type), 1);
  uint64_t units = uint64_t(cfg.unitsPer1K(type)) * k;
  units = (units + base - 1) / base * base;   // round up to a multiple of Base
  uint64_t amount = units * rate / base;
  uint64_t contract = amount;
  if (buy) {
    if (contract < cfg.minCost(type)) contract = cfg.minCost(type);
  } else {
    // [INFERRED] offer dropped when the city already has at least this much to sell
    if (units < ctx.availableToSell || (units == ctx.availableToSell && !raised)) return std::nullopt;
  }
  Deal d;
  d.neighbourName = neighbours[n].cityName;
  d.text = "";
  d.penalty = int32_t(uint64_t(cfg.penaltyMultiplier) * amount);
  d.monthlyAmount = int32_t(contract);
  d.neighbourIndex = uint8_t(n);
  d.dealType = uint8_t(type);
  d.monthCounter = 0;
  d.quantity = int32_t(units);
  d.rate = int32_t(rate);
  d.sizeOrTerm = uint32_t(k);
  d.year = uint32_t(ctx.currentYear);
  pending.push_back(d);
  return d;
}

bool NeighbourDeals::accept(size_t i) {
  if (i >= pending.size()) return false;
  Deal d = pending[i];
  pending.erase(pending.begin() + i);
  d.monthCounter = 0;
  if (d.neighbourIndex < kNeighbours && d.dealType < kDealTypes) neighbours[d.neighbourIndex].dealMade[d.dealType] = true;
  active.push_back(d);
  return true;
}

int64_t NeighbourDeals::cancel(int type) {
  for (auto it = active.begin(); it != active.end(); ++it) {
    if (it->dealType != type || it->terminated()) continue;
    if (it->monthCounter < 60) {
      it->monthCounter = -1;
      return it->penalty;
    }
    if (it->neighbourIndex < kNeighbours) neighbours[it->neighbourIndex].dealMade[it->dealType] = false;
    active.erase(it);
    return 0;
  }
  return 0;
}

void NeighbourDeals::reportQuantity(int type, int32_t q) {
  for (auto& d : active) {
    if (d.dealType != type || d.terminated()) continue;
    if ((type & 1) == 0) d.quantity = q;
    else if (d.quantity < q) d.monthCounter = -1;   // terminated without penalty
  }
}

static void parseDeal(Reader& r, Deal& d) {
  d.neighbourName = rdString(r);
  d.text = rdString(r);
  d.penalty = r.s32();
  d.monthlyAmount = r.s32();
  d.neighbourIndex = r.u8();
  d.dealType = r.u8();
  d.monthCounter = r.s32();
  d.quantity = r.s32();
  d.rate = r.s32();
  d.sizeOrTerm = r.u32();
  d.year = r.u32();
  d.fromSave = true;
}
static void putDeal(Bytes& b, const Deal& d) {
  putString(b, d.neighbourName);
  putString(b, d.text);
  put32(b, uint32_t(d.penalty));
  put32(b, uint32_t(d.monthlyAmount));
  b.push_back(d.neighbourIndex);
  b.push_back(d.dealType);
  put32(b, uint32_t(d.monthCounter));
  put32(b, uint32_t(d.quantity));
  put32(b, uint32_t(d.rate));
  put32(b, d.sizeOrTerm);
  put32(b, d.year);
}

void NeighbourDeals::parse(const uint8_t* p, size_t n) {
  Reader r(p, n);
  Neighbour nb[kNeighbours];
  for (auto& x : nb) {
    x.cityName = rdString(r);
    x.mayorName = rdString(r);
    x.population = r.u32();
    x.populationCap = r.u32();
    x.exists = r.u8() != 0;
    x.nRoad = r.u32(); x.nRail = r.u32(); x.nPower = r.u32();
    x.nPipe = r.u32(); x.nHighway = r.u32(); x.nSubway = r.u32();
    for (auto& f : x.dealMade) f = r.u8() != 0;
  }
  std::vector<Deal> lists[2];
  for (auto& l : lists) {
    uint32_t cnt = r.u32();
    for (uint32_t i = 0; i < cnt; i++) {
      Deal d;
      parseDeal(r, d);
      l.push_back(d);
    }
  }
  std::set<uint32_t> sets[6];
  for (auto& s : sets) {
    uint32_t cnt = r.u32();
    for (uint32_t i = 0; i < cnt; i++) s.insert(r.u32());
  }
  for (int i = 0; i < kNeighbours; i++) neighbours[i] = nb[i];
  active = lists[0];
  pending = lists[1];
  for (int i = 0; i < 6; i++) conn[i] = sets[i];
}

Bytes NeighbourDeals::serialize() const {
  Bytes b;
  for (auto& x : neighbours) {
    putString(b, x.cityName);
    putString(b, x.mayorName);
    put32(b, x.population);
    put32(b, x.populationCap);
    b.push_back(x.exists ? 1 : 0);
    put32(b, x.nRoad); put32(b, x.nRail); put32(b, x.nPower);
    put32(b, x.nPipe); put32(b, x.nHighway); put32(b, x.nSubway);
    for (bool f : x.dealMade) b.push_back(f ? 1 : 0);
  }
  for (auto* l : {&active, &pending}) {
    put32(b, uint32_t(l->size()));
    for (auto& d : *l) putDeal(b, d);
  }
  for (auto& s : conn) {
    put32(b, uint32_t(s.size()));
    for (uint32_t v : s) put32(b, v);   // std::set: ascending
  }
  return b;
}

// ================================================================ BusinessDeals
BusinessDeals::BusinessDeals() {
  defs = {{"Casino", 19003, 1915, 350, 149, true},
          {"Prison", 19004, 1900, 250, 210, true},
          {"Toxic_Factory", 19001, 1950, 400, 16, true},
          {"MegaMall", 19002, 1970, 300, 206, true}};
}

BusinessDeals BusinessDeals::fromIni(const IniFile& ini) {
  BusinessDeals b;
  b.monthsTillBusinessDeal = int(tune::getInt(ini, "BUSINESS_DEALS", "MONTHS_TILL_BUSINESS_DEAL", b.monthsTillBusinessDeal));
  b.treasuryLow = tune::getInt(ini, "TREASURY_NUMBERS", "TREASURY_LOW", b.treasuryLow);
  if (ini.has("BUSINESS_DEALS2")) {
    std::vector<BusinessDealDef> ds;
    for (auto& kv : ini.section("BUSINESS_DEALS2")) {
      auto f = splitChar(kv.second, ';');
      if (f.size() < 4) continue;
      BusinessDealDef d;
      d.name = trimSpaces(kv.first);
      d.buildingType = parseIdNumber(trimSpaces(f[0]));
      d.year = int(parseIdNumber(trimSpaces(f[1])));
      d.value = int(parseIdNumber(trimSpaces(f[2])));
      d.messageId = parseIdNumber(trimSpaces(f[3]));
      ds.push_back(d);
    }
    if (!ds.empty()) b.defs = ds;
  }
  return b;
}

std::optional<size_t> BusinessDeals::weeklyEvaluate(const Inputs& in, SimRng& rng) {
  const bool low = in.funds < treasuryLow;
  if (low) lowTreasuryCounter++;
  else lowTreasuryCounter = std::max(lowTreasuryCounter - 1, 0);
  if (pendingOffer || !low || in.population <= 1000 || lowTreasuryCounter < monthsTillBusinessDeal || defs.empty())
    return std::nullopt;
  size_t pick = rng.rand(uint32_t(defs.size()));
  const BusinessDealDef& d = defs[pick];
  if (d.year > in.currentYear || !d.enabled) return std::nullopt;
  if (in.cityHasBusiness && in.cityHasBusiness(d.buildingType)) return std::nullopt;
  if (d.buildingType == 19003 && in.casinoAllowed && !in.casinoAllowed()) return std::nullopt;
  pendingOffer = pick;
  return pick;
}

std::vector<size_t> BusinessDeals::eligibleDeals(const Inputs& in) const {
  std::vector<size_t> v;
  if (in.population <= 1000) return v;
  for (size_t i = 0; i < defs.size(); i++) {
    const BusinessDealDef& d = defs[i];
    if (d.year > in.currentYear || !d.enabled) continue;
    if (in.cityHasBusiness && in.cityHasBusiness(d.buildingType)) continue;
    if (std::find(inForce.begin(), inForce.end(), d.buildingType) != inForce.end()) continue;
    if (d.buildingType == 19003 && in.casinoAllowed && !in.casinoAllowed()) continue;
    v.push_back(i);
  }
  return v;
}

bool BusinessDeals::acceptByMessage(uint32_t messageId) {
  for (size_t i = 0; i < defs.size(); i++) {
    if (defs[i].messageId != messageId) continue;
    if (std::find(inForce.begin(), inForce.end(), defs[i].buildingType) != inForce.end()) return false;
    inForce.push_back(defs[i].buildingType);
    if (pendingOffer && *pendingOffer == i) pendingOffer.reset();
    return true;
  }
  return false;
}

bool BusinessDeals::accept() {
  if (!pendingOffer) return false;
  inForce.push_back(defs[*pendingOffer].buildingType);
  pendingOffer.reset();
  return true;
}

void BusinessDeals::remove(uint32_t t) {
  auto it = std::find(inForce.begin(), inForce.end(), t);
  if (it != inForce.end()) inForce.erase(it);
}

int64_t BusinessDeals::monthlyIncome() const {
  int64_t s = 0;
  for (uint32_t t : inForce)
    for (auto& d : defs)
      if (d.buildingType == t) { s += d.value; break; }
  return s;
}

}  // namespace m3k
