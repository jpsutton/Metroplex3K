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
// Pixel of the data-map palette 000005E5:21 (32 x 32): each row is one map's colour scale, taken without blending (spec 09 7.1.1: row 0 Aura,
// 1 white-to-red scales, 2 Density/Land Value, 4 base land and water, 5 Power, 6 Water, 7 R/C/I, 8 Government/Seaports/Airports/Landfill).
uint32_t palPx(const Image* pal, int row, int col, uint32_t fallback) {
  if (!pal || pal->empty() || row < 0 || row >= pal->h || col < 0) return fallback;
  return pal->px[size_t(row) * size_t(pal->w) + size_t(std::min(col, pal->w - 1))] | 0xFF000000u;
}
// Category swatch colour of the two-state and zone maps; falls back to MapWindow::legendColour without the palette.
uint32_t categoryColour(const Image* pal, DataLayer l, int i) {
  const uint32_t fb = MapWindow::legendColour(l, i, 0);
  switch (l) {
    case DataLayer::Power: return palPx(pal, 5, i, fb);
    case DataLayer::Water: return palPx(pal, 6, i, fb);
    case DataLayer::Zones: return i < 3 ? palPx(pal, 7, i, fb) : palPx(pal, 8, i - 3, fb);
    default: return fb;
  }
}
// Tint of a zone tile on the Zones map (observed on Berlin): pure colours by type and density, shade 255 / 206 / 148 from light to dense.
uint32_t zoneTint(const Image* pal, int cat, int lvl) {
  lvl = std::max(0, std::min(2, lvl));
  switch (cat) {
    case 0: { static const uint32_t c[3] = {rgb(0, 255, 0), rgb(0, 203, 0), rgb(0, 150, 0)}; return c[lvl]; }
    case 1: { static const uint32_t c[3] = {rgb(0, 0, 255), rgb(0, 0, 206), rgb(0, 0, 148)}; return c[lvl]; }
    case 2: { static const uint32_t c[3] = {rgb(255, 251, 0), rgb(206, 203, 0), rgb(148, 150, 0)}; return c[lvl]; }
    case 3: return rgb(239, 93, 0);
    case 6: return rgb(156, 101, 49);
    default: return categoryColour(pal, DataLayer::Zones, cat);   // seaports, airports: palette row 8 [unobserved on the map]
  }
}
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

void dataMapPaletteCell(DataLayer l, uint8_t val, int& row, int& col) {
  row = 1; col = val >> 3;   // Crime, Flammability, Pollution, Traffic: grey (low) .. red (high)
  if (l == DataLayer::Aura) { row = 0; col = (127 - (int(val) - 128)) >> 3; }   // aura -127..127: column 0 = good .. 31 = bad [INFERRED quantisation]
  else if (l == DataLayer::Density || l == DataLayer::LandValue) { row = 2; col = (255 - val) >> 3; }   // blue (high) .. light (low)
  col = std::max(0, std::min(31, col));
}

