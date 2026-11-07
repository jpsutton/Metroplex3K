// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// User-made buildings dialogs: Building Replacement Manager and Building List Editor (spec 24 section 2.5).
// Layouts measured on dialog-shots/main-menu--start-new-city--user-made-buildings-select*.png (800x600).
#include <algorithm>
#include "core/skin.h"
#include "core/windows.h"

namespace m3k {
using namespace wui;

namespace wui {
// Sprite of a composite-sprite record (animation frame 0, orientation 0) at the closest zoom whose image fits
// maxW x maxH. False when nothing resolves.
bool thumbSprite(GameData* gd, const ResKey& appearance, int maxW, int maxH, uint32_t& cat, uint32_t& id, bool* dbl) {
  if (!gd || !appearance.type) return false;
  const CsAttrib* cs = gd->csattrib(appearance);
  if (!cs || !cs->valid || cs->zoomCount <= 0) return false;
  for (int slot = cs->zoomCount - 1; slot >= 0; slot--) {
    const CsFrame* f = cs->frame(0, slot, 0);
    if (!f) continue;
    const SpriteImage* sp = gd->sprite(f->category, f->spriteId);
    if (!sp || sp->img.empty()) continue;
    int sc = f->flags == 2 ? 2 : 1;   // flags 2: the sprite is drawn at double size (spec 25)
    if ((sp->img.w * sc <= maxW && sp->img.h * sc <= maxH) || slot == 0) { cat = f->category; id = f->spriteId; if (dbl) *dbl = sc == 2; return true; }
  }
  return false;
}

// Colour sprite (opacity plane blended) centred in `box`, clipped to `clip`.
void spriteCentred(Ui& ui, GameData* gd, const ResKey& appearance, Rect box, Rect clip) {
  uint32_t cat = 0, id = 0;
  bool dbl = false;
  if (!ui.pixels || !thumbSprite(gd, appearance, box.w, box.h, cat, id, &dbl)) return;
  const int k = dbl ? 2 : 1;
  const SpriteImage* sp = gd->sprite(cat, id);
  const Plane* pl = gd->spritePlane(cat, id + 1);
  if (pl && (pl->w != sp->img.w || pl->h != sp->img.h)) pl = nullptr;
  Rect c{std::max(clip.x, 0), std::max(clip.y, 0), 0, 0};
  c.w = std::min(clip.x + clip.w, ui.W) - c.x; c.h = std::min(clip.y + clip.h, ui.H) - c.y;
  if (c.w <= 0 || c.h <= 0) return;
  const int iw = sp->img.w * k, ih = sp->img.h * k;
  const int x0 = box.x + (box.w - iw) / 2, y0 = box.y + (box.h - ih) / 2;
  for (int y = 0; y < ih; y++) {
    int dy = y0 + y; if (dy < c.y || dy >= c.y + c.h) continue;
    for (int x = 0; x < iw; x++) {
      int dx = x0 + x; if (dx < c.x || dx >= c.x + c.w) continue;
      int sx = x / k, sy = y / k;
      uint32_t s = sp->img.px[size_t(sy) * sp->img.w + sx];
      int a = pl ? pl->v[size_t(sy) * pl->w + sx] * 255 / 31 : int(s >> 24);
      if (!pl && (s & 0xFFFFFF) == 0xFF00FF) a = 0;
      if (a <= 0) continue;
      uint32_t& d = ui.at(dx, dy);
      uint32_t o = 0xFF000000u;
      for (int sh = 0; sh < 24; sh += 8) o |= uint32_t((int((s >> sh) & 255) * a + int((d >> sh) & 255) * (255 - a) + 127) / 255) << sh;
      d = o;
    }
  }
}
}  // namespace wui

namespace {

const uint32_t kInk = rgb(0x31, 0x34, 0x7B), kLabel = rgb(0x7B, 0x82, 0xB5), kGrey = rgb(0x84, 0x82, 0x84),
               kWhite = rgb(255, 255, 255), kSelRow = rgb(0x7B, 0x92, 0xFF), kLavender = rgb(0xDE, 0xDF, 0xFF);

Rect intersect(Rect a, Rect b) {
  int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y), x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
  return x1 > x0 && y1 > y0 ? Rect{x0, y0, x1 - x0, y1 - y0} : Rect{x0, y0, 0, 0};
}

void fillClip(Ui& ui, Rect r, Rect clip, uint32_t c) {
  Rect i = intersect(r, clip);
  if (i.w > 0 && i.h > 0) ui.fill(i, c);
}


// Image scaled (nearest) to fit and centred in `box`.
void imageFit(Ui& ui, const Image& im, Rect box) {
  if (im.empty() || !ui.pixels) return;
  double sc = std::min(1.0, std::min(double(box.w) / im.w, double(box.h) / im.h));
  int w = std::max(1, int(im.w * sc)), h = std::max(1, int(im.h * sc));
  int x0 = box.x + (box.w - w) / 2, y0 = box.y + (box.h - h) / 2;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    int dx = x0 + x, dy = y0 + y;
    if (dx < 0 || dy < 0 || dx >= ui.W || dy >= ui.H) continue;
    uint32_t c = im.px[size_t(y * im.h / h) * im.w + size_t(x * im.w / w)];
    if ((c >> 24) == 0) continue;
    ui.at(dx, dy) = c | 0xFF000000u;
  }
}

