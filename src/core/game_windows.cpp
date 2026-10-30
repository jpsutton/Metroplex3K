// Game shell: in-city windows. Builds the window environment, opens/draws every window and applies the
// events the windows report (spec 09 sections 4.2, 6, 7).
#include <algorithm>
#include <cstdio>
#include <memory>

#include "core/game.h"
#include "core/game_scenario.h"

namespace m3k {

namespace {
std::string schemeLabel(uint32_t id, const char* kind) {
  if (!id) return std::string("Default ") + kind;
  char b[48]; std::snprintf(b, sizeof b, "%s %08X", kind, id); return b;
}
size_t indexOfId(const std::vector<uint32_t>& v, uint32_t id) {
  auto it = std::find(v.begin(), v.end(), id);
  return it == v.end() ? 0 : size_t(it - v.begin());
}
AgentHierarchy hierarchyOf(const City& c) {
  AgentHierarchy h;
  for (const auto& kv : c.config().parent) h.setParent(kv.first, kv.second);
  return h;
}
}  // namespace

// ---------------------------------------------------------------- environment

WinEnv Game::env() {
  WinEnv E;
  E.data = haveData_ ? &data_ : nullptr;
  E.image = [this](uint32_t g, uint32_t i) { return icon(g, i); };
  E.tc.mayor = world_.info.mayorName.empty() ? nc_mayor_ : world_.info.mayorName;
  E.tc.city = world_.info.cityName.empty() ? nc_name_ : world_.info.cityName;
  E.tc.population = simReady_ || scr_ == Screen::InCity ? sim_.population() : 0;
  E.tc.year = scr_ == Screen::InCity ? sim_.city().clock.date().year : nc_year_;
  const auto& nb = sim_.city().neighbours.neighbours[0];
  if (!nb.cityName.empty()) E.tc.neighbour = nb.cityName;
  E.currency = "$";
  E.spots = spotSink_;
  return E;
}

// ---------------------------------------------------------------- modal handling (spec 09 4.2)

void Game::modalOpened() {
  if (modalDepth_++ == 0) { wasPausedBeforeModal_ = paused_; paused_ = true; }
}

void Game::modalClosed() {
  if (modalDepth_ > 0 && --modalDepth_ == 0) paused_ = wasPausedBeforeModal_;
}

bool Game::anyBlockingWindow() const {
  return showBudget_ || showOrd_ || showDeals_ || showAdv_ || showProps_ || showPicker_ || showTip_ || showPrefs_ ||
         msgBox_.active || petView_ != nullptr;
}

void Game::closeModelessWindows() {
  showMap_ = showGraphs_ = showCharts_ = showGoals_ = showNeigh_ = showQuery_ = false;
}

// ---------------------------------------------------------------- opening

void Game::openPicker(uint32_t param) {
  PickerCategory pc;
  if (!pickerCategoryFromParam(param, pc)) return;
  picker_.category = pc; picker_.sel = -1; picker_.scroll = 0;
  buildPickerEntries(pc);
  showPicker_ = true;
}

void Game::openWindow(const std::string& name) {
  if (name == "budget") { budgetWin_.openMain(); showBudget_ = true; }
  else if (name == "ordinances") { showOrd_ = true; ordWin_.scroll = 0; }
  else if (name == "neighbours" || name == "neighbors") showNeigh_ = true;
  else if (name == "deals") showDeals_ = true;
  else if (name == "advisors") { advWin_.open(0); advWin_.ordinances = &sim_.city().ordinances; showAdv_ = true; }
  else if (name == "prefs") { prefsState_ = prefsFromSettings(general_, prefs_); applySettingsToAudio(); showPrefs_ = true; }
  else if (name == "props") {
    if (!schemesLoaded_) loadSchemes();
    CityPropsState& s = propsState_;
    s = CityPropsState();
    s.name = world_.info.cityName; s.mayor = world_.info.mayorName;
    for (uint32_t id : schemeIdsL_) s.landscapes.push_back(schemeLabel(id, "Landscape"));
    for (uint32_t id : schemeIdsF_) s.floraSets.push_back(schemeLabel(id, "Flora"));
    for (uint32_t id : schemeIdsB_) s.buildingSets.push_back(schemeLabel(id, "Buildings"));
    s.landscape = int(indexOfId(schemeIdsL_, world_.scheme.landscape));
    s.flora = int(indexOfId(schemeIdsF_, world_.scheme.floraSet));
    s.buildings = int(indexOfId(schemeIdsB_, world_.scheme.buildingSet));
    s.editable = true;
    showProps_ = true;
  }
  else if (name == "goals") showGoals_ = true;
  else if (name == "map") showMap_ = true;
  else if (name == "graphs") showGraphs_ = true;
  else if (name == "charts") showCharts_ = true;
  else if (name == "tip") showTip_ = true;
}

// ---------------------------------------------------------------- view builders

void Game::buildBudgetView(BudgetView& v) {
  City& c = sim_.city();
  MonthEndInput& in = v.in;
  cal::Date d = c.clock.date();
  in.month = d.month; in.year = d.year;
  in.population = uint32_t(std::max(0, c.population()));
  in.avgLV = c.config().synthetic ? 128 : c.landValue.cityAverage();
  in.zoneTotals[0] = int32_t(c.valves.resUnits); in.zoneTotals[1] = int32_t(c.valves.comUnits); in.zoneTotals[2] = int32_t(c.valves.indUnits);
  OrdinanceMoneyInput om; om.population = in.population;
  Ordinances::Money m = c.ordinances.monthlyMoney(om);
  in.ordIncome = m.income; in.ordCost = m.cost;
  NeighbourDeals::Money dm = c.neighbours.monthlyMoney();
  in.dealIncome = dm.income; in.dealCost = dm.cost;
  in.bizIncome = c.business.monthlyIncome();
  in.fares = c.traffic ? double(c.traffic->rec().faresLastMonth) : 0.0;
  IndicatorData& ind = v.ind;
  ind.citySize = std::min(100, int(in.population / 5000));
  uint64_t np = 0;
  for (const auto& n : c.neighbours.neighbours) np += n.population;
  ind.nationalPop = uint32_t(np);
  ind.unemployment = std::max(0, std::min(100, c.pop.unemploymentPercent()));
  ind.pollution = std::min(100, int(c.pollution.avgAir) * 100 / 255);
  ind.commerce = std::min(100, int(in.zoneTotals[1] / 50));
  ind.industry = std::min(100, int(in.zoneTotals[2] / 50));
  v.approval = -1;
}

void Game::buildPickerEntries(PickerCategory c) {
  City& city = sim_.city();
  AgentHierarchy hier = hierarchyOf(city);
  pickerEntries_.clear();
  if (c == PickerCategory::Rewards) {
    pickerEntries_ = rewardPickerEntries(advisors_.rewards, &city.budget, &hier);
  } else if (haveData_) {
    std::function<bool(uint32_t, const OccAttr&)> placed;
    if (c == PickerCategory::Landmarks)
      placed = [this](uint32_t occ, const OccAttr&) {
        for (const auto& o : world_.occupants) if (o.occId == occ) return true;
        return false;
      };
    pickerEntries_ = collectPickerEntries(data_, PickerFilter::defaults(c), &city.budget, &hier, {}, placed);
  }
  for (auto& e : pickerEntries_) if (!e.picture) e.picture = nullptr;
}

void Game::buildMapSource(MapSource& src) {
  src.W = world_.W; src.H = world_.H;
  auto buf = std::make_shared<std::vector<uint8_t>>();
  src.values = [this, buf](DataLayer l) -> const std::vector<uint8_t>* {
    City& c = sim_.city();
    const int W = world_.W, H = world_.H;
    if (!W) return nullptr;
    buf->assign(size_t(W) * H, 0);
    auto at = [&](int x, int z) -> uint8_t& { return (*buf)[size_t(x) * H + z]; };
    auto clampB = [](int v) { return uint8_t(std::max(0, std::min(255, v))); };
    switch (l) {
      case DataLayer::Zones:
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) {
          int zt = world_.zone[size_t(x) * H + z];
          at(x, z) = zt == 0 ? 0xFF : zt <= 4 ? 0 : zt <= 8 ? 1 : zt <= 12 ? 2 : 3;
        }
        break;
      case DataLayer::Aura:
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) at(x, z) = clampB(c.aura.aura(x, z) + 128);
        break;
      case DataLayer::Crime:
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) at(x, z) = clampB(c.crime.crimeAt(x, z));
        break;
      case DataLayer::Fire:
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) at(x, z) = clampB(c.fire.coverageAt(x, z));
        break;
      case DataLayer::LandValue:
        if (c.landValue.lv.empty()) return nullptr;
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) at(x, z) = clampB(c.landValue.landValueAtTile(x, z));
        break;
      case DataLayer::Power:
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) {
          bool any = c.isPowerLine(x, z) || world_.zone[size_t(x) * H + z] != 0 || c.occupantAt(x, z) >= 0;
          at(x, z) = !any ? 0xFF : c.powered(x, z) ? 0 : 1;
        }
        break;
      case DataLayer::Water:
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) {
          bool any = c.isPipe(x, z) || world_.zone[size_t(x) * H + z] != 0 || c.occupantAt(x, z) >= 0;
          at(x, z) = !any ? 0xFF : c.watered(x, z) ? 0 : 1;
        }
        break;
      case DataLayer::Pollution: {
        int kind = mapWin_.pollutionKind;
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) {
          int v = kind == 0 ? c.pollution.airAt(x, z) : kind == 1 ? c.pollution.waterAt(x, z) : c.pollution.standingGarbageAt(x, z);
          at(x, z) = clampB(v / 128);
        }
        break;
      }
      case DataLayer::Density:
        if (c.valves.effectMap.size() != size_t(W) * H) return nullptr;
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) at(x, z) = c.valves.effectMap[size_t(x) * H + z];
        break;
      default: return nullptr;   // Traffic: the traffic simulator exposes no per-tile density yet
    }
    return buf.get();
  };
  src.baseColour = [this](int x, int z) -> uint32_t {
    if (!world_.inTiles(x, z)) return 0;
    int a = world_.altAt(x, z), w = world_.waterAt(x, z);
    if (w > a) return rgb(40, 80, 170);
    int g = std::min(255, 90 + (a - world_.seaLevel) * 2);
    return rgb(uint8_t(g / 2), uint8_t(g), uint8_t(g / 3));
  };
}

