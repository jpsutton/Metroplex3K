// Game shell: in-city windows. Builds the window environment, opens/draws every window and applies the
// events the windows report (spec 09 sections 4.2, 6, 7).
#include <algorithm>
#include <cstdio>
#include <memory>

#include "core/game.h"
#include "core/game_scenario.h"
#include "core/overview.h"

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
  const HudSnap& hs = hudNow();   // not the live city: it may be mid-tick on the worker
  E.tc.population = simReady_ || scr_ == Screen::InCity ? hs.pop : 0;
  E.tc.year = scr_ == Screen::InCity ? hs.clockYear : nc_year_;
  if (!hs.neighbourName.empty()) E.tc.neighbour = hs.neighbourName;
  for (int i = 0; i < 4; i++) E.tc.neighbours[i] = hs.neighbourNames[size_t(i)];   // %ANYNEIGHBOR% draws one of the four slots
  E.currency = "$";
  E.spots = spotSink_;
  return E;
}

// ---------------------------------------------------------------- modal handling (spec 09 4.2)

void Game::modalOpened() {
  if (modalDepth_++ == 0) {
    wasPausedBeforeModal_ = paused_;
    paused_ = true;  // UI freeze only — does not raise city pause count
    // Spec 14 §2.3 / Q-AZ: pending/modal count (not player pause); raises city hold.
    if (simReady_) sim_.city().raisePending();
  }
}

void Game::modalClosed() {
  if (modalDepth_ > 0 && --modalDepth_ == 0) {
    paused_ = wasPausedBeforeModal_;
    if (simReady_) sim_.city().clearPending();
  }
}

void Game::syncScreenPending() {
  // Blocking screens while a city is loaded: save/load, new-city, terrain editor, scenario end, …
  const bool want = simReady_ && scr_ != Screen::InCity;
  if (want == screenPending_) return;
  if (want) sim_.city().raisePending();
  else sim_.city().clearPending();
  screenPending_ = want;
}

bool Game::anyBlockingWindow() const {
  // Spec 14 §2.3 / 09 §4.2 (Q-AZ): these raise pending/modal, not the player pause count.
  return showBudget_ || showOrd_ || showDeals_ || showAdv_ || showProps_ || showPicker_ || showTip_ || showPrefs_ || showEngine_ ||
         showQuery_ || showGoals_ || showLayers_ || showBrm_ || showBle_ || msgBox_.active || petView_ != nullptr;
}

void Game::closeAllWindows() {
  closeModelessWindows();
  showBudget_ = showOrd_ = showDeals_ = showAdv_ = showProps_ = showPicker_ = showTip_ = showPrefs_ = showEngine_ = false;
  showBrm_ = showBle_ = false;
  msgBox_.active = false;
}

void Game::closeModelessWindows() {
  showMap_ = showGraphs_ = showCharts_ = showGoals_ = showNeigh_ = showQuery_ = showLayers_ = false;
}

// ---------------------------------------------------------------- opening

void Game::openPicker(uint32_t param) {
  PickerCategory pc;
  if (!pickerCategoryFromParam(param, pc)) return;
  picker_.category = pc; picker_.sel = -1; picker_.scroll = 0;
  buildPickerEntries(pc);
  for (const auto& e : pickerEntries_) if (e.available) { picker_.sel = 0; break; }   // first entry starts selected (observed)
  showPicker_ = true;
}