// Sunken box with explicit top/left and bottom/right line pairs (outer line first).
void edged(Ui& ui, Rect b, uint32_t tl0, uint32_t tl1, uint32_t br0, uint32_t br1) {
  ui.fill({b.x, b.y, b.w, 1}, tl0); ui.fill({b.x, b.y, 1, b.h}, tl0);
  ui.fill({b.x + 1, b.y + 1, b.w - 2, 1}, tl1); ui.fill({b.x + 1, b.y + 1, 1, b.h - 2}, tl1);
  ui.fill({b.x, b.y + b.h - 1, b.w, 1}, br0); ui.fill({b.x + b.w - 1, b.y, 1, b.h}, br0);
  ui.fill({b.x + 1, b.y + b.h - 2, b.w - 2, 1}, br1); ui.fill({b.x + b.w - 2, b.y + 1, 1, b.h - 2}, br1);
}

// GZScrollbar (SYS E2B99E01, 16x16 cells: up 6/7, thumb 8, track 9, down 10/11) at the right of a list.
void gzScroll(Ui& ui, const WinEnv& E, const std::string& name, int x, int y, int h, int total, int visible, int& scroll) {
  const Skin* sk = ui.skin;
  const Image* sb = sk ? sk->image(skin_group::Sys, 0xE2B99E01) : nullptr;
  Rect up{x, y, 16, 16}, dn{x, y + h - 16, 16, 16}, track{x, y + 16, 16, h - 32};
  E.spot(name + ".up", up); E.spot(name + ".down", dn);
  int range = std::max(0, total - visible);
  scroll = std::max(0, std::min(scroll, range));
  int ty = track.y + (range ? (track.h - 16) * scroll / range : 0);
  if (sb && sb->w >= 192) {
    for (int yy = track.y; yy < track.y + track.h; yy += 16) ui.blitRect(*sb, track.x, yy, 9 * 16, 0, 16, std::min(16, track.y + track.h - yy));
    Skin::drawCell(ui, sb, 12, ui.hover(up) && ui.in.down ? 7 : 6, up.x, up.y);
    Skin::drawCell(ui, sb, 12, ui.hover(dn) && ui.in.down ? 11 : 10, dn.x, dn.y);
    Skin::drawCell(ui, sb, 12, 8, track.x, ty);
  } else {
    ui.fill(track, ui.panelDark); ui.fill({track.x, ty, 16, 16}, ui.panelCol); ui.button(up, "^"); ui.button(dn, "v");
  }
  if (ui.clicked(up)) scroll--;
  if (ui.clicked(dn)) scroll++;
  if (ui.clicked(track)) scroll += ui.in.my < ty ? -visible : ui.in.my >= ty + 16 ? visible : 0;
  scroll = std::max(0, std::min(scroll, range));
}

