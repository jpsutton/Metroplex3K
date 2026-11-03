#include "core/skin.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace m3k {

using namespace skin_group;

const uint32_t kRciColors[3] = {rgba(0, 192, 0), rgba(0, 0, 192), rgba(192, 192, 0)};
constexpr int RciGeometry::barX[3];

int cell4For(bool enabled, bool pressed, bool checked, bool hover) {
  if (!enabled) return CELL_DISABLED;
  if (pressed || checked) return CELL_PRESSED;
  if (hover) return CELL_HOVER;
  return CELL_NORMAL;
}
int cell8For(bool enabled, bool pressed, bool checked, bool hover) {
  int state = !enabled ? 3 : pressed ? 2 : hover ? 1 : 0;
  return 2 * state + (checked ? 0 : 1);
}

Rect RciGeometry::bar(int i, double v) {
  if (i < 0 || i > 2) return {};
  // Spec 09 §4.5 [STATIC Q-BQ]: pixels = trunc(|u| × 16.0).
  int h = int(std::trunc(std::fabs(v) * unitPx));
  Rect r{barX[i], 0, barW, 0};
  if (v > 0) {
    int top = std::max(slotTop, zeroY - h);
    r.y = top; r.h = zeroY - top;
  } else if (v < 0) {
    int bot = std::min(lowerBottom, lowerTop + h);
    r.y = lowerTop; r.h = std::max(0, bot - lowerTop);
  } else {
    r.y = zeroY;
  }
  return r;
}

// ---- tool menu layout -----------------------------------------------------------------------
ToolMenuLayout toolMenuLayout(int screenH) {
  ToolMenuLayout L;
  int hc = screenH <= 480 ? 0 : screenH <= 600 ? 1 : 2;  // 480 / 600 / >600
  L.valid = true;
  L.heightClass = hc == 0 ? 480 : hc == 1 ? 600 : 1000;
  L.artGroup = hc == 0 ? Ui640 : UiBig;
  // x is always 33. rows: {y, dx, dy} per height class.
  static const int ry[9][3] = {{3, 3, 100}, {39, 39, 136}, {75, 75, 172}, {111, 111, 208}, {147, 147, 244},
                               {219, 291, 388}, {255, 327, 424}, {291, 363, 460}, {183, 219, 316}};
  static const int pdy[9][3] = {{0, 0, 97}, {11, 11, 108}, {11, 11, 108}, {11, 11, 108}, {11, 11, 108},
                                {0, 0, 97}, {45, 118, 215}, {0, 118, 215}, {83, 119, 216}};
  for (int n = 0; n < 9; n++) {
    auto& r = L.root[n];
    r.id = 0x1001 + n; r.x = 33; r.y = ry[n][hc]; r.dx = 0; r.dy = pdy[n][hc];
    r.hasSub = n != 5;
  }
  auto run = [&](int n, std::vector<uint32_t> ids, int base) {
    L.subIds[n] = ids;
    for (size_t k = 0; k < ids.size(); k++) L.subY[n].push_back(base + 36 * int(k));
  };
  int b = hc == 2 ? 118 : 21;
  run(0, {0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006}, b);
  run(1, {0x3001, 0x3002, 0x3003, 0x3004, 0x3005, 0x3006, 0x3007, 0x3008}, b);
  run(2, {0x4001, 0x4002, 0x4004, 0x4005, 0x4007, 0x4009, 0x400A}, b);
  run(3, {0x5001, 0x5002, 0x5003, 0x5009, 0x500A, 0x500B}, b);
  run(4, {0x6002, 0x6003, 0x6004, 0x6006, 0x6008, 0x6009, 0x600A}, b);
  static const int revBase[3] = {57, 129, 226}, emBase[3] = {93, 129, 226}, optBase[3] = {57, 129, 226};
  run(6, {0x8010, 0x8011, 0x8001, 0x8005, 0x8002, 0x8003, 0x8004}, revBase[hc]);
  run(7, {0x9001, 0x900A, 0x9002, 0x9006, 0x9003, 0x9004, 0x9005}, optBase[hc]);
  if (hc > 0) { L.subY[7][0] -= 1; L.subY[7][1] -= 1; }  // original table: first two Options buttons 1 px high
  run(8, {0xA001, 0xA002, 0xA003, 0xA004, 0xA006, 0xA005}, emBase[hc]);

  if (hc == 0) {
    L.pieces = {{Ui640, 0xFFFF, 0, false}};
  } else if (hc == 1) {
    L.pieces = {{UiBig, 0xFFFF, 0, false}, {UiBig, 0xFFFE, 417, false}};
  } else {
    // FFFE is the column's bottom cap (bevelled bottom edge): it sits against the top of the navigator panel
    // (navTop - 90 so its bevel is hidden under the panel), and the FFFC filler tiles between FFFF and the cap
    // [OBSERVED: no seam at any window height]. At 768 the cap is directly below FFFF.
    const int capY = std::max(97 + 417, screenH - kNavPanelH - 90);
    L.pieces = {{Ui1024, 0xFFFD, 0, false}, {UiBig, 0xFFFF, 97, false}};
    if (screenH > 768) L.pieces.push_back({Ui1024, 0xFFFC, 97 + 417, true, capY});
    L.pieces.push_back({Ui1024, 0xFFFE, capY, false});
  }
  return L;
}

