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

Rect centered(const Ui& ui, int w, int h) {
  Rect r{(ui.W - w) / 2, (ui.H - h) / 2, w, h};
  ui.windowRects.push_back(r);
  return r;
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
               const char* filter) {
  const uint32_t ink = rgb(0x31, 0x34, 0x7B);
  E.spot(name, fr);
  ui.fill(fr, rgb(255, 255, 255));
  ui.fill({fr.x, fr.y, fr.w, 1}, rgb(0x6B, 0x71, 0xBD)); ui.fill({fr.x, fr.y, 1, fr.h}, rgb(0x6B, 0x71, 0xBD));
  ui.fill({fr.x + 1, fr.y + 1, fr.w - 2, 1}, ink); ui.fill({fr.x + 1, fr.y + 1, 1, fr.h - 2}, ink);
  ui.fill({fr.x + fr.w - 2, fr.y + 1, 1, fr.h - 2}, rgb(0x7B, 0x82, 0xBD));
  if (multiLine) ui.fill({fr.x + 1, fr.y + fr.h - 2, fr.w - 2, 1}, rgb(0x7B, 0x82, 0xBD));
  ui.fill({fr.x + fr.w - 1, fr.y, 1, fr.h}, rgb(0xB5, 0xB6, 0xE7)); ui.fill({fr.x, fr.y + fr.h - 1, fr.w, 1}, rgb(0xB5, 0xB6, 0xE7));
  if (ui.in.pressed) { if (fr.contains(ui.in.mx, ui.in.my)) ui.focusId = id; else if (ui.focusId == id) ui.focusId = 0; }
  bool focus = ui.focusId == id, edited = false;
  if (focus) {
    for (char c : ui.in.text)
      if (c >= 32 && val.size() < maxLen && !(filter && std::strchr(filter, c))) { val.push_back(c); edited = true; }
    if (ui.in.key[K_BACKSPACE] && !val.empty()) { val.pop_back(); edited = true; }
  }
  std::string shown = val + (focus ? "|" : "");
  if (!multiLine) ui.text(fr.x + 4, fr.y + 3, shown, "main9", ink);
  else {
    int y = fr.y + 3;
    for (const auto& l : wrap(ui, shown, fr.w - 8, "main9")) { if (y + 14 > fr.y + fr.h - 2) break; ui.text(fr.x + 4, y, l, "main9", ink); y += 14; }
  }
  return edited;
}

void spriteAt(Ui& ui, GameData* gd, uint32_t cat, uint32_t id, int ax, int ay) {
  if (!gd || !id || !ui.fb) return;
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
      int a = pl ? pl->v[size_t(y) * pl->w + x] * 255 / 31 : int(c >> 24);
      if (!pl && (c & 0xFFFFFF) == 0xFF00FF) a = 0;
      if (a <= 0) continue;
      uint32_t& d = (*ui.fb)[size_t(dy) * ui.W + dx];
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
  if (flora == 0x3FBB0000) put(flora, 546, 90); else put(flora, 554, 133);
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
  title_ = title; message_ = message; style_ = std::max(0, std::min(5, style)); icon_ = icon; active = true;
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
  Rect box = wui::centered(ui, L.boxW, L.boxH);
  const uint32_t ink = rgb(0x31, 0x34, 0x7B);
  bool closeClicked = false;
  if (ui.skin && ui.skin->drawDialogFrame(ui, box, title_)) {
    Rect hit;
    if (ui.skin->dialogCloseBox(ui, box, hit, closeClicked)) E.spot("close", hit);
    // Sunken body panel: dark blue top/left line, light bottom/right line over the cloud body.
    Rect body{box.x + 4, box.y + L.bodyY, box.w - 8, L.bodyH};
    ui.fill({body.x, body.y, body.w, 1}, rgb(0x7B, 0x8A, 0xFF));
    ui.fill({body.x, body.y, 1, body.h}, rgb(0x7B, 0x8A, 0xFF));
    ui.fill({body.x, body.y + body.h - 1, body.w, 1}, rgb(0xFF, 0xF3, 0xFF));
    ui.fill({body.x + body.w - 1, body.y, 1, body.h}, rgb(0xFF, 0xF3, 0xFF));
    // Button band: same blue band with lighter ring as the title bar.
    Rect band{box.x + 4, box.y + L.bandY, box.w - 9, 35};
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
    R.add(Act::MessageResult, 0, int64_t(bs[size_t(idx)]));
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
  Ui sub = ui; sub.fb = &buf; sub.W = view.w; sub.H = view.h;
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
      if (dx >= 0 && dy >= 0 && dx < ui.W && dy < ui.H) (*ui.fb)[size_t(dy) * ui.W + dx] = buf[size_t(y) * view.w + x];
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

void TickerWidget::update(uint32_t dtMs, int width, const Ui& ui, NewsTicker* t, int32_t today, GameRng* rng, bool paused) {
  accumMs += int(dtMs);
  int px = TickerScroll::advance(accumMs);
  if (px) {
    accumMs = 0;
    for (auto& i : items) i.x -= px;
  }
  items.erase(std::remove_if(items.begin(), items.end(), [&](const Item& i) { return i.x + ui.textWidth(i.n.text) < 0; }), items.end());
  if (paused) {
    // Repeat the paused message across the whole strip; 4 glyph widths between copies.
    static const char* kMsg = "Simulation Paused";
    const int gap = ui.textWidth("0000");
    for (int guard = 0; guard < 16; guard++) {
      int right = 0;
      for (auto& i : items) right = std::max(right, i.x + ui.textWidth(i.n.text) + i.gap);
      if (!items.empty() && right > width) break;
      Item it; it.n.text = kMsg; it.n.cls = 6; it.n.number = 0; it.gap = gap;
      it.x = std::max(width, right);
      items.push_back(it);
    }
    return;
  }
  for (int guard = 0; guard < 4 && t && rng; guard++) {
    int right = 0;
    for (auto& i : items) right = std::max(right, i.x + ui.textWidth(i.n.text) + i.gap);
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
  for (int y = 0; y < r.h; y++)
    for (int x = 0; x < r.w; x++) buf[size_t(y) * r.w + x] = rgb(20, 24, 60);
  Ui sub = ui; sub.fb = &buf; sub.W = r.w; sub.H = r.h;
  int fh = ui.fonts->ui().height();
  int ty = (r.h - fh) / 2;
  for (auto& it : items) {
    int w = ui.textWidth(it.n.text);
    if (it.x > r.w || it.x + w < 0) continue;
    uint32_t col = rgb(it.n.r, it.n.g, it.n.b);
    sub.text(it.x, ty, it.n.text, "main10", col);
    if (it.n.hasLoc) {
      if (ty + fh < r.h) for (int x = std::max(0, it.x); x < std::min(r.w, it.x + w); x++) buf[size_t(ty + fh) * r.w + x] = col;
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
      if (dx >= 0 && dy >= 0 && dx < ui.W && dy < ui.H) (*ui.fb)[size_t(dy) * ui.W + dx] = buf[size_t(y) * r.w + x];
    }
  ui.frame(r, false);
  return R;
}

}  // namespace m3k
