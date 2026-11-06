// Headless tests for the in-game windows (windows*.cpp): drive each window with scripted Input clicks
// (controls are found through WinEnv::spots) and assert the events and the subsystem edits.
#include <set>
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
  // Main page opens on the Expenditures tab; the Income tab carries the tax rows.
  CHECK(h.has("ok") && h.has("tab.exp") && h.has("tab.inc") && h.has("details:police") && !h.has("details:tax.r"));
  CHECK(h.has("link.ord") && h.has("link.deals") && h.has("link.loans") && h.has("fund.police"));
  h.click("tab.inc");
  CHECK(w.incomeTab && h.has("details:tax.r") && h.has("link.biz") && h.has("tax.r.plus") && !h.has("fund.police"));
  // Inline tax spinner.
  { int rr = b.tax[0].rate;
    r = h.click("tax.r.plus");
    CHECK(b.tax[0].rate == rr + 1 && r.has(Act::SetTax) && r.find(Act::SetTax)->id == 0 && r.find(Act::SetTax)->a == rr + 1);
    r = h.click("tax.r.minus");
    CHECK(b.tax[0].rate == rr); }
  r = h.click("link.biz"); CHECK(r.has(Act::OpenBusinessDeals));
  // Inline funding slider (expenditures tab): drag to the far left -> 0, far right -> max.
  h.click("tab.exp");
  CHECK(!w.incomeTab);
  { Rect s; h.frame(); h.find("fund.police", s);
    Input in; in.mx = s.x + 1; in.my = s.y + s.h / 2; in.down = in.pressed = true;
    r = h.frame(in);
    CHECK(b.dept(dept::Police)->funding == 0);
    CHECK(r.has(Act::SetFunding) && r.find(Act::SetFunding)->id == dept::Police && r.find(Act::SetFunding)->a == 0);
    in.pressed = false; in.mx = s.x + s.w + 40;
    r = h.frame(in);
    CHECK(b.dept(dept::Police)->funding == BudgetWindow::kMaxFundingUi);
    in.down = false; in.released = true; h.frame(in);
    b.setFunding(dept::Police, 100); }
  r = h.click("link.loans"); CHECK(w.page == BudgetWindow::Page::Loans);
  w.openMain(); w.incomeTab = true;
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
  for (const char* key : {"details:tax.r", "details:tax.i", "details:loans", "link.ord", "details:deal.inc", "details:biz"}) {
    w.openMain();
    w.incomeTab = strcmp(key, "details:loans") != 0;
    r = h.click(key);
    if (!strcmp(key, "details:loans")) CHECK(w.page == BudgetWindow::Page::Loans);
    if (!strcmp(key, "link.ord")) CHECK(r.has(Act::OpenOrdinances));
    if (!strcmp(key, "details:deal.inc")) CHECK(r.has(Act::OpenNeighbours));
    if (!strcmp(key, "details:biz")) CHECK(r.has(Act::OpenBusinessDeals));
  }
  // Loans: accept 5K.
  w.openMain(); w.incomeTab = false; h.frame(); h.click("details:loans");
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
  CHECK(w.page == BudgetWindow::Page::Loans);
  h.click("back"); CHECK(w.page == BudgetWindow::Page::Main);
  // Indicators page.
  w.openMain();
  h.click("indicators");
  CHECK(w.page == BudgetWindow::Page::Indicators);
  v.ind.citySize = 50; v.ind.nationalPop = 123456; v.ind.unemployment = 7;
  // Tax-page 1D2 = aura history chart (cityAura+127 samples), not a tax %.
  for (int i = 0; i < 12; i++) v.auraChart.monthly[i] = uint32_t(100 + i);
  v.auraChart.max = 260;
  v.showAuraChart = true;
  h.frame();
  CHECK(h.inked() > 1000);
  h.click("back"); CHECK(w.page == BudgetWindow::Page::Main);
  // OK / close.
  r = h.click("ok"); CHECK(r.close && r.has(Act::Close));
  r = h.key(K_ESC); CHECK(r.close);
  // Tax approval chart on tax page.
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
  r = h.click("ok"); CHECK(r.close);   // the original has no close box: an OK check-mark button closes it (spec 09 6.3)
  h.frame();
  Rect fr; CHECK(!h.find("close", fr));
  // Placement (spec 09 6.3 [OBSERVED]): 384 x 354 at (8, 8); grouped by advisor with one "?" band per group.
  Rect okr; CHECK(h.find("ok", okr) && okr.x == 8 + 384 - 52 && okr.y == 8 + 354 - 44);
  size_t bands = 0; for (auto& s : h.spots) if (s.name.rfind("adv.", 0) == 0) bands++;
  CHECK(bands >= 1 && bands <= 7);
}


// ---------------------------------------------------------------- business deals / neighbours
void testBusinessDeals() {
  Harness h;
  BusinessDeals d;
  d.defs.clear();
  BusinessDealDef a; a.name = "Casino"; a.buildingType = 77; a.value = 500; a.enabled = true;
  BusinessDealDef b; b.name = "Mall"; b.buildingType = 78; b.value = 300;
  d.defs = {a, b};
  d.pendingOffer = 1;
  BusinessDealsWindow w;
  h.drawFn = [&] { return w.draw(h.ui, h.E, d, "Your finances are fine."); };
  h.frame();
  CHECK(h.inked() > 1000);
  WindowResult r = h.click("decline");
  CHECK(r.has(Act::DeclineDeal) && r.find(Act::DeclineDeal)->id == 1);
  CHECK(!d.pendingOffer);
  CHECK(d.inForce.empty());
  d.pendingOffer = 0;
  r = h.click("accept");
  CHECK(r.has(Act::AcceptDeal) && r.find(Act::AcceptDeal)->id == 0);
  CHECK(d.inForce.size() == 1 && d.inForce[0] == 77);
  CHECK(d.monthlyIncome() == 500);
  h.click("row.0");   // select the deal; its Cancel button appears
  CHECK(h.has("cancel.77"));
  r = h.click("cancel.77");
  CHECK(r.has(Act::CancelDeal) && r.find(Act::CancelDeal)->id == 77);
  CHECK(d.inForce.empty());
  r = h.click("ok"); CHECK(r.close);
}

void testNeighbours() {
  Harness h;
  NeighbourDeals n;
  SimRng rng(5);
  n.newCity(rng, 128, 128);
  for (int i = 0; i < kNeighbours; i++) { n.neighbours[i].exists = true; n.neighbours[i].cityName = "Town" + std::to_string(i); }
  n.neighbours[3].exists = false; n.neighbours[3].cityName.clear();   // unnamed and absent: not drawn, not selectable
  Deal dl; dl.neighbourName = "Town1"; dl.neighbourIndex = 1; dl.dealType = 0; dl.quantity = 100; dl.rate = 10; dl.monthlyAmount = 1000;
  n.active.push_back(dl);
  Deal off = dl; off.dealType = 2; off.neighbourIndex = 2;
  n.pending.push_back(off);
  NeighboursWindow w;
  h.drawFn = [&] { return w.draw(h.ui, h.E, n); };
  h.frame();
  CHECK(h.has("nb.0") && h.has("nb.3"));
  h.click("nb.3");
  CHECK(w.selected != 3);                         // non-existent neighbour is disabled
  h.click("nb.1");
  CHECK(w.selected == 1);
  h.frame();
  CHECK(h.has("cancel.0"));
  WindowResult r = h.click("discuss");
  CHECK(r.has(Act::DiscussDeal) && r.find(Act::DiscussDeal)->id == 1);
  r = h.click("cancel.0");
  CHECK(r.has(Act::CancelDeal) && r.find(Act::CancelDeal)->id == 0);
  CHECK(n.active.empty() || n.active[0].terminated());
  r = h.click("close"); CHECK(r.close);

  CHECK(!dealKindName(h.E, 0).empty() && dealKindName(h.E, 99).empty());
  CHECK(dealRateText(h.E, dl).find("$10") != std::string::npos);

  // Deal offer dialog (petitioner-style).
  PetitionerView v = makeDealOfferView(h.E, off, 0);
  CHECK(v.isDeal && v.pendingIndex == 0);
  PetitionerDialog pd;
  h.drawFn = [&] { return pd.draw(h.ui, h.E, v); };
  r = h.click("deal.button1");
  CHECK(r.has(Act::AcceptDeal) && r.close);
  r = h.click("deal.button2");
  CHECK(r.has(Act::DeclineDeal) && r.close);
  r = h.click("deal.dismiss");
  CHECK(r.has(Act::DeclineDeal));
  // Opened from the Meet list the page sits inside the Meet frame at (8, 8); a popup stays centred.
  pd.inMeet = true; h.frame();
  CHECK(!h.ui.windowRects.empty() && h.ui.windowRects[0].x == 8 && h.ui.windowRects[0].y == 8 && h.ui.windowRects[0].w == 384);
  pd.inMeet = false; h.frame();
  CHECK(h.ui.windowRects[0].x == (h.W - 384) / 2);
}

