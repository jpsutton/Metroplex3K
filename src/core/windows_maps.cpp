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
const uint32_t kBandBlue = rgb(123, 146, 255), kLabelInk = rgb(123, 125, 181), kInkBlue = rgb(49, 52, 123), kAxisInk = rgb(49, 56, 132);   // radio/caption ink (49,52,123); the axis ticks and axis titles keep (49,56,132) [OBSERVED]

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
  const uint32_t ink = rgb(49, 52, 123), grey = rgb(123, 130, 181), white = rgb(255, 255, 255);   // disabled label ink (123,130,181); label ink (49,52,123) [OBSERVED]
  if (frame(ui, E, r, E.win(0x269, "View City Layers"))) { R.close = true; R.add(Act::Close); }
  blueBand(ui, {r.x + 4, r.y + 39, 142, 34});
  blueBand(ui, {r.x + 150, r.y + 39, 142, 34});
  blueBand(ui, {r.x + 4, r.y + 185, 142, 34});
  blueBand(ui, {r.x + 4, r.y + 256, 288, 76});

  // Group header: a toggle picking the mode. On = white face with an inset outline (pressed), off = lavender face
  // with a navy outline and a dark drop shadow.
  auto header = [&](const std::string& name, int x, int y, const std::string& label, int mode, int labelDy = 0) {
    Rect f{r.x + x, r.y + y, 132, 20};
    E.spot(name, f);
    const bool on = edit.mode == mode, down = ui.hover(f) && ui.in.down;
    const Image* hdrArt = sk ? sk->image(skin_group::Sys, 0xE2B99E02) : nullptr;   // 120 x 30, 4 cells of 30: 3-slice 10 / 10 tiled / 10, cell 2 on, cell 1 off [OBSERVED, 100 % of pixels]
    if (hdrArt && hdrArt->w >= 120 && hdrArt->h >= 30) {
      const int cell = on || down ? 2 : 1, ax = f.x - 2, ay = f.y - 4, tw = 138;
      ui.blitRect(*hdrArt, ax, ay, cell * 30, 0, 10, 30);
      for (int off = 10; off < tw - 10; off += 10) ui.blitRect(*hdrArt, ax + off, ay, cell * 30 + 10, 0, std::min(10, tw - 10 - off), 30);
      ui.blitRect(*hdrArt, ax + tw - 10, ay, cell * 30 + 20, 0, 10, 30);
    } else if (on || down) {
      // The original's plate art [OBSERVED, column x = 100]: a 3-row darkening ring above (and a 1 px one left of) the white field, the field one row lower, its
      // inner top line 2 rows below that and the inner bottom line on its last row, then the two shadow rows and a 3-row light glow.
      ui.fill({f.x, f.y - 2, f.w, 1}, rgb(123, 138, 247)); ui.fill({f.x, f.y - 1, f.w, 1}, rgb(107, 130, 231)); ui.fill({f.x, f.y, f.w, 1}, rgb(99, 113, 198));
      ui.fill({f.x - 1, f.y + 1, 1, f.h - 1}, rgb(107, 130, 231));
      ui.fill({f.x, f.y + 1, f.w, f.h - 1}, white);
      ui.fill({f.x + 2, f.y + 3, f.w - 4, 1}, rgb(123, 125, 189)); ui.fill({f.x + 2, f.y + f.h - 1, f.w - 4, 1}, rgb(123, 125, 181));
      ui.fill({f.x + 2, f.y + 3, 1, f.h - 3}, rgb(123, 125, 189)); ui.fill({f.x + f.w - 3, f.y + 3, 1, f.h - 3}, rgb(123, 125, 189));
      ui.fill({f.x + 1, f.y + f.h, f.w, 1}, rgb(0xC6, 0xCB, 0xDE)); ui.fill({f.x + 2, f.y + f.h + 1, f.w, 1}, rgb(0xA5, 0xA6, 0xB5));
      ui.fill({f.x + 1, f.y + f.h + 2, f.w, 1}, rgb(173, 186, 255)); ui.fill({f.x + 1, f.y + f.h + 3, f.w, 1}, rgb(156, 170, 255)); ui.fill({f.x + 1, f.y + f.h + 4, f.w, 1}, rgb(140, 154, 255));
    } else {
      // Raised plate with the original's soft shadow [OBSERVED, columns x = 250 and row y = 86]: a faint ring above/left, a 2-row/2-column highlight, a navy
      // outline (top/left (49,56,132), bottom (49,52,123), right (49,56,123)), the field, and a shadow fading over 7 rows below / 6 columns right.
      const int W = 130;
      ui.fill({f.x, f.y - 2, W, 1}, rgb(123, 138, 247)); ui.fill({f.x, f.y - 1, W, 1}, rgb(107, 130, 231));
      ui.fill({f.x - 1, f.y, 1, 20}, rgb(107, 130, 231));
      ui.fill({f.x, f.y, W, 20}, rgb(214, 219, 255));
      ui.fill({f.x, f.y, W, 1}, rgb(231, 235, 255)); ui.fill({f.x, f.y + 1, W, 1}, rgb(222, 227, 255));
      ui.fill({f.x, f.y + 2, 1, 17}, rgb(239, 235, 255)); ui.fill({f.x + 1, f.y + 2, 1, 17}, rgb(231, 231, 255));
      ui.fill({f.x + 2, f.y + 2, W - 2, 1}, rgb(49, 56, 132)); ui.fill({f.x + 2, f.y + 2, 1, 17}, rgb(49, 56, 132));
      ui.fill({f.x + 2, f.y + 18, W - 2, 1}, rgb(49, 52, 123)); ui.fill({f.x + W - 1, f.y + 2, 1, 17}, rgb(49, 56, 123));
      static const uint32_t below[7] = {rgb(148, 146, 173), rgb(107, 105, 123), rgb(24, 32, 57), rgb(49, 56, 99), rgb(74, 89, 156), rgb(99, 113, 198), rgb(107, 130, 231)};
      static const uint32_t side[6] = {rgb(156, 162, 189), rgb(107, 113, 132), rgb(24, 32, 57), rgb(49, 56, 99), rgb(74, 89, 156), rgb(99, 121, 214)};
      for (int k = 0; k < 7; k++) ui.fill({f.x + 3, f.y + 19 + k, W - 3 + (k < 6 ? 1 + k : 0), 1}, below[k]);
      for (int k = 0; k < 6; k++) ui.fill({f.x + W + k, f.y + 3, 1, 16 + (k < 5 ? 1 : 0)}, side[k]);
    }
    const int o = down && !on ? 1 : 0;   // the on-state caption sits where the off-state one does [OBSERVED]
    ui.textCentered({f.x + o, f.y + o + 1 + labelDy, f.w, f.h}, label, "main9", ink);   // the next smaller bold face [OBSERVED: ink 85 px against 88]
    if (ui.clicked(f)) edit.mode = mode;
  };
  // Check box (GZCheckBox) / radio (GZRadioBtn) with a label; greyed out while its group's mode is not selected.
  const Image* checkArt = sk ? sk->get("check") : nullptr;
  const Image* radioArt = sk ? sk->get("radio") : nullptr;
  auto control = [&](const std::string& name, bool radio, int x, int y, const std::string& label, bool value, bool enabled, int labelDx = -1) {
    const int box = radio ? 12 : 13;
    Rect hit{r.x + x, r.y + y, box + 4 + ui.textWidth(label, "system9"), box + 2};
    E.spot(name, hit);
    const bool hot = enabled && ui.hover(hit);
    if (!(sk && sk->drawCheck(ui, radio ? radioArt : checkArt, r.x + x, r.y + y, value, enabled, hot, hot && ui.in.down))) {
      ui.fill({r.x + x, r.y + y, box, box}, enabled ? white : rgb(0xDE, 0xDF, 0xEF));
      ui.frame({r.x + x, r.y + y, box, box}, false);
      if (value) ui.fill({r.x + x + 3, r.y + y + 3, box - 6, box - 6}, enabled ? ink : grey);
    }
    ui.text(r.x + x + box + 3 + labelDx, r.y + y + (box - ui.fonts->get("system9").height()) / 2, label, "system9", enabled ? ink : grey);   // labels 1 px further left [OBSERVED]
    return enabled && ui.clicked(hit);
  };

  // ---- Above Ground
  header("mode.above", 8, 45, E.win(0x26A, "Above Ground:"), CityLayers::AboveGround, -1);   // left column: 1 px higher than first drawn [OBSERVED]
  struct Check { const char* name; uint32_t id; const char* fb; bool* v; };
  const Check checks[6] = {{"transport", 0x26E, "Transportation", &edit.transport}, {"powerLines", 0x26F, "Power Lines", &edit.powerLines},
                           {"flora", 0x270, "Flora", &edit.flora}, {"zonedBuildings", 0x271, "Zoned Buildings", &edit.zonedBuildings},
                           {"otherBuildings", 0x272, "Other Buildings", &edit.otherBuildings}, {"zones", 0x273, "Zones", &edit.zones}};
  const bool above = edit.mode == CityLayers::AboveGround;
  for (int i = 0; i < 6; i++)
    if (control(std::string("above.") + checks[i].name, false, 12, 75 + 18 * i, E.win(checks[i].id, checks[i].fb), *checks[i].v, above))
      *checks[i].v = !*checks[i].v;

  // ---- Underground
  header("mode.under", 8, 191, E.win(0x280, "Underground:"), CityLayers::Underground, -1);
  const bool under = edit.mode == CityLayers::Underground;
  if (control("under.subways", true, 12, 222, E.win(0x274, "Subways"), edit.underground == 0, under)) edit.underground = 0;
  if (control("under.pipes", true, 12, 238, E.win(0x275, "Water Pipes"), edit.underground == 1, under)) edit.underground = 1;

  // ---- City Layer Views
  header("mode.layers", 154, 45, E.win(0x26B, "City Layer Views:"), CityLayers::LayerView, -1);
  const bool layers = edit.mode == CityLayers::LayerView;
  const auto& views = cityLayerViews();
  for (size_t i = 0; i < views.size(); i++)
    if (control("layer." + std::to_string(i), true, 158, 76 + 16 * int(i), E.win(views[i].labelId, views[i].fallback), edit.layer == int(i), layers))   // glyph x 191..199, label x 204 [OBSERVED]
      edit.layer = int(i);

  // ---- Buttons
  auto pill = [&](const char* name, Rect b, const std::string& label) { E.spot(name, b); ui.buttonLabelDy = -1; const bool c = ui.button(b, label, true, false, "title9"); ui.buttonLabelDy = 0; return c; };   // caption 1 px up [OBSERVED]
  if (pill("apply", {r.x + 8, r.y + 260, 106, 33}, E.win(0x26D, "Apply"))) R.add(Act::ApplyLayers);
  if (pill("default", {r.x + 8, r.y + 293, 106, 33}, E.win(0x26C, "Default View"))) { edit = CityLayers(); R.add(Act::ApplyLayers); }
  Rect grid{r.x + 249, r.y + 260, 36, 35};
  E.spot("grid", grid);
  { bool c = false;
    std::string tip = E.gui(0x74, "Grid");
    if (!(sk && sk->imageButtonCells(ui, sk->image(skin_group::UiBig, 0x45CBC2F3), 4, grid, tip, gridOn, true, c, Ui::kTipLeft))) c = ui.button(grid, "#", true, gridOn);
    if (c) R.add(Act::ToggleGrid); }
  Rect done{r.x + 246, r.y + 295, 42, 32};
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
  if (range == 0) for (int i = 10; i >= 0; i--) v.push_back(c.monthly[i]);   // the 12-month plot skips the oldest slot; the caller appends the live value [OBSERVED]
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
  ui.fill({r.x + 4, r.y + 6, 1, 431}, rgb(173, 182, 255)); ui.fill({r.x + 37, r.y + 6, 1, 431}, rgb(173, 182, 255));   // icon column: borders at x 44 and x 77   // the frame's left line runs the full height [OBSERVED]
  const Rect box{r.x + 44, r.y + 50, 356, 189};
  // Stripes measured on the original (screen rows 85.. = local 51..): base (222,223,255) 51..64, then 17 px bands (edges 65, 82, ... 201) alternating
  // with (231,231,255) through local 217; below the 2 px axis (local 218..219) the base colour again to the frame (local 220..237).
  ui.fill({box.x, box.y, box.w, box.h}, rgb(222, 223, 255));
  for (int y = 51; y < 218; y++) ui.fill({box.x, r.y + y, 257, 1}, (y < 65 ? 0 : 1 + (y - 65) / 17) % 2 ? rgb(231, 231, 255) : rgb(222, 223, 255));
  // Frame: dark top/left (123,142,255), white right/bottom [OBSERVED].
  ui.fill({box.x, box.y, box.w, 1}, rgb(123, 142, 255)); ui.fill({box.x, box.y, 1, box.h}, rgb(123, 142, 255));
  ui.fill({box.x + box.w - 1, box.y, 1, box.h}, rgb(255, 255, 255)); ui.fill({box.x, box.y + box.h - 1, box.w, 1}, rgb(255, 255, 255));
  const int x0 = r.x + 46, x1 = r.x + 300, yTop = r.y + 56, yBot = r.y + 214;
  struct Lab { std::string text; int endY; uint32_t col; int y = 0; };
  std::vector<Lab> labs;
  int nPts = 0;
  for (size_t i = 0; i < series.size() && i < 16; i++) {
    if (!(enabledMask & (1u << i)) || !series[i].chart) continue;
    auto s = samples(*series[i].chart, range);
    if (range == 0) s.push_back(uint32_t(series[i].live >= 0 ? series[i].live : int64_t(series[i].chart->monthly[0])));   // 12th point = the live current value (the newest saved sample when unknown)
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
  ui.fill({r.x + 300, box.y + 1, 1, box.h - 2}, rgb(173, 178, 222));   // the "now" line between the lines and their labels (axis colour)
  ui.fill({r.x + 44, r.y + 218, 256, 2}, rgb(173, 178, 222));         // axis, 2 px
  // Ticks: 1 x 2 px at rows 254..255, one per sample from x 85 to the now line (12 ticks, ~23.2 px apart on a 1-year view) [OBSERVED].
  if (nPts > 1) for (int k = 0; k < nPts; k++) ui.fill({r.x + 45 + k * 255 / (nPts - 1), r.y + 220, 1, 2}, rgb(173, 178, 222));
  // The year label (axis colour) is centred under the tick span it names: the second-to-last tick [INFERRED from one same-length capture].
  if (year) {
    // 12-month view: January's tick = (n - 1) - (month - 1) [OBSERVED: March -> tick 9 of 12]; other ranges: the second-to-last tick [INFERRED].
    int jt = nPts - 2;
    if (range == 0 && month >= 1 && month <= 12 && nPts > 12 - 1) jt = std::max(0, nPts - month);
    const int tx = nPts > 2 ? r.x + 45 + jt * 255 / (nPts - 1) : r.x + 253;
    ui.text(tx - ui.textWidth(std::to_string(year), "system9") / 2, r.y + 221, std::to_string(year), "system9", rgb(173, 178, 222));
  }
  // check-box grid: three columns of 5, 6 and 5
  static const int colSize[3] = {5, 6, 5}, colX[3] = {58, 178, 298};
  size_t idx = 0;
  for (int c = 0; c < 3; c++)
    for (int row = 0; row < colSize[c] && idx < series.size() && idx < 16; row++, idx++) {
      const bool on = (enabledMask & (1u << idx)) != 0;
      const Rect cb{r.x + colX[c], r.y + 252 + 24 * row, 12, 12};
      const Rect hit{cb.x, cb.y, 112, 14};
      E.spot("series." + std::to_string(idx), hit);
      // The same check-box art as the Preferences window (dark outline, dark check mark) [OBSERVED].
      const Image* checkArt = ui.skin ? ui.skin->get("check") : nullptr;
      const bool hot = ui.hover(hit);
      if (!(ui.skin && ui.skin->drawCheck(ui, checkArt, cb.x, cb.y, on, true, hot, hot && ui.in.down))) {
        ui.fill(cb, rgb(255, 255, 255)); sunkenLines(ui, cb);
        if (on) {
          for (int k = 0; k < 3; k++) ui.fill({cb.x + 2 + k, cb.y + 5 + k, 1, 2}, kInkBlue);
          for (int k = 0; k < 5; k++) ui.fill({cb.x + 4 + k, cb.y + 7 - k, 1, 2}, kInkBlue);
        }
      }
      ui.text(cb.x + 15, cb.y + 1 + (12 - ui.fonts->get("system9").height()) / 2, E.win(series[idx].labelId, series[idx].fallback), "system9", rgb(49, 52, 123));
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
  d.workforceEQ = p.qEQ(20, 65);   // Q(20, 65) [OBSERVED: Berlin 119.55 -> 120]
  d.populationLE = p.cityLifeExpectancy();
  double s = 0; int n = 0;
  for (int i = 20; i <= 65 && i <= p.oldestAge; i++) if (p.peopleAt(i, cityPop) > 0) { s += p.le[i]; n++; }
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
      const double ang = std::atan2(-fx, -topY);   // 0 at 12 o'clock, COUNTER-clockwise in legend order [OBSERVED]
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
    const int px = cx - int(std::sin(mid) * rx * 0.62), py = cy - int(std::cos(mid) * ry * 0.62);
    ui.text(px - ui.textWidth(t, "main10") / 2, py - 6, t, "main10", 0);
  }
}
// The original's pie control (spec 09 7.1.1 [STATIC, SIMUI 0x100749E5]): a 172 x 86 ellipse mask stamped 23 times (ellipse plus 22 px of rim), radial lines in the
// slice colours, each slice flood-filled from its label anchor, then the 172 x 108 grey shading image applied per channel (the dither is baked into it), then the
// percentages. Counter-clockwise from 12 o'clock. Returns false when the art is missing.
bool pieChartArt(Ui& ui, const WinEnv& E, int ox, int oy, const std::vector<PieSlice>& sl) {
  const Image* mask = E.img(0xC2BF3646u, 0x62D32734u);
  const Image* shade = E.img(0xC2BF3646u, 0x02D32710u);
  if (!mask || mask->empty() || !shade || shade->empty() || sl.empty()) return false;
  const int W = 172, H = 108, kRim = 22, cx = 86, cy = 43;
  double total = 0; for (const auto& s : sl) total += s.value;
  if (total <= 0) return false;
  total = std::max(total, 1e-4);
  const uint32_t kBase = 0x01010100u, kNone = 0u;
  std::vector<uint32_t> buf(size_t(W) * H, kNone);
  for (int k = 0; k <= kRim; k++)
    for (int y = 0; y < mask->h && y + k < H; y++)
      for (int x = 0; x < mask->w && x < W; x++)
        if (mask->px[size_t(y) * mask->w + x] >> 24) buf[size_t(y + k) * W + x] = kBase;
  auto put = [&](int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < W && y < H && buf[size_t(y) * W + x] != kNone) buf[size_t(y) * W + x] = c; };
  auto line = [&](int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int g = 0; g < 1000; g++) {
      put(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      const int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  };
  auto fill = [&](int sx, int sy, uint32_t c) {
    if (sx < 0 || sy < 0 || sx >= W || sy >= H || buf[size_t(sy) * W + sx] != kBase) return;
    std::vector<std::pair<int, int>> st{{sx, sy}};
    while (!st.empty()) {
      auto [x, y] = st.back(); st.pop_back();
      if (x < 0 || y < 0 || x >= W || y >= H || buf[size_t(y) * W + x] != kBase) continue;
      buf[size_t(y) * W + x] = c;
      st.push_back({x + 1, y}); st.push_back({x - 1, y}); st.push_back({x, y + 1}); st.push_back({x, y - 1});
    }
  };
  const double kTwoPi = 6.2831855, kHalfPi = 1.5707964;
  auto edge = [&](double f, int& ex, int& ey) { const double th = f * kTwoPi + kHalfPi; ex = cx + int(86.0 * std::cos(th)); ey = cy - int(43.0 * std::sin(th)); };
  struct Lab { int ax, ay; double frac; uint32_t col; };
  std::vector<Lab> labs;
  double acc = 0;
  for (const auto& s : sl) {
    const double f = s.value / total, f0 = acc, f1 = acc + f;
    acc = f1;
    int sx, sy, ex, ey, mx, my;
    edge(f0, sx, sy); edge(f1, ex, ey); edge((f0 + f1) / 2, mx, my);
    const double k = f > 0.1 ? 0.6 : 0.8;
    const int ax = cx + int(double(mx - cx) * k), ay = cy + int(double(my - cy) * k);
    line(cx, cy, sx, sy, s.col); line(cx, cy, ex, ey, s.col);
    for (auto p : {std::pair<int, int>{sx, sy}, std::pair<int, int>{ex, ey}}) if (p.second > cy) for (int d = 1; d <= kRim; d++) put(p.first, p.second + d, s.col);
    if (ax != ex || ay != ey) fill(ax, ay, s.col);
    labs.push_back({ax, ay, f, s.col});
  }
  // Shading: per channel, s < 125 darkens, 125..131 unchanged, >= 132 lightens.
  for (int y = 0; y < H && y < shade->h; y++)
    for (int x = 0; x < W && x < shade->w; x++) {
      uint32_t& d = buf[size_t(y) * W + x];
      if (d == kNone) continue;
      if (d == kBase) d = labs.empty() ? kBase : labs.back().col;
      const int sg = int(shade->px[size_t(y) * shade->w + x] & 255);
      // The original's canvas is RGB565: unpack c8 = c5 * 255 / 31 (g6 * 255 / 63), shade, pack by truncation, unpack again [OBSERVED, 1153/1153 px].
      int ch[3] = {int(d & 255) >> 3, int((d >> 8) & 255) >> 2, int((d >> 16) & 255) >> 3};
      const int mx[3] = {31, 63, 31};
      for (int i = 0; i < 3; i++) {
        int c = ch[i] * 255 / mx[i];
        if (sg < 125) c = c * sg / 128;
        else if (sg >= 132) c = c + (sg - 128) * (255 - c) / 128;
        ch[i] = (c >> (i == 1 ? 2 : 3)) * 255 / mx[i];
      }
      d = 0xFF000000u | (uint32_t(ch[2]) << 16) | (uint32_t(ch[1]) << 8) | uint32_t(ch[0]);
    }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      if (buf[size_t(y) * W + x] != kNone && ox + x >= 0 && oy + y >= 0 && ox + x < ui.W && oy + y < ui.H) ui.at(ox + x, oy + y) = buf[size_t(y) * W + x];
  for (const Lab& l : labs) {
    const std::string t = std::to_string(int(std::lround(l.frac * 100))) + "%";
    const int sum = int(l.col & 255) + int((l.col >> 8) & 255) + int((l.col >> 16) & 255);
    ui.text(ox + l.ax - ui.textWidth(t, "main10") / 2, oy + l.ay - 6, t, "main10", sum < 385 ? rgb(255, 255, 255) : rgb(0, 0, 0));   // white on dark slices
  }
  return true;
}
}  // namespace

