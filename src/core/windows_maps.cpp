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

// Data-map mode of the View Data window (spec 09 7.1 [OBSERVED]): the whole city as one large diamond on the right, compass rose at the
// top left, "Use As Map" toggle at the bottom left, the Good..Bad legend at the bottom right. The corners of the diamond's box are empty,
// so the legend and the toggle sit in them.
WindowResult MapWindow::drawBody(Ui& ui, const WinEnv& E, Rect area, const MapSource& src) {
  WindowResult R;
  const int lh = lineHeight(ui);
  const Rect mm{area.x, area.y + (area.h - 372) / 2, 372, 372};
  const DataMapDef* def = selected >= 0 && selected < int(dataMapDefs().size()) ? &dataMapDefs()[size_t(selected)] : nullptr;
  const std::vector<uint8_t>* vals = def && src.values ? src.values(def->layer) : nullptr;
  if (src.W > 0 && src.H > 0) {
    for (int py = 0; py < mm.h; py++)
      for (int px = 0; px < mm.w; px++) {
        int a = px * 2 * src.W / mm.w - src.W;
        int b = py * 2 * src.H / mm.h;
        int x = (a + b) / 2, z = (b - a) / 2;
        if (a + b < 0 || b - a < 0 || x < 0 || z < 0 || x >= src.W || z >= src.H) continue;
        uint32_t col = src.baseColour ? src.baseColour(x, z) : rgb(90, 130, 70);
        if (def && vals && vals->size() >= size_t(src.W) * src.H) {
          uint8_t v = (*vals)[size_t(x) * src.H + z];
          if (categorical(def->layer)) { if (v != 0xFF) col = MapWindow::legendColour(def->layer, v, 0); }
          else col = MapWindow::legendColour(def->layer, 0, float(v) / 255.f);
        }
        ui.at(mm.x + px, mm.y + py) = col;
      }
    E.spot("minimap", mm);
    if (ui.clicked(mm)) {
      int px = ui.in.mx - mm.x, py = ui.in.my - mm.y;
      int a = px * 2 * src.W / mm.w - src.W, b = py * 2 * src.H / mm.h;
      int x = std::max(0, std::min(src.W - 1, (a + b) / 2)), z = std::max(0, std::min(src.H - 1, (b - a) / 2));
      R.add(Act::CenterView, 0, x, z);
    }
  }
  // compass rose, top left (image 000005E5:0D)
  if (const Image* c = E.img(0x000005E5, 0x0D)) ui.blit(*c, area.x, area.y);
  // "Use As Map", bottom left
  if (button(ui, E, "showInCity", {area.x, area.y + area.h - 26, 110, 24}, E.win(0x2D4, "Use As Map"), true, showInCityView)) {
    showInCityView = !showInCityView;
    overlayActive_ = showInCityView && selected >= 0 && overlay_.row.size() > 0;
    R.add(Act::DataMapOptionChanged, 0, 0, 0, "showInCity");
  }
  // legend, bottom right
  if (def) {
    const int right = area.x + area.w - 4;
    if (categorical(def->layer)) {
      static const char* const zfb[7] = {"Residential", "Commercial", "Industrial", "Government", "Seaports", "Airports", "Landfills"};
      const int n = int(def->categories.size());
      int cy = area.y + area.h - 4 - n * (lh + 2);
      for (int i = 0; i < n; i++, cy += lh + 2) {
        std::string fb = def->layer == DataLayer::Zones ? zfb[i] : def->layer == DataLayer::Power ? (i ? "Unpowered" : "Powered") : (i ? "Unwatered" : "Watered");
        std::string t = E.win(def->categories[size_t(i)], fb);
        ui.text(right - ui.textWidth(t, "system9"), cy, t, "system9", rgb(0x31, 0x38, 0x84));
        ui.fill({right - ui.textWidth(t, "system9") - 16, cy + 1, 12, 12}, legendColour(def->layer, i, 0));
      }
    } else {
      auto el = endLabels(def->layer);
      const std::string top = E.win(def->lowId, el.first), bot = E.win(def->highId, el.second);
      const int bh = 90, bx = right - 16, by = area.y + area.h - 4 - bh - lh;
      ui.text(right - ui.textWidth(top, "system9"), by - lh, top, "system9", rgb(0x31, 0x38, 0x84));
      for (int i = 0; i < bh; i++) ui.fill({bx, by + i, 16, 1}, legendColour(def->layer, 0, float(i) / float(bh - 1)));
      ui.frame({bx, by, 16, bh}, false);
      ui.text(right - ui.textWidth(bot, "system9"), by + bh + 2, bot, "system9", rgb(0x31, 0x38, 0x84));
    }
  }
  // Layer options, top right (spec 09 7.2: coverage toggle, pollution kind). Their place in the original window is not observed yet [#57].
  if (def && (def->toggleId || !def->selector.empty())) {
    int oy = area.y + 2;
    const int ox = area.x + area.w - 112;
    if (def->toggleId) {
      bool v = coverage;
      if (check(ui, E, "coverage", {ox, oy, 112, 16}, E.win(def->toggleId, "Show coverage"), v)) { coverage = v; R.add(Act::DataMapOptionChanged, 0, 0, 0, "coverage"); }
      oy += 18;
    }
    static const char* const sfb[3] = {"Air", "Water", "Garbage"};
    for (size_t i = 0; i < def->selector.size(); i++, oy += 16)
      if (radio(ui, E, "pollution." + std::to_string(i), {ox, oy, 112, 16}, E.win(def->selector[i], sfb[i]), pollutionKind == int(i))) {
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
  Rect r = placedAt(ui, 32, 32, 296, 336);
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
  // Spec 09 7.3 [OBSERVED]: the grid lists three columns; the string ids of Approval Rating and Funds are not known yet (0 = fallback text).
  struct E { uint32_t id; const char* fb; int fmt; };
  static const E t[16] = {
    {0, "Approval Rating", 0}, {0x162, "Residents", 0}, {0x16F, "Land Value", 0}, {0x161, "City Size", 0}, {0x168, "Unemployment", 1},
    {0x167, "Pollution", 0}, {0x16E, "Education", 0}, {0x166, "Traffic", 0}, {0x163, "Commerce", 0}, {0x165, "Nat'l Pop", 0}, {0x16C, "Water %", 1},
    {0x169, "Crime", 0}, {0x16D, "Health", 0}, {0x16A, "Funds", 2}, {0x164, "Industry", 0}, {0x16B, "Power %", 1}};
  std::vector<GraphSeries> v;
  for (auto& e : t) { GraphSeries g; g.labelId = e.id; g.fallback = e.fb; g.format = e.fmt; v.push_back(g); }
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
const uint32_t kSeriesCol[16] = {rgb(0, 150, 60), rgb(0, 140, 0), rgb(60, 60, 60), rgb(200, 0, 0), rgb(220, 90, 0), rgb(90, 90, 90), rgb(160, 80, 160), rgb(0, 150, 150),
                                 rgb(160, 120, 0), rgb(120, 0, 160), rgb(0, 170, 200), rgb(150, 0, 80), rgb(90, 160, 0), rgb(0, 0, 200), rgb(130, 90, 40), rgb(0, 100, 220)};
}  // namespace

// Graphs mode of the View Data window (spec 09 7.3 [OBSERVED]): only the checked series are plotted, each labelled at its right end with its
// name and current value; below the plot a three-column check-box grid; the 1 / 10 / 100 Years radio buttons at the bottom right.
WindowResult GraphsWindow::drawBody(Ui& ui, const WinEnv& E, Rect area, const std::vector<GraphSeries>& series) {
  WindowResult R;
  const int lh = lineHeight(ui);
  const uint32_t ink = rgb(0x31, 0x38, 0x84);
  if (!maskInit) {   // defaults: City Size, Funds, Power %
    enabledMask = 0;
    for (size_t i = 0; i < series.size() && i < 16; i++)
      if (series[i].fallback == "City Size" || series[i].fallback == "Funds" || series[i].fallback == "Power %") enabledMask |= 1u << i;
    maskInit = true;
  }
  const Rect plot{area.x, area.y + 2, area.w, area.h - 142};
  ui.fill(plot, rgb(255, 255, 255)); ui.frame(plot, false);
  for (int i = 1; i < 4; i++) ui.fill({plot.x + 1, plot.y + i * plot.h / 4, plot.w - 2, 1}, rgb(225, 228, 240));
  int labelRight = 0;   // the plot leaves room on the right for the end labels
  struct Lab { std::string text; int y; uint32_t col; };
  std::vector<Lab> labs;
  for (size_t i = 0; i < series.size() && i < 16; i++) {
    if (!(enabledMask & (1u << i)) || !series[i].chart) continue;
    auto s = samples(*series[i].chart, range);
    if (s.size() < 2) continue;
    uint32_t lo = 0, hi = series[i].chart->max;
    if (hi == 0) { lo = *std::min_element(s.begin(), s.end()); hi = *std::max_element(s.begin(), s.end()); }
    const int px0 = plot.x + 6, px1 = plot.x + plot.w - 120;
    int px = 0, py = 0;
    for (size_t k = 0; k < s.size(); k++) {
      int x = px0 + int(k) * (px1 - px0) / int(s.size() - 1);
      float t = hi <= lo ? 0.5f : std::max(0.f, std::min(1.f, float(int64_t(s[k]) - int64_t(lo)) / float(hi - lo)));
      int y = plot.y + plot.h - 8 - int(t * float(plot.h - 16));
      if (k) line(ui, px, py, x, y, kSeriesCol[i]);
      px = x; py = y;
    }
    const uint32_t cur = s.back();
    std::string val = series[i].format == 1 ? std::to_string(cur) + "%" : series[i].format == 2 ? groupDigits(int64_t(cur) * 1000 - 100000) : groupDigits(cur);
    labs.push_back({E.win(series[i].labelId, series[i].fallback) + " " + val, py - lh / 2, kSeriesCol[i]});
    labelRight = px1;
  }
  std::sort(labs.begin(), labs.end(), [](const Lab& a, const Lab& b) { return a.y < b.y; });
  int lastY = -1000;
  for (auto& l : labs) {   // keep the labels from overprinting each other
    int y = std::max(l.y, lastY + lh - 2);
    y = std::max(plot.y + 1, std::min(plot.y + plot.h - lh, y));
    ui.text(labelRight + 4, y, l.text, "system9", l.col);
    lastY = y;
  }
  // check-box grid: three columns of 5, 6 and 5
  static const int colSize[3] = {5, 6, 5};
  const int gx = area.x + 4, gy = plot.y + plot.h + 8, cw = area.w / 3, rowH = 18;
  size_t idx = 0;
  for (int c = 0; c < 3; c++)
    for (int r = 0; r < colSize[c] && idx < series.size() && idx < 16; r++, idx++) {
      bool on = (enabledMask & (1u << idx)) != 0;
      if (check(ui, E, "series." + std::to_string(idx), {gx + c * cw, gy + r * rowH, cw - 6, rowH}, E.win(series[idx].labelId, series[idx].fallback), on)) {
        if (on) enabledMask |= 1u << idx; else enabledMask &= ~(1u << idx);
      }
    }
  // range radio buttons, bottom right
  const uint32_t rid[3] = {0x170, 0x171, 0x172};
  const char* rfb[3] = {"1 Year", "10 Years", "100 Years"};
  for (int i = 0; i < 3; i++)
    if (radio(ui, E, "range." + std::to_string(i), {area.x + area.w - 96, area.y + area.h - 56 + i * 18, 96, 18}, E.win(rid[i], rfb[i]), range == i)) {
      range = i;
      R.add(Act::SetTimeRange, 0, i);
    }
  (void)ink;
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

// Charts mode (spec 09 7.1 [OBSERVED]): the chart on top, the two caption lines below it, then a centred column of five radio buttons.
WindowResult ChartsWindow::drawBody(Ui& ui, const WinEnv& E, Rect area, const ChartsData& d) {
  WindowResult R;
  const int lh = lineHeight(ui);
  Rect plot{area.x + 4, area.y + lh + 6, area.w - 8, 190};
  int cy = plot.y + plot.h + 8;
  auto ax = [&](const std::string& y) {
    ui.text(plot.x + 2, plot.y - lh + 2, y, "main10", rgb(0x60, 0x68, 0x90));
    ui.textCentered({plot.x, plot.y + plot.h + 2, plot.w, lh}, E.win(0x1E6, "Age"), "main10", rgb(0x60, 0x68, 0x90));
    cy = plot.y + plot.h + lh + 10;
  };
  // two caption lines, label left / value right, one per line
  auto caption = [&](uint32_t id, const char* fb, const std::string& val, int line) {
    labelValue(ui, area.x + 12, cy + line * lh, area.w - 24, E.win(id, fb), val);
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
      ui.text(x + 2, plot.y + plot.h + 2 + (i % 2) * lh, E.win(labels[size_t(i)].first, labels[size_t(i)].second), "system9");
    }
    cy = plot.y + plot.h + 2 * lh + 10;
    caption(el ? 0x28Au : 0x1F3u, el ? "Annual average production" : "Garbage by destination", "", 0);
    caption(el ? 0x289u : 0x1F8u, el ? "Total (MW-h per year)" : "Total (tons per year)", groupDigits(int64_t(total)), 1);
  }
  // radio column, centred
  const uint32_t tid[5] = {0x1DD, 0x1E0, 0x1E1, 0x1DE, 0x1DF};
  const char* tfb[5] = {"Education", "Health", "Population", "Electricity", "Garbage"};
  const int ry = cy + 2 * lh + 8, rw = 120;
  for (int i = 0; i < 5; i++)
    if (radio(ui, E, "tab." + std::to_string(i), {area.x + (area.w - rw) / 2, ry + i * 18, rw, 18}, E.win(tid[i], tfb[i]), tab == i)) tab = i;
  return R;
}

// ================================================================ View Data window (7.1)
void ViewDataWindow::setMode(int m, const MapSource& src) {
  mode = std::max(0, std::min(int(ModeGraphs), m));
  if (mode < int(ModeCharts)) map.select(mode, src);
}

WindowResult ViewDataWindow::draw(Ui& ui, const WinEnv& E, const MapSource& src, const std::vector<GraphSeries>& series, const ChartsData& cd) {
  WindowResult R;
  const Rect r = placedAt(ui, 40, 34, 424, 440);
  std::string title = mode == int(ModeCharts) ? E.win(0x183, "Charts") : mode == int(ModeGraphs) ? E.win(0x184, "Graphs")
                      : E.win(dataMapDefs()[size_t(mode)].titleId, "Data Map");
  if (frame(ui, E, r, title) || ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }
  // mode strip: ten data maps, a gap, Charts, Graphs. The icon art is not identified yet, so the buttons carry short captions [#57].
  static const char* const cap[12] = {"Au", "Cr", "De", "Po", "Fl", "LV", "Pl", "Tr", "Wa", "Zo", "Ch", "Gr"};
  const int sx = r.x + 8, sy = r.y + 38, pitch = 30, bs = 28;
  for (int i = 0; i < 12; i++) {
    const int y = sy + i * pitch + (i >= 10 ? 8 : 0);
    const Rect b{sx, y, bs, bs};
    std::string tip = i == 10 ? E.win(0x183, "Charts") : i == 11 ? E.win(0x184, "Graphs") : E.win(dataMapDefs()[size_t(i)].titleId, "Data Map");
    if (button(ui, E, "mode." + std::to_string(i), b, cap[i], true, mode == i)) { setMode(i, src); R.add(Act::SelectDataMap, 0, i < 10 ? int(dataMapDefs()[size_t(i)].layer) : -1); }
    if (ui.hover(b)) ui.tooltip = tip;
  }
  const Rect area{r.x + 44, r.y + 34, r.w - 52, r.h - 42};
  if (mode < int(ModeCharts)) { if (map.selected != mode) map.select(mode, src); R.merge(map.drawBody(ui, E, area, src)); }
  else if (mode == int(ModeCharts)) R.merge(charts.drawBody(ui, E, area, cd));
  else R.merge(graphs.drawBody(ui, E, area, series));
  return R;
}

}  // namespace m3k