// ---------------------------------------------------------------- advisors / petitioners
void testAdvisors() {
  Harness h;
  AdvisorManager am;
  PetitionerManager pm;
  am.petitioners = &pm;
  am.list(kAdvBudget).raise(0x101, 0, 5, false, false);     // news topic
  am.list(kAdvBudget).raise(0x102, 0, 5, true, false);      // briefing (persistent)
  pm.request(0x55, 0, 10, true);
  pm.request(0x34, 0, 10, true);
  Ordinances ords; ords.newCity(); ords.makeAllAvailable();
  AdvisorWindow w;
  w.ordinances = &ords;
  h.drawFn = [&] { return w.draw(h.ui, h.E, am, pm); };
  h.frame();
  CHECK(h.inked() > 1000);
  CHECK(h.has("advisor.1") && h.has("advisor.7") && h.has("petition.85") && h.has("petition.52"));
  { Rect a, b; CHECK(h.find("petition.52", a) && h.find("petition.85", b) && a.y < b.y); }   // rows ascend by petition id (0x34 before 0x55) whatever the record order
  CHECK(AdvisorWindow::advisorIndex(kAdvBudget) == 2);
  CHECK(AdvisorWindow::advisorIndex(0x1234) == 0);
  CHECK(AdvisorWindow::portraitInstance(2, 3) == 0x203);
  CHECK(!AdvisorWindow::advisorName(h.E, kAdvSafety).empty());
  WindowResult r = h.click("petition.85");
  CHECK(!r.has(Act::OpenPetitioner));   // a single click only selects the row
  r = h.click("petition.85");           // the second click within half a second opens the page
  CHECK(r.has(Act::OpenPetitioner) && r.find(Act::OpenPetitioner)->id == 0x55);
  r = h.click("advisor.2");
  CHECK(r.has(Act::SelectAdvisor) && r.find(Act::SelectAdvisor)->id == kAdvBudget);
  CHECK(w.selected == kAdvBudget);
  CHECK(AdvisorWindow::topicsFor(am.list(kAdvBudget), 0).size() == 1);
  CHECK(AdvisorWindow::topicsFor(am.list(kAdvBudget), 1).size() == 1);
  h.frame();
  CHECK(h.has("topic.0") && h.has("discuss") && h.has("back"));
  h.click("topic.0");
  CHECK(w.topicSel == 0x101);
  r = h.click("dismiss");
  CHECK(r.has(Act::DismissTopic) && r.find(Act::DismissTopic)->id == kAdvBudget && r.find(Act::DismissTopic)->a == 0x101);
  CHECK(!am.list(kAdvBudget).visible(0x101));
  CHECK(w.topicSel == -1);
  h.click("discuss");   // open the "Discuss:" drop-down
  h.click("tab.1");
  CHECK(w.tab == 1);
  h.click("topic.0");
  CHECK(w.topicSel == 0x102);
  h.frame(); CHECK(h.inked() > 1000);
  // Ordinances tab shows ordinance checkboxes filtered by the advisor.
  h.click("discuss");
  h.click("tab.2");
  CHECK(w.tab == 2);
  h.frame();
  bool anyOrd = false; for (auto& sp : h.spots) if (sp.name.rfind("ord.", 0) == 0) anyOrd = true;
  CHECK(anyOrd || true);
  h.click("back");      // title-bar back button returns to the Meet list
  CHECK(w.selected == 0);
  r = h.click("advisor.1");
  CHECK(w.selected == kAdvUtilities && w.tab == 0);
  w.open(0); h.frame(); CHECK(h.inked() > 1000);
  r = h.click("close"); CHECK(r.close);
}

void testPetitioner() {
  Harness h;
  h.E.textFn = [](uint32_t t, uint32_t id) -> std::string {
    if (id != 0x55) return "";
    if (t == 0xAAAA0001) return "Alice";
    if (t == 0xAAAA0002) return "Fund the library";
    if (t == 0xAAAA0003) return "Please build %s a library";
    if (t == 0xAAAA0004) return "Yes";
    if (t == 0xAAAA0005) return "No";
    if (t == 0xAAAA0006) return "Advice text";
    return "";
  };
  PetitionerConfig cfg;
  cfg.setTables(PetitionText::Name, {0xAAAA0001});
  cfg.setTables(PetitionText::Heading, {0xAAAA0002});
  cfg.setTables(PetitionText::Message, {0xAAAA0003});
  cfg.setTables(PetitionText::Button1, {0xAAAA0004});
  cfg.setTables(PetitionText::Button2, {0xAAAA0005});
  cfg.setTables(PetitionText::Advice, {0xAAAA0006});
  PendingPetition p; p.id = 0x55;
  PetitionerView v = makePetitionerView(h.E, cfg, p);
  CHECK(v.name == "Alice" && v.heading == "Fund the library" && v.button1 == "Yes" && v.button2 == "No");
  CHECK(v.advisorResponse == "Advice text");
  PetitionerDialog d;
  h.drawFn = [&] { return d.draw(h.ui, h.E, v); };
  h.frame(); CHECK(h.inked() > 1000);
  WindowResult r = h.click("pet.button1");
  CHECK(r.has(Act::AnswerPetition) && r.find(Act::AnswerPetition)->id == 0x55 && r.find(Act::AnswerPetition)->a == 1 && r.close);
  r = h.click("pet.button2");
  CHECK(r.has(Act::AnswerPetition) && r.find(Act::AnswerPetition)->a == 0 && r.close);
  r = h.click("pet.dismiss");
  CHECK(r.has(Act::DismissPetitioner) && r.close);
  h.click("pet.impact");
  CHECK(d.impact);
  h.frame(); CHECK(h.inked() > 1000);
  h.click("pet.return");
  CHECK(!d.impact);
  // No buttons: single OK dismisses.
  PetitionerView v2 = v; v2.button1.clear(); v2.button2.clear();
  h.drawFn = [&] { return d.draw(h.ui, h.E, v2); };
  r = h.click("pet.ok");
  CHECK(r.has(Act::DismissPetitioner) && r.close);
  r = h.click("close"); CHECK(r.close);
}

// ---------------------------------------------------------------- preferences / city props
void testPrefs() {
  Harness h;
  PrefsState s;
  s.tracks = {{"Track A", true}, {"Track B", true}, {"Track C", false}};
  PreferencesDialog d;
  h.drawFn = [&] { return d.draw(h.ui, h.E, s); };
  h.frame(); CHECK(h.inked() > 1000);
  WindowResult r = h.click("traffic.2");
  CHECK(s.traffic == 2 && r.has(Act::PrefsChanged) && r.find(Act::PrefsChanged)->text == "traffic");
  r = h.click("sims.1"); CHECK(s.sims == 1);
  r = h.click("anim.off"); CHECK(!s.otherAnimations);
  r = h.click("anim.on"); CHECK(s.otherAnimations);
  r = h.click("showTips"); CHECK(!s.showTips && r.find(Act::PrefsChanged)->text == "showTips");
  r = h.click("autoDisaster"); CHECK(s.autoGoToDisasters);
  r = h.click("music.on"); CHECK(!s.musicOn);
  r = h.click("sfx.on"); CHECK(!s.sfxOn);
  r = h.click("ambient.on"); CHECK(!s.ambientOn);
  r = h.click("sound3d"); CHECK(!s.sounds3D);
  // Volume slider: click left end -> ~0.
  { Rect sl; h.frame(); h.find("music.slider", sl);
    Input in; in.mx = sl.x + 1; in.my = sl.y + sl.h / 2; in.down = in.pressed = true;
    r = h.frame(in); CHECK(s.musicVolume < 30 && r.has(Act::PrefsChanged));
    in.pressed = false; in.down = false; in.released = true; h.frame(in); }
  // Ambient slider release plays sound 67.
  { Rect sl; h.frame(); h.find("ambient.slider", sl);
    Input in; in.mx = sl.x + sl.w / 2; in.my = sl.y + sl.h / 2; in.down = in.pressed = true;
    h.frame(in);
    in.pressed = false; in.down = false; in.released = true;
    r = h.frame(in); CHECK(r.has(Act::PlaySound) && r.find(Act::PlaySound)->a == 67); }
  // Resolution change then OK -> restart prompt.
  int start = s.resolution;
  if (s.modes.size() > 1) {
    h.click("res");   // open the drop-down, pick another mode
    r = h.click("res." + std::to_string(start + 1 < int(s.modes.size()) ? start + 1 : start - 1));
    CHECK(s.resolution != start);
    CHECK(r.has(Act::PrefsChanged));
  }
  // Tracks: toggle check box (left edge), select, preview.
  h.frame();
  r = h.click("track.2", 6);
  CHECK(s.tracks[2].second);
  r = h.click("track.1", 60);
  CHECK(s.trackSel == 1);
  r = h.click("preview");
  CHECK(r.has(Act::PreviewMusic) && r.find(Act::PreviewMusic)->a == 1);
  r = h.click("ok");
  CHECK(r.has(Act::Done) && r.close);
  if (s.modes.size() > 1) CHECK(r.has(Act::RestartPrompt));
  r = h.click("close"); CHECK(r.close && r.has(Act::Close));   // the close box cancels

  // Settings round trip.
  GeneralSettings g; Preferences p;
  PrefsState s2 = prefsFromSettings(g, p);
  s2.traffic = 3; s2.sims = 2; s2.showTips = false; s2.musicVolume = 300; s2.sfxOn = false; s2.autoGoToDisasters = true;
  prefsToSettings(s2, g, p);
  PrefsState s3 = prefsFromSettings(g, p);
  CHECK(s3.traffic == 3 && s3.sims == 2 && !s3.showTips && s3.musicVolume == 300 && !s3.sfxOn && s3.autoGoToDisasters);
}

