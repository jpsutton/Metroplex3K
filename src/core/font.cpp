#include "core/font.h"
#include <random>
#include "core/font_builtin.h"

#include <algorithm>
#include <cstdio>

namespace m3k {

bool Font::load(const uint8_t* p, size_t n) {
  if (n < 16) return false;
  uint32_t ver = rd32(p), w = rd32(p + 4), h = rd32(p + 8), fmt = rd32(p + 12);
  if (ver != 1 || fmt != 1 || w == 0 || h == 0 || w > 4096 || h > 4096) return false;
  size_t need = 16 + size_t(w) * h + 512 + 5120;
  if (n < need) return false;
  w_ = int(w); h_ = int(h);
  atlas_.assign(p + 16, p + 16 + size_t(w) * h);
  for (int i = 0; i < 256; i++) pal_[i] = rd16(p + 16 + size_t(w) * h + i * 2);
  const uint8_t* g = p + 16 + size_t(w) * h + 512;
  for (int i = 0; i < 256; i++) {
    g_[i] = {int(rd32(g + i * 20)), int(rd32(g + i * 20 + 4)), int(rd32(g + i * 20 + 8)), int(rd32(g + i * 20 + 12)), int(rd32(g + i * 20 + 16))};
  }
  glyphH_ = g_['A'].b - g_['A'].t;
  if (glyphH_ <= 0) glyphH_ = g_['0'].b - g_['0'].t;
  // Multi-colour fonts use palette indices beyond 17.
  int mx = 0;
  for (uint8_t v : atlas_) mx = std::max<int>(mx, v);
  multi_ = mx > 17;
  return true;
}

void Font::makeBuiltin(int s) {
  w_ = 256 * 6 * s; h_ = 9 * s; glyphH_ = 9 * s; multi_ = false;
  atlas_.assign(size_t(w_) * h_, 16);
  for (int ch = 0; ch < 256; ch++) {
    const unsigned char* col = builtinGlyphColumns(ch);
    int x0 = ch * 6 * s;
    for (int cx = 0; cx < 5; cx++) for (int ry = 0; ry < 8; ry++)
      if (col[cx] & (1 << ry)) for (int dy = 0; dy < s; dy++) for (int dx = 0; dx < s; dx++)
        atlas_[size_t(ry * s + dy) * w_ + x0 + cx * s + dx] = 1;
    g_[ch] = {x0, 0, x0 + 5 * s, 9 * s, 6 * s};
  }
}

int Font::measure(const std::string& s) const {
  int x = 0, best = 0;
  for (unsigned char c : s) { if (c == '\n') { best = std::max(best, x); x = 0; continue; } x += g_[c].adv; }
  return std::max(best, x);
}

void Font::draw(uint32_t* fb, int fbW, int fbH, int x0, int y0, const std::string& s, uint32_t col, int pitch, bool overlay) const {
  if (!loaded() || !fb) return;
  if (pitch <= 0) pitch = fbW;
  int x = x0, y = y0;
  uint32_t cr = col & 255, cg = (col >> 8) & 255, cb = (col >> 16) & 255;
  for (unsigned char c : s) {
    if (c == '\n') { x = x0; y += glyphH_ + 1; continue; }
    if (c == '\r') continue;
    const Glyph& g = g_[c];
    for (int gy = g.t; gy < g.b; gy++) {
      int dy = y + gy - g.t;
      if (dy < 0 || dy >= fbH || gy < 0 || gy >= h_) continue;
      for (int gx = g.l; gx < g.r; gx++) {
        int dx = x + gx - g.l;
        if (dx < 0 || dx >= fbW || gx < 0 || gx >= w_) continue;
        uint8_t idx = atlas_[size_t(gy) * w_ + gx];
        uint32_t& d = fb[size_t(dy) * pitch + dx];
        if (multi_) {
          uint16_t pv = pal_[idx];
          if (pv == 0xF81F) continue;
          d = expand16(pv, 7);
        } else {
          if (idx < 1 || idx > 15) continue;
          float cov = float(16 - idx) / 15.f;
          if (overlay && (d >> 24) != 255) { d = overlayOver(d, float(cr), float(cg), float(cb), cov); continue; }
          uint32_t dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255;
          dr = uint32_t(dr + (float(cr) - dr) * cov); dg = uint32_t(dg + (float(cg) - dg) * cov); db = uint32_t(db + (float(cb) - db) * cov);
          d = 0xFF000000u | (db << 16) | (dg << 8) | dr;
        }
      }
    }
    x += g.adv;
  }
}

std::string groupDigits(long long v) {
  std::string s = std::to_string(v < 0 ? -v : v), out;
  int n = 0;
  for (size_t i = s.size(); i-- > 0;) { out.insert(out.begin(), s[i]); if (++n % 3 == 0 && i) out.insert(out.begin(), ','); }
  return (v < 0 ? "-" : "") + out;
}

std::string expandTokens(const std::string& s, const TextContext& c) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '%') {
      size_t e = s.find('%', i + 1);
      if (e != std::string::npos && e > i + 1 && e - i <= 14) {
        std::string t = s.substr(i + 1, e - i - 1);
        const char* rep = nullptr; std::string tmp;
        if (t == "MAYOR" || t == "YOURNAME") rep = c.mayor.c_str();
        else if (t == "CITYNAME" || t == "YOURCITY") rep = c.city.c_str();
        else if (t == "POPULATION") { tmp = groupDigits(c.population); rep = tmp.c_str(); }
        else if (t == "YEAR") { tmp = std::to_string(c.year); rep = tmp.c_str(); }
        else if (t == "NEIGHBOR") rep = c.neighbour.c_str();
        else if (t == "ANYNEIGHBOR") {   // uniformly random among the four slots, drawn anew for each expansion (spec 05 3.3 [STATIC])
          static std::mt19937 rng{std::random_device{}()};
          const std::string& n = c.neighbours[rng() % 4];
          rep = n.empty() ? c.neighbour.c_str() : n.c_str();
        }
        if (rep) { out += rep; i = e + 1; continue; }
      }
    }
    out.push_back(s[i++]);
  }
  return out;
}

void Fonts::load(const fs::path& dir) {
  const char* names[] = {"system9", "main9", "main10", "title9", "title12", "serif17", "systemloadsave"};
  for (auto n : names) {
    fs::path p = resolveCI(dir, std::string(n) + ".fbf");
    Bytes b; Font f;
    if (!p.empty() && readFile(p, b) && f.load(b.data(), b.size())) f_[n] = std::move(f);
  }
}

const Font& Fonts::get(const std::string& n) const {
  auto it = f_.find(n);
  if (it != f_.end()) return it->second;
  it = f_.find("main10");
  if (it != f_.end()) return it->second;
  if (!builtin_[0].loaded()) { builtin_[0].makeBuiltin(1); builtin_[1].makeBuiltin(2); }
  bool big = n == "title12" || n == "serif17" || n == "title9";
  return builtin_[big ? 1 : 0];
}

}  // namespace m3k
