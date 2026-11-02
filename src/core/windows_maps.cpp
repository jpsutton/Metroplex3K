// Map / data-view window, graphs and charts (spec 09 section 7).
#include <algorithm>
#include <cmath>
#include "core/skin.h"
#include "core/windows.h"

namespace m3k {

using namespace wui;

// ================================================================ data maps
const std::vector<DataMapDef>& dataMapDefs() {
  static const std::vector<DataMapDef> d = {
    {DataLayer::Aura, 0x111, 0x01, 0x127, 0x129, {}, 0, {}},
    {DataLayer::Zones, 0x11C, 0x19, 0, 0, {0x139, 0x13A, 0x13B, 0x13C, 0x2D5, 0x2D6, 0x2D7}, 0, {}},
    {DataLayer::Crime, 0x112, 0x04, 0x23E, 0x124, {}, 0x2A1, {}},
    {DataLayer::Density, 0x115, 0x10, 0x242, 0x243, {}, 0, {}},
    {DataLayer::Power, 0x113, 0x11, 0, 0, {0x12A, 0x23F}, 0, {}},
    {DataLayer::Fire, 0x114, 0x07, 0x240, 0x241, {}, 0x2A5, {}},
    {DataLayer::LandValue, 0x116, 0x0C, 0x244, 0x245, {}, 0, {}},
    {DataLayer::Pollution, 0x118, 0x0F, 0x246, 0x247, {}, 0, {0x2A2, 0x2A3, 0x2A4}},
    {DataLayer::Traffic, 0x11A, 0x15, 0x248, 0x249, {}, 0, {}},
    {DataLayer::Water, 0x11B, 0x17, 0, 0, {0x136, 0x24A}, 0, {}},
  };
  return d;
}

namespace {
const char* mapFallback(DataLayer l) {
  switch (l) {
    case DataLayer::Aura: return "Mayor rating";
    case DataLayer::Zones: return "Zones";
    case DataLayer::Crime: return "Crime";
    case DataLayer::Density: return "Density";
    case DataLayer::Power: return "Power grid";
    case DataLayer::Fire: return "Fire risk";
    case DataLayer::LandValue: return "Land value";
    case DataLayer::Pollution: return "Pollution";
    case DataLayer::Traffic: return "Traffic";
    case DataLayer::Water: return "Water supply";
  }
  return "";
}
// End-label fallbacks (spec 09 7.2): first label sits at the left end of the ramp, second at the right.
std::pair<const char*, const char*> endLabels(DataLayer l) {
  switch (l) {
    case DataLayer::Aura: return {"Good", "Bad"};
    case DataLayer::Crime: return {"Low", "High"};
    case DataLayer::Density: case DataLayer::LandValue: return {"High", "Low"};
    case DataLayer::Fire: return {"Low", "High"};
    case DataLayer::Pollution: return {"Clean", "Polluted"};
    case DataLayer::Traffic: return {"Light", "Heavy"};
    default: return {"Low", "High"};
  }
}
bool categorical(DataLayer l) { return l == DataLayer::Zones || l == DataLayer::Power || l == DataLayer::Water; }

uint32_t lerpCol(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int s) { return int(float((a >> s) & 255) * (1 - t) + float((b >> s) & 255) * t); };
  return rgb(ch(0), ch(8), ch(16));
}
}  // namespace

uint32_t MapWindow::legendColour(DataLayer l, int category, float t) {
  if (categorical(l)) {
    static const uint32_t pal[8] = {rgb(0, 192, 0), rgb(0, 0, 192), rgb(192, 192, 0), rgb(160, 160, 160), rgb(0, 160, 200), rgb(220, 120, 0), rgb(130, 90, 40), rgb(200, 0, 200)};
    if (l == DataLayer::Power) return category == 0 ? rgb(230, 220, 40) : rgb(70, 70, 90);
    if (l == DataLayer::Water) return category == 0 ? rgb(60, 140, 255) : rgb(150, 110, 70);
    return pal[category & 7];
  }
  t = std::max(0.f, std::min(1.f, t));
  // blue -> green -> yellow -> red ramp
  if (t < 0.33f) return lerpCol(rgb(40, 60, 200), rgb(40, 190, 60), t / 0.33f);
  if (t < 0.66f) return lerpCol(rgb(40, 190, 60), rgb(240, 220, 40), (t - 0.33f) / 0.33f);
  return lerpCol(rgb(240, 220, 40), rgb(220, 30, 30), (t - 0.66f) / 0.34f);
}

