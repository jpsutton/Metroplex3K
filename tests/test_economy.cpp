#include <cmath>
#include <string>
#include "core/economy.h"
#include "testutil.h"

using namespace m3k;

static void testTaxRevenue() {
  // floor(7*10000/100)=700 * 100 * 0.0035 = 245
  CHECK(taxRevenue(7, 10000, 100, 35, 0.0035f) == 245);
  // L raised to MinAverageLV when N>0
  CHECK(taxRevenue(7, 10000, 10, 35, 0.0035f) == int64_t(float(700u * 35u) * 0.0035f));
  // N = 0: no floor applies, product 0
  CHECK(taxRevenue(7, 0, 10, 35, 0.0035f) == 0);
  CHECK(taxRevenue(0, 5000, 200, 35, 0.005f) == 0);
  // 32-bit product treated as unsigned: 700000 * 100000 wraps
  int64_t wrapped = taxRevenue(22, 2000000000, 255, 35, 1.0f);   // 440000000*255 wraps in u32
  uint32_t w32 = uint32_t(int32_t(440000000)) * 255u;
  CHECK(wrapped == int64_t(float(w32) * 1.0f));
}

static void testTaxTable() {
  TaxDemandTable t;
  TaxEffectEntry e0{0.3f, -0.7f, 7, 0};
  auto tab = TaxDemandTable::entryTable(e0);
  CHECK(tab[0] == 0.3f);
  CHECK(tab[7] == 0.0f);
  CHECK(std::fabs(tab[22] - (-0.7f)) < 1e-6f);
  CHECK(std::fabs(tab[3] - 4.0f * 0.3f / 7.0f) < 1e-6f);
  CHECK(std::fabs(tab[10] - 3.0f * -0.7f / 15.0f) < 1e-6f);
  t.setEntries({e0});
  t.rebuild(12345);
  CHECK(t.effect(0) == 0.3f && t.effect(99) == t.effect(22));
  // two entries: interpolation uses pop, not pop - P_lo (defect)
  TaxEffectEntry e1{0.2f, -0.5f, 10, 1000};
  t.setEntries({e1, e0});   // unsorted input is sorted by P
  t.rebuild(500);
  auto lo = TaxDemandTable::entryTable(e0), hi = TaxDemandTable::entryTable(e1);
  for (int r = 0; r <= 22; r++) {
    float want = lo[r] + (hi[r] - lo[r]) * 500.0f / 1000.0f;
    CHECK(std::fabs(t.effect(r) - want) < 1e-6f);
  }
  t.rebuild(0);   // exact breakpoint: copy
  CHECK(t.effect(0) == 0.3f);
  // pop beyond the last breakpoint: copy of the last table
  t.rebuild(5000);
  CHECK(std::fabs(t.effect(0) - 0.2f) < 1e-6f);
  // defect visible: pop = 2*P_hi? with P_lo=1000 table equal to hi when pop == P_hi
  t.rebuild(1000);
  CHECK(std::fabs(t.effect(0) - 0.2f) < 1e-6f);
  // no entry at P=0 and pop below first: table unchanged
  TaxDemandTable u;
  u.setEntries({e1});
  u.rebuild(2000);
  float before = u.effect(0);
  u.rebuild(10);
  CHECK(u.effect(0) == before);
}

static void testConfigIni() {
  IniFile ini = IniFile::fromText(
      "[Admin]\nTaxationDemandEffect=1,1,1,1\n"
      "[Bonds]\nMaxBonds=4\nMonthlyPaymentPer1K=12\nMaxBondAmt=20\nBondLifespan=5\n"
      "[SpecialCosts]\nLowDensityZone=3\nDemolish=1\n"
      "[BuildingPlacementCosts]\n100 = 500 ; parent comment\n200 = 900 child\n"
      "[TuningParameters]\nLVToResTaxTransmogrifier=40\nMinAverageLV=300\n"
      "TaxationDemandEffect=3000,-7000,7,0;2000,-6000,9,10000;\n"
      "TireRecyclingOrdEffect=25:200,300\n");
  BudgetConfig c = BudgetConfig::fromIni(ini);
  CHECK(c.maxBonds == 4 && c.monthlyPaymentPer1K == 12 && c.maxBondAmt == 20 && c.bondLifespan == 5);
  CHECK(std::fabs(c.lvToTax[0] - 0.004f) < 1e-7f && std::fabs(c.lvToTax[1] - 0.0045f) < 1e-7f);
  CHECK(c.minAverageLV == 255);   // clamped
  CHECK(c.taxEffects.size() == 2 && c.taxEffects[1].pop == 10000 && c.taxEffects[1].n == 9);
  CHECK(std::fabs(c.taxEffects[0].b + 0.7f) < 1e-6f);
  CHECK(c.specialCostFor(0) == 3 && c.specialCostFor(7) == 1 && c.specialCostFor(5) == 0 && c.specialCostFor(99) == 0);
  CHECK(c.tireRecyclingPct == 25 && c.tireRecyclingTypes.count(300));
  AgentHierarchy h;
  h.setParent(300, 200);
  h.setParent(200, 100);
  h.setParent(400, 100);
  CHECK(c.placementCostFor(100, h, false) == 500);
  CHECK(c.placementCostFor(200, h, false) == 900);   // nearest listed ancestor is itself
  CHECK(c.placementCostFor(300, h, false) == 900);
  CHECK(c.placementCostFor(400, h, false) == 500);
  CHECK(c.placementCostFor(999, h, false) == 0);
  CHECK(c.placementCostFor(300, h, true) == 900 - 900 * 25 / 100);
  CHECK(c.placementCostFor(200, h, true) == 900 - 900 * 25 / 100);
  CHECK(c.placementCostFor(100, h, true) == 500);   // 100 not in the tire list
  // defaults without data
  BudgetConfig d = BudgetConfig::fromIni(IniFile::fromText(""));
  CHECK(d.maxBonds == 10 && d.maxBondAmt == 25 && d.minAverageLV == 35 && d.taxEffects.size() == 1);
  IniFile adv = IniFile::fromText("[TREASURY_NUMBERS]\nLOSE_GAME_LEVEL=50000\nTREASURY_LOW=900\n");
  BudgetConfig a = BudgetConfig::fromIni(IniFile::fromText(""), &adv);
  CHECK(a.loseGameLevel == 50000 && a.treasuryLow == 900);
}

