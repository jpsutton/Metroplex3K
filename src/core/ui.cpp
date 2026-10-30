#include "core/ui.h"

#include <algorithm>
#include "core/skin.h"

namespace m3k {

void Ui::fill(Rect r, uint32_t c) {
  int x0 = std::max(0, r.x), y0 = std::max(0, r.y), x1 = std::min(W, r.x + r.w), y1 = std::min(H, r.y + r.h);
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) (*fb)[size_t(y) * W + x] = c;
}

void Ui::fillAlpha(Rect r, uint32_t c, float a) {
  int x0 = std::max(0, r.x), y0 = std::max(0, r.y), x1 = std::min(W, r.x + r.w), y1 = std::min(H, r.y + r.h);
  uint32_t cr = c & 255, cg = (c >> 8) & 255, cb = (c >> 16) & 255;
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
    uint32_t& d = (*fb)[size_t(y) * W + x];
    uint32_t dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255;
    d = 0xFF000000u | (uint32_t(db + (float(cb) - db) * a) << 16) | (uint32_t(dg + (float(cg) - dg) * a) << 8) | uint32_t(dr + (float(cr) - dr) * a);
  }
}

void Ui::frame(Rect r, bool raised) {
  uint32_t hi = raised ? panelLight : panelDark, lo = raised ? panelDark : panelLight;
  fill({r.x, r.y, r.w, 1}, hi); fill({r.x, r.y, 1, r.h}, hi);
  fill({r.x, r.y + r.h - 1, r.w, 1}, lo); fill({r.x + r.w - 1, r.y, 1, r.h}, lo);
}

void Ui::blitRect(const Image& im, int dx, int dy, int sx, int sy, int sw, int sh) {
  for (int j = 0; j < sh; j++) {
    int y = dy + j; if (y < 0 || y >= H || sy + j >= im.h) continue;
    for (int i = 0; i < sw; i++) {
      int x = dx + i; if (x < 0 || x >= W || sx + i >= im.w) continue;
      uint32_t p = im.px[size_t(sy + j) * im.w + sx + i];
      if (p >> 24) (*fb)[size_t(y) * W + x] = p;
    }
  }
}

void Ui::panel(Rect r, const std::string& title) {
  if (!title.empty() && skin && skin->drawDialogFrame(*this, r, title)) return;
  fill(r, panelCol);
  frame(r, true);
  if (!title.empty()) {
    fill({r.x + 2, r.y + 2, r.w - 4, 22}, panelDark);
    text(r.x + 8, r.y + 4, title, "title12", rgb(255, 255, 255));
  }
}

void Ui::text(int x, int y, const std::string& s, const char* font, uint32_t col) {
  fonts->get(font).draw(*fb, W, H, x, y, s, col ? col : textCol);
}
int Ui::textWidth(const std::string& s, const char* font) const { return fonts->get(font).measure(s); }
void Ui::textCentered(Rect r, const std::string& s, const char* font, uint32_t col) {
  const Font& f = fonts->get(font);
  text(r.x + (r.w - f.measure(s)) / 2, r.y + (r.h - f.height()) / 2, s, font, col);
}

void Ui::blit(const Image& im, int x, int y, int sx, int sw) {
  if (sw < 0) sw = im.w - sx;
  for (int j = 0; j < im.h; j++) {
    int dy = y + j; if (dy < 0 || dy >= H) continue;
    for (int i = 0; i < sw; i++) {
      int dx = x + i; if (dx < 0 || dx >= W) continue;
      uint32_t p = im.px[size_t(j) * im.w + sx + i];
      if (p >> 24) (*fb)[size_t(dy) * W + dx] = p;
    }
  }
}

bool Ui::button(Rect r, const std::string& label, bool enabled, bool toggled, const char* font) {
  if (skin && skin->get("button")) {
    bool c = false;
    if (skin->button3(*this, skin->get("button"), r, label, enabled, toggled, c, enabled ? textCol : rgb(0x90, 0x94, 0xA0), font)) return c;
  }
  bool hot = enabled && hover(r), down = hot && in.down;
  fill(r, toggled ? panelDark : hot ? rgb(0xDC, 0xE4, 0xF0) : panelCol);
  frame(r, !(down || toggled));
  textCentered({r.x + (down ? 1 : 0), r.y + (down ? 1 : 0), r.w, r.h}, label, font, enabled ? (toggled ? rgb(255, 255, 255) : 0) : rgb(0x90, 0x94, 0xA0));
  return enabled && clicked(r);
}

