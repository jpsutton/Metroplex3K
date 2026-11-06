#include <algorithm>
#include <cmath>
#include <cstring>
#include "core/windows.h"
#include "core/gamedata.h"
#include "core/skin.h"

namespace m3k {

// ================================================================ WinEnv / formatting
std::string WinEnv::text(uint32_t table, uint32_t id, const std::string& fallback) const {
  std::string s;
  if (textFn) s = textFn(table, id);
  else if (data) s = data->string(table, id);
  if (s.empty()) s = fallback;
  return expandTokens(s, tc);
}

std::string WinEnv::rawText(uint32_t table, uint32_t id) const {
  if (textFn) return textFn(table, id);
  if (data) return data->string(table, id);
  return std::string();
}

bool WinEnv::has(uint32_t table, uint32_t id) const {
  if (textFn) return !textFn(table, id).empty();
  if (data) return !data->string(table, id).empty();
  return false;
}

std::string WinEnv::money(int64_t v) const {
  std::string s = groupDigits(v < 0 ? -v : v);
  return (v < 0 ? "-" : "") + currency + s;
}

std::string subst(const std::string& tpl, const std::vector<std::string>& args) {
  std::string out;
  size_t next = 0;
  for (size_t i = 0; i < tpl.size(); i++) {
    char c = tpl[i];
    if (c != '%' || i + 1 >= tpl.size()) { out.push_back(c); continue; }
    char n = tpl[i + 1];
    if (n == '%') { out.push_back('%'); i++; continue; }
    if (n == 's' || n == 'd') { if (next < args.size()) out += args[next]; next++; i++; continue; }
    if ((n == 'x' || n == 'y') && i + 2 < tpl.size() && tpl[i + 2] == '%') {
      size_t k = n == 'x' ? 0 : 1;
      if (k < args.size()) out += args[k];
      i += 2;
      continue;
    }
    if (n >= '1' && n <= '9') { size_t k = size_t(n - '1'); if (k < args.size()) out += args[k]; i++; continue; }
    out.push_back(c);
  }
  return out;
}

// ================================================================ wui helpers
namespace wui {

Rect centered(Ui& ui, int w, int h) {
  Rect r = ui.movable({(ui.W - w) / 2, (ui.H - h) / 2, w, h});
  ui.windowRects.push_back(r);
  return r;
}

Rect placedAt(Ui& ui, int x, int y, int w, int h) {
  Rect r = ui.movable({x, y, w, h});
  ui.windowRects.push_back(r);
  return r;
}

// Spec 09 s6.0 [OBSERVED]: centred in the city-view area = the client minus the 96 px right panel and the bottom bar
// (56 px at 800 wide, 64 px at 1024 and up).
Rect viewAreaCentre(const Ui& ui, int w, int h) {
  const int bar = ui.W <= 800 ? 56 : 64;
  return {(ui.W - 96 - w) / 2, (ui.H - bar - h) / 2, w, h};
}
Rect centredInView(Ui& ui, int w, int h) {
  Rect c = viewAreaCentre(ui, w, h);
  return placedAt(ui, c.x, c.y, w, h);
}

int lineHeight(const Ui& ui) { return ui.fonts->ui().height() + 3; }

bool button(Ui& ui, const WinEnv& E, const std::string& name, Rect r, const std::string& label, bool enabled, bool toggled) {
  E.spot(name, r);
  return ui.button(r, label, enabled, toggled);
}

bool check(Ui& ui, const WinEnv& E, const std::string& name, Rect r, const std::string& label, bool& v) {
  E.spot(name, r);
  return ui.checkbox(r, label, v);
}

// Compass: disc and star image 000005E5:0D, letters N W E S drawn over it (measured on the original's Berlin screenshot, view
// rotation 0). With the view turned, the letters move round the disc with the map. (kx, ky) = disc centre.
void compassRose(Ui& ui, const WinEnv& E, int kx, int ky, int rot) {
  const uint32_t lt = rgb(0xDE, 0xDF, 0xFF);
    if (const Image* im = E.img(0x5E5, 0x0D)) ui.blit(*im, kx - 30, ky - 30);
    else {
      for (int y = ky - 30; y < ky + 30; y++)
        for (int x = kx - 30; x < kx + 30; x++)
          if ((x + 0.5 - kx) * (x + 0.5 - kx) + (y + 0.5 - ky) * (y + 0.5 - ky) <= 29.5 * 29.5) ui.fill({x, y, 1, 1}, rgb(0x7B, 0x92, 0xFF));
      const uint32_t dk = rgb(0x18, 0x20, 0x6B);
      for (int k = 0; k <= 12; k++) {
        int w = std::max(0, 3 - k / 4);
        ui.fill({kx - w, ky - k, w, 1}, dk); ui.fill({kx, ky - k, w, 1}, lt);
        ui.fill({kx - k, ky - w, 1, w}, dk); ui.fill({kx - k, ky, 1, w}, lt);
        ui.fill({kx + k, ky - w, 1, w}, lt); ui.fill({kx + k, ky, 1, w}, dk);
        ui.fill({kx - w, ky + k, w, 1}, lt); ui.fill({kx, ky + k, w, 1}, dk);
      }
    }
    // Letter centres relative to the disc centre at rotation 0 (twice the offset, to keep the half pixels).
    struct L { uint32_t id; const char* fb; int dx2, dy2; };
    const L letters[4] = {{0x11D, "N", 0, -37}, {0x11E, "W", -39, 1}, {0x11F, "E", 40, 0}, {0x120, "S", -2, 39}};
    for (const L& l : letters) {
      int dx2 = l.dx2, dy2 = l.dy2;
      for (int k = 0; k < rot; k++) { int t = dx2; dx2 = dy2; dy2 = -t; }
      std::string t = E.win(l.id, l.fb);
      ui.text(kx + (dx2 - ui.textWidth(t, "title9")) / 2, ky + (dy2 - 19) / 2, t, "title9", lt);
    }
  }

// Text rotated 90 degrees counter-clockwise (reads bottom to top), box top-left (x, y), box size (font height) x (text width).
void verticalText(Ui& ui, int x, int y, const std::string& s, const char* font, uint32_t col) {
  const Font& f = ui.fonts->get(font);
  const int w = f.measure(s), h = f.height();
  if (w <= 0 || h <= 0) return;
  std::vector<uint32_t> tmp(size_t(w) * size_t(h), 0xFFFFFFFFu);
  f.draw(tmp, w, h, 0, 0, s, col | 0xFF000000u);
  for (int sy = 0; sy < h; sy++)
    for (int sx = 0; sx < w; sx++) {
      const uint32_t p = tmp[size_t(sy) * size_t(w) + size_t(sx)];
      if (p == 0xFFFFFFFFu) continue;
      const int dx = x + sy, dy = y + (w - 1 - sx);
      if (dx >= 0 && dy >= 0 && dx < ui.W && dy < ui.H) ui.at(dx, dy) = p | 0xFF000000u;
    }
}

bool radio(Ui& ui, const WinEnv& E, const std::string& name, Rect r, const std::string& label, bool selected, const char* font) {
  const Skin* sk = ui.skin;
  const Image* art = sk ? sk->get("radio") : nullptr;
  const uint32_t ink = rgb(49, 52, 123);   // radio label ink [OBSERVED]
  E.spot(name, r);
  const bool hot = ui.hover(r);
  const int box = 12, by = r.y + (r.h - box) / 2;
  if (!(sk && sk->drawCheck(ui, art, r.x, by, selected, true, hot, hot && ui.in.down))) {
    ui.fill({r.x, by, box, box}, rgb(255, 255, 255));
    ui.frame({r.x, by, box, box}, false);
    if (selected) ui.fill({r.x + 3, by + 3, box - 6, box - 6}, ink);
  }
  ui.text(r.x + box + 3 + ui.radioLabelDx, r.y + (r.h - ui.fonts->get(font).height()) / 2, label, font, ink);
  return ui.clicked(r);
}

bool colourButton(Ui& ui, const WinEnv& E, const std::string& name, Rect r, const std::string& label, uint32_t col) {
  E.spot(name, r);
  bool hot = ui.hover(r), down = hot && ui.in.down;
  ui.fill(r, hot ? rgb(0xDC, 0xE4, 0xF0) : ui.panelCol);
  ui.frame(r, !down);
  ui.textCentered({r.x + (down ? 1 : 0), r.y + (down ? 1 : 0), r.w, r.h}, label, "main10", col);
  return ui.clicked(r);
}

bool frame(Ui& ui, const WinEnv& E, Rect r, const std::string& title) {
  ui.panel(r, title);
  if (ui.skin && !title.empty()) {
    Rect hit; bool clicked;
    if (ui.skin->dialogCloseBox(ui, r, hit, clicked)) { E.spot("close", hit); return clicked; }
  }
  Rect c{r.x + r.w - 21, r.y + 4, 16, 16};
  return button(ui, E, "close", c, "x");
}

std::vector<std::string> wrap(const Ui& ui, const std::string& s, int width, const char* font) {
  std::vector<std::string> lines;
  size_t p = 0;
  while (p <= s.size()) {
    size_t e = s.find('\n', p);
    std::string para = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
    std::string cur, word;
    auto flush = [&]() {
      if (word.empty()) return;
      std::string t = cur.empty() ? word : cur + " " + word;
      if (!cur.empty() && ui.textWidth(t, font) > width) { lines.push_back(cur); cur = word; }
      else cur = t;
      word.clear();
    };
    for (char c : para) { if (c == ' ') flush(); else word.push_back(c); }
    flush();
    lines.push_back(cur);
    if (e == std::string::npos) break;
    p = e + 1;
  }
  return lines;
}

// Like wrap(), but runs of spaces inside a line are kept (the original's text boxes show the double space after a full stop); the spaces at
// a break are dropped.
std::vector<std::string> wrapKeepSpaces(const Ui& ui, const std::string& s, int width, const char* font, bool measureCollapsed) {
  std::vector<std::string> lines;
  size_t p = 0;
  while (p <= s.size()) {
    size_t e = s.find('\n', p);
    const std::string para = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
    std::string cur;
    size_t i = 0;
    while (i < para.size()) {
      size_t sp = i; while (sp < para.size() && para[sp] == ' ') sp++;
      size_t we = sp; while (we < para.size() && para[we] != ' ') we++;
      const std::string gap = para.substr(i, sp - i), word = para.substr(sp, we - sp);
      i = we;
      if (word.empty()) { if (cur.empty()) cur = gap; else cur += gap; break; }
      std::string t = cur.empty() ? (gap.size() > 1 && lines.empty() ? gap + word : word) : cur + gap + word;
      // measureCollapsed: the original measures a run of spaces as one space while drawing all of them (goal text, observed).
      std::string m = t;
      if (measureCollapsed) { m.clear(); for (char ch : t) if (!(ch == ' ' && !m.empty() && m.back() == ' ')) m.push_back(ch); }
      if (!cur.empty() && ui.textWidth(m, font) > width) { lines.push_back(cur); cur = word; }
      else cur = t;
    }
    lines.push_back(cur);
    if (e == std::string::npos) break;
    p = e + 1;
  }
  return lines;
}

int paragraph(Ui& ui, Rect r, const std::string& s, const char* font, uint32_t col) {
  int lh = ui.fonts->get(font).height() + 2;
  int y = 0;
  for (auto& l : wrap(ui, s, r.w, font)) {
    if (r.h > 0 && y + lh > r.h) break;
    ui.text(r.x, r.y + y, l, font, col);
    y += lh;
  }
  return y;
}

void rightText(Ui& ui, int right, int y, const std::string& s, const char* font, uint32_t col) {
  ui.text(right - ui.textWidth(s, font), y, s, font, col);
}

void labelValue(Ui& ui, int x, int y, int w, const std::string& label, const std::string& value, uint32_t col) {
  ui.text(x, y, label, "main10", col);
  rightText(ui, x + w, y, value, "main10", col);
}

void bar(Ui& ui, Rect r, float frac, uint32_t col) {
  ui.fill(r, rgb(255, 255, 255));
  ui.frame(r, false);
  int w = int(float(r.w - 2) * std::max(0.f, std::min(1.f, frac)));
  ui.fill({r.x + 1, r.y + 1, w, r.h - 2}, col);
}

bool stepper(Ui& ui, const WinEnv& E, const std::string& name, Rect r, int& v, int lo, int hi, int step, const std::string& suffix) {
  int old = v;
  Rect minus{r.x, r.y, 20, r.h}, plus{r.x + r.w - 20 - 56, r.y, 20, r.h};
  Rect sl{r.x + 24, r.y + 2, r.w - 24 - 20 - 56 - 4, r.h - 4};
  if (button(ui, E, name + ".minus", minus, "-") ) v = std::max(lo, v - step);
  if (button(ui, E, name + ".plus", plus, "+")) v = std::min(hi, v + step);
  E.spot(name + ".slider", sl);
  int sv = v;
  if (ui.slider(sl, sv, lo, hi) && step > 0) {
    sv = lo + int(std::lround(double(sv - lo) / step)) * step;
    v = std::max(lo, std::min(hi, sv));
  }
  ui.textCentered({r.x + r.w - 54, r.y, 54, r.h}, std::to_string(v) + suffix);
  return v != old;
}

void scrollBar(Ui& ui, const WinEnv& E, Rect r, int total, int visible, int& scroll) {
  Rect up{r.x + r.w - 14, r.y, 14, 14}, dn{r.x + r.w - 14, r.y + r.h - 14, 14, 14};
  if (ui.hover(r) && ui.in.wheel) scroll -= ui.in.wheel;
  if (button(ui, E, "scroll.up", up, "^")) scroll--;
  if (button(ui, E, "scroll.down", dn, "v")) scroll++;
  scroll = std::max(0, std::min(scroll, std::max(0, total - visible)));
  if (total > visible) {
    int track = r.h - 32;
    int th = std::max(8, track * visible / total);
    int ty = r.y + 16 + (track - th) * scroll / std::max(1, total - visible);
    ui.fill({r.x + r.w - 12, r.y + 16, 10, track}, ui.panelDark);
    ui.fill({r.x + r.w - 12, ty, 10, th}, ui.panelLight);
  }
}

// Classic scroll bar of the original's list boxes (spec 09 6.3 [OBSERVED]): square arrow buttons, a thin blue track and a
// square thumb; r is 16 px wide. The mouse wheel works over `wheelArea`; a press on the track follows the pointer.
void classicScrollBar(Ui& ui, const WinEnv& E, Rect r, Rect wheelArea, int total, int visible, int& scroll) {
  const uint32_t face = rgb(0xD6, 0xDB, 0xFF), hi = rgb(0xFF, 0xF3, 0xFF), lo = rgb(0x31, 0x38, 0x84), mid = rgb(0x7B, 0x8A, 0xFF);
  auto bevel = [&](Rect b, bool down) {
    ui.fill(b, face);
    ui.fill({b.x, b.y, b.w, 1}, down ? lo : hi); ui.fill({b.x, b.y, 1, b.h}, down ? lo : hi);
    ui.fill({b.x, b.y + b.h - 1, b.w, 1}, down ? hi : lo); ui.fill({b.x + b.w - 1, b.y, 1, b.h}, down ? hi : lo);
  };
  auto arrow = [&](const std::string& name, Rect b, bool up) {
    E.spot(name, b);
    const bool down = ui.hover(b) && ui.in.down;
    bevel(b, down);
    const int cx = b.x + b.w / 2 + (down ? 1 : 0), cy = b.y + b.h / 2 + (down ? 1 : 0);
    for (int i = 0; i < 4; i++) ui.fill({cx - i, up ? cy - 2 + i : cy + 1 - i, 2 * i + 1, 1}, lo);
    return ui.clicked(b);
  };
  const Rect up{r.x, r.y, r.w, 16}, dn{r.x, r.y + r.h - 16, r.w, 16};
  if (ui.hover(wheelArea) && ui.in.wheel) scroll -= ui.in.wheel;
  if (arrow("scroll.up", up, true)) scroll--;
  if (arrow("scroll.down", dn, false)) scroll++;
  const int range = std::max(0, total - visible);
  const Rect track{r.x, r.y + 16, r.w, r.h - 32};
  ui.fill({track.x + track.w / 2 - 1, track.y, 3, track.h}, mid);
  if (range > 0) {
    const int th = std::min(16, track.h);
    E.spot("scroll.track", track);
    if (ui.hover(track) && ui.in.down) {
      const int pos = ui.in.my - track.y - th / 2;
      scroll = (pos * range + (track.h - th) / 2) / std::max(1, track.h - th);
    }
    scroll = std::max(0, std::min(scroll, range));
    bevel({track.x, track.y + (track.h - th) * scroll / range, track.w, th}, false);
  } else {
    scroll = 0;
  }
}

// Bevelled frame of the scenario text box and goals list: dark (123,138,255) left/top, light (255,243,255) right/bottom [OBSERVED 800 x 600].
void insetBevel(Ui& ui, Rect b) {
  ui.fill({b.x, b.y, 1, b.h}, rgb(123, 138, 255)); ui.fill({b.x, b.y, b.w, 1}, rgb(123, 138, 255));
  ui.fill({b.x + b.w - 1, b.y, 1, b.h}, rgb(255, 243, 255)); ui.fill({b.x, b.y + b.h - 1, b.w, 1}, rgb(255, 243, 255));
}

// Vertical scroll bar from the skin strip Sys 82B9B75B:E2B99E01 (192 x 16: twelve 16 x 16 cells; 6/7 up arrow normal/pressed, 8 vertical thumb,
// 9 vertical groove, 10/11 down arrow) [OBSERVED 800 x 600: up arrow at the top of r, thumb below it, groove tiled, down arrow at the bottom].
// Scrolls by item. r is 16 px wide.
void scenarioScrollBar(Ui& ui, const WinEnv& E, Rect r, Rect wheelArea, int total, int visible, int& scroll) {
  const Image* strip = ui.skin ? ui.skin->image(skin_group::Sys, 0xE2B99E01) : nullptr;
  ui.fill(r, rgb(123, 146, 255));
  const Rect up{r.x, r.y, 16, 16}, dn{r.x, r.y + r.h - 16, 16, 16};
  E.spot("scroll.up", up); E.spot("scroll.down", dn);
  if (ui.hover(wheelArea) && ui.in.wheel) scroll -= ui.in.wheel;
  if (ui.clicked(up)) scroll--;
  if (ui.clicked(dn)) scroll++;
  const int range = std::max(0, total - visible);
  const Rect track{r.x, r.y + 16, 16, r.h - 32};
  const int th = 16;
  if (range > 0) {
    E.spot("scroll.track", track);
    if (ui.hover(track) && ui.in.down) {
      const int pos = ui.in.my - track.y - th / 2;
      scroll = (pos * range + (track.h - th) / 2) / std::max(1, track.h - th);
    }
    scroll = std::max(0, std::min(scroll, range));
  } else scroll = 0;
  const int ty = track.y + (range > 0 ? (track.h - th) * scroll / range : 0);
  if (!strip) {   // no art: flat stand-ins
    ui.fill(up, rgb(214, 215, 255)); ui.fill(dn, rgb(214, 215, 255)); ui.fill({track.x, ty, 16, th}, rgb(214, 215, 255));
    return;
  }
  for (int y = track.y; y < track.y + track.h; y += 16)   // groove, tiled (the last tile clipped to the track)
    ui.blitRect(*strip, track.x, y, 9 * 16, 0, 16, std::min(16, track.y + track.h - y));
  const bool upDown = ui.hover(up) && ui.in.down, dnDown = ui.hover(dn) && ui.in.down;
  Skin::drawCell(ui, strip, 12, upDown ? 7 : 6, up.x, up.y);
  Skin::drawCell(ui, strip, 12, dnDown ? 11 : 10, dn.x, dn.y);
  if (range > 0) Skin::drawCell(ui, strip, 12, 8, track.x, ty);
}

void portrait(Ui& ui, Rect r, const Image* im, const std::string& letter, uint32_t col) {
  if (im && !im->empty()) {
    ui.fill(r, ui.panelDark);
    ui.blit(*im, r.x + (r.w - im->w) / 2, r.y + (r.h - im->h) / 2);
  } else {
    ui.fill(r, col);
    ui.textCentered(r, letter, "title12", rgb(255, 255, 255));
  }
  ui.frame(r, false);
}

void sunkenLines(Ui& ui, Rect b) {
  ui.fill({b.x, b.y, b.w, 1}, rgb(0x7B, 0x8A, 0xFF)); ui.fill({b.x, b.y, 1, b.h}, rgb(0x7B, 0x8A, 0xFF));
  ui.fill({b.x, b.y + b.h - 1, b.w, 1}, rgb(0xFF, 0xF3, 0xFF)); ui.fill({b.x + b.w - 1, b.y, 1, b.h}, rgb(0xFF, 0xF3, 0xFF));
}

void blueBand(Ui& ui, Rect b) {
  ui.fill(b, rgb(0xAD, 0xB6, 0xFF));
  ui.fill({b.x + 1, b.y + 1, b.w - 2, b.h - 2}, rgb(0x7B, 0x92, 0xFF));
}

bool editField(Ui& ui, const WinEnv& E, const std::string& name, Rect fr, std::string& val, int id, size_t maxLen, bool multiLine,
               const char* filter, bool plain) {
  const uint32_t ink = rgb(0x31, 0x34, 0x7B);
  E.spot(name, fr);
  ui.fill(fr, rgb(255, 255, 255));
  if (plain) {   // `fr` is the white interior; a 1 px black frame is drawn just outside it
    ui.fill({fr.x - 1, fr.y - 1, fr.w + 2, 1}, 0xFF000000u); ui.fill({fr.x - 1, fr.y + fr.h, fr.w + 2, 1}, 0xFF000000u);
    ui.fill({fr.x - 1, fr.y, 1, fr.h}, 0xFF000000u); ui.fill({fr.x + fr.w, fr.y, 1, fr.h}, 0xFF000000u);
  } else {
    ui.fill({fr.x, fr.y, fr.w, 1}, rgb(0x6B, 0x71, 0xBD)); ui.fill({fr.x, fr.y, 1, fr.h}, rgb(0x6B, 0x71, 0xBD));
    ui.fill({fr.x + 1, fr.y + 1, fr.w - 2, 1}, ink); ui.fill({fr.x + 1, fr.y + 1, 1, fr.h - 2}, ink);
    ui.fill({fr.x + fr.w - 2, fr.y + 1, 1, fr.h - 2}, rgb(0x7B, 0x82, 0xBD));
    if (multiLine) ui.fill({fr.x + 1, fr.y + fr.h - 2, fr.w - 2, 1}, rgb(0x7B, 0x82, 0xBD));
    ui.fill({fr.x + fr.w - 1, fr.y, 1, fr.h}, rgb(0xB5, 0xB6, 0xE7)); ui.fill({fr.x, fr.y + fr.h - 1, fr.w, 1}, rgb(0xB5, 0xB6, 0xE7));
  }
  if (ui.in.pressed) { if (fr.contains(ui.in.mx, ui.in.my)) ui.focusId = id; else if (ui.focusId == id) ui.focusId = 0; }
  bool focus = ui.focusId == id, edited = false;
  if (focus) {
    for (char c : ui.in.text)
      if (c >= 32 && val.size() < maxLen && !(filter && std::strchr(filter, c))) { val.push_back(c); edited = true; }
    if (ui.in.key[K_BACKSPACE] && !val.empty()) { val.pop_back(); edited = true; }
  }
  std::string shown = val + (focus ? "|" : "");
  if (!multiLine) { if (plain) ui.text(fr.x + 1, fr.y, shown, "main9", ink); else ui.text(fr.x + 4, fr.y + 3, shown, "main9", ink); }
  else {
    int y = fr.y + 3;
    for (const auto& l : wrap(ui, shown, fr.w - 8, "main9")) { if (y + 14 > fr.y + fr.h - 2) break; ui.text(fr.x + 4, y, l, "main9", ink); y += 14; }
  }
  return edited;
}

void spriteAt(Ui& ui, GameData* gd, uint32_t cat, uint32_t id, int ax, int ay) {
  if (!gd || !id || !ui.pixels) return;
  const SpriteImage* sp = gd->sprite(cat, id);
  if (!sp) return;
  const Plane* pl = gd->spritePlane(cat, id + 1);
  if (pl && (pl->w != sp->img.w || pl->h != sp->img.h)) pl = nullptr;
  const int x0 = ax - sp->L, y0 = ay - sp->T;
  for (int y = 0; y < sp->img.h; y++) {
    int dy = y0 + y; if (dy < 0 || dy >= ui.H) continue;
    for (int x = 0; x < sp->img.w; x++) {
      int dx = x0 + x; if (dx < 0 || dx >= ui.W) continue;
      uint32_t c = sp->img.px[size_t(y) * sp->img.w + x];
      if (pl && !ui.overlay) {
        // Plane sprites [OBSERVED, New City preview, 0 px differ]: visit only the pixels where the colour sprite is opaque; blend in 5 bits per channel with the
        // plane level a5 (0..31, m2 = 32): floor((32 d + a5 (s - d)) / 32), green taken at 5 bits (g6 >> 1); write back as 565 with the green LSB cleared, also at a5 = 0.
        if ((c >> 24) == 0 || (c & 0xFFFFFF) == 0xFF00FF) continue;
        const int a5 = std::min<int>(31, pl->v[size_t(y) * pl->w + x]);
        uint32_t& d = ui.at(dx, dy);
        int out[3];
        for (int k = 0; k < 3; k++) {
          const int sh = k * 8;
          const int s5 = int((c >> sh) & 255) >> 3, d5 = int((d >> sh) & 255) >> 3;
          out[k] = std::max(0, std::min(31, (32 * d5 + a5 * (s5 - d5)) >> 5));
        }
        const int g6 = out[1] << 1;
        d = 0xFF000000u | uint32_t((out[2] << 3) | (out[2] >> 2)) << 16 | uint32_t((g6 << 2) | (g6 >> 4)) << 8 | uint32_t((out[0] << 3) | (out[0] >> 2));
        continue;
      }
      int a = pl ? pl->v[size_t(y) * pl->w + x] * 255 / 31 : int(c >> 24);
      if (!pl && (c & 0xFFFFFF) == 0xFF00FF) a = 0;
      if (a <= 0) continue;
      uint32_t& d = ui.at(dx, dy);
      if (ui.overlay && (d >> 24) != 255) { d = overlayOver(d, float(c & 255), float((c >> 8) & 255), float((c >> 16) & 255), float(a) / 255.f); continue; }
      uint32_t o = 0xFF000000u;
      for (int k = 0; k < 24; k += 8) o |= uint32_t((int((c >> k) & 255) * a + int((d >> k) & 255) * (255 - a) + 127) / 255) << k;
      d = o;
    }
  }
}

void schemePreview(Ui& ui, GameData* gd, int x, int y, uint32_t land, uint32_t flora, uint32_t building) {
  // Top-left corners fitted on the original's previews (dialog-relative): landscape (444,92), left tree (445,110) and
  // house (470,145) for every set seen; right tree see below. Drawn back to front:
  // landscape, right tree, house, left tree.
  auto put = [&](uint32_t id, int px, int py) {
    if (const SpriteImage* sp = gd ? gd->sprite(0, id) : nullptr) spriteAt(ui, gd, 0, id, x + px + sp->L, y + py + sp->T);
  };
  put(land, 444, 92);
  // The right tree's place depends on the tree set; observed for the default pines (3FBE0000) and the broadleaf set
  // (3FBB0000). Other sets use the pine position until observed (the sprite spans do not explain the difference).
  // Each flora choice has two sprites; the second (xxxx0001, fmt 0x03) decodes as a plane, not a tree, so the same tree sprite is drawn twice [the 79fe354
  // attempt to draw it as the right tree left that tree missing].
  // Right tree, behind the house: the same sprite as the left tree at (546,90) for every set [OBSERVED, both sets 0 px differ].
  put(flora, 546, 90);
  put(building, 470, 145);
  put(flora, 445, 110);
}

}  // namespace wui

// ================================================================ message box (spec 08 9.1)
std::vector<MsgButton> MessageBox::buttonsFor(int style) {
  switch (style) {
    case 1: return {MsgButton::OK, MsgButton::Cancel};
    case 2: return {MsgButton::Retry, MsgButton::Cancel};
    case 3: return {MsgButton::Yes, MsgButton::No};
    case 4: return {MsgButton::Yes, MsgButton::No, MsgButton::Cancel};
    case 5: return {MsgButton::Abort, MsgButton::Retry, MsgButton::Ignore};
    default: return {MsgButton::OK};
  }
}

void MessageBox::open(const std::string& title, const std::string& message, int style, int icon) {
  title_ = title; message_ = message; style_ = std::max(0, std::min(5, style)); icon_ = icon; active = true; closeCancels = false;
}

std::string msgButtonCaption(const WinEnv& E, MsgButton b) {
  switch (b) {
    case MsgButton::Abort: return E.app(0x114, "Abort");
    case MsgButton::Cancel: return E.app(0xB6, "Cancel");
    case MsgButton::Ignore: return E.app(0x118, "Ignore");
    case MsgButton::No: return E.app(0xC2, "No");
    case MsgButton::OK: return E.app(0xB4, "OK");
    case MsgButton::Retry: return E.app(0x116, "Retry");
    case MsgButton::Yes: return E.app(0xC0, "Yes");
  }
  return "";
}

MessageBox::Layout MessageBox::layout(const Ui& ui) const {
  Layout L;
  int n = int(buttonsFor(style_).size());
  L.base = n == 1 ? 100 : n == 2 ? 208 : 316;
  int W = std::max(200, L.base);
  if (!title_.empty()) W = std::max(W, ui.textWidth(title_, "title12") + 16);
  int lh = wui::lineHeight(ui);
  auto lines = wui::wrap(ui, message_, W);
  int H = int(lines.size()) * lh;
  while (2 * H >= W && W < 4000) {
    W += 16;
    lines = wui::wrap(ui, message_, W);
    H = int(lines.size()) * lh;
  }
  L.W = W; L.H = H; L.lines = lines;
  // Frame measured on the original 2-button prompts (288x139 for one text line): the text column is W wide and
  // starts 56 px in (icon column at the left); title band 4..34; sunken body panel from y 39, at least 53 tall;
  // blue button band 8 px below it, 35 tall, 4 px bottom margin; the 33 px button art sits unscaled 1 px into the
  // band, 100 px wide, 8 px apart, centred.
  L.boxW = W + 80;
  L.textX = 56;
  L.bodyY = 39;
  L.bodyH = std::max(53, H + 26);
  L.bandY = L.bodyY + L.bodyH + 8;
  L.boxH = L.bandY + 35 + 4;
  L.buttonY = L.bandY + 1;
  L.buttonX0 = (L.boxW - L.base) / 2;
  return L;
}

WindowResult MessageBox::draw(Ui& ui, const WinEnv& E) {
  WindowResult R;
  if (!active) return R;
  Layout L = layout(ui);
  const int lh = wui::lineHeight(ui);
  Rect box = centreInView ? wui::centredInView(ui, L.boxW, L.boxH) : wui::centered(ui, L.boxW, L.boxH);   // observed: in a city the box is centred on the viewport
  const uint32_t ink = rgb(0x31, 0x34, 0x7B);
  bool closeClicked = false;
  if (ui.skin && ui.skin->drawDialogFrame(ui, box, title_)) {
    Rect hit;
    if (ui.skin->dialogCloseBox(ui, box, hit, closeClicked)) E.spot("close", hit);
    // Sunken body panel: dark blue top/left line, light bottom/right line over the cloud body.
    Rect body{box.x + 4, box.y + L.bodyY, box.w - 8, L.bodyH};
    ui.fill(body, rgb(222, 223, 255));   // flat interior (the cloud texture does not show through) [OBSERVED]
    ui.fill({body.x, body.y, body.w, 1}, rgb(0x7B, 0x8A, 0xFF));
    ui.fill({body.x, body.y, 1, body.h}, rgb(0x7B, 0x8A, 0xFF));
    ui.fill({body.x, body.y + body.h - 1, body.w, 1}, rgb(0xFF, 0xF3, 0xFF));
    ui.fill({body.x + body.w - 1, body.y, 1, body.h}, rgb(0xFF, 0xF3, 0xFF));
    // Button band: same blue band with lighter ring as the title bar.
    Rect band{box.x + 4, box.y + L.bandY, box.w - 8, 35};
    ui.fill(band, rgb(173, 182, 255));
    ui.fill({band.x + 1, band.y + 1, band.w - 2, band.h - 2}, rgb(123, 146, 255));
    const Image* icons = ui.skin->image(skin_group::Sys, 0x42E55FD4);
    if (icons && icon_ >= 0) Skin::drawCell(ui, icons, 4, icon_, box.x + 12, box.y + L.bodyY + 12);
  } else {
    ui.panel(box, title_.empty() ? " " : title_);
    if (wui::button(ui, E, "close", {box.x + box.w - 21, box.y + 4, 16, 16}, "x")) closeClicked = true;
  }
  // Text block centred in the body panel, each line centred in the text column (odd leftover rounds right, as observed).
  int ty = box.y + L.bodyY + (L.bodyH - int(L.lines.size()) * lh) / 2;
  const int fh = ui.fonts->get("main10").height();
  for (auto& l : L.lines) {
    ui.text(box.x + L.textX + (L.W - ui.textWidth(l) + 1) / 2, ty + (lh - fh) / 2, l, "main10", ink);
    ty += lh;
  }
  auto bs = buttonsFor(style_);
  int idx = -1;
  for (size_t i = 0; i < bs.size(); i++) {
    Rect b{box.x + L.buttonX0 + int(i) * 108, box.y + L.buttonY, 100, 33};
    E.spot("msg." + std::to_string(i), b);
    if (ui.button(b, msgButtonCaption(E, bs[i]), true, false, "title9")) idx = int(i);   // caption font as observed
  }
  if (ui.in.key[K_ENTER]) idx = 0;
  if (ui.in.key[K_ESC] || closeClicked) idx = int(bs.size()) - 1;
  if (idx >= 0) {
    active = false;
    const MsgButton res = (closeCancels && (ui.in.key[K_ESC] || closeClicked)) ? MsgButton::Cancel : bs[size_t(idx)];
    R.add(Act::MessageResult, 0, int64_t(res));
    R.close = true;
  }
  return R;
}

// ================================================================ Tip of the Day (spec 09 6.14)
int TipWindow::next(int t) { t++; if (t == 0x10) t++; if (t > 0x39 || t < 1) t = 1; return t; }
int TipWindow::prev(int t) { t--; if (t == 0x10) t--; if (t < 1) t = 0x39; if (t > 0x39) t = 0x39; return t; }

WindowResult TipWindow::draw(Ui& ui, const WinEnv& E) {
  WindowResult R;
  Rect r = wui::centered(ui, 420, 210);
  if (wui::frame(ui, E, r, E.text(kTblTod, 0x3B, "Tip of the Day"))) R.close = true;
  std::string body = E.text(kTblTod, uint32_t(tip < 1 ? 1 : tip), "Tip " + std::to_string(tip) + ": text unavailable.");
  wui::paragraph(ui, {r.x + 14, r.y + 34, r.w - 28, r.h - 100}, body);
  bool v = showAtStartup;
  if (wui::check(ui, E, "showTips", {r.x + 14, r.y + r.h - 56, 250, 18}, E.text(kTblTod, 0x3D, "Show tips at startup"), v)) {
    showAtStartup = v;
    R.add(Act::ShowTipsChanged, 0, v);
  }
  if (wui::button(ui, E, "prev", {r.x + r.w - 244, r.y + r.h - 34, 70, 24}, "<")) { this->tip = prev(this->tip); R.add(Act::TipChanged, 0, this->tip); }
  if (wui::button(ui, E, "next", {r.x + r.w - 170, r.y + r.h - 34, 100, 24}, E.text(kTblTod, 0x3C, "Next tip"))) { this->tip = next(this->tip); R.add(Act::TipChanged, 0, this->tip); }
  if (wui::button(ui, E, "done", {r.x + 14, r.y + r.h - 34, 80, 24}, E.app(0xB4, "OK"))) { R.close = true; R.add(Act::Done); }
  if (ui.in.key[K_ESC]) R.close = true;
  return R;
}

// ================================================================ Credits (spec 09 6.15)
// Credits table layout [DATA]: ids 1..199 are left-hand entries, id + 199 holds the matching name(s) (lines joined by '\n').
// A leading ASCII digit is a style code: '1' heading, '4' normal entry, '5' placeholder that is not shown. No digit = normal.
std::vector<CreditsWindow::Row> CreditsWindow::layoutRows(const std::vector<uint32_t>& ids, const std::vector<std::string>& lines) {
  constexpr uint32_t kNameBase = 200, kNameOffset = 199;
  std::map<uint32_t, std::string> byId;
  for (size_t i = 0; i < ids.size() && i < lines.size(); i++) byId[ids[i]] = lines[i];
  auto splitLines = [](const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == '\n') { out.push_back(cur); cur.clear(); } else if (c != '\r') cur += c; }
    out.push_back(cur);
    return out;
  };
  std::vector<Row> rows;
  for (auto& [id, raw] : byId) {
    if (id >= kNameBase) continue;                      // consumed as a name below
    char style = 0; std::string text = raw;
    if (!text.empty() && text[0] >= '0' && text[0] <= '9') { style = text[0]; text.erase(0, 1); }
    if (style == '5') continue;
    Row r; r.left = text;
    auto nm = byId.find(id + kNameOffset);
    if (style == '1') r.kind = Row::Heading;
    else if (nm != byId.end() && !nm->second.empty()) { r.kind = Row::Pair; r.names = splitLines(nm->second); }
    else r.kind = text.empty() ? Row::Blank : Row::Centered;
    // Blank line before a heading, and between paired and centred rows.
    if (!rows.empty()) {
      Row::Kind prev = rows.back().kind;
      bool swap = (r.kind == Row::Pair && prev == Row::Centered) || (r.kind == Row::Centered && prev == Row::Pair);
      if (r.kind == Row::Heading ? (prev != Row::Blank && prev != Row::Heading) : swap) rows.push_back(Row());
    }
    rows.push_back(r);
  }
  return rows;
}