void Game::openWindow(const std::string& name) {
  if (name == "budget") { budgetWin_.openMain(); showBudget_ = true; }
  else if (name == "budget.income") { budgetWin_.openMain(); budgetWin_.incomeTab = true; showBudget_ = true; }
  else if (name == "budget.loans") { budgetWin_.openMain(); budgetWin_.incomeTab = false; budgetWin_.page = BudgetWindow::Page::Loans; showBudget_ = true; }
  else if (name == "ordinances") { showOrd_ = true; ordWin_.scroll = 0; }
  else if (name == "neighbours" || name == "neighbors") {
    showNeigh_ = true;
    // Region image (spec 11 section 9): the city's heights in the middle, deterministic invented terrain around it,
    // coloured with row 3 of the map ramp 000005E5:21.
    std::array<uint32_t, 12> ramp = neighbourRegionDefaultRamp();
    if (haveData_)
      if (ResourceRef rr = data_.db().find({T_UI_IMAGE, 0x5E5, 0x21})) {
        try {
          Image im = decodeUiImage(rr.data, rr.available, false);
          if (im.w >= 12 && im.h >= 4) for (int k = 0; k < 12; k++) ramp[size_t(k)] = im.px[size_t(3) * im.w + k] | 0xFF000000u;
        } catch (const FormatError&) {}
      }
    const City& cc = sim_.city();
    auto edges = neighbourSeaEdges(world_, [&cc](int x, int z) { return cc.pollution.seaWaterAt(x, z); });
    neighRegion_ = neighbourRegionImage(world_, edges, ramp);
  }
  else if (name == "deals") showDeals_ = true;
  else if (name == "advisors") { advWin_.open(0); advWin_.ordinances = &sim_.city().ordinances; showAdv_ = true; }
  else if (name == "engine") showEngine_ = true;
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
    s.landscapeIcons = schemeIconL_; s.floraIcons = schemeIconF_; s.buildingIcons = schemeIconB_;
    s.landscapePreview = schemePrevL_; s.floraPreview = schemePrevF_; s.buildingPreview = schemePrevB_;
    s.disasters = disastersOn_; s.autoBudget = autoBudget_;
    s.editable = true;
    showProps_ = true;
  }
  else if (name == "goals") showGoals_ = true;
  else if (name == "map") showMap_ = true;
  else if (name == "layers") { layersDlg_.edit = layers_; showLayers_ = true; }
  else if (name == "graphs") showGraphs_ = true;
  else if (name == "charts") showCharts_ = true;
  else if (name == "tip") showTip_ = true;
  else if (name == "userbuildings") openUserBuildings();
  else if (name == "buildinglist") { openUserBuildings(); openBuildingList(); }
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
  // Newest samples of the Graphs-window series (spec 09 s6.1, 10 s6.13). Before the first world cycle of a new city the
  // charts are empty: fall back to population and the city plus its neighbours.
  const auto& wc = c.valves.world.charts;
  ind.citySize = wc[0].w[0] ? wc[0].w[0] : uint32_t(std::max(0, c.population())); ind.citySizeMax = wc[0].w[32];
  ind.commerce = wc[1].w[0]; ind.commerceMax = wc[1].w[32];
  ind.industry = wc[2].w[0]; ind.industryMax = wc[2].w[32];
  ind.unemployment = wc[3].w[0] || wc[3].w[32] ? int(wc[3].w[0]) : std::max(0, std::min(100, c.valves.unemploymentPct()));
  uint64_t np = wc[4].w[0];
  if (np == 0) {
    np = uint64_t(std::max(0, c.population()));
    for (const auto& n : c.neighbours.neighbours) np += n.population;
  }
  ind.nationalPop = uint32_t(std::min<uint64_t>(np, 0xFFFFFFFFu));
  ind.pollution = c.pollution.chart.monthly[0]; ind.pollutionMax = c.pollution.chart.max;
  // Tax-page 1D2 = aura history chart (spec 09 §6.1 Q-AP / 47dc8f9): copy from aura record; do not
  // invent a tax→% mapping. Slider drags do not recompute — month-end aura push advances samples.
  {
    const us::Chart& u = c.aura.chart;
    std::copy(u.monthly, u.monthly + 12, v.auraChart.monthly);
    std::copy(u.yearly, u.yearly + 10, v.auraChart.yearly);
    std::copy(u.decade, u.decade + 10, v.auraChart.decade);
    v.auraChart.max = u.max;
    v.showAuraChart = true;
  }
}