BottomBarLayout bottomBarLayout(int screenW) {
  BottomBarLayout B;
  auto R = [](int l, int t, int r, int b) { return Rect{l, t, r - l, b - t}; };
  if (screenW <= 640) {
    B.group = Ui640; B.variant = 0;
    B.fieldA = R(8, 46, 340, 64); B.fieldB = R(8, 25, 116, 43); B.fieldC = R(120, 25, 270, 43); B.fieldD = R(264, 25, 350, 43);
    B.textLine = R(8, 3, 340, 23);
    B.p1x = 371; B.p1y = 14; B.p2x = 348; B.p2y = 14; B.p3x = 399; B.p3y = 14; B.p4x = 349; B.p4y = 34;
  } else if (screenW <= 800) {
    B.group = Ui800; B.variant = 1;
    B.fieldA = R(8, 26, 144, 44); B.fieldB = R(146, 26, 258, 44); B.fieldC = R(260, 26, 408, 44); B.fieldD = R(396, 26, 500, 44);
    B.textLine = R(8, 5, 481, 22);
    B.p1x = 511; B.p1y = 9; B.p2x = 488; B.p2y = 9; B.p3x = 547; B.p3y = 11; B.p4x = 489; B.p4y = 29;
  } else {
    B.group = Ui1024; B.variant = 2;
    B.fieldA = R(8, 26, 366, 44); B.fieldB = R(372, 26, 480, 44); B.fieldC = R(486, 26, 630, 44); B.fieldD = R(630, 26, 712, 44);
    B.textLine = R(8, 6, 704, 23);
    B.p1x = 735; B.p1y = 10; B.p2x = 712; B.p2y = 10; B.p3x = 773; B.p3y = 9; B.p4x = 713; B.p4y = 29;
  }
  return B;
}

// ---- blitting -------------------------------------------------------------------------------
void skinBlitScaled(Ui& ui, const Image& im, Rect src, Rect dst) {
  if (!ui.pixels || im.empty() || src.w <= 0 || src.h <= 0 || dst.w <= 0 || dst.h <= 0) return;
  for (int j = 0; j < dst.h; j++) {
    int dy = dst.y + j; if (dy < 0 || dy >= ui.H) continue;
    int sy = src.y + (src.h == dst.h ? j : j * src.h / dst.h);
    if (sy < 0 || sy >= im.h) continue;
    for (int i = 0; i < dst.w; i++) {
      int dx = dst.x + i; if (dx < 0 || dx >= ui.W) continue;
      int sx = src.x + (src.w == dst.w ? i : i * src.w / dst.w);
      if (sx < 0 || sx >= im.w) continue;
      uint32_t p = im.px[size_t(sy) * im.w + sx];
      uint32_t a = p >> 24;
      if (a == 255) { ui.at(dx, dy) = p; continue; }
      if (!a) continue;
      uint32_t& d = ui.at(dx, dy);   // partial alpha (button shadow): blend over what is there
      if (ui.overlay && (d >> 24) != 255) { d = overlayOver(d, float(p & 255), float(p >> 8 & 255), float(p >> 16 & 255), float(a) / 255.f); continue; }
      uint32_t out = 0xFF000000u;
      for (int sh = 0; sh < 24; sh += 8) out |= (((p >> sh) & 255) * a + ((d >> sh) & 255) * (255 - a) + 127) / 255 << sh;
      d = out;
    }
  }
}

