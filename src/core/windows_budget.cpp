// Budget, ordinances, business deals, neighbours (spec 09 sections 6.1, 6.3, 6.4, 6.7).
#include <algorithm>
#include "core/windows.h"

namespace m3k {

using namespace wui;

// ================================================================ Budget window
namespace {

struct DeptRow { uint32_t id; uint32_t label; const char* fb; const char* key; uint32_t advisor; };
const DeptRow kDepts[] = {
  {dept::Police, 0x92, "Police", "police", kAdvSafety},
  {dept::Fire, 0x94, "Fire", "fire", kAdvSafety},
  {dept::Health, 0x95, "Public health", "health", kAdvDemographics},
  {dept::Education, 0x96, "Education", "education", kAdvDemographics},
  {dept::Roads, 0, "Roads", "roads", kAdvTransport},
  {dept::Transit, 0x97, "Transit", "transit", kAdvTransport},
};
const uint32_t kTaxLabel[3] = {0xA3, 0xA4, 0xA5};
const char* kTaxFb[3] = {"Residential tax", "Commercial tax", "Industrial tax"};
const char* kTaxKey[3] = {"tax.r", "tax.c", "tax.i"};

const DeptRow* deptRow(uint32_t id) {
  for (auto& d : kDepts) if (d.id == id) return &d;
  return nullptr;
}

std::string label(const WinEnv& E, uint32_t id, const char* fb) { return id ? E.win(id, fb) : std::string(fb); }

int64_t monthlyBondPayments(const Budget& b) {
  int64_t s = 0;
  for (auto& bd : b.bonds) s += bd.monthlyPayment;
  return s;
}

}  // namespace

std::vector<BudgetWindow::Line> BudgetWindow::lines(const WinEnv& E, const Budget& b, const BudgetView& v) {
  std::vector<Line> out;
  int m = std::max(1, std::min(12, v.in.month));
  auto add = [&](const std::string& key, const std::string& lab, int64_t ytd, int64_t est, bool income, uint32_t id) {
    out.push_back({key, lab, ytd, est, income, id});
  };
  for (auto& dr : kDepts) {
    const Department* d = b.dept(dr.id);
    if (!d) continue;
    add(dr.key, label(E, dr.label, dr.fb), d->ytd, Budget::estimate(d->ytd, d->charge(), m), false, dr.label);
  }
  add("ord.exp", E.win(0x8F, "Ordinances"), b.ordCostYTD, Budget::estimate(b.ordCostYTD, v.in.ordCost, m), false, 0x8F);
  add("deal.exp", E.win(0x90, "Neighbor deals"), b.dealCostYTD, Budget::estimate(b.dealCostYTD, v.in.dealCost, m), false, 0x90);
  add("loans", E.win(0x236, "Loans"), b.bondYTD, Budget::estimate(b.bondYTD, monthlyBondPayments(b), m), false, 0x236);
  for (int g = 0; g < kTaxGroups; g++)
    add(kTaxKey[g], E.win(kTaxLabel[g], kTaxFb[g]), b.tax[g].ytd, Budget::estimate(b.tax[g].ytd, b.currentTax(g, v.in), m), true, kTaxLabel[g]);
  add("ord.inc", E.win(0x8F, "Ordinances"), b.ordIncomeYTD, Budget::estimate(b.ordIncomeYTD, v.in.ordIncome, m), true, 0x8F);
  add("deal.inc", E.win(0x90, "Neighbor deals"), b.dealIncomeYTD, Budget::estimate(b.dealIncomeYTD, v.in.dealIncome, m), true, 0x90);
  add("biz", E.win(0xA2, "Business deals"), b.bizYTD, Budget::estimate(b.bizYTD, v.in.bizIncome, m), true, 0xA2);
  add("fares", E.win(0x288, "Transit fares"), b.otherYTD, Budget::estimate(b.otherYTD, int64_t(v.in.fares), m), true, 0x288);
  add("relief", E.win(0x1AF, "Disaster relief"), b.miscYTD, b.miscYTD, true, 0x1AF);
  return out;
}

WindowResult BudgetWindow::draw(Ui& ui, const WinEnv& E, Budget& b, const BudgetView& v) {
  WindowResult R;
  int lh = lineHeight(ui);
  int month = std::max(1, std::min(12, v.in.month));
  Rect r = centered(ui, 620, 460);
  std::string title = E.win(0x89, "Budget");
  if (page == Page::Dept || page == Page::Tax) {
    std::string rowName;
    if (page == Page::Dept) { if (auto* dr = deptRow(dept)) rowName = label(E, dr->label, dr->fb); }
    else rowName = E.win(kTaxLabel[taxGroup % 3], kTaxFb[taxGroup % 3]);
    title = subst(E.win(0x8A, "%s"), {rowName});
  } else if (page == Page::Loans) title = E.win(0x236, "Loans");
  else if (page == Page::Indicators) title = E.win(0x161, "City indicators");
  if (frame(ui, E, r, title)) { R.close = true; R.add(Act::Close); }
  int x0 = r.x + 14, y0 = r.y + 34;

  auto backAndAdvisor = [&](uint32_t advisor) {
    if (button(ui, E, "back", {r.x + 14, r.y + r.h - 34, 90, 24}, E.win(0x84, "Back"))) openMain();
    Rect pr{r.x + r.w - 104, r.y + 34, 90, 90};
    E.spot("advisor", pr);
    int idx = AdvisorWindow::advisorIndex(advisor);
    portrait(ui, pr, E.img(0x52DB60A0, AdvisorWindow::portraitInstance(idx, 3)), "$", rgb(0x7A, 0x84, 0xB8));
    if (ui.clicked(pr)) R.add(Act::OpenAdvisor, advisor);
    ui.textCentered({pr.x - 10, pr.y + pr.h + 2, pr.w + 20, lh}, E.win(0x8E, "Meet advisor"));
    if (button(ui, E, "link.ord", {r.x + r.w - 150, r.y + 160, 136, 22}, E.win(0x8F, "Ordinances"))) R.add(Act::OpenOrdinances);
    if (button(ui, E, "link.deals", {r.x + r.w - 150, r.y + 186, 136, 22}, E.win(0x90, "Neighbor deals"))) R.add(Act::OpenNeighbours);
    if (button(ui, E, "link.biz", {r.x + r.w - 150, r.y + 212, 136, 22}, E.win(0xA2, "Business deals"))) R.add(Act::OpenBusinessDeals);
  };

  if (page == Page::Main) {
    auto L = lines(E, b, v);
    int64_t incY = 0, incE = 0, expY = 0, expE = 0;
    for (auto& l : L) { if (l.income) { incY += l.ytd; incE += l.est; } else { expY += l.ytd; expE += l.est; } }
    // Two columns.
    const int colW = 292, rowH = lh + 5;
    for (int col = 0; col < 2; col++) {
      bool income = col == 1;
      int cx = x0 + col * (colW + 8);
      ui.text(cx, y0, E.win(income ? 0x87 : 0x86, income ? "Income" : "Expenditures"), "title12");
      rightText(ui, cx + colW - 90, y0 + 2, "YTD");
      rightText(ui, cx + colW - 22, y0 + 2, "Est.");
      int y = y0 + lh + 8;
      for (auto& l : L) {
        if (l.income != income) continue;
        ui.text(cx, y, l.label);
        rightText(ui, cx + colW - 90, y, E.money(l.ytd));
        rightText(ui, cx + colW - 22, y, E.money(l.est));
        Rect d{cx + colW - 18, y - 2, 18, rowH - 2};
        if (button(ui, E, "details:" + l.key, d, "?")) {
          if (l.key == "ord.exp" || l.key == "ord.inc") R.add(Act::OpenOrdinances);
          else if (l.key == "deal.exp" || l.key == "deal.inc") R.add(Act::OpenNeighbours);
          else if (l.key == "biz") R.add(Act::OpenBusinessDeals);
          else if (l.key == "loans") page = Page::Loans;
          else if (l.key == "tax.r") openTax(0);
          else if (l.key == "tax.c") openTax(1);
          else if (l.key == "tax.i") openTax(2);
          else for (auto& dr : kDepts) if (l.key == dr.key) openDept(dr.id);
        }
        y += rowH;
      }
    }
    // Summary block.
    int sy = y0 + lh + 8 + 9 * (lh + 5) + 10;
    ui.fill({x0, sy - 4, r.w - 28, 1}, ui.panelDark);
    int w2 = 280;
    int64_t endFunds = b.funds + (incE - expE);   // funds settle at year end (spec 19 s7), so add the whole-year estimate
    labelValue(ui, x0, sy, w2, E.win(0x99, "Year-to-date income"), E.money(incY));
    labelValue(ui, x0, sy + lh, w2, E.win(0x9A, "Estimated income"), E.money(incE));
    labelValue(ui, x0, sy + 2 * lh, w2, E.win(0x9B, "Year-to-date expenses"), E.money(expY));
    labelValue(ui, x0, sy + 3 * lh, w2, E.win(0x9C, "Estimated expenses"), E.money(expE));
    labelValue(ui, x0, sy + 4 * lh, w2, E.win(0x9D, "Year-to-date cash flow"), E.money(incY - expY));
    labelValue(ui, x0, sy + 5 * lh, w2, E.win(0x9E, "Estimated cash flow"), E.money(incE - expE));
    labelValue(ui, x0 + 300, sy, w2, E.win(0x9F, "Current funds"), E.money(b.funds), b.funds < 0 ? rgb(255, 0, 0) : 0);
    labelValue(ui, x0 + 300, sy + lh, w2, E.win(0xA0, "End-of-year funds"), E.money(endFunds), endFunds < 0 ? rgb(255, 0, 0) : 0);
    if (button(ui, E, "advisor", {x0 + 300, sy + 3 * lh, 200, 24}, E.win(0x238, "Meet financial advisor"))) R.add(Act::OpenAdvisor, kAdvBudget);
    if (button(ui, E, "indicators", {x0 + 300, sy + 3 * lh + 30, 200, 24}, E.win(0x161, "Indicators"))) page = Page::Indicators;
    if (button(ui, E, "ok", {r.x + r.w - 104, r.y + r.h - 34, 90, 24}, E.app(0xB4, "OK"))) { R.close = true; R.add(Act::Close); }
    return R;
  }

  if (page == Page::Dept) {
    Department* d = b.dept(dept);
    const DeptRow* dr = deptRow(dept);
    if (!d || !dr) { page = Page::Main; return R; }
    backAndAdvisor(dr->advisor);
    int pct = d->funding;
    ui.text(x0, y0 + 4, E.win(0x8B, "Adjust"), "title12");
    if (stepper(ui, E, "adjust", {x0, y0 + 30, 380, 22}, pct, 0, kMaxFundingUi, 1, "%")) {
      b.setFunding(dept, pct);
      R.add(Act::SetFunding, dept, pct);
    }
    int64_t ytd = d->ytd, est = Budget::estimate(d->ytd, d->charge(), month);
    int ry = y0 + 72;
    labelValue(ui, x0, ry, 380, E.win(0x8C, "Year to date"), E.money(ytd));
    labelValue(ui, x0, ry + lh, 380, E.win(0x8D, "Year-end estimate"), E.money(est));
    labelValue(ui, x0, ry + 2 * lh, 380, E.win(0x1B4, "Full monthly cost"), E.money(d->fullCost));
    labelValue(ui, x0, ry + 3 * lh, 380, E.win(0x9F, "Current funds"), E.money(b.funds));
    return R;
  }

  if (page == Page::Tax) {
    int g = taxGroup % 3;
    backAndAdvisor(kAdvBudget);
    int rate = b.tax[g].rate;
    ui.text(x0, y0 + 4, E.win(0x8B, "Adjust"), "title12");
    if (stepper(ui, E, "adjust", {x0, y0 + 30, 380, 22}, rate, 0, kMaxTaxRate, 1, "%")) {
      b.setTaxRate(g, rate);
      R.add(Act::SetTax, uint32_t(g), rate);
    }
    int64_t cur = b.currentTax(g, v.in);
    int64_t ytd = b.tax[g].ytd, est = Budget::estimate(ytd, cur, month);
    int ry = y0 + 72;
    labelValue(ui, x0, ry, 380, E.win(0xA7, "Year to date"), E.money(ytd));
    labelValue(ui, x0, ry + lh, 380, E.win(0x8D, "Year-end estimate"), E.money(est));
    labelValue(ui, x0, ry + 2 * lh, 380, E.win(0x283, "Funds"), E.money(b.funds));
    for (int k = 0; k < 3; k++)
      labelValue(ui, x0, ry + (4 + k) * lh, 380, E.win(kTaxLabel[k], kTaxFb[k]), std::to_string(b.tax[k].rate) + "%");
    if (v.approval >= 0) {
      ui.text(x0, ry + 8 * lh, E.win(0x1D2, "Approval rating"));
      bar(ui, {x0 + 170, ry + 8 * lh, 210, lh - 3}, float(v.approval) / 100.f, rgb(0, 160, 60));
    }
    return R;
  }

  if (page == Page::Loans) {
    if (button(ui, E, "back", {r.x + 14, r.y + r.h - 34, 90, 24}, E.win(0x84, "Back"))) openMain();
    const int cw[4] = {120, 130, 130, 130};
    const uint32_t hid[4] = {0x1B3, 0x1B4, 0x1B5, 0x1B6};
    const char* hfb[4] = {"Maturity", "Total", "Owed", "Paid"};
    int cx[4]; cx[0] = x0; for (int i = 1; i < 4; i++) cx[i] = cx[i - 1] + cw[i - 1];
    for (int i = 0; i < 4; i++) ui.text(cx[i], y0, E.win(hid[i], hfb[i]), "title12");
    ui.fill({x0, y0 + lh + 4, 520, 1}, ui.panelDark);
    int y = y0 + lh + 8;
    for (auto& bd : b.bonds) {
      ui.text(cx[0], y, std::to_string(bd.month) + "/" + std::to_string(bd.maturityYear));
      rightText(ui, cx[1] + cw[1] - 14, y, E.money(bd.totalRepayable));
      rightText(ui, cx[2] + cw[2] - 14, y, E.money(bd.remaining));
      rightText(ui, cx[3] + cw[3] - 14, y, E.money(bd.paid));
      y += lh;
      if (y > r.y + 250) break;
    }
    int fy = r.y + 270;
    labelValue(ui, x0, fy, 320, E.win(0x1B7, "Borrowed"), E.money(b.totalPrincipal));
    labelValue(ui, x0, fy + lh, 320, E.win(0x1B8, "Borrowing limit"), E.money(int64_t(b.cfg.maxBondAmt) * 1000 * b.cfg.maxBonds));
    ui.text(x0, fy + 2 * lh + 8, E.win(0x23B, "New loan amount"));
    int maxK = std::max(5, b.cfg.maxBondAmt);
    newLoanThousands = std::max(5, std::min(maxK, newLoanThousands / 5 * 5));
    stepper(ui, E, "amount", {x0 + 150, fy + 2 * lh + 4, 300, 22}, newLoanThousands, 5, maxK, 5, "K");
    if (button(ui, E, "accept", {x0, fy + 2 * lh + 34, 140, 24}, E.win(0x27F, "Accept loan"))) {
      int64_t amt = int64_t(newLoanThousands) * 1000;
      bool ok = b.issueBond(amt, v.in.month, v.in.year);
      R.add(Act::IssueBond, 0, amt, ok);
      loanMessage = ok ? "" : "Loan refused";
    }
    ui.text(x0 + 150, fy + 2 * lh + 38, loanMessage, "main10", rgb(192, 0, 0));
    ui.text(x0, fy + 2 * lh + 66, subst(E.win(0x23D, "Maximum number of loans: %s"), {std::to_string(b.cfg.maxBonds)}) +
                                      "  (" + std::to_string(b.bondCount()) + ")");
    if (button(ui, E, "advisor", {r.x + r.w - 214, r.y + r.h - 34, 200, 24}, E.win(0x1B9, "Consult financial advisor"))) R.add(Act::OpenAdvisor, kAdvBudget);
    return R;
  }

  // Indicators
  if (button(ui, E, "back", {r.x + 14, r.y + r.h - 34, 90, 24}, E.win(0x84, "Back"))) openMain();
  struct Ind { uint32_t id; const char* fb; std::string val; float frac; };
  const IndicatorData& d = v.ind;
  Ind inds[] = {{0x161, "City size", std::to_string(d.citySize), d.citySize / 100.f},
                {0x165, "National population", groupDigits(d.nationalPop), -1},
                {0x163, "Commerce", std::to_string(d.commerce), d.commerce / 100.f},
                {0x164, "Industry", std::to_string(d.industry), d.industry / 100.f},
                {0x168, "Unemployment", std::to_string(d.unemployment) + "%", d.unemployment / 100.f},
                {0x167, "Pollution", std::to_string(d.pollution), d.pollution / 100.f}};
  int y = y0 + 8;
  for (auto& i : inds) {
    ui.text(x0, y, E.win(i.id, i.fb));
    rightText(ui, x0 + 300, y, i.val);
    if (i.frac >= 0) bar(ui, {x0 + 320, y, 200, lh - 3}, i.frac, rgb(0x31, 0x36, 0x7C));
    y += lh + 10;
  }
  return R;
}

// ================================================================ Ordinances
std::string ordinanceName(const WinEnv& E, const OrdinanceDef& d) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "Ordinance %08X", d.id);
  return E.text(kTblOrdinance, d.nameStr, buf);
}