void Game::buildPickerEntries(PickerCategory c) {
  City& city = sim_.city();
  AgentHierarchy hier = hierarchyOf(city);
  pickerEntries_.clear();
  if (c == PickerCategory::Rewards) {
    pickerEntries_ = rewardPickerEntries(advisors_.rewards, &city.budget, &hier, cheatAllRewards_);
  } else if (haveData_) {
    std::function<bool(uint32_t, const OccAttr&)> placed;
    if (c == PickerCategory::Landmarks)
      placed = [this](uint32_t occ, const OccAttr&) {
        for (const auto& o : world_.occupants) if (o.occId == occ) return true;
        return false;
      };
    // Power, water and garbage menus list unlocked types only (spec 22 "Building availability", 09 s6.11); nuclear is hidden while
    // Nuclear Free Zone is enacted. `power to the masses` / `water in the desert` skip the unlock flag.
    std::function<bool(uint32_t, const OccAttr&)> avail;
    if (c == PickerCategory::Landmarks)
      avail = [hier](uint32_t, const OccAttr& a) { return hier.isA(a.agentType, 18000); };
    if (c == PickerCategory::PowerPlants || c == PickerCategory::Water || c == PickerCategory::Garbage) {
      const bool all = c == PickerCategory::PowerPlants ? cheats_.active("power to the masses")
                       : c == PickerCategory::Water ? cheats_.active("water in the desert") : cheats_.active("garbage in, garbage out");
      const bool nfz = city.ordinances.enacted(0xE0D0722E);
      avail = [this, all, nfz](uint32_t, const OccAttr& a) {
        if (nfz && a.agentType == 12115) return false;
        return all || advisors_.buildingUnlocked(int(a.agentType));
      };
    }
    pickerEntries_ = collectPickerEntries(data_, PickerFilter::defaults(c), &city.budget, &hier, avail, placed);
  }
  // Second info column (observed: "Power: 6000 MW-h", plain number) from the utility tunables.
  for (auto& e : pickerEntries_) {
    if (c == PickerCategory::PowerPlants) {
      auto it = city.power.params().plants.find(e.agentType);
      if (it != city.power.params().plants.end() && it->second.capacity > 0)
        e.capacity = subst(str(kTblCatalog, 3).empty() ? std::string("Power: %s MW-h") : str(kTblCatalog, 3), {std::to_string(it->second.capacity)});
    } else if (c == PickerCategory::Water) {
      const us::WaterParams& wp = city.water.params();
      int cap = e.agentType == 12840 ? wp.towerCapa : e.agentType == 12810 ? wp.pumpCapa : e.agentType == 12830 ? wp.desaltCapa : 0;
      if (cap > 0) e.capacity = subst(str(kTblCatalog, 4).empty() ? std::string("Capacity: %s m\xB3/Month") : str(kTblCatalog, 4), {std::to_string(cap)});
    }
  }
}

void Game::buildMapSource(MapSource& src, int pollutionKind) {
  src.W = world_.W; src.H = world_.H;
  auto buf = std::make_shared<std::vector<uint8_t>>();
  src.values = [this, buf, pollutionKind](DataLayer l) -> const std::vector<uint8_t>* {
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
        int kind = pollutionKind >= 0 ? pollutionKind : mapWin_.pollutionKind;
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) {
          int v = kind == 0 ? c.pollution.airAt(x, z) : kind == 1 ? c.pollution.waterAt(x, z) : c.pollution.standingGarbageAt(x, z);
          // Air / water: the cell amount against MaxAir/WaterPolluteForUI (11000), byte = min(255, v * 256 / 11000) (spec 25 9.2
          // recommendation, [OPEN] in the original); garbage keeps its own scale.
          at(x, z) = kind <= 1 ? clampB(v * 256 / 11000) : clampB(v / 128);
        }
        break;
      }
      case DataLayer::Density:
        if (c.valves.effectMap.size() != size_t(W) * H) return nullptr;
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) at(x, z) = c.valves.effectMap[size_t(x) * H + z];
        break;
      case DataLayer::Traffic:   // spec 18 s7.1: the snapshot tile value over road, rail, station and highway (no subway);
                                 // tiles without a requested network keep their terrain colour
        if (!c.traffic) return nullptr;
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) {
          bool net = (c.traffic->flags(x, z) & (TF_ROAD | TF_HIGHWAY | TF_RAIL | TF_STATION | TF_XFER)) != 0;
          at(x, z) = net ? uint8_t(c.traffic->tileValue(x, z, 0x01 | 0x02 | 0x04 | 0x10 | 0x20)) : 0xFF;
        }
        break;
      default: return nullptr;
    }
    return buf.get();
  };
  src.baseColour = [this](int x, int z) -> uint32_t {
    // Land and water come from row 3 of the map ramp image 000005E5:21, column k by height above the water (spec 25 9.2).
    if (!world_.inTiles(x, z)) return 0;
    if (!mapRampLoaded_) {
      mapRampLoaded_ = true;
      mapRamp_ = neighbourRegionDefaultRamp();
      if (haveData_)
        if (ResourceRef rr = data_.db().find({T_UI_IMAGE, 0x5E5, 0x21})) {
          try {
            Image im = decodeUiImage(rr.data, rr.available, false);
            if (im.w >= 12 && im.h >= 4) for (int k = 0; k < 12; k++) mapRamp_[size_t(k)] = im.px[size_t(3) * im.w + k] | 0xFF000000u;
          } catch (const FormatError&) {}
        }
    }
    const int d = int(world_.altAt(x, z)) - int(world_.waterAt(x, z));
    return mapRamp_[size_t(d <= 0 ? 0 : neighbourRegionColourIndex(d))];
  };
}