// 4-cell image button (0 disabled, 1 normal, 2 pressed, 3 hover) with a plain fallback.
bool cellButton(Ui& ui, const WinEnv& E, const std::string& name, uint32_t inst, Rect r, bool enabled, const std::string& fb) {
  E.spot(name, r);
  const Skin* sk = ui.skin;
  bool c = false;
  if (!(sk && sk->imageButtonCells(ui, sk->image(skin_group::Sys, inst), 4, r, "", false, enabled, c))) c = ui.button(r, fb, enabled);
  return c && enabled;
}

std::string sizeText(int lot) { return std::to_string(lot) + "x" + std::to_string(lot); }

}  // namespace

int ubZoneClass(uint32_t a) {
  if (a >= 3000 && a < 5000) return 0;    // residential agents 3100..4700
  if (a >= 5000 && a < 8000) return 1;    // commercial 5100..6700
  if (a >= 8000 && a < 10000) return 2;   // industrial 8100..9300 (8100 also the misc industrial lots)
  if (a >= 11000 && a < 13000) return 3;  // military, power plants, transport (airport, seaport, stations)
  return 4;                                // civic buildings, rewards, landmarks
}

std::vector<int> UserBuildingsState::filtered() const {
  std::vector<int> out;
  for (int i = 0; i < int(replaceable.size()); i++) {
    const UbBuilding& b = replaceable[size_t(i)];
    if (b.zone == zoneFilter && b.lot == sizeFilter + 1) out.push_back(i);
  }
  return out;
}

const UbBuilding* UserBuildingsState::selected() const {
  std::vector<int> f = filtered();
  return sel >= 0 && sel < int(f.size()) ? &replaceable[size_t(f[size_t(sel)])] : nullptr;
}

std::vector<int> UserBuildingsState::batChoices() const {
  std::vector<int> out;
  const UbBuilding* r = selected();
  for (int i = 0; i < int(bat.size()); i++) if (!r || bat[size_t(i)].lot == r->lot) out.push_back(i);
  return out;
}