WindowResult OrdinancesWindow::draw(Ui& ui, const WinEnv& E, Ordinances& o, const OrdinanceMoneyInput& money,
                                    uint32_t advisorFilter, bool framed, Rect area) {
  WindowResult R;
  Rect r = area;
  if (framed) {
    r = centered(ui, 520, 400);
    if (frame(ui, E, r, E.win(0x76, "Ordinances"))) { R.close = true; R.add(Act::Close); }
    r = {r.x + 10, r.y + 30, r.w - 20, r.h - 40};
  }
  int lh = lineHeight(ui);
  struct Row { uint32_t id; std::string name; };
  std::vector<Row> rows;
  for (auto& kv : o.catalogue()) {
    if (!o.available(kv.first)) continue;
    if (advisorFilter && kv.second.category != advisorFilter) continue;
    rows.push_back({kv.first, ordinanceName(E, kv.second)});
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.name != b.name ? a.name < b.name : a.id < b.id; });
  ui.text(r.x + 4, r.y, E.win(0x1C1, "Available ordinances"), "title12");
  rightText(ui, r.x + r.w - 20, r.y + 2, E.win(0x1C3, "Monthly"));
  int listY = r.y + lh + 8, rowH = lh + 6;
  int visible = std::max(1, (r.h - (listY - r.y) - lh - 12) / rowH);
  scrollBar(ui, E, {r.x, listY, r.w, visible * rowH}, int(rows.size()), visible, scroll);
  for (int i = 0; i < visible && scroll + i < int(rows.size()); i++) {
    const Row& row = rows[size_t(scroll + i)];
    int y = listY + i * rowH;
    bool en = o.enacted(row.id);
    if (check(ui, E, "ord." + std::to_string(row.id), {r.x + 4, y, r.w - 150, rowH}, row.name, en)) {
      bool ok = en ? o.enact(row.id) : o.repeal(row.id);
      if (ok) R.add(Act::SetOrdinance, row.id, en);
    }
    int64_t val = o.monthlyValue(row.id, money);
    rightText(ui, r.x + r.w - 24, y + 3, val == 0 ? "-" : E.money(val), "main10", val < 0 ? rgb(160, 0, 0) : 0);
  }
  auto m = o.monthlyMoney(money);
  int64_t net = m.income - m.cost;
  ui.fill({r.x, r.y + r.h - lh - 8, r.w - 16, 1}, ui.panelDark);
  ui.text(r.x + 4, r.y + r.h - lh - 4, E.win(0x1C4, "Monthly total"));
  rightText(ui, r.x + r.w - 24, r.y + r.h - lh - 4, E.money(net), "main10", net < 0 ? rgb(160, 0, 0) : 0);
  return R;
}