WindowResult CreditsWindow::draw(Ui& ui, const WinEnv& E) {
  WindowResult R;
  if (lines.empty()) {
    for (uint32_t id = 1; id <= 378; id++) {
      std::string s = E.text(kTblCredits, id, "");
      if (!s.empty()) { lines.push_back(s); ids.push_back(id); }
    }
    if (lines.empty()) {
      lines = {"Metroplex3K", "", "A clean-room SimCity 3000 Unlimited compatible engine", "", "Original game by Maxis", "Thanks for playing"};
      for (size_t i = 0; i < lines.size(); i++) ids.push_back(uint32_t(i + 1));
    }
    rows = layoutRows(ids, lines);
  }
  Rect r = wui::centered(ui, 560, 420);
  const uint32_t bgCol = rgb(8, 10, 30);
  ui.fill({0, 0, ui.W, ui.H}, bgCol);
  const Image* bg = E.img(0x82B9B75C, 0x22729940);
  int logoH = 0;
  if (bg) { ui.blit(*bg, r.x + (r.w - bg->w) / 2, r.y); logoH = bg->h; }
  // Scrolling text sits below the logo so it never overlaps it.
  Rect view{r.x + 10, r.y + logoH + 8, r.w - 20, r.h - logoH - 16};
  // Clip by rendering into a sub-buffer.
  std::vector<uint32_t> buf(size_t(view.w) * view.h, bgCol);
  Ui sub = ui; sub.setTarget(&buf, view.w, view.h);
  int lh = wui::lineHeight(ui) + 2;
  int totalLines = 0;
  for (auto& row : rows) totalLines += row.lineCount();
  double total = double(totalLines) * lh + view.h;
  if (scroll > total) scroll = 0;
  const int colGap = 8, mid = view.w / 2;
  int line = 0;
  for (auto& row : rows) {
    int y = int(view.h - scroll + double(line) * lh);
    line += row.lineCount();
    if (y < -lh * row.lineCount() || y > view.h) continue;
    switch (row.kind) {
      case Row::Blank: break;
      case Row::Heading: sub.text((view.w - ui.textWidth(row.left)) / 2, y, row.left, "main10", rgb(255, 214, 120)); break;
      case Row::Centered: sub.text((view.w - ui.textWidth(row.left)) / 2, y, row.left, "main10", rgb(230, 235, 255)); break;
      case Row::Pair:
        sub.text(mid - colGap - ui.textWidth(row.left), y, row.left, "main10", rgb(170, 180, 220));
        for (size_t k = 0; k < row.names.size(); k++) sub.text(mid + colGap, y + int(k) * lh, row.names[k], "main10", rgb(230, 235, 255));
        break;
    }
  }
  for (int y = 0; y < view.h; y++)
    for (int x = 0; x < view.w; x++) {
      int dx = view.x + x, dy = view.y + y;
      if (dx >= 0 && dy >= 0 && dx < ui.W && dy < ui.H) ui.at(dx, dy) = buf[size_t(y) * view.w + x];
    }
  if (ui.in.key[K_ESC] || ui.in.pressed) R.close = true;
  return R;
}