void MapWindow::rebuild(const MapSource& src) {
  overlayActive_ = false;
  if (selected < 0 || selected >= int(dataMapDefs().size()) || !src.values || src.W <= 0 || src.H <= 0) return;
  const DataMapDef& def = dataMapDefs()[size_t(selected)];
  const std::vector<uint8_t>* v = src.values(def.layer);
  if (!v || v->size() < size_t(src.W) * size_t(src.H)) return;
  overlay_ = makeDataOverlay(def.layer, *v, src.W, src.H);
  overlayActive_ = showInCityView;
}

void MapWindow::select(int index, const MapSource& src) {
  selected = index;
  rebuild(src);
}

WindowResult MapWindow::draw(Ui& ui, const WinEnv& E, const MapSource& src) {
  WindowResult R;
  int lh = lineHeight(ui);
  Rect r = centered(ui, 640, 440);
  if (frame(ui, E, r, E.win(0x269, "View city layers"))) { R.close = true; R.add(Act::Close); }
  // ---- mini-map (356x189 iso diamond)
  Rect mm{r.x + 12, r.y + 34, 356, 189};
  ui.fill(mm, rgb(20, 24, 50)); ui.frame(mm, false);
  const DataMapDef* def = selected >= 0 && selected < int(dataMapDefs().size()) ? &dataMapDefs()[size_t(selected)] : nullptr;
  const std::vector<uint8_t>* vals = def && src.values ? src.values(def->layer) : nullptr;
  if (src.W > 0 && src.H > 0) {
    for (int py = 0; py < mm.h - 2; py++)
      for (int px = 0; px < mm.w - 2; px++) {
        int a = px * 2 * src.W / (mm.w - 2) - src.W;
        int b = py * 2 * src.H / (mm.h - 2);
        int x = (a + b) / 2, z = (b - a) / 2;
        if (a + b < 0 || b - a < 0 || x < 0 || z < 0 || x >= src.W || z >= src.H) continue;
        uint32_t col = src.baseColour ? src.baseColour(x, z) : rgb(90, 130, 70);
        if (vals && vals->size() >= size_t(src.W) * src.H) {
          uint8_t v = (*vals)[size_t(x) * src.H + z];
          if (categorical(def->layer)) { if (v != 0xFF) col = MapWindow::legendColour(def->layer, v, 0); }
          else col = MapWindow::legendColour(def->layer, 0, float(v) / 255.f);
        }
        ui.at(mm.x + 1 + px, mm.y + 1 + py) = col;
      }
    E.spot("minimap", mm);
    if (ui.clicked(mm) && ui.in.mx > mm.x && ui.in.my > mm.y) {
      int px = ui.in.mx - mm.x - 1, py = ui.in.my - mm.y - 1;
      int a = px * 2 * src.W / (mm.w - 2) - src.W, b = py * 2 * src.H / (mm.h - 2);
      int x = std::max(0, std::min(src.W - 1, (a + b) / 2)), z = std::max(0, std::min(src.H - 1, (b - a) / 2));
      R.add(Act::CenterView, 0, x, z);
    }
  }
  // ---- legend
  int ly = mm.y + mm.h + 10;
  if (def) {
    ui.text(mm.x, ly, E.win(def->titleId, mapFallback(def->layer)), "title12");
    ly += lh + 8;
    if (const Image* li = E.img(0x000005E5, def->legendImage)) ui.blit(*li, mm.x, ly);
    if (categorical(def->layer)) {
      static const char* const zfb[7] = {"Residential", "Commercial", "Industrial", "Government", "Seaports", "Airports", "Landfills"};
      for (size_t i = 0; i < def->categories.size(); i++) {
        int cx = mm.x + int(i % 4) * 90, cy = ly + int(i / 4) * (lh + 6);
        ui.fill({cx, cy + 1, 12, 12}, legendColour(def->layer, int(i), 0));
        std::string fb = def->layer == DataLayer::Zones ? zfb[i] : def->layer == DataLayer::Power ? (i ? "Unpowered" : "Powered") : (i ? "Unwatered" : "Watered");
        ui.text(cx + 16, cy, E.win(def->categories[i], fb));
      }
    } else {
      for (int i = 0; i < 200; i++) ui.fill({mm.x + i, ly, 1, 12}, legendColour(def->layer, 0, float(i) / 199.f));
      ui.frame({mm.x, ly, 200, 12}, false);
      auto el = endLabels(def->layer);
      ui.text(mm.x, ly + 16, E.win(def->lowId, el.first));
      rightText(ui, mm.x + 200, ly + 16, E.win(def->highId, el.second));
    }
  } else {
    ui.text(mm.x, ly, E.win(0x269, "View city layers"));
  }
  // ---- data-map buttons (right column)
  int bx = r.x + 384, by = r.y + 34;
  for (size_t i = 0; i < dataMapDefs().size(); i++) {
    const auto& d = dataMapDefs()[i];
    if (button(ui, E, "map." + std::to_string(i), {bx, by + int(i) * 28, 104, 26}, E.win(d.titleId, mapFallback(d.layer)), true, selected == int(i))) {
      select(int(i), src);
      R.add(Act::SelectDataMap, 0, int(d.layer));
    }
  }
  if (button(ui, E, "map.neighbours", {bx + 112, by, 104, 26}, E.win(0x117, "Neighbors"))) R.add(Act::OpenNeighbours);
  // ---- options
  int oy = r.y + 34 + 10 * 28 + 10;
  bool v = showInCityView;
  if (check(ui, E, "showInCity", {bx, oy, 240, 18}, E.win(0x186, "Show in city view"), v)) {
    showInCityView = v;
    overlayActive_ = v && selected >= 0 && overlay_.row.size() > 0;
    R.add(Act::DataMapOptionChanged, 0, 0, 0, "showInCity");
  }
  if (def && def->toggleId) {
    v = coverage;
    if (check(ui, E, "coverage", {bx, oy + 22, 240, 18}, E.win(def->toggleId, "Show coverage"), v)) { coverage = v; R.add(Act::DataMapOptionChanged, 0, 0, 0, "coverage"); }
  }
  if (def && !def->selector.empty()) {
    static const char* const sfb[3] = {"Air", "Water", "Garbage"};
    ui.text(bx, oy + 46, E.win(0x2D8, "Show"));
    for (size_t i = 0; i < def->selector.size(); i++)
      if (button(ui, E, "pollution." + std::to_string(i), {bx + 44 + int(i) * 66, oy + 44, 62, 22}, E.win(def->selector[i], sfb[i]), true, pollutionKind == int(i))) {
        pollutionKind = int(i);
        R.add(Act::DataMapOptionChanged, 0, int(i), 0, "pollution");
      }
  }
  if (selected >= 0) rebuild(src);   // keep the overlay current with live layer data
  return R;
}