// ================================================================ Business deals
WindowResult BusinessDealsWindow::draw(Ui& ui, const WinEnv& E, BusinessDeals& d, const std::string& briefing) {
  WindowResult R;
  int lh = lineHeight(ui);
  Rect r = centered(ui, 500, 380);
  if (frame(ui, E, r, E.win(0x1FE, "Business deals"))) { R.close = true; R.add(Act::Close); }
  int x0 = r.x + 14, y = r.y + 34;
  if (d.pendingOffer && *d.pendingOffer < d.defs.size()) {
    const auto& def = d.defs[*d.pendingOffer];
    ui.text(x0, y, "Offer: " + def.name + " (" + E.money(def.value) + "/mo)", "main10", rgb(0, 120, 0));
    if (button(ui, E, "accept", {x0 + 300, y - 4, 80, 22}, E.app(0xC0, "Accept"))) { size_t i = *d.pendingOffer; if (d.accept()) R.add(Act::AcceptDeal, uint32_t(i)); }
    if (button(ui, E, "decline", {x0 + 386, y - 4, 80, 22}, E.app(0xC2, "Decline"))) { size_t i = *d.pendingOffer; d.decline(); R.add(Act::DeclineDeal, uint32_t(i)); }
    y += lh + 12;
  }
  ui.text(x0, y, E.win(0x1D0, "Current deals"), "title12");
  rightText(ui, x0 + 420, y + 2, E.win(0x13E, "Monthly"));
  y += lh + 8;
  int rowH = lh + 6;
  std::vector<uint32_t> types = d.inForce;
  int visible = 6;
  wui::scrollBar(ui, E, {x0, y, 440, visible * rowH}, int(types.size()), visible, scroll);
  if (types.empty()) ui.text(x0 + 4, y, "-");
  for (int i = 0; i < visible && scroll + i < int(types.size()); i++) {
    uint32_t t = types[size_t(scroll + i)];
    std::string name = "Deal " + std::to_string(t);
    int val = 0;
    for (auto& def : d.defs) if (def.buildingType == t) { name = def.name.empty() ? name : def.name; val = def.value; break; }
    ui.text(x0 + 4, y + 3, name);
    rightText(ui, x0 + 340, y + 3, E.money(val));
    if (button(ui, E, "cancel." + std::to_string(t), {x0 + 352, y, 70, rowH - 2}, E.app(0xB6, "Cancel"))) {
      d.remove(t);
      R.add(Act::CancelDeal, t, 0, 0, "business");
    }
    y += rowH;
  }
  int fy = r.y + 34 + 8 * (lh + 8) + 30;
  ui.fill({x0, fy - 6, 440, 1}, ui.panelDark);
  ui.text(x0, fy, E.win(0x239, "Total monthly income"));
  rightText(ui, x0 + 340, fy, E.money(d.monthlyIncome()));
  ui.text(x0, fy + lh + 6, E.win(0x23A, "Finance briefing"), "title12");
  paragraph(ui, {x0, fy + 2 * lh + 10, 460, r.y + r.h - (fy + 2 * lh + 10) - 44}, briefing);
  if (button(ui, E, "ok", {r.x + r.w - 104, r.y + r.h - 34, 90, 24}, E.app(0xB4, "OK"))) { R.close = true; R.add(Act::Close); }
  return R;
}