// GZBtn art is baked onto a solid blue plate (about 7B92FF, faint vignette) with the pill's drop shadow
// multiplied into it. Remove the plate so the button sits on whatever the dialog draws: plate -> alpha 0,
// shadow -> black at partial alpha, pill -> opaque. A shadow pixel is the plate colour scaled evenly on all
// three channels; anything lighter than the plate, or scaled unevenly, belongs to the pill.
static void keyButtonPlate(Image& im) {
  const float plate[3] = {0x7B, 0x92, 0xFF};
  for (uint32_t& p : im.px) {
    if (!(p >> 24)) continue;
    float r[3], lo = 9.f, hi = 0.f, sum = 0.f;
    for (int c = 0; c < 3; c++) { r[c] = float((p >> (8 * c)) & 255) / plate[c]; lo = std::min(lo, r[c]); hi = std::max(hi, r[c]); sum += r[c]; }
    if (hi > 1.02f || hi - lo > 0.1f) continue;   // pill pixel: keep
    float a = 1.f - sum / 3.f;
    p = a < 0.1f ? 0u : (uint32_t(std::lround(a * 255.f)) << 24);   // black + alpha; plate vignette (<10% dark) vanishes
  }
}

// ---- loading --------------------------------------------------------------------------------
static uint64_t key64(uint32_t g, uint32_t i) { return (uint64_t(g) << 32) | i; }
static std::string hex(uint32_t v, int digits) {
  char b[16]; std::snprintf(b, sizeof b, "%0*X", digits, v); return b;
}

const Image* Skin::fetch(uint32_t group, uint32_t inst) const {
  if (!db_) return nullptr;
  uint64_t k = key64(group, inst);
  auto it = cache_.find(k);
  if (it != cache_.end()) return &it->second;
  if (missing_.count(k)) return nullptr;
  ResourceRef r = db_->find({T_UI_IMAGE, group, inst});
  if (r) {
    try {
      Image im = decodeUiImage(r.data, r.available, true);
      if (group == Sys && inst == 0xE2B66DB8) keyButtonPlate(im);
      if (!im.empty()) return &(cache_[k] = std::move(im));
    } catch (const FormatError&) {}
  }
  missing_[k] = true;
  return nullptr;
}

void Skin::bind(const std::string& name, uint32_t group, uint32_t inst) {
  if (fetch(group, inst)) names_[name] = {group, inst};
}

const Image* Skin::get(const std::string& name) const {
  auto ex = extras_.find(name);
  if (ex != extras_.end()) return &ex->second;
  auto it = names_.find(name);
  return it == names_.end() ? nullptr : fetch(it->second.first, it->second.second);
}