// ================================================================ Building Replacement Manager
WindowResult UserBuildingsDialog::draw(Ui& ui, const WinEnv& E, UserBuildingsState& s) {
  WindowResult R;
  // The prompt is modal: the dialog underneath takes no input while it is open.
  Input live = ui.in;
  if (prompt.active) { ui.in = Input(); ui.in.mx = ui.in.my = -1; }
  const Skin* sk = ui.skin;
  Rect r = centered(ui, 635, 478);
  auto at = [&](int x, int y, int w, int h) { return Rect{r.x + x, r.y + y, w, h}; };
  if (frame(ui, E, r, E.se(0x135, "Building Replacement Manager"))) { R.close = true; R.add(Act::Close); }
  blueBand(ui, at(4, 34, 8, 440));
  blueBand(ui, at(4, 439, 628, 35));
  blueBand(ui, at(16, 359, 615, 68));
  blueBand(ui, at(413, 44, 218, 122));

  std::vector<int> filt = s.filtered();
  s.sel = std::max(0, std::min(s.sel, int(filt.size()) - 1));
  const UbBuilding* cur = s.selected();
  std::vector<int> choices = s.batChoices();
  if (s.batSel >= 0 && std::find(choices.begin(), choices.end(), s.batSel) == choices.end()) s.batSel = -1;
  if (s.batSel < 0 && !choices.empty()) s.batSel = choices[0];
  const UbBuilding* bat = s.batSel >= 0 && s.batSel < int(s.bat.size()) ? &s.bat[size_t(s.batSel)] : nullptr;

  // BAT building list (lot size of the selected replaceable building).
  {
    Rect box = at(16, 40, 260, 131);
    ui.fill(box, kLavender); sunkenLines(ui, box);
    const int rowH = 16, visible = (box.h - 2) / rowH, total = int(choices.size());
    if (ui.hover(box) && ui.in.wheel) s.batScroll -= ui.in.wheel;
    s.batScroll = std::max(0, std::min(s.batScroll, std::max(0, total - visible)));
    for (int i = 0; i < visible && s.batScroll + i < total; i++) {
      int idx = choices[size_t(s.batScroll + i)];
      Rect row{box.x + 1, box.y + 1 + i * rowH, box.w - 2, rowH};
      E.spot("bat." + std::to_string(s.batScroll + i), row);
      bool on = idx == s.batSel;
      if (on) ui.fill(row, kSelRow);
      ui.text(row.x + 3, row.y + 2, s.bat[size_t(idx)].name, "main9", on ? kWhite : kInk);
      if (ui.clicked(row)) s.batSel = idx;
    }
  }
  // Edit List (pill button on a blue plate).
  {
    Rect b = at(107, 175, 77, 33);
    ui.fill(b, kSelRow);
    E.spot("editList", b);
    if (ui.button(b, E.se(0x12E, "Edit List"), true, false, "title9")) R.add(Act::OpenBuildingList);
  }

  // Info box: Name / Architect / Size of the selected BAT building.
  {
    Rect box = at(418, 48, 208, 80);
    ui.fill(box, kLavender);
    edged(ui, box, rgb(0x6B, 0x71, 0xBD), kInk, rgb(0xB5, 0xB6, 0xE7), rgb(0x7B, 0x82, 0xBD));
    const std::string labels[3] = {E.se(0x139, "Name:"), E.se(0x132, "Architect:"), E.se(0x133, "Size:")};
    const std::string values[3] = {bat ? bat->name : "", bat ? bat->author : "", bat ? sizeText(bat->lot) : ""};
    for (int i = 0; i < 3; i++) {
      ui.text(r.x + 426, r.y + 53 + 14 * i, labels[i], "main9", kLabel);
      ui.text(r.x + 499, r.y + 53 + 14 * i, values[i], "main9", kInk);
    }
  }

  // Filmstrip of replaceable buildings: outer and inner boxes, cells at 96 px steps around the selected one.
  {
    Rect outer = at(16, 228, 569, 132), inner = at(28, 239, 545, 110);
    ui.fill(outer, rgb(0x42, 0x41, 0x84));
    edged(ui, outer, rgb(0x84, 0x86, 0xB5), rgb(0x5A, 0x5D, 0x94), kInk, rgb(0x21, 0x28, 0x5A));
    edged(ui, inner, kInk, rgb(0x21, 0x28, 0x5A), rgb(0x84, 0x86, 0xB5), rgb(0x5A, 0x5D, 0x94));
    Rect clip{inner.x + 2, inner.y + 2, inner.w - 4, inner.h - 4};
    for (int k = -3; k <= 3; k++) {
      int i = s.sel + k;
      if (k == 0 || i < 0 || i >= int(filt.size())) continue;
      Rect cell = at(318 + 96 * k, 244, 92, 100);
      Rect vis = intersect(cell, clip);
      if (vis.w <= 0) continue;
      fillClip(ui, cell, clip, rgb(0xC6, 0xC3, 0xE7));
      const UbBuilding& b = s.replaceable[size_t(filt[size_t(i)])];
      const ResKey* rep = s.set.find(b.key);
      spriteCentred(ui, E.data, rep ? *rep : b.appearance, {cell.x + 2, cell.y + 2, cell.w - 4, cell.h - 4}, vis);
      E.spot("cell." + std::to_string(i), vis);
      if (ui.clicked(vis)) s.sel = i;
    }
  }

  // Preview column art (upper box, arrow shaft, lower box) and its contents.
  if (const Image* col = sk ? sk->image(skin_group::Sys, 0xDA096CE5) : nullptr) ui.blit(*col, r.x + 291, r.y + 40);
  ui.fill(at(304, 52, 100, 100), rgb(0xFF, 0xFF, 0xD6));
  if (bat) spriteCentred(ui, E.data, bat->appearance, at(306, 54, 96, 96), at(304, 52, 100, 100));
  ui.fill(at(303, 244, 99, 98), rgb(0xC6, 0xC3, 0xE7));
  if (cur) {
    const ResKey* rep = s.set.find(cur->key);
    spriteCentred(ui, E.data, rep ? *rep : cur->appearance, at(305, 246, 95, 94), at(303, 244, 99, 98));
    std::string st = E.se(0x133, "Size:") + sizeText(cur->lot);
    ui.text(r.x + 352 - ui.textWidth(st, "main9") / 2, r.y + 345, st, "main9", kGrey);
    E.spot("current", at(303, 244, 99, 98));
  }

  // Replace / Revert arrows.
  const bool canReplace = cur && bat && bat->lot == cur->lot;
  const bool canRevert = cur && s.set.find(cur->key);
  ui.text(r.x + 270, r.y + 199, E.se(0x12C, "Replace"), "main9", canReplace ? kInk : kGrey);
  ui.text(r.x + 396, r.y + 192, E.se(0x131, "Revert"), "main9", canRevert ? kInk : kGrey);
  if (cellButton(ui, E, "replace", 0x9A096C66, at(307, 213, 36, 21), canReplace, "v")) s.set.set(cur->key, bat->appearance);
  if (cellButton(ui, E, "revert", 0xBA096C8E, at(362, 161, 38, 23), canRevert, "^")) s.set.revert(cur->key);

  // Filmstrip scroll buttons: back one, back a page, forward a page, forward one.
  {
    const uint32_t ids[4] = {0xBA096C28, 0xFA096C3D, 0xBA096CD6, 0xBA096CB6};
    const int step[4] = {-1, -5, 5, 1};
    const char* names[4] = {"prev", "prevPage", "nextPage", "next"};
    const char* fb[4] = {"<", "<<", ">>", ">"};
    for (int i = 0; i < 4; i++)
      if (cellButton(ui, E, names[i], ids[i], at(587, 230 + 32 * i, 42, 32), !filt.empty(), fb[i]))
        s.sel = std::max(0, std::min(int(filt.size()) - 1, s.sel + step[i]));
  }

  // Filters: count text and the two radio groups (zone type, lot size).
  {
    std::string t = E.se(0x137, "Filters:") + " " +
                     subst(E.se(0x138, "%x%  of %y% Buildings Selected"), {std::to_string(filt.size()), std::to_string(s.totalReplaceable)});
    ui.text(r.x + 22, r.y + 364, t, "main9", kInk);
    const uint32_t zoneArt[5] = {0xDA096C77, 0x5A096BBA, 0x9A096C11, 0x7A096C9F, 0x1A096C4F};
    const uint32_t sizeArt[5] = {0xFA096AF4, 0x9A096B35, 0x7A096B55, 0xDA096B70, 0x7A096B86};
    for (int g = 0; g < 2; g++)
      for (int i = 0; i < 5; i++) {
        Rect b = at((g ? 204 : 20) + 36 * i, 384, 36, 36);
        int& v = g ? s.sizeFilter : s.zoneFilter;
        E.spot(std::string(g ? "size." : "zone.") + std::to_string(i), b);
        const Image* art = sk ? sk->image(skin_group::Sys, g ? sizeArt[i] : zoneArt[i]) : nullptr;
        bool hot = ui.hover(b);
        if (!(art && Skin::drawCell(ui, art, 4, v == i ? 2 : hot ? 3 : 1, b.x, b.y))) ui.button(b, std::to_string(i + 1), true, v == i);
        if (ui.clicked(b) && v != i) { v = i; s.sel = 0; }
      }
  }

  // Import Set / Clear Set.
  E.spot("import", at(532, 368, 92, 26));
  if (ui.button(at(532, 368, 92, 26), E.se(0x134, "Import Set"), true, false, "title9")) {
    promptKind_ = 0;
    prompt.open(E.se(0x140, "Import Set from Selected City"), "Importing a set from another city is not available yet.", 0, MessageBox::IconInfo);
  }
  E.spot("clear", at(532, 396, 92, 26));
  if (ui.button(at(532, 396, 92, 26), E.se(0x136, "Clear Set"), true, false, "title9") && !s.set.pairs.empty()) {
    promptKind_ = 1;
    prompt.open(E.se(0x135, "Building Replacement Manager"),
                E.se(0x141, "You have chosen to clear all replacement buildings from your current set.  Are you sure you want to continue?"), 3,
                MessageBox::IconQuestion);
  }

  // Done (check mark).
  if (cellButton(ui, E, "ok", 0xA2DEFD9A, at(592, 442, 42, 32), true, "OK") || (!prompt.active && ui.in.key[K_ENTER])) {
    R.add(Act::Done); R.close = true;
  }
  if (!prompt.active && ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }

  ui.in = live;
  if (prompt.active) {
    WindowResult pr = prompt.draw(ui, E);
    if (const WindowEvent* e = pr.find(Act::MessageResult)) {
      prompt.active = false;
      if (promptKind_ == 1 && MsgButton(e->a) == MsgButton::Yes) s.set.pairs.clear();
      promptKind_ = 0;
    }
    ui.in.pressed = ui.in.released = false;
  }
  return R;
}

