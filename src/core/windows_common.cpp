#include <algorithm>
#include <cmath>
#include "core/windows.h"

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

Rect centered(const Ui& ui, int w, int h) { return {(ui.W - w) / 2, (ui.H - h) / 2, w, h}; }

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

void MessageBox::open(const std::string& title, const std::string& message, int style) {
  title_ = title; message_ = message; style_ = std::max(0, std::min(5, style)); active = true;
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

static uint32_t msgButtonColour(MsgButton b) {
  switch (b) {
    case MsgButton::Abort: case MsgButton::Cancel: case MsgButton::No: return rgb(192, 0, 0);
    case MsgButton::Ignore: return rgb(192, 192, 192);
    default: return rgb(0, 192, 0);
  }
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
  L.buttonY = H + 24 + (title_.empty() ? 0 : std::max(lh, ui.fonts->get("title12").height() + 2));
  L.buttonX0 = (W - L.base) / 2 + 8;
  return L;
}

WindowResult MessageBox::draw(Ui& ui, const WinEnv& E) {
  WindowResult R;
  if (!active) return R;
  Layout L = layout(ui);
  int lh = wui::lineHeight(ui);
  int titleBar = 24;
  Rect box = wui::centered(ui, L.W + 16, titleBar + L.buttonY + 24 + 12);
  ui.fillAlpha({0, 0, ui.W, ui.H}, rgb(0, 0, 0), 0.35f);   // modal dim
  ui.panel(box, "");
  int cx = box.x + 8, cy = box.y + titleBar;
  int ty = cy;
  if (!title_.empty()) { ui.text(cx, ty, title_, "title12"); ty += std::max(lh, ui.fonts->get("title12").height() + 2); }
  for (auto& l : L.lines) { ui.text(cx, ty + 4, l); ty += lh; }
  auto bs = buttonsFor(style_);
  int idx = -1;
  for (size_t i = 0; i < bs.size(); i++) {
    Rect b{cx + L.buttonX0 - 8 + int(i) * 108, cy + L.buttonY, 100, 24};
    if (wui::colourButton(ui, E, "msg." + std::to_string(i), b, msgButtonCaption(E, bs[i]), msgButtonColour(bs[i]))) idx = int(i);
  }
  if (ui.in.key[K_ENTER]) idx = 0;
  if (ui.in.key[K_ESC]) idx = int(bs.size()) - 1;
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
WindowResult CreditsWindow::draw(Ui& ui, const WinEnv& E) {
  WindowResult R;
  if (lines.empty()) {
    for (uint32_t id = 1; id <= 378; id++) {
      std::string s = E.text(kTblCredits, id, "");
      if (!s.empty()) lines.push_back(s);
    }
    if (lines.empty()) lines = {"Metroplex3K", "", "A clean-room SimCity 3000 Unlimited compatible engine", "", "Original game by Maxis", "Thanks for playing"};
  }
  Rect r = wui::centered(ui, 400, 320);
  ui.fill({0, 0, ui.W, ui.H}, rgb(8, 10, 30));
  if (const Image* bg = E.img(0x82B9B75C, 0x22729940)) ui.blit(*bg, r.x + (r.w - bg->w) / 2, r.y - 4);
  Rect view{r.x + 20, r.y + 60, r.w - 40, r.h - 80};
  // Clip by rendering into a sub-buffer.
  std::vector<uint32_t> buf(size_t(view.w) * view.h, rgb(8, 10, 30));
  Ui sub = ui; sub.fb = &buf; sub.W = view.w; sub.H = view.h;
  int lh = wui::lineHeight(ui) + 2;
  double total = double(lines.size()) * lh + view.h;
  if (scroll > total) scroll = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    int y = int(view.h - scroll + double(i) * lh);
    if (y < -lh || y > view.h) continue;
    sub.text((view.w - ui.textWidth(lines[i])) / 2, y, lines[i], "main10", rgb(230, 235, 255));
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

void TickerWidget::update(uint32_t dtMs, int width, const Ui& ui, NewsTicker* t, int32_t today, GameRng* rng) {
  accumMs += int(dtMs);
  int px = TickerScroll::advance(accumMs);
  if (px) {
    accumMs = 0;
    for (auto& i : items) i.x -= px;
  }
  items.erase(std::remove_if(items.begin(), items.end(), [&](const Item& i) { return i.x + ui.textWidth(i.n.text) < 0; }), items.end());
  for (int guard = 0; guard < 4 && t && rng; guard++) {
    int right = 0;
    for (auto& i : items) right = std::max(right, i.x + ui.textWidth(i.n.text) + TickerScroll::kGapPixels);
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