// ================================================================ Neighbours
std::string dealKindName(const WinEnv& E, int type) {
  static const uint32_t ids[kDealTypes] = {0x143, 0x140, 0x144, 0x141, 0x142, 0x145};
  static const char* fb[kDealTypes] = {"Sells you power", "Buys your power", "Sells you water", "Buys your water", "Takes your garbage", "Gives you garbage"};
  if (type < 0 || type >= kDealTypes) return "";
  return E.win(ids[type], fb[type]);
}

std::string dealRateText(const WinEnv& E, const Deal& d) {
  int g = d.dealType < 2 ? 0 : d.dealType < 4 ? 1 : 2;
  static const uint32_t ids[3] = {0x298, 0x296, 0x297};
  static const char* fb[3] = {"%s per 1000 MW-h", "%s per 1000 cubic m", "%s per 100 tons"};
  return subst(E.win(ids[g], fb[g]), {E.money(d.rate)});
}

PetitionerView makeDealOfferView(const WinEnv& E, const Deal& d, size_t pendingIndex) {
  PetitionerView v;
  v.isDeal = true;
  v.pendingIndex = pendingIndex;
  v.name = d.neighbourName;
  v.heading = dealKindName(E, d.dealType);
  v.message = d.text.empty() ? v.heading + ": " + std::to_string(d.quantity) + " units, " + dealRateText(E, d) + " (" + E.money(d.monthlyAmount) + "/month)" : d.text;
  v.button1 = E.win(0x78, "Accept deal");
  v.button2 = E.win(0x2DE, "Decline");
  v.tag = "deal";
  return v;
}