// ================================================================ Building List Editor
WindowResult BuildingListEditor::draw(Ui& ui, const WinEnv& E, BuildingListState& s) {
  WindowResult R;
  frame_++;
  const Skin* sk = ui.skin;
  Rect r = centered(ui, 515, 345);
  auto at = [&](int x, int y, int w, int h) { return Rect{r.x + x, r.y + y, w, h}; };
  if (frame(ui, E, r, E.se(0x13B, "Building List Editor"))) { R.close = true; R.add(Act::Close); }
  blueBand(ui, at(4, 34, 8, 307));
  blueBand(ui, at(4, 300, 507, 41));

  // Active List (BAT.DAT buildings).
  ui.text(r.x + 22, r.y + 42, E.se(0x13C, "Active List"), "main10", kInk);
  {
    Rect box = at(20, 60, 164, 195);
    ui.fill(box, rgb(0xDE, 0xDF, 0xFF)); sunkenLines(ui, box);   // lavender list, as observed
    const int rowH = 16, visible = (box.h - 2) / rowH, total = int(s.active.size());
    if (ui.hover(box) && ui.in.wheel) s.activeScroll -= ui.in.wheel;
    s.activeScroll = std::max(0, std::min(s.activeScroll, std::max(0, total - visible)));
    for (int i = 0; i < visible && s.activeScroll + i < total; i++) {
      int idx = s.activeScroll + i;
      Rect row{box.x + 1, box.y + 1 + i * rowH, box.w - 2, rowH};
      E.spot("active." + std::to_string(idx), row);
      bool on = idx == s.activeSel;
      if (on) ui.fill(row, rgb(0x7B, 0x92, 0xFF));
      ui.text(row.x + 2, row.y, s.active[size_t(idx)].name, "main10", on ? kWhite : kInk);
      if (ui.clicked(row)) { s.activeSel = idx; s.previewFile = false; }
    }
    gzScroll(ui, E, "activeScroll", r.x + 183, box.y, box.h, total, visible, s.activeScroll);
  }
  // Remove (pill on a blue plate).
  {
    ui.fill(at(50, 264, 120, 35), rgb(0x7B, 0x92, 0xFF));
    bool en = s.activeSel >= 0 && s.activeSel < int(s.active.size());
    E.spot("remove", at(51, 265, 118, 33));
    if (ui.button(at(51, 265, 118, 33), E.se(0x13E, "Remove"), en, false, "title9") && en) R.add(Act::BatRemove, s.active[size_t(s.activeSel)].buildingId);
  }

  // Preview (light-yellow box) and size.
  {
    Rect box = at(209, 60, 97, 97);
    ui.fill(box, rgb(0xFF, 0xFF, 0xD6)); sunkenLines(ui, box);
    int lot = 0;
    if (s.previewFile) { imageFit(ui, s.fileThumb, {box.x + 2, box.y + 2, box.w - 4, box.h - 4}); lot = s.fileLot; }
    else if (s.activeSel >= 0 && s.activeSel < int(s.active.size())) {
      spriteCentred(ui, E.data, s.active[size_t(s.activeSel)].appearance, {box.x + 2, box.y + 2, box.w - 4, box.h - 4}, box);
      lot = s.active[size_t(s.activeSel)].lot;
    }
    if (lot > 0) {
      std::string t = E.se(0x133, "Size:") + " " + sizeText(lot);
      ui.text(r.x + 234, r.y + 166, t, "main9", kInk);   // position and font as observed
    }
  }
  // Add (big left arrow; cell 0 = disabled).
  {
    bool en = s.fileSel >= 0 && s.fileSel < int(s.files.size()) && s.files[size_t(s.fileSel)].type == 2;
    Rect ar = at(218, 195, 80, 54);
    if (cellButton(ui, E, "add", 0xFA3A81CA, ar, en, E.se(0x13F, "Add"))) R.add(Act::BatAdd, 0, 0, 0, s.files[size_t(s.fileSel)].path);
    // Caption on the arrow body (grey while disabled).
    ui.textCentered({ar.x + 14, ar.y, ar.w - 14, ar.h}, E.se(0x13F, "Add"), "title12", en ? kInk : rgb(0xAD, 0xAA, 0xAD));
  }

  // Look in: folder combo and the file list.
  ui.text(r.x + 322, r.y + 42, E.se(0x13D, "Look in"), "main10", kInk);
  {
    Rect combo = at(320, 60, 161, 21);   // edit-field outline, as observed
    ui.fill(combo, kWhite);
    ui.fill({combo.x, combo.y, combo.w, 1}, rgb(0x6B, 0x71, 0xBD)); ui.fill({combo.x, combo.y, 1, combo.h}, rgb(0x6B, 0x71, 0xBD));
    ui.fill({combo.x + 1, combo.y + 1, combo.w - 2, 1}, kInk); ui.fill({combo.x + 1, combo.y + 1, 1, combo.h - 2}, kInk);
    ui.fill({combo.x + combo.w - 1, combo.y, 1, combo.h}, rgb(0xB5, 0xB6, 0xE7)); ui.fill({combo.x, combo.y + combo.h - 1, combo.w, 1}, rgb(0xB5, 0xB6, 0xE7));
    if (s.icons[1]) ui.blit(*s.icons[1], combo.x + 3, combo.y + 2);
    ui.text(combo.x + 24, combo.y + 3, s.folder, "system9", kInk);
    Rect arrow = at(483, 62, 16, 16);
    bool c = false;
    if (!(sk && sk->imageButtonCells(ui, sk->get("combo"), 4, arrow, "", false, true, c))) ui.button(arrow, "v");
  }
  {
    Rect list = at(320, 90, 164, 206);
    ui.fill(list, kWhite); sunkenLines(ui, list);
    const int rowH = 17, visible = (list.h - 2) / rowH, total = int(s.files.size());
    if (ui.hover(list) && ui.in.wheel) s.fileScroll -= ui.in.wheel;
    s.fileScroll = std::max(0, std::min(s.fileScroll, std::max(0, total - visible)));
    for (int i = 0; i < visible && s.fileScroll + i < total; i++) {
      int idx = s.fileScroll + i;
      const BuildingListState::File& f = s.files[size_t(idx)];
      Rect row{list.x + 1, list.y + 1 + i * rowH, list.w - 2, rowH};
      E.spot("file." + std::to_string(idx), row);
      bool on = idx == s.fileSel;
      if (on) ui.fill(row, rgb(0, 0, 255));
      const Image* ic = s.icons[std::max(0, std::min(2, f.type))];
      if (ic && !ic->empty()) ui.blit(*ic, row.x + 2, row.y + (rowH - ic->h) / 2);
      ui.text(row.x + 23, row.y + 2, f.label, "system9", on ? kWhite : kInk);
      if (ui.clicked(row)) {
        bool dbl = lastClickRow_ == idx && frame_ - lastClickFrame_ < 30;
        lastClickRow_ = idx; lastClickFrame_ = frame_;
        s.fileSel = idx;
        if (f.type == 2) { s.previewFile = true; R.add(Act::BatSelectFile, 0, 0, 0, f.path); }
        else if (dbl) R.add(Act::BatOpenFolder, 0, 0, 0, f.path);
      }
    }
    gzScroll(ui, E, "fileScroll", r.x + 484, list.y, list.h, total, visible, s.fileScroll);
  }

  // Done (check mark).
  if (cellButton(ui, E, "ok", 0xA2DEFD9A, at(465, 308, 42, 32), true, "OK") || ui.in.key[K_ENTER]) { R.add(Act::Done); R.close = true; }
  if (ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }
  return R;
}

}  // namespace m3k