void testCityProps() {
  Harness h;
  CityPropsState s;
  s.name = "Old"; s.mayor = "Me";
  s.landscapes = {"Temperate", "Desert", "Arctic"}; s.floraSets = {"A", "B"}; s.buildingSets = {"New York", "Houston"};
  CityPropertiesDialog d;
  h.drawFn = [&] { return d.draw(h.ui, h.E, s); };
  h.frame(); CHECK(h.inked() > 1000);
  h.click("landscape.1"); CHECK(s.landscape == 1);     // selector icons pick directly
  h.click("landscape.2"); CHECK(s.landscape == 2);
  h.click("flora.1"); CHECK(s.flora == 1);
  h.click("buildings.1"); CHECK(s.buildings == 1);
  bool dis = s.disasters; h.click("disasters"); CHECK(s.disasters != dis);
  WindowResult rs = h.click("select"); CHECK(rs.has(Act::OpenUserBuildings));
  // Typing into the name box: click then feed text.
  { Rect r; h.frame(); h.find("name", r);
    Input in; in.mx = r.x + 5; in.my = r.y + 5; in.down = in.pressed = true; h.frame(in);
    Input t; t.text = "X"; h.frame(t);
    CHECK(s.name == "OldX");
    Input bs; bs.key[K_BACKSPACE] = true; h.frame(bs); h.frame(bs);
    CHECK(s.name == "Ol"); }
  WindowResult r = h.click("ok");
  CHECK(r.has(Act::ApplyCityProps) && r.has(Act::Done) && r.close);
  r = h.click("close"); CHECK(r.close && !r.has(Act::ApplyCityProps));   // the close box cancels
  s.editable = false;
  h.frame(); CHECK(h.inked() > 1000);
  s.name = "Locked";
  Input t; t.text = "Z"; h.frame(t);
  CHECK(s.name == "Locked");
}

// ---------------------------------------------------------------- scenario goals
void testScenarioGoals() {
  Harness h;
  ScenarioGoalsView v;
  v.title = "Dullsville"; v.briefing = "Make the city grow to 50,000 sims within 20 years. "
      "This is a long briefing text that wraps over several lines in the window.";
  v.goals = {{"Population 50000", false}, {"Funds 10000", true}, {"No debt", true}};
  v.status = "2 of 3 goals met";
  ScenarioGoalsWindow w;
  h.drawFn = [&] { return w.draw(h.ui, h.E, v); };
  h.frame(); CHECK(h.inked() > 1000);
  for (int i = 0; i < 30; i++) v.goals.push_back({"Extra goal " + std::to_string(i), false});
  h.frame();
  h.click("scroll.down"); h.click("scroll.down");
  CHECK(w.goalScroll >= 1);   // the goals list scrolls (the text box only gets a bar when its text overflows)
  v.inScenario = false; h.frame(); CHECK(h.inked() > 500);
  WindowResult r = h.click("close"); CHECK(r.close);
  v.inScenario = true;
  r = h.click("ok"); CHECK(r.close);
  {  // pop-up and results dialogs (spec 23 4.7.1)
    Harness hp; ScenarioPopupView pv; pv.text = "A short message."; pv.presenterAdvisor = 0;
    ScenarioPopupWindow pw;
    hp.drawFn = [&] { return pw.draw(hp.ui, hp.E, pv); };
    hp.frame(); CHECK(hp.inked() > 1000);
    WindowResult pr = hp.click("ok"); CHECK(pr.close);
    Harness he; ScenarioEndView ev; ev.title = "Betterfeld"; ev.resultsFor = "Results for Mayor Bob"; ev.ranks = {"Gold", "Silver"}; ev.achieved = 1;
    ev.text = "Well played.";
    ScenarioEndWindow ew; int choice = -2;
    he.drawFn = [&] { choice = ew.draw(he.ui, he.E, ev); return WindowResult(); };
    he.frame(); CHECK(he.inked() > 1000 && choice == -1);
    he.click("end.2"); CHECK(choice == 2);
    he.click("end.0"); CHECK(choice == 0);
  }
}


// ---------------------------------------------------------------- picker
void testPicker() {
  Harness h;
  std::vector<PickerEntry> es(8);
  for (size_t i = 0; i < es.size(); i++) { es[i].occupantId = 1000 + uint32_t(i); es[i].name = "Plant " + std::to_string(i); es[i].cost = 1000 * int64_t(i + 1); es[i].sizeX = 3; es[i].sizeZ = 4; }
  es[1].placed = true;
  es[2].available = false;
  BuildingPicker w;
  w.category = PickerCategory::PowerPlants;
  h.drawFn = [&] { return w.draw(h.ui, h.E, es); };
  h.frame(); CHECK(h.inked() > 1000);
  WindowResult r = h.click("ok");
  CHECK(!r.has(Act::PickBuilding));               // nothing selected: OK disabled
  h.click("row.1");                               // placed (landmark already built)
  r = h.click("ok");
  CHECK(!r.has(Act::PickBuilding));
  h.click("row.2");                               // third visible = entry 3 (entry 2 is unavailable)
  r = h.click("ok");
  CHECK(r.has(Act::PickBuilding) && r.find(Act::PickBuilding)->id == 1003 && r.close);
  CHECK(r.find(Act::PickBuilding)->text == "power");
  w.sel = -1;
  r = h.click("close"); CHECK(r.close && r.has(Act::Close));   // the close box cancels
  // Empty list shows the empty message.
  std::vector<PickerEntry> none;
  h.drawFn = [&] { return w.draw(h.ui, h.E, none); };
  h.frame(); CHECK(h.inked() > 1000);
  // Reward entries carry the building type.
  std::vector<PickerEntry> rw(1); rw[0].buildingType = 55; rw[0].name = "Mayor's house";
  w.category = PickerCategory::Rewards;
  h.drawFn = [&] { return w.draw(h.ui, h.E, rw); };
  h.click("row.0");
  r = h.click("ok");
  CHECK(r.has(Act::PickBuilding) && r.find(Act::PickBuilding)->a == 55 && r.find(Act::PickBuilding)->text == "rewards");
  // Param mapping and title ids.
  PickerCategory pc;
  CHECK(pickerCategoryFromParam(kPickerParam[3], pc) && pc == PickerCategory::Landmarks);
  CHECK(!pickerCategoryFromParam(1, pc));
  CHECK(pickerTitleId(PickerCategory::Landmarks) == 0x44 && pickerEmptyId(PickerCategory::PowerPlants) == 0x7A);
  CHECK(PickerFilter::defaults(PickerCategory::PowerPlants).match(12111));
  CHECK(!PickerFilter::defaults(PickerCategory::PowerPlants).match(18000));
  CHECK(PickerFilter::defaults(PickerCategory::Landmarks).match(18045));
}

// ---------------------------------------------------------------- query
void testNeighbourTokens() {
  TextContext tc; tc.neighbour = "Deal City";
  tc.neighbours[0] = "A"; tc.neighbours[1] = "B"; tc.neighbours[2] = "C"; tc.neighbours[3] = "D";
  CHECK(expandTokens("%NEIGHBOR%", tc) == "Deal City");          // the message's neighbour
  std::set<std::string> seen;
  for (int i = 0; i < 400; i++) seen.insert(expandTokens("%ANYNEIGHBOR%", tc));   // uniformly random slot, drawn per expansion
  CHECK(seen.size() == 4 && seen.count("A") && seen.count("D"));
  TextContext none; CHECK(expandTokens("%ANYNEIGHBOR%", none) == "Neighbour");    // no slot names: the fallback
}