WindowResult NeighboursWindow::draw(Ui& ui, const WinEnv& E, NeighbourDeals& n) {
  WindowResult R;
  int lh = lineHeight(ui);
  Rect r = centered(ui, 600, 400);
  if (frame(ui, E, r, E.win(0x117, "Neighbors"))) { R.close = true; R.add(Act::Close); }
  // Map area with four compass boxes.
  Rect m{r.x + 14, r.y + 34, 280, 300};
  ui.fill(m, rgb(0xB0, 0xC4, 0xA0)); ui.frame(m, false);
  struct Pos { int idx; Rect box; uint32_t id; const char* fb; };
  Pos pos[4] = {{0, {m.x + 70, m.y + 8, 140, 40}, 0x11D, "North"}, {1, {m.x + 146, m.y + 130, 128, 40}, 0x11F, "East"},
                {2, {m.x + 70, m.y + 252, 140, 40}, 0x120, "South"}, {3, {m.x + 6, m.y + 130, 128, 40}, 0x11E, "West"}};
  for (auto& p : pos) {
    const Neighbour& nb = n.neighbours[p.idx];
    std::string lbl = E.win(p.id, p.fb);
    std::string nm = nb.exists ? (nb.cityName.empty() ? "Neighbour " + std::to_string(p.idx + 1) : nb.cityName) : "";
    if (button(ui, E, "nb." + std::to_string(p.idx), p.box, "", nb.exists, selected == p.idx)) selected = p.idx;
    uint32_t tc = !nb.exists ? rgb(0x90, 0x94, 0xA0) : selected == p.idx ? rgb(255, 255, 255) : 0;
    ui.textCentered({p.box.x, p.box.y + 4, p.box.w, lh}, lbl, "main10", tc);
    if (!nm.empty()) ui.textCentered({p.box.x, p.box.y + 4 + lh, p.box.w, lh}, nm, "main10", tc);
  }
  ui.textCentered({m.x + 100, m.y + 70, 80, 30}, "City");
  int x0 = r.x + 310, y = r.y + 40;
  if (selected >= 0 && selected < kNeighbours) {
    const Neighbour& nb = n.neighbours[selected];
    labelValue(ui, x0, y, 270, E.win(0x29A, "Neighbor"), nb.cityName.empty() ? "Neighbour " + std::to_string(selected + 1) : nb.cityName);
    labelValue(ui, x0, y + lh, 270, E.win(0x165, "National population"), groupDigits(nb.population));
    y += 3 * lh;
    ui.text(x0, y, E.win(0x13D, "Current deals"), "title12");
    y += lh + 6;
    bool any = false;
    int64_t total = 0;
    for (auto& d : n.active) {
      if (d.neighbourIndex != selected || d.terminated()) continue;
      any = true;
      ui.text(x0, y, dealKindName(E, d.dealType));
      y += lh;
      ui.text(x0 + 8, y, E.win(0x29B, "Rate") + ": " + dealRateText(E, d));
      y += lh;
      ui.text(x0 + 8, y, E.win(0x13E, "Monthly") + ": " + E.money(d.monthlyAmount));
      total += d.monthlyAmount;
      if (button(ui, E, "cancel." + std::to_string(d.dealType), {x0 + 190, y - 2, 78, lh + 2}, E.app(0xB6, "Cancel"))) {
        int64_t pen = n.cancel(d.dealType);
        R.add(Act::CancelDeal, d.dealType, pen);
        break;   // active list changed
      }
      y += lh + 6;
    }
    if (!any) { ui.text(x0 + 8, y, "-"); y += lh; }
    size_t pend = 0;
    for (auto& d : n.pending) if (d.neighbourIndex == selected) pend++;
    if (pend) ui.text(x0, y + 4, std::to_string(pend) + " offer(s) pending", "main10", rgb(0, 120, 0));
    if (button(ui, E, "discuss", {x0, r.y + r.h - 70, 160, 24}, E.win(0x29C, "Discuss deal"), nb.exists)) R.add(Act::DiscussDeal, uint32_t(selected));
    (void)total;
  } else {
    ui.text(x0, y, "-");
  }
  if (button(ui, E, "ok", {r.x + r.w - 104, r.y + r.h - 34, 90, 24}, E.app(0xB4, "OK"))) { R.close = true; R.add(Act::Close); }
  return R;
}

}  // namespace m3k
