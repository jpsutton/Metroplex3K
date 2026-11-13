// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Renders every in-game window headlessly with fake data to PNG files.
// Usage: windows_demo [outdir]   (default ".") -> win_<name>.png
#include <cmath>
#include <functional>
#include <iostream>
#include "core/windows.h"
#include "png.h"
using namespace m3k;

static Fonts g_fonts;
static const int W = 900, H = 680;

static void shot(const std::string& dir, const std::string& name, const std::function<void(Ui&, WinEnv&)>& draw, Input in = Input()) {
  std::vector<uint32_t> fb(size_t(W) * H);
  // Backdrop: a faint green "city" with a grid so clipping against the window edge is visible.
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) fb[size_t(y) * W + x] = ((x / 32 + y / 32) & 1) ? rgb(0x4A, 0x70, 0x48) : rgb(0x52, 0x7A, 0x50);
  Ui ui; ui.fonts = &g_fonts;
  WinEnv E;
  ui.begin(&fb, W, H, in);
  draw(ui, E);
  writePng(dir + "/win_" + name + ".png", W, H, fb);
  std::cout << "wrote win_" << name << ".png\n";
}

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";

  // ---- budget
  Budget b; b.newCity(123456);
  b.dept(dept::Police)->fullCost = 18000; b.dept(dept::Fire)->fullCost = 12000; b.dept(dept::Health)->fullCost = 9000;
  b.dept(dept::Education)->fullCost = 30000; b.dept(dept::Roads)->fullCost = 25000; b.dept(dept::Transit)->fullCost = 4000;
  for (auto& d : b.depts) d.ytd = d.charge() * 4;
  for (int g = 0; g < 3; g++) b.tax[g].ytd = 90000 + g * 30000;
  b.ordCostYTD = 3000; b.ordIncomeYTD = 1500; b.bizYTD = 2000;
  b.issueBond(10000, 4, 2000); b.issueBond(15000, 4, 2000);
  BudgetView bv; bv.in.month = 5; bv.in.year = 2000; bv.in.population = 80000; bv.in.avgLV = 120;
  bv.in.zoneTotals[0] = 4000; bv.in.zoneTotals[1] = 1500; bv.in.zoneTotals[2] = 800; bv.in.ordCost = 800; bv.in.ordIncome = 300;
  bv.ind = {42, 123456789, 55, 30, 7, 20};
  for (int i = 0; i < 12; i++) bv.auraChart.monthly[i] = uint32_t(100 + i * 5);
  bv.auraChart.max = 260; bv.showAuraChart = true;
  { BudgetWindow w; shot(dir, "budget_main", [&](Ui& ui, WinEnv& E) { w.draw(ui, E, b, bv); }); }
  { BudgetWindow w; w.openDept(dept::Police); shot(dir, "budget_dept", [&](Ui& ui, WinEnv& E) { w.draw(ui, E, b, bv); }); }
  { BudgetWindow w; w.openTax(1); shot(dir, "budget_tax", [&](Ui& ui, WinEnv& E) { w.draw(ui, E, b, bv); }); }
  { BudgetWindow w; w.page = BudgetWindow::Page::Loans; shot(dir, "budget_loans", [&](Ui& ui, WinEnv& E) { w.draw(ui, E, b, bv); }); }
  { BudgetWindow w; w.page = BudgetWindow::Page::Indicators; shot(dir, "budget_indicators", [&](Ui& ui, WinEnv& E) { w.draw(ui, E, b, bv); }); }

  // ---- ordinances
  Ordinances ords; ords.newCity(); ords.makeAllAvailable();
  { int n = 0; for (auto& kv : ords.catalogue()) if (n++ % 3 == 0) ords.enact(kv.first); }
  OrdinanceMoneyInput money; money.population = 80000;
  { OrdinancesWindow w; shot(dir, "ordinances", [&](Ui& ui, WinEnv& E) { w.draw(ui, E, ords, money); }); }

  // ---- business deals / neighbours
  BusinessDeals bd;
  BusinessDealDef d1; d1.name = "Casino"; d1.buildingType = 1; d1.value = 1500;
  BusinessDealDef d2; d2.name = "Toxic waste dump"; d2.buildingType = 2; d2.value = 800;
  BusinessDealDef d3; d3.name = "Prison"; d3.buildingType = 3; d3.value = 900;
  bd.defs = {d1, d2, d3}; bd.inForce = {1, 2}; bd.pendingOffer = 2;
  { BusinessDealsWindow w; shot(dir, "business_deals", [&](Ui& ui, WinEnv& E) { w.draw(ui, E, bd, "The treasury is low. Consider a business deal to improve your finances."); }); }
  NeighbourDeals nd; SimRng rng(3); nd.newCity(rng, 128, 128);
  for (int i = 0; i < 4; i++) nd.neighbours[i].cityName = std::string("Neighbortown ") + char('A' + i);
  Deal dl; dl.neighbourName = "Neighbortown B"; dl.neighbourIndex = 1; dl.dealType = 0; dl.quantity = 5000; dl.rate = 10; dl.monthlyAmount = 3500;
  nd.active.push_back(dl); dl.dealType = 4; dl.monthlyAmount = 1200; dl.rate = 12; nd.active.push_back(dl);
  { NeighboursWindow w; w.selected = 1; shot(dir, "neighbours", [&](Ui& ui, WinEnv& E) { w.draw(ui, E, nd); }); }

  // ---- advisors / petitioner
  AdvisorManager am; PetitionerManager pm; am.petitioners = &pm;
  am.list(kAdvBudget).raise(0x101, 0, 5, false, false);
  am.list(kAdvBudget).raise(0x103, 0, 5, false, false);
  am.list(kAdvBudget).raise(0x102, 0, 5, true, false);
  pm.request(0x55, 0, 10, true); pm.request(0x56, 0, 10, true);
  { AdvisorWindow w; w.ordinances = &ords; w.open(kAdvBudget); shot(dir, "advisor", [&](Ui& ui, WinEnv& E) { w.draw(ui, E, am, pm); }); }
  { AdvisorWindow w; w.ordinances = &ords; w.open(kAdvBudget); w.tab = 2; shot(dir, "advisor_ord", [&](Ui& ui, WinEnv& E) { w.draw(ui, E, am, pm); }); }
  { PetitionerView v; v.petition = 1; v.name = "Alice Anderson"; v.heading = "Library funding";
    v.message = "Mayor, the children of this city need more books. Please build a library in our neighbourhood and fund it properly.";
    v.button1 = "Accept"; v.button2 = "Decline"; v.tag = "pet";
    v.impactTitle = "Public Safety Advisor"; v.impactText = "I recommend accepting. A library gives the neighbourhood a place to study and meet, and it costs little to run.";
    PetitionerDialog d; shot(dir, "petitioner", [&](Ui& ui, WinEnv& E) { d.draw(ui, E, v); });
    d.impact = true; shot(dir, "petitioner-impact", [&](Ui& ui, WinEnv& E) { d.draw(ui, E, v); }); }

  // ---- prefs / city props / goals
  PrefsState ps; ps.tracks = {{"Track A", true}, {"Track B", true}, {"Track C", false}, {"Track D", true}};
  { PreferencesDialog d; shot(dir, "prefs", [&](Ui& ui, WinEnv& E) { d.draw(ui, E, ps); }); }
  CityPropsState cp; cp.name = "New Metroplex"; cp.mayor = "Mayor Mayor"; cp.landscapes = {"Temperate", "Desert"}; cp.floraSets = {"Deciduous"}; cp.buildingSets = {"New York"};
  cp.description = "Rolling hills and rivers.";
  { CityPropertiesDialog d; shot(dir, "cityprops", [&](Ui& ui, WinEnv& E) { d.draw(ui, E, cp); }); }
  ScenarioGoalsView gv; gv.title = "Dullsville"; gv.briefing = "Dullsville is a dull town. Bring it to life by growing to 100,000 sims within 20 years without going into debt.";
  gv.goals = {{"Population 100000", false}, {"No debt", true}, {"Crime under 20", false}}; gv.status = "1 of 3 goals met";
  { ScenarioGoalsWindow d; shot(dir, "goals", [&](Ui& ui, WinEnv& E) { d.draw(ui, E, gv); }); }

  // ---- picker / query
  std::vector<PickerEntry> es(7);
  for (size_t i = 0; i < es.size(); i++) { es[i].name = "Power plant " + std::to_string(i); es[i].cost = 100000 * int64_t(i + 1); es[i].sizeX = 4; es[i].sizeZ = 5; }
  es[2].placed = true;
  { BuildingPicker d; d.sel = 1; shot(dir, "picker", [&](Ui& ui, WinEnv& E) { d.draw(ui, E, es); }); }
  QueryInfo q; q.name = "Fire station"; q.cls = QueryClass::Fire; q.v[0] = 12; q.v[1] = 90; q.band[0] = 200; q.landValue = 150; q.crime = 60;
  q.flammability = 80; q.showPower = true; q.powered = true; q.fundingCost = 800; q.zoneCode = 5; q.elevation = 14;
  q.pollution = 30; q.airPollution = 90; q.status = QsStrike; q.growable = true;
  { QueryWindow d; d.open({200, 120, 0, 0}); shot(dir, "query", [&](Ui& ui, WinEnv& E) { d.draw(ui, E, q); }); }

  // ---- maps / graphs / charts
  MapSource src; src.W = 128; src.H = 128;
  std::vector<uint8_t> land(128 * 128), zone(128 * 128, 0xFF);
  for (int x = 0; x < 128; x++) for (int z = 0; z < 128; z++) { land[size_t(x) * 128 + z] = uint8_t(128 + 127 * std::sin(x * 0.1) * std::cos(z * 0.13)); if (x > 40 && x < 90 && z > 30 && z < 100) zone[size_t(x) * 128 + z] = uint8_t((x / 8 + z / 8) % 7); }
  src.values = [&](DataLayer l) -> const std::vector<uint8_t>* { return l == DataLayer::Zones ? &zone : &land; };
  src.baseColour = [](int x, int z) { return ((x + z) % 7 < 2) ? rgb(70, 120, 200) : rgb(100, 150, 80); };
  Chart ch[16];
  for (int i = 0; i < 16; i++) { ch[i] = Chart::make(0, 1000); for (int k = 0; k < 12; k++) ch[i].pushMonthly(uint32_t(500 + 400 * std::sin(k * 0.5 + i))); for (int k = 0; k < 10; k++) ch[i].pushYearly(uint32_t(300 + 60 * k + 20 * i)); for (int k = 0; k < 10; k++) ch[i].pushDecade(uint32_t(100 + 80 * k)); }
  auto series = graphSeriesTemplate();
  for (int i = 0; i < 16; i++) series[size_t(i)].chart = &ch[i];
  Population pop; pop.newCity();
  ChartsData cd = chartsFromPopulation(pop, 80000);
  for (int i = 0; i < 10; i++) cd.electricity[i] = 100 * (i % 4 + 1);
  for (int i = 0; i < 5; i++) cd.garbage[i] = 50 * (i + 1);
  // One View Data window (spec 09 7.1): data maps, charts and graphs are modes of it.
  { ViewDataWindow v; v.setMode(6, src); shot(dir, "map_landvalue", [&](Ui& ui, WinEnv& E) { v.draw(ui, E, src, series, cd); }); }
  { ViewDataWindow v; v.setMode(1, src); shot(dir, "map_zones", [&](Ui& ui, WinEnv& E) { v.draw(ui, E, src, series, cd); }); }
  { ViewDataWindow v; v.setMode(0, src); shot(dir, "map_aura", [&](Ui& ui, WinEnv& E) { v.draw(ui, E, src, series, cd); }); }
  { ViewDataWindow v; v.mode = ViewDataWindow::ModeGraphs; shot(dir, "graphs", [&](Ui& ui, WinEnv& E) { v.draw(ui, E, src, series, cd); }); }
  for (int t = 0; t < 5; t++) { ViewDataWindow v; v.mode = ViewDataWindow::ModeCharts; v.charts.tab = t; shot(dir, "charts" + std::to_string(t), [&](Ui& ui, WinEnv& E) { v.draw(ui, E, src, series, cd); }); }

  // ---- ticker / message box / tip / credits
  { TickerWidget t; NewsItem a; a.number = 1; a.text = "Mayor opens new library"; a.hasLoc = true; a.r = 255; a.g = 255; a.b = 120;
    NewsItem n2; n2.number = 2; n2.text = "Fire at 5th Street"; n2.r = 255; n2.g = 90; n2.b = 90; n2.hasLoc = true;
    Ui tmp; tmp.fonts = &g_fonts;
    t.push(a, 400); t.push(n2, 400);
    for (int i = 0; i < 120; i++) t.update(60, 400, tmp, nullptr, 0, nullptr);
    shot(dir, "ticker", [&](Ui& ui, WinEnv& E) { t.draw(ui, E, {20, 20, 400, 22}); }); }
  { MessageBox m; m.open("Save city?", "The city has been modified. Do you want to save your changes before quitting? This cannot be undone.", 4);
    shot(dir, "msgbox", [&](Ui& ui, WinEnv& E) { m.draw(ui, E); }); }
  { TipWindow t; shot(dir, "tip", [&](Ui& ui, WinEnv& E) { t.draw(ui, E); }); }
  { CreditsWindow c; c.scroll = 150; shot(dir, "credits", [&](Ui& ui, WinEnv& E) { c.draw(ui, E); }); }
  return 0;
}