static void testTreasury() {
  Budget b;
  int notified = 0;
  b.onFundsChanged = [&]() { notified++; };
  b.newCity(Budget::startingFunds(2), 30, -2, 7);
  CHECK(b.funds == 20000 && b.tax[0].rate == 22 && b.tax[1].rate == 0 && b.tax[2].rate == 7);
  CHECK(Budget::startingFunds(1) == 50000 && Budget::startingFunds(3) == 10000);
  CHECK(!b.spend(20001) && b.funds == 20000);
  CHECK(b.spend(20000) && b.funds == 0);
  CHECK(!b.spend(1));
  b.add(-5);   // Add is unconditional
  CHECK(b.funds == -5);
  CHECK(notified >= 3);
  // free placement
  b.cfg.specialCost[7] = 10;
  CHECK(b.specialCost(7) == 10);
  b.freePlacement = true;
  CHECK(b.specialCost(7) == 0);
  b.setTaxRate(1, 23);
  CHECK(b.tax[1].rate == 22);
  b.setFunding(dept::Police, 108);
  CHECK(b.dept(dept::Police)->funding == 108);
  CHECK(Budget::estimate(100, 10, 1) == 220 && Budget::estimate(100, 10, 12) == 110);
}

static MonthEndInput inputFor(int month, int year) {
  MonthEndInput in;
  in.month = month;
  in.year = year;
  in.population = 5000;
  in.avgLV = 100;
  in.zoneTotals[0] = 10000; in.zoneTotals[1] = 2000; in.zoneTotals[2] = 1000;
  return in;
}

static void testMonthEnd() {
  Budget b;
  b.newCity(1000);
  b.setDeptFullCost(dept::Police, 200);
  b.setDeptFullCost(dept::Roads, 100);
  b.setFunding(dept::Roads, 110);
  b.addMisc(70, 1);   // January misc
  MonthEndInput in = inputFor(2, 2000);   // closing January: slot 0
  in.ordIncome = 5; in.ordCost = 7;
  in.dealIncome = 11; in.dealCost = 13;
  in.bizIncome = 300;
  in.fares = 40.9;
  b.dealPenaltyAccumulator = 20;
  auto r = b.monthEnd(in);
  CHECK(r.slot == 0 && r.budgetYear == 2000 && !r.yearEnd && !r.openBudgetWindow);
  int64_t tR = taxRevenue(7, 10000, 100, 35, 0.0035f);
  CHECK(b.tax[0].hist[0] == tR && b.tax[0].ytd == tR);
  CHECK(b.tax[1].hist[0] == taxRevenue(7, 2000, 100, 35, 0.0045f));
  CHECK(b.ordIncomeHist[0] == 5 && b.ordCostYTD == 7);
  CHECK(b.dealCostHist[0] == 33 && b.dealPenaltyAccumulator == 0 && b.dealIncomeYTD == 11);
  CHECK(b.bizHist[0] == 300 && b.otherHist[0] == 40 && b.otherYTD == 40);
  CHECK(b.miscYTD == 70 && b.miscHist[1] == 0);
  CHECK(b.dept(dept::Roads)->hist[0] == 110 && b.dept(dept::Roads)->lastCharge == 110 && b.dept(dept::Police)->ytd == 200);
  CHECK(b.funds == 1000);   // money moves only at year end
  CHECK(b.treasuryChart.monthly[0] == (1000 + 100000) / 1000);
  CHECK(b.treasuryChart.yearly[0] == 100);

  // run Feb..Dec closing: months 3..12, then Jan (year end)
  for (int m = 3; m <= 12; m++) b.monthEnd(inputFor(m, 2000));
  CHECK(b.funds == 1000);
  MonthEndInput jan = inputFor(1, 2001);
  auto y = b.monthEnd(jan);   // closes December of 2000
  CHECK(y.yearEnd && y.slot == 11 && y.budgetYear == 2000 && y.openBudgetWindow);
  // year totals: 12 months of tax, with first month having extras
  int64_t taxes = 12 * (tR + taxRevenue(7, 2000, 100, 35, 0.0045f) + taxRevenue(7, 1000, 100, 35, 0.005f));
  int64_t income = taxes + 5 + 11 + 300 * 1 + 40 + 70;
  int64_t expenses = 7 + 33 + 12 * 200 + 12 * 110;
  CHECK(b.funds == 1000 + income - expenses);
  CHECK(b.tax[0].ytd == 0 && b.ordCostYTD == 0 && b.miscYTD == 0 && b.dept(dept::Police)->ytd == 0);
  CHECK(b.treasuryChart.yearly[0] == uint32_t((b.funds + 100000) / 1000));
  CHECK(b.treasuryChart.decade[0] == 100);   // 2000: (2000+1) % 10 != 0, no decade sample
  // decade sample when budget year + 1 is a multiple of 10
  Budget c;
  c.newCity(0);
  for (int m = 2; m <= 12; m++) c.monthEnd(inputFor(m, 2009));
  c.funds = 12345;
  c.monthEnd(inputFor(1, 2010));
  CHECK(c.treasuryChart.decade[0] == uint32_t((c.funds + 100000) / 1000) && c.treasuryChart.yearly[0] == c.treasuryChart.decade[0]);
}