// ---------------------------------------------------------------- View City Layers (spec 09 7.2)

void Game::applyLayers() {
  const CityLayers& L = layers_;
  ++ropt_.filterEpoch;   // showNet / showOccupant below may change meaning (item list cache key)
  const bool above = L.mode == CityLayers::AboveGround, view = L.mode == CityLayers::LayerView;
  const size_t li = size_t(std::max(0, std::min(int(cityLayerViews().size()) - 1, L.layer)));
  ropt_.underground = L.mode == CityLayers::Underground;
  ropt_.subways = L.underground == 0;
  ropt_.pipes = L.underground == 1;
  // Data layers hide buildings, flora and automata; networks stay (spec 25 9.2). Traffic keeps the zone tint.
  ropt_.flora = above && L.flora;
  ropt_.automata = above && L.transport;
  // The Zones layer is a flat palBasic overlay (buildZoneOverlay); the translucent road tint stays only for the Traffic layer.
  ropt_.zonePaint = view && cityLayerViews()[li].layer == DataLayer::Traffic;
  if (above && (!L.transport || !L.powerLines)) {
    const bool t = L.transport, p = L.powerLines;
    ropt_.showNet = [this, t, p](const NetPiece& np) { return sim_.city().kindOf(np) == uint32_t(NetworkKind::Power) ? p : t; };
  } else ropt_.showNet = nullptr;
  if (above && (!L.zonedBuildings || !L.otherBuildings)) {
    const bool zb = L.zonedBuildings, ob = L.otherBuildings;
    ropt_.showOccupant = [zb, ob](const Occupant& o) { uint32_t k = o.agentType / 1000; return k >= 3 && k <= 9 ? zb : ob; };   // zoned = agent types 3000-9999
  } else if (view) ropt_.showOccupant = [](const Occupant&) { return false; };
  else ropt_.showOccupant = nullptr;
  layerOverlay_ = DataOverlay();
  if (view) refreshLayerOverlay();
}