// Data-map mode of the View Data window (spec 09 7.1.1 [OBSERVED], coordinates relative to the window): the whole city as a diamond with
// corners (230,47) (42,234) (417,234) (230,424), compass at (63,60) (not on Pollution), "Use As Map" panel at the bottom left, the legend
// box at the bottom right, the coverage / pollution-kind controls in an inset at the top right.
WindowResult MapWindow::drawBody(Ui& ui, const WinEnv& E, Rect r, const MapSource& src) {
  WindowResult R;
  const DataMapDef* def = selected >= 0 && selected < int(dataMapDefs().size()) ? &dataMapDefs()[size_t(selected)] : nullptr;
  const bool pollution = def && def->layer == DataLayer::Pollution;
  const Rect mm{r.x + 42, r.y + 47, 376, 377};
  const std::vector<uint8_t>* vals = def && src.values ? src.values(def->layer) : nullptr;
  const Image* pal = E.img(0x5E5, 0x21);   // the data-map palette
  // Tile <-> diamond transform (spec 09 7.1.1). At the default view px = (z - x + W) s, py = (x + z) s with s = mm.w / 2W: tile x runs along the
  // top-left edge, z along the top-right edge (checked against the original's coverage circles). Each view rotation step turns the whole diamond
  // 90 degrees counter-clockwise on screen (the compass letters turn the same way; observed after one PageUp).
  const int rot = viewRotation & 3;
  const float sc = src.W > 0 ? float(mm.w) / float(2 * src.W) : 1.f;
  auto toPixel = [&](float x, float z, float& px, float& py) {
    float a = z - x, b = x + z - float(src.W);
    for (int k = 0; k < rot; k++) { const float t = a; a = b; b = -t; }   // counter-clockwise quarter turn (y down)
    px = (a + float(src.W)) * sc; py = (b + float(src.W)) * sc;
  };
  auto toTile = [&](int px, int py, int& x, int& z) {
    float a = float(px) / sc - float(src.W), b = float(py) / sc - float(src.W);
    for (int k = 0; k < rot; k++) { const float t = a; a = -b; b = t; }   // undo: clockwise quarter turn
    z = int(std::floor((a + b + float(src.W)) / 2.f)); x = int(std::floor((b - a + float(src.W)) / 2.f));
  };
  if (src.W > 0 && src.H > 0) {
    for (int py = 0; py < mm.h; py++)
      for (int px = 0; px < mm.w; px++) {
        int x, z;
        toTile(px, py, x, z);
        if (x < 0 || z < 0 || x >= src.W || z >= src.H) continue;
        float u = (float(px) + 0.5f) / sc - float(src.W), v = (float(py) + 0.5f) / sc - float(src.W);
        if (std::fabs(u) + std::fabs(v) > float(src.W)) continue;   // outside the diamond
        uint32_t col = src.baseColour ? src.baseColour(x, z) : rgb(90, 130, 70);
        if (def && vals && vals->size() >= size_t(src.W) * src.H) {
          uint8_t val = (*vals)[size_t(x) * src.H + z];
          const bool sentinel = def->layer == DataLayer::Aura || def->layer == DataLayer::Crime || def->layer == DataLayer::Fire ||
                                def->layer == DataLayer::LandValue || def->layer == DataLayer::Traffic;   // these use 0xFF = not tinted
          if (def->layer == DataLayer::Zones) { if (val != 0xFF) col = zoneTint(pal, val >> 2, val & 3); }   // pure colours by type and density, not the palette
          else if (categorical(def->layer)) { if (val != 0xFF) col = categoryColour(pal, def->layer, val); }
          else if (sentinel ? val != 0xFF : val != 0) {   // palette pixel, not a blend
            int row, c;
            dataMapPaletteCell(def->layer, val, row, c);
            col = pal ? palPx(pal, row, c, col) : lerpCol(col, MapWindow::legendColour(def->layer, 0, float(val) / 255.f), 0.7f);
          }
        }
        ui.at(mm.x + px, mm.y + py) = col;
      }
    E.spot("minimap", mm);
    if (ui.clicked(mm)) {
      int x, z;
      toTile(ui.in.mx - mm.x, ui.in.my - mm.y, x, z);
      R.add(Act::CenterView, 0, std::max(0, std::min(src.W - 1, x)), std::max(0, std::min(src.H - 1, z)));
    }
  }
  // Coverage circles (Crime: police stations, Flammability: fire stations): cyan outlines. A tile-space circle is a circle on the diamond, scaled by sqrt(2)
  // times the half-tile step.
  if (coverage && def && def->toggleId && src.circles && src.W > 0 && src.H > 0) {
    for (const auto& c : src.circles(def->layer == DataLayer::Crime)) {
      float fx, fy;
      toPixel(c[0], c[1], fx, fy);
      const float cx = float(mm.x) + fx, cy = float(mm.y) + fy;
      const float rad = c[2] * 1.41421356f * sc;
      const int n = std::max(24, int(rad * 6.28f));
      for (int k = 0; k < n; k++) {
        const float a = 6.2831853f * float(k) / float(n);
        const int px = int(cx + std::cos(a) * rad), py = int(cy + std::sin(a) * rad);
        if (px >= mm.x && py >= mm.y && px < mm.x + mm.w && py < mm.y + mm.h) ui.at(px, py) = rgb(0, 203, 255);
      }
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
      auto cl = [&](int i) { return categoryColour(pal, DataLayer::Zones, i); };
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
        ui.fill({r.x + 292, r.y + 363 + i * 33, 32, 33}, categoryColour(pal, def->layer, i));
        legendLabel(ui, lx, i ? ly1 : ly0, E.win(def->categories[size_t(i)], def->layer == DataLayer::Power ? pfb[i] : wfb[i]));
      }
    } else {
      insetBox(ui, std_, true);
      // The legend is the map's palette row drawn vertically, 2 px per column from y = 363 (32 columns over 64 px), x 292..323 [OBSERVED]; the
      // images 5E5:09 / 0A are 11-step ramps and are not used here.
      const int lrow = def->layer == DataLayer::Aura ? 0 : (def->layer == DataLayer::Density || def->layer == DataLayer::LandValue) ? 2 : 1;
      for (int col = 0; col < 32; col++) ui.fill({r.x + 292, r.y + 363 + 2 * col, 32, 2}, palPx(pal, lrow, col, lerpCol(rgb(255, 255, 255), rgb(220, 30, 30), float(col) / 31.f)));
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
    if (!(sk && sk->imageButtonCells(ui, sk->image(skin_group::UiBig, 0x22F96E31), 4, grid, tip, gridOn, true, c, Ui::kTipLeft))) c = ui.button(grid, "#", true, gridOn);
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
  // Stripes measured on the original: y 52..64 (222,223,255), then 17 px bands (edges 65, 82, ... 201) alternating with (231,231,255), within the plot y 52..218.
  ui.fill({box.x, box.y, box.w, box.h}, rgb(231, 231, 255));
  for (int y = 52; y < 218; y++) ui.fill({box.x, r.y + y, box.w, 1}, (y < 65 ? 0 : 1 + (y - 65) / 17) % 2 ? rgb(231, 231, 255) : rgb(222, 223, 255));
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
  ui.fill({r.x + 300, box.y + 1, 1, box.h - 2}, rgb(123, 138, 255));   // divider between the lines and their labels
  ui.fill({r.x + 44, r.y + 218, 256, 1}, rgb(128, 128, 128));         // axis line along the ticks
  if (nPts > 1) for (int k = 0; k < nPts; k++) ui.fill({x0 + k * (x1 - x0) / (nPts - 1), r.y + 219, 1, 4}, rgb(128, 128, 128));
  if (year) rightText(ui, r.x + 299, r.y + 221, std::to_string(year), "system9", rgb(128, 128, 128));
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
std::string capText(const WinEnv& E, uint32_t id, const char* fb, const std::string& val) {
  std::string t = E.win(id, fb);
  if (t.find("%s") != std::string::npos || t.find("%d") != std::string::npos) return subst(t, {val});
  return val.empty() ? t : t + " " + val;
}
std::string whole(double v) { return std::to_string(int64_t(std::llround(v))); }

struct PieSlice { std::string label; double value; uint32_t col; };

// 3-D pie (spec 09 7.1.1 charts pages): an ellipse of about 175 x 110 centred at c with a dark rim below it; slices run clockwise from the top
// and their percentages are drawn in black.
void pieChart(Ui& ui, int cx, int cy, const std::vector<PieSlice>& sl) {
  double total = 0; for (const auto& s : sl) total += s.value;
  if (total <= 0) return;
  const int rx = 87, ry = 48, rim = 12;
  std::vector<double> cum;   // cumulative fraction at the end of each slice
  double acc = 0; for (const auto& s : sl) { acc += s.value / total; cum.push_back(acc); }
  auto colourAt = [&](double frac) { for (size_t i = 0; i < sl.size(); i++) if (frac < cum[i] + 1e-9) return sl[i].col; return sl.back().col; };
  const double kPi = 3.14159265358979;
  for (int dy = ry + rim; dy >= -ry; dy--)   // rim first (lower), then the top face over it
    for (int dx = -rx; dx <= rx; dx++) {
      const double fx = double(dx) / rx;
      const double topY = double(dy) / ry;
      const bool top = fx * fx + topY * topY <= 1.0;
      const double rimY = double(dy - rim) / ry;
      const bool inRim = !top && fx * fx + rimY * rimY <= 1.0 && dy > 0;
      if (!top && !inRim) continue;
      const double ang = std::atan2(fx, -topY);   // 0 at the top, clockwise
      double frac = ang / (2 * kPi); if (frac < 0) frac += 1;
      uint32_t col = colourAt(frac);
      if (inRim) col = lerpCol(col, rgb(0, 0, 0), 0.45f);
      else if (fx * fx + topY * topY > 0.94) col = lerpCol(col, rgb(0, 0, 0), 0.35f);   // dark rim line on the top face edge
      ui.fill({cx + dx, cy + dy, 1, 1}, col);
    }
  acc = 0;
  for (size_t i = 0; i < sl.size(); i++) {
    const double f = sl[i].value / total, mid = (acc + f / 2) * 2 * kPi;
    acc += f;
    if (f < 0.04) continue;
    const std::string t = std::to_string(int(std::lround(f * 100))) + "%";
    const int px = cx + int(std::sin(mid) * rx * 0.62), py = cy - int(std::cos(mid) * ry * 0.62);
    ui.text(px - ui.textWidth(t, "main10") / 2, py - 6, t, "main10", 0);
  }
}
}  // namespace

// Charts mode (spec 09 7.1.1 [OBSERVED], coordinates relative to the window). Chart box (52,50)-(408,238), plot x 144..408 y 52..205, 15 five-year
// bars (7 px wide, pitch 13 from x = 149) on the age pages (Education, Health, Population), 3-D pies on Electricity and Garbage; captions centred at
// x 225 (y 249 / 265); the five page radios centred at x ~197 (y = 314 + 20 i).
WindowResult ChartsWindow::drawBody(Ui& ui, const WinEnv& E, Rect r, const ChartsData& d) {
  WindowResult R;
  const Rect box{r.x + 52, r.y + 50, 357, 189}, plot{r.x + 144, r.y + 52, 264, 153};
  sunkenLines(ui, box);
  const bool agePage = tab <= 2;
  // Stripe bands (spec 09 7.1.1): each page has its band period and the row of the first edge, counted from the plot top (y = 51); the Workforce
  // band draws them from the page's pink tile (5E5:31 Health 15 px, 33 Education 17 px, 35 Population 12 px), the rest of the plot draws the
  // same bands in lavender. The pie pages keep the Education pattern.
  struct Band { int period, edge1; uint32_t tile; };
  static const Band kBand[5] = {{17, 17, 0x33}, {15, 3, 0x31}, {12, 9, 0x35}, {17, 17, 0x33}, {17, 17, 0x33}};
  const Band bd = kBand[tab % 5];
  auto bandIndex = [&](int row) { return row < bd.edge1 ? 0 : 1 + (row - bd.edge1) / bd.period; };
  const uint32_t lavLight = rgb(231, 231, 255), lavDark = rgb(222, 223, 255);
  ui.fill({box.x + 1, box.y + 1, box.w - 2, box.h - 2}, lavLight);
  for (int row = 0; row < 155; row++)
    ui.fill({plot.x + 1, r.y + 51 + row, plot.w - 1, 1}, bandIndex(row) % 2 ? lavDark : lavLight);
  if (!agePage) ui.fill({box.x + 1, box.y + 1, box.w - 2, box.h - 2}, lavDark);   // the pie pages have a plain (222,223,255) background
  std::string cap1, cap2;
  if (agePage) {
    const uint32_t axis = rgb(0x18, 0x20, 0x6B);
    {   // Workforce band over about age 22..67: the page's 32 x 153 pink tile repeated over x 197..314, row 0 at y 51
      const Image* tile = E.img(0x5E5, bd.tile);
      for (int row = 0; row < 153; row++)
        for (int x = 197; x <= 314; x++) {
          uint32_t col = bandIndex(row) % 2 ? rgb(230, 198, 197) : rgb(238, 214, 222);
          if (tile && !tile->empty() && row < tile->h) { const uint32_t px = tile->px[size_t(row) * tile->w + size_t((x - 197) % tile->w)]; if (px >> 24) col = px | 0xFF000000u; }
          ui.at(r.x + x, r.y + 51 + row) = col;
        }
      ui.textCentered({r.x + 197, plot.y + 1, 118, 12}, E.win(0x1E5, "Workforce"), "system9", rgb(230, 130, 0));
    }
    ui.fill({plot.x, plot.y, 1, plot.h + 1}, axis); ui.fill({plot.x, plot.y + plot.h, plot.w, 1}, axis);
    const std::vector<float>& src = tab == 0 ? d.eq : tab == 1 ? d.lifeExpectancy : d.population;
    // y axis: Education 0..140 step 20, Health 0..90 step 10, Population 0..10 step 5 (thousand people per year of age); labels right-aligned at x 138
    const int step = tab == 0 ? 20 : tab == 1 ? 10 : 5, top = tab == 0 ? 140 : tab == 1 ? 90 : 10;
    for (int v = 0; v <= top; v += step) {
      const int cy = plot.y + plot.h - int(142.f * float(v) / float(top) + 0.5f);
      rightText(ui, r.x + 138, cy - 5, std::to_string(v), "system9", kInkBlue);
    }
    for (int n = 10; n <= 90; n += 10) {   // x labels, 26 px per 10 years, "10" at x ~ 164
      const std::string t = std::to_string(n);
      ui.text(r.x + 138 + 26 * n / 10 - ui.textWidth(t, "system9") / 2, r.y + 210, t, "system9", kInkBlue);
    }
    ui.textCentered({r.x + 176, r.y + 220, 200, 12}, E.win(0x1E6, "Resident Age"), "system9", kInkBlue);
    {   // vertical axis caption, rotated: Education Quotient on two lines, Health and Population on one
      const std::string t = E.win(tab == 0 ? 0x1E4u : tab == 1 ? 0x1E0u : 0x1E1u, tab == 0 ? "Education Quotient" : tab == 1 ? "Health" : "Population");
      const size_t sp = tab == 0 ? t.find(' ') : std::string::npos;
      const int hgt = ui.fonts->get("system9").height();
      auto put = [&](const std::string& s, int x) { verticalText(ui, x, r.y + 170 - ui.textWidth(s, "system9") / 2, s, "system9", kInkBlue); };
      if (sp == std::string::npos) put(t, r.x + 80 - hgt / 2);
      else { put(t.substr(0, sp), r.x + 68); put(t.substr(sp + 1), r.x + 68 + hgt + 2); }
    }
    for (int i = 0; i < 15; i++) {   // one bar per 5-year bucket, ages 0-4 .. 70-74
      float sum = 0; int n = 0;
      for (int a = i * 5; a < i * 5 + 5 && a < int(src.size()); a++) { sum += src[size_t(a)]; n++; }
      float v = n ? sum / float(n) : 0.f;
      if (tab == 2) v /= 1000.f;
      const int h = std::max(0, std::min(plot.h - 4, int(v * 142.f / float(top))));
      const int bx = r.x + 149 + 13 * i;
      if (h > 0) {
        ui.fill({bx, plot.y + plot.h - h, 7, h}, rgb(0, 211, 140));
        ui.fill({bx - 1, plot.y + plot.h - h, 1, h}, rgb(173, 178, 222)); ui.fill({bx + 7, plot.y + plot.h - h, 1, h}, rgb(173, 178, 222));
      }
    }
    if (tab == 0) { cap1 = capText(E, 0x1E2, "Population EQ = %s", whole(d.populationEQ)); cap2 = capText(E, 0x1E3, "Workforce EQ = %s", whole(d.workforceEQ)); }
    else if (tab == 1) { cap1 = capText(E, 0x1F9, "Population Life Expectancy = %s Years", whole(d.populationLE)); cap2 = capText(E, 0x1FA, "Workforce Life Expectancy = %s Years", whole(d.workforceLE)); }
    else { cap1 = capText(E, 0x1FC, "Total Population = %s", groupDigits(d.totalPopulation)); cap2 = capText(E, 0x1FD, "Workforce = %s%% of Population", std::to_string(d.workforcePercent)); }
  } else {
    const bool el = tab == 3;
    // Colours of the categories seen in the original (spec 09 7.1.1); the others are not observed and are mine.
    struct Cat { uint32_t id; const char* fb; uint32_t col; double v; };
    std::vector<Cat> cats;
    double total = 0;
    if (el) {
      const Cat L[10] = {{0x1E8, "Coal Power", rgb(132, 65, 66), d.electricity[0]}, {0x1E9, "Oil Power", rgb(90, 60, 110), d.electricity[1]},
                         {0x1EA, "Gas Power", rgb(200, 200, 255), d.electricity[2]}, {0x1EB, "Wind Power", rgb(255, 130, 0), d.electricity[3]},
                         {0x1EC, "Solar Power", rgb(0, 255, 0), d.electricity[4]}, {0x1ED, "Nuclear Power", rgb(255, 255, 0), d.electricity[5]},
                         {0x1EE, "Microwave Power", rgb(255, 0, 255), d.electricity[6]}, {0x1EF, "Fusion Power", rgb(0, 255, 255), d.electricity[7]},
                         {0x1F0, "Waste-to-Energy", rgb(120, 200, 80), d.electricity[8]}, {0x1F1, "Neighbor Deals", rgb(180, 180, 180), d.electricity[9]}};
      for (const Cat& c : L) { cats.push_back(c); total += c.v; }
    } else {
      const Cat L[5] = {{0x1F4, "Landfill", rgb(247, 56, 231), d.garbage[0]}, {0x1F1, "Neighbor Deals", rgb(150, 150, 150), d.garbage[1]},
                        {0x1F5, "Incinerators", rgb(230, 110, 40), d.garbage[2]}, {0x1F6, "Recycling", rgb(57, 69, 247), d.garbage[3]},
                        {0x1F7, "Collecting in Streets", rgb(110, 110, 60), d.garbage[4]}};
      for (const Cat& c : L) { cats.push_back(c); total += c.v; }
    }
    // heading at the top left of the box, then one colour bar per category present
    ui.text(box.x + 6, box.y + 4, E.win(el ? 0x28A : 0x1F3, el ? "Annually Averaged" : "% Garbage Goes To:"), "main10", kInkBlue);
    std::vector<PieSlice> slices;
    int by = box.y + 22;
    for (const Cat& c : cats) {
      if (c.v <= 0) continue;
      ui.fill({box.x + 6, by, 168, 16}, c.col);
      const uint32_t lum = ((c.col & 255) * 30 + ((c.col >> 8) & 255) * 59 + ((c.col >> 16) & 255) * 11) / 100;
      ui.text(box.x + 10, by + 2, E.win(c.id, c.fb), "main10", lum > 140 ? rgb(0, 0, 0) : rgb(255, 255, 255));
      by += 18;
      slices.push_back({"", c.v, c.col});
    }
    pieChart(ui, r.x + 318, r.y + 117, slices);
    cap1 = capText(E, el ? 0x289 : 0x1F8, el ? "Total Electricity Usage Annually = %s MW-h" : "Total Garbage Produced Annually = %s Tons", groupDigits(int64_t(total)));
  }
  ui.textCentered({r.x + 55, r.y + 249, 340, 14}, cap1, "main10", kInkBlue);
  if (!cap2.empty()) ui.textCentered({r.x + 55, r.y + 265, 340, 14}, cap2, "main10", rgb(230, 130, 0));
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
    if (!(ui.skin && ui.skin->imageButtonCells(ui, E.img(0x5E5, img[i]), 4, b, tip, mode == i, true, c, Ui::kTipBelow))) {
      c = ui.button(b, cap[i], true, mode == i);
      if (ui.hover(b)) ui.setTip(tip, b, Ui::kTipBelow);
    }
    if (c) { setMode(i, src); R.add(Act::SelectDataMap, 0, i < 10 ? int(dataMapDefs()[size_t(i)].layer) : -1); }
  }
  if (mode < int(ModeCharts)) { if (map.selected != mode) map.select(mode, src); R.merge(map.drawBody(ui, E, r, src)); }
  else if (mode == int(ModeCharts)) R.merge(charts.drawBody(ui, E, r, cd));
  else { R.merge(graphs.drawBody(ui, E, r, series)); }
  return R;
}

}  // namespace m3k