// Charts mode (spec 09 7.1.1 [OBSERVED], coordinates relative to the window). Chart box (52,50)-(408,238), plot x 144..408 y 52..205, 15 five-year
// bars (7 px wide, pitch 13 from x = 149) on the age pages (Education, Health, Population), 3-D pies on Electricity and Garbage; captions centred at
// x 225 (y 249 / 265); the five page radios centred at x ~197 (y = 314 + 20 i).
WindowResult ChartsWindow::drawBody(Ui& ui, const WinEnv& E, Rect r, const ChartsData& d) {
  WindowResult R;
  const Rect box{r.x + 52, r.y + 50, 357, 189}, plot{r.x + 144, r.y + 52, 264, 153};
  // Frame: (123,142,255) left / top, white bottom [OBSERVED]; outside the plot the interior is (222,223,255).
  const bool agePage = tab <= 2;
  const uint32_t frameTL = agePage ? rgb(123, 142, 255) : rgb(123, 138, 255), frameBR = agePage ? rgb(255, 255, 255) : rgb(255, 243, 255);   // the pie pages' frame colours differ [OBSERVED]
  ui.fill({box.x, box.y, 1, box.h}, frameTL); ui.fill({box.x, box.y, box.w, 1}, frameTL);
  ui.fill({box.x + box.w - (agePage ? 1 : 2), box.y, 1, box.h}, frameBR); ui.fill({box.x, box.y + box.h - 1, box.w, 1}, frameBR);
  // Stripe bands (spec 09 7.1.1): each page has its band period and the row of the first edge, counted from the plot top (y = 51); the Workforce
  // band draws them from the page's pink tile (5E5:31 Health 15 px, 33 Education 17 px, 35 Population 12 px), the rest of the plot draws the
  // same bands in lavender. The pie pages keep the Education pattern.
  struct Band { int period, edge1; uint32_t tile; };
  static const Band kBand[5] = {{17, 17, 0x33}, {15, 3, 0x31}, {12, 9, 0x35}, {17, 17, 0x33}, {17, 17, 0x33}};
  const Band bd = kBand[tab % 5];
  auto bandIndex = [&](int row) { return row < bd.edge1 ? 0 : 1 + (row - bd.edge1) / bd.period; };
  const uint32_t lavLight = rgb(231, 231, 255), lavDark = rgb(222, 223, 255);
  ui.fill({box.x + 1, box.y + 1, box.w - 2, box.h - 2}, lavDark);
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
      ui.textCentered({r.x + 198, plot.y, 118, 12}, E.win(0x1E5, "Workforce"), "main9", rgb(214, 142, 90));
    }
    // Axes: light (173,178,222), 2 px: vertical x 183..184 over the full box height, horizontal rows 238..239 [OBSERVED].
    (void)axis;
    ui.fill({r.x + 143, r.y + 51, 2, 187}, rgb(173, 178, 222)); ui.fill({r.x + 143, r.y + 204, plot.w + 1, 2}, rgb(173, 178, 222));
    const std::vector<float>& src = tab == 0 ? d.eq : tab == 1 ? d.lifeExpectancy : d.population;
    // y axis: Education 0..140 step 20, Health 0..90 step 10, Population 0..10 step 5 (thousand people per year of age); labels right-aligned at x 138
    // Population: percent of the people per 5-year bucket, 10.2 px per unit, labels 0 / 5 / 10 [OBSERVED 800 x 600; the unit is INFERRED].
    const int step = tab == 0 ? 20 : tab == 1 ? 10 : 5, top = tab == 0 ? 140 : tab == 1 ? 90 : 10;
    const float pxPerUnit = tab == 2 ? 10.2f : 142.f / float(top);
    for (int v = 0; v <= top; v += step) {
      // Label tops 235 - round(v * 143 / top) on the original (Education: 235, 215, 194, 174, 153, 133, 112, 92), right-aligned at x 160.
      const int ly = 198 - int(143.f * float(v) / float(top) + 0.5f);
      rightText(ui, r.x + 139, r.y + ly, std::to_string(v), "main9", kAxisInk);
    }
    for (int n = 10; n <= 90; n += 10) {   // x labels, 26 px per 10 years, "10" at x ~ 164
      const std::string t = std::to_string(n);
      ui.text(r.x + 138 + 26 * n / 10 - ui.textWidth(t, "main9") / 2 - (n > 10 ? 0 : 0), r.y + 208, t, "main9", kAxisInk);   // the bold face like the y ticks   // row y244, lefts 199 / 223 / 249 ... [OBSERVED]
    }
    ui.text(r.x + 237, r.y + 219, E.win(0x1E6, "Resident Age"), "main9", kAxisInk);   // "R" ink from x277, rows y256..264 [OBSERVED]   // bold, ink from x257 [OBSERVED]
    {   // vertical axis caption, rotated: Education Quotient on two lines, Health and Population on one
      const std::string t = E.win(tab == 0 ? 0x1E4u : tab == 1 ? 0x1E0u : 0x1E1u, tab == 0 ? "Education Quotient" : tab == 1 ? "Health" : "Population");
      const size_t sp = tab == 0 ? t.find(' ') : std::string::npos;
      const int hgt = ui.fonts->get("system9").height();
      auto put = [&](const std::string& s, int x) { verticalText(ui, x, r.y + 170 - ui.textWidth(s, "main9") / 2, s, "main9", kAxisInk); };
      if (sp == std::string::npos) put(t, r.x + 80 - hgt / 2);
      else { put(t.substr(0, sp), r.x + 67); put(t.substr(sp + 1), r.x + 81); }   // ink columns x110 and x124 [OBSERVED]
    }
    for (int i = 0; i < 15; i++) {   // one bar per 5-year bucket, ages 0-4 .. 70-74
      float sum = 0; int n = 0;
      // Education / Health buckets are Q-style means: the unweighted mean over the ages of the bucket that have people (spec 20 4.5) [OBSERVED].
      for (int a = i * 5; a < i * 5 + 5 && a < int(src.size()); a++) {
        if (tab < 2 && a < int(d.population.size()) && d.population[size_t(a)] <= 0.f) continue;
        sum += src[size_t(a)]; n++;
      }
      float v = n ? sum / float(n) : 0.f;
      if (tab == 2) v = d.totalPopulation > 0 ? sum * 100.f / float(d.totalPopulation) : 0.f;   // percent of the population in the bucket
      const int h = std::max(0, std::min(plot.h - 4, int(v * pxPerUnit + (tab == 2 ? 0.5f : 0.f))));
      // Outlines: right at x 196 + 13 k; left at 188 + 13 k for the first three bars and 187 + 13 k from the fourth: fills of 7, then 8 px [OBSERVED].
      const int fx0 = r.x + 149 + 13 * i - (i >= 3 ? 1 : 0), fw = i >= 3 ? 8 : 7;
      if (h > 0) {
        ui.fill({fx0, plot.y + plot.h - h, fw, h}, rgb(0, 211, 140));
        ui.fill({fx0 - 1, plot.y + plot.h - h, 1, h}, rgb(173, 178, 222)); ui.fill({fx0 + fw, plot.y + plot.h - h, 1, h}, rgb(173, 178, 222));
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
      // Legend order, strings and colours [STATIC spec 09 7.1.1]; the neighbour-deal share is 1 - the sum of the others, shown only when it is positive.
      const Cat L[10] = {{0x1E8, "Coal Power", rgb(0x80, 0x40, 0x40), d.electricity[0]}, {0x1E9, "Oil Power", rgb(0xFF, 0x7E, 0x7E), d.electricity[1]},
                         {0x1EA, "Gas Power", rgb(0x00, 0x80, 0xFF), d.electricity[2]}, {0x1EB, "Wind Power", rgb(0xFF, 0x80, 0x00), d.electricity[3]},
                         {0x1EC, "Solar Power", rgb(0x00, 0xFF, 0x00), d.electricity[4]}, {0x1ED, "Nuclear Power", rgb(0xFF, 0xFF, 0x00), d.electricity[5]},
                         {0x1EE, "Microwave Power", rgb(0xFF, 0x00, 0x00), d.electricity[6]}, {0x1EF, "Fusion Power", rgb(0x00, 0xFF, 0xFF), d.electricity[7]},
                         {0x1F0, "Waste-to-Energy", rgb(0x00, 0x00, 0xFF), d.electricity[8]}, {0x1F1, "Neighbor Deals", rgb(0xF0, 0x3A, 0xE6), d.electricity[9]}};
      for (const Cat& c : L) { cats.push_back(c); total += c.v; }
    } else {
      const Cat L[5] = {{0x1F4, "Landfill", rgb(247, 56, 231), d.garbage[0]}, {0x1F1, "Neighbor Deals", rgb(150, 150, 150), d.garbage[1]},
                        {0x1F5, "Incinerators", rgb(230, 110, 40), d.garbage[2]}, {0x1F6, "Recycling", rgb(57, 69, 247), d.garbage[3]},
                        {0x1F7, "Collecting in Streets", rgb(110, 110, 60), d.garbage[4]}};
      for (const Cat& c : L) { cats.push_back(c); total += c.v; }
    }
    // heading at the top left of the box, then one colour bar per category present
    { std::string head = E.win(el ? 0x28A : 0x1F3, el ? "Annually Averaged" : "% Garbage Goes To:");
      auto lines = wrap(ui, head, 168, "main10");
      ui.text(box.x + 7, box.y + 5, lines.empty() ? head : lines[0], "main10", rgb(49, 52, 123)); }   // the first line only; the legend bars cover the rest
    std::vector<PieSlice> slices;
    int by = box.y + 20;
    const int barH = el ? 14 : 23, barPitch = el ? 16 : 25;   // legend bars x 98..264: 14 rows at a 16 px pitch (electricity), 23 rows at 25 (garbage) [OBSERVED]
    for (const Cat& c : cats) {
      if (c.v <= 0) continue;
      ui.fill({box.x + 6, by, 167, barH}, c.col);
      ui.text(box.x + 8, by + (barH - 12) / 2 - 1, E.win(c.id, c.fb), "main10", (int(c.col & 255) + int((c.col >> 8) & 255) + int((c.col >> 16) & 255)) < 385 ? rgb(255, 255, 255) : rgb(0, 0, 0));   // by the 8-bit source colour [OBSERVED]
      by += barPitch;
      slices.push_back({"", c.v, c.col});
    }
    if (!pieChartArt(ui, E, r.x + 231, r.y + 90, slices)) pieChart(ui, r.x + 318, r.y + 138, slices);   // art origin (271, 124) in the 800 x 600 window [OBSERVED]
    cap1 = capText(E, el ? 0x289 : 0x1F8, el ? "Total Electricity Usage Annually = %s MW-h" : "Total Garbage Produced Annually = %s Tons", groupDigits(!el && d.garbageProduced >= 0 ? int64_t(std::llround(d.garbageProduced / 10.0)) : int64_t(total)));
  }
  const int capDy = -6;   // the caption lines sit 6 px higher on every chart page (observed)
  ui.textCentered({r.x + 56, r.y + 248 + capDy, 340, 14}, cap1, "main9", agePage ? kAxisInk : kInkBlue);   // the caption lines are MAIN9 [OBSERVED on the pie pages; the age pages' text starts 2 px right of main10's]
  if (!cap2.empty()) ui.textCentered({r.x + 56, r.y + 264 + capDy, 340, 14}, cap2, "main9", rgb(247, 142, 90));
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
  { const uint32_t ring = rgb(173, 182, 255);   // 1 px ring just outside the strip: columns x44 / x77 (y69..468) and the row y469 [OBSERVED]
    ui.fill({r.x + 4, r.y + 35, 1, 400}, ring); ui.fill({r.x + 37, r.y + 35, 1, 400}, ring); ui.fill({r.x + 4, r.y + 435, 34, 1}, ring); }
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