// ================================================================ View City Layers (7.2)
const std::vector<CityLayerView>& cityLayerViews() {
  static const std::vector<CityLayerView> v = {
    {DataLayer::Aura, 0x276, "Aura"}, {DataLayer::Crime, 0x277, "Crime"}, {DataLayer::Density, 0x278, "Density"},
    {DataLayer::Power, 0x279, "Electric Power"}, {DataLayer::Fire, 0x27A, "Flammability"},
    {DataLayer::LandValue, 0x27B, "Land Value"}, {DataLayer::Pollution, 0x27C, "Pollution"}, {DataLayer::Traffic, 0x27D, "Traffic"}};
  return v;
}

// Layout (dialog-relative) measured on the original at 800x600: blue bands behind the three group headers and the
// button row, header toggles 132x20 with a 3 px drop shadow, check boxes / radios on 18 / 16 px rows, GZBtn captions,
// the navigator's lattice strip (02F78C6D:22F96E31) as the grid button and the GZBtnDone check mark.
WindowResult CityLayersDialog::draw(Ui& ui, const WinEnv& E, bool gridOn) {
  WindowResult R;
  Rect r = centered(ui, 296, 336);
  const Skin* sk = ui.skin;
  const uint32_t ink = rgb(0x31, 0x38, 0x84), grey = rgb(0x9C, 0xA2, 0xBD), white = rgb(255, 255, 255);
  if (frame(ui, E, r, E.win(0x269, "View City Layers"))) { R.close = true; R.add(Act::Close); }
  blueBand(ui, {r.x + 4, r.y + 39, 142, 34});
  blueBand(ui, {r.x + 150, r.y + 39, 142, 34});
  blueBand(ui, {r.x + 4, r.y + 185, 142, 34});
  blueBand(ui, {r.x + 4, r.y + 256, 288, 76});

  // Group header: a toggle picking the mode. On = white face with an inset outline (pressed), off = lavender face
  // with a navy outline and a dark drop shadow.
  auto header = [&](const std::string& name, int x, int y, const std::string& label, int mode) {
    Rect f{r.x + x, r.y + y, 132, 20};
    E.spot(name, f);
    const bool on = edit.mode == mode, down = ui.hover(f) && ui.in.down;
    if (on || down) {
      ui.fill(f, white);
      ui.frame({f.x + 2, f.y + 2, f.w - 4, f.h - 4}, false);
      ui.fill({f.x + 2, f.y + 2, f.w - 4, 1}, rgb(0x7B, 0x7D, 0xBD)); ui.fill({f.x + 2, f.y + 2, 1, f.h - 4}, rgb(0x7B, 0x7D, 0xBD));
      ui.fill({f.x + 2, f.y + f.h - 3, f.w - 4, 1}, rgb(0x7B, 0x7D, 0xBD)); ui.fill({f.x + f.w - 3, f.y + 2, 1, f.h - 4}, rgb(0x7B, 0x7D, 0xBD));
      ui.fill({f.x + 1, f.y + f.h, f.w, 1}, rgb(0xC6, 0xC7, 0xDE)); ui.fill({f.x + 2, f.y + f.h + 1, f.w, 1}, rgb(0xA5, 0xA6, 0xB5));
    } else {
      ui.fill({f.x + 3, f.y + 3, f.w, f.h}, rgb(0x18, 0x20, 0x39));   // drop shadow
      ui.fill(f, rgb(0xD6, 0xDB, 0xFF));
      ui.fill({f.x, f.y, f.w, 2}, rgb(0xEF, 0xEB, 0xFF)); ui.fill({f.x, f.y, 2, f.h}, rgb(0xEF, 0xEB, 0xFF));
      const uint32_t navy = rgb(0x31, 0x38, 0x84);
      ui.fill({f.x + 2, f.y + 2, f.w - 3, 1}, navy); ui.fill({f.x + 2, f.y + 2, 1, f.h - 3}, navy);
      ui.fill({f.x + 2, f.y + f.h - 2, f.w - 3, 1}, navy); ui.fill({f.x + f.w - 2, f.y + 2, 1, f.h - 3}, navy);
    }
    const int o = on || down ? 1 : 0;
    ui.textCentered({f.x + o, f.y + o + 1, f.w, f.h}, label, "main10", ink);
    if (ui.clicked(f)) edit.mode = mode;
  };
  // Check box (GZCheckBox) / radio (GZRadioBtn) with a label; greyed out while its group's mode is not selected.
  const Image* checkArt = sk ? sk->get("check") : nullptr;
  const Image* radioArt = sk ? sk->get("radio") : nullptr;
  auto control = [&](const std::string& name, bool radio, int x, int y, const std::string& label, bool value, bool enabled) {
    const int box = radio ? 12 : 13;
    Rect hit{r.x + x, r.y + y, box + 4 + ui.textWidth(label, "system9"), box + 2};
    E.spot(name, hit);
    const bool hot = enabled && ui.hover(hit);
    if (!(sk && sk->drawCheck(ui, radio ? radioArt : checkArt, r.x + x, r.y + y, value, enabled, hot, hot && ui.in.down))) {
      ui.fill({r.x + x, r.y + y, box, box}, enabled ? white : rgb(0xDE, 0xDF, 0xEF));
      ui.frame({r.x + x, r.y + y, box, box}, false);
      if (value) ui.fill({r.x + x + 3, r.y + y + 3, box - 6, box - 6}, enabled ? ink : grey);
    }
    ui.text(r.x + x + box + 3, r.y + y + (box - ui.fonts->get("system9").height()) / 2, label, "system9", enabled ? ink : grey);
    return enabled && ui.clicked(hit);
  };

  // ---- Above Ground
  header("mode.above", 8, 45, E.win(0x26A, "Above Ground:"), CityLayers::AboveGround);
  struct Check { const char* name; uint32_t id; const char* fb; bool* v; };
  const Check checks[6] = {{"transport", 0x26E, "Transportation", &edit.transport}, {"powerLines", 0x26F, "Power Lines", &edit.powerLines},
                           {"flora", 0x270, "Flora", &edit.flora}, {"zonedBuildings", 0x271, "Zoned Buildings", &edit.zonedBuildings},
                           {"otherBuildings", 0x272, "Other Buildings", &edit.otherBuildings}, {"zones", 0x273, "Zones", &edit.zones}};
  const bool above = edit.mode == CityLayers::AboveGround;
  for (int i = 0; i < 6; i++)
    if (control(std::string("above.") + checks[i].name, false, 12, 76 + 18 * i, E.win(checks[i].id, checks[i].fb), *checks[i].v, above))
      *checks[i].v = !*checks[i].v;

  // ---- Underground
  header("mode.under", 8, 191, E.win(0x280, "Underground:"), CityLayers::Underground);
  const bool under = edit.mode == CityLayers::Underground;
  if (control("under.subways", true, 12, 223, E.win(0x274, "Subways"), edit.underground == 0, under)) edit.underground = 0;
  if (control("under.pipes", true, 12, 239, E.win(0x275, "Water Pipes"), edit.underground == 1, under)) edit.underground = 1;

  // ---- City Layer Views
  header("mode.layers", 154, 45, E.win(0x26B, "City Layer Views:"), CityLayers::LayerView);
  const bool layers = edit.mode == CityLayers::LayerView;
  const auto& views = cityLayerViews();
  for (size_t i = 0; i < views.size(); i++)
    if (control("layer." + std::to_string(i), true, 157, 76 + 16 * int(i), E.win(views[i].labelId, views[i].fallback), edit.layer == int(i), layers))
      edit.layer = int(i);

  // ---- Buttons
  auto pill = [&](const char* name, Rect b, const std::string& label) { E.spot(name, b); return ui.button(b, label, true, false, "title9"); };
  if (pill("apply", {r.x + 9, r.y + 262, 102, 33}, E.win(0x26D, "Apply"))) R.add(Act::ApplyLayers);
  if (pill("default", {r.x + 9, r.y + 295, 102, 33}, E.win(0x26C, "Default View"))) { edit = CityLayers(); R.add(Act::ApplyLayers); }
  Rect grid{r.x + 248, r.y + 260, 36, 35};
  E.spot("grid", grid);
  { bool c = false;
    std::string tip = E.gui(0x74, "Grid");
    if (!(sk && sk->imageButtonCells(ui, sk->image(skin_group::UiBig, 0x22F96E31), 4, grid, tip, gridOn, true, c))) c = ui.button(grid, "#", true, gridOn);
    if (c) R.add(Act::ToggleGrid); }
  Rect done{r.x + 246, r.y + 296, 42, 32};
  E.spot("ok", done);
  { bool c = false;
    if (!(sk && sk->imageButtonCells(ui, sk->image(skin_group::Sys, 0xA2DEFD9A), 4, done, "", false, true, c))) c = ui.button(done, E.app(0xB4, "OK"));
    if (c || ui.in.key[K_ENTER]) { R.add(Act::ApplyLayers); R.add(Act::Done); R.close = true; } }
  if (ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }
  return R;
}