// Build a 4-cell "engine" (options) strip from the help "?" button: keep the chrome, replace the glyph with a cog.
void Skin::buildEngineButton() {
  const Image* help = get("help");
  if (!help || help->w < 4 || help->h < 8) return;
  Image im = *help;
  const int cells = 4, cw = im.w / cells, ch = im.h;
  auto at = [&](int x, int y) -> uint32_t& { return im.px[size_t(y) * im.w + x]; };
  auto putDisk = [&](int cx, int cy, float r, uint32_t col, int x0, int x1) {
    const int rr = int(std::ceil(r));
    const int r2 = int(r * r + 0.5f);
    for (int y = cy - rr; y <= cy + rr; y++)
      for (int x = cx - rr; x <= cx + rr; x++)
        if (x >= x0 && x < x1 && y >= 0 && y < ch && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r2) at(x, y) = col;
  };
  for (int cell = 0; cell < cells; cell++) {
    const int x0 = cell * cw, x1 = x0 + cw;
    const int cx = x0 + cw / 2 - 1, cy = ch / 2 - 1;
    // Sample the button face on a ring around the glyph so we can paint out the "?".
    std::vector<uint32_t> samples;
    for (int a = 0; a < 360; a += 10)
      for (int rad : {12, 13, 14}) {
        const int x = int(std::lround(cx + rad * std::cos(a * 0.017453292519943295)));
        const int y = int(std::lround(cy + rad * std::sin(a * 0.017453292519943295)));
        if (x < x0 || x >= x1 || y < 0 || y >= ch) continue;
        uint32_t p = at(x, y);
        int r = int(p & 255), g = int((p >> 8) & 255), b = int((p >> 16) & 255), al = int(p >> 24);
        if (al < 200) continue;
        if (r > 180 && g > 140 && b < 120) continue;   // skip yellow glyph samples
        samples.push_back(p);
      }
    if (samples.empty()) continue;
    std::sort(samples.begin(), samples.end());
    const uint32_t face = samples[samples.size() / 2];
    const int fr = int(face & 255), fg = int((face >> 8) & 255), fb = int((face >> 16) & 255);
    for (int y = cy - 13; y <= cy + 13; y++)
      for (int x = cx - 13; x <= cx + 13; x++) {
        if (x < x0 || x >= x1 || y < 0 || y >= ch) continue;
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > 12 * 12) continue;
        uint32_t p = at(x, y);
        int r = int(p & 255), g = int((p >> 8) & 255), b = int((p >> 16) & 255), al = int(p >> 24);
        if (al < 128) continue;
        const bool yellow = r > 160 && g > 120 && b < 130 && r + g > b * 3;
        const bool dark = r + g + b < 140;
        const int dist = std::abs(r - fr) + std::abs(g - fg) + std::abs(b - fb);
        if (yellow || dark || dist > 55) at(x, y) = (p & 0xFF000000u) | (face & 0x00FFFFFFu);
      }
    // Cog: yellow fill + black outline on active cells; dark embossed on the disabled cell.
    const bool yellow = cell != CELL_DISABLED;
    const bool pressed = cell == CELL_PRESSED;
    const int gx = cx + (pressed ? 1 : 0), gy = cy + (pressed ? 1 : 0);
    const uint32_t outline = yellow ? rgba(12, 12, 16) : rgba(18, 18, 24);
    const uint32_t fill = yellow ? rgba(255, 224, 48) : rgba(52, 52, 60);
    constexpr int kTeeth = 8;
    auto plot = [&](int x, int y, uint32_t col) {
      if (x >= x0 && x < x1 && y >= 0 && y < ch) at(x, y) = col;
    };
    for (int t = 0; t < kTeeth; t++) {
      const double a = (t * 360.0 / kTeeth - 90.0) * 0.017453292519943295;
      for (int rad = 5; rad <= 10; rad++)
        for (double da : {-0.18, -0.09, 0.0, 0.09, 0.18})
          plot(int(std::lround(gx + rad * std::cos(a + da))), int(std::lround(gy + rad * std::sin(a + da))), outline);
    }
    putDisk(gx, gy, 7.2f, outline, x0, x1);
    putDisk(gx, gy, 6.0f, fill, x0, x1);
    for (int t = 0; t < kTeeth; t++) {
      const double a = (t * 360.0 / kTeeth - 90.0) * 0.017453292519943295;
      for (int rad = 5; rad <= 9; rad++)
        for (double da : {-0.12, -0.04, 0.04, 0.12})
          plot(int(std::lround(gx + rad * std::cos(a + da))), int(std::lround(gy + rad * std::sin(a + da))), fill);
      for (double da : {-0.14, 0.0, 0.14})
        plot(int(std::lround(gx + 10 * std::cos(a + da))), int(std::lround(gy + 10 * std::sin(a + da))), outline);
    }
    putDisk(gx, gy, 3.6f, outline, x0, x1);
    putDisk(gx, gy, 2.6f, fill, x0, x1);
    putDisk(gx, gy, 1.4f, outline, x0, x1);
  }
  extras_["engine"] = std::move(im);
}

