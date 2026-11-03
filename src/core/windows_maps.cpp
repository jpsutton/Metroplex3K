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
    // Observed legend swatches (spec 09 7.1.1).
    static const uint32_t pal[8] = {rgb(57, 117, 33), rgb(90, 93, 173), rgb(198, 158, 33), rgb(239, 93, 0), rgb(99, 162, 132), rgb(107, 166, 181), rgb(132, 105, 82), rgb(200, 0, 200)};
    if (l == DataLayer::Power) return category == 0 ? rgb(214, 186, 49) : rgb(33, 32, 66);
    if (l == DataLayer::Water) return category == 0 ? rgb(8, 0, 198) : rgb(198, 199, 198);
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

namespace {
const uint32_t kBandBlue = rgb(123, 146, 255), kLabelInk = rgb(123, 125, 181), kInkBlue = rgb(0x31, 0x38, 0x84);

// Inset legend box (spec 09 7.1.1): left/top lines (123,138,255), right/bottom highlight (255,243,255); `cut` = 18 px diagonal at the top left.
void insetBox(Ui& ui, Rect b, bool cut) {
  const uint32_t lo = rgb(123, 138, 255), hi = rgb(255, 243, 255);
  const int c = cut ? 18 : 0;
  ui.fill({b.x + c, b.y, b.w - c, 1}, lo); ui.fill({b.x, b.y + c, 1, b.h - c}, lo);
  ui.fill({b.x, b.y + b.h - 1, b.w, 1}, hi); ui.fill({b.x + b.w - 1, b.y, 1, b.h}, hi);
  for (int i = 0; i <= c; i++) ui.fill({b.x + i, b.y + c - i, 1, 1}, lo);
}

// Round toggle (radio-style): a 20 px disc with a dot while on.
void roundToggle(Ui& ui, int cx, int cy, bool on) {
  for (int y = -10; y <= 10; y++)
    for (int x = -10; x <= 10; x++) {
      const int d2 = x * x + y * y;
      if (d2 > 100) continue;
      uint32_t col = d2 > 80 ? kInkBlue : rgb(255, 255, 255);
      if (on && d2 <= 25) col = kInkBlue;
      ui.fill({cx + x, cy + y, 1, 1}, col);
    }
}

void legendLabel(Ui& ui, int x, int y, const std::string& t) { ui.text(x, y, t, "main10", kLabelInk); }
}  // namespace