// ---------------------------------------------------------------- query popup

void Game::drawQueryPopup() {
  if (!showQuery_) return;
  WinEnv E = env();
  WindowResult r = queryWin_.draw(ui_, E, queryInfo_);
  Rect qr = queryWin_.rect; if (qr.w <= 0) qr = {0, 0, 330, 280};
  uiHit_ = true;   // the query panel is modal: map clicks are ignored until it is closed (spec 09 7a.4)
  (void)qr;
  handleWindowResult(r);
  if (r.close || r.has(Act::Close)) showQuery_ = false;
}

// ---------------------------------------------------------------- drawing

void Game::drawWindows(uint32_t dtMs) {
  (void)dtMs;
  WinEnv E = env();
  City& city = sim_.city();
  auto finish = [&](const WindowResult& r, bool& flag) {
    handleWindowResult(r);
    if (r.close || r.has(Act::Close) || r.has(Act::Done)) flag = false;
  };
  auto hit = [&](int w, int h) { if (ui_.hover(wui::centered(ui_, w, h))) uiHit_ = true; };

  // Modeless.
  if (showMap_) {
    MapSource src; buildMapSource(src);
    hit(640, 440);
    finish(mapWin_.draw(ui_, E, src), showMap_);
  }
  if (showGraphs_) {
    auto series = graphSeriesTemplate();
    Chart conv[4];
    auto cv = [](const us::Chart& u, Chart& c) {
      std::copy(u.monthly, u.monthly + 12, c.monthly); std::copy(u.yearly, u.yearly + 10, c.yearly);
      std::copy(u.decade, u.decade + 10, c.decade); c.max = u.max;
    };
    cv(city.pollution.chart, conv[0]); cv(city.crime.chart, conv[1]); cv(city.power.chart, conv[2]); cv(city.water.chart, conv[3]);
    if (series.size() >= 14) {
      series[1].chart = &city.pop.popChart;
      series[6].chart = &conv[0];
      series[8].chart = &conv[1];
      series[9].chart = &conv[2];
      series[10].chart = &conv[3];
      series[11].chart = &city.pop.leChart;
      series[12].chart = &city.pop.eqChart;
    }
    hit(680, 420);
    finish(graphsWin_.draw(ui_, E, series), showGraphs_);
  }
  if (showCharts_) {
    ChartsData d = chartsFromPopulation(city.pop, uint32_t(std::max(0, city.population())));
    hit(600, 420);
    finish(chartsWin_.draw(ui_, E, d), showCharts_);
  }
  if (showNeigh_) {
    hit(600, 400);
    finish(neighWin_.draw(ui_, E, city.neighbours), showNeigh_);
  }
  if (showGoals_) {
    ScenarioGoalsView v;
    if (scen_) v = scen_->goalsView(); else v.inScenario = false;
    hit(560, 420);
    finish(goalsWin_.draw(ui_, E, v), showGoals_);
  }
  drawQueryPopup();

  // Modal.
  if (showBudget_) {
    BudgetView v; buildBudgetView(v);
    hit(620, 460);
    finish(budgetWin_.draw(ui_, E, city.budget, v), showBudget_);
  }
  if (showOrd_) {
    OrdinanceMoneyInput money; money.population = uint32_t(std::max(0, city.population()));
    hit(520, 400);
    finish(ordWin_.draw(ui_, E, city.ordinances, money), showOrd_);
  }
  if (showDeals_) {
    hit(500, 380);
    finish(dealsWin_.draw(ui_, E, city.business, lastBriefing_), showDeals_);
  }
  if (showAdv_) {
    advWin_.ordinances = &city.ordinances;
    advWin_.money.population = uint32_t(std::max(0, city.population()));
    hit(680, 460);
    finish(advWin_.draw(ui_, E, advisors_, petitioners_), showAdv_);
  }
  if (showProps_) {
    hit(540, 420);
    finish(propsDlg_.draw(ui_, E, propsState_), showProps_);
  }
  if (showPrefs_) {
    hit(620, 470);
    finish(prefsDlg_.draw(ui_, E, prefsState_), showPrefs_);
    if (!showPrefs_) { saveSettings(); applySettingsToAudio(); }
  }
  if (showPicker_) {
    hit(480, 380);
    finish(picker_.draw(ui_, E, pickerEntries_), showPicker_);
  }
  if (petView_) {
    hit(520, 330);
    WindowResult r = petDlg_.draw(ui_, E, *petView_);
    handleWindowResult(r);
    if ((r.close || r.has(Act::Close)) && petView_) petView_.reset();
  }
  if (showTip_) {
    hit(420, 210);
    WindowResult r = tipWin_.draw(ui_, E);
    handleWindowResult(r);
    if (r.close || r.has(Act::Done) || r.has(Act::Close)) showTip_ = false;
  }
  if (msgBox_.active) {
    uiHit_ = true;
    handleWindowResult(msgBox_.draw(ui_, E));
  }

  // Pause the sim while a modal window is open; restore the previous state when the last one closes.
  bool modal = anyBlockingWindow();
  if (modal && modalDepth_ == 0) modalOpened();
  else if (!modal && modalDepth_ > 0) { modalDepth_ = 1; modalClosed(); }
  if (modal) uiHit_ = true;
}