bool Skin::load(ResourceDb& db, int screenH, int screenW) {
  db_ = &db; h_ = screenH;
  w_ = screenW > 0 ? screenW : screenH <= 480 ? 640 : screenH <= 600 ? 800 : 1024;
  cache_.clear(); missing_.clear(); names_.clear(); extras_.clear();
  layout_ = toolMenuLayout(screenH);
  bar_ = bottomBarLayout(w_);

  // Tool menu column and root/sub-menu art.
  bind("rootpanel", layout_.artGroup, 0xFFFF);
  for (const auto& p : layout_.pieces) bind("piece." + hex(p.group, 8) + "." + hex(p.inst, 4), p.group, p.inst);
  for (int n = 1; n <= 9; n++) {
    uint32_t base = 0x10000000u | (uint32_t(n) << 16);
    bind("root." + std::to_string(n) + ".hover", UiBig, base | 1);
    bind("root." + std::to_string(n) + ".press", UiBig, base | 5);
    bind("sub." + std::to_string(n), layout_.artGroup, base | 3);
  }
  for (int n = 0; n < 9; n++)
    for (uint32_t id : layout_.subIds[n]) bind("tool." + hex(id, 4), layout_.artGroup, id);

  // Bottom bar, speed, view, RCI.
  bind("bottombar", bar_.group, 0x82790741);
  bind("bottombar.cap", UiBig, 0x82790742);
  bind("rci", UiBig, 0x42FB77F6);
  for (uint32_t k = 0; k < 6; k++) bind("speed." + std::to_string(k), UiBig, 0x52FB06E4 + k);
  bind("help", UiBig, 0x52FB06EA);
  buildEngineButton();   // Metroplex3K options: same chrome as "help", cog instead of "?"
  // Navigator panel pieces (bottom-right): top-left, top-right, bottom (160 wide), layers strip, 4 zoom/rotate strips.
  bind("nav.tl", UiBig, 0x22F96E32); bind("nav.tr", UiBig, 0x22F96E33); bind("nav.bot", UiBig, 0x22F96E34);
  bind("nav.layers", UiBig, 0x22F96E35);   // 22F96E31 is the lattice (grid) strip
  for (uint32_t k = 0; k < 4; k++) bind("nav.btn." + std::to_string(k), UiBig, 0x22F96E2D + k);
  // Pause / run buttons (spec 09 4.2): lit, hover (yellow) and unlit (grey) faces.
  bind("pause", UiBig, 0x52FB06E0); bind("pause.hover", UiBig, 0x52FB06E1); bind("pause.off", UiBig, 0x52FB06EB);
  bind("run", UiBig, 0x52FB06E2); bind("run.hover", UiBig, 0x52FB06E3); bind("run.off", UiBig, 0x52FB06EC);

  // Generic controls.
  bind("button", Sys, 0xE2B66DB8);
  bind("check", Sys, 0xC2D6E93A);
  bind("radio", Sys, 0x0000000A);
  bind("combo", Sys, 0x62B99D31);
  bind("field", Sys, 0x42B99D48);
  bind("scrollbar", Sys, 0xE2B99E01);
  bind("spinner", Sys, 0x82B99D9D);
  bind("window", Sys, 0xE2B14588);
  bind("sysbtns", Sys, 0x62B19CE9);
  for (uint32_t i = 1; i <= 0x1C; i++) bind("gonzo." + hex(i, 2), Gonzo, i);

  // Front end: pick the 800x600 set when the window can hold it and it exists, else 640x480.
  bool big = w_ >= 800 && h_ >= 600;
  if (big && fetch(Main, 0x22729921)) {   // observed: the skyline image is the original's 800x600 menu art
    bind("menu.bg", Main, 0x22729921);
    for (uint32_t k = 0; k < 9; k++) bind("menu.btn." + std::to_string(k), Main, 0x22729930 + k);
  } else if (big && fetch(Main, 0x01140000)) {
    bind("menu.bg", Main, 0x01140000);
    for (uint32_t k = 0; k < 9; k++) bind("menu.btn." + std::to_string(k), Main, 0x01140001 + k);
  }
  if (!names_.count("menu.bg")) {
    static const uint32_t bgs[] = {0x22721000, 0x22721001, 0x22721002, 0x22721003, 0x22721004, 0x22721006, 0x22729921};
    for (uint32_t b : bgs) if (fetch(Main, b)) { bind("menu.bg", Main, b); break; }
    for (uint32_t k = 0; k < 9; k++) bind("menu.btn." + std::to_string(k), Main, 0x22729930 + k);
  }

  loaded_ = !names_.empty();
  return loaded_;
}