// Data-map mode of the View Data window (spec 09 7.1.1 [OBSERVED], coordinates relative to the window): the whole city as a diamond with
// corners (230,47) (42,234) (417,234) (230,424), compass at (63,60) (not on Pollution), "Use As Map" panel at the bottom left, the legend
// box at the bottom right, the coverage / pollution-kind controls in an inset at the top right.
WindowResult MapWindow::drawBody(Ui& ui, const WinEnv& E, Rect r, const MapSource& src) {
  WindowResult R;
  const DataMapDef* def = selected >= 0 && selected < int(dataMapDefs().size()) ? &dataMapDefs()[size_t(selected)] : nullptr;
  const bool pollution = def && def->layer == DataLayer::Pollution;
  const Rect mm{r.x + 42, r.y + 47, 376, 377};
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
          else if (v) col = lerpCol(col, MapWindow::legendColour(def->layer, 0, float(v) / 255.f), 0.7f);   // data tints over the terrain base
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
  if (!pollution) compassRose(ui, E, r.x + 63 + 30, r.y + 60 + 30, viewRotation);
  // "Use As Map": L-shaped blue panel, round toggle (42DEC7D2:02) and the label
  ui.fill({r.x + 59, r.y + 341, 29, 30}, kBandBlue);
  ui.fill({r.x + 59, r.y + 370, 96, 35}, kBandBlue);
  {
    const Rect tb{r.x + 62, r.y + 345, 26, 25};
    E.spot("showInCity", tb);
    bool c = false;
    const Image* art = E.img(0x42DEC7D2, 0x02);
    if (!(ui.skin && art && ui.skin->imageButtonCells(ui, art, 4, tb, "", showInCityView, true, c))) { roundToggle(ui, tb.x + 13, tb.y + 12, showInCityView); c = ui.clicked(tb); }
    const Rect lab{r.x + 62, r.y + 372, 92, 30};
    ui.text(r.x + 71, r.y + 383, E.win(0x2D4, "Use As Map"), "main10", kInkBlue);
    if (c || ui.clicked(lab)) {
      showInCityView = !showInCityView;
      overlayActive_ = showInCityView && selected >= 0 && overlay_.row.size() > 0;
      R.add(Act::DataMapOptionChanged, 0, 0, 0, "showInCity");
    }
  }
  if (def) {
    const Rect std_{r.x + 287, r.y + 358, 129, 76};
    const int lx = r.x + 331, ly0 = r.y + 361, ly1 = r.y + 419;
    auto stack = [&](Rect box, int n, int firstY, int swatchH, auto&& swatchCol, auto&& nameOf, bool cut) {
      insetBox(ui, box, cut);
      for (int i = 0; i < n; i++) {
        const int y = firstY + i * swatchH;
        ui.fill({r.x + 292, y, 32, swatchH}, swatchCol(i));
        legendLabel(ui, r.x + 332, y + (swatchH - 11) / 2, nameOf(i));
      }
    };
    if (def->layer == DataLayer::Zones) {
      static const char* const zfb[7] = {"Residential", "Commercial", "Industrial", "Government", "Seaports", "Airports", "Landfill"};
      auto nm = [&](int i) { return E.win(def->categories[size_t(i)], zfb[i]); };
      auto cl = [&](int i) { return legendColour(DataLayer::Zones, i, 0); };
      stack({r.x + 287, r.y + 54, 130, 61}, 3, r.y + 58, 17, cl, nm, true);
      insetBox(ui, std_, true);
      for (int i = 0; i < 4; i++) {
        const int y = r.y + 362 + i * 17;
        ui.fill({r.x + 292, y, 32, 17}, cl(3 + i));
        legendLabel(ui, r.x + 332, y + 3, nm(3 + i));
      }
    } else if (categorical(def->layer)) {
      insetBox(ui, std_, true);
      static const char* const pfb[2] = {"Powered", "Unpowered"};
      static const char* const wfb[2] = {"Watered", "Unwatered"};
      for (int i = 0; i < 2; i++) {
        ui.fill({r.x + 292, r.y + 363 + i * 33, 32, 33}, legendColour(def->layer, i, 0));
        legendLabel(ui, lx, i ? ly1 : ly0, E.win(def->categories[size_t(i)], def->layer == DataLayer::Power ? pfb[i] : wfb[i]));
      }
    } else {
      insetBox(ui, std_, true);
      const Image* g = (def->layer == DataLayer::Aura) ? E.img(0x5E5, 0x09)
                       : (def->layer == DataLayer::Density || def->layer == DataLayer::LandValue) ? E.img(0x5E5, 0x0A) : nullptr;
      if (g && !g->empty()) ui.blit(*g, r.x + 292, r.y + 362);
      else for (int i = 0; i < 66; i++) ui.fill({r.x + 292, r.y + 362 + i, 32, 1}, lerpCol(rgb(255, 255, 255), rgb(220, 30, 30), float(i) / 65.f));   // white -> red [OPEN: image not found]
      auto el = endLabels(def->layer);
      legendLabel(ui, lx, ly0, E.win(def->lowId, el.first));
      legendLabel(ui, lx, ly1, E.win(def->highId, el.second));
    }
    // coverage toggle (Crime, Flammability)
    if (def->toggleId) {
      insetBox(ui, {r.x + 287, r.y + 48, 129, 43}, false);
      const Rect hit{r.x + 296, r.y + 52, 118, 34};
      E.spot("coverage", hit);
      roundToggle(ui, r.x + 308, r.y + 69, coverage);
      std::string t = E.win(def->toggleId, def->layer == DataLayer::Crime ? "Police Station Coverage" : "Fire Station Coverage");
      const size_t sp = t.rfind(' ');
      legendLabel(ui, r.x + 323, r.y + 58, sp == std::string::npos ? t : t.substr(0, sp));
      if (sp != std::string::npos) legendLabel(ui, r.x + 323, r.y + 72, t.substr(sp + 1));
      if (ui.clicked(hit)) { coverage = !coverage; R.add(Act::DataMapOptionChanged, 0, 0, 0, "coverage"); }
    }
    // pollution kind (Pollution)
    if (!def->selector.empty()) {
      legendLabel(ui, r.x + 288, r.y + 48, E.win(0x2D8, "Show:"));
      static const char* const sfb[3] = {"Air Pollution", "Water Pollution", "Garbage Pollution"};
      static const int cy[3] = {66, 81, 95};
      for (size_t i = 0; i < def->selector.size() && i < 3; i++)
        if (radio(ui, E, "pollution." + std::to_string(i), {r.x + 288, r.y + cy[i] - 8, 126, 16}, E.win(def->selector[i], sfb[i]), pollutionKind == int(i), "main10")) {
          pollutionKind = int(i);
          R.add(Act::DataMapOptionChanged, 0, int(i), 0, "pollution");
        }
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
  // Spec 09 7.3 [OBSERVED]: the order of the check-box grid (columns of 5, 6 and 5). tmpl = the label template of the line's right end; the
  // original prints a value only for Health, Res, City Size, Education, Com, Ind, Crime, Pollution and Unemployment.
  struct E { uint32_t id; const char* fb; int fmt; uint32_t tmpl; const char* tfb; bool val; };
  static const E t[16] = {
    {0x1D2, "Approval Rating", 0, 0x28D, "Approval Rating %s%%", false}, {0x162, "Residents", 0, 0x1D4, "Res %s", true},
    {0x16F, "Land Value", 0, 0x1DB, "Value %s", false}, {0x161, "City Size", 0, 0x1D6, "City Size %s", true},
    {0x168, "Unemployment", 1, 0x1D5, "Unemployment %s", true},
    {0x167, "Pollution", 0, 0x1D9, "Pollution %s", true}, {0x16E, "Education", 0, 0x28F, "Education %s", true},
    {0x166, "Traffic", 0, 0x1D7, "Traffic", false}, {0x163, "Commerce", 0, 0x1DA, "Com %s", true},
    {0x165, "Nat'l Pop", 0, 0x28C, "Nat'l Population %s", false}, {0x16C, "Water %", 1, 0, "Water %", false},
    {0x169, "Crime", 0, 0x1D8, "Crime %s", true}, {0x16D, "Health", 0, 0x28E, "Health %s", true},
    {0x283, "Funds", 2, 0x283, "Funds", false}, {0x164, "Industry", 0, 0x1D3, "Ind %s", true}, {0x16B, "Power %", 1, 0x1DC, "Power %s%%", false}};
  std::vector<GraphSeries> v;
  for (auto& e : t) { GraphSeries g; g.labelId = e.id; g.fallback = e.fb; g.format = e.fmt; g.tmplId = e.tmpl; g.tmplFb = e.tfb; g.showValue = e.val; v.push_back(g); }
  return v;
}

namespace {
void line(Ui& ui, int x0, int y0, int x1, int y1, uint32_t col, int w = 2) {
  int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (int guard = 0; guard < 4000; guard++) {
    ui.fill({x0, y0, w, w}, col);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
// Series colours of spec 09 7.3 in the order of graphSeriesTemplate().
const uint32_t kSeriesCol[16] = {rgb(222, 97, 165), rgb(0, 146, 0), rgb(198, 146, 49), rgb(49, 97, 33), rgb(0, 97, 165), rgb(99, 81, 49), rgb(173, 97, 82), rgb(140, 113, 140),
                                 rgb(24, 65, 255), rgb(165, 97, 165), rgb(123, 146, 222), rgb(198, 0, 66), rgb(33, 195, 99), rgb(115, 97, 165), rgb(231, 162, 66), rgb(255, 105, 0)};
}  // namespace

// Graphs mode of the View Data window (spec 09 7.3 / 7.1.1 [OBSERVED], coordinates relative to the window). Plot box (44,50)-(400,238): lines
// run x 44..300, their labels sit in x 300..400 at a 10 px pitch, joined to the line ends by a short slanted connector; below the plot a
// 3-column check-box grid (x 58 / 178 / 298, rows y = 253 + 24 r) and the range radios at x ~322.
WindowResult GraphsWindow::drawBody(Ui& ui, const WinEnv& E, Rect r, const std::vector<GraphSeries>& series) {
  WindowResult R;
  if (!maskInit) {   // defaults: City Size, Funds, Power %
    enabledMask = 0;
    for (size_t i = 0; i < series.size() && i < 16; i++)
      if (series[i].fallback == "City Size" || series[i].fallback == "Funds" || series[i].fallback == "Power %") enabledMask |= 1u << i;
    maskInit = true;
  }
  const Rect box{r.x + 44, r.y + 50, 357, 189};
  for (int y = 0; y < box.h; y += 16) ui.fill({box.x, box.y + y, box.w, std::min(16, box.h - y)}, (y / 16) % 2 ? rgb(214, 219, 255) : rgb(231, 231, 255));
  sunkenLines(ui, box);
  const int x0 = r.x + 46, x1 = r.x + 300, yTop = r.y + 56, yBot = r.y + 214;
  struct Lab { std::string text; int endY; uint32_t col; int y = 0; };
  std::vector<Lab> labs;
  int nPts = 0;
  for (size_t i = 0; i < series.size() && i < 16; i++) {
    if (!(enabledMask & (1u << i)) || !series[i].chart) continue;
    auto s = samples(*series[i].chart, range);
    if (s.size() < 2) continue;
    nPts = int(s.size());
    uint32_t lo = 0, hi = series[i].chart->max;
    if (hi == 0) { lo = *std::min_element(s.begin(), s.end()); hi = *std::max_element(s.begin(), s.end()); }   // [OPEN] scaling
    int px = 0, py = 0;
    for (size_t k = 0; k < s.size(); k++) {
      int x = x0 + int(k) * (x1 - x0) / int(s.size() - 1);
      float t = hi <= lo ? 0.5f : std::max(0.f, std::min(1.f, float(int64_t(s[k]) - int64_t(lo)) / float(hi - lo)));
      int y = yBot - int(t * float(yBot - yTop));
      if (k) line(ui, px, py, x, y, kSeriesCol[i]);
      px = x; py = y;
    }
    std::string val;
    if (series[i].showValue) {
      const int64_t cur = series[i].live >= 0 ? series[i].live : int64_t(s.back());
      val = series[i].format == 1 ? std::to_string(cur) + "%" : groupDigits(cur);
    }
    std::string t = subst(E.win(series[i].tmplId, series[i].tmplFb), {val});
    while (!t.empty() && t.back() == ' ') t.pop_back();
    labs.push_back({t, py, kSeriesCol[i], 0});
  }
  std::sort(labs.begin(), labs.end(), [](const Lab& a, const Lab& b) { return a.endY < b.endY; });
  int lastY = -1000;
  for (auto& l : labs) {   // 10 px pitch, pushed apart in line-end order
    l.y = std::max(l.endY - 5, lastY + 10);
    l.y = std::max(r.y + 52, std::min(r.y + 226, l.y));
    lastY = l.y;
  }
  for (auto& l : labs) {
    line(ui, x1, l.endY, r.x + 304, l.y + 5, l.col, 1);
    ui.text(r.x + 306, l.y, l.text, "system9", l.col);
  }
  // ticks along y ~ 220 and the current year in grey
  if (nPts > 1) for (int k = 0; k < nPts; k++) ui.fill({x0 + k * (x1 - x0) / (nPts - 1), r.y + 219, 1, 4}, rgb(128, 128, 128));
  if (year) rightText(ui, r.x + 298, r.y + 224, std::to_string(year), "system9", rgb(128, 128, 128));
  // check-box grid: three columns of 5, 6 and 5
  static const int colSize[3] = {5, 6, 5}, colX[3] = {58, 178, 298};
  size_t idx = 0;
  for (int c = 0; c < 3; c++)
    for (int row = 0; row < colSize[c] && idx < series.size() && idx < 16; row++, idx++) {
      const bool on = (enabledMask & (1u << idx)) != 0;
      const Rect cb{r.x + colX[c], r.y + 253 + 24 * row, 12, 12};
      const Rect hit{cb.x, cb.y, 112, 14};
      E.spot("series." + std::to_string(idx), hit);
      ui.fill(cb, rgb(255, 255, 255)); sunkenLines(ui, cb);
      if (on) {
        for (int k = 0; k < 3; k++) ui.fill({cb.x + 2 + k, cb.y + 5 + k, 1, 2}, kInkBlue);
        for (int k = 0; k < 5; k++) ui.fill({cb.x + 4 + k, cb.y + 7 - k, 1, 2}, kInkBlue);
      }
      ui.text(cb.x + 14, cb.y + (12 - ui.fonts->get("system9").height()) / 2, E.win(series[idx].labelId, series[idx].fallback), "system9", kInkBlue);
      if (ui.clicked(hit)) { if (on) enabledMask &= ~(1u << idx); else enabledMask |= 1u << idx; }
    }
  // range radios
  static const uint32_t rid[3] = {0x170, 0x171, 0x172};
  static const char* const rfb[3] = {"1 Year", "10 Years", "100 Years"};
  static const int ry[3] = {387, 405, 423};
  for (int i = 0; i < 3; i++)
    if (radio(ui, E, "range." + std::to_string(i), {r.x + 316, r.y + ry[i] - 6, 90, 12}, E.win(rid[i], rfb[i]), range == i)) {
      range = i;
      R.add(Act::SetTimeRange, 0, i);
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

namespace {
std::string capText(const WinEnv& E, uint32_t id, const char* fb, const std::string& val) {
  std::string t = E.win(id, fb);
  if (t.find("%s") != std::string::npos || t.find("%d") != std::string::npos) return subst(t, {val});
  return val.empty() ? t : t + " " + val;
}
}  // namespace

// Charts mode (spec 09 7.1.1 [OBSERVED], coordinates relative to the window). Chart box (52,50)-(408,238), plot x 144..408 y 52..205 on stripe tiles
// 000005E5:31/33/35, 15 five-year bars (7 px wide, pitch 13 from x = 149) on the age pages; captions centred at x 225 (y 249 / 265); the five page
// radios centred at x ~197 (y = 314 + 20 i).
WindowResult ChartsWindow::drawBody(Ui& ui, const WinEnv& E, Rect r, const ChartsData& d) {
  WindowResult R;
  const Rect box{r.x + 52, r.y + 50, 357, 189}, plot{r.x + 144, r.y + 52, 264, 153};
  sunkenLines(ui, box);
  ui.fill({box.x + 1, box.y + 1, box.w - 2, box.h - 2}, rgb(222, 223, 255));
  {
    static const uint32_t tile[3] = {0x31, 0x33, 0x35};
    const Image* im = E.img(0x5E5, tile[tab % 3]);
    if (im && !im->empty()) { for (int x = plot.x; x < plot.x + plot.w; x += im->w) ui.blit(*im, x, plot.y, 0, std::min(im->w, plot.x + plot.w - x)); }
    else for (int y = 0; y < plot.h; y += 17) ui.fill({plot.x, plot.y + y, plot.w, std::min(17, plot.h - y)}, (y / 17) % 2 ? rgb(247, 223, 222) : rgb(231, 199, 198));
  }
  const uint32_t axis = rgb(0x18, 0x20, 0x6B);
  ui.fill({plot.x, plot.y, 1, plot.h + 1}, axis); ui.fill({plot.x, plot.y + plot.h, plot.w, 1}, axis);
  const bool agePage = tab <= 2;
  auto f1 = [](double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", v); return std::string(b); };
  std::string cap1, cap2;
  if (agePage) {
    const std::vector<float>& src = tab == 0 ? d.eq : tab == 1 ? d.lifeExpectancy : d.population;
    float axisMax = 140.f;
    if (tab == 2) { float m = 0; for (float f : src) m = std::max(m, f); axisMax = std::max(1.f, std::ceil(m / 7.f / 10.f) * 70.f); }
    // y labels, 0..axisMax in 7 steps, right-aligned at x 138
    for (int i = 0; i <= 7; i++) {
      const int cy = plot.y + plot.h - int(20.5f * float(i) + 0.5f);
      rightText(ui, r.x + 138, cy - 5, tab == 2 ? groupDigits(int64_t(axisMax * float(i) / 7.f)) : std::to_string(20 * i), "system9", kInkBlue);
    }
    for (int n = 10; n <= 90; n += 10) {   // x labels, 26 px per 10 years, "10" at x ~ 164
      const std::string t = std::to_string(n);
      ui.text(r.x + 138 + 26 * n / 10 - ui.textWidth(t, "system9") / 2, r.y + 210, t, "system9", kInkBlue);
    }
    ui.textCentered({r.x + 176, r.y + 220, 200, 12}, E.win(0x1E6, "Resident Age"), "system9", kInkBlue);
    {   // vertical axis caption (the original writes it rotated; here two horizontal lines)
      std::string t = E.win(tab == 0 ? 0x1E4u : tab == 1 ? 0x1E0u : 0x1E1u, tab == 0 ? "Education Quotient" : tab == 1 ? "Life Expectancy" : "Population");
      const size_t sp = t.find(' ');
      const std::string a = sp == std::string::npos ? t : t.substr(0, sp), b = sp == std::string::npos ? "" : t.substr(sp + 1);
      ui.textCentered({r.x + 56, r.y + 158, 62, 12}, a, "system9", kInkBlue);
      if (!b.empty()) ui.textCentered({r.x + 56, r.y + 171, 62, 12}, b, "system9", kInkBlue);
    }
    if (tab == 0) {   // workforce band over about age 22..67
      for (int y = plot.y; y < plot.y + plot.h; y++)
        for (int x = r.x + 197; x < r.x + 316; x++) ui.at(x, y) = lerpCol(ui.at(x, y), rgb(231, 199, 198), 0.5f);
      ui.textCentered({r.x + 197, plot.y + 1, 119, 12}, E.win(0x1E5, "Workforce"), "system9", rgb(230, 130, 0));
    }
    for (int i = 0; i < 15; i++) {   // one bar per 5-year bucket, ages 0-4 .. 70-74
      float sum = 0; int n = 0;
      for (int a = i * 5; a < i * 5 + 5 && a < int(src.size()); a++) { sum += src[size_t(a)]; n++; }
      const float v = n ? sum / float(n) : 0.f;
      const int h = std::max(0, std::min(plot.h - 4, int(v * 142.f / axisMax)));
      const int bx = r.x + 149 + 13 * i;
      if (h > 0) {
        ui.fill({bx, plot.y + plot.h - h, 7, h}, rgb(0, 211, 140));
        ui.fill({bx - 1, plot.y + plot.h - h, 1, h}, rgb(173, 178, 222)); ui.fill({bx + 7, plot.y + plot.h - h, 1, h}, rgb(173, 178, 222));
      }
    }
    if (tab == 0) { cap1 = capText(E, 0x1E2, "Population EQ %s", f1(d.populationEQ)); cap2 = capText(E, 0x1E3, "Workforce EQ %s", f1(d.workforceEQ)); }
    else if (tab == 1) { cap1 = capText(E, 0x1F9, "Population life expectancy %s", f1(d.populationLE)); cap2 = capText(E, 0x1FA, "Workforce life expectancy %s", f1(d.workforceLE)); }
    else { cap1 = capText(E, 0x1FC, "Total population %s", groupDigits(d.totalPopulation)); cap2 = capText(E, 0x1FD, "Workforce %s%%", std::to_string(d.workforcePercent)); }
  } else {
    const bool el = tab == 3;
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
    float mx = 0; for (float f : v) mx = std::max(mx, f);
    if (mx <= 0) mx = 1;
    const int n = int(v.size()), pitch = (plot.w - 8) / n;
    for (int i = 0; i < n; i++) {
      const int h = int(v[size_t(i)] / mx * float(plot.h - 8));
      const int bx = plot.x + 6 + i * pitch, bw = std::max(3, pitch - 6);
      if (h > 0) ui.fill({bx, plot.y + plot.h - h, bw, h}, el ? rgb(0xC0, 0xA0, 0x00) : rgb(0x80, 0x60, 0x40));
      ui.text(bx - 2, plot.y + plot.h + 3 + (i % 2) * 11, E.win(labels[size_t(i)].first, labels[size_t(i)].second), "system9", kInkBlue);
    }
    cap1 = capText(E, el ? 0x28A : 0x1F3, el ? "Annual average production" : "Garbage by destination", "");
    cap2 = capText(E, el ? 0x289 : 0x1F8, el ? "Total %s (MW-h per year)" : "Total %s (tons per year)", groupDigits(int64_t(total)));
  }
  ui.textCentered({r.x + 55, r.y + 249, 340, 14}, cap1, "main10", kInkBlue);
  ui.textCentered({r.x + 55, r.y + 265, 340, 14}, cap2, "main10", rgb(230, 130, 0));
  static const uint32_t tid[5] = {0x1DD, 0x1E0, 0x1E1, 0x1DE, 0x1DF};
  static const char* const tfb[5] = {"Education", "Health", "Population", "Electricity", "Garbage"};
  for (int i = 0; i < 5; i++)
    if (radio(ui, E, "tab." + std::to_string(i), {r.x + 192, r.y + 314 + 20 * i - 6, 140, 12}, E.win(tid[i], tfb[i]), tab == i, "main10")) tab = i;
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
  ui.fill({r.x + 5, r.y + 30, 32, 405}, kBandBlue);   // mode strip, joined to the title bar in an L
  // Mode buttons: 26 x 26 at x = 8; data maps at y = 38 + 28 k, then after a two-slot gap Charts (374) and Graphs (402). Each image is a
  // 104 x 26 strip of four cells; the active mode shows the pressed cell.
  static const uint32_t img[12] = {0x01, 0x04, 0x10, 0x11, 0x07, 0x0C, 0x0F, 0x15, 0x17, 0x19, 0x03, 0x0B};
  static const char* const cap[12] = {"Au", "Cr", "De", "Po", "Fl", "LV", "Pl", "Tr", "Wa", "Zo", "Ch", "Gr"};
  for (int i = 0; i < 12; i++) {
    const int y = i < 10 ? 38 + 28 * i : i == 10 ? 374 : 402;
    const Rect b{r.x + 8, r.y + y, 26, 26};
    const std::string tip = i == 10 ? E.win(0x183, "Charts") : i == 11 ? E.win(0x184, "Graphs") : E.win(dataMapDefs()[size_t(i)].titleId, "Data Map");
    E.spot("mode." + std::to_string(i), b);
    bool c = false;
    if (!(ui.skin && ui.skin->imageButtonCells(ui, E.img(0x5E5, img[i]), 4, b, tip, mode == i, true, c))) {
      c = ui.button(b, cap[i], true, mode == i);
      if (ui.hover(b)) ui.tooltip = tip;
    }
    if (c) { setMode(i, src); R.add(Act::SelectDataMap, 0, i < 10 ? int(dataMapDefs()[size_t(i)].layer) : -1); }
  }
  if (mode < int(ModeCharts)) { if (map.selected != mode) map.select(mode, src); R.merge(map.drawBody(ui, E, r, src)); }
  else if (mode == int(ModeCharts)) R.merge(charts.drawBody(ui, E, r, cd));
  else { R.merge(graphs.drawBody(ui, E, r, series)); }
  return R;
}

}  // namespace m3k