void Game::refreshLayerOverlay() {
  layerOverlayAge_ = 0;
  bumpCityVisual();
  if (layers_.mode != CityLayers::LayerView || !world_.W) { layerOverlay_ = DataOverlay(); return; }
  const auto& views = cityLayerViews();
  const DataLayer dl = views[size_t(std::max(0, std::min(int(views.size()) - 1, layers_.layer)))].layer;
  MapSource src; buildMapSource(src, 0);   // pollution: the air layer
  const std::vector<uint8_t>* v = src.values ? src.values(dl) : nullptr;
  if (!v) { layerOverlay_ = DataOverlay(); return; }
  layerOverlay_ = makeDataOverlay(dl, *v, world_.W, world_.H);
  // Electric Power colours powered tiles only; unpowered land keeps its terrain colour (spec 25 9.2 [OBSERVED]).
  if (dl == DataLayer::Power) for (auto& r : layerOverlay_.row) if (r != 0) r = 0xFF;
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
  if (r.close || r.has(Act::Close)) {
    showQuery_ = false;
    if (!r.has(Act::QueryOk) && !r.has(Act::QueryReplace)) sound(56);   // the close box plays CloseWindow (spec 09 6.12.2)
  }
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

  // Modeless.
  if (showMap_) {
    MapSource src; buildMapSource(src);
    finish(mapWin_.draw(ui_, E, src), showMap_);
  }
  if (showLayers_) finish(layersDlg_.draw(ui_, E, ropt_.grid), showLayers_);
  if (showGraphs_) {
    auto series = graphSeriesTemplate();
    Chart conv[4];
    auto cv = [](const us::Chart& u, Chart& c) {
      std::copy(u.monthly, u.monthly + 12, c.monthly); std::copy(u.yearly, u.yearly + 10, c.yearly);
      std::copy(u.decade, u.decade + 10, c.decade); c.max = u.max;
    };
    cv(city.pollution.chart, conv[0]); cv(city.crime.chart, conv[1]); cv(city.power.chart, conv[2]); cv(city.water.chart, conv[3]);
    // World layer charts (spec 10 6.13): city size, commerce, industry, unemployment, national population.
    Chart wc[5];
    for (int k = 0; k < 5; k++) {
      const auto& w = city.valves.world.charts[k].w;
      for (int i = 0; i < 12; i++) wc[k].monthly[i] = w[size_t(i)];
      for (int i = 0; i < 10; i++) { wc[k].yearly[i] = w[size_t(12 + i)]; wc[k].decade[i] = w[size_t(22 + i)]; }
      wc[k].max = w[32];
    }
    // Traffic (0x166) = the traffic record's road-traffic history with its graph scale; land value (0x16F) = the land-value
    // record's final 132 bytes (spec 10 s6.12/s6.13): 12 monthly, 10 yearly, 10 decade samples, then the scale.
    Chart trafficChart, lvChart;
    auto fill32 = [](Chart& ch, const uint32_t* h, uint32_t mx) {
      for (int i = 0; i < 12; i++) ch.monthly[i] = h[i];
      for (int i = 0; i < 10; i++) { ch.yearly[i] = h[12 + i]; ch.decade[i] = h[22 + i]; }
      ch.max = mx;
    };
    if (city.traffic) fill32(trafficChart, city.traffic->rec().history, city.traffic->rec().graphMax);
    fill32(lvChart, city.landValue.history.data(), city.landValue.tail);
    if (series.size() >= 14) {
      series[5].chart = &trafficChart;
      series[13].chart = &lvChart;
      series[0].chart = &wc[0]; series[2].chart = &wc[1]; series[3].chart = &wc[2];
      series[7].chart = &wc[3]; series[4].chart = &wc[4];
      series[1].chart = &city.pop.popChart;
      series[6].chart = &conv[0];
      series[8].chart = &conv[1];
      series[9].chart = &conv[2];
      series[10].chart = &conv[3];
      series[11].chart = &city.pop.leChart;
      series[12].chart = &city.pop.eqChart;
    }
    finish(graphsWin_.draw(ui_, E, series), showGraphs_);
  }
  if (showCharts_) {
    ChartsData d = chartsFromPopulation(city.pop, uint32_t(std::max(0, city.population())));
    finish(chartsWin_.draw(ui_, E, d), showCharts_);
  }
  if (showNeigh_) {
    neighWin_.cityName = world_.info.cityName; neighWin_.cityPop = city.population();
    // The SimNation population is the newest sample of the world layer's fifth chart (Berlin: 41,000,660 = the original's value).
    uint64_t np = city.valves.world.charts[4].w[0];
    if (np == 0) {
      np = uint64_t(std::max(0, city.population()));
      for (const auto& nb : city.neighbours.neighbours) np += nb.population;
    }
    neighWin_.nationalPop = np;
    neighWin_.regionMap = neighRegion_.empty() ? nullptr : &neighRegion_;
    neighWin_.viewRotation = cam_.rot;
    finish(neighWin_.draw(ui_, E, city.neighbours), showNeigh_);
  }
  if (showGoals_) {
    ScenarioGoalsView v;
    if (scen_) v = scen_->goalsView(); else v.inScenario = false;
    finish(goalsWin_.draw(ui_, E, v), showGoals_);
  }
  drawQueryPopup();

  // Modal.
  if (showBudget_) {
    BudgetView v; buildBudgetView(v);
    finish(budgetWin_.draw(ui_, E, city.budget, v), showBudget_);
  }
  if (showOrd_) {
    OrdinanceMoneyInput money; money.population = uint32_t(std::max(0, city.population()));
    finish(ordWin_.draw(ui_, E, city.ordinances, money), showOrd_);
  }
  if (showDeals_) {
    finish(dealsWin_.draw(ui_, E, city.business, lastBriefing_), showDeals_);
  }
  if (showAdv_) {
    advWin_.ordinances = &city.ordinances;
    advWin_.money.population = uint32_t(std::max(0, city.population()));
    finish(advWin_.draw(ui_, E, advisors_, petitioners_), showAdv_);
  }
  if (showProps_) {
    finish(propsDlg_.draw(ui_, E, propsState_), showProps_);
  }
  if (showPrefs_) {
    finish(prefsDlg_.draw(ui_, E, prefsState_), showPrefs_);
    if (!showPrefs_) { saveSettings(); applySettingsToAudio(); }
  }
  if (showEngine_) {
    finish(engineDlg_.draw(ui_, E, engineOpts_), showEngine_);
    if (!showEngine_) saveSettings();
  }
  if (showPicker_) {
    finish(picker_.draw(ui_, E, pickerEntries_), showPicker_);
  }
  if (petView_) {
    WindowResult r = petDlg_.draw(ui_, E, *petView_);
    handleWindowResult(r);
    if ((r.close || r.has(Act::Close)) && petView_) petView_.reset();
  }
  if (showTip_) {
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
  for (const Rect& wr : ui_.windowRects) if (ui_.hover(wr)) uiHit_ = true;   // windows register their frames while drawing
  if (ui_.draggingWindow()) uiHit_ = true;   // a window being dragged by its title bar owns the pointer
}

// ---------------------------------------------------------------- results

void Game::handleWindowResult(const WindowResult& r) {
  City& city = sim_.city();
  for (const WindowEvent& e : r.events) {
    switch (e.act) {
      case Act::Close: case Act::Done: case Act::DismissTopic: case Act::SelectAdvisor:
      case Act::SetTimeRange:
      case Act::OpenBuildingList: case Act::BatRemove: case Act::BatAdd: case Act::BatSelectFile: case Act::BatOpenFolder:   // user-building dialogs (game_userbldg.cpp)
        break;
      case Act::OpenBudget: openWindow("budget"); break;
      case Act::OpenOrdinances: openWindow("ordinances"); break;
      case Act::OpenNeighbours: openWindow("neighbours"); break;
      case Act::OpenBusinessDeals: openWindow("deals"); break;
      case Act::OpenMapWindow: openWindow("map"); break;
      case Act::ApplyLayers: layers_ = layersDlg_.edit; applyLayers(); break;
      case Act::ToggleGrid: ropt_.grid = !ropt_.grid; break;
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
        if (audio_ && e.a < 0) audio_->stopMusic();   // preview stop button
        else if (audio_) {
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
        setDisastersOn(s.disasters); autoBudget_ = s.autoBudget;
        world_.info.flagA = disastersOn_ ? 1 : 0; world_.info.flagB = autoBudget_ ? 1 : 0;
        uint32_t L = size_t(s.landscape) < schemeIdsL_.size() ? schemeIdsL_[size_t(s.landscape)] : world_.scheme.landscape;
        uint32_t F = size_t(s.flora) < schemeIdsF_.size() ? schemeIdsF_[size_t(s.flora)] : world_.scheme.floraSet;
        uint32_t B = size_t(s.buildings) < schemeIdsB_.size() ? schemeIdsB_[size_t(s.buildings)] : world_.scheme.buildingSet;
        bool changed = L != world_.scheme.landscape || F != world_.scheme.floraSet || B != world_.scheme.buildingSet;
        world_.scheme.landscape = L; world_.scheme.floraSet = F; world_.scheme.buildingSet = B;
        if (changed && haveData_) {
          data_.unmountSets();
          if (B) data_.mountBuildingSet(B);
          if (F) data_.mountFloraSet(F);
          invalidateCityView();
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
      case Act::OpenUserBuildings:
        openUserBuildings();
        break;
      case Act::MessageResult:
        msgBox_.active = false;
        onMessageResult(MsgButton(e.a));
        break;
      case Act::TipChanged: break;
      case Act::ShowTipsChanged:
        prefsState_.showTips = e.b != 0 || e.a != 0;
        prefsToSettings(prefsState_, general_, prefs_);
        saveSettings();
        break;
      case Act::ToggleHistorical:
        queryInfo_.historical = !queryInfo_.historical;   // only the box changes; OK writes it, the close box discards it
        sound(13);
        break;
      case Act::QueryOk: {
        sound(12);
        const int oi = sim_.occupantAt(e.a, e.b);
        if (oi >= 0 && size_t(oi) < world_.occupants.size() && queryInfo_.growable) {
          Occupant& oc = world_.occupants[size_t(oi)];
          const bool stored = (oc.state & 0x200u) != 0;
          if (stored != queryInfo_.historical) { oc.state ^= 0x200u; dirty_ = true; }   // protected flag [INFERRED], spec 13 3
        }
        break;
      }
      case Act::QueryReplace:
        sound(12);
        // Replace closes the panel and opens the Building Replacement dialog on the queried building's footprint (spec 09 6.12.1).
        showQuery_ = false;
        openUserBuildings();
        brmState_.sizeFilter = std::max(0, std::min(4, queryInfo_.footprint - 1));
        break;
    }
  }
}

}  // namespace m3k