// ---- tool menu ------------------------------------------------------------------------------
Rect Skin::columnRect(int winW, int winH) const {
  const Image* p = get("rootpanel");
  int h = winH;
  if (p && layout_.heightClass == 480) h = p->h;
  return {winW - layout_.width, 0, layout_.width, h};
}
Rect Skin::rootButtonRect(int n, int ox, int oy) const {
  if (n < 1 || n > 9) return {};
  const auto& r = layout_.root[n - 1];
  return {ox + r.x, oy + r.y, 56, 32};
}
Rect Skin::subButtonRect(int n, int k, int ox, int oy) const {
  if (n < 1 || n > 9 || k < 0 || size_t(k) >= layout_.subY[n - 1].size()) return {};
  return {ox, oy + layout_.subY[n - 1][k], 36, 36};
}
Rect Skin::subPanelRect(int n, int ox, int oy) const {
  if (n < 1 || n > 9 || !layout_.root[n - 1].hasSub) return {};
  const Image* im = get("sub." + std::to_string(n));
  if (!im) return {};
  const auto& r = layout_.root[n - 1];
  return {ox + r.dx, oy + r.dy, im->w, im->h};
}

bool Skin::drawRootPanel(Ui& ui, int ox, int oy, int hover, int selected) {
  const Image* bg = get("rootpanel");
  if (!bg) return false;
  for (const auto& p : layout_.pieces) {
    const Image* im = get("piece." + hex(p.group, 8) + "." + hex(p.inst, 4));
    if (!im) continue;
    if (p.tileDown) {
      for (int y = oy + p.y; y < (p.until ? oy + p.until : ui.H); y += im->h) ui.blit(*im, ox, y);
    } else {
      ui.blit(*im, ox, oy + p.y);
    }
  }
  for (int pass = 0; pass < 2; pass++) {
    int n = pass == 0 ? hover : selected;
    if (n < 1 || n > 9 || (pass == 0 && n == selected)) continue;
    const Image* ov = get("root." + std::to_string(n) + (pass == 0 ? ".hover" : ".press"));
    if (!ov) continue;
    Rect r = rootButtonRect(n, ox, oy);
    ui.blit(*ov, r.x, r.y);
  }
  return true;
}

bool Skin::drawSubPanel(Ui& ui, int ox, int oy, int n) {
  Rect r = subPanelRect(n, ox, oy);
  const Image* im = r.w ? get("sub." + std::to_string(n)) : nullptr;
  if (!im) return false;
  ui.blit(*im, r.x, r.y);
  return true;
}

// ---- bottom bar -----------------------------------------------------------------------------
bool Skin::bottomBarPlace(BottomBarPlace& o) const {
  const Image* bg = get("bottombar");
  if (!bg) return false;
  int x = 0, y = h_ - bg->h;
  o.bar = {x, y, bg->w, bg->h};
  auto mv = [&](Rect r) { return Rect{r.x + x, r.y + y, r.w, r.h}; };
  o.fieldA = mv(bar_.fieldA); o.fieldB = mv(bar_.fieldB); o.fieldC = mv(bar_.fieldC); o.fieldD = mv(bar_.fieldD);
  o.textLine = mv(bar_.textLine);
  o.p1x = x + bar_.p1x; o.p1y = y + bar_.p1y; o.p2x = x + bar_.p2x; o.p2y = y + bar_.p2y;
  o.p3x = x + bar_.p3x; o.p3y = y + bar_.p3y; o.p4x = x + bar_.p4x; o.p4y = y + bar_.p4y;
  o.rightEnd = w_;
  return true;
}