// ================================================================ news ticker strip (spec 09 4.4)
void TickerWidget::push(const NewsItem& n, int width) {
  Item it; it.n = n;
  int right = 0;   // estimated right edge of the last item (6 px per glyph), items separated by 20 px
  for (auto& i : items) right = std::max(right, i.x + int(i.n.text.size()) * 6 + TickerScroll::kGapPixels);
  it.x = std::max(width, right);
  items.push_back(it);
  if (n.cls == 1) alarm = true;
}

void TickerWidget::push(const NewsItem& n, int width, const Ui& ui) {
  Item it; it.n = n;
  int right = 0;
  for (auto& i : items) right = std::max(right, i.x + ui.textWidth(i.n.text) + TickerScroll::kGapPixels);
  it.x = std::max(width, right);
  items.push_back(it);
  if (n.cls == 1) alarm = true;
}

void TickerWidget::update(uint32_t dtMs, int width, const Ui& ui, NewsTicker* t, int32_t today, GameRng* rng, bool paused,
                          uint8_t pauseR, uint8_t pauseG, uint8_t pauseB) {
  accumMs += int(dtMs);
  // 2 px every 50 ms (40 px/s; the original measured about 39): the remainder carries over so 16 ms frames do not stretch the step to 64 ms.
  int px = 0;
  while (accumMs >= TickerScroll::kStepMs) { accumMs -= TickerScroll::kStepMs; px += TickerScroll::kStepPixels; }
  if (px) for (auto& i : items) i.x -= px;
  items.erase(std::remove_if(items.begin(), items.end(), [&](const Item& i) { return i.x + ui.textWidth(i.n.text, "main9") < 0; }), items.end());
  if (paused) {
    // Repeat the paused message across the whole strip at a 132 px period (107 px text + 25 px gap) [OBSERVED].
    static const char* kMsg = "Simulation Paused";
    const int gap = std::max(4, 132 - ui.textWidth(kMsg, "main9"));
    for (int guard = 0; guard < 16; guard++) {
      int right = 0;
      for (auto& i : items) right = std::max(right, i.x + ui.textWidth(i.n.text, "main9") + i.gap);
      if (!items.empty() && right > width) break;
      Item it; it.n.text = kMsg; it.n.cls = 1; it.n.number = 0; it.gap = gap;
      it.n.r = pauseR; it.n.g = pauseG; it.n.b = pauseB;
      it.x = std::max(width, right);
      items.push_back(it);
    }
    return;
  }
  for (int guard = 0; guard < 4 && t && rng; guard++) {
    int right = 0;
    for (auto& i : items) right = std::max(right, i.x + ui.textWidth(i.n.text, "main9") + i.gap);
    if (!items.empty() && right > width) break;
    NewsItem n = t->next(today, *rng);
    if (n.text.empty()) break;
    Item it; it.n = n; it.x = std::max(width, right);
    items.push_back(it);
    if (n.cls == 1) alarm = true;
  }
}

