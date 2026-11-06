// Budget, ordinances, business deals, neighbours (spec 09 sections 6.1, 6.3, 6.4, 6.7).
#include <algorithm>
#include <cmath>
#include <iterator>
#include "core/skin.h"
#include "core/windows.h"

namespace m3k {

using namespace wui;

// ================================================================ Budget window
namespace {

struct DeptRow { uint32_t id; uint32_t label; const char* fb; const char* key; uint32_t advisor; };
// Row order of the original window [OBSERVED, spec 19 s6.1]: Education, Public Health, Fire, Roads, Police, Mass Transit.
const DeptRow kDepts[] = {
  {dept::Education, 0x96, "Education", "education", kAdvDemographics},
  {dept::Health, 0x95, "Public health", "health", kAdvDemographics},
  {dept::Fire, 0x94, "Fire", "fire", kAdvSafety},
  {dept::Roads, 0, "Roads", "roads", kAdvTransport},
  {dept::Police, 0x92, "Police", "police", kAdvSafety},
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

// ---- main page (Expenditures / Income tabs) and Loans sub-dialog, laid out after the original 2.0 dialogs
// (box 500x480 and 360x400; measured coordinates are box-relative). Art comes from the dialog group 42DEC7D2.
namespace {

const uint32_t kInkLabel = rgb(0x18, 0x20, 0x6B), kInkHead = rgb(0x7B, 0x79, 0xB5), kInkDark = rgb(0x31, 0x34, 0x7B);
const uint32_t kBlack = rgb(0, 0, 0), kRed = rgb(0xFF, 0, 0), kWhite = rgb(255, 255, 255);
const char* kMonthName[12] = {"January", "February", "March", "April", "May", "June",
                              "July", "August", "September", "October", "November", "December"};

const Image* dlgArt(const Ui& ui, uint32_t inst) { return ui.skin ? ui.skin->image(skin_group::Gonzo, inst) : nullptr; }

// 4-cell image button (0 disabled, 1 normal, 2 pressed, 3 hover) with a plain text fallback.
bool artButton(Ui& ui, const WinEnv& E, const std::string& name, const Image* strip, Rect r, const std::string& fb,
               bool enabled = true) {
  E.spot(name, r);
  bool c = false;
  if (!(ui.skin && strip && ui.skin->imageButtonCells(ui, strip, 4, r, "", false, enabled, c))) c = ui.button(r, fb, enabled);
  return c && enabled;
}

// Up/down spinner (42DEC7D2:0B, 4 cells 12x18; the pressed cell shows both halves pressed, only the held half is used).
// Returns +1 / -1 when the upper / lower half is clicked. Spots: name.plus (upper half) and name.minus (lower half).
int spinner(Ui& ui, const WinEnv& E, const std::string& name, int x, int y, int gap = 0) {
  Rect up{x, y, 12, 9}, dn{x, y + 9 + gap, 12, 9};
  E.spot(name + ".plus", up);
  E.spot(name + ".minus", dn);
  const Image* s = dlgArt(ui, 0x0B);
  if (s && !s->empty() && s->w >= 48) {
    if (gap > 0) { ui.blitRect(*s, x, y, CELL_NORMAL * 12, 0, 12, 9); ui.blitRect(*s, dn.x, dn.y, CELL_NORMAL * 12, 9, 12, 9); }   // the two halves drawn apart
    else Skin::drawCell(ui, s, 4, CELL_NORMAL, x, y);
    for (int h = 0; h < 2; h++) {
      Rect hr = h ? dn : up;
      if (ui.hover(hr)) ui.blitRect(*s, hr.x, hr.y, (ui.in.down ? CELL_PRESSED : CELL_HOVER) * 12, h * 9, 12, 9);
    }
  } else {
    ui.button(up, "");
    ui.button(dn, "");
  }
  return ui.clicked(up) ? 1 : ui.clicked(dn) ? -1 : 0;
}

// Union of blue bands: all rings first, then all fills, so overlapping bands merge into one shape.
void bands(Ui& ui, Rect box, std::initializer_list<Rect> rs) {
  for (Rect b : rs) ui.fill({box.x + b.x, box.y + b.y, b.w, b.h}, rgb(0xAD, 0xB6, 0xFF));
  for (Rect b : rs) ui.fill({box.x + b.x + 1, box.y + b.y + 1, b.w - 2, b.h - 2}, rgb(0x7B, 0x92, 0xFF));
}

// Sunken field with white interior.
void whiteField(Ui& ui, Rect f) {
  ui.fill({f.x + 1, f.y + 1, f.w - 2, f.h - 2}, kWhite);
  sunkenLines(ui, f);
}

// Two-line heading split before its last word, centred on `cx`.
void splitHeading(Ui& ui, int cx, int y, const std::string& s, const char* font, uint32_t col, bool wordPerLine = false) {
  std::vector<std::string> parts;
  if (wordPerLine) {
    size_t p = 0;
    while (p < s.size()) { size_t e = s.find(' ', p); if (e == std::string::npos) e = s.size(); if (e > p) parts.push_back(s.substr(p, e - p)); p = e + 1; }
  } else {
    size_t sp = s.rfind(' ');
    if (sp == std::string::npos) parts.push_back(s);
    else { parts.push_back(s.substr(0, sp)); parts.push_back(s.substr(sp + 1)); }
  }
  for (size_t i = 0; i < parts.size(); i++) {
    // The original centres these three lines 2 px off the ink-width centre (English strings, observed): "Expense" and "Estimate" 2 px right, "Meet" 2 px left.
    const int adj = (parts[i] == "Expense" || parts[i] == "Estimate" || parts[i] == "Mo/Yr") ? 2 : parts[i] == "Meet" ? -2 : 0;
    ui.text(cx - ui.textWidth(parts[i], font) / 2 + adj, y + int(i) * 14, parts[i], font, col);
  }
}

uint32_t moneyCol(int64_t v) { return v < 0 ? kRed : kBlack; }

// Main-page row: line key (lines()), label id/fallback, adjust-column icon (0 none), "?" button.
struct MainRow { const char* key; uint32_t label; const char* fb; uint32_t icon; const char* link; bool details; };
const MainRow kExpRows[] = {
  {"ord.exp", 0x8F, "Ordinances", 0x09, "link.ord", false},
  {"deal.exp", 0x90, "Neighbor Deals", 0x08, "link.deals", true},
  {"loans", 0x236, "Loans", 0x13, "link.loans", true},
  {nullptr, 0, "", 0, nullptr, false},
  {"education", 0x96, "Education", 0, nullptr, true},
  {"health", 0x95, "Public Health", 0, nullptr, true},
  {"fire", 0x94, "Fire", 0, nullptr, true},
  {"roads", 0x1AD, "Roads", 0, nullptr, true},
  {"police", 0x92, "Police", 0, nullptr, true},
  {"transit", 0x27E, "Mass Transit", 0, nullptr, true},
};
const MainRow kIncRows[] = {
  {"ord.inc", 0x8F, "Ordinances", 0x09, "link.ord", false},
  {"deal.inc", 0x90, "Neighbor Deals", 0x08, "link.deals", true},
  {"biz", 0xA2, "Business Deals", 0x14, "link.biz", true},
  {"fares", 0x288, "Transit Fares", 0, nullptr, false},
  {"relief", 0x1AF, "Disaster Relief", 0, nullptr, false},
  {nullptr, 0, "", 0, nullptr, false},
  {"tax.r", 0xA3, "Residential Tax Rate", 0, nullptr, true},
  {"tax.c", 0xA4, "Commercial Tax Rate", 0, nullptr, true},
  {"tax.i", 0xA5, "Industrial Tax Rate", 0, nullptr, true},
};

// Funding slider geometry: knob 18x15 travels x 150..247 for 0..kMaxFundingUi (100% sits on the marker bar).
constexpr int kKnobX0 = 149, kKnobTravel = 97;   // knob travel starts 2 px left of the first measurement (observed at 800 x 600)

}  // namespace

void BudgetWindow::drawMainPage(Ui& ui, const WinEnv& E, Budget& b, const BudgetView& v, Rect r, WindowResult& R) {
  const char* F = "system9";
  auto at = [&](int x, int y, int w, int h) { return Rect{r.x + x, r.y + y, w, h}; };
  auto L = lines(E, b, v);
  auto line = [&](const std::string& k) -> const Line* { for (auto& l : L) if (l.key == k) return &l; return nullptr; };
  int64_t incY = 0, incE = 0, expY = 0, expE = 0;
  for (auto& l : L) { if (l.income) { incY += l.ytd; incE += l.est; } else { expY += l.ytd; expE += l.est; } }

  ui.panel(r, E.win(0x89, "Budget"));
  // Title-bar page tabs (42DEC7D2:01, 4 cells 54x31, stretched; active page = pressed cell).
  {
    const Image* tab = dlgArt(ui, 0x01);
    struct Tab { const char* name; Rect rc; uint32_t id; const char* fb; bool income; };
    Tab tabs[2] = {{"tab.exp", at(176, 4, 110, 31), 0x86, "Expenditures", false}, {"tab.inc", at(294, 4, 74, 31), 0x87, "Income", true}};
    for (auto& t : tabs) {
      E.spot(t.name, t.rc);
      bool active = incomeTab == t.income;
      std::string cap = E.win(t.id, t.fb);
      if (ui.skin && tab && ui.skin->drawButton3(ui, tab, 4, active ? CELL_PRESSED : CELL_NORMAL, t.rc))
        ui.text(t.rc.x + 15, t.rc.y + 6, cap, "title9", kInkDark);   // caption 15 px in, same on both states
      else ui.button(t.rc, cap, true, active, "title9");
      if (ui.clicked(t.rc)) incomeTab = t.income;
    }
  }
  bands(ui, r, {{4, 34, 8, 442}, {4, 75, 489, 8}, {144, 75, 128, 269}, {432, 75, 48, 269}, {4, 336, 489, 8},
                {4, 446, 64, 30}, {434, 441, 59, 34}});
  // Zebra rows of the label list.
  for (int j = 0; j < 5; j++) ui.fill(at(12, 106 + 46 * j, 131, 22), kWhite);

  // Header: date box (doubles as the link to the city indicators page), column headings.
  Rect dateBox = at(16, 39, 127, 35);
  sunkenLines(ui, dateBox);
  E.spot("indicators", dateBox);
  if (ui.clicked(dateBox)) page = Page::Indicators;
  int month = std::max(1, std::min(12, v.in.month));
  ui.text(r.x + 20, r.y + 43, subst(E.win(0x8A, "%s Budget"), {std::to_string(v.in.year)}), F, kInkHead);
  ui.text(r.x + 20, r.y + 57, std::string(kMonthName[month - 1]) + " " + std::to_string(v.in.year), F, kInkHead);
  ui.text(r.x + 192, r.y + 43, E.win(0x8B, "Adjust:"), F, kInkHead);
  splitHeading(ui, r.x + 310, r.y + 43, incomeTab ? E.win(0x99, "Year to Date Income") : E.win(0x8C, "Yr. To Date Expense"), F, kInkHead);
  splitHeading(ui, r.x + 390, r.y + 43, E.win(0x8D, "Year End Estimate"), F, kInkHead);
  splitHeading(ui, r.x + 456, r.y + 43, E.win(0x8E, "Meet Advisor:"), F, kInkHead);

  // Rows.
  const MainRow* rows = incomeTab ? kIncRows : kExpRows;
  int nRows = incomeTab ? int(std::size(kIncRows)) : int(std::size(kExpRows));
  if (!incomeTab) ui.fillAlpha(at(234, 177, 8, 139), kWhite, 0.33f);   // 100% funding marker behind the sliders
  for (int k = 0; k < nRows; k++) {
    const MainRow& row = rows[k];
    if (!row.key) continue;
    int fy = 87 + 23 * k;
    std::string key = row.key;
    ui.text(r.x + 18, r.y + fy + 2, E.win(row.label, row.fb), F, kInkLabel);
    if (const Line* l = line(key)) {
      int64_t ya = incomeTab ? l->ytd : -l->ytd, ye = incomeTab ? l->est : -l->est;
      // Value boxes [OBSERVED, Berlin, top rows 119 144 165 -- 212 235 258 281 304 327]: the first row (ord.exp) and the Loans row keep 18 rows; the
      // second row (deal.exp) starts 2 px lower and ends 1 px lower; the department rows are 17 rows tall (top 1 px lower).
      int vt = 0, vb = 0;
      if (!incomeTab && key != "ord.exp" && key != "loans") { vt = key == "deal.exp" ? 2 : 1; vb = key == "deal.exp" ? 1 : 0; }
      whiteField(ui, at(275, fy + vt, 76, 18 - vt + vb));
      whiteField(ui, at(353, fy + vt, 76, 18 - vt + vb));
      rightText(ui, r.x + 349, r.y + fy + 2, E.money(ya), F, moneyCol(ya));
      rightText(ui, r.x + 429, r.y + fy + 2, E.money(ye), F, moneyCol(ye));
    }
    if (row.icon && artButton(ui, E, row.link, dlgArt(ui, row.icon), at(190, fy - 2, 36, 24), E.win(row.label, row.fb))) {
      if (key == "ord.exp" || key == "ord.inc") R.add(Act::OpenOrdinances);
      else if (key == "deal.exp" || key == "deal.inc") R.add(Act::OpenNeighbours);
      else if (key == "biz") R.add(Act::OpenBusinessDeals);
      else if (key == "loans") page = Page::Loans;
    }
    if (row.details && artButton(ui, E, "details:" + key, dlgArt(ui, 0x19), at(438, fy - 2, 36, 24), "?")) {
      if (key == "deal.exp" || key == "deal.inc") R.add(Act::OpenNeighbours);
      else if (key == "biz") R.add(Act::OpenBusinessDeals);
      else if (key == "loans") page = Page::Loans;
      else if (key == "tax.r") openTax(0);
      else if (key == "tax.c") openTax(1);
      else if (key == "tax.i") openTax(2);
      else for (auto& dr : kDepts) if (key == dr.key) openDept(dr.id);
    }
    // Tax-rate spinner + value box (income page).
    for (int g = 0; g < 3; g++) {
      if (key != kTaxKey[g]) continue;
      int d = spinner(ui, E, kTaxKey[g], r.x + 190, r.y + fy - 1, 2);   // up half 2 px higher, a 2 row gap, down half unchanged (observed)
      int rate = std::max(0, std::min(kMaxTaxRate, b.tax[g].rate + d));
      if (d && rate != b.tax[g].rate) { b.setTaxRate(g, rate); R.add(Act::SetTax, uint32_t(g), rate); }
      whiteField(ui, at(206, fy - 2, 20, 23));
      rightText(ui, r.x + 222, r.y + fy + 2, std::to_string(b.tax[g].rate), F, kInkDark);
    }
    // Funding slider (expenditures page): track + knob 42DEC7D2:1C.
    for (auto& dr : kDepts) {
      if (key != dr.key) continue;
      Department* dp = b.dept(dr.id);
      if (!dp) continue;
      int ty = r.y + fy + 8;
      // Track bevel, 116 x 3 [OBSERVED pixel by pixel]: top (24,20,74) / (57,56,123) / (156,154,231); middle (57,56,123) / (8,12,24) / white; bottom (156,154,231) / white.
      ui.fill({r.x + 150, ty, 116, 1}, rgb(57, 56, 123)); ui.fill({r.x + 150, ty, 1, 1}, rgb(24, 20, 74)); ui.fill({r.x + 265, ty, 1, 1}, rgb(156, 154, 231));
      ui.fill({r.x + 150, ty + 1, 116, 1}, rgb(8, 12, 24)); ui.fill({r.x + 150, ty + 1, 1, 1}, rgb(57, 56, 123)); ui.fill({r.x + 265, ty + 1, 1, 1}, rgb(255, 255, 255));
      ui.fill({r.x + 150, ty + 2, 116, 1}, rgb(255, 255, 255)); ui.fill({r.x + 150, ty + 2, 1, 1}, rgb(156, 154, 231));
      if (!incomeTab) {   // the 100% marker shows through the track: two columns blended over the bevel
        ui.fill({r.x + 235, ty, 5, 1}, rgb(107, 105, 165)); ui.fill({r.x + 235, ty + 1, 5, 1}, rgb(41, 36, 74));
      }
      Rect track = at(kKnobX0, fy + 2, kKnobTravel + 18, 15);
      E.spot("fund." + key, track);
      int id = ui.nextId();
      if (ui.in.pressed && ui.hover(track)) ui.activeId = id;
      if (ui.activeId == id && !ui.in.down) ui.activeId = 0;
      int val = std::max(0, std::min(kMaxFundingUi, int(dp->funding)));
      if (ui.activeId == id && ui.in.down) {
        int nv = int(std::lround(double(ui.in.mx - (track.x + 9)) * kMaxFundingUi / kKnobTravel));
        nv = std::max(0, std::min(kMaxFundingUi, nv));
        if (nv != dp->funding) { b.setFunding(dr.id, nv); R.add(Act::SetFunding, dr.id, nv); val = nv; }
      }
      int kx = track.x + val * kKnobTravel / kMaxFundingUi;
      const Image* knob = dlgArt(ui, 0x1C);
      int cell = ui.activeId == id ? CELL_PRESSED : ui.hover({kx, track.y, 18, 15}) ? CELL_HOVER : CELL_NORMAL;
      if (!(knob && !knob->empty() && Skin::drawCell(ui, knob, 4, cell, kx, track.y))) ui.fill({kx, track.y, 18, 15}, kInkDark);
    }
  }

  // Summary block.
  int64_t ytdExp = -expY, estExp = -expE, flowY = incY - expY, flowE = incE - expE;
  int64_t endFunds = b.funds + flowE;   // funds settle at year end (spec 19 s7), so add the whole-year estimate
  for (int y : {346, 378, 410}) whiteField(ui, at(275, y, 154, 30));
  whiteField(ui, at(275, 441, 154, 16));
  whiteField(ui, at(275, 459, 154, 16));
  struct Sum { int y; uint32_t id; const char* fb; uint32_t lc; int64_t val; int right; };
  Sum sums[] = {{346, 0x99, "Year to Date Income", kBlack, incY, 348}, {360, 0x9A, "Estimated Income", kBlack, incE, 428},
                {378, 0x9B, "Year to Date Expense", kRed, ytdExp, 348}, {392, 0x9C, "Estimated Expense", kRed, estExp, 428},
                {410, 0x9D, "Year to Date Cash Flow", kInkLabel, flowY, 348}, {424, 0x9E, "Estimated Cash Flow", kInkLabel, flowE, 428},
                {442, 0x9F, "Current Funds", kInkLabel, b.funds, 428}, {460, 0xA0, "End of Year Funds", kInkLabel, endFunds, 428}};
  for (auto& s : sums) {
    rightText(ui, r.x + 269, r.y + s.y, E.win(s.id, s.fb), F, s.lc);
    rightText(ui, r.x + s.right + 1, r.y + s.y, E.money(s.val), F, moneyCol(s.val));
  }
  {
    std::string adv = E.win(0x238, "Meet Financial Advisor:");
    size_t sp = adv.rfind(' ');
    ui.text(r.x + 17, r.y + 410, sp == std::string::npos ? adv : adv.substr(0, sp), "main9", kInkHead);
    if (sp != std::string::npos) ui.text(r.x + 17, r.y + 424, adv.substr(sp + 1), "main9", kInkHead);
  }
  if (artButton(ui, E, "advisor", dlgArt(ui, 0x19), at(31, 450, 36, 24), "?")) R.add(Act::OpenAdvisor, kAdvBudget);
  const Image* done = ui.skin ? ui.skin->image(skin_group::Sys, 0xA2DEFD9A) : nullptr;
  if (artButton(ui, E, "ok", done, at(450, 442, 42, 32), E.app(0xB4, "OK")) || ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }
}

void BudgetWindow::drawLoans(Ui& ui, const WinEnv& E, Budget& b, const BudgetView& v, Rect r, WindowResult& R) {
  const char* F = "main9";
  auto at = [&](int x, int y, int w, int h) { return Rect{r.x + x, r.y + y, w, h}; };
  ui.panel(r, E.win(0x236, "Loans"));
  bands(ui, r, {{28, 273, 40, 28}, {92, 312, 176, 82}, {310, 358, 46, 36}});
  // Loan table: 5 rows, white zebra on rows 0, 2, 4.
  sunkenLines(ui, at(6, 73, 348, 181));
  static const int kRowY[5] = {74, 109, 146, 181, 218};   // stripe tops: heights 35 / 37 / 35 / 37 / 35 [OBSERVED]
  auto rowY = [](int i) { return kRowY[i]; };
  for (int i = 0; i < 5; i += 2) ui.fill(at(7, rowY(i), 346, 35), kWhite);
  const int colC[4] = {46, 136, 224, 312};
  splitHeading(ui, r.x + colC[0], r.y + 41, E.win(0x1B3, "Maturity Date Mo/Yr"), F, kInkLabel);
  const uint32_t hid[3] = {0x1B4, 0x1B5, 0x1B6};
  const char* hfb[3] = {"Total", "Amount Owed", "Amount Paid"};
  for (int i = 0; i < 3; i++) {
    std::string h = E.win(hid[i], hfb[i]);
    ui.text(r.x + colC[i + 1] - ui.textWidth(h, F) / 2, r.y + 41, h, F, kInkLabel);
  }
  int i = 0;
  for (auto& bd : b.bonds) {
    if (i >= 5) break;
    int y = r.y + rowY(i) + 11;
    std::string cells[4] = {std::to_string(bd.month) + "/" + std::to_string(bd.maturityYear), E.money(bd.totalRepayable),
                            E.money(bd.remaining), E.money(bd.paid)};
    for (int c = 0; c < 4; c++) ui.text(r.x + colC[c] - ui.textWidth(cells[c], F) / 2, y, cells[c], F, kBlack);
    i++;
  }
  rightText(ui, r.x + 179, r.y + 259, E.win(0x1B7, "Borrowed:"), F, kInkLabel);
  rightText(ui, r.x + 267, r.y + 259, E.money(b.totalPrincipal), F, kBlack);
  rightText(ui, r.x + 179, r.y + 277, E.win(0x1B8, "Borrowing Limit:"), F, kInkLabel);
  rightText(ui, r.x + 267, r.y + 277, E.money(int64_t(b.cfg.maxBondAmt) * 1000 * b.cfg.maxBonds), F, kInkLabel);
  if (artButton(ui, E, "advisor", dlgArt(ui, 0x19), at(30, 275, 36, 24), "?")) R.add(Act::OpenAdvisor, kAdvBudget);
  splitHeading(ui, r.x + 46, r.y + 301, E.win(0x1B9, "Consult Financial Advisor"), F, kInkLabel, true);
  // New loan.
  std::string nl = E.win(0x23B, "New Loan:");
  ui.text(r.x + 180 - ui.textWidth(nl, F) / 2, r.y + 316, nl, F, kInkLabel);
  int maxK = std::max(5, b.cfg.maxBondAmt);
  newLoanThousands = std::max(5, std::min(maxK, newLoanThousands / 5 * 5));
  int d = spinner(ui, E, "amount", r.x + 136, r.y + 335, 3);   // a 3 row gap between the halves
  if (d) newLoanThousands = std::max(5, std::min(maxK, newLoanThousands + 5 * d));
  whiteField(ui, at(152, 334, 72, 24));
  rightText(ui, r.x + 220, r.y + 339, std::to_string(int64_t(newLoanThousands) * 1000), "system9", kInkDark);
  Rect acc = at(128, 360, 104, 33);
  E.spot("accept", acc);
  bool clicked = false;
  std::string cap = E.win(0x27F, "Accept Loan");
  if (ui.skin && ui.skin->button3(ui, dlgArt(ui, 0x15), acc, "", true, false, clicked)) {
    int off = ui.hover(acc) && ui.in.down ? 1 : 0;
    ui.text(acc.x + 15 + off, acc.y + 7 + off, cap, "title9", kInkDark);
  } else clicked = ui.button(acc, cap, true, false, "title9");
  if (clicked) {
    int64_t amt = int64_t(newLoanThousands) * 1000;
    bool ok = b.issueBond(amt, v.in.month, v.in.year);
    R.add(Act::IssueBond, 0, amt, ok);
    loanMessage = ok ? "" : "Loan refused";
  }
  if (!loanMessage.empty()) ui.text(r.x + 274, r.y + 322, loanMessage, F, kRed);
  const Image* done = ui.skin ? ui.skin->image(skin_group::Sys, 0xA2DEFD9A) : nullptr;
  if (artButton(ui, E, "back", done, at(312, 360, 42, 32), E.app(0xB4, "OK")) || ui.in.key[K_ESC]) openMain();
}

WindowResult BudgetWindow::draw(Ui& ui, const WinEnv& E, Budget& b, const BudgetView& v) {
  WindowResult R;
  if (page == Page::Main) {
    drawMainPage(ui, E, b, v, centredInView(ui, 500, 480), R);
    return R;
  }
  if (page == Page::Loans) {
    // Loans opens over the expenditures page, which stays visible but inert.
    {
      WinEnv bg = E;
      bg.spots = nullptr;
      Input saved = ui.in;
      ui.in = Input();
      ui.in.mx = ui.in.my = -1;
      bool inc = incomeTab;
      incomeTab = false;
      WindowResult ignored;
      drawMainPage(ui, bg, b, v, centredInView(ui, 500, 480), ignored);
      incomeTab = inc;
      ui.in = saved;
    }
    drawLoans(ui, E, b, v, [&] { Rect m = viewAreaCentre(ui, 500, 480); return placedAt(ui, m.x + 70, m.y + 40, 360, 400); }(), R);   // centred on the Budget window (observed)
    return R;
  }
  int lh = lineHeight(ui);
  int month = std::max(1, std::min(12, v.in.month));
  Rect r = centered(ui, 620, 460);
  std::string title = E.win(0x89, "Budget");
  if (page == Page::Dept || page == Page::Tax) {
    std::string rowName;
    if (page == Page::Dept) { if (auto* dr = deptRow(dept)) rowName = label(E, dr->label, dr->fb); }
    else rowName = E.win(kTaxLabel[taxGroup % 3], kTaxFb[taxGroup % 3]);
    title = subst(E.win(0x8A, "%s"), {rowName});
  } else if (page == Page::Indicators) title = E.win(0x161, "City indicators");
  if (frame(ui, E, r, title)) { R.close = true; R.add(Act::Close); }
  if (ui.in.key[K_ESC]) openMain();
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
    // Approval (1D2) = city-aura history chart (spec 09 §6.1): monthly samples cityAura+127, not a tax %.
    if (v.showAuraChart) {
      ui.text(x0, ry + 8 * lh, E.win(0x1D2, "Approval rating"));
      Rect plot{x0 + 170, ry + 8 * lh, 210, lh * 3};
      ui.fill(plot, rgb(255, 255, 255));
      ui.frame(plot, false);
      // Newest sample is index 0; draw oldest→newest left→right (same as GraphsWindow).
      uint32_t lo = v.auraChart.monthly[0], hi = lo;
      for (int i = 1; i < 12; i++) {
        lo = std::min(lo, v.auraChart.monthly[i]);
        hi = std::max(hi, v.auraChart.monthly[i]);
      }
      if (hi == lo) { lo = 0; hi = std::max(v.auraChart.max, 254u); }
      auto plotLine = [&](int ax, int ay, int bx, int by, uint32_t col) {
        int dx = std::abs(bx - ax), dy = -std::abs(by - ay), sx = ax < bx ? 1 : -1, sy = ay < by ? 1 : -1, err = dx + dy;
        for (int guard = 0; guard < 4000; guard++) {
          ui.fill({ax, ay, 2, 2}, col);
          if (ax == bx && ay == by) break;
          int e2 = 2 * err;
          if (e2 >= dy) { err += dy; ax += sx; }
          if (e2 <= dx) { err += dx; ay += sy; }
        }
      };
      int px = 0, py = 0;
      for (int k = 0; k < 12; k++) {
        uint32_t s = v.auraChart.monthly[11 - k];
        int x = plot.x + 4 + k * (plot.w - 8) / 11;
        float t = float(s - lo) / float(hi - lo);
        int y = plot.y + plot.h - 4 - int(t * float(plot.h - 8));
        if (k) plotLine(px, py, x, y, rgb(0, 160, 60));
        px = x; py = y;
      }
    }
    return R;
  }

  // Indicators
  if (button(ui, E, "back", {r.x + 14, r.y + r.h - 34, 90, 24}, E.win(0x84, "Back"))) openMain();
  struct Ind { uint32_t id; const char* fb; std::string val; float frac; };
  const IndicatorData& d = v.ind;
  auto frac = [](uint32_t v, uint32_t m) { return m ? std::min(1.f, float(v) / float(m)) : 0.f; };
  Ind inds[] = {{0x161, "City size", groupDigits(d.citySize), frac(d.citySize, d.citySizeMax)},
                {0x165, "National population", groupDigits(d.nationalPop), -1},
                {0x163, "Commerce", groupDigits(d.commerce), frac(d.commerce, d.commerceMax)},
                {0x164, "Industry", groupDigits(d.industry), frac(d.industry, d.industryMax)},
                {0x168, "Unemployment", std::to_string(d.unemployment) + "%", d.unemployment / 100.f},
                {0x167, "Pollution", std::to_string(d.pollution), frac(d.pollution, d.pollutionMax)}};
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

// The Review > Ordinances window as observed on the original (spec 09 6.3, 800x600): 384 x 354 frame at (8, 8) with no close box,
// header "Ordinance" / "Current Monthly Cost", a list box with a classic scroll bar grouped by advisor (24 px band with the advisor
// title and a "?" button, then 18 px rows: check box, name, cost right-aligned and grey while not enacted), "Monthly Total" and an OK
// check-mark button at the bottom right.
WindowResult OrdinancesWindow::drawFramed(Ui& ui, const WinEnv& E, Ordinances& o, const OrdinanceMoneyInput& money, uint32_t advisorFilter) {
  WindowResult R;
  const uint32_t ink = rgb(0x31, 0x34, 0x7B), grey = rgb(132, 130, 132), band = rgb(123, 146, 255), bandEdge = rgb(173, 182, 255);
  Rect r = placedAt(ui, 8, 8, 384, 354);
  ui.panel(r, E.win(0x76, "Ordinances"));
  // All positions below are measured on the original at 800 x 600 (window at 8, 8); main9 is the 9 pt UI face (cap height 9 rows).
  ui.text(r.x + 6, r.y + 41, E.win(0x1C2, "Ordinance"), "main9", ink);
  rightText(ui, r.x + 351, r.y + 41, E.win(0x1C3, "Current Monthly Cost"), "main9", ink);
  const Rect list{r.x + 4, r.y + 58, 376, 228};   // frame: dark top / left, light bottom / right; flat interior
  ui.fill(list, rgb(222, 223, 255)); insetBevel(ui, list);
  static const uint32_t order[7] = {kAdvUtilities, kAdvDemographics, kAdvSafety, kAdvEnvironment, kAdvPlanner, kAdvTransport, kAdvBudget};
  struct Item { bool header; uint32_t id; std::string name; };
  std::vector<Item> items;
  for (uint32_t adv : order) {
    if (advisorFilter && adv != advisorFilter) continue;
    std::vector<Item> rows;
    for (auto& kv : o.catalogue())
      if (kv.second.category == adv && o.available(kv.first)) rows.push_back({false, kv.first, ordinanceName(E, kv.second)});
    if (rows.empty()) continue;
    items.push_back({true, adv, AdvisorWindow::advisorName(E, adv)});
    items.insert(items.end(), rows.begin(), rows.end());
  }
  // Layout: a band 26 rows tall, its first row 28 below the band top, rows 18 apart, the next band 16 below the last row's top.
  auto advance = [&](size_t i) { const bool nextHeader = i + 1 < items.size() && items[i + 1].header; return items[i].header ? 28 : (nextHeader ? 16 : 18); };
  const int top = list.y + 5, bottom = list.y + list.h - 1;   // interior rows 67..292 on the original
  int visible = 0;
  { int h = 0; for (size_t i = size_t(scroll); i < items.size(); i++) { h += advance(i); if (h > bottom - top) break; visible++; } }
  const bool bar = int(items.size()) > std::max(1, visible);
  if (bar) {
    const Rect br{list.x + 356, list.y + 5, 16, 216};
    ui.fill({br.x, br.y, 16, 218}, band);
    scenarioScrollBar(ui, E, br, list, int(items.size()), std::max(1, visible), scroll);
  } else scroll = 0;
  const int bodyR = list.x + (bar ? 360 : 372);   // right limit of the band
  const Image* checkArt = ui.skin ? ui.skin->get("check") : nullptr;
  // Rows are clipped at the interior bottom (y 292): a partly visible last row is drawn whole and the pixels below the interior are then restored.
  std::vector<uint32_t> below;
  const int clipY = list.y + list.h - 1, clipRows = 40;
  for (int yy = 0; yy < clipRows; yy++)
    for (int xx = list.x; xx < list.x + list.w; xx++) below.push_back(clipY + yy < ui.H && xx < ui.W ? ui.at(xx, clipY + yy) : 0u);
  int y = top;
  for (size_t i = size_t(scroll); i < items.size(); i++) {
    const Item& it = items[i];
    if (y >= bottom) break;
    if (it.header) {
      const Rect bd{list.x + 4, y, 349, 26};   // x 16..364
      ui.fill(bd, bandEdge); ui.fill({bd.x + 1, bd.y + 1, bd.w - 2, bd.h - 2}, band);
      ui.text(bd.x + 4, bd.y + 6, it.name, "main9", ink);   // ink rows 80..88: 9 below the band top
      bool c = false;
      const Rect q{bd.x + bd.w - 31, bd.y + 1, 23, 23};
      E.spot("adv." + std::to_string(it.id), q);
      if (ui.skin && ui.skin->imageButtonCells(ui, ui.skin->image(skin_group::Gonzo, 0x19), 4, q, "", false, true, c)) {}
      else c = ui.button(q, "?");
      if (c) R.add(Act::OpenAdvisor, it.id);
    } else {
      const bool en = o.enacted(it.id);
      const Rect cb{list.x + 4, y, 13, 13};   // check box art 13 x 13 at (16, 99 + 18 k)
      const Rect hit{list.x + 4, y, bodyR - (list.x + 4) - 8, 18};
      E.spot("ord." + std::to_string(it.id), hit);
      const bool hot = ui.hover(hit);
      if (!(ui.skin && ui.skin->drawCheck(ui, checkArt, cb.x, cb.y, en, true, hot, hot && ui.in.down))) {
        ui.fill(cb, rgb(255, 255, 255));
        ui.fill({cb.x, cb.y, cb.w, 1}, ink); ui.fill({cb.x, cb.y, 1, cb.h}, ink); ui.fill({cb.x, cb.y + cb.h - 1, cb.w, 1}, ink); ui.fill({cb.x + cb.w - 1, cb.y, 1, cb.h}, ink);
        if (en) { for (int k = 0; k < 3; k++) ui.fill({cb.x + 2 + k, cb.y + 5 + k, 1, 2}, ink); for (int k = 0; k < 5; k++) ui.fill({cb.x + 4 + k, cb.y + 7 - k, 1, 2}, ink); }
      }
      if (ui.clicked(hit)) {
        const bool ok = en ? o.repeal(it.id) : o.enact(it.id);
        if (ok) R.add(Act::SetOrdinance, it.id, !en);
      }
      ui.text(list.x + 19, y, it.name, "system9", ink);   // SYSTEM9 (not bold), ink x 31, cap top 101
      const int64_t val = en ? o.monthlyValue(it.id, money) : o.ifEnactedValue(it.id, money);   // not enacted: the cost it would have, in grey
      rightText(ui, list.x + 351, y, E.money(val), "main9", en ? (val < 0 ? rgb(0xC0, 0, 0) : 0) : grey);
    }
    y += advance(i);
  }
  for (int yy = 0; yy < clipRows; yy++)
    for (int xx = list.x; xx < list.x + list.w; xx++)
      if (clipY + yy < ui.H && xx < ui.W) ui.at(xx, clipY + yy) = below[size_t(yy) * size_t(list.w) + size_t(xx - list.x)];
  const auto m = o.monthlyMoney(money);
  const int64_t net = m.income - m.cost;
  { // Footer: "Monthly Total" from x 255, the amount from x 343, cap top y 298.
    const std::string lab = E.win(0x1C4, "Monthly Total"), val = E.money(net);
    ui.text(r.x + 247, r.y + 288, lab, "main9", ink);
    ui.text(r.x + 335, r.y + 288, val, "main9", net < 0 ? rgb(0xC0, 0, 0) : 0);
  }
  const Rect done{r.x + r.w - 52, r.y + r.h - 44, 44, 34};
  E.spot("ok", done);
  bool c = false;
  if (!(ui.skin && ui.skin->imageButtonCells(ui, ui.skin->image(skin_group::Sys, 0xA2DEFD9A), 4, done, "", false, true, c))) c = ui.button(done, E.app(0xB4, "OK"));
  if (c || ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }
  return R;
}

WindowResult OrdinancesWindow::draw(Ui& ui, const WinEnv& E, Ordinances& o, const OrdinanceMoneyInput& money,
                                    uint32_t advisorFilter, bool framed, Rect area) {
  WindowResult R;
  Rect r = area;
  if (framed) return drawFramed(ui, E, o, money, advisorFilter);
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
// Layout measured on dialog-shots/hud--review--budget--income--business-deals.png (800x600): 328x196 frame, deals list
// in a sunken lavender box (name left, "+$value" right), total line, and two blue plates: the "?" (finance briefing from
// the financial advisor) and the check mark. Rows are selectable; the selected deal can be cancelled. A pending offer
// (no reference screenshot) shows Accept / Decline pills between the plates.
WindowResult BusinessDealsWindow::draw(Ui& ui, const WinEnv& E, BusinessDeals& d, const std::string& briefing) {
  WindowResult R;
  const uint32_t ink = rgb(0x31, 0x34, 0x7B), white = rgb(255, 255, 255);
  Rect r = centered(ui, 328, 196);
  ui.panel(r, E.win(0x1FE, "Business Deals"));   // no close box in the original; the check mark (or Esc) closes
  auto T = [&](int x, int y, const std::string& s) { ui.text(r.x + x, r.y + y, s, "main9", ink); };
  auto plus = [&](int64_t v) { return (v > 0 ? "+" : "") + E.money(v); };   // no "+" for zero [OBSERVED]
  T(24, 43, E.win(0x1D0, "Current Business Deals:"));
  T(178, 43, E.win(0x13E, "Monthly Contribution:"));
  Rect lr{r.x + 7, r.y + 59, 315, 69};
  ui.fill(lr, rgb(0xDE, 0xDF, 0xFF)); sunkenLines(ui, lr);
  std::vector<uint32_t> types = d.inForce;
  const int visible = (lr.h - 1) / 14;
  if (ui.hover(lr) && ui.in.wheel) scroll -= ui.in.wheel;
  scroll = std::max(0, std::min(scroll, std::max(0, int(types.size()) - visible)));
  for (int i = 0; i < visible && scroll + i < int(types.size()); i++) {
    uint32_t t = types[size_t(scroll + i)];
    std::string name = "Deal " + std::to_string(t);
    int val = 0;
    for (auto& def : d.defs) if (def.buildingType == t) { name = def.name.empty() ? name : def.name; val = def.value; break; }
    if (auto nm = names.find(t); nm != names.end() && !nm->second.empty()) name = nm->second;
    Rect row{lr.x + 1, lr.y + 1 + 14 * i, lr.w - 2, 14};
    E.spot("row." + std::to_string(i), row);
    bool on = selType == int(t);
    if (on) ui.fill(row, rgb(0x7B, 0x92, 0xFF));
    ui.text(r.x + 25, row.y, name, "main9", on ? white : ink);
    std::string vs = plus(val);
    ui.text(r.x + 304 - ui.textWidth(vs, "main9"), row.y, vs, "main9", on ? white : ink);
    if (ui.clicked(row)) selType = on ? -1 : int(t);
  }
  T(113, 129, E.win(0x239, "Total Monthly Contribution:"));
  { std::string tv = plus(d.monthlyIncome()); ui.text(r.x + 304 - ui.textWidth(tv, "main9"), r.y + 129, tv, "main9", ink); }
  // Plates.
  blueBand(ui, {r.x + 86, r.y + 155, 44, 34});
  blueBand(ui, {r.x + 271, r.y + 156, 44, 34});   // plate 42 x 32 at (508, 358) inside a 1 px light border, outer box x 507..550 y 357..390 [OBSERVED]
  { std::string fb = E.win(0x23A, "Finance Briefing:");
    size_t sp = fb.rfind(' ');
    T(24, 159, sp == std::string::npos ? fb : fb.substr(0, sp));
    if (sp != std::string::npos) T(24, 173, fb.substr(sp + 1)); }
  Rect q{r.x + 90, r.y + 158, 36, 28};
  E.spot("briefing", q);
  { bool c = false; const Image* art = E.img(0x42DEC7D2, 0x19);
    if (!(ui.skin && art && ui.skin->imageButtonCells(ui, art, 4, q, briefing, false, true, c))) c = ui.button(q, "?");
    if (c) R.add(Act::OpenAdvisor, kAdvBudget); }
  // Pending offer / cancel the selected deal (no reference): pills between the plates.
  if (d.pendingOffer && *d.pendingOffer < d.defs.size()) {
    if (button(ui, E, "accept", {r.x + 134, r.y + 156, 66, 33}, E.app(0xC0, "Accept"))) { size_t i = *d.pendingOffer; if (d.accept()) R.add(Act::AcceptDeal, uint32_t(i)); }
    if (button(ui, E, "decline", {r.x + 202, r.y + 156, 66, 33}, E.app(0xC2, "Decline"))) { size_t i = *d.pendingOffer; d.decline(); R.add(Act::DeclineDeal, uint32_t(i)); }
  } else if (selType >= 0) {
    if (button(ui, E, "cancel." + std::to_string(selType), {r.x + 150, r.y + 156, 100, 33}, E.app(0xB6, "Cancel"))) {
      d.remove(uint32_t(selType));
      R.add(Act::CancelDeal, uint32_t(selType), 0, 0, "business");
      selType = -1;
    }
  }
  Rect done{r.x + 272, r.y + 156, 42, 32};   // the check art sits 1 px higher than the plate (observed)
  E.spot("ok", done);
  { bool c = false;
    if (!(ui.skin && ui.skin->imageButtonCells(ui, ui.skin->image(skin_group::Sys, 0xA2DEFD9A), 4, done, "", false, true, c))) c = ui.button(done, E.app(0xB4, "OK"));
    if (c || ui.in.key[K_ENTER] || ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); } }
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

// "Neighbors Map", measured on dialog-shots/hud--review--budget--income--neighbor-deals.png (800x600): 372x445 frame,
// region diamond (3 x 3 cells; the city in the middle, the four neighbours on the diagonal cells) with names and
// populations, compass, national population, the selected neighbour's name, "Discuss Deal" (42DEC7D2:10) and the
// current deals with that neighbour in a sunken list. Neighbour cells: see offs below.
WindowResult NeighboursWindow::draw(Ui& ui, const WinEnv& E, NeighbourDeals& n) {
  WindowResult R;
  const uint32_t ink = rgb(0x31, 0x34, 0x7B), lav = rgb(0x7B, 0x7D, 0xB5), white = rgb(255, 255, 255);
  Rect r = placedAt(ui, 50, 50, 372, 445);
  if (frame(ui, E, r, E.win(0x117, "Neighbors Map"))) { R.close = true; R.add(Act::Close); }
  for (Rect b : {Rect{10, 325, 111, 26}, Rect{232, 300, 131, 51}, Rect{10, 326, 11, 110}, Rect{352, 300, 11, 136}, Rect{10, 395, 353, 41}})
    ui.fill({r.x + b.x, r.y + b.y, b.w, b.h}, rgb(0x7B, 0x92, 0xFF));
  // Region diamond.
  const double cx = r.x + 182.5, cy = r.y + 191.5;
  const int rot = viewRotation & 3;
  const double phi = (45.0 - 90.0 * rot) * 3.14159265358979323846 / 180.0, cphi = std::cos(phi), sphi = std::sin(phi);
  const double side = 302.0 / std::sqrt(2.0);   // image side in screen pixels
  for (int y = r.y + 39; y <= r.y + 345; y++)
    for (int x = r.x + 30; x <= r.x + 336; x++) {
      double px = x + 0.5, py = y + 0.5, d = std::abs(px - cx) + std::abs(py - cy);
      if (d > 153) continue;
      uint32_t c;
      if (d > 151) c = px < cx ? rgb(0x7B, 0x8A, 0xFF) : rgb(0xFF, 0xF3, 0xFF);
      else {
        if (regionMap) {
          // Region image (spec 11 section 9.7) turned by phi clockwise so its diagonals span the 302 px widget;
          // phi = 45 degrees at view rotation 0, each further rotation turns it 90 degrees back. Nearest pixel. The
          // image centre sits one pixel right of and below the outline's centre: best fit to the original's Berlin
          // screenshot (palette-index agreement 92.9 % against 90.8 % unshifted).
          int N = regionMap->w;
          double ex = px - cx - 1, ey = py - cy - 1;
          double ix = ex * cphi + ey * sphi, iy = -ex * sphi + ey * cphi;
          int col = int(std::floor(ix * N / side + N * 0.5)), row = int(std::floor(iy * N / side + N * 0.5));
          col = std::max(0, std::min(N - 1, col)); row = std::max(0, std::min(regionMap->h - 1, row));
          c = regionMap->px[size_t(row) * N + col];
        } else {
          int cell = int(std::floor((px - cx + py - cy) / 100.0 + 1.5)) * 3 + int(std::floor((py - cy - (px - cx)) / 100.0 + 1.5));
          static const uint32_t pal[5] = {rgb(0x8C, 0x9E, 0x6B), rgb(0x7B, 0x92, 0x6B), rgb(0x9C, 0xA2, 0x73), rgb(0x94, 0x86, 0x63), rgb(0x84, 0x71, 0x5A)};
          c = pal[(cell % 5 + 5) % 5];
        }
      }
      ui.fill({x, y, 1, 1}, c);
    }
  // Neighbour cells: outlines (blue, the selected one red), click to select, names and populations in white.
  // At view rotation 0: neighbour 0 top-right, 1 top-left, 2 bottom-left, 3 bottom-right (observed); each further
  // rotation turns them 90 degrees anticlockwise with the region image.
  int offs[4][2] = {{50, -50}, {-50, -50}, {-50, 50}, {50, 50}};
  for (int k = 0; k < rot; k++) for (auto& o : offs) { int t = o[0]; o[0] = o[1]; o[1] = -t; }
  auto outline = [&](double ox, double oy, double h, double w, uint32_t col) {
    for (int y = int(cy + oy - h - w); y <= int(cy + oy + h + w); y++)
      for (int x = int(cx + ox - h - w); x <= int(cx + ox + h + w); x++) {
        double d = std::abs(x + 0.5 - (cx + ox)) + std::abs(y + 0.5 - (cy + oy));
        if (d > h - w && d <= h) ui.fill({x, y, 1, 1}, col);
      }
  };
  for (int i = 0; i < kNeighbours; i++) {
    const Neighbour& nb = n.neighbours[i];
    double ox = offs[i][0], oy = offs[i][1];
    Rect hit{int(cx + ox - 35), int(cy + oy - 35), 70, 70};
    E.spot("nb." + std::to_string(i), hit);
    // The original lists every named neighbour, including one whose `exists` byte is 0 (Hawkin's Creek in Berlin).
    if (!nb.exists && nb.cityName.empty()) continue;
    outline(ox, oy, 47, 2, rgb(0x6B, 0x92, 0xF7));
    if (selected == i) outline(ox + 0.5, oy + 1.5, 46, 3, rgb(255, 0, 0));   // box (137,147)-(228,238) on the original (observed)
    const int ncx = int(cx + ox), ncy = int(cy + oy);
    // A neighbour without a land link (`exists` 0) gets an arrow pointing out of the map (000005E5:28 up-left, 2B up-right,
    // 29 down-left, 23 down-right), under its name. Placement measured for the up-left arrow (Hawkin's Creek in
    // Berlin): top-left at (-22, -16) from the cell centre; the others mirror it.
    if (!nb.exists) {
      bool left = ox < 0, up = oy < 0;
      uint32_t inst = up ? (left ? 0x28 : 0x2B) : (left ? 0x29 : 0x23);
      if (const Image* im = E.img(0x5E5, inst)) ui.blit(*im, left ? ncx - 22 : ncx + 4, up ? ncy - 16 : ncy - 2);
    }
    // Active deals with this neighbour: one icon per commodity (power 000005E5:27, water 22, garbage 24), in a row
    // below the population. One icon's top-left is at (-8, +12) from the cell centre (Upper Marazan's garbage deal
    // in Berlin); several are spaced 14 px apart around that spot.
    {
      bool kind[3] = {};
      for (const Deal& d : n.active)
        if (d.neighbourIndex == i && !d.terminated() && d.dealType < kDealTypes) kind[d.dealType / 2] = true;
      static const uint32_t dealIcon[3] = {0x27, 0x22, 0x24};
      int count = int(kind[0]) + int(kind[1]) + int(kind[2]), x = ncx - 8 - (count - 1) * 7;
      for (int k = 0; k < 3; k++)
        if (kind[k]) { if (const Image* im = E.img(0x5E5, dealIcon[k])) ui.blit(*im, x, ncy + 12); x += 14; }
    }
    std::string nm = nb.cityName.empty() ? "Neighbour " + std::to_string(i + 1) : nb.cityName;
    std::string pop = groupDigits(nb.population);
    ui.text(int(cx + ox) - ui.textWidth(nm, "main9") / 2, int(cy + oy) - 22, nm, "main9", white);
    ui.text(int(cx + ox) - ui.textWidth(pop, "main9") / 2, int(cy + oy) - 1, pop, "main9", white);
    if (ui.clicked(hit)) selected = i;
  }
  if (!cityName.empty()) {
    std::string pop = groupDigits(cityPop);
    ui.text(int(cx) - ui.textWidth(cityName, "main9") / 2, int(cy) - 11, cityName, "main9", white);
    ui.text(int(cx) - ui.textWidth(pop, "main9") / 2, int(cy) + 10, pop, "main9", white);
  }
  wui::compassRose(ui, E, r.x + 50, r.y + 80, rot);
  ui.text(r.x + 242, r.y + 52, E.win(0x165, "Nat'l Pop"), "main9", lav);
  ui.text(r.x + 242, r.y + 66, groupDigits(int64_t(nationalPop)), "main9", lav);
  // Selected neighbour's name.
  ui.text(r.x + 17, r.y + 308, E.win(0x29A, "City:"), "main9", lav);
  Rect cf{r.x + 18, r.y + 329, 98, 17};
  ui.fill(cf, rgb(0, 0, 0)); ui.fill({cf.x + 1, cf.y + 1, cf.w - 2, cf.h - 2}, white);
  const bool haveSel = selected >= 0 && selected < kNeighbours && (n.neighbours[selected].exists || !n.neighbours[selected].cityName.empty());
  if (haveSel) ui.text(r.x + 20, r.y + 330, n.neighbours[selected].cityName, "main9", ink);
  ui.text(r.x + 239, r.y + 318, E.win(0x29C, "Discuss Deal:"), "main9", ink);
  { Rect db{r.x + 322, r.y + 310, 38, 33};
    E.spot("discuss", db);
    bool canDeal = false;   // enabled only when the selected neighbour has deals in the list below [STATIC SIMUI]
    if (haveSel) for (const Deal& d : n.active) if (d.neighbourIndex == selected && !d.terminated() && d.dealType < kDealTypes) canDeal = true;
    bool c = false; const Image* art = E.img(0x42DEC7D2, 0x10);
    if (!(ui.skin && art && ui.skin->imageButtonCells(ui, art, 4, db, "", false, canDeal, c))) c = ui.button(db, "?", canDeal);
    if (c && canDeal) R.add(Act::DiscussDeal, uint32_t(selected)); }
  // Current deals with the selected neighbour.
  ui.text(r.x + 48, r.y + 357, E.win(0x13D, "Current Deals:"), "main9", lav);
  ui.text(r.x + 185, r.y + 357, E.win(0x29B, "Rate:"), "main9", lav);
  { std::string mc = E.win(0x13E, "Monthly Contribution:"); size_t sp = mc.find(' ');
    std::string a = sp == std::string::npos ? mc : mc.substr(0, sp), b2 = sp == std::string::npos ? "" : mc.substr(sp + 1);
    ui.text(r.x + 294 - ui.textWidth(a, "main9") / 2, r.y + 350, a, "main9", lav);
    ui.text(r.x + 294 - ui.textWidth(b2, "main9") / 2, r.y + 364, b2, "main9", lav); }
  Rect lr{r.x + 30, r.y + 380, 311, 60};
  ui.fill(lr, rgb(0xDE, 0xDF, 0xFF)); sunkenLines(ui, lr);
  if (haveSel) {
    int y = lr.y + 1;
    for (auto& d : n.active) {
      if (d.neighbourIndex != selected || d.terminated() || y + 14 > lr.y + lr.h) continue;
      ui.text(lr.x + 4, y, dealKindName(E, d.dealType), "main9", ink);
      ui.text(r.x + 160, y, dealRateText(E, d), "main9", ink);
      std::string mv = E.money(d.monthlyAmount);
      ui.text(lr.x + lr.w - 22 - ui.textWidth(mv, "main9"), y, mv, "main9", ink);
      if (button(ui, E, "cancel." + std::to_string(d.dealType), {lr.x + lr.w - 18, y + 1, 14, 13}, "x")) {
        int64_t pen = n.cancel(d.dealType);
        R.add(Act::CancelDeal, d.dealType, pen);
        break;   // active list changed
      }
      y += 14;
    }
  }
  if (ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }
  return R;
}

}  // namespace m3k