static void testYearEndPopupAndFloor() {
  Budget b;
  b.newCity(0);
  b.showBudgetOnlyInDebt = true;
  for (int m = 2; m <= 12; m++) b.monthEnd(inputFor(m, 2000));
  MonthEndInput in = inputFor(1, 2001);
  in.zoneTotals[0] = in.zoneTotals[1] = in.zoneTotals[2] = 0;
  b.setDeptFullCost(dept::Roads, 10000);   // expensive: forces debt at year-end
  b.dept(dept::Roads)->ytd = 0;
  auto r = b.monthEnd(in);
  CHECK(r.yearEnd && b.funds < 0 && r.openBudgetWindow);   // in debt: shown even with the option on
  // not in debt with option on: no window
  Budget ok;
  ok.newCity(5000);
  ok.showBudgetOnlyInDebt = true;
  MonthEndInput dec = inputFor(1, 2001);
  auto r2 = ok.monthEnd(dec);
  CHECK(r2.yearEnd && ok.funds >= 0 && !r2.openBudgetWindow);
  // debt floor
  Budget f;
  f.newCity(0);
  f.debtFloor = true;
  f.setDeptFullCost(dept::Roads, 1000000);
  MonthEndInput z = inputFor(1, 2001);
  z.zoneTotals[0] = z.zoneTotals[1] = z.zoneTotals[2] = 0;
  f.monthEnd(z);   // first month: slot 11, charges 1,000,000
  CHECK(f.funds == -99999);
  CHECK(f.treasuryChart.monthly[0] == 0);   // (-99999+100000)/1000 = 0
}

static void testBonds() {
  Budget b;
  b.newCity(0);
  CHECK(!b.issueBond(4999, 3, 2000));
  CHECK(!b.issueBond(25001 + 5000, 3, 2000));   // above the single-bond maximum
  CHECK(b.issueBond(25000, 3, 2000));
  CHECK(b.funds == 25000 && b.totalPrincipal == 25000);
  const Bond& bd = b.bonds[0];
  CHECK(bd.month == 3 && bd.maturityYear == 2010 && bd.monthlyPayment == 250 && bd.totalRepayable == 30000 &&
        bd.remaining == 30000 && bd.paid == 0);
  CHECK(b.issueBond(12345, 4, 2000));   // rounds down to 10,000
  CHECK(b.bonds[1].principal == 10000 && b.bonds[1].monthlyPayment == 100);
  // count limit
  Budget c;
  for (int i = 0; i < 10; i++) CHECK(c.issueBond(25000, 1, 2000));
  CHECK(!c.issueBond(5000, 1, 2000));
  // payments
  Budget d;
  d.newCity(0);
  d.issueBond(10000, 12, 2000);   // month 12: slot never equals 12 -> only remaining==0 retires
  int months = 0;
  int y = 2000, mo = 2;
  while (!d.bonds.empty() && months < 200) {
    auto r = d.monthEnd(inputFor(mo, y));
    (void)r;
    if (!d.bonds.empty()) CHECK(d.bondPayHist[(mo == 1 ? 11 : mo - 2)] == 100);
    months++;
    if (++mo == 13) { mo = 1; y++; }
  }
  CHECK(months == 120 && d.totalPrincipal == 0);
  // retire by maturity: stored month (1-based) compared directly with the 0-based slot
  Budget e;
  e.newCity(0);
  e.issueBond(10000, 5, 2000);   // maturity 2010, month field 5
  e.bonds[0].remaining *= 2;     // keep it from paying off first
  int cnt = 0;
  y = 2000; mo = 6;              // first closing: slot 4 of 2000
  while (!e.bonds.empty() && cnt < 400) {
    e.monthEnd(inputFor(mo, y));
    cnt++;
    if (++mo == 13) { mo = 1; y++; }
  }
  CHECK(cnt == 10 * 12 + 2);     // retires at slot 5 of 2010 (new month 7)
  CHECK(e.totalPrincipal == 0);
}

