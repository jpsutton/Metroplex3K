// Navigator panel (bottom-right): mini-map diamond, zoom / rotate / layers buttons.
// Geometry measured from the original at 800x600 (panel 160x164 anchored to the window's bottom-right corner):
// diamond centre (79,81) panel-relative, 128 px wide and tall; zoom-in (26,112), zoom-out (133,112),
// rotate-left (47,133), rotate-right (112,133), layers (131,24) button centres [OBSERVED at 800x600]; the button rects below were re-measured at 1024x768.
#include <algorithm>
#include "core/game.h"
#include "core/overview.h"

namespace m3k {

namespace {
constexpr int kPanelW = 160, kPanelH = kNavPanelH, kMapHalf = 64;
constexpr int kCx = 79, kCy = 81;   // diamond centre, panel-relative
}  // namespace

bool Game::navigatorArt() const {
  return haveData_ && skin_.loaded() && skin_.get("nav.tl") && skin_.get("nav.tr") && skin_.get("nav.bot");
}

Rect Game::navigatorRect(int barH) const {
  if (navigatorArt()) return {W_ - kPanelW, H_ - kPanelH, kPanelW, kPanelH};
  return {W_ - 156, H_ - barH, 156, barH};
}

void Game::rebuildMiniMap() {
  const int N = std::max(world_.W, world_.H);
  if (!world_.W || !N) { miniMap_ = Image(); return; }
  miniMap_.w = miniMap_.h = 2 * kMapHalf;
  miniMap_.px.assign(size_t(miniMap_.w) * miniMap_.h, 0);
  for (int py = 0; py < miniMap_.h; py++)
    for (int px = 0; px < miniMap_.w; px++) {
      // Diamond in view space: dx = (v-u)/2, dy = (u+v)/2 - N/2 at 128 px across, scaled to the map size.
      float dx = float(px - kMapHalf) + 0.5f, dy = float(py - kMapHalf) + 0.5f;
      if (std::abs(dx) + std::abs(dy) > float(kMapHalf)) continue;
      float s = float(N) / float(2 * kMapHalf);                  // tiles per pixel along each view axis
      float u = (dy - dx) * s + float(N) * 0.5f, v = (dy + dx) * s + float(N) * 0.5f;
      int x, z;
      Renderer::viewToWorldVertex(cam_.rot, world_.W, world_.H, int(u), int(v), x, z);
      if (x < 0 || z < 0 || x >= world_.W || z >= world_.H) continue;
      // Same colours as the save file's overview thumbnail: zones over banded terrain.
      miniMap_.px[size_t(py) * miniMap_.w + px] = overviewTileColour(world_, x, z);
    }
}

void Game::drawNavigator(int barH) {
  Rect nav = navigatorRect(barH);
  if (ui_.hover(nav)) uiHit_ = true;
  auto zoomBy = [&](int d) { int z = std::max(0, std::min(4, cam_.zoom + d)); if (z != cam_.zoom) sound(d > 0 ? 1 : 2); cam_.zoom = z; };
  auto rot = [&](int d) {
    float Wf = float(world_.W), Hf = float(world_.H), x, z;
    switch (cam_.rot & 3) { case 0: x = cam_.u; z = cam_.v; break; case 1: x = cam_.v; z = Wf - cam_.u; break; case 2: x = Wf - cam_.u; z = Hf - cam_.v; break; default: x = Hf - cam_.v; z = cam_.u; }
    cam_.rot = (cam_.rot + d + 4) & 3; renderer_.centreOnWorld(cam_, x, z); sound(4);
    miniFrame_ = 0;
  };

  if (!navigatorArt()) {   // no art: plain panel with text buttons
    ui_.panel(nav);
    const int by1 = nav.y + 6, by2 = nav.y + 34, bw = 34, gap = 2, x0 = nav.x + 6, bw2 = 46;
    if (ui_.button({x0, by1, bw, 24}, "+")) zoomBy(1);
    if (ui_.button({x0 + (bw + gap), by1, bw, 24}, "-")) zoomBy(-1);
    if (ui_.button({x0 + 2 * (bw + gap), by1, bw, 24}, "<")) rot(-1);
    if (ui_.button({x0 + 3 * (bw + gap), by1, bw, 24}, ">")) rot(1);
    if (ui_.button({x0, by2, bw2, 24}, "Grid", true, ropt_.grid)) ropt_.grid = !ropt_.grid;
    if (ui_.button({x0 + bw2 + gap, by2, bw2, 24}, "Query", true, tool_ == ToolKind::Query)) setTool(ToolKind::Query);
    if (ui_.button({x0 + 2 * (bw2 + gap), by2, bw2 - 4, 24}, "Map")) openWindow("map");
    return;
  }

  const Image* tl = skin_.get("nav.tl"); const Image* tr = skin_.get("nav.tr"); const Image* bot = skin_.get("nav.bot");
  const int ox = nav.x, oy = nav.y;
  ui_.blit(*bot, ox, oy + kPanelH - bot->h);
  ui_.blit(*tl, ox, oy + kPanelH - bot->h - tl->h);
  ui_.blit(*tr, ox + tl->w, oy + kPanelH - bot->h - tr->h);

  // Mini-map diamond (view orientation), rebuilt a few times per second.
  if (miniMap_.empty() || (++miniFrame_ % 20) == 1) rebuildMiniMap();
  if (!miniMap_.empty()) ui_.blit(miniMap_, ox + kCx - kMapHalf, oy + kCy - kMapHalf);
  // Visible area: axis-aligned rectangle in mini-map space (red).
  if (world_.W) {
    const int N = std::max(world_.W, world_.H);
    float us[2], vs[2], ua, va, ub, vb;
    renderer_.screenToView(cam_, 0, 0, ua, va); renderer_.screenToView(cam_, W_, H_, ub, vb);
    (void)us; (void)vs;
    auto toMap = [&](float u, float v, int& mx, int& my) {
      float s = float(2 * kMapHalf) / float(N);
      mx = int(ox + kCx + (v - u) * 0.5f * s);
      my = int(oy + kCy + ((u + v) * 0.5f - N * 0.5f) * s);
    };
    // Corner screen points in view space: left/right/top/bottom of the screen in mini-map space.
    float ucs[4], vcs[4]; const int sx[4] = {0, W_, 0, W_}, sy[4] = {0, 0, H_, H_};
    for (int i = 0; i < 4; i++) renderer_.screenToView(cam_, sx[i], sy[i], ucs[i], vcs[i]);
    int xmin = 1 << 20, xmax = -(1 << 20), ymin = 1 << 20, ymax = -(1 << 20);
    for (int i = 0; i < 4; i++) { int mx, my; toMap(ucs[i], vcs[i], mx, my); xmin = std::min(xmin, mx); xmax = std::max(xmax, mx); ymin = std::min(ymin, my); ymax = std::max(ymax, my); }
    const int cl = ox + kCx - kMapHalf, cr = ox + kCx + kMapHalf, ct = oy + kCy - kMapHalf, cb = oy + kCy + kMapHalf;
    xmin = std::max(xmin, cl); xmax = std::min(xmax, cr); ymin = std::max(ymin, ct); ymax = std::min(ymax, cb);
    const uint32_t red = rgb(220, 20, 20);
    if (xmax > xmin && ymax > ymin) {
      // Rectangle outline clipped to the diamond (only the parts inside it show, as in the original).
      auto inDiamond = [&](int x, int y) { return std::abs(x - (ox + kCx)) + std::abs(y - (oy + kCy)) <= kMapHalf; };
      for (int x = xmin; x < xmax; x++) {
        if (inDiamond(x, ymin)) ui_.fill({x, ymin, 1, 1}, red);
        if (inDiamond(x, ymax - 1)) ui_.fill({x, ymax - 1, 1, 1}, red);
      }
      for (int y = ymin; y < ymax; y++) {
        if (inDiamond(xmin, y)) ui_.fill({xmin, y, 1, 1}, red);
        if (inDiamond(xmax - 1, y)) ui_.fill({xmax - 1, y, 1, 1}, red);
      }
    }
    // Click or drag on the diamond centres the view there.
    Rect mapRect{ox + kCx - kMapHalf, oy + kCy - kMapHalf, 2 * kMapHalf, 2 * kMapHalf};
    if (ui_.in.down && mapRect.contains(ui_.in.mx, ui_.in.my) && ui_.hover(nav)) {
      float dx = float(ui_.in.mx - (ox + kCx)), dy = float(ui_.in.my - (oy + kCy));
      if (std::abs(dx) + std::abs(dy) <= float(kMapHalf)) {
        float s = float(N) / float(2 * kMapHalf);
        float u = (dy - dx) * s + N * 0.5f, v = (dy + dx) * s + N * 0.5f;
        int x, z; Renderer::viewToWorldVertex(cam_.rot, world_.W, world_.H, int(u), int(v), x, z);
        if (x >= 0 && z >= 0 && x < world_.W && z < world_.H && ui_.in.pressed) centreOn(x, z);
      }
    }
  }

  // "N" marker at the diamond corner where the map's north corner (world 0,0) is: top at rotation 0, and it moves
  // top -> right -> bottom -> left as the map turns clockwise (spec 08 5.4 [OBSERVED]).
  {
    int nu, nv; Renderer::worldToViewVertex(cam_.rot, world_.W, world_.H, 0, 0, nu, nv);
    const int sxs = nv - nu, sys = nu + nv - world_.W;   // screen x / y direction of the corner from the centre
    const int dxs = (sxs > 0) - (sxs < 0), dys = (sys > 0) - (sys < 0);
    const int off = kMapHalf + 10;
    ui_.textCentered({ox + kCx + dxs * off - 10, oy + kCy + dys * off - 7, 20, 14}, "N", "main10", rgb(255, 199, 24));
  }

  // Buttons: 4-cell strips (26x29 cells) centred on the observed button centres.
  // Button rects measured on the original at 1024 x 768 (panel-relative; the panel is 160 x 164 at the window's bottom-right corner): zoom in
  // (14,100), zoom out (120,100), the left rotate button (34,120), the right one (100,120), all 26 x 29; layer views (114,6) 36 x 35.
  auto cellBtn = [&](const char* art, Rect rel, const std::string& tip, const std::function<void()>& fn, int tipPlace = Ui::kTipAbove) {
    const Image* s = skin_.get(art); if (!s) return;
    bool clicked = false;
    skin_.imageButtonCells(ui_, s, 4, {ox + rel.x, oy + rel.y, rel.w, rel.h}, tip, false, true, clicked, tipPlace);
    if (clicked) fn();
  };
  auto tip = [&](uint32_t id, const char* fb) { std::string t = str(0x225872FE, id); return t.empty() ? std::string(fb) : t; };
  cellBtn("nav.btn.0", {14, 100, 26, 29}, tip(0xA, "Zoom in"), [&] { zoomBy(1); });
  cellBtn("nav.btn.1", {120, 100, 26, 29}, tip(0xB, "Zoom out"), [&] { zoomBy(-1); });
  // Clockwise = the map turns clockwise as seen: the bottom corner goes to the left corner (lower rotation index; spec 08 5.4).
  cellBtn("nav.btn.2", {34, 120, 26, 29}, tip(0xD, "Rotate Clockwise"), [&] { rot(-1); });   // left button: clockwise, like PageDown (observed)
  cellBtn("nav.btn.3", {100, 120, 26, 29}, tip(0xC, "Rotate Counterclockwise"), [&] { rot(1); });   // right button: counter-clockwise, like PageUp
  cellBtn("nav.layers", {114, 6, 36, 35}, tip(0xA7, "Layer views"), [&] { if (showLayers_) showLayers_ = false; else openWindow("layers"); }, Ui::kTipLeft);
}

}  // namespace m3k