bool Skin::drawBottomBar(Ui& ui, BottomBarPlace* out) {
  BottomBarPlace pl;
  if (!bottomBarPlace(pl)) return false;
  pl.rightEnd = ui.W;
  pl.bar.y = ui.H - pl.bar.h;
  // re-anchor to the Ui's actual height
  int dy = pl.bar.y - (h_ - pl.bar.h);
  if (dy) {
    for (Rect* r : {&pl.fieldA, &pl.fieldB, &pl.fieldC, &pl.fieldD, &pl.textLine}) r->y += dy;
    pl.p1y += dy; pl.p2y += dy; pl.p3y += dy; pl.p4y += dy;
  }
  ui.blit(*get("bottombar"), pl.bar.x, pl.bar.y);
  if (const Image* cap = get("bottombar.cap"))
    for (int x = pl.bar.x + pl.bar.w; x < ui.W; x += cap->w) ui.blit(*cap, x, ui.H - cap->h);
  if (out) *out = pl;
  return true;
}

// ---- RCI ------------------------------------------------------------------------------------
bool Skin::drawRciIndicator(Ui& ui, int x, int y, double r, double c, double i) {
  const Image* bg = get("rci");
  if (!bg) return false;
  ui.blit(*bg, x, y);
  double v[3] = {r, c, i};
  for (int k = 0; k < 3; k++) {
    Rect b = RciGeometry::bar(k, v[k]);
    if (b.h > 0) ui.fill({x + b.x, y + b.y, b.w, b.h}, kRciColors[k]);
  }
  return true;
}

// ---- generic controls -----------------------------------------------------------------------
bool Skin::sliceCell(const Image& s, int cells, int cell, Image& out) {
  if (s.empty() || cells <= 0 || cell < 0 || cell >= cells || s.w % cells) return false;
  int cw = s.w / cells;
  out.w = cw; out.h = s.h; out.px.resize(size_t(cw) * s.h);
  for (int j = 0; j < s.h; j++)
    for (int i = 0; i < cw; i++) out.px[size_t(j) * cw + i] = s.px[size_t(j) * s.w + cell * cw + i];
  return true;
}

bool Skin::drawCell(Ui& ui, const Image* s, int cells, int cell, int x, int y) {
  if (!s || s->empty() || cells <= 0 || cell < 0 || cell >= cells) return false;
  int cw = s->w / cells;
  ui.blit(*s, x, y, cell * cw, cw);
  return true;
}

bool Skin::imageButtonCells(Ui& ui, const Image* s, int cells, Rect r, const std::string& tip, bool toggled,
                            bool enabled, bool& clicked) const {
  clicked = false;
  if (!s || s->empty() || cells <= 0) return false;
  int cw = s->w / cells;
  bool hot = enabled && ui.hover(r), down = hot && ui.in.down;
  int cell = cell4For(enabled, down, toggled, hot);
  if (cell >= cells) cell = std::min(1, cells - 1);
  ui.blit(*s, r.x + (r.w - cw) / 2, r.y + (r.h - s->h) / 2, cell * cw, cw);
  if (hot) ui.tooltip = tip;
  clicked = enabled && ui.clicked(r);
  return true;
}