// ---------------------------------------------------------------- results

void Game::handleWindowResult(const WindowResult& r) {
  City& city = sim_.city();
  for (const WindowEvent& e : r.events) {
    switch (e.act) {
      case Act::Close: case Act::Done: case Act::DismissTopic: case Act::SelectAdvisor:
      case Act::SetTimeRange:
        break;
      case Act::OpenBudget: openWindow("budget"); break;
      case Act::OpenOrdinances: openWindow("ordinances"); break;
      case Act::OpenNeighbours: openWindow("neighbours"); break;
      case Act::OpenBusinessDeals: openWindow("deals"); break;
      case Act::OpenMapWindow: openWindow("map"); break;
      case Act::OpenAdvisor: openWindow("advisors"); advWin_.open(e.id); break;
      case Act::OpenPetitioner: {
        const PendingPetition* p = petitioners_.find(e.id);
        if (p) {
          WinEnv E = env();
          petView_ = std::make_unique<PetitionerView>(makePetitionerView(E, petitioners_.config, *p, &E.tc));
          petViewId_ = e.id; petDlg_.impact = false;
        }
        break;
      }
      case Act::SetFunding:
        city.budget.setFunding(e.id, int(e.a));
        if (e.id == dept::Police) city.police.setFunding(int(e.a));
        else if (e.id == dept::Fire) city.fire.setFunding(int(e.a));
        break;
      case Act::SetTax:
        if (e.id < uint32_t(kTaxGroups)) city.budget.setTaxRate(int(e.id), int(e.a));
        break;
      case Act::IssueBond:
        sound(e.b ? 15 : 32);
        break;
      case Act::SetOrdinance:
        advisors_.ordinanceChanged(e.id, e.a != 0);
        dirty_ = true;
        break;
      case Act::AcceptDeal: case Act::DeclineDeal:
        if (petView_ && petView_->isDeal) {
          size_t i = size_t(e.id);
          if (e.act == Act::AcceptDeal) city.neighbours.accept(i);
          else if (i < city.neighbours.pending.size()) city.neighbours.pending.erase(city.neighbours.pending.begin() + long(i));
          petView_.reset();
          dirty_ = true;
        }
        sound(e.act == Act::AcceptDeal ? 15 : 24);
        break;
      case Act::CancelDeal:
        if (e.text != "business") city.budget.dealPenaltyAccumulator += e.a;
        dirty_ = true;
        break;
      case Act::DiscussDeal: {
        auto& pend = city.neighbours.pending;
        for (size_t i = 0; i < pend.size(); i++)
          if (pend[i].neighbourIndex == e.id) {
            petView_ = std::make_unique<PetitionerView>(makeDealOfferView(env(), pend[i], i));
            petDlg_.impact = false;
            break;
          }
        break;
      }
      case Act::AnswerPetition: {
        PetitionEffect eff = petitioners_.answer(e.id, e.a != 0);
        petView_.reset();
        applyPetitionEffect(eff);
        presentPetitions();
        dirty_ = true;
        break;
      }
      case Act::DismissPetitioner:
        petView_.reset();
        break;
      case Act::PrefsChanged:
        prefsToSettings(prefsState_, general_, prefs_);
        applySettingsToAudio();
        break;
      case Act::PreviewMusic:
        if (audio_) {
          int i = 0;
          for (const auto& m : audio_->ini().music)
            if (m.inSongList) { if (i++ == int(e.a)) { audio_->playSong(m.id); break; } }
        }
        break;
      case Act::PlaySound: sound(int(e.a)); break;
      case Act::RestartPrompt: {
        WinEnv E = env();
        msgBox_.open(E.app(0x6F, "Preferences"), E.text(kTblMessage, 18, "Some changes take effect after restarting the game."), 0);
        msgBoxPurpose_ = 1;
        break;
      }
      case Act::ApplyCityProps: {
        const CityPropsState& s = propsState_;
        world_.info.cityName = s.name; world_.info.mayorName = s.mayor;
        uint32_t L = size_t(s.landscape) < schemeIdsL_.size() ? schemeIdsL_[size_t(s.landscape)] : world_.scheme.landscape;
        uint32_t F = size_t(s.flora) < schemeIdsF_.size() ? schemeIdsF_[size_t(s.flora)] : world_.scheme.floraSet;
        uint32_t B = size_t(s.buildings) < schemeIdsB_.size() ? schemeIdsB_[size_t(s.buildings)] : world_.scheme.buildingSet;
        bool changed = L != world_.scheme.landscape || F != world_.scheme.floraSet || B != world_.scheme.buildingSet;
        world_.scheme.landscape = L; world_.scheme.floraSet = F; world_.scheme.buildingSet = B;
        if (changed && haveData_) {
          data_.unmountSets();
          if (B) data_.mountBuildingSet(B);
          if (F) data_.mountFloraSet(F);
          renderer_.invalidateShade();
        }
        dirty_ = true;
        break;
      }
      case Act::PickBuilding:
        if (e.id) {
          setTool(ToolKind::Plop, 0, e.id);
          for (const auto& pe : pickerEntries_) if (pe.occupantId == e.id) { toolName_ = pe.name; break; }
          showPicker_ = false;
        } else message("That building is not available.");
        break;
      case Act::SelectDataMap: {
        MapSource src; buildMapSource(src);
        (void)src;   // MapWindow already rebuilt its overlay from the same source
        break;
      }
      case Act::DataMapOptionChanged: {
        MapSource src; buildMapSource(src);
        mapWin_.select(mapWin_.selected, src);
        break;
      }
      case Act::CenterView: case Act::TickerClick:
        centreOn(int(e.a), int(e.b));
        break;
      case Act::MessageResult:
        msgBox_.active = false;
        msgBoxPurpose_ = 0;
        break;
      case Act::TipChanged: break;
      case Act::ShowTipsChanged:
        prefsState_.showTips = e.b != 0 || e.a != 0;
        prefsToSettings(prefsState_, general_, prefs_);
        saveSettings();
        break;
      case Act::ToggleHistorical:
        queryInfo_.historical = !queryInfo_.historical;
        break;
    }
  }
}

}  // namespace m3k