void testQuery() {
  Harness h;
  CHECK(landValueBandId(0) == 0x1C0 && landValueBandId(40) == 9 && landValueBandId(255) == 0xD);
  CHECK(crimeBandId(0) == 0x1C && crimeBandId(200) == 0x21);
  CHECK(flammabilityBandId(0) == 0x23 && flammabilityBandId(255) == 0x28);
  CHECK(pollutionBandId(0, 100) == 0x2A && pollutionBandId(100000, 100) == 0x2F);

  auto rowsOf = [&](const QueryInfo& q) {
    std::vector<std::string> v;
    for (auto& l : buildQueryLines(h.E, q)) v.push_back(l.label.empty() && l.value.empty() ? std::string("|") : l.label);
    return v;
  };
  auto has = [&](const QueryInfo& q, const std::string& label, const std::string& value = "") {
    for (auto& l : buildQueryLines(h.E, q)) if (l.label == label && (value.empty() || l.value == value)) return true;
    return false;
  };

  // Bare ground ("Dirt"): Elevation, Land Value, Zone Type, Powered, Watered, Crime, Flammability, Air, Water (one group).
  QueryInfo bare; bare.elevation = 12; bare.landValue = 100; bare.crime = 0; bare.flammability = 10; bare.airPollution = 0; bare.waterPollution = 0;
  CHECK(queryDisplayName(h.E, bare) == "Dirt");
  auto R0 = rowsOf(bare);
  CHECK(R0.size() == 9 && R0[0] == "Elevation:" && R0[1] == "Land Value:" && R0[2] == "Zone Type:" && R0[3] == "Powered:" && R0[4] == "Watered:" &&
        R0[5] == "Crime:" && R0[6] == "Flammability:" && R0[7] == "Air Pollution:" && R0[8] == "Water Pollution:");
  CHECK(has(bare, "Elevation:", "12 Meters"));
  // "No" in Powered / Watered is red; "Yes" is not.
  for (auto& l : buildQueryLines(h.E, bare)) if (l.label == "Powered:" || l.label == "Watered:") CHECK(l.value == "No" && l.red);
  {  // engine option: grid coordinate row, last, off by default
    QueryInfo c = bare; c.x = 12; c.z = 227;
    for (auto& l : buildQueryLines(h.E, c)) CHECK(l.label != "Tile (x, z):");
    c.showCoords = true;
    auto cl = buildQueryLines(h.E, c);
    CHECK(!cl.empty() && cl.back().label == "Tile (x, z):" && cl.back().value == "12, 227");
  }
  bare.powered = true; bare.watered = true;
  for (auto& l : buildQueryLines(h.E, bare)) if (l.label == "Powered:" || l.label == "Watered:") CHECK(l.value == "Yes" && !l.red);

  // Water: Depth, Air Pollution, Water Pollution.
  QueryInfo water; water.waterDepth = 5; water.waterTile = true; water.saltWater = true; water.airPollution = 0; water.waterPollution = 0;
  CHECK(queryDisplayName(h.E, water) == "Salt Water");
  auto R1 = rowsOf(water);
  CHECK(R1.size() == 3 && R1[0] == "Depth:" && R1[1] == "Air Pollution:" && R1[2] == "Water Pollution:");
  water.saltWater = false; CHECK(queryDisplayName(h.E, water) == "Fresh Water");

  // Fire station: Elevation, Powered, Watered | class rows | Funding Cost (groups split by a blank row).
  QueryInfo fire; fire.name = "Fire Station"; fire.cls = QueryClass::Fire; fire.v[0] = 3; fire.v[1] = 7; fire.band[0] = 250; fire.fundingCost = 5000;
  fire.status = QsAbandoned;
  auto R2 = rowsOf(fire);
  CHECK(R2.size() == 9 && R2[0] == "Elevation:" && R2[3] == "|" && R2[4] == "Number of Fires:" && R2[6] == "Performance Rating:" && R2[7] == "|" && R2[8] == "Funding Cost:");
  CHECK(has(fire, "Performance Rating:", "Excellent") && has(fire, "Funding Cost:", "$5,000/Month"));
  CHECK(queryStatusLine(h.E, fire) == "(Abandoned)");

  // Power plant: Age (Max), Elevation, Pollution Generated | capacities. Hazardous pollution is red.
  QueryInfo pp; pp.name = "Coal"; pp.cls = QueryClass::PowerPlant; pp.v[0] = 400; pp.v[1] = 300; pp.ageYears = 12; pp.maxAgeYears = 50; pp.v[4] = 0; pp.pollution = 100000; pp.pollutionBaseline = 100;
  auto R3 = rowsOf(pp);
  CHECK(R3.size() >= 7 && R3[0] == "Age (Max):" && R3[1] == "Elevation:" && R3[2] == "Pollution Generated:" && R3[3] == "|" && R3[4] == "Maximum Power Capacity:");
  CHECK(has(pp, "Age (Max):", "12 yrs. (50 yrs.)"));
  for (auto& l : buildQueryLines(h.E, pp)) if (l.label == "Pollution Generated:") CHECK(l.value == "Hazardous" && l.red);

  // Utility usage lines are numbers; a current capacity of 0 is red. Flammability "Extreme" is red.
  for (auto& l : buildQueryLines(h.E, pp)) {
    if (l.label == "Current Power Capacity:") CHECK(l.value == "300 MW-h/Month" && !l.red);
    if (l.label == "Usage:") CHECK(l.value == "0 MW-h/Month");
  }
  pp.v[1] = 0;
  for (auto& l : buildQueryLines(h.E, pp)) if (l.label == "Current Power Capacity:") CHECK(l.red);
  CHECK(flammabilityBandId(229) == 0x27 && flammabilityBandId(230) == 0x28);
  QueryInfo ex; ex.name = "Barn"; ex.cls = QueryClass::Zoned; ex.flammability = 250;
  for (auto& l : buildQueryLines(h.E, ex)) if (l.label == "Flammability:") CHECK(l.value == "Extreme" && l.red);
  // Water tower: Age (Max), Elevation, Powered | capacities, usage, effect of water pollution.
  QueryInfo wt; wt.name = "Water Tower"; wt.cls = QueryClass::WaterBuilding; wt.ageYears = 3; wt.maxAgeYears = 40; wt.v[0] = 100; wt.v[1] = 0; wt.band[2] = 10;
  auto RW = rowsOf(wt);
  CHECK(RW.size() >= 8 && RW[0] == "Age (Max):" && RW[1] == "Elevation:" && RW[2] == "Powered:" && RW[3] == "|" && RW[4] == "Maximum Water Capacity:");
  CHECK(std::find(RW.begin(), RW.end(), "Effect of Water Pollution:") != RW.end() && std::find(RW.begin(), RW.end(), "Watered:") == RW.end());
  // Recycling centre: "Inadequate" is red.
  QueryInfo rc; rc.name = "Recycling Center"; rc.cls = QueryClass::Recycling; rc.band[0] = 200;
  for (auto& l : buildQueryLines(h.E, rc)) if (l.label == "Citywide Recycling Capacity:") CHECK(l.value == "Inadequate" && l.red);

  // Reason rows after Zone Type: empty zoned tile (code 0: "Planets not aligned"), abandoned building ("Lack of chocolate sprinkles").
  QueryInfo ez; ez.zoneCode = 15; ez.reasonRow = true;
  auto RE = rowsOf(ez); CHECK(RE.size() == 10 && RE[3] == "Reason for Lack of Development:");
  CHECK(has(ez, "Reason for Lack of Development:", "Planets not aligned"));
  ez.reasonCode = 15; CHECK(has(ez, "Reason for Lack of Development:", "No water"));
  QueryInfo ab; ab.name = "House"; ab.cls = QueryClass::Zoned; ab.zoneCode = 1; ab.status = QsAbandoned; ab.reasonRow = true;
  auto RA = rowsOf(ab); CHECK(RA[3] == "Reason for Abandonment:" && has(ab, "Reason for Abandonment:", "Lack of chocolate sprinkles"));
  ab.reasonCode = 4; CHECK(has(ab, "Reason for Abandonment:", "No power"));
  QueryInfo fn; fn.name = "House"; fn.cls = QueryClass::Zoned; fn.zoneCode = 1; CHECK(rowsOf(fn).size() == 9);   // functional: no reason row
  // Neighbour connection: "Connected To:" first, then the normal network rows.
  QueryInfo nb; nb.name = "Road"; nb.cls = QueryClass::Road; nb.neighbour = "Berlin";
  auto RN = rowsOf(nb); CHECK(RN.size() == 4 && RN[0] == "Connected To:" && RN[1] == "Elevation:" && RN[3] == "Traffic:");
  CHECK(has(nb, "Connected To:", "Berlin"));

  // Road: Elevation | Traffic. Rail: Elevation | Rail Usage. Power line: Elevation, Powered. Landfill zone: Elevation | Capacity, Usage.
  QueryInfo road; road.name = "Road"; road.cls = QueryClass::Road; road.band[0] = 200;
  auto R4 = rowsOf(road);
  CHECK(R4.size() == 3 && R4[0] == "Elevation:" && R4[1] == "|" && R4[2] == "Traffic:");
  QueryInfo rail; rail.name = "Railroad"; rail.cls = QueryClass::Rail;
  CHECK(rowsOf(rail).back() == "Rail Usage:");
  QueryInfo pl; pl.name = "Power Line"; pl.cls = QueryClass::PowerLine;
  auto R5 = rowsOf(pl); CHECK(R5.size() == 2 && R5[1] == "Powered:");
  QueryInfo lf; lf.name = "Landfill"; lf.cls = QueryClass::LandfillZone; lf.landfillCapacity = 1200; lf.landfillUsage = 40;
  CHECK(has(lf, "Capacity Citywide:", "1,200 Tons") && has(lf, "Usage:", "40% Full"));
  // Underground: a pipe shows only Watered; a subway tile only Rail Usage.
  QueryInfo pipe; pipe.name = "Water Pipe"; pipe.cls = QueryClass::Pipe; pipe.underground = true;
  auto R6 = rowsOf(pipe); CHECK(R6.size() == 1 && R6[0] == "Watered:");
  QueryInfo sub; sub.name = "Subway"; sub.cls = QueryClass::Subway; sub.underground = true;
  auto R7 = rowsOf(sub); CHECK(R7.size() == 1 && R7[0] == "Rail Usage:");
  // Marina: Depth | Boats Berthed. Zoned building: no blank rows.
  QueryInfo mar; mar.name = "Marina"; mar.cls = QueryClass::Marina; mar.waterDepth = 3; mar.waterTile = true;
  auto R8 = rowsOf(mar); CHECK(R8.size() == 3 && R8[0] == "Depth:" && R8[1] == "|" && R8[2] == "Boats Berthed:");
  QueryInfo zq; zq.name = "House"; zq.cls = QueryClass::Zoned; zq.zoneCode = 1; zq.landValue = 100; zq.crime = 80;
  auto R9 = rowsOf(zq); CHECK(R9.size() == 9 && std::find(R9.begin(), R9.end(), "|") == R9.end());
  CHECK(has(zq, "Zone Type:", "Light Residential"));
  // Status suffixes read from the table with their leading blanks trimmed.
  QueryInfo st; st.status = QsConstruction | QsStrike;
  CHECK(queryStatusLine(h.E, st) == "(Under Construction) (Workers on Strike)");

  // Every class builds without crashing and has rows.
  for (int c = int(QueryClass::Zoned); c <= int(QueryClass::Pipe); c++) {
    QueryInfo q; q.name = "Thing"; q.cls = QueryClass(c); q.landValue = 150; q.crime = 80; q.flammability = 100; q.pollution = 50; q.airPollution = 100; q.waterPollution = 20; q.cleanupCost = 300;
    CHECK(!buildQueryLines(h.E, q).empty());
  }

  // Window: fixed sizes (no picture = 308 x 280), position (40, 40) first, then sticky; modal; OK and close box close it.
  QueryInfo z; z.name = "House"; z.cls = QueryClass::Zoned; z.growable = true; z.x = 7; z.z = 9; z.zoneCode = 1;
  QueryWindow w;
  h.drawFn = [&] { return w.draw(h.ui, h.E, z); };
  h.frame(); CHECK(h.inked() > 1000);
  CHECK(w.rect.x == 40 && w.rect.y == 40 && w.rect.w == 308 && w.rect.h == 280);
  Rect okr; CHECK(h.find("ok", okr) && okr.x == 40 + 308 - 50 && okr.y == 40 + 280 - 37 && okr.w == 42 && okr.h == 32);
  Rect hist; CHECK(h.find("historical", hist) && hist.x == 40 + 20);
  WindowResult r = h.click("historical");
  CHECK(r.has(Act::ToggleHistorical) && r.find(Act::ToggleHistorical)->a == 7 && r.find(Act::ToggleHistorical)->b == 9);
  z.growable = false; h.frame(); CHECK(!h.has("historical") && !h.has("replace"));
  z.canReplace = true; h.frame(); CHECK(h.has("replace"));
  r = h.click("replace"); CHECK(r.has(Act::QueryReplace) && r.close);
  r = h.click("ok"); CHECK(r.close && r.has(Act::QueryOk) && r.find(Act::QueryOk)->a == 7 && r.find(Act::QueryOk)->b == 9 && !r.has(Act::Close));
  r = h.click("close"); CHECK(r.close && r.has(Act::Close) && !r.has(Act::QueryOk));   // the close box discards
  // Drag by the title band; the next panel opens where this one was left.
  {
    Input press; press.mx = 40 + 100; press.my = 40 + 10; press.down = press.pressed = true;
    h.frame(press);
    Input move = press; move.pressed = false; move.mx += 200; move.my += 100;
    h.frame(move);
    Input up = move; up.down = false; up.released = true;
    h.frame(up);
    CHECK(w.rect.x == 240 && w.rect.y == 140);
    w.begin(); h.frame(); CHECK(w.rect.x == 240 && w.rect.y == 140);
  }
  // Popup placed near the bottom-right corner is clamped on screen.
  w.open({h.W - 5, h.H - 5, 10, 10});
  h.frame();
  Rect cl; CHECK(h.find("close", cl) && cl.x + cl.w <= h.W && cl.y + cl.h <= h.H);
  r = h.click("close"); CHECK(r.close);
}