bool Skin::drawButton3(Ui& ui, const Image* s, int cells, int cell, Rect r, const std::string& label,
                       uint32_t textCol, const char* font) const {
  if (!s || s->empty() || cells <= 0 || cell < 0 || cell >= cells) return false;
  int cw = s->w / cells, third = cw / 3;
  if (third <= 0) return false;
  int sx = cell * cw;
  int lw = std::min(third, r.w / 2), rw = lw;
  skinBlitScaled(ui, *s, {sx, 0, lw, s->h}, {r.x, r.y, lw, r.h});
  skinBlitScaled(ui, *s, {sx + cw - rw, 0, rw, s->h}, {r.x + r.w - rw, r.y, rw, r.h});
  if (r.w > lw + rw) skinBlitScaled(ui, *s, {sx + third, 0, cw - 2 * third, s->h}, {r.x + lw, r.y, r.w - lw - rw, r.h});
  if (!label.empty() && ui.fonts) {
    int off = cell == CELL_PRESSED ? 1 : 0;
    // Centre on the pill, not the whole cell: the art carries a drop shadow on the right and bottom (measured on the
    // original's prompt buttons: caption sits 2 px left and 1 px up of the cell centre).
    ui.textCentered({r.x + off, r.y + off, r.w - 4, r.h - 2}, label, font,
                    cell == CELL_DISABLED ? rgb(0x90, 0x94, 0xA0) : textCol ? textCol : ui.textCol);
  }
  return true;
}

bool Skin::button3(Ui& ui, const Image* s, Rect r, const std::string& label, bool enabled, bool toggled,
                   bool& clicked, uint32_t textCol, const char* font) const {
  clicked = false;
  if (!s || s->empty()) return false;
  bool hot = enabled && ui.hover(r), down = hot && ui.in.down;
  if (!drawButton3(ui, s, 4, cell4For(enabled, down, toggled, hot), r, label, textCol, font)) return false;
  clicked = enabled && ui.clicked(r);
  return true;
}

bool Skin::drawDialogFrame(Ui& ui, Rect r, const std::string& title) const {
  const Image* cloud = get("window");
  if (!cloud || cloud->empty() || r.w < 16 || r.h < 40) return false;
  // Cloud body, tiled from the dialog's top-left.
  for (int y = 0; y < r.h; y += cloud->h)
    for (int x = 0; x < r.w; x += cloud->w)
      ui.blitRect(*cloud, r.x + x, r.y + y, 0, 0, std::min(cloud->w, r.w - x), std::min(cloud->h, r.h - y));
  const uint32_t white = rgb(255, 255, 255), black = rgb(0, 0, 0);
  ui.fill({r.x, r.y, r.w, 1}, white); ui.fill({r.x, r.y, 1, r.h}, white);
  ui.fill({r.x, r.y + r.h - 1, r.w, 1}, black); ui.fill({r.x + r.w - 1, r.y, 1, r.h}, black);
  // Title bar: 1 px lighter ring around a flat blue fill, inset 4 px; 31 px tall.
  Rect bar{r.x + 4, r.y + 4, r.w - 9, 31};
  ui.fill(bar, rgb(173, 182, 255));
  ui.fill({bar.x + 1, bar.y + 1, bar.w - 2, bar.h - 2}, rgb(123, 146, 255));
  ui.text(r.x + 8, r.y + 8, title, "title12", rgb(222, 223, 255));
  return true;
}

bool Skin::dialogCloseBox(Ui& ui, Rect d, Rect& hit, bool& clicked) const {
  const Image* sb = get("sysbtns");
  clicked = false;
  if (!sb || sb->w < 96 || sb->h < 100) return false;
  const int cw = sb->w / 4, ch = sb->h / 4;   // 4 columns (states) x 4 rows (close, back, minimise, maximise)
  hit = {d.x + d.w - 32, d.y + 7, cw, ch};
  bool hot = ui.hover(hit), down = hot && ui.in.down;
  int col = down ? 2 : hot ? 3 : 1;
  ui.blitRect(*sb, hit.x, hit.y, col * cw, 0, cw, ch);
  clicked = ui.clicked(hit);
  return true;
}

int Skin::drawCheck(Ui& ui, const Image* s, int x, int y, bool checked, bool enabled, bool hover, bool pressed) const {
  if (!s || s->empty() || s->w % 8) return 0;
  if (!drawCell(ui, s, 8, cell8For(enabled, pressed, checked, hover), x, y)) return 0;
  return s->w / 8;
}

}  // namespace m3k
