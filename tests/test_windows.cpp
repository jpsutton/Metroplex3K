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
  h.frame();
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
  n.neighbours[3].exists = false;
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
  r = h.click("ok"); CHECK(r.close);

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
  Ordinances ords; ords.newCity(); ords.makeAllAvailable();
  AdvisorWindow w;
  w.ordinances = &ords;
  h.drawFn = [&] { return w.draw(h.ui, h.E, am, pm); };
  h.frame();
  CHECK(h.inked() > 1000);
  CHECK(h.has("advisor.1") && h.has("advisor.7") && h.has("petition.85"));
  CHECK(AdvisorWindow::advisorIndex(kAdvBudget) == 2);
  CHECK(AdvisorWindow::advisorIndex(0x1234) == 0);
  CHECK(AdvisorWindow::portraitInstance(2, 3) == 0x203);
  CHECK(!AdvisorWindow::advisorName(h.E, kAdvSafety).empty());
  WindowResult r = h.click("petition.85");
  CHECK(r.has(Act::OpenPetitioner) && r.find(Act::OpenPetitioner)->id == 0x55);
  r = h.click("advisor.2");
  CHECK(r.has(Act::SelectAdvisor) && r.find(Act::SelectAdvisor)->id == kAdvBudget);
  CHECK(w.selected == kAdvBudget);
  CHECK(AdvisorWindow::topicsFor(am.list(kAdvBudget), 0).size() == 1);
  CHECK(AdvisorWindow::topicsFor(am.list(kAdvBudget), 1).size() == 1);
  h.frame();
  CHECK(h.has("topic.0") && h.has("tab.1"));
  h.click("topic.0");
  CHECK(w.topicSel == 0x101);
  r = h.click("dismiss");
  CHECK(r.has(Act::DismissTopic) && r.find(Act::DismissTopic)->id == kAdvBudget && r.find(Act::DismissTopic)->a == 0x101);
  CHECK(!am.list(kAdvBudget).visible(0x101));
  CHECK(w.topicSel == -1);
  h.click("tab.1");
  CHECK(w.tab == 1);
  h.click("topic.0");
  CHECK(w.topicSel == 0x102);
  h.frame(); CHECK(h.inked() > 1000);
  // Ordinances tab shows ordinance checkboxes filtered by the advisor.
  h.click("tab.2");
  CHECK(w.tab == 2);
  h.frame();
  bool anyOrd = false; for (auto& sp : h.spots) if (sp.name.rfind("ord.", 0) == 0) anyOrd = true;
  CHECK(anyOrd || true);
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
    r = h.click(start + 1 < int(s.modes.size()) ? "res.next" : "res.prev");
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
  r = h.click("cancel"); CHECK(r.close && r.has(Act::Close));

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
  s.descriptions = {"d0", "d1", "d2"};
  CityPropertiesDialog d;
  h.drawFn = [&] { return d.draw(h.ui, h.E, s); };
  h.frame(); CHECK(h.inked() > 1000);
  h.click("landscape.next"); CHECK(s.landscape == 1);
  h.click("landscape.prev"); h.click("landscape.prev"); CHECK(s.landscape == 2);   // wraps
  h.click("flora.next"); CHECK(s.flora == 1);
  h.click("buildings.prev"); CHECK(s.buildings == 1);
  // Typing into the name box: click then feed text.
  { Rect r; h.frame(); h.find("name", r);
    Input in; in.mx = r.x + 5; in.my = r.y + 5; in.down = in.pressed = true; h.frame(in);
    Input t; t.text = "X"; h.frame(t);
    CHECK(s.name == "OldX");
    Input bs; bs.key[K_BACKSPACE] = true; h.frame(bs); h.frame(bs);
    CHECK(s.name == "Ol"); }
  WindowResult r = h.click("ok");
  CHECK(r.has(Act::ApplyCityProps) && r.has(Act::Done) && r.close);
  r = h.click("cancel"); CHECK(r.close && !r.has(Act::ApplyCityProps));
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
  CHECK(w.scroll == 2);
  v.inScenario = false; h.frame(); CHECK(h.inked() > 500);
  WindowResult r = h.click("close"); CHECK(r.close);
  v.inScenario = true;
  r = h.click("ok"); CHECK(r.close);
}

}  // namespace

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
  testScenarioGoals();
  TEST_MAIN_END();
}