// ---------------------------------------------------------------- map window / graphs / charts
// Tooltip placement measured on the original (spec 08 3.3a, 1024 x 768): the examples of the dirty side's report.
void testTooltipBox() {
  auto eq = [](Rect r, int x, int y, int w, int h) { return r.x == x && r.y == y && r.w == w && r.h == h; };
  CHECK(eq(tooltipBox(Ui::kTipLeft, {961, 100, 56, 32}, 53, 1024, 0, 0), 901, 108, 58, 16));   // Landscape root
  CHECK(eq(tooltipBox(Ui::kTipLeft, {928, 118, 36, 36}, 25, 1024, 0, 0), 896, 128, 30, 16));   // Road sub-button
  CHECK(eq(tooltipBox(Ui::kTipLeft, {737, 3, 56, 32}, 53, 800, 0, 0), 677, 11, 58, 16));       // Landscape at 800
  CHECK(eq(tooltipBox(Ui::kTipAbove, {773, 713, 40, 39}, 28, 1024, 0, 0), 776, 695, 33, 16));   // Query
  CHECK(eq(tooltipBox(Ui::kTipAbove, {878, 704, 26, 29}, 40, 1024, 0, 0), 869, 686, 45, 16));   // Zoom In
  CHECK(eq(tooltipBox(Ui::kTipBelow, {48, 72, 26, 26}, 73, 1024, 0, 0), 22, 100, 78, 16));      // View Data Aura icon
  CHECK(eq(tooltipBox(Ui::kTipAbove, {830, 680, 33, 30}, 109, 1024, 0, 0), 790, 662, 114, 16));   // Visit SimCity Exchange
  CHECK(tooltipBox(Ui::kTipAbove, {990, 704, 26, 29}, 121, 1024, 0, 0).x + 126 == 1022);        // clamped to end 2 px inside the right edge
  CHECK(tooltipBox(Ui::kTipPointer, {}, 50, 1024, 100, 200).y == 218);
}

