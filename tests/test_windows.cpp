// Headless tests for the in-game windows (windows*.cpp): drive each window with scripted Input clicks
// (controls are found through WinEnv::spots) and assert the events and the subsystem edits.
#include <cmath>
#include <functional>
#include "core/windows.h"
#include <cstring>
#include "testutil.h"
using namespace m3k;

namespace {

struct Harness {
  Fonts fonts;
  Ui ui;
  std::vector<uint32_t> fb;
  std::vector<Spot> spots;
  WinEnv E;
  int W = 1024, H = 768;
  std::function<WindowResult()> drawFn;
  Harness() { fb.assign(size_t(W) * H, 0); ui.fonts = &fonts; E.spots = &spots; }

  WindowResult frame(const Input& in = Input()) {
    spots.clear();
    std::fill(fb.begin(), fb.end(), rgb(0x40, 0x60, 0x40));
    ui.begin(&fb, W, H, in);
    return drawFn();
  }
  bool find(const std::string& name, Rect& r) const {
    for (auto& s : spots) if (s.name == name) { r = s.r; return true; }
    return false;
  }
  bool has(const std::string& name) const { Rect r; return find(name, r); }
  // Full click: idle frame (to learn the layout), press frame, release frame. Returns merged results.
  WindowResult click(const std::string& name, int dx = -1, int dy = -1) {
    WindowResult all;
    frame();
    Rect r;
    if (!find(name, r)) { std::fprintf(stderr, "no control '%s'\n", name.c_str()); g_failures++; return all; }
    Input in;
    in.mx = r.x + (dx < 0 ? r.w / 2 : dx);
    in.my = r.y + (dy < 0 ? r.h / 2 : dy);
    in.down = in.pressed = true;
    all.merge(frame(in));
    in.pressed = false; in.down = false; in.released = true;
    all.merge(frame(in));
    return all;
  }
  WindowResult key(int k) {
    Input in; in.key[k] = true;
    return frame(in);
  }
  // Number of pixels differing from the background colour (rough "something was drawn").
  size_t inked() const { size_t n = 0; for (auto p : fb) if (p != rgb(0x40, 0x60, 0x40)) n++; return n; }
};

void testHelpers() {
  CHECK(subst("%s has %d", {"a", "3"}) == "a has 3");
  CHECK(subst("%1 and %2", {"x", "y"}) == "x and y");
  CHECK(subst("100%%", {}) == "100%");
  WinEnv E;
  CHECK(E.money(1234) == "$1,234");
  CHECK(E.money(-1234) == "-$1,234");
  CHECK(E.win(1, "fallback") == "fallback");
  E.textFn = [](uint32_t, uint32_t id) { return id == 5 ? std::string("five") : std::string(); };
  CHECK(E.win(5, "x") == "five");
  CHECK(E.win(6, "x") == "x");
  CHECK(E.has(kTblWindow, 5) && !E.has(kTblWindow, 6));
}

// ---------------------------------------------------------------- budget
void testBudget() {
  Harness h;
  Budget b;
  b.newCity(50000);
  b.dept(dept::Police)->fullCost = 1000;
  BudgetView v;
  v.in.month = 3; v.in.year = 2000;
  BudgetWindow w;
  h.drawFn = [&] { return w.draw(h.ui, h.E, b, v); };
  WindowResult r = h.frame();
  CHECK(h.inked() > 1000);
  CHECK(h.has("close") && h.has("ok") && h.has("details:tax.r"));
  // Lines table: 3 tax lines + 6 depts + ordinances/deals/loans etc.
  auto L = BudgetWindow::lines(h.E, b, v);
  CHECK(L.size() >= 14);
  int incomes = 0; for (auto& l : L) incomes += l.income;
  CHECK(incomes >= 6);

  // Tax page.
  r = h.click("details:tax.c");
  CHECK(w.page == BudgetWindow::Page::Tax && w.taxGroup == 1);
  int before = b.tax[1].rate;
  r = h.click("adjust.plus");
  CHECK(b.tax[1].rate == before + 1);
  CHECK(r.has(Act::SetTax) && r.find(Act::SetTax)->id == 1 && r.find(Act::SetTax)->a == before + 1);
  r = h.click("adjust.minus");
  r = h.click("adjust.minus");
  CHECK(b.tax[1].rate == before - 1);
  // Slider drag to far right -> max rate; far left -> 0.
  { Rect s; h.frame(); h.find("adjust.slider", s);
    Input in; in.mx = s.x + s.w - 1; in.my = s.y + s.h / 2; in.down = in.pressed = true;
    r = h.frame(in);
    CHECK(b.tax[1].rate >= kMaxTaxRate - 1);
    in.pressed = false; in.mx = s.x + s.w + 30;   // drag past the end
    r.merge(h.frame(in));
    CHECK(b.tax[1].rate == kMaxTaxRate);
    CHECK(r.has(Act::SetTax));
    in.pressed = false; in.down = false; in.released = true; h.frame(in);
    in = Input(); in.mx = s.x; in.my = s.y + 2; in.down = in.pressed = true;
    r = h.frame(in);
    CHECK(b.tax[1].rate == 0);
    in.pressed = false; in.down = false; in.released = true; h.frame(in); }
  // Plus at max stays at max.
  b.setTaxRate(1, kMaxTaxRate);
  r = h.click("adjust.plus");
  CHECK(b.tax[1].rate == kMaxTaxRate);
  r = h.click("advisor");
  CHECK(r.has(Act::OpenAdvisor) && r.find(Act::OpenAdvisor)->id == kAdvBudget);
  r = h.click("link.ord"); CHECK(r.has(Act::OpenOrdinances));
  r = h.click("link.deals"); CHECK(r.has(Act::OpenNeighbours));
  r = h.click("link.biz"); CHECK(r.has(Act::OpenBusinessDeals));
  r = h.click("back");
  CHECK(w.page == BudgetWindow::Page::Main);

  // Department page: funding.
  w.openDept(dept::Police);
  h.frame();
  CHECK(h.inked() > 1000);
  r = h.click("adjust.minus");
  CHECK(b.dept(dept::Police)->funding == 99);
  CHECK(r.has(Act::SetFunding) && r.find(Act::SetFunding)->id == dept::Police && r.find(Act::SetFunding)->a == 99);
  b.setFunding(dept::Police, 120);
  r = h.click("adjust.plus");
  CHECK(b.dept(dept::Police)->funding == BudgetWindow::kMaxFundingUi || b.dept(dept::Police)->funding >= 120);
  // Every department reachable from the main page.
  for (const char* key : {"details:tax.r", "details:tax.i", "details:loans", "details:ord.exp", "details:deal.inc", "details:biz"}) {
    w.openMain();
    r = h.click(key);
    if (!strcmp(key, "details:loans")) CHECK(w.page == BudgetWindow::Page::Loans);
    if (!strcmp(key, "details:ord.exp")) CHECK(r.has(Act::OpenOrdinances));
    if (!strcmp(key, "details:deal.inc")) CHECK(r.has(Act::OpenNeighbours));
    if (!strcmp(key, "details:biz")) CHECK(r.has(Act::OpenBusinessDeals));
  }
  // Loans: accept 5K.
  w.openMain(); h.frame(); h.click("details:loans");
  CHECK(w.page == BudgetWindow::Page::Loans);
  int64_t funds = b.funds;
  w.newLoanThousands = 5;
  r = h.click("amount.plus");
  CHECK(w.newLoanThousands == 10);
  r = h.click("accept");
  CHECK(r.has(Act::IssueBond));
  CHECK(r.find(Act::IssueBond)->a == 10000 && r.find(Act::IssueBond)->b == 1);
  CHECK(b.bondCount() == 1);
  CHECK(b.funds == funds + 10000);
  h.frame();
  CHECK(h.inked() > 1000);
  // Exhaust loans -> refusal reported.
  for (int i = 0; i < 12; i++) r = h.click("accept");
  CHECK(b.bondCount() <= b.cfg.maxBonds);
  CHECK(r.has(Act::IssueBond) && r.find(Act::IssueBond)->b == 0);
  CHECK(!w.loanMessage.empty());
  r = h.click("advisor"); CHECK(r.has(Act::OpenAdvisor));
  // Indicators page.
  w.openMain();
  h.click("indicators");
  CHECK(w.page == BudgetWindow::Page::Indicators);
  v.ind.citySize = 50; v.ind.nationalPop = 123456; v.ind.unemployment = 7;
  v.approval = 66;
  h.frame();
  CHECK(h.inked() > 1000);
  h.click("back"); CHECK(w.page == BudgetWindow::Page::Main);
  // OK / close.
  r = h.click("ok"); CHECK(r.close && r.has(Act::Close));
  r = h.click("close"); CHECK(r.close);
  // Tax approval bar on tax page.
  w.openTax(0); h.frame(); CHECK(h.inked() > 1000);
}

// ---------------------------------------------------------------- ordinances
void testOrdinances() {
  Harness h;
  Ordinances o;
  o.newCity();
  o.makeAllAvailable();
  OrdinanceMoneyInput money; money.population = 100000;
  OrdinancesWindow w;
  h.drawFn = [&] { return w.draw(h.ui, h.E, o, money); };
  h.frame();
  CHECK(h.inked() > 1000);
  // Find first ordinance check spot.
  std::string name; uint32_t id = 0;
  for (auto& s : h.spots) if (s.name.rfind("ord.", 0) == 0) { name = s.name; id = uint32_t(std::stoul(s.name.substr(4))); break; }
  CHECK(id != 0);
  CHECK(!o.enacted(id));
  WindowResult r = h.click(name);
  CHECK(o.enacted(id));
  CHECK(r.has(Act::SetOrdinance) && r.find(Act::SetOrdinance)->id == id && r.find(Act::SetOrdinance)->a == 1);
  r = h.click(name);
  CHECK(!o.enacted(id));
  CHECK(r.has(Act::SetOrdinance) && r.find(Act::SetOrdinance)->a == 0);
  // Unavailable ordinance not listed.
  Ordinances o2; o2.newCity();
  h.drawFn = [&] { return w.draw(h.ui, h.E, o2, money); };
  h.frame();
  size_t listed = 0; for (auto& s : h.spots) if (s.name.rfind("ord.", 0) == 0) listed++;
  size_t avail = 0; for (auto& kv : o2.catalogue()) avail += o2.available(kv.first);
  CHECK(listed == std::min<size_t>(avail, listed) && listed <= avail);
  // Scrolling.
  h.drawFn = [&] { return w.draw(h.ui, h.E, o, money); };
  h.frame();
  size_t first = 0; for (auto& s : h.spots) if (s.name.rfind("ord.", 0) == 0) first++;
  h.click("scroll.down");
  CHECK(w.scroll >= 1 || first >= o.catalogue().size());
  // Advisor filter: only that category.
  OrdinancesWindow wf;
  uint32_t cat = o.catalogue().begin()->second.category;
  h.drawFn = [&] { return wf.draw(h.ui, h.E, o, money, cat); };
  h.frame();
  size_t filt = 0; for (auto& s : h.spots) if (s.name.rfind("ord.", 0) == 0) filt++;
  size_t inCat = 0; for (auto& kv : o.catalogue()) inCat += kv.second.category == cat;
  CHECK(filt > 0 && filt <= inCat);
  // Close.
  h.drawFn = [&] { return w.draw(h.ui, h.E, o, money); };
  r = h.click("close"); CHECK(r.close);
}

}  // namespace

int main() {
  testHelpers();
  testBudget();
  testOrdinances();
  TEST_MAIN_END();
}