WindowResult TickerWidget::draw(Ui& ui, const WinEnv& E, Rect r) {
  WindowResult R;
  if (alarm) { alarm = false; R.add(Act::PlaySound, 0, 54); }
  std::vector<uint32_t> buf(size_t(r.w) * r.h);
  for (int y = 0; y < r.h; y++)   // the bar art around the strip stays: copy what is there
    for (int x = 0; x < r.w; x++) { const int dx = r.x + x, dy = r.y + y; buf[size_t(y) * r.w + x] = (dx >= 0 && dy >= 0 && dx < ui.W && dy < ui.H) ? ui.at(dx, dy) : 0u; }
  // The strip, relative to the HUD's text line (y549 on the original at 800 x 600): a (99,109,198) top row at rel 0 and 16 pure black rows (y550..565) below it,
  // x7..481, i.e. one column left of the rect and 475 wide [OBSERVED].
  const int sw = std::min(r.w, 474);
  for (int x = 0; x < sw; x++) {
    buf[size_t(x)] = rgb(99, 109, 198);
    for (int y = 1; y <= 16 && y < r.h; y++) buf[size_t(y) * r.w + x] = rgb(0, 0, 0);
  }
  Ui sub = ui; sub.setTarget(&buf, r.w, r.h);
  int fh = ui.fonts->ui().height();
  int ty = (r.h - fh) / 2;
  for (auto& it : items) {
    int w = ui.textWidth(it.n.text, "main9");
    if (it.x > r.w || it.x + w < 0) continue;
    uint32_t col = rgb(it.n.r, it.n.g, it.n.b);
    sub.text(it.x, ty + 1, it.n.text, "main9", col);   // main9 (9-row capitals) [OBSERVED]
    if (it.n.hasLoc) {
      if (14 < r.h) for (int x = std::max(0, it.x); x < std::min(r.w, it.x + w + 1); x++) buf[size_t(14) * r.w + x] = col;   // underline row y563 (rel 14), 1 px past the glyphs [OBSERVED]
      Rect ir{r.x + it.x, r.y, w, r.h};
      if (ui.clicked(ir) && r.contains(ui.in.mx, ui.in.my)) {
        R.add(Act::TickerClick, it.n.number, it.n.locX, it.n.locZ);
        R.add(Act::PlaySound, 0, 47);
      }
      E.spot("ticker." + std::to_string(it.n.number), {std::max(ir.x, r.x), r.y, std::min(w, r.w), r.h});
    }
  }
  for (int y = 0; y < r.h; y++)
    for (int x = 0; x < r.w; x++) {
      int dx = r.x + x, dy = r.y + y;
      if (dx >= 0 && dy >= 0 && dx < ui.W && dy < ui.H) ui.at(dx, dy) = buf[size_t(y) * r.w + x];
    }
  if (r.x >= 1) {   // the strip's extra left column (x7)
    ui.at(r.x - 1, r.y) = rgb(99, 109, 198);
    for (int y = 1; y <= 16; y++) ui.at(r.x - 1, r.y + y) = rgb(0, 0, 0);
  }
  return R;
}

}  // namespace m3k