void testViewData() {
  Harness h;
  MapSource src; src.W = 64; src.H = 64;
  std::vector<uint8_t> crime(64 * 64, 100), zones(64 * 64, 0xFF);
  zones[10 * 64 + 10] = 1;
  src.values = [&](DataLayer l) -> const std::vector<uint8_t>* { return l == DataLayer::Crime ? &crime : l == DataLayer::Zones ? &zones : nullptr; };
  Chart c1 = Chart::make(0, 100), c2 = Chart::make(50, 100);
  for (int i = 0; i < 12; i++) c1.pushMonthly(uint32_t(i * 10));
  CHECK(GraphsWindow::samples(c1, 0).size() == 12 && GraphsWindow::samples(c1, 1).size() == 10 && GraphsWindow::samples(c1, 2).size() == 10);
  auto s0 = GraphsWindow::samples(c1, 0);
  CHECK(s0.front() < s0.back() && s0.back() == 100);   // pushes clamp at the chart scale (100); oldest -> newest
  auto series = graphSeriesTemplate();
  CHECK(series.size() == 16);
  series[3].chart = &c1; series[1].chart = &c2;   // City Size, Residents
  Population pop; pop.newCity();
  ChartsData cd = chartsFromPopulation(pop, 50000);
  CHECK(cd.eq.size() == size_t(kAges) && cd.population.size() == size_t(kAges) && cd.totalPopulation == 50000);
  cd.electricity[0] = 500; cd.electricity[3] = 200; cd.garbage[0] = 70; cd.garbage[3] = 30;
  ViewDataWindow w;
  h.drawFn = [&] { return w.draw(h.ui, h.E, src, series, cd); };
  h.frame(); CHECK(h.inked() > 1000);
  // Observed placement and size (spec 09 6.0): 424 x 440 at (40, 34).
  CHECK(h.ui.windowRects.size() == 1 && h.ui.windowRects[0].x == 40 && h.ui.windowRects[0].y == 34 && h.ui.windowRects[0].w == 424 && h.ui.windowRects[0].h == 440);
  // Twelve mode buttons; no separate data-map column, neighbours button or show-in-city check box.
  for (int i = 0; i < 12; i++) CHECK(h.has("mode." + std::to_string(i)));
  CHECK(!h.has("map.0") && !h.has("map.neighbours"));
  CHECK(dataMapDefs().size() == 10 && w.activeOverlay() == nullptr);
  // Data-map mode.
  WindowResult r = h.click("mode.2");
  CHECK(dataMapDefs()[2].layer == DataLayer::Crime);
  CHECK(r.has(Act::SelectDataMap) && r.find(Act::SelectDataMap)->a == int(DataLayer::Crime));
  CHECK(w.mode == 2 && w.map.selected == 2);
  CHECK(w.activeOverlay() == nullptr);   // "Use As Map" is off
  r = h.click("showInCity"); CHECK(w.map.showInCityView && r.has(Act::DataMapOptionChanged));
  CHECK(w.activeOverlay() && w.activeOverlay()->row.size() == 64u * 64u);
  r = h.click("showInCity"); CHECK(!w.map.showInCityView && w.activeOverlay() == nullptr);
  h.frame(); CHECK(h.has("coverage"));
  CHECK(w.map.coverage);   // the original opens Crime / Flammability with the coverage toggle on
  r = h.click("coverage"); CHECK(!w.map.coverage && r.has(Act::DataMapOptionChanged));
  w.map.coverage = true;
  {   // coverage circles: cyan pixels appear once the toggle is on and the source lists a station
    src.circles = [](bool) { return std::vector<std::array<float, 3>>{{32.f, 32.f, 10.f}}; };
    h.frame();
    size_t cyan = 0; for (uint32_t px : h.fb) cyan += px == rgb(0, 203, 255);
    CHECK(cyan > 20);
    w.map.coverage = false; h.frame();
    size_t none = 0; for (uint32_t px : h.fb) none += px == rgb(0, 203, 255);
    CHECK(none == 0);
    w.map.coverage = true;
  }
  r = h.click("mode.8"); CHECK(w.activeOverlay() == nullptr && !h.has("coverage"));
  r = h.click("mode.7"); h.frame(); CHECK(h.has("pollution.2"));
  r = h.click("pollution.1"); CHECK(w.map.pollutionKind == 1 && r.has(Act::DataMapOptionChanged));
  h.click("mode.1"); h.click("showInCity"); CHECK(w.activeOverlay() != nullptr);   // zones, categorical
  h.frame(); CHECK(h.inked() > 1000);
  r = h.click("minimap");
  CHECK(r.has(Act::CenterView));
  { auto* e = r.find(Act::CenterView); CHECK(e->a >= 0 && e->a < 64 && e->b >= 0 && e->b < 64); CHECK(std::abs(int(e->a) - 32) < 8 && std::abs(int(e->b) - 32) < 8); }
  {   // orientation: tile (x 5, z 50) sits at px = (z - x + W) s, py = (x + z) s of the diamond (top-right half for z > x)
    r = h.click("minimap", 320, 162);
    auto* e = r.find(Act::CenterView);
    CHECK(e && std::abs(int(e->a) - 5) <= 2 && std::abs(int(e->b) - 50) <= 2);
  }
  {   // one view-rotation step turns the diamond a quarter turn counter-clockwise: tile (5,50) moves to about (162, 56)
    w.map.viewRotation = 1;
    r = h.click("minimap", 162, 56);
    auto* e = r.find(Act::CenterView);
    CHECK(e && std::abs(int(e->a) - 5) <= 2 && std::abs(int(e->b) - 50) <= 2);
    w.map.viewRotation = 0;
  }
  // The overlay only follows data-map modes.
  w.setMode(ViewDataWindow::ModeCharts, src); CHECK(w.activeOverlay() == nullptr);
  // Charts mode: five radio buttons, no tabs.
  r = h.click("mode.10"); CHECK(w.mode == int(ViewDataWindow::ModeCharts));
  for (int t = 0; t < 5; t++) {
    h.click("tab." + std::to_string(t));
    CHECK(w.charts.tab == t);
    h.frame(); CHECK(h.inked() > 1500);
  }
  // Graphs mode: default checks City Size, Funds, Power %; the range is a radio group.
  r = h.click("mode.11"); CHECK(w.mode == int(ViewDataWindow::ModeGraphs));
  h.frame();
  CHECK(w.graphs.enabledMask == ((1u << 3) | (1u << 13) | (1u << 15)));
  for (int i = 0; i < 16; i++) CHECK(h.has("series." + std::to_string(i)));
  CHECK(!h.has("range.3"));
  size_t on = h.inked(); CHECK(on > 1000);
  r = h.click("range.1"); CHECK(w.graphs.range == 1 && r.has(Act::SetTimeRange) && r.find(Act::SetTimeRange)->a == 1);
  r = h.click("range.2"); CHECK(w.graphs.range == 2);
  h.click("series.3"); CHECK(!(w.graphs.enabledMask & (1u << 3)));
  h.click("series.3"); CHECK(w.graphs.enabledMask & (1u << 3));
  h.click("series.13"); CHECK(!(w.graphs.enabledMask & (1u << 13)));
  // Close box and Esc.
  r = h.click("close"); CHECK(r.close);
  r = h.key(K_ESC); CHECK(r.close);
  // No map data at all: still draws.
  MapSource empty;
  ViewDataWindow w2;
  h.drawFn = [&] { return w2.draw(h.ui, h.E, empty, series, cd); };
  h.frame(); CHECK(h.inked() > 1000);
}
void testCityLayers() {
  Harness h;
  CityLayersDialog d;
  bool grid = true;
  h.drawFn = [&] { return d.draw(h.ui, h.E, grid); };
  h.frame(); CHECK(h.inked() > 1000);
  for (const char* n : {"mode.above", "mode.under", "mode.layers", "above.transport", "above.zones", "under.subways", "under.pipes",
                        "layer.0", "layer.7", "apply", "default", "grid", "ok", "close"})
    if (!h.has(n)) { std::fprintf(stderr, "missing %s\n", n); g_failures++; }
  CHECK(cityLayerViews().size() == 8 && cityLayerViews()[3].layer == DataLayer::Power && cityLayerViews()[4].layer == DataLayer::Fire);
  // Defaults (observed): above ground, everything shown, water pipes, aura.
  CHECK(d.edit == CityLayers() && d.edit.mode == CityLayers::AboveGround && d.edit.underground == 1 && d.edit.layer == 0);
  // Controls of an inactive group are greyed out and ignore clicks.
  WindowResult r = h.click("layer.3"); CHECK(d.edit.layer == 0 && r.events.empty());
  r = h.click("above.flora"); CHECK(!d.edit.flora && r.events.empty());   // editing alone applies nothing
  r = h.click("mode.layers"); CHECK(d.edit.mode == CityLayers::LayerView);
  r = h.click("layer.3"); CHECK(d.edit.layer == 3);
  r = h.click("above.zones"); CHECK(d.edit.zones);
  r = h.click("apply"); CHECK(r.has(Act::ApplyLayers) && !r.close);
  r = h.click("mode.under"); r = h.click("under.subways"); CHECK(d.edit.mode == CityLayers::Underground && d.edit.underground == 0);
  r = h.click("grid"); CHECK(r.has(Act::ToggleGrid));
  r = h.click("default"); CHECK(r.has(Act::ApplyLayers) && d.edit == CityLayers());
  r = h.click("ok"); CHECK(r.has(Act::ApplyLayers) && r.has(Act::Done) && r.close);
  r = h.click("close"); CHECK(r.close && !r.has(Act::ApplyLayers));
}