// ================================================================ graphs (7.3)
std::vector<uint32_t> GraphsWindow::samples(const Chart& c, int range) {
  std::vector<uint32_t> v;
  if (range == 0) for (int i = 11; i >= 0; i--) v.push_back(c.monthly[i]);
  else if (range == 1) for (int i = 9; i >= 0; i--) v.push_back(c.yearly[i]);
  else for (int i = 9; i >= 0; i--) v.push_back(c.decade[i]);
  return v;
}

std::vector<GraphSeries> graphSeriesTemplate() {
  static const std::pair<uint32_t, const char*> t[14] = {
    {0x161, "City size"}, {0x162, "Residents"}, {0x163, "Commerce"}, {0x164, "Industry"}, {0x165, "National population"},
    {0x166, "Traffic"}, {0x167, "Pollution"}, {0x168, "Unemployment"}, {0x169, "Crime"}, {0x16B, "Power %"},
    {0x16C, "Water %"}, {0x16D, "Health"}, {0x16E, "Education"}, {0x16F, "Land value"}};
  std::vector<GraphSeries> v;
  for (auto& e : t) v.push_back({e.first, e.second, nullptr});
  return v;
}

namespace {
void line(Ui& ui, int x0, int y0, int x1, int y1, uint32_t col) {
  int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (int guard = 0; guard < 4000; guard++) {
    ui.fill({x0, y0, 2, 2}, col);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
const uint32_t kSeriesCol[14] = {rgb(200, 0, 0), rgb(0, 140, 0), rgb(0, 0, 200), rgb(160, 120, 0), rgb(120, 0, 160), rgb(0, 150, 150), rgb(90, 90, 90),
                                 rgb(220, 90, 0), rgb(150, 0, 80), rgb(0, 100, 220), rgb(0, 170, 200), rgb(90, 160, 0), rgb(160, 80, 160), rgb(60, 60, 60)};
}  // namespace

WindowResult GraphsWindow::draw(Ui& ui, const WinEnv& E, const std::vector<GraphSeries>& series) {
  WindowResult R;
  int lh = lineHeight(ui);
  Rect r = centered(ui, 680, 420);
  if (frame(ui, E, r, E.win(0x184, "Graphs"))) { R.close = true; R.add(Act::Close); }
  Rect plot{r.x + 14, r.y + 40, 440, 280};
  ui.fill(plot, rgb(255, 255, 255)); ui.frame(plot, false);
  for (int i = 1; i < 4; i++) ui.fill({plot.x + 1, plot.y + i * plot.h / 4, plot.w - 2, 1}, rgb(225, 228, 240));
  int shown = 0;
  for (size_t i = 0; i < series.size() && i < 14; i++) {
    if (!(enabledMask & (1u << i)) || !series[i].chart) continue;
    auto s = samples(*series[i].chart, range);
    if (s.size() < 2) continue;
    uint32_t lo = *std::min_element(s.begin(), s.end()), hi = *std::max_element(s.begin(), s.end());
    int px = 0, py = 0;
    for (size_t k = 0; k < s.size(); k++) {
      int x = plot.x + 8 + int(k) * (plot.w - 16) / int(s.size() - 1);
      float t = hi == lo ? 0.5f : float(s[k] - lo) / float(hi - lo);
      int y = plot.y + plot.h - 10 - int(t * float(plot.h - 20));
      if (k) line(ui, px, py, x, y, kSeriesCol[i]);
      px = x; py = y;
    }
    shown++;
  }
  (void)shown;
  ui.text(plot.x + 4, plot.y + 2, E.win(0x187, "High"), "main10", rgb(0x60, 0x68, 0x90));
  ui.text(plot.x + 4, plot.y + plot.h - lh, E.win(0x188, "Low"), "main10", rgb(0x60, 0x68, 0x90));
  // range buttons
  const uint32_t rid[3] = {0x170, 0x171, 0x172};
  const char* rfb[3] = {"1 year", "10 years", "100 years"};
  for (int i = 0; i < 3; i++)
    if (button(ui, E, "range." + std::to_string(i), {plot.x + i * 110, plot.y + plot.h + 10, 104, 24}, E.win(rid[i], rfb[i]), true, range == i)) {
      range = i;
      R.add(Act::SetTimeRange, 0, i);
    }
  // series check list
  int cx = r.x + 470, cy = r.y + 40;
  for (size_t i = 0; i < series.size() && i < 14; i++) {
    bool on = (enabledMask & (1u << i)) != 0;
    ui.fill({cx, cy + 4, 10, 10}, kSeriesCol[i]);
    if (check(ui, E, "series." + std::to_string(i), {cx + 16, cy, 190, 18}, E.win(series[i].labelId, series[i].fallback), on)) {
      if (on) enabledMask |= 1u << i; else enabledMask &= ~(1u << i);
    }
    cy += 22;
  }
  return R;
}

// ================================================================ charts (7.4)
ChartsData chartsFromPopulation(const Population& p, uint32_t cityPop) {
  ChartsData d;
  for (int i = 0; i < kAges; i++) {
    d.eq.push_back(p.eq[i]);
    d.lifeExpectancy.push_back(p.le[i]);
    d.population.push_back(float(p.peopleAt(i, cityPop)));
  }
  d.populationEQ = p.cityEQ();
  d.workforceEQ = p.qEQ(18, std::min(89, p.oldestAge));
  d.populationLE = p.cityLifeExpectancy();
  double s = 0; int n = 0;
  for (int i = 18; i <= std::min(89, p.oldestAge); i++) { s += p.le[i]; n++; }
  d.workforceLE = n ? float(s / n) : 0;
  d.totalPopulation = cityPop;
  d.workforcePercent = int(p.workforceFraction() * 100.f + 0.5f);
  return d;
}

namespace {
void barChart(Ui& ui, Rect r, const std::vector<float>& v, uint32_t col, float maxV = 0) {
  ui.fill(r, rgb(255, 255, 255)); ui.frame(r, false);
  if (v.empty()) return;
  float mx = maxV;
  if (mx <= 0) for (float f : v) mx = std::max(mx, f);
  if (mx <= 0) mx = 1;
  int n = int(v.size());
  for (int i = 0; i < n; i++) {
    int x0 = r.x + 2 + i * (r.w - 4) / n, x1 = r.x + 2 + (i + 1) * (r.w - 4) / n;
    int h = int(std::max(0.f, std::min(1.f, v[size_t(i)] / mx)) * float(r.h - 6));
    ui.fill({x0, r.y + r.h - 3 - h, std::max(1, x1 - x0 - (n < 20 ? 6 : 0)), h}, col);
  }
}
}  // namespace

WindowResult ChartsWindow::draw(Ui& ui, const WinEnv& E, const ChartsData& d) {
  WindowResult R;
  int lh = lineHeight(ui);
  Rect r = centered(ui, 600, 420);
  if (frame(ui, E, r, E.win(0x183, "Charts"))) { R.close = true; R.add(Act::Close); }
  const uint32_t tid[5] = {0x1DD, 0x1E0, 0x1E1, 0x1DE, 0x1DF};
  const char* tfb[5] = {"Education", "Health", "Population", "Electricity", "Garbage"};
  for (int i = 0; i < 5; i++)
    if (button(ui, E, "tab." + std::to_string(i), {r.x + 12 + i * 114, r.y + 34, 110, 24}, E.win(tid[i], tfb[i]), true, tab == i)) tab = i;
  Rect plot{r.x + 16, r.y + 72, r.w - 32, 220};
  int cy = plot.y + plot.h + 8;
  auto ax = [&](const std::string& y) {
    ui.text(plot.x + 2, plot.y - lh + 2, y, "main10", rgb(0x60, 0x68, 0x90));
    ui.textCentered({plot.x, plot.y + plot.h + 2, plot.w, lh}, E.win(0x1E6, "Age"), "main10", rgb(0x60, 0x68, 0x90));
    cy = plot.y + plot.h + lh + 10;
  };
  auto caption = [&](uint32_t id, const char* fb, const std::string& val, int col) {
    labelValue(ui, r.x + 16 + col * 280, cy, 260, E.win(id, fb), val);
  };
  auto f1 = [](double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", v); return std::string(b); };
  if (tab == 0) {
    barChart(ui, plot, d.eq, rgb(0x31, 0x36, 0x7C)); ax(E.win(0x1E4, "Education quotient"));
    caption(0x1E2, "Population EQ", f1(d.populationEQ), 0); caption(0x1E3, "Workforce EQ", f1(d.workforceEQ), 1);
  } else if (tab == 1) {
    barChart(ui, plot, d.lifeExpectancy, rgb(0x20, 0x90, 0x40)); ax(E.win(0x1E0, "Life expectancy"));
    caption(0x1F9, "Population life expectancy", f1(d.populationLE), 0); caption(0x1FA, "Workforce life expectancy", f1(d.workforceLE), 1);
  } else if (tab == 2) {
    barChart(ui, plot, d.population, rgb(0xA0, 0x60, 0x20)); ax(E.win(0x1E1, "Population"));
    caption(0x1FC, "Total population", groupDigits(d.totalPopulation), 0); caption(0x1FD, "Workforce %", std::to_string(d.workforcePercent) + "%", 1);
  } else {
    bool el = tab == 3;
    std::vector<float> v;
    std::vector<std::pair<uint32_t, const char*>> labels;
    double total = 0;
    if (el) {
      static const std::pair<uint32_t, const char*> L[10] = {{0x1E8, "Coal"}, {0x1E9, "Oil"}, {0x1EA, "Gas"}, {0x1EB, "Wind"}, {0x1EC, "Solar"},
                                                             {0x1ED, "Nuclear"}, {0x1EE, "Microwave"}, {0x1EF, "Fusion"}, {0x1F0, "Waste-to-energy"}, {0x1F1, "Neighbor deals"}};
      for (int i = 0; i < 10; i++) { v.push_back(float(d.electricity[i])); labels.push_back(L[i]); total += d.electricity[i]; }
    } else {
      static const std::pair<uint32_t, const char*> L[5] = {{0x1F4, "Landfill"}, {0x1F1, "Neighbor deals"}, {0x1F5, "Incinerators"}, {0x1F6, "Recycling"}, {0x1F7, "Streets"}};
      for (int i = 0; i < 5; i++) { v.push_back(float(d.garbage[i])); labels.push_back(L[i]); total += d.garbage[i]; }
    }
    barChart(ui, plot, v, el ? rgb(0xC0, 0xA0, 0x00) : rgb(0x80, 0x60, 0x40));
    int n = int(v.size());
    for (int i = 0; i < n; i++) {
      int x = plot.x + 2 + i * (plot.w - 4) / n;
      ui.text(x + 2, plot.y + plot.h + 2 + (i % 2) * lh, E.win(labels[size_t(i)].first, labels[size_t(i)].second));
    }
    cy = plot.y + plot.h + 2 * lh + 10;
    caption(el ? 0x289u : 0x1F8u, el ? "Total (MW-h per year)" : "Total (tons per year)", groupDigits(int64_t(total)), 0);
    ui.text(plot.x + 2, plot.y + 2, E.win(el ? 0x28A : 0x1F3, el ? "Annual average production" : "Garbage by destination"), "main10", rgb(0x60, 0x68, 0x90));
  }
  return R;
}

}  // namespace m3k