static void testBudgetRecord() {
  Budget b;
  b.newCity(50000);
  Bytes fresh = b.serialize();
  CHECK(fresh.size() == 1981);   // every shipped record: 3 tax groups, 6 departments, 0 bonds
  CHECK(rd32(fresh.data() + 8) == 3);
  CHECK(rd32(fresh.data() + 12) == 3000 && fresh[16] == 7);
  // populate everything
  b.issueBond(15000, 6, 1999);
  b.setFunding(dept::Education, 104);
  for (int m = 2; m <= 8; m++) b.monthEnd(inputFor(m, 1999));
  b.addMisc(33, 9);
  b.dealPenaltyAccumulator = 77;
  Bytes s = b.serialize();
  CHECK(s.size() == 1981 + 25);
  Budget l;
  l.parse(s.data(), s.size());
  CHECK(l.serialize() == s);
  CHECK(l.funds == b.funds && l.bonds.size() == 1 && l.bonds[0].principal == 15000);
  CHECK(l.dept(dept::Education)->funding == 104 && l.dealPenaltyAccumulator == 77);
  CHECK(l.tax[0].hist[0] == b.tax[0].hist[0] && l.treasuryChart.monthly[0] == b.treasuryChart.monthly[0]);
  CHECK(!l.freePlacement && !l.debtFloor);
  // unknown department IDs are skipped, bonds beyond MaxBonds discarded
  Bytes t = s;
  size_t deptOff = 8 + 4 + 3 * 109 + 4;
  wr32(t.data() + deptOff, 0x12345678);   // first department (Education, funded 104) becomes unknown
  Budget u;
  u.parse(t.data(), t.size());
  CHECK(u.dept(dept::Education)->funding == 100);
  CHECK(u.dept(0x12345678) == nullptr && u.depts.size() == 6);
  Budget v(BudgetConfig{});
  v.cfg.maxBonds = 0;
  v.parse(s.data(), s.size());
  CHECK(v.bonds.empty());
  // truncated record throws
  bool threw = false;
  try { Budget w; w.parse(s.data(), 100); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
}

// New-city budget record against the observed template (spec 30 s6, spec 19 s6.1).
static void testNewCityBudgetRecord() {
  for (int diff = 1; diff <= 3; diff++) {
    Budget b;
    if (diff == 3) { b.newCity(0); b.issueBond(10000, 1, 2000); }
    else b.newCity(Budget::startingFunds(diff));
    Bytes s = b.serialize();
    const size_t bond = diff == 3 ? 25 : 0;
    CHECK(s.size() == 1981 + bond);
    CHECK(rd32(s.data()) == uint32_t(diff == 1 ? 50000 : diff == 2 ? 20000 : 10000));
    CHECK(s[8] == 3 && rd16(s.data() + 12) == 3000 && s[16] == 7);
    CHECK(rd16(s.data() + 121) == 5000 && s[125] == 7 && rd16(s.data() + 230) == 8000 && s[234] == 7);
    CHECK(s[339] == 6);
    // Departments in ascending ID order: Education, Public Health, Fire, Roads, Police, Mass Transit; funding 100.
    const uint32_t ids[6] = {dept::Education, dept::Health, dept::Fire, dept::Roads, dept::Police, dept::Transit};
    const uint32_t raw[6] = {0x02922A5D, 0x42922A6C, 0x62963928, 0x8290F605, 0xA2963983, 0xC290F609};
    for (int i = 0; i < 6; i++) {
      CHECK(ids[i] == raw[i]);
      CHECK(rd32(s.data() + 343 + 117 * i) == raw[i]);
      CHECK(s[343 + 117 * i + 4] == 100);
    }
    if (diff == 3) {
      CHECK(s[1253] == 1 && s[1257] == 1);
      CHECK(rd32(s.data() + 1262) == 10000 && rd32(s.data() + 1266) == 12000 && rd32(s.data() + 1270) == 12000);
      CHECK(rd32(s.data() + 1274) == 0 && rd32(s.data() + 1278) == 100 && rd32(s.data() + 1338) == 10000);
    }
    // Treasury chart: 32 samples of 100, scale 200.
    for (size_t o = 1425 + bond; o <= 1549 + bond; o += 4) CHECK(rd32(s.data() + o) == 100);
    CHECK(rd32(s.data() + 1553 + bond) == 200);
    // Nothing else is non-zero (the maturity word of the Hard bond at 1258 is excluded, see spec 19 s3).
    std::vector<bool> known(s.size(), false);
    auto mark = [&](size_t o, size_t n) { for (size_t i = 0; i < n; i++) known[o + i] = true; };
    mark(0, 4); mark(8, 1); mark(12, 2); mark(16, 1); mark(121, 2); mark(125, 1); mark(230, 2); mark(234, 1); mark(339, 1);
    for (int i = 0; i < 6; i++) mark(343 + 117 * i, 5);
    if (diff == 3) { mark(1253, 1); mark(1257, 1); mark(1258, 4); mark(1262, 12); mark(1278, 1); mark(1338, 2); }
    for (size_t o = 1425 + bond; o <= 1553 + bond; o += 4) mark(o, 1);
    int stray = 0;
    for (size_t i = 0; i < s.size(); i++) if (!known[i] && s[i] != 0) stray++;
    CHECK(stray == 0);
  }
}

static void testOrdinances() {
  Ordinances o;
  CHECK(o.catalogue().size() == 42);
  o.newCity();
  CHECK(o.available(ord::LegalGambling) && !o.available(ord::CleanAir) && !o.available(ord::YouthCurfew));
  CHECK(!o.enact(ord::CleanAir));          // not available
  CHECK(o.enact(ord::LegalGambling) && o.enacted(ord::LegalGambling));
  OrdinanceMoneyInput in;
  in.population = 105000;
  auto m = o.monthlyMoney(in);
  CHECK(m.income == 105000 / 1050 && m.cost == 0);   // gambling is income
  o.setAvailable(ord::YouthSports, true);
  o.enact(ord::YouthSports);
  m = o.monthlyMoney(in);
  CHECK(m.cost == 105000 / 14000 + 1);
  CHECK(o.monthlyValue(ord::YouthSports, in) == -(105000 / 14000 + 1));
  // cost plus facility
  o.setAvailable(ord::YouthCurfew, true);
  o.enact(ord::YouthCurfew);
  in.policeStations = 4;
  CHECK(o.monthlyValue(ord::YouthCurfew, in) == -(105000 / 8000 + 1 + 4));
  // world cost default population / 1000, configurable
  o.setAvailable(ord::SmokeDetectors, true);
  o.enact(ord::SmokeDetectors);
  CHECK(o.monthlyValue(ord::SmokeDetectors, in) == -((105000 / 1000) / 959 + 1));
  in.world = 959 * 300;
  CHECK(o.monthlyValue(ord::SmokeDetectors, in) == -301);
  // free
  o.setAvailable(ord::FarmersMarket, true);
  o.enact(ord::FarmersMarket);
  CHECK(o.monthlyValue(ord::FarmersMarket, in) == 0);
  // repeal
  CHECK(o.repeal(ord::YouthSports) && o.monthlyValue(ord::YouthSports, in) == 0);
  // enacted but not available (loaded state) gives nothing
  Ordinances p;
  CHECK(p.monthlyMoney(in).cost == 0 && p.monthlyMoney(in).income == 0);
}

static void testOrdinanceAvailability() {
  Ordinances o;
  o.newCity();
  AvailabilityInput in;
  in.currentYear = 1925;
  // find a seed where the 10% roll passes
  int hits = 0, tries = 0;
  SimRng rng(1);
  std::optional<uint32_t> first;
  while (!first && tries < 1000) {
    first = o.availabilityScan(rng, in);
    tries++;
  }
  CHECK(first.has_value());
  // ascending ID order: the lowest unavailable ordinance whose year < 1925 and year != 9999
  CHECK(o.available(*first) && o.def(*first)->year < 1925);
  // exactly one new ordinance per success; probability about 10%
  Ordinances q;
  q.newCity();
  SimRng r2(42);
  int success = 0;
  for (int i = 0; i < 3000; i++) {
    Ordinances t;
    t.newCity();
    if (t.availabilityScan(r2, in)) success++;
  }
  hits = success;
  CHECK(hits > 200 && hits < 400);
  // year gating: strictly after
  const OrdinanceDef* d = o.def(ord::CleanAir);   // year 1950
  CHECK(!o.conditionHolds(*d, in));
  in.currentYear = 1950;
  CHECK(!o.conditionHolds(*d, in));
  in.currentYear = 1951;
  CHECK(o.conditionHolds(*d, in));
  // special triggers
  AvailabilityInput t;
  t.currentYear = 2000;
  CHECK(!o.conditionHolds(*o.def(ord::HomelessShelter), t));
  t.homelessPercent = 11;
  CHECK(o.conditionHolds(*o.def(ord::HomelessShelter), t));
  CHECK(!o.conditionHolds(*o.def(ord::Aerospace), t));
  t.airports = 1;
  CHECK(o.conditionHolds(*o.def(ord::Aerospace), t));
  t.currentYear = 1960;
  CHECK(!o.conditionHolds(*o.def(ord::Aerospace), t));   // must exceed 1960
  CHECK(!o.conditionHolds(*o.def(ord::YouthCurfew), t));
  t.policeStations = 1;
  CHECK(o.conditionHolds(*o.def(ord::YouthCurfew), t));
  t.busStations = 2; t.railStations = 1;
  CHECK(o.conditionHolds(*o.def(ord::Shuttle), t) && !o.conditionHolds(*o.def(ord::SubsidizedTransit), t));
  t.subwayStations = 1;
  CHECK(o.conditionHolds(*o.def(ord::SubsidizedTransit), t));
  t.commercialBuildings = 26;
  CHECK(o.conditionHolds(*o.def(ord::TouristPromotion), t));
  t.commercialBuildings = 25;
  CHECK(!o.conditionHolds(*o.def(ord::TouristPromotion), t));
  t.residentialBuildings = 300;
  CHECK(o.conditionHolds(*o.def(ord::EarthquakeRetrofit), t));   // 300+25+... > 300
  CHECK(!o.conditionHolds(*o.def(ord::NuclearFree), AvailabilityInput{1980}) );
  // cheat: all available, announced
  std::vector<uint32_t> ann;
  o.makeAllAvailable(&ann);
  CHECK(ann.size() == 42 && o.available(ord::Aerospace));
}

static void testOrdinanceIniAndRecord() {
  IniFile ini = IniFile::fromText(
      "[ORDINANCE_TRIGGERS]\nHOMELESS_SHELTER_PERCENT=20\nAEROSPACE_AVAILABLE_YEAR=1970\n"
      "[ListOfOrdinances]\n0xA0D07129 = 0x628D0C45;0;121;1050\n0xC2BF1DC5 = 0x828D0A2F;9999;120;0\n"
      "0x0 = 5;0;1;1\n0x1234 = 0;0;1;1\n");
  Ordinances o;
  o.loadCatalogue(ini);
  CHECK(o.catalogue().size() == 2);
  CHECK(o.triggers.homelessShelterPercent == 20 && o.triggers.aerospaceAvailableYear == 1970);
  CHECK(o.def(ord::YouthCurfew)->costDivisor == 100);   // 0 treated as 100
  CHECK(o.def(ord::YouthCurfew)->money == OrdMoney::CostPlusFacility);
  CHECK(o.def(ord::LegalGambling)->money == OrdMoney::Income);

  Ordinances full;
  full.newCity();
  full.enact(ord::LegalGambling);
  full.setAvailable(ord::Carpool, true);
  full.enact(ord::Carpool);
  Bytes s = full.serialize();
  CHECK(s.size() == 4 + 6 * 42);
  CHECK(rd32(s.data()) == 42);
  // ascending ID order
  bool asc = true;
  for (int i = 1; i < 42; i++) if (rd32(s.data() + 4 + 6 * i) <= rd32(s.data() + 4 + 6 * (i - 1))) asc = false;
  CHECK(asc);
  Ordinances r;
  r.parse(s.data(), s.size());
  CHECK(r.enacted(ord::LegalGambling) && r.enacted(ord::Carpool) && r.available(ord::LegalGambling));
  CHECK(r.serialize() == s);
  // unknown IDs ignored
  Bytes t;
  put32(t, 2);
  put32(t, 0xDEAD0001); t.push_back(1); t.push_back(1);
  put32(t, ord::Carpool); t.push_back(1); t.push_back(1);
  Ordinances u;
  u.parse(t.data(), t.size());
  CHECK(u.enacted(ord::Carpool) && u.catalogue().size() == 42);
}

static void testGrowth() {
  // wrap quirk: small population that drops below zero wraps and is reduced under the cap
  bool sawJump = false;
  for (uint32_t seed = 1; seed < 400 && !sawJump; seed++) {
    SimRng r(seed);
    uint32_t p = NeighbourDeals::growPopulation(2000, 4000000, r);
    CHECK(p >= 1000 && p <= 4000000);
    if (p > 3000000) sawJump = true;
  }
  // deterministic checks on the step rules
  SimRng r(9);
  uint32_t p = NeighbourDeals::growPopulation(500000, 4000000, r);
  CHECK(p > 400000 && p < 600000);
  // above the cap: reduced by multiples of 10,000 until <= cap
  SimRng r2(9);
  uint32_t big = NeighbourDeals::growPopulation(4500000, 4000000, r2);
  CHECK(big <= 4000000 && big > 3989999 - 100000);
}

static void testNeighbours() {
  NeighbourDeals nd;
  SimRng rng(7);
  nd.newCity(rng, 128, 128, [](int x, int z) { return !(x == 0 && z == 64); });
  for (int i = 0; i < 4; i++) {
    CHECK(nd.neighbours[i].population >= 2000 && nd.neighbours[i].population < 15000);
    CHECK(nd.neighbours[i].populationCap == 4000000);
  }
  CHECK(!nd.neighbours[0].exists && nd.neighbours[1].exists);
  // distinct names
  for (int i = 0; i < 4; i++)
    for (int j = i + 1; j < 4; j++) CHECK(nd.neighbours[i].cityName != nd.neighbours[j].cityName);
  // named from a string-table lookup
  NeighbourDeals nn;
  nn.newCity(rng, 64, 64, {}, [](uint32_t inst) { return "N" + std::to_string(inst); });
  CHECK(nn.neighbours[0].cityName.size() >= 3);
  uint32_t inst = uint32_t(std::stoi(nn.neighbours[0].cityName.substr(1)));
  CHECK(inst >= 41 && inst <= 76);
  inst = uint32_t(std::stoi(nn.neighbours[0].mayorName.substr(1)));
  CHECK(inst >= 2 && inst <= 37);

  // edge mapping, corners explicit
  CHECK(NeighbourDeals::edgeNeighbour(0, 0, 64, 64) == 0);
  CHECK(NeighbourDeals::edgeNeighbour(63, 0, 64, 64) == 1);
  CHECK(NeighbourDeals::edgeNeighbour(63, 63, 64, 64) == 2);
  CHECK(NeighbourDeals::edgeNeighbour(0, 63, 64, 64) == 3);
  CHECK(NeighbourDeals::edgeNeighbour(0, 10, 64, 64) == 0);
  CHECK(NeighbourDeals::edgeNeighbour(10, 0, 64, 64) == 1);
  CHECK(NeighbourDeals::edgeNeighbour(63, 10, 64, 64) == 2);
  CHECK(NeighbourDeals::edgeNeighbour(10, 63, 64, 64) == 3);
  CHECK(NeighbourDeals::edgeNeighbour(10, 10, 64, 64) == -1);
  CHECK(nd.registerConnection(ConnPower, 5, 0, 128, 128) == 1);
  CHECK(nd.registerConnection(ConnPower, 5, 0, 128, 128) == 1);   // duplicate: no recount
  CHECK(nd.neighbours[1].nPower == 1 && nd.conn[3].size() == 1);
  CHECK(nd.registerConnection(ConnRoad, 0, 20, 128, 128) == 0 && nd.neighbours[0].nRoad == 1 && nd.conn[1].count((0u << 16) | 20));
  CHECK(nd.registerConnection(ConnRail, 20, 127, 128, 128) == 3 && nd.conn[0].count((20u << 16) | 127));
  CHECK(nd.registerConnection(ConnPipe, 50, 50, 128, 128) == -1);
  CHECK(nd.connectionCount(1, ConnPower) == 1);
}

static OfferContext ctxFor(uint32_t pop) {
  OfferContext c;
  c.cityPopulation = pop;
  c.currentYear = 1990;
  c.powered = [](int, int) { return true; };
  c.watered = [](int, int) { return true; };
  return c;
}

static void testDeals() {
  NeighbourDeals nd;
  nd.cfg.baseRate[0] = 20;
  nd.cfg.rateVariable = 1;      // deterministic rate
  nd.cfg.basePower = 1000;
  nd.cfg.powerUnitsPer1K = 100;
  nd.cfg.percentPopForBuy = 100;
  nd.cfg.penaltyMultiplier = 3;
  nd.cfg.minPowerBuy = 50;
  SimRng rng(3);
  nd.newCity(rng, 64, 64);
  // no connection: no offer
  CHECK(!nd.makeOffer(0, ctxFor(100000), rng));
  for (int i = 0; i < 4; i++) nd.neighbours[i].nPower = 1;
  auto offer = nd.makeOffer(0, ctxFor(100000), rng);
  CHECK(offer.has_value() && nd.pending.size() == 1);
  // k = floor(100000*100/100000) = 100 -> units = 100*100 = 10000 (multiple of 1000)
  CHECK(offer->quantity == 10000 && offer->rate == 20 && offer->sizeOrTerm == 100);
  CHECK(offer->monthlyAmount == 10000 * 20 / 1000 && offer->penalty == 3 * 200);
  CHECK(offer->year == 1990);
  CHECK(!nd.makeOffer(0, ctxFor(100000), rng));   // pending offer of same type
  CHECK(nd.accept(0) && nd.active.size() == 1 && nd.pending.empty());
  CHECK(nd.neighbours[nd.active[0].neighbourIndex].dealMade[0]);
  auto m = nd.monthlyMoney();
  CHECK(m.cost == 200 && m.income == 0);
  // buy quantity follows reported usage, never below minimum cost
  nd.reportQuantity(0, 1000);
  m = nd.monthlyMoney();
  CHECK(m.cost == 50);
  // age advances monthly and deal can be cancelled with penalty before 60 months
  nd.monthly(3, rng);
  CHECK(nd.active[0].monthCounter == 1);
  const int64_t expectPen = nd.active[0].penalty;
  int64_t pen = nd.cancel(0);   // cancel then remove: gone at once, every flag of the type cleared (spec 19 10.5)
  CHECK(pen == expectPen && pen > 0 && nd.active.empty());
  for (int i = 0; i < 4; i++) CHECK(!nd.neighbours[i].dealMade[0]);
  CHECK(nd.monthlyMoney().cost == 0);
  // after 60 months: removed without penalty
  nd.pending.clear();
  auto o2 = nd.makeOffer(0, ctxFor(100000), rng);
  CHECK(o2.has_value());
  nd.accept(0);
  for (int i = 0; i < 60; i++) nd.monthly(3, rng);
  CHECK(nd.active[0].monthCounter == 60);
  CHECK(nd.cancel(0) == 0 && nd.active.empty());

  {  // monthly connection check (spec 19 10.5): lost link -> terminated with penalty (age < 60) or removed (age >= 60)
    NeighbourDeals k; SimRng r2(5); k.newCity(r2, 64, 64);
    for (int i = 0; i < 4; i++) k.neighbours[i].nPower = 1;
    OfferContext cx = ctxFor(100000);
    auto of = k.makeOffer(0, cx, r2); CHECK(of.has_value()); k.accept(0);
    const int nb = k.active[0].neighbourIndex;
    CHECK(k.checkConnections(cx) == 0 && !k.active[0].terminated());   // link intact
    k.neighbours[nb].nPower = 0;
    const int64_t pen = k.active[0].penalty;
    CHECK(k.checkConnections(cx) == pen && k.active[0].terminated() && k.neighbours[nb].dealMade[0]);   // flag kept
    CHECK(k.checkConnections(cx) == 0);                                  // already terminated: no second penalty
    k.removeType(0);
    k.neighbours[nb].nPower = 1; k.pending.clear();
    auto o3 = k.makeOffer(0, cx, r2); CHECK(o3.has_value()); k.accept(0);
    const int nb3 = k.active[0].neighbourIndex; k.active[0].monthCounter = 60;
    k.neighbours[nb3].nPower = 0;
    CHECK(k.checkConnections(cx) == 0 && k.active.empty() && !k.neighbours[nb3].dealMade[0]);   // old deal: removed, flag cleared
  }

  // sell deal: income, and dropped when units < reported quantity
  NeighbourDeals s;
  s.cfg.baseRate[1] = 30;
  s.cfg.rateVariable = 1;
  s.cfg.percentPopForSell = 100;
  s.cfg.basePower = 1000;
  s.cfg.powerUnitsPer1K = 100;
  s.newCity(rng, 64, 64);
  for (int i = 0; i < 4; i++) { s.neighbours[i].nPower = 1; s.neighbours[i].population = 50000; }
  OfferContext c = ctxFor(0);
  c.referenceRate = 35;     // rate raised to the neighbour's reference rate
  c.availableToSell = 0;
  auto so = s.makeOffer(1, c, rng);
  CHECK(so.has_value() && so->rate == 35 && so->quantity == 5000 && so->monthlyAmount == 175);
  s.accept(0);
  auto sm = s.monthlyMoney();
  CHECK(sm.income == 175 && sm.cost == 0);
  s.reportQuantity(1, 6000);   // city cannot deliver: terminated, no penalty
  CHECK(s.active[0].terminated());
  s.monthly(2, rng);
  // No sweep: the terminated deal stays, keeps its flag and still occupies its type until it is removed or replaced.
  CHECK(s.active.size() == 1 && s.active[0].terminated() && s.dealOfType(1) && s.neighbours[s.active[0].neighbourIndex].dealMade[1]);
  CHECK(s.monthlyMoney().income == 0);   // billing of terminated deals is not modelled (spec 19 10.5: cost side unchecked)
  s.removeType(1);
  CHECK(s.active.empty() && !s.dealOfType(1));
  for (int i = 0; i < 4; i++) CHECK(!s.neighbours[i].dealMade[1]);
  // unpowered connection: no sell offer
  NeighbourDeals u;
  u.newCity(rng, 64, 64);
  for (int i = 0; i < 4; i++) u.neighbours[i].nPower = 1;
  OfferContext uc = ctxFor(0);
  uc.powered = [](int, int) { return false; };
  CHECK(!u.makeOffer(1, uc, rng));
  // garbage: seaport suffices
  NeighbourDeals g;
  g.newCity(rng, 64, 64);
  OfferContext gc = ctxFor(50000);
  CHECK(!g.makeOffer(4, gc, rng));
  gc.hasSeaport = true;
  CHECK(g.makeOffer(4, gc, rng).has_value());
  // scenario gate
  NeighbourDeals gate;
  gate.newCity(rng, 64, 64);
  gate.newDealsEnabled = false;
  gc.hasSeaport = true;
  CHECK(!gate.makeOffer(4, gc, rng));
  // one deal per commodity: neighbour with a deal of the opposite direction is skipped
  NeighbourDeals w;
  w.newCity(rng, 64, 64);
  for (int i = 0; i < 4; i++) { w.neighbours[i].nPower = 1; w.neighbours[i].nPipe = 1; }
  w.neighbours[0].dealMade[1] = w.neighbours[1].dealMade[1] = w.neighbours[2].dealMade[1] = true;
  bool onlyThree = true;
  for (int i = 0; i < 20; i++) {
    int n = w.chooseNeighbour(0, ctxFor(1000), rng);
    if (n != 3) onlyThree = false;
  }
  CHECK(onlyThree);
}

static void testNeighbourRecord() {
  NeighbourDeals nd;
  SimRng rng(11);
  nd.newCity(rng, 128, 128);
  nd.registerConnection(ConnRoad, 0, 10, 128, 128);
  nd.registerConnection(ConnRoad, 0, 11, 128, 128);
  nd.registerConnection(ConnPower, 40, 0, 128, 128);
  nd.registerConnection(ConnPipe, 127, 30, 128, 128);
  nd.registerConnection(ConnHighway, 40, 127, 128, 128);
  nd.registerConnection(ConnRail, 1, 0, 128, 128);
  Deal d;
  d.neighbourName = nd.neighbours[1].cityName;
  d.text = "placeholder %s";
  d.penalty = -5; d.monthlyAmount = 1234; d.neighbourIndex = 1; d.dealType = 3;
  d.monthCounter = 17; d.quantity = 2000; d.rate = 55; d.sizeOrTerm = 10; d.year = 1999;
  nd.active.push_back(d);
  nd.pending.push_back(d);
  nd.pending.push_back(d);
  Bytes s = nd.serialize();
  NeighbourDeals l;
  l.parse(s.data(), s.size());
  CHECK(l.serialize() == s);
  CHECK(l.active.size() == 1 && l.pending.size() == 2);
  CHECK(l.active[0].text == "placeholder %s" && l.active[0].penalty == -5 && l.active[0].rate == 55);
  CHECK(l.neighbours[0].nRoad == 2 && l.conn[1].size() == 2 && l.conn[3].size() == 1 && l.conn[2].size() == 1);
  CHECK(l.neighbours[0].cityName == nd.neighbours[0].cityName);
  // counts and sets are kept independently as stored
  l.neighbours[0].nRoad = 1;
  Bytes s2 = l.serialize();
  NeighbourDeals l2;
  l2.parse(s2.data(), s2.size());
  CHECK(l2.neighbours[0].nRoad == 1 && l2.conn[1].size() == 2);
  // sets are written ascending
  nd.conn[1].insert(5);
  Bytes s3 = nd.serialize();
  NeighbourDeals l3;
  l3.parse(s3.data(), s3.size());
  CHECK(*l3.conn[1].begin() == 5);
  // size of an empty-deal record: 4 neighbours + 2 counts + 6 set counts
  NeighbourDeals e;
  for (auto& n : e.neighbours) { n.cityName = "A"; n.mayorName = "B"; }
  CHECK(e.serialize().size() == 4 * (5 + 5 + 4 + 4 + 1 + 24 + 6) + 8 + 24);
  bool threw = false;
  try { NeighbourDeals x; x.parse(s.data(), s.size() - 3); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
}

static void testDealIni() {
  IniFile ini = IniFile::fromText(
      "[DEAL_RATE_CONSTANTS]\nPOWER_BUY_RATE=21\nDEAL_RATE_VARIABLE=9\nDEAL_PENALTY_MULTIPLIER=4\n"
      "[BASE_DEAL_QUANTITIES]\nBASE_POWER=500\nPowerUnitsPer1K=250\nBASE_GARBAGE=40\n"
      "[MINIMUM_DEAL_COSTS]\nMIN_WATER_BUY=77\n");
  DealConfig c = DealConfig::fromIni(ini);
  CHECK(c.baseRate[0] == 21 && c.rateVariable == 9 && c.penaltyMultiplier == 4);
  CHECK(c.base(0) == 500 && c.base(1) == 500 && c.base(4) == 40 && c.unitsPer1K(1) == 250);
  CHECK(c.minCost(2) == 77 && c.minCost(3) == 77);
}

static void testBusinessDeals() {
  BusinessDeals b;
  b.monthsTillBusinessDeal = 3;
  b.treasuryLow = 1500;
  SimRng rng(1);
  BusinessDeals::Inputs in;
  in.funds = 100;
  in.population = 5000;
  in.currentYear = 2000;
  int offered = 0;
  // counter increments weekly while treasury is low; no offer before the threshold
  CHECK(!b.weeklyEvaluate(in, rng) && b.lowTreasuryCounter == 1);
  CHECK(!b.weeklyEvaluate(in, rng) && b.lowTreasuryCounter == 2);
  for (int i = 0; i < 40 && !b.pendingOffer; i++) {
    if (b.weeklyEvaluate(in, rng)) offered++;
  }
  CHECK(b.pendingOffer.has_value() && offered == 1);
  CHECK(b.defs[*b.pendingOffer].year <= 2000);
  size_t idx = *b.pendingOffer;
  CHECK(!b.weeklyEvaluate(in, rng));   // an offer is pending
  CHECK(b.accept() && !b.pendingOffer);
  CHECK(b.monthlyIncome() == b.defs[idx].value);
  b.remove(b.defs[idx].buildingType);
  CHECK(b.monthlyIncome() == 0);
  // counter decays when funds recover
  in.funds = 5000;
  int c0 = b.lowTreasuryCounter;
  b.weeklyEvaluate(in, rng);
  CHECK(b.lowTreasuryCounter == c0 - 1);
  for (int i = 0; i < 100; i++) b.weeklyEvaluate(in, rng);
  CHECK(b.lowTreasuryCounter == 0);
  // population gate and city-already-has gate
  BusinessDeals g;
  g.monthsTillBusinessDeal = 0;
  in.funds = 0;
  in.population = 1000;
  for (int i = 0; i < 50; i++) CHECK(!g.weeklyEvaluate(in, rng));
  in.population = 5000;
  in.cityHasBusiness = [](uint32_t) { return true; };
  for (int i = 0; i < 50; i++) CHECK(!g.weeklyEvaluate(in, rng));
  in.cityHasBusiness = {};
  in.casinoAllowed = []() { return false; };
  in.currentYear = 1899;   // nothing available yet
  for (int i = 0; i < 50; i++) CHECK(!g.weeklyEvaluate(in, rng));
  // MegaMall pays 300 per month
  BusinessDeals m;
  m.inForce.push_back(19002);
  CHECK(m.monthlyIncome() == 300);
  // ini
  IniFile ini = IniFile::fromText(
      "[BUSINESS_DEALS]\nMONTHS_TILL_BUSINESS_DEAL=7\n[TREASURY_NUMBERS]\nTREASURY_LOW=900\n"
      "[BUSINESS_DEALS2]\nCasino = 19003;1915;350;149\nMegaMall = 19002;1970;310;206\n");
  BusinessDeals f = BusinessDeals::fromIni(ini);
  CHECK(f.monthsTillBusinessDeal == 7 && f.treasuryLow == 900 && f.defs.size() == 2 && f.defs[1].value == 310);
  CHECK(f.defs[0].buildingType == 19003 && f.defs[0].messageId == 149);
}

int main() {
  testTaxRevenue();
  testTaxTable();
  testConfigIni();
  testTreasury();
  testMonthEnd();
  testYearEndPopupAndFloor();
  testBonds();
  testBudgetRecord();
  testNewCityBudgetRecord();
  testOrdinances();
  testOrdinanceAvailability();
  testOrdinanceIniAndRecord();
  testGrowth();
  testNeighbours();
  testDeals();
  testNeighbourRecord();
  testDealIni();
  testBusinessDeals();
  TEST_MAIN_END();
}