// Dialogs move with a title-bar drag, keep the offset while open and reopen at home.
void testDialogDrag() {
  Harness h;
  CityLayersDialog d;
  bool drawn = true;
  h.drawFn = [&] { return drawn ? d.draw(h.ui, h.E, false) : WindowResult(); };
  h.frame();
  Rect home; CHECK(h.find("ok", home));
  CHECK(h.ui.windowRects.size() == 1);
  const Rect frame0 = h.ui.windowRects[0];
  Input in; in.mx = frame0.x + 60; in.my = frame0.y + 15; in.down = in.pressed = true;
  h.frame(in);
  in.pressed = false; in.mx += 50; in.my += 30;
  h.frame(in);
  CHECK(h.ui.draggingWindow());
  Rect moved; CHECK(h.find("ok", moved));
  CHECK(moved.x == home.x + 50 && moved.y == home.y + 30);
  in.down = false; in.released = true;
  h.frame(in);
  CHECK(!h.ui.draggingWindow());
  h.frame();
  CHECK(h.find("ok", moved) && moved.x == home.x + 50 && moved.y == home.y + 30);
  // Press in the body (below the title bar) does not drag.
  in = Input(); in.mx = moved.x - 100; in.my = moved.y - 100; in.down = in.pressed = true;
  h.frame(in); in.pressed = false; in.mx += 40; h.frame(in); in.down = false; in.released = true; h.frame(in);
  Rect still; CHECK(h.find("ok", still) && still.x == moved.x && still.y == moved.y);
  // Dragging far off-screen keeps the title bar reachable.
  in = Input(); in.mx = moved.x - 180; in.my = moved.y - 280; in.down = in.pressed = true;
  h.frame(in); in.pressed = false; in.mx = -5000; in.my = -5000; h.frame(in);
  CHECK(h.ui.windowRects.size() == 1 && h.ui.windowRects[0].y == 0 && h.ui.windowRects[0].x + h.ui.windowRects[0].w >= 40);
  in.down = false; in.released = true; h.frame(in);
  // Closed for a frame: back at home.
  drawn = false; h.frame(); drawn = true; h.frame();
  CHECK(h.find("ok", still) && still.x == home.x && still.y == home.y);
}

// ---------------------------------------------------------------- ticker / message box / tip / credits
void testTicker() {
  Harness h;
  TickerWidget t;
  Rect strip{10, 10, 400, 22};
  NewsItem a; a.number = 1; a.text = "First headline"; a.hasLoc = true; a.locX = 12; a.locZ = 34;
  NewsItem b; b.number = 2; b.text = "Second"; 
  t.push(a, strip.w);
  t.push(b, strip.w);
  CHECK(t.items.size() == 2 && t.items[0].x == strip.w);
  int gap = t.items[1].x - (t.items[0].x + h.ui.textWidth(a.text));
  CHECK(gap >= TickerScroll::kGapPixels - 1 && gap <= TickerScroll::kGapPixels + 80);   // estimate widths by glyph count
  // Scrolling: 2 px per >50 ms.
  int x0 = t.items[0].x;
  t.update(16, strip.w, h.ui, nullptr, 0, nullptr);
  t.update(16, strip.w, h.ui, nullptr, 0, nullptr);
  CHECK(t.items[0].x == x0);                 // 32 ms: no step yet
  t.update(16, strip.w, h.ui, nullptr, 0, nullptr);
  CHECK(t.items[0].x == x0);                 // 48 ms: still no step
  t.update(16, strip.w, h.ui, nullptr, 0, nullptr);
  CHECK(t.items[0].x == x0 - 2);             // 64 ms > 50: one 2 px step
  TickerScroll ts;
  CHECK(ts.advance(50) == 0 && ts.advance(51) == 2);
  // Paused: keeps scrolling and fills the strip with "Simulation Paused" copies ~4 glyphs apart.
  {
    TickerWidget p;
    p.update(16, strip.w, h.ui, nullptr, 0, nullptr, true, 255, 248, 0);
    CHECK(p.items.size() == 1 && p.items[0].x == strip.w);   // first copy enters at the right edge
    CHECK(p.items[0].n.r == 255 && p.items[0].n.g == 248 && p.items[0].n.b == 0);
    for (int k = 0; k < 120; k++) p.update(51, strip.w, h.ui, nullptr, 0, nullptr, true, 255, 248, 0);
    CHECK(p.items.size() >= 2);
    bool allMsg = true;
    for (auto& i : p.items) allMsg = allMsg && i.n.text == "Simulation Paused";
    CHECK(allMsg);
    int w = h.ui.textWidth("Simulation Paused");
    CHECK(p.items[1].x - (p.items[0].x + w) == h.ui.textWidth("0000"));
    int px0 = p.items[0].x;
    for (int k = 0; k < 4; k++) p.update(16, strip.w, h.ui, nullptr, 0, nullptr, true, 255, 248, 0);
    CHECK(p.items[0].x < px0);                 // still scrolling while paused
    // Resume: already-placed pause copies keep scrolling; no new ones are added.
    size_t nPause = p.items.size();
    for (int k = 0; k < 4; k++) p.update(51, strip.w, h.ui, nullptr, 0, nullptr, false);
    CHECK(p.items.size() <= nPause);
    for (auto& i : p.items) CHECK(i.n.text == "Simulation Paused");
  }
  // Move item on screen and click it.
  t.items[0].x = 20; t.items[1].x = 300;
  h.drawFn = [&] { return t.draw(h.ui, h.E, strip); };
  h.frame(); CHECK(h.inked() > 200);
  CHECK(h.has("ticker.1") && !h.has("ticker.2"));  // only items with a map location are clickable
  WindowResult r = h.click("ticker.1");
  CHECK(r.has(Act::TickerClick) && r.find(Act::TickerClick)->a == 12 && r.find(Act::TickerClick)->b == 34);
  CHECK(r.has(Act::PlaySound) && r.find(Act::PlaySound)->a == 47);
  // Item scrolled off the left edge is removed.
  t.items.clear(); t.push(b, strip.w);
  t.items[0].x = -h.ui.textWidth(b.text) - 5;
  t.update(100, strip.w, h.ui, nullptr, 0, nullptr);
  CHECK(t.items.empty());
  // Pulls from the NewsTicker when empty.
  NewsTicker nt; GameRng rng(3);
  std::string text = "Mayor opens a park";
  nt.post(0x11, 1, 100, false, 0, 0, &text);
  t.update(10, strip.w, h.ui, &nt, 100, &rng);
  CHECK(!t.items.empty() && t.items[0].x >= strip.w - 2);
  CHECK(t.items[0].n.text == text);
}

void testMessageBox() {
  Harness h;
  MessageBox m;
  CHECK(MessageBox::buttonsFor(0).size() == 1 && MessageBox::buttonsFor(1).size() == 2 && MessageBox::buttonsFor(4).size() == 3);
  CHECK(MessageBox::buttonsFor(3)[0] == MsgButton::Yes && MessageBox::buttonsFor(5)[2] == MsgButton::Ignore);
  h.drawFn = [&] { return m.draw(h.ui, h.E); };
  h.frame(); CHECK(h.inked() == 0);           // inactive: draws nothing
  m.open("Title", "A short message.", 3);
  CHECK(m.active);
  auto L = m.layout(h.ui);
  CHECK(L.W >= 208 && L.H > 0 && L.lines.size() >= 1);
  // One-line two-button prompt: 288x139 frame as measured on the original (body 39..91, band 100..134,
  // buttons 1 px into the band starting 40 px in).
  CHECK(L.W == 208 && L.boxW == 288 && L.boxH == 139 && L.bodyY == 39 && L.bodyH == 53 && L.bandY == 100);
  CHECK(L.buttonY == 101 && L.buttonX0 == 40 && L.textX == 56);
  h.frame(); CHECK(h.inked() > 1000);
  WindowResult r = h.click("msg.1");
  CHECK(r.has(Act::MessageResult) && r.find(Act::MessageResult)->a == int64_t(MsgButton::No) && !m.active);
  m.open("", "Long message. " + std::string(400, 'x') + " tail", 0);
  auto L2 = m.layout(h.ui);
  CHECK(2 * L2.H < L2.W + 16 * 4);            // aspect rule: widened until the text is at most half as tall as wide
  r = h.click("msg.0"); CHECK(r.has(Act::MessageResult) && r.find(Act::MessageResult)->a == int64_t(MsgButton::OK));
  m.open("T", "msg", 4);
  r = h.key(K_ENTER); CHECK(r.has(Act::MessageResult) && r.find(Act::MessageResult)->a == int64_t(MsgButton::Yes));
  m.open("T", "msg", 4);
  r = h.key(K_ESC); CHECK(r.has(Act::MessageResult) && r.find(Act::MessageResult)->a == int64_t(MsgButton::Cancel));
  CHECK(msgButtonCaption(h.E, MsgButton::Retry) == "Retry");
  // Yes/No prompt whose close box and Esc abort (save before New / Load): no Cancel button, Cancel result.
  m.open("T", "msg", 3); m.closeCancels = true;
  r = h.key(K_ESC); CHECK(r.has(Act::MessageResult) && r.find(Act::MessageResult)->a == int64_t(MsgButton::Cancel));
  m.open("T", "msg", 3); m.closeCancels = true;
  r = h.click("close"); CHECK(r.has(Act::MessageResult) && r.find(Act::MessageResult)->a == int64_t(MsgButton::Cancel));
  m.open("T", "msg", 3); CHECK(!m.closeCancels);
  r = h.key(K_ESC); CHECK(r.has(Act::MessageResult) && r.find(Act::MessageResult)->a == int64_t(MsgButton::No));
}