bool Ui::imageButton(Rect r, const Image& strip, int cells, const std::string& tip, bool toggled) {
  int cw = strip.w / cells;
  bool hot = hover(r), down = hot && in.down;
  int cell = toggled ? 2 : down ? 2 : hot ? 3 : 1;
  if (cell >= cells) cell = 1;
  fill(r, panelCol);
  blit(strip, r.x + (r.w - cw) / 2, r.y + (r.h - strip.h) / 2, cell * cw, cw);
  if (hot) tooltip = tip;
  return clicked(r);
}

bool Ui::checkbox(Rect r, const std::string& label, bool& v) {
  if (skin && skin->get("check")) {
    const Image* cs = skin->get("check");
    int cw = cs->w / 8, ch = cs->h;
    Rect box{r.x, r.y + (r.h - ch) / 2, cw, ch};
    bool hot = hover(r);
    skin->drawCheck(*this, cs, box.x, box.y, v, true, hot, hot && in.down);
    text(r.x + cw + 6, r.y + (r.h - fonts->ui().height()) / 2, label);
    if (clicked(r)) { v = !v; return true; }
    return false;
  }
  Rect box{r.x, r.y + (r.h - 14) / 2, 14, 14};
  fill(box, rgb(255, 255, 255)); frame(box, false);
  if (v) { fill({box.x + 3, box.y + 3, 8, 8}, textCol); }
  text(r.x + 20, r.y + (r.h - fonts->ui().height()) / 2, label);
  if (clicked(r)) { v = !v; return true; }
  return false;
}

bool Ui::slider(Rect r, int& v, int lo, int hi) {
  int id = nextId();
  fill({r.x, r.y + r.h / 2 - 2, r.w, 4}, panelDark);
  if (in.pressed && r.contains(in.mx, in.my)) activeId = id;
  if (activeId == id) {
    if (in.down) { int nv = lo + (in.mx - r.x) * (hi - lo) / std::max(1, r.w); nv = std::max(lo, std::min(hi, nv)); if (nv != v) { v = nv; return true; } }
    else activeId = 0;
  }
  int kx = r.x + (v - lo) * (r.w - 10) / std::max(1, hi - lo);
  Rect k{kx, r.y, 10, r.h};
  fill(k, panelCol); frame(k, true);
  return false;
}

bool Ui::list(Rect r, const std::vector<std::string>& rows, int& sel, int& scroll) {
  fill(r, rgb(255, 255, 255)); frame(r, false);
  int rh = fonts->ui().height() + 4;
  int vis = std::max(1, (r.h - 4) / rh);
  if (hover(r) && in.wheel) scroll -= in.wheel;
  scroll = std::max(0, std::min(scroll, std::max(0, int(rows.size()) - vis)));
  bool changed = false;
  for (int i = 0; i < vis && scroll + i < int(rows.size()); i++) {
    Rect rr{r.x + 2, r.y + 2 + i * rh, r.w - 4, rh};
    int idx = scroll + i;
    if (idx == sel) fill(rr, rgb(0x90, 0xA8, 0xE0));
    text(rr.x + 3, rr.y + 2, rows[idx]);
    if (clicked(rr) && sel != idx) { sel = idx; changed = true; }
  }
  if (focusId == 0) {
    if (in.key[K_DOWN] && sel + 1 < int(rows.size())) { sel++; changed = true; if (sel >= scroll + vis) scroll++; }
    if (in.key[K_UP] && sel > 0) { sel--; changed = true; if (sel < scroll) scroll--; }
  }
  return changed;
}

bool Ui::textBox(Rect r, std::string& s, size_t maxLen) {
  int id = nextId();
  if (in.pressed) { if (r.contains(in.mx, in.my)) focusId = id; else if (focusId == id) focusId = 0; }
  fill(r, rgb(255, 255, 255)); frame(r, false);
  bool edited = false;
  if (focusId == id) {
    for (char c : in.text) if (c >= 32 && s.size() < maxLen) { s.push_back(c); edited = true; }
    if (in.key[K_BACKSPACE] && !s.empty()) { s.pop_back(); edited = true; }
  }
  text(r.x + 4, r.y + (r.h - fonts->ui().height()) / 2, s + (focusId == id ? "|" : ""));
  return edited;
}

}  // namespace m3k