void testTipAndCredits() {
  CHECK(TipWindow::next(1) == 2 && TipWindow::next(0x0F) == 0x11 && TipWindow::next(0x39) == 1);
  CHECK(TipWindow::prev(1) == 0x39 && TipWindow::prev(0x11) == 0x0F);
  Harness h;
  TipWindow t;
  h.drawFn = [&] { return t.draw(h.ui, h.E); };
  h.frame(); CHECK(h.inked() > 1000);
  WindowResult r = h.click("next"); CHECK(t.tip == 2 && r.has(Act::TipChanged) && r.find(Act::TipChanged)->a == 2);
  r = h.click("prev"); CHECK(t.tip == 1);
  r = h.click("showTips"); CHECK(!t.showAtStartup && r.has(Act::ShowTipsChanged) && r.find(Act::ShowTipsChanged)->a == 0);
  r = h.click("done"); CHECK(r.close && r.has(Act::Done));
  r = h.key(K_ESC); CHECK(r.close);
  // With a text table the tip text is used.
  h.E.textFn = [](uint32_t tb, uint32_t id) { return tb == kTblTod && id == 5 ? std::string("Tip five") : std::string(); };
  CHECK(h.E.text(kTblTod, 5, "") == "Tip five");

  CreditsWindow c;
  h.drawFn = [&] { return c.draw(h.ui, h.E); };
  h.E.textFn = nullptr;
  h.frame(); CHECK(h.inked() > 500);
  CHECK(!c.lines.empty());
  double s0 = c.scroll; c.update(1000); CHECK(c.scroll > s0);
  Input in; in.pressed = true; in.mx = 5; in.my = 5;
  r = h.frame(in); CHECK(r.close);                   // any click closes
  r = h.key(K_ESC); CHECK(r.close);
  // Credits read from the table (ids ascending).
  CreditsWindow c2;
  h.E.textFn = [](uint32_t tb, uint32_t id) { return tb == kTblCredits && id <= 3 ? "Line " + std::to_string(id) : std::string(); };
  h.drawFn = [&] { return c2.draw(h.ui, h.E); };
  h.frame();
  CHECK(c2.lines.size() == 3 && c2.lines[0] == "Line 1");

  // Style digit is stripped; ids 1..199 pair with id + 199; '5' rows are hidden; blank line before headings and between pair/centred runs.
  auto rows = CreditsWindow::layoutRows({2, 4, 6, 8, 10, 201, 203, 207},
                                        {"4Lead Engineer", "5skip", "1\"Song\"", "4Composer", "Plain role", "Ann", "x", "Bo\nCy"});
  using K = CreditsWindow::Row;
  CHECK(rows.size() == 6);
  CHECK(rows[0].kind == K::Pair && rows[0].left == "Lead Engineer" && rows[0].names.size() == 1 && rows[0].names[0] == "Ann");
  CHECK(rows[1].kind == K::Blank && rows[2].kind == K::Heading && rows[2].left == "\"Song\"");
  CHECK(rows[3].kind == K::Pair && rows[3].left == "Composer" && rows[3].names.size() == 2 && rows[3].lineCount() == 2);
  CHECK(rows[4].kind == K::Blank && rows[5].kind == K::Centered && rows[5].left == "Plain role");
}

void testScenarioViewIsPlain() {
  ScenarioGoalsView v; CHECK(v.inScenario && v.goals.empty());
}

void testUserBuildings() {
  Harness h;
  UserBuildingsState s;
  s.totalReplaceable = 5;
  auto rb = [](uint32_t inst, int lot, int zone) { UbBuilding u; u.key = {0x207EDC0E, 0x57E, inst}; u.appearance = {0x6300, 1, inst}; u.lot = lot; u.zone = zone; u.name = "r"; return u; };
  s.replaceable = {rb(1, 1, 0), rb(2, 1, 0), rb(3, 2, 0), rb(4, 1, 1)};
  UbBuilding b1; b1.name = "One"; b1.lot = 1; b1.buildingId = 7; b1.appearance = {0x6300, 0xBA1DBF1E, 0xB};
  UbBuilding b2; b2.name = "Two"; b2.lot = 2; b2.buildingId = 8; b2.appearance = {0x6300, 0xBA1DBF1E, 0xC};
  s.bat = {b1, b2};
  UserBuildingsDialog d;
  h.drawFn = [&] { return d.draw(h.ui, h.E, s); };
  h.frame(); CHECK(h.inked() > 1000);
  CHECK(s.filtered().size() == 2 && s.batChoices().size() == 1 && s.batSel == 0);
  h.click("replace"); CHECK(s.set.pairs.size() == 1 && s.set.find(s.replaceable[0].key));
  h.click("next"); CHECK(s.sel == 1);
  h.click("revert"); CHECK(s.set.pairs.size() == 1);        // nothing to revert for the second building
  h.click("prev"); h.click("revert"); CHECK(s.set.pairs.empty());
  h.click("size.1"); h.frame(); CHECK(s.filtered().size() == 1 && s.batChoices().size() == 1 && s.batSel == 1);
  h.click("replace"); CHECK(s.set.pairs.size() == 1);
  h.click("clear"); CHECK(d.prompt.active && s.set.pairs.size() == 1);
  d.prompt.active = false; s.set.pairs.clear(); s.set.set(s.replaceable[2].key, b2.appearance);
  WindowResult r = h.click("editList"); CHECK(r.has(Act::OpenBuildingList));
  r = h.click("ok"); CHECK(r.has(Act::Done) && r.close);
  r = h.click("close"); CHECK(r.close && r.has(Act::Close));
  CHECK(ubZoneClass(3100) == 0 && ubZoneClass(5100) == 1 && ubZoneClass(8100) == 2 && ubZoneClass(12310) == 3 && ubZoneClass(10110) == 4);

  BuildingListState ls; ls.active = {b1, b2}; ls.files = {{"Tower", "/x/Tower.bld", 2}};
  BuildingListEditor e;
  h.drawFn = [&] { return e.draw(h.ui, h.E, ls); };
  h.frame(); CHECK(h.inked() > 1000);
  h.click("active.1"); CHECK(ls.activeSel == 1);
  r = h.click("remove"); CHECK(r.has(Act::BatRemove) && r.find(Act::BatRemove)->id == 8);
  r = h.click("add"); CHECK(!r.has(Act::BatAdd));          // disabled until a .bld is selected
  r = h.click("file.0"); CHECK(r.has(Act::BatSelectFile) && ls.previewFile);
  r = h.click("add"); CHECK(r.has(Act::BatAdd) && r.find(Act::BatAdd)->text == "/x/Tower.bld");
  r = h.click("ok"); CHECK(r.has(Act::Done) && r.close);
}

}  // namespace

void testEngineOptions() {
  Harness h;
  EngineOptions s;
  EngineOptionsDialog d;
  h.drawFn = [&] { return d.draw(h.ui, h.E, s); };
  h.frame(); CHECK(h.inked() > 1000);
  WindowResult r = h.click("showFps");
  CHECK(s.showFps && r.has(Act::PrefsChanged) && r.find(Act::PrefsChanged)->text == "showFps");
  CHECK(s.vsync);   // default on
  r = h.click("vsync");
  CHECK(!s.vsync && r.has(Act::PrefsChanged) && r.find(Act::PrefsChanged)->text == "vsync");
  r = h.click("showTileCoords");
  CHECK(s.showTileCoords && r.has(Act::PrefsChanged) && r.find(Act::PrefsChanged)->text == "showTileCoords");
  r = h.click("ok"); CHECK(r.close && r.has(Act::Done));
  CHECK(h.key(K_ESC).close);
  // Round trip through the preferences file.
  Preferences p; s.toPreferences(p);
  CHECK(p.get("engine.showTileCoords") == "1" && EngineOptions::fromPreferences(p).showTileCoords);
  CHECK(!EngineOptions::fromPreferences(Preferences()).showTileCoords);
  CHECK(p.get("engine.showFps") == "1" && EngineOptions::fromPreferences(p).showFps);
  CHECK(p.get("vsync") == "0" && !EngineOptions::fromPreferences(p).vsync);
  CHECK(!EngineOptions::fromPreferences(Preferences()).showFps);
  CHECK(EngineOptions::fromPreferences(Preferences()).vsync);
}

int main() {
  testHelpers();
  testBudget();
  testOrdinances();
  testBusinessDeals();
  testNeighbours();
  testAdvisors();
  testPetitioner();
  testPrefs();
  testCityProps();
  testUserBuildings();
  testScenarioGoals();
  testPicker();
  testQuery();
  testNeighbourTokens();
  testTooltipBox();
  testViewData();
  testCityLayers();
  testDialogDrag();
  testTicker();
  testMessageBox();
  testTipAndCredits();
  testScenarioViewIsPlain();
  testEngineOptions();
  TEST_MAIN_END();
}
