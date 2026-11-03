#include "core/render.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include "core/workpool.h"

namespace m3k {

// ======================================================================================
// Coordinate helpers
// ======================================================================================

void Renderer::viewToWorldVertex(int rot, int W, int H, int u, int v, int& x, int& z) {
  switch (rot & 3) {
    case 0: x = u; z = v; break;
    case 1: x = v; z = W - u; break;
    case 2: x = W - u; z = H - v; break;
    default: x = H - v; z = u; break;
  }
}
void Renderer::worldToViewVertex(int rot, int W, int H, int x, int z, int& u, int& v) {
  switch (rot & 3) {
    case 0: u = x; v = z; break;
    case 1: u = W - z; v = x; break;
    case 2: u = W - x; v = H - z; break;
    default: u = z; v = H - x; break;
  }
}
void Renderer::worldToViewSub(int rot, int W, int H, int32_t x, int32_t z, int32_t& a, int32_t& b) {
  const int32_t mw = W * 256 - 1, mh = H * 256 - 1;
  switch (rot & 3) {
    case 0: a = x; b = z; break;
    case 1: a = mw - z; b = x; break;
    case 2: a = mw - x; b = mh - z; break;
    default: a = z; b = mh - x; break;
  }
}
void Renderer::viewToWorldSub(int rot, int W, int H, int32_t a, int32_t b, int32_t& x, int32_t& z) {
  const int32_t mw = W * 256 - 1, mh = H * 256 - 1;
  switch (rot & 3) {
    case 0: x = a; z = b; break;
    case 1: x = b; z = mw - a; break;
    case 2: x = mw - a; z = mh - b; break;
    default: x = mh - b; z = a; break;
  }
}

// ======================================================================================
// Pure helpers (exposed for tests)
// ======================================================================================

void farZoomAdjust(int zoom, int& r, int& g, int& b) {
  if (zoom >= 3 || zoom < 0) return;
  static const int K[3][3] = {{49, 51, 53}, {33, 34, 35}, {16, 17, 18}};
  int* ch[3] = {&r, &g, &b};
  for (int i = 0; i < 3; i++) {
    int c = *ch[i];
    int S = (c + 5) / 10;
    c = c + K[zoom][i] - S * (3 - zoom);
    *ch[i] = std::max(0, std::min(255, c));
  }
}

int blend5(int s, int d, int alpha) {
  int v = (32 * d + alpha * (s - d)) >> 5;
  return std::min(31, std::max(0, v));
}
int blend8(int s, int d, int alpha) {
  int v = (32 * d + alpha * (s - d)) >> 5;
  return std::min(255, std::max(0, v));
}

Image expandPalBasic(const Image& raw) {
  Image out;
  const int n = int(raw.px.size());
  if (n == 0) return out;
  out.w = 32; out.h = n; out.px.resize(size_t(32) * n);
  const float PI = 3.14159265358979f;
  for (int row = 0; row < n; row++) {
    uint32_t c = raw.px[row];
    int cr = c & 255, cg = (c >> 8) & 255, cb = (c >> 16) & 255;
    for (int k = 0; k < 32; k++) {
      float f = std::cos(float(31 - k) * PI / 62.0f);
      int r = int(float(cr) * f), g = int(float(cg) * f), b = int(float(cb) * f);
      if (k == 31) { r = cr; g = cg; b = cb; }
      out.px[size_t(row) * 32 + k] = rgba(uint8_t(r), uint8_t(g), uint8_t(b));
    }
  }
  return out;
}

std::vector<uint8_t> computeVertexShade(const World& w) {
  std::vector<uint8_t> shade(size_t(w.W + 1) * (w.H + 1), 15);
  if (w.alt.size() < shade.size()) return shade;
  auto h = [&](int x, int z) { return float(w.altAt(x, z)); };
  const float s = w.heightScale, d = 16.0f;
  const float inv = 1.0f / std::sqrt(2.0f);
  const float PI = 3.14159265f;
  for (int x = 0; x <= w.W; x++)
    for (int z = 0; z <= w.H; z++) {
      float c = h(x, z);
      auto nb = [&](int nx, int nz, int ox, int oz) {
        if (nx >= 0 && nz >= 0 && nx <= w.W && nz <= w.H) return h(nx, nz);
        return std::min(255.f, std::max(0.f, 2 * c - h(ox, oz)));
      };
      float xm = nb(x - 1, z, x + 1, z), xp = nb(x + 1, z, x - 1, z);
      float zm = nb(x, z - 1, x, z + 1), zp = nb(x, z + 1, x, z - 1);
      float nx = s * (xm - xp) / 2, ny = d, nz = s * (zm - zp) / 2;
      float len = std::sqrt(nx * nx + ny * ny + nz * nz);
      nx /= len; ny /= len; nz /= len;
      float d1 = std::max(0.f, nx * -inv + ny * inv), d2 = std::max(0.f, nx * inv + ny * inv);
      int t1 = int(std::acos(std::min(1.f, d1)) * 62.f / PI), t2 = int(std::acos(std::min(1.f, d2)) * 62.f / PI);
      shade[size_t(w.vIdx(x, z))] = uint8_t(std::min(31, std::max(t1, t2)));
    }
  return shade;
}

DataOverlay makeDataOverlay(DataLayer layer, const std::vector<uint8_t>& values, int W, int H) {
  DataOverlay o;
  bool categorical = layer == DataLayer::Zones || layer == DataLayer::Power || layer == DataLayer::Water;
  switch (layer) {
    // spec 25 9.2 [OBSERVED]: land value, density, power and aura use data1Pal; crime, pollution,
    // flammability (fire) and traffic use data2Pal. (Aura's negative half on data2Pal is not split out.)
    case DataLayer::Zones: case DataLayer::Power: case DataLayer::Water:
    case DataLayer::LandValue: case DataLayer::Density: case DataLayer::Aura: o.slot = 1; break;
    case DataLayer::Crime: case DataLayer::Fire: case DataLayer::Pollution: case DataLayer::Traffic: o.slot = 2; break;
    default: o.slot = 3; break;
  }
  o.row.assign(size_t(W) * H, 0xFF);
  for (size_t i = 0; i < o.row.size() && i < values.size(); i++) {
    int v = values[i];
    if (categorical) o.row[i] = v == 0xFF ? 0xFF : uint8_t(std::min(31, v * 4));
    else o.row[i] = uint8_t(31 - std::min(31, v / 8));  // low values on the grey bottom row [INFERRED]
  }
  return o;
}

DataOverlay makeZoneOverlay(const World& w) {
  std::vector<uint8_t> v(size_t(w.W) * w.H, 0xFF);
  for (size_t i = 0; i < v.size() && i < w.zone.size(); i++) {
    int zt = w.zone[i];
    if (!zt) continue;
    v[i] = (zt >= 1 && zt <= 3) ? 0 : (zt >= 5 && zt <= 7) ? 1 : (zt >= 9 && zt <= 11) ? 2 : 3;
  }
  return makeDataOverlay(DataLayer::Zones, v, w.W, w.H);
}

// ======================================================================================
// Setup, noise, palettes
// ======================================================================================

void Renderer::setNoiseSeed(uint32_t seed) {
  terrainLayerValid_ = false;
  uint32_t s = seed ? seed : 0x9E3779B9u;
  auto rnd = [&]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
  for (auto& n : noiseLand_) n = int8_t(int(rnd() % 3) * 8 - 8);
  int i = 0;
  while (i < 1024) {
    int run = 1 + int(rnd() % 8);
    int val = (rnd() & 1) ? 8 : -9;
    for (int k = 0; k < run && i < 1024; k++) noiseEdge_[i++] = int8_t(val);
    run = 1 + int(rnd() % 8);
    for (int k = 0; k < run && i < 1024; k++) noiseEdge_[i++] = 0;
  }
  noiseInit_ = true;
}

void Renderer::setSubstituteSprite(int footprint, uint32_t category, uint32_t spriteId) {
  if (footprint < 0 || footprint > 16) return;
  ++itemsGen_; itemCacheValid_ = false;
  if (int(subst_.size()) <= footprint) subst_.resize(footprint + 1, Subst{0xFFFFFFFFu, 0});
  subst_[footprint] = {category, spriteId};
}

void Renderer::setTileTint(int x, int z, uint8_t row) {
  if (!world_ || !world_->inTiles(x, z)) return;
  if (tint_.size() != size_t(world_->W) * world_->H) tint_.assign(size_t(world_->W) * world_->H, 0);
  tint_[world_->tIdx(x, z)] = row;
}

void Renderer::setWorld(const World* w, GameData* data) {
  world_ = w; data_ = data; shadeValid_ = false; itemsValid_ = false; terrainLayerValid_ = false; itemCacheValid_ = false; ++itemsGen_;
  tint_.clear(); siteCache_.clear(); bldIniTried_ = false; bldIniOk_ = false;
  for (int i = 0; i < 5; i++) { back_[i] = Image(); backTried_[i] = false; }
  pal_ = TerrainPalettes(); palOverride_ = false;
  makeFallbackPalettes();
  loadPalettes();
  if (!noiseInit_) setNoiseSeed(0x6D33B1u);
}

void Renderer::resize(int w, int h) { fbW_ = w; fbH_ = h; fb_.assign(size_t(w) * h, 0xFF000000u); terrainLayerValid_ = false; }

namespace {

Image makeImage(int w, int h) { Image i; i.w = w; i.h = h; i.px.assign(size_t(w) * h, 0xFF000000u); return i; }

uint32_t mix8(float r, float g, float b) {
  auto c = [](float f) { return uint8_t(std::max(0.f, std::min(255.f, f))); };
  return rgba(c(r), c(g), c(b));
}

void hsv(float h, float s, float v, float& r, float& g, float& b) {
  h = h - std::floor(h);
  float c = v * s, hp = h * 6.f, x = c * (1 - std::fabs(std::fmod(hp, 2.f) - 1)), m = v - c;
  float rr = 0, gg = 0, bb = 0;
  int k = int(hp);
  switch (k) { case 0: rr = c; gg = x; break; case 1: rr = x; gg = c; break; case 2: gg = c; bb = x; break;
    case 3: gg = x; bb = c; break; case 4: rr = x; bb = c; break; default: rr = c; bb = x; break; }
  r = (rr + m) * 255; g = (gg + m) * 255; b = (bb + m) * 255;
}

inline void fetch(const Image& im, int row, int col, int& r, int& g, int& b) {
  if (im.empty()) { r = g = b = 128; return; }
  row = std::max(0, std::min(im.h - 1, row)); col = std::max(0, std::min(im.w - 1, col));
  uint32_t c = im.px[size_t(row) * im.w + col];
  r = c & 255; g = (c >> 8) & 255; b = (c >> 16) & 255;
}

}  // namespace

void Renderer::makeFallbackPalettes() {
  TerrainPalettes& p = pal_;
  auto lit = [](int col) { return 0.6f + 0.4f * std::min(1.f, float(col) / 15.f); };
  for (int pass = 0; pass < 2; pass++) {
    Image& im = pass == 0 ? p.land : p.under;
    im = makeImage(32, 512);
    for (int row = 0; row < 512; row++)
      for (int col = 0; col < 32; col++) {
        float L = lit(col), r, g, b;
        if (row < 256) {  // above water, top-down: 255 = shore ... 0 = peak
          float hgt = float(255 - row) / 255.f;
          if (pass == 0) { r = (60 + 170 * hgt) * L; g = (130 + 90 * hgt) * L; b = (50 + 180 * hgt) * L; }
          else { r = (70 + 60 * hgt) * L; g = (60 + 50 * hgt) * L; b = (50 + 40 * hgt) * L; }
        } else {
          float d = std::min(1.f, float(row - 256) / 120.f);
          if (pass == 0) { r = (90 * (1 - d) + 20) * L; g = (120 * (1 - d) + 30) * L; b = (130 * (1 - d) + 60) * L; }
          else { r = (60 * (1 - d) + 25) * L; g = (50 * (1 - d) + 20) * L; b = (45 * (1 - d) + 20) * L; }
        }
        im.px[size_t(row) * 32 + col] = mix8(r, g, b);
      }
  }
  p.water = makeImage(1, 256);
  for (int d = 0; d < 256; d++) { float f = 1.f - std::min(1.f, d / 120.f); p.water.px[d] = mix8(40 + 30 * f, 90 + 60 * f, 150 + 70 * f); }
  for (int k = 0; k < 2; k++) {
    Image& im = k == 0 ? p.edgeL : p.edgeD;
    im = makeImage(1, 512);
    float sc = k == 0 ? 1.f : 0.7f;
    for (int i = 0; i < 512; i++) {
      if (i < 256) { float f = 1.f - std::min(1.f, i / 200.f); im.px[i] = mix8((30 + 30 * f) * sc, (70 + 60 * f) * sc, (130 + 70 * f) * sc); }
      else { float f = 1.f - std::min(1.f, (i - 256) / 255.f); im.px[i] = mix8((150 * f + 40) * sc, (110 * f + 30) * sc, (70 * f + 20) * sc); }
    }
  }
  for (int s = 0; s < 3; s++) {
    p.data[s] = makeImage(64, 32);
    for (int row = 0; row < 32; row++)
      for (int col = 0; col < 64; col++) {
        float r, g, b, L = 0.5f + 0.5f * float(std::min(col, 31)) / 31.f;
        if (s == 0) hsv(float(row % 8) / 8.f, 0.8f, 1.f, r, g, b);                        // categorical hues
        else if (s == 1) hsv(0.66f - 0.66f * float(row) / 31.f, 0.9f, 1.f, r, g, b);      // blue -> red
        else hsv(0.33f - 0.33f * float(row) / 31.f, 0.85f, 1.f, r, g, b);                 // green -> red
        p.data[s].px[size_t(row) * 64 + col] = mix8(r * L, g * L, b * L);
      }
  }
  Image raw = makeImage(1, 53);
  for (int i = 0; i < 53; i++) {
    float r, g, b;
    hsv(float(i) / 51.f, 0.6f, 0.9f, r, g, b);
    raw.px[i] = mix8(r, g, b);
  }
  raw.px[51] = rgba(250, 240, 160);
  raw.px[52] = rgba(64, 64, 64);
  p.basic = expandPalBasic(raw);
}

void Renderer::loadPalettes() {
  if (!data_) return;
  auto loadBmp = [&](const char* name, Image& out) {
    fs::path wp = resolveCI(data_->appsDir(), std::string("Res/Dirt/Palette/") + name);
    if (wp.empty()) return false;
    Bytes b;
    if (!readFile(wp, b)) return false;
    try { out = decodeBmp(b.data(), b.size(), false); } catch (const FormatError&) { return false; }
    return true;
  };
  // Landscape -> palette instance from SC3CityScheme.ini [LandScapes] (4th field).
  uint32_t inst = 1;
  IniFile ini;
  if (world_ && data_->ini().load("SC3CityScheme.ini", ini)) {
    for (const auto& kv : ini.section("LandScapes")) {
      if (parseIdNumber(kv.first) == world_->scheme.landscape) {
        std::vector<std::string> f; std::string cur;
        for (char c : kv.second) { if (c == ',') { f.push_back(trimSpaces(cur)); cur.clear(); } else cur.push_back(c); }
        f.push_back(trimSpaces(cur));
        if (f.size() >= 4) inst = parseIdNumber(f[3]);
        break;
      }
    }
  }
  ResourceRef r = data_->db().find({T_UI_IMAGE, 0x455A72B1, inst});
  if (r) {
    try {
      Image im = decodeUiImage(r.data, r.available, false);
      if (im.w >= 32 && im.h >= 512) { pal_.land = im; pal_.fromGame = true; }
    } catch (const FormatError&) {}
  }
  Image im;
  if (loadBmp("palBasic.bmp", im)) pal_.basic = expandPalBasic(im);
  const char* dn[3] = {"data1Pal.bmp", "data2Pal.bmp", "data3Pal.bmp"};
  for (int i = 0; i < 3; i++) if (loadBmp(dn[i], im)) pal_.data[i] = im;
  if (loadBmp("palUnder.bmp", im)) pal_.under = im;
  if (loadBmp("palWater.bmp", im)) pal_.water = im;
  if (loadBmp("palEdgeL.bmp", im)) pal_.edgeL = im;
  if (loadBmp("palEdgeD.bmp", im)) pal_.edgeD = im;
}

void Renderer::ensureShade() {
  if (shadeValid_ || !world_) return;
  shade_ = computeVertexShade(*world_);
  maxAlt_ = 0;
  for (uint8_t a : world_->alt) maxAlt_ = std::max<int>(maxAlt_, a);
  shadeValid_ = true;
}

uint8_t Renderer::shadeAt(int x, int z) {
  if (!world_) return 15;
  ensureShade();
  return shade_[world_->vIdx(x, z)];
}

void Renderer::landVertexColour(int x, int z, int zoom, bool underground, int& r, int& g, int& b) {
  ensureShade();
  const World& w = *world_;
  int row = 256 + w.waterAt(x, z) - w.altAt(x, z);
  int col = shade_[w.vIdx(x, z)] / 2;
  fetch(underground ? pal_.under : pal_.land, row, col, r, g, b);
  farZoomAdjust(zoom, r, g, b);
}

// ======================================================================================
// Rasteriser
// ======================================================================================

namespace {

// Scan-line triangle walker. Rows are sampled at integer y in [yLo, yHi), spans are [xa, xe).
// Edges are always interpolated from the upper (then left) endpoint, so triangles that share an
// edge produce identical crossings.
template <class F>
void rasterTri(int yLo, int yHi, float x0, float y0, const float* a0, float x1, float y1, const float* a1,
               float x2, float y2, const float* a2, F&& span) {
  struct P { float x, y; const float* a; };
  P v[3] = {{x0, y0, a0}, {x1, y1, a1}, {x2, y2, a2}};
  std::sort(v, v + 3, [](const P& p, const P& q) { return p.y < q.y || (p.y == q.y && p.x < q.x); });
  int ys = std::max(yLo, int(std::ceil(v[0].y))), ye = std::min(yHi, int(std::ceil(v[2].y)));
  auto cross = [](const P& p, const P& q, float y, float* out) {
    float t = (q.y == p.y) ? 0.f : (y - p.y) / (q.y - p.y);
    out[0] = p.x + (q.x - p.x) * t;
    for (int i = 0; i < 3; i++) out[1 + i] = p.a[i] + (q.a[i] - p.a[i]) * t;
  };
  for (int y = ys; y < ye; y++) {
    float fy = float(y), L[4], R[4];
    cross(v[0], v[2], fy, L);
    if (fy < v[1].y) cross(v[0], v[1], fy, R); else cross(v[1], v[2], fy, R);
    if (R[0] < L[0]) for (int i = 0; i < 4; i++) std::swap(L[i], R[i]);
    int xa = int(std::ceil(L[0])), xe = int(std::ceil(R[0]));
    if (xe <= xa) continue;
    span(y, xa, xe, L + 1, R + 1);
  }
}

// Drawing target. Primitives only touch rows [Y0, H): a render band (see Renderer::render).
struct Ctx {
  uint32_t* fb; int W, H, zoom;
  const int8_t* noiseL; const int8_t* noiseE;
  int Y0 = 0;
  int XS = 0, XE = 1 << 30;   // columns [XS, XE) may be written (strip redraws for the scrolling terrain cache)
};

enum class NoiseMode { None, Mono, Rgb };

inline int seedFor(int tx, int tz) { return ((-169 * tz - 327 * tx) % 1024 + 1024) % 1024; }
inline uint32_t pack(int r, int g, int b) { return 0xFF000000u | (uint32_t(b & 255) << 16) | (uint32_t(g & 255) << 8) | uint32_t(r & 255); }
inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

// Gouraud triangle with spec 6.3 noise.
void gouraudTri(const Ctx& c, const float* p0, const float* p1, const float* p2, int seed, int ytop, NoiseMode nm) {
  // p = {x, y, r, g, b}
  rasterTri(c.Y0, c.H, p0[0], p0[1], p0 + 2, p1[0], p1[1], p1 + 2, p2[0], p2[1], p2 + 2,
            [&](int y, int xa, int xe, const float* L, const float* R) {
    int n = xe - xa;
    float inv = 1.f / float(n + 1);
    // 16.16 fixed-point colour steps — same formula as the Phase 2 compute span fill (bit-exact CPU/GPU).
    const int32_t L0 = int32_t(L[0] * 65536.f), L1 = int32_t(L[1] * 65536.f), L2 = int32_t(L[2] * 65536.f);
    const int32_t d0 = int32_t((R[0] - L[0]) * inv * 65536.f);
    const int32_t d1 = int32_t((R[1] - L[1]) * inv * 65536.f);
    const int32_t d2 = int32_t((R[2] - L[2]) * inv * 65536.f);
    int ni = ((seed + 143 * (y - ytop)) % 1024 + 1024) % 1024;
    int x0 = std::max(xa, std::max(0, c.XS)), x1 = std::min(xe, std::min(c.XE, c.W));
    for (int x = x0; x < x1; x++) {
      int i = x - xa;
      int r = int((L0 + d0 * i) / 65536), g = int((L1 + d1 * i) / 65536), b = int((L2 + d2 * i) / 65536);
      if (nm == NoiseMode::Rgb) { r += c.noiseL[(ni + 3 * i) & 1023]; g += c.noiseL[(ni + 3 * i + 1) & 1023]; b += c.noiseL[(ni + 3 * i + 2) & 1023]; }
      else if (nm == NoiseMode::Mono) { int nz = c.noiseE[(ni + i) & 1023]; r += nz; g += nz; b += nz; }
      else { r = clamp255(r); g = clamp255(g); b = clamp255(b); }
      c.fb[size_t(y) * c.W + x] = pack(r, g, b);
    }
  });
}

// Shoreline colour-key fill (spec 6.4): key colour pixels become water colour R.
void shoreTri(const Ctx& c, const float* p0, const float* p1, const float* p2, int seed, int ytop, const int key[3], const int rep[3]) {
  rasterTri(c.Y0, c.H, p0[0], p0[1], p0 + 2, p1[0], p1[1], p1 + 2, p2[0], p2[1], p2 + 2,
            [&](int y, int xa, int xe, const float* L, const float* R) {
    int n = xe - xa;
    float inv = 1.f / float(n + 1);
    const int32_t L0 = int32_t(L[0] * 65536.f), L1 = int32_t(L[1] * 65536.f), L2 = int32_t(L[2] * 65536.f);
    const int32_t d0 = int32_t((R[0] - L[0]) * inv * 65536.f);
    const int32_t d1 = int32_t((R[1] - L[1]) * inv * 65536.f);
    const int32_t d2 = int32_t((R[2] - L[2]) * inv * 65536.f);
    int ni = ((seed + 143 * (y - ytop)) % 1024 + 1024) % 1024;
    int x0 = std::max(xa, std::max(0, c.XS)), x1 = std::min(xe, std::min(c.XE, c.W));
    for (int x = x0; x < x1; x++) {
      int i = x - xa;
      int P[3] = {int((L0 + d0 * i) / 65536), int((L1 + d1 * i) / 65536), int((L2 + d2 * i) / 65536)};
      int d2sq = 0;
      for (int k = 0; k < 3; k++) { int d = P[k] - key[k]; d2sq += d * d; }
      int o[3];
      if (d2sq <= 256) {
        int nz = c.noiseE[(ni + 3 * i) & 1023];
        for (int k = 0; k < 3; k++) o[k] = rep[k] + nz;
      } else {
        for (int k = 0; k < 3; k++) {
          int v = d2sq < 1024 ? (rep[k] * (1024 - d2sq) + P[k] * (d2sq - 256)) / 768 : P[k];
          o[k] = v + c.noiseL[(ni + 3 * i + k) & 1023];
        }
      }
      c.fb[size_t(y) * c.W + x] = pack(o[0], o[1], o[2]);
    }
  });
}

// Palette-index Gouraud (map-edge faces).
void indexTri(const Ctx& c, const Image& tbl, const float* p0, const float* p1, const float* p2, int seed, int ytop, bool mono) {
  // p = {x, y, idx, 0, 0}
  rasterTri(c.Y0, c.H, p0[0], p0[1], p0 + 2, p1[0], p1[1], p1 + 2, p2[0], p2[1], p2 + 2,
            [&](int y, int xa, int xe, const float* L, const float* R) {
    int n = xe - xa;
    float step = (R[0] - L[0]) / float(n + 1);
    int ni = ((seed + 143 * (y - ytop)) % 1024 + 1024) % 1024;
    int x0 = std::max(xa, std::max(0, c.XS)), x1 = std::min(xe, std::min(c.XE, c.W));
    for (int x = x0; x < x1; x++) {
      int i = x - xa;
      int idx = std::max(0, std::min(511, int(L[0] + step * i)));
      int r, g, b;
      fetch(tbl, idx, 0, r, g, b);
      farZoomAdjust(c.zoom, r, g, b);
      if (mono) { int nz = c.noiseE[(ni + i) & 1023]; r += nz; g += nz; b += nz; }
      c.fb[size_t(y) * c.W + x] = pack(r, g, b);
    }
  });
}

// Constant-colour alpha blend triangle (previews).
void blendTri(const Ctx& c, const float* p0, const float* p1, const float* p2, int r, int g, int b, int alpha) {
  float z3[3] = {0, 0, 0};
  rasterTri(c.Y0, c.H, p0[0], p0[1], z3, p1[0], p1[1], z3, p2[0], p2[1], z3,
            [&](int y, int xa, int xe, const float*, const float*) {
    int x0 = std::max(xa, std::max(0, c.XS)), x1 = std::min(xe, std::min(c.XE, c.W));
    for (int x = x0; x < x1; x++) {
      uint32_t& d = c.fb[size_t(y) * c.W + x];
      int dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255;
      d = pack(dr + (r - dr) * alpha / 255, dg + (g - dg) * alpha / 255, db + (b - db) * alpha / 255);
    }
  });
}

void darken(const Ctx& c, int x, int y, int delta) {
  if (x < std::max(0, c.XS) || y < c.Y0 || x >= std::min(c.XE, c.W) || y >= c.H) return;
  uint32_t& p = c.fb[size_t(y) * c.W + x];
  p = pack(clamp255(int(p & 255) + delta), clamp255(int((p >> 8) & 255) + delta), clamp255(int((p >> 16) & 255) + delta));
}

// Grid delta on one polygon edge (spec 6.3 step 5): first |dx/dy|+1 pixels of each span.
void gridEdge(const Ctx& c, float ax, float ay, float bx, float by, bool leftEdge, int delta) {
  if (by < ay) { std::swap(ax, bx); std::swap(ay, by); }
  if (by == ay) return;
  int n = int(std::fabs((bx - ax) / (by - ay))) + 1;
  for (int y = std::max(c.Y0, int(std::ceil(ay))); y < std::min(c.H, int(std::ceil(by))); y++) {
    float t = (float(y) - ay) / (by - ay);
    int x = int(std::ceil(ax + (bx - ax) * t));
    if (leftEdge) for (int k = 0; k < n; k++) darken(c, x + k, y, delta);
    else for (int k = 1; k <= n; k++) darken(c, x - k, y, delta);
  }
}

}  // namespace

// ======================================================================================
// Projection helpers
// ======================================================================================

void Renderer::computeOrigin(const Camera& cam) {
  if (!world_) return;
  curZoom_ = cam.zoom; curRot_ = cam.rot;
  const float Wt = 8.f * float(1 << cam.zoom), Ht = Wt / 2;
  originX_ = fbW_ / 2 - int((cam.v - cam.u) * (Wt / 2));
  int cx, cz;
  viewToWorldVertex(cam.rot, world_->W, world_->H, std::max(0, std::min(world_->W, int(cam.u))), std::max(0, std::min(world_->H, int(cam.v))), cx, cz);
  cx = std::max(0, std::min(world_->W, cx)); cz = std::max(0, std::min(world_->H, cz));
  originY_ = fbH_ / 2 - int((cam.u + cam.v) * (Ht / 2)) + int(world_->altAt(cx, cz) * float(1 << cam.zoom));
  if (cam.zoom == 4) { originX_ &= ~1; originY_ &= ~1; }  // spec 2.1: even scroll origin at zoom 4
}

void Renderer::screenToView(const Camera& cam, int sx, int sy, float& u, float& v) const {
  const float Wt = 8.f * float(1 << cam.zoom), Ht = Wt / 2;
  float px = float(sx - originX_), py = float(sy - originY_);
  u = (py / (Ht / 2) - px / (Wt / 2)) / 2;
  v = (py / (Ht / 2) + px / (Wt / 2)) / 2;
}

void Renderer::projectSub(const Camera& cam, int32_t x, int32_t z, int32_t height, int& sx, int& sy) {
  computeOrigin(cam);
  int32_t a, b;
  worldToViewSub(cam.rot, world_->W, world_->H, x, z, a, b);
  const int Wh = 4 << cam.zoom, Hh = 2 << cam.zoom, A = 1 << cam.zoom;
  sx = originX_ + Wh * (b - a) / 256;
  sy = originY_ + Hh * (a + b) / 256 - A * height / 256;
}

void Renderer::centreOnWorld(Camera& cam, float x, float z) const {
  float W = float(world_->W), H = float(world_->H);
  switch (cam.rot & 3) {
    case 0: cam.u = x; cam.v = z; break;
    case 1: cam.u = W - z; cam.v = x; break;
    case 2: cam.u = W - x; cam.v = H - z; break;
    default: cam.u = z; cam.v = H - x; break;
  }
}

// Corner order: top (u,v), right (u,v+1), left (u+1,v), bottom (u+1,v+1). Screen at altitude 0.
void Renderer::tileQuad(const Camera& cam, int u, int v, float sx[4], float sy[4], int wx[4], int wz[4]) const {
  static const int cu[4] = {0, 0, 1, 1}, cv[4] = {0, 1, 0, 1};
  const float Wt = 8.f * float(1 << cam.zoom), Ht = Wt / 2;
  for (int i = 0; i < 4; i++) {
    viewToWorldVertex(cam.rot, world_->W, world_->H, u + cu[i], v + cv[i], wx[i], wz[i]);
    sx[i] = float(originX_) + float((v + cv[i]) - (u + cu[i])) * (Wt / 2);
    sy[i] = float(originY_) + float((u + cu[i]) + (v + cv[i])) * (Ht / 2);
  }
}

// ======================================================================================
// Background + terrain
// ======================================================================================

void Renderer::loadBackground(int zoom) {
  if (!backTried_[zoom]) {
    backTried_[zoom] = true;
    if (data_) {
      fs::path p = resolveCI(data_->appsDir(), "Res/Sprites/BACK" + std::to_string(zoom) + ".BMP");
      Bytes b;
      if (!p.empty() && readFile(p, b)) { try { back_[zoom] = decodeBmp(b.data(), b.size(), false); } catch (const FormatError&) {} }
    }
  }
}

void Renderer::drawBackground(int zoom, int y0, int y1, int xs, int xe) {
  const Image& im = back_[zoom];
  if (im.empty()) return;
  const int xa = std::max(0, xs), xb = std::min(fbW_, xe);
  for (int y = y0; y < y1; y++) {
    int ty = ((y - originY_) % im.h + im.h) % im.h;
    const uint32_t* row = &im.px[size_t(ty) * im.w];
    uint32_t* dst = &fb_[size_t(y) * fbW_];
    int tx = ((xa - originX_) % im.w + im.w) % im.w;
    for (int x = xa; x < xb; x++) {
      dst[x] = row[tx] | 0xFF000000u;
      if (++tx == im.w) tx = 0;
    }
  }
}

// Shared terrain state; runs before the (parallel) drawTerrain bands.
void Renderer::prepareTerrain(const RenderOptions& opt) {
  const World& w = *world_;
  ensureShade();
  roadMask_.assign(size_t(w.W) * w.H, 0);
  // Grey network paint is a stand-in for missing piece sprites only. With sprites, road/rail pieces cover
  // their tile anyway, and power-line pieces leave the ground visible, so painting would show a grey strip.
  const bool netSprites = opt.objects && (data_ || spriteFn_);
  if (opt.zonePaint && !opt.underground && !netSprites)
    for (const auto& np : w.surface) if (w.inTiles(np.x, np.z)) roadMask_[w.tIdx(np.x, np.z)] = 1;
}

void Renderer::drawTerrain(const Camera& cam, const RenderOptions& opt, int y0, int y1, int xs, int xe) {
  const World& w = *world_;
  const int z = cam.zoom;
  const float A = float(1 << z);
  Ctx ctx{fb_.data(), fbW_, y1, z, noiseLand_, noiseEdge_, y0};
  ctx.XS = xs; ctx.XE = xe;

  float u0, v0, u1, v1, u2, v2, u3, v3;
  screenToView(cam, 0, 0, u0, v0); screenToView(cam, fbW_, 0, u1, v1);
  screenToView(cam, 0, fbH_, u2, v2); screenToView(cam, fbW_, fbH_, u3, v3);
  int pad = 4 + (maxAlt_ + 1) / 2;
  int umin = int(std::floor(std::min(std::min(u0, u1), std::min(u2, u3)))) - 2;
  int umax = int(std::ceil(std::max(std::max(u0, u1), std::max(u2, u3)))) + 2 + pad;
  int vmin = int(std::floor(std::min(std::min(v0, v1), std::min(v2, v3)))) - 2 - pad;
  int vmax = int(std::ceil(std::max(std::max(v0, v1), std::max(v2, v3)))) + 2 + pad;
  umin = std::max(umin, 0); vmin = std::max(vmin, 0);
  umax = std::min(umax, w.W - 1); vmax = std::min(vmax, w.H - 1);

  // palBasic tint colour (column 31, far-zoom adjusted).
  auto basicTint = [&](int row, int c[3]) {
    fetch(pal_.basic, row, 31, c[0], c[1], c[2]);
    farZoomAdjust(z, c[0], c[1], c[2]);
  };
  const Image* wpal = &pal_.water;

  for (int s = umin + vmin; s <= umax + vmax; s++) {
    for (int u = std::max(umin, s - vmax); u <= std::min(umax, s - vmin); u++) {
      int v = s - u;
      float qx[4], qy[4]; int wx[4], wz[4];
      tileQuad(cam, u, v, qx, qy, wx, wz);
      int alt[4], wat[4];
      for (int i = 0; i < 4; i++) { alt[i] = w.altAt(wx[i], wz[i]); wat[i] = w.waterAt(wx[i], wz[i]); }
      float gy[4];
      float minY = 1e9f, maxY = -1e9f, minX = 1e9f, maxX = -1e9f;
      for (int i = 0; i < 4; i++) {
        gy[i] = qy[i] - float(alt[i]) * A;
        float top = std::min(gy[i], qy[i] - float(std::max(alt[i], wat[i])) * A);
        minY = std::min(minY, top); maxY = std::max(maxY, gy[i]);
        minX = std::min(minX, qx[i]); maxX = std::max(maxX, qx[i]);
      }
      if (maxY < float(y0) || minY > float(y1) || maxX < float(xs) || minX > float(std::min(xe, fbW_))) continue;
      int tx0 = std::min(std::min(wx[0], wx[1]), std::min(wx[2], wx[3])), tz0 = std::min(std::min(wz[0], wz[1]), std::min(wz[2], wz[3]));
      if (!w.inTiles(tx0, tz0)) continue;
      const int tIdx = w.tIdx(tx0, tz0);
      const int seed = seedFor(tx0, tz0);
      const int ytop = int(std::ceil(minY));

      // Corner indices: 0 top, 1 right, 2 left, 3 bottom.
      // Spec 6: split along the left-right diagonal only when L,R are level but T,B are not.
      auto split = [&](const void*) { return alt[2] == alt[1] && alt[0] != alt[3]; };
      auto drawQuad = [&](const float P[4][5], int mode /*0 land rgb,1 mono,2 none*/) {
        NoiseMode nm = mode == 0 ? NoiseMode::Rgb : mode == 1 ? NoiseMode::Mono : NoiseMode::None;
        if (split(nullptr)) { gouraudTri(ctx, P[2], P[0], P[1], seed, ytop, nm); gouraudTri(ctx, P[2], P[1], P[3], seed, ytop, nm); }
        else { gouraudTri(ctx, P[0], P[2], P[3], seed, ytop, nm); gouraudTri(ctx, P[0], P[3], P[1], seed, ytop, nm); }
      };

      // Per-vertex land colours.
      int col[4][3];
      for (int i = 0; i < 4; i++) landVertexColour(wx[i], wz[i], z, opt.underground, col[i][0], col[i][1], col[i][2]);

      int overlayRow = 0xFF;
      if (opt.overlay && opt.overlay->row.size() == size_t(w.W) * w.H) overlayRow = opt.overlay->row[tIdx];

      float P[4][5];
      for (int i = 0; i < 4; i++) { P[i][0] = qx[i]; P[i][1] = gy[i]; }

      if (overlayRow != 0xFF) {
        // Solid-colour tile: palette slot, fixed row, column from shade; no noise, no tint.
        const Image& im = opt.overlay->slot == 0 ? pal_.basic : pal_.data[std::max(0, std::min(2, opt.overlay->slot - 1))];
        for (int i = 0; i < 4; i++) {
          int r, g, b;
          fetch(im, overlayRow, shade_[w.vIdx(wx[i], wz[i])] / 2, r, g, b);
          farZoomAdjust(z, r, g, b);
          P[i][2] = float(r); P[i][3] = float(g); P[i][4] = float(b);
        }
        drawQuad(P, 2);
      } else {
        // Tint selection (spec 6.4, 9.3).
        int tintRow = (tint_.size() == size_t(w.W) * w.H) ? tint_[tIdx] : 0;
        if (opt.underground && !tintRow) {
          int t = alt[0], r = alt[1], l = alt[2], b = alt[3];
          bool gentle = ((t == r && l == b) || (t == l && r == b)) &&
                        std::abs(t - b) <= 2 && std::abs(t - l) <= 2 && std::abs(t - r) <= 2;
          if (!gentle) tintRow = 52;
        }
        bool dry[4], anyDry = false, allDry = true;
        for (int i = 0; i < 4; i++) { dry[i] = opt.underground || alt[i] > wat[i]; anyDry |= dry[i]; allDry &= dry[i]; }  // underground: no water surface, no shoreline
        int tintCol[3] = {0, 0, 0};
        if (tintRow) {
          basicTint(tintRow, tintCol);
        }
        if (tintRow)
          for (int i = 0; i < 4; i++) if (dry[i] || !anyDry) for (int k = 0; k < 3; k++) col[i][k] = (col[i][k] + tintCol[k]) / 2;
        // Legacy zone/road paint.
        if (!tintRow && allDry && opt.zonePaint && !opt.underground) {
          int zt = w.zone[tIdx];
          float tr = 0, tg = 0, tb = 0, ta = 0;
          if (roadMask_[tIdx]) { tr = 90; tg = 90; tb = 95; ta = 0.85f; }
          else if (zt >= 1 && zt <= 3) { tr = 40; tg = 210; tb = 60; ta = 0.45f; }
          else if (zt >= 5 && zt <= 7) { tr = 60; tg = 100; tb = 250; ta = 0.45f; }
          else if (zt >= 9 && zt <= 11) { tr = 240; tg = 210; tb = 40; ta = 0.45f; }
          else if (zt) { tr = 200; tg = 120; tb = 200; ta = 0.45f; }
          if (ta > 0) for (int i = 0; i < 4; i++) {
            col[i][0] += int((tr - col[i][0]) * ta); col[i][1] += int((tg - col[i][1]) * ta); col[i][2] += int((tb - col[i][2]) * ta);
          }
        }
        if (anyDry && !allDry) {
          // Shoreline colour-key variant.
          int key[3] = {160, 160, 160};
          farZoomAdjust(z, key[0], key[1], key[2]);
          int rep[3];
          fetch(*wpal, 0, 0, rep[0], rep[1], rep[2]);
          farZoomAdjust(z, rep[0], rep[1], rep[2]);
          if (tintRow) for (int k = 0; k < 3; k++) { rep[k] = (rep[k] + tintCol[k]) / 2; }
          for (int i = 0; i < 4; i++) {
            int* c = dry[i] ? col[i] : key;
            P[i][2] = float(c[0]); P[i][3] = float(c[1]); P[i][4] = float(c[2]);
            // Wet corners sit at the water surface so the waterline meets the neighbouring water polygons without a seabed gap.
            if (!dry[i]) P[i][1] = qy[i] - float(wat[i]) * A;
          }
          if (split(nullptr)) { shoreTri(ctx, P[2], P[0], P[1], seed, ytop, key, rep); shoreTri(ctx, P[2], P[1], P[3], seed, ytop, key, rep); }
          else { shoreTri(ctx, P[0], P[2], P[3], seed, ytop, key, rep); shoreTri(ctx, P[0], P[3], P[1], seed, ytop, key, rep); }
        } else {
          for (int i = 0; i < 4; i++) { P[i][2] = float(col[i][0]); P[i][3] = float(col[i][1]); P[i][4] = float(col[i][2]); }
          // Seabed polygon first (fills the gaps beside shoreline tiles), water polygon on top.
          drawQuad(P, 0);
          if (!anyDry) {
            // Water surface polygon on top of the submerged land.
            float W4[4][5];
            for (int i = 0; i < 4; i++) {
              int r, g, b;
              fetch(*wpal, std::max(0, wat[i] - alt[i]), 0, r, g, b);
              farZoomAdjust(z, r, g, b);
              if (tintRow) { r = (r + tintCol[0]) / 2; g = (g + tintCol[1]) / 2; b = (b + tintCol[2]) / 2; }
              W4[i][0] = qx[i]; W4[i][1] = qy[i] - float(wat[i]) * A;
              W4[i][2] = float(r); W4[i][3] = float(g); W4[i][4] = float(b);
            }
            drawQuad(W4, 1);
            for (int i = 0; i < 4; i++) P[i][1] = W4[i][1];
          }
        }
      }
      if (opt.grid) {
        int delta = z <= 1 ? -8 : -16;
        gridEdge(ctx, P[0][0], P[0][1], P[2][0], P[2][1], true, delta);   // upper-left edge
        gridEdge(ctx, P[0][0], P[0][1], P[1][0], P[1][1], false, delta);  // upper-right edge
      }
    }
  }
}

void Renderer::collectTerrainTris(const Camera& cam, const RenderOptions& opt, std::vector<GpuTerrainTri>& out) {
  if (!world_ || fbW_ <= 0 || fbH_ <= 0) return;
  const World& w = *world_;
  const int z = cam.zoom;
  const float A = float(1 << z);
  const int y0 = 0, y1 = fbH_;

  float u0, v0, u1, v1, u2, v2, u3, v3;
  screenToView(cam, 0, 0, u0, v0); screenToView(cam, fbW_, 0, u1, v1);
  screenToView(cam, 0, fbH_, u2, v2); screenToView(cam, fbW_, fbH_, u3, v3);
  int pad = 4 + (maxAlt_ + 1) / 2;
  int umin = int(std::floor(std::min(std::min(u0, u1), std::min(u2, u3)))) - 2;
  int umax = int(std::ceil(std::max(std::max(u0, u1), std::max(u2, u3)))) + 2 + pad;
  int vmin = int(std::floor(std::min(std::min(v0, v1), std::min(v2, v3)))) - 2 - pad;
  int vmax = int(std::ceil(std::max(std::max(v0, v1), std::max(v2, v3)))) + 2 + pad;
  umin = std::max(umin, 0); vmin = std::max(vmin, 0);
  umax = std::min(umax, w.W - 1); vmax = std::min(vmax, w.H - 1);

  auto basicTint = [&](int row, int c[3]) {
    fetch(pal_.basic, row, 31, c[0], c[1], c[2]);
    farZoomAdjust(z, c[0], c[1], c[2]);
  };
  const Image* wpal = &pal_.water;
  auto pushTri = [&](const float* a, const float* b, const float* c, int seed, int ytop, int mode,
                     const int* key, const int* rep) {
    GpuTerrainTri t{};
    for (int i = 0; i < 5; i++) { t.p0[i] = a[i]; t.p1[i] = b[i]; t.p2[i] = c[i]; }
    t.seed = seed; t.ytop = ytop; t.mode = mode;
    if (key) for (int i = 0; i < 3; i++) t.key[i] = key[i];
    if (rep) for (int i = 0; i < 3; i++) t.rep[i] = rep[i];
    out.push_back(t);
  };
  auto emitQuad = [&](const float P[4][5], int mode, int seed, int ytop, bool splitLR) {
    if (splitLR) {
      pushTri(P[2], P[0], P[1], seed, ytop, mode, nullptr, nullptr);
      pushTri(P[2], P[1], P[3], seed, ytop, mode, nullptr, nullptr);
    } else {
      pushTri(P[0], P[2], P[3], seed, ytop, mode, nullptr, nullptr);
      pushTri(P[0], P[3], P[1], seed, ytop, mode, nullptr, nullptr);
    }
  };
  auto emitShore = [&](const float P[4][5], int seed, int ytop, bool splitLR, const int key[3], const int rep[3]) {
    if (splitLR) {
      pushTri(P[2], P[0], P[1], seed, ytop, 3, key, rep);
      pushTri(P[2], P[1], P[3], seed, ytop, 3, key, rep);
    } else {
      pushTri(P[0], P[2], P[3], seed, ytop, 3, key, rep);
      pushTri(P[0], P[3], P[1], seed, ytop, 3, key, rep);
    }
  };

  for (int s = umin + vmin; s <= umax + vmax; s++) {
    for (int u = std::max(umin, s - vmax); u <= std::min(umax, s - vmin); u++) {
      int v = s - u;
      float qx[4], qy[4]; int wx[4], wz[4];
      tileQuad(cam, u, v, qx, qy, wx, wz);
      int alt[4], wat[4];
      for (int i = 0; i < 4; i++) { alt[i] = w.altAt(wx[i], wz[i]); wat[i] = w.waterAt(wx[i], wz[i]); }
      float gy[4];
      float minY = 1e9f, maxY = -1e9f, minX = 1e9f, maxX = -1e9f;
      for (int i = 0; i < 4; i++) {
        gy[i] = qy[i] - float(alt[i]) * A;
        float top = std::min(gy[i], qy[i] - float(std::max(alt[i], wat[i])) * A);
        minY = std::min(minY, top); maxY = std::max(maxY, gy[i]);
        minX = std::min(minX, qx[i]); maxX = std::max(maxX, qx[i]);
      }
      if (maxY < float(y0) || minY > float(y1) || maxX < 0 || minX > fbW_) continue;
      int tx0 = std::min(std::min(wx[0], wx[1]), std::min(wx[2], wx[3])), tz0 = std::min(std::min(wz[0], wz[1]), std::min(wz[2], wz[3]));
      if (!w.inTiles(tx0, tz0)) continue;
      const int tIdx = w.tIdx(tx0, tz0);
      const int seed = seedFor(tx0, tz0);
      const int ytop = int(std::ceil(minY));
      const bool splitLR = alt[2] == alt[1] && alt[0] != alt[3];

      int col[4][3];
      for (int i = 0; i < 4; i++) landVertexColour(wx[i], wz[i], z, opt.underground, col[i][0], col[i][1], col[i][2]);
      int overlayRow = 0xFF;
      if (opt.overlay && opt.overlay->row.size() == size_t(w.W) * w.H) overlayRow = opt.overlay->row[tIdx];
      float P[4][5];
      for (int i = 0; i < 4; i++) { P[i][0] = qx[i]; P[i][1] = gy[i]; }

      if (overlayRow != 0xFF) {
        const Image& im = opt.overlay->slot == 0 ? pal_.basic : pal_.data[std::max(0, std::min(2, opt.overlay->slot - 1))];
        for (int i = 0; i < 4; i++) {
          int r, g, b;
          fetch(im, overlayRow, shade_[w.vIdx(wx[i], wz[i])] / 2, r, g, b);
          farZoomAdjust(z, r, g, b);
          P[i][2] = float(r); P[i][3] = float(g); P[i][4] = float(b);
        }
        emitQuad(P, 2, seed, ytop, splitLR);
      } else {
        int tintRow = (tint_.size() == size_t(w.W) * w.H) ? tint_[tIdx] : 0;
        if (opt.underground && !tintRow) {
          int t = alt[0], r = alt[1], l = alt[2], b = alt[3];
          bool gentle = ((t == r && l == b) || (t == l && r == b)) &&
                        std::abs(t - b) <= 2 && std::abs(t - l) <= 2 && std::abs(t - r) <= 2;
          if (!gentle) tintRow = 52;
        }
        bool dry[4], anyDry = false, allDry = true;
        for (int i = 0; i < 4; i++) { dry[i] = opt.underground || alt[i] > wat[i]; anyDry |= dry[i]; allDry &= dry[i]; }  // underground: no water surface, no shoreline
        int tintCol[3] = {0, 0, 0};
        if (tintRow) basicTint(tintRow, tintCol);
        if (tintRow)
          for (int i = 0; i < 4; i++) if (dry[i] || !anyDry) for (int k = 0; k < 3; k++) col[i][k] = (col[i][k] + tintCol[k]) / 2;
        if (!tintRow && allDry && opt.zonePaint && !opt.underground) {
          int zt = w.zone[tIdx];
          float tr = 0, tg = 0, tb = 0, ta = 0;
          if (roadMask_[tIdx]) { tr = 90; tg = 90; tb = 95; ta = 0.85f; }
          else if (zt >= 1 && zt <= 3) { tr = 40; tg = 210; tb = 60; ta = 0.45f; }
          else if (zt >= 5 && zt <= 7) { tr = 60; tg = 100; tb = 250; ta = 0.45f; }
          else if (zt >= 9 && zt <= 11) { tr = 240; tg = 210; tb = 40; ta = 0.45f; }
          else if (zt) { tr = 200; tg = 120; tb = 200; ta = 0.45f; }
          if (ta > 0) for (int i = 0; i < 4; i++) {
            col[i][0] += int((tr - col[i][0]) * ta); col[i][1] += int((tg - col[i][1]) * ta); col[i][2] += int((tb - col[i][2]) * ta);
          }
        }
        if (anyDry && !allDry) {
          int key[3] = {160, 160, 160};
          farZoomAdjust(z, key[0], key[1], key[2]);
          int rep[3];
          fetch(*wpal, 0, 0, rep[0], rep[1], rep[2]);
          farZoomAdjust(z, rep[0], rep[1], rep[2]);
          if (tintRow) for (int k = 0; k < 3; k++) { rep[k] = (rep[k] + tintCol[k]) / 2; }
          for (int i = 0; i < 4; i++) {
            int* c = dry[i] ? col[i] : key;
            P[i][2] = float(c[0]); P[i][3] = float(c[1]); P[i][4] = float(c[2]);
            if (!dry[i]) P[i][1] = qy[i] - float(wat[i]) * A;
          }
          emitShore(P, seed, ytop, splitLR, key, rep);
        } else {
          for (int i = 0; i < 4; i++) { P[i][2] = float(col[i][0]); P[i][3] = float(col[i][1]); P[i][4] = float(col[i][2]); }
          emitQuad(P, 0, seed, ytop, splitLR);
          if (!anyDry) {
            float W4[4][5];
            for (int i = 0; i < 4; i++) {
              int r, g, b;
              fetch(*wpal, std::max(0, wat[i] - alt[i]), 0, r, g, b);
              farZoomAdjust(z, r, g, b);
              if (tintRow) { r = (r + tintCol[0]) / 2; g = (g + tintCol[1]) / 2; b = (b + tintCol[2]) / 2; }
              W4[i][0] = qx[i]; W4[i][1] = qy[i] - float(wat[i]) * A;
              W4[i][2] = float(r); W4[i][3] = float(g); W4[i][4] = float(b);
            }
            emitQuad(W4, 1, seed, ytop, splitLR);
            for (int i = 0; i < 4; i++) P[i][1] = W4[i][1];
          }
        }
      }
    }
  }
}

void Renderer::drawTerrainGrid(const Camera& cam, const RenderOptions& opt, int y0, int y1, int xs, int xe) {
  if (!opt.grid || !world_) return;
  const World& w = *world_;
  const int z = cam.zoom;
  const float A = float(1 << z);
  Ctx ctx{fb_.data(), fbW_, y1, z, noiseLand_, noiseEdge_, y0};
  ctx.XS = xs; ctx.XE = xe;
  float u0, v0, u1, v1, u2, v2, u3, v3;
  screenToView(cam, 0, 0, u0, v0); screenToView(cam, fbW_, 0, u1, v1);
  screenToView(cam, 0, fbH_, u2, v2); screenToView(cam, fbW_, fbH_, u3, v3);
  int pad = 4 + (maxAlt_ + 1) / 2;
  int umin = int(std::floor(std::min(std::min(u0, u1), std::min(u2, u3)))) - 2;
  int umax = int(std::ceil(std::max(std::max(u0, u1), std::max(u2, u3)))) + 2 + pad;
  int vmin = int(std::floor(std::min(std::min(v0, v1), std::min(v2, v3)))) - 2 - pad;
  int vmax = int(std::ceil(std::max(std::max(v0, v1), std::max(v2, v3)))) + 2 + pad;
  umin = std::max(umin, 0); vmin = std::max(vmin, 0);
  umax = std::min(umax, w.W - 1); vmax = std::min(vmax, w.H - 1);
  const int delta = z <= 1 ? -8 : -16;
  for (int s = umin + vmin; s <= umax + vmax; s++) {
    for (int u = std::max(umin, s - vmax); u <= std::min(umax, s - vmin); u++) {
      int v = s - u;
      float qx[4], qy[4]; int wx[4], wz[4];
      tileQuad(cam, u, v, qx, qy, wx, wz);
      int alt[4], wat[4];
      for (int i = 0; i < 4; i++) { alt[i] = w.altAt(wx[i], wz[i]); wat[i] = w.waterAt(wx[i], wz[i]); }
      float gy[4], minY = 1e9f, maxY = -1e9f, minX = 1e9f, maxX = -1e9f;
      for (int i = 0; i < 4; i++) {
        gy[i] = qy[i] - float(alt[i]) * A;
        float top = std::min(gy[i], qy[i] - float(std::max(alt[i], wat[i])) * A);
        minY = std::min(minY, top); maxY = std::max(maxY, gy[i]);
        minX = std::min(minX, qx[i]); maxX = std::max(maxX, qx[i]);
      }
      if (maxY < float(y0) || minY > float(y1) || maxX < float(xs) || minX > float(std::min(xe, fbW_))) continue;
      int tx0 = std::min(std::min(wx[0], wx[1]), std::min(wx[2], wx[3])), tz0 = std::min(std::min(wz[0], wz[1]), std::min(wz[2], wz[3]));
      if (!w.inTiles(tx0, tz0)) continue;
      bool dry[4], anyDry = false, allDry = true;
      for (int i = 0; i < 4; i++) { dry[i] = opt.underground || alt[i] > wat[i]; anyDry |= dry[i]; allDry &= dry[i]; }
      float py[4] = {gy[0], gy[1], gy[2], gy[3]};
      if (anyDry && !allDry) {
        for (int i = 0; i < 4; i++) if (!dry[i]) py[i] = qy[i] - float(wat[i]) * A;
      } else if (!anyDry) {
        for (int i = 0; i < 4; i++) py[i] = qy[i] - float(wat[i]) * A;
      }
      gridEdge(ctx, qx[0], py[0], qx[2], py[2], true, delta);
      gridEdge(ctx, qx[0], py[0], qx[1], py[1], false, delta);
    }
  }
}

// ======================================================================================
// Map-edge faces (spec 6.5)
// ======================================================================================

void Renderer::drawEdgeFaces(const Camera& cam, const RenderOptions& opt, int y0, int y1, int xs, int xe) {
  const World& w = *world_;
  const int z = cam.zoom;
  const float Wt = 8.f * float(1 << z), Ht = Wt / 2, A = float(1 << z);
  Ctx ctx{fb_.data(), fbW_, y1, z, noiseLand_, noiseEdge_, y0};
  ctx.XS = xs; ctx.XE = xe;
  const bool even = (cam.rot & 1) == 0;
  auto seg = [&](int ua, int va, int ub, int vb, const Image& tbl) {
    int wx[2], wz[2];
    viewToWorldVertex(cam.rot, w.W, w.H, ua, va, wx[0], wz[0]);
    viewToWorldVertex(cam.rot, w.W, w.H, ub, vb, wx[1], wz[1]);
    float sx[2], s0[2]; int alt[2], wat[2];
    int us[2] = {ua, ub}, vs[2] = {va, vb};
    float minX = 1e9f, maxX = -1e9f;
    for (int i = 0; i < 2; i++) {
      alt[i] = w.altAt(wx[i], wz[i]); wat[i] = w.waterAt(wx[i], wz[i]);
      sx[i] = float(originX_) + float(vs[i] - us[i]) * (Wt / 2);
      s0[i] = float(originY_) + float(us[i] + vs[i]) * (Ht / 2);
      minX = std::min(minX, sx[i]); maxX = std::max(maxX, sx[i]);
    }
    if (maxX < 0 || minX > fbW_) return;
    float topY = std::min(s0[0], s0[1]) - 255 * A;
    if (topY > y1) return;
    int seed = seedFor(std::min(wx[0], w.W - 1), std::min(wz[0], w.H - 1));
    bool waterPart = !opt.underground && (wat[0] > alt[0] || wat[1] > alt[1]);  // no water surface underground
    if (waterPart) {
      float Q[4][5];
      float ymin = 1e9f;
      for (int i = 0; i < 2; i++) {
        int surf = std::max(alt[i], wat[i]);
        Q[i][0] = sx[i]; Q[i][1] = s0[i] - float(surf) * A; Q[i][2] = 0; Q[i][3] = Q[i][4] = 0;
        Q[3 - i][0] = sx[i]; Q[3 - i][1] = s0[i] - float(alt[i]) * A; Q[3 - i][2] = float(std::max(0, wat[i] - alt[i])); Q[3 - i][3] = Q[3 - i][4] = 0;
        ymin = std::min(ymin, Q[i][1]);
      }
      // Q0,Q1 surface (v1,v2); Q2,Q3 ground (v2,v1)
      int yt = int(std::ceil(ymin));
      indexTri(ctx, tbl, Q[0], Q[1], Q[2], seed, yt, false);
      indexTri(ctx, tbl, Q[0], Q[2], Q[3], seed, yt, false);
    }
    {
      float Q[4][5];
      float ymin = 1e9f;
      for (int i = 0; i < 2; i++) {
        Q[i][0] = sx[i]; Q[i][1] = s0[i] - float(alt[i]) * A; Q[i][2] = 256; Q[i][3] = Q[i][4] = 0;
        Q[3 - i][0] = sx[i]; Q[3 - i][1] = s0[i] + 4 * A; Q[3 - i][2] = float(std::min(511, 260 + alt[i])); Q[3 - i][3] = Q[3 - i][4] = 0;
        ymin = std::min(ymin, Q[i][1]);
      }
      int yt = int(std::ceil(ymin));
      indexTri(ctx, tbl, Q[0], Q[1], Q[2], seed, yt, true);
      indexTri(ctx, tbl, Q[0], Q[2], Q[3], seed, yt, true);
    }
  };
  // Border u = W (lower-left side) and v = H (lower-right side).
  const Image& tA = even ? pal_.edgeL : pal_.edgeD;
  const Image& tB = even ? pal_.edgeD : pal_.edgeL;
  for (int v = 0; v < w.H; v++) seg(w.W, v, w.W, v + 1, tA);
  for (int u = 0; u < w.W; u++) seg(u, w.H, u + 1, w.H, tB);
}

// ======================================================================================
// Sprites / draw list
// ======================================================================================

const SpriteImage* Renderer::getSprite(uint32_t c, uint32_t i) {
  if (spriteFn_) return spriteFn_(c, i);
  return data_ ? data_->sprite(c, i) : nullptr;
}
const Plane* Renderer::getPlane(uint32_t c, uint32_t i) {
  if (planeFn_) return planeFn_(c, i);
  return data_ ? data_->spritePlane(c, i) : nullptr;
}

namespace {
uint32_t floraInstance(uint8_t cell, uint32_t hash) {
  int kind = cell >> 4, lvl = cell & 15;
  if (kind < 1 || kind > 4 || lvl > 9) return 0;
  bool alt = (hash & 1) != 0;
  switch (kind) {
    case 1: return lvl >= 8 ? 7057 : (alt ? 7055 : 7054);
    case 2: return lvl <= 5 ? 7040 + lvl : lvl <= 7 ? (alt ? 7053 : 7046) : 9200;
    case 3: return lvl <= 5 ? 6 + lvl : lvl <= 7 ? (alt ? 7056 : 12) : 7057;
    default: return lvl <= 5 ? 7000 + lvl : lvl <= 7 ? (alt ? 7052 : 7006) : 9201;
  }
}
}  // namespace

uint32_t floraSpriteInstance(uint8_t cell, int x, int z) { return floraInstance(cell, uint32_t(x * 73856093u ^ z * 19349663u) >> 7); }

bool Renderer::siteOccupant(uint32_t state, int size, uint32_t agent, ResKey& out) {
  if (!data_) return false;
  if (!bldIniTried_) {
    bldIniTried_ = true;
    bldIniOk_ = data_->ini().load("SC3BuildingLayer.ini", bldIni_);
  }
  if (!bldIniOk_) return false;
  std::string ck = std::to_string(state) + ":" + std::to_string(size) + ":" + std::to_string(agent);
  auto it = siteCache_.find(ck);
  if (it != siteCache_.end()) { out = it->second; return out.instance != 0; }
  ResKey found;
  uint32_t agents[2] = {agent, 100};
  for (int k = 0; k < 2 && !found.instance; k++) {
    std::string key = std::to_string(state) + ":" + std::to_string(size) + ":" + std::to_string(agents[k]);
    std::string val;
    if (!bldIni_.get("DefaultBuildingData", key, val)) continue;
    std::string first = val.substr(0, val.find('+'));
    std::vector<std::string> f; std::string cur;
    for (char c : first) { if (c == ':') { f.push_back(trimSpaces(cur)); cur.clear(); } else cur.push_back(c); }
    f.push_back(trimSpaces(cur));
    if (f.size() == 3) { found.type = parseIdNumber(f[0]); found.group = parseIdNumber(f[1]); found.instance = parseIdNumber(f[2]); }
  }
  siteCache_[ck] = found;
  out = found;
  return found.instance != 0;
}

// ---- building animations -----------------------------------------------------------------------------------------

namespace {
uint64_t mix64(uint64_t h, uint64_t v) { h = (h ^ v) * 0x9E3779B97F4A7C15ull; return h ^ (h >> 29); }
int slotZoomOf(const CsAttrib* cs, int zoom) { return cs->zoomCount < 5 ? zoom - (5 - cs->zoomCount) : zoom; }
}  // namespace

void Renderer::bindAnim(AnimInst& a, const AnimCand& c, int zoom, int rot, int dateMD) {
  a.slotZoom = zoom; a.slotRot = rot;
  a.scripts.clear(); a.runs.clear(); a.children.clear();
  const int sz = slotZoomOf(c.cs, zoom);
  for (const CsScript& sc : c.cs->scripts) {
    if (sc.zoom != sz || sc.orient != c.orient) continue;
    a.scripts.push_back(&sc);
    ScriptRun run;
    initialScriptFrame(sc, run, a.rng, dateMD);
    a.runs.push_back(run);
  }
  for (const CsChild& ch : c.cs->children) {
    if (ch.zoom != sz || ch.orient != c.orient) continue;
    const CsAttrib* ccs = data_ ? data_->csattrib(ResKey{c.appearance.type, 0x00006400u, ch.child}) : nullptr;
    if (!ccs || !ccs->valid) continue;
    AnimChild child;
    child.cs = ccs; child.geo = ch;
    child.rng = AnimRng(mix64(c.key, ch.child * 31u + uint32_t(ch.dx) + uint32_t(ch.dy) * 7u));
    child.j = int(child.rng.next() & 3);
    const int csz = slotZoomOf(ccs, zoom);
    for (const CsScript& sc : ccs->scripts) {
      if (sc.zoom != csz || sc.orient != c.orient) continue;
      child.scripts.push_back(&sc);
      ScriptRun run;
      initialScriptFrame(sc, run, child.rng, dateMD);
      child.runs.push_back(run);
    }
    if (!child.scripts.empty()) a.children.push_back(std::move(child));
  }
}


// Resolve the image and optional alpha plane of a Draw/DrawAlpha frame. DrawAlpha pairs frame f with frame f+1; the
// spec says f is the colour image, but the shipped smoke data has the mask on the even frame and the colour on the odd
// one, so the pair is resolved by what each frame actually decodes to.
bool Renderer::resolveFrame(const CsAttrib* cs, int frame, bool alpha, const SpriteImage*& img, const Plane*& plane, bool& dbl) {
  img = nullptr; plane = nullptr; dbl = false;
  if (frame < 0 || frame >= int(cs->frames.size())) return false;
  const CsFrame& f = cs->frames[size_t(frame)];
  if (!alpha) { img = getSprite(f.category, f.spriteId); dbl = f.flags == 2; return img != nullptr; }
  if (frame + 1 >= int(cs->frames.size())) { img = getSprite(f.category, f.spriteId); dbl = f.flags == 2; return img != nullptr; }
  const CsFrame& g = cs->frames[size_t(frame) + 1];
  const SpriteImage* a = getSprite(f.category, f.spriteId);
  const SpriteImage* b = getSprite(g.category, g.spriteId);
  if (a && !b) { img = a; plane = getPlane(g.category, g.spriteId); dbl = f.flags == 2; }
  else if (b && !a) { img = b; plane = getPlane(f.category, f.spriteId); dbl = g.flags == 2; }
  else { img = a; dbl = f.flags == 2; }   // both images (or neither): plain draw of f
  return img != nullptr;
}

void Renderer::emitAnimOverlay(const AnimCand& c, const CsScript& sc, const ScriptRun& run, std::vector<Item>& out) {
  const SpriteImage* os = nullptr; const Plane* pl = nullptr; bool dbl = false;
  if (!resolveFrame(c.cs, run.frame, run.alpha, os, pl, dbl)) return;
  Item ov = c.base;
  ov.spr = os; ov.dbl = dbl; ov.pickable = false; ov.groundLevel = false;
  ov.plane = pl;
  ov.ax = originX_ + c.base.ax + sc.offX + run.dx;
  ov.ay = originY_ + c.base.ay + sc.offY + run.dy;
  const int scale = ov.dbl ? 2 : 1;
  if (ov.ax - os->L * scale > fbW_ || ov.ax - os->L * scale + os->img.w * scale < 0 || ov.ay - os->T * scale > fbH_ ||
      ov.ay - os->T * scale + os->img.h * scale < 0) return;
  out.push_back(ov);
}

void Renderer::emitChild(const AnimCand& c, const AnimChild& ch, const ScriptRun& run, const CsScript& sc, std::vector<Item>& out) {
  const SpriteImage* os = nullptr; const Plane* pl = nullptr; bool dbl = false;
  if (!resolveFrame(ch.cs, run.frame, run.alpha, os, pl, dbl)) return;
  const World& w = *world_;
  const int Wh = 4 << curZoom_, Hh = 2 << curZoom_, A = 1 << curZoom_;
  const int32_t X = c.tileX * 256 + ch.geo.dx, Z = c.tileZ * 256 + ch.geo.dy, height = c.alt * 256 + ch.geo.dz;
  int32_t a, b;
  worldToViewSub(curRot_, w.W, w.H, X, Z, a, b);
  a = std::max<int32_t>(0, a); b = std::max<int32_t>(0, b);
  Item it;
  it.spr = os; it.dbl = dbl; it.pickable = false;
  it.plane = pl;
  it.ax = originX_ + Wh * (b - a) / 256 + sc.offX + run.dx;
  it.ay = originY_ + Hh * (a + b) / 256 - A * height / 256 + sc.offY + run.dy;
  const int scale = it.dbl ? 2 : 1;
  if (it.ax - os->L * scale > fbW_ || it.ax - os->L * scale + os->img.w * scale < 0 || it.ay - os->T * scale > fbH_ ||
      it.ay - os->T * scale + os->img.h * scale < 0) return;
  const int p = a >> 8, q = b >> 8;
  it.key = 0x3FFFC000u - 0x4000u * uint32_t(p + q) - 0x100u * uint32_t(std::max(0, height / 1024));
  it.boxA0 = p * 256; it.boxA1 = (p + 1) * 256; it.boxB0 = q * 256; it.boxB1 = (q + 1) * 256;
  out.push_back(it);
}

void Renderer::buildAnimItems(const Camera& cam, const RenderOptions& opt, std::vector<Item>& out) {
  if (animCands_.empty()) { anim_.clear(); animSelSig_ = 0; return; }
  auto visible = [&](const AnimCand& c) {
    const Item& b = c.base;
    const int x0 = originX_ + b.cAx - b.cL, y0 = originY_ + b.cAy - b.cT;
    return !(x0 > fbW_ || x0 + b.cW < 0 || y0 > fbH_ || y0 + b.cH < 0);
  };
  std::vector<const AnimCand*> vis;
  for (const AnimCand& c : animCands_) if (visible(c)) vis.push_back(&c);
  const bool animate = opt.animTick >= 0 && cam.zoom >= 2;
  // Static first frames for buildings that are not animating (so holes in the base image stay filled).
  auto staticFrames = [&](const AnimCand& c) {
    AnimRng rng(c.key);
    const int sz = slotZoomOf(c.cs, cam.zoom);
    for (const CsScript& sc : c.cs->scripts) {
      if (sc.zoom != sz || sc.orient != c.orient) continue;
      ScriptRun run;
      initialScriptFrame(sc, run, rng, opt.dateMD);
      emitAnimOverlay(c, sc, run, out);
    }
  };
  if (!animate) {
    anim_.clear(); animSelSig_ = 0;
    for (const AnimCand* c : vis) staticFrames(*c);
    return;
  }
  const int64_t clockMs = int64_t(opt.animTick) * kAnimTickMs;
  // Cap selection when the visible candidate set or the view slot changes (spec 7.3): keep running ones that are
  // still visible, fill the free slots with uniformly random picks among the other candidates.
  uint64_t sig = mix64(uint64_t(cam.zoom) * 4 + uint64_t(cam.rot & 3), vis.size());
  for (const AnimCand* c : vis) sig = mix64(sig, c->key);
  if (sig != animSelSig_) {
    animSelSig_ = sig;
    std::unordered_map<uint64_t, bool> visKey;
    for (const AnimCand* c : vis) visKey[c->key] = true;
    for (auto it = anim_.begin(); it != anim_.end();) it = visKey.count(it->first) ? std::next(it) : anim_.erase(it);
    std::vector<const AnimCand*> pool;
    for (const AnimCand* c : vis) if (!anim_.count(c->key)) pool.push_back(c);
    if (pool.size() > 200) pool.resize(200);
    while (int(anim_.size()) < maxAnimations_ && !pool.empty()) {
      const size_t k = animSelRng_.next() % pool.size();
      const AnimCand* c = pool[k];
      pool[k] = pool.back(); pool.pop_back();
      AnimInst a;
      a.rng = AnimRng(c->key);
      a.j = int(a.rng.next() & 3);
      a.startMs = a.lastUpdMs = clockMs;
      anim_[c->key] = std::move(a);
    }
  }
  for (const AnimCand* c : vis) {
    auto it = anim_.find(c->key);
    if (it == anim_.end()) { staticFrames(*c); continue; }
    AnimInst& a = it->second;
    if (a.slotZoom != cam.zoom || a.slotRot != cam.rot) bindAnim(a, *c, cam.zoom, cam.rot, opt.dateMD);
    // Advance in fixed 16 ms updates so the result does not depend on how often render() is called.
    int steps = 0;
    while (a.lastUpdMs + 16 <= clockMs && steps < 256) {
      a.lastUpdMs += 16; steps++;
      const int64_t now = (a.lastUpdMs - a.startMs) * 1000 / 1024;   // the original's microsecond clock >> 10
      for (size_t i = 0; i < a.scripts.size(); i++) stepScript(*a.scripts[i], a.runs[i], now, a.j, a.rng, opt.dateMD);
      for (AnimChild& ch : a.children)
        for (size_t i = 0; i < ch.scripts.size(); i++) stepScript(*ch.scripts[i], ch.runs[i], now, ch.j, ch.rng, opt.dateMD);
    }
    if (steps == 256) a.lastUpdMs = clockMs;
    for (size_t i = 0; i < a.scripts.size(); i++) emitAnimOverlay(*c, *a.scripts[i], a.runs[i], out);
    for (const AnimChild& ch : a.children)
      for (size_t i = 0; i < ch.scripts.size(); i++) emitChild(*c, ch, ch.runs[i], *ch.scripts[i], out);
  }
}

void Renderer::buildItems(const Camera& cam, const RenderOptions& opt, const RenderScene* scene) {
  items_.clear();
  occ_.clear();
  animCands_.clear();
  itemsCam_ = cam; itemsOpt_ = opt; itemsValid_ = true;
  if (!world_ || !opt.objects) return;
  const World& w = *world_;
  const int z = cam.zoom;
  const int Wh = 4 << z, Hh = 2 << z, A = 1 << z;
  const bool haveSprites = data_ || spriteFn_;
  if (!haveSprites) return;

  // `flat` holds ground-level network pieces (spec 25 5.1: surface, drawn before every object); `statics` the
  // depth-sorted objects that moving sprites are merged into.
  std::vector<Item> statics, movers, flat, sceneStatics;
  std::vector<AnimCand> animCands;
  auto cullOut = [&](int ax, int ay, int pw, int ph, int L, int T) {
    int x0 = ax - L, y0 = ay - T;
    return x0 > fbW_ || x0 + pw < 0 || y0 > fbH_ || y0 + ph < 0;
  };

  // Footprint -> item geometry. (umin..umax, vmin..vmax) are view vertex coordinates.
  auto setGeometry = [&](Item& it, int x, int zz, int y, int sizeX, int sizeZ) {
    int us[4], vs[4];
    int cxs[4] = {x, x + sizeX, x, x + sizeX}, czs[4] = {zz, zz, zz + sizeZ, zz + sizeZ};
    for (int i = 0; i < 4; i++) worldToViewVertex(cam.rot, w.W, w.H, cxs[i], czs[i], us[i], vs[i]);
    int umin = *std::min_element(us, us + 4), umax = *std::max_element(us, us + 4);
    int vmin = *std::min_element(vs, vs + 4), vmax = *std::max_element(vs, vs + 4);
    it.u0 = umin; it.u1 = umax; it.v0 = vmin; it.v1 = vmax;
    it.ax = (vmin - umax) * Wh;                 // relative to the screen origin; render adds it after culling
    it.ay = (umax + vmin) * Hh - y * A;
    int p = umax - 1, q = vmin, su = umax - umin, sv = vmax - vmin;
    it.boxA0 = (p - su + 1) * 256; it.boxA1 = (p + 1) * 256; it.boxB0 = q * 256; it.boxB1 = (q + sv) * 256;
    it.key = 0x3FFFC000u - 0x4000u * uint32_t(p + q) - 0x100u * uint32_t(y / 4);
    it.pickKey = 0x40000000u - 0x4000u * uint32_t(p + q) - 0x40u * uint32_t(y);
    it.tileX = x; it.tileZ = zz; it.subX = x * 256; it.subZ = zz * 256; it.height = y * 256;
  };

  // Resolve the frame set of a sprite-instance record for the current zoom.
  auto frameFor = [&](const CsAttrib* cs, int anim, int orient) -> const CsFrame* {
    int slot = z;
    if (cs->zoomCount < 5) { slot = z - (5 - cs->zoomCount); if (slot < 0) return nullptr; }
    if (slot >= cs->zoomCount) slot = cs->zoomCount - 1;
    return cs->frame(anim, slot, orient);
  };

  auto addStatic = [&](const ResKey& appearance, bool useOwn, int footprint, int x, int zz, int y, int rot, int sizeX, int sizeZ, bool ground) {
    const CsAttrib* cs = data_ ? data_->csattrib(appearance) : nullptr;
    const SpriteImage* sp = nullptr;
    bool dbl = false;
    const CsFrame* fr = nullptr;
    if (!useOwn && footprint < int(subst_.size()) && subst_[footprint].cat != 0xFFFFFFFFu) {
      sp = getSprite(subst_[footprint].cat, subst_[footprint].id);
    }
    if (!sp) {
      if (!cs) return;
      int orient = (cs->zoomSetCount >= 4 && cs->rotCount == 4) ? ((rot + cam.rot) & 3) : 0;
      fr = frameFor(cs, 0, orient);
      if (!fr) return;
      sp = getSprite(fr->category, fr->spriteId);
      dbl = fr->flags == 2;
    }
    if (!sp) return;
    Item it;
    setGeometry(it, x, zz, y, sizeX, sizeZ);
    int sc = dbl ? 2 : 1;
    // Cull box is stored, not tested here: the list is cached across camera moves and culled per frame.
    it.cAx = it.ax; it.cAy = it.ay; it.cL = sp->L * sc; it.cT = sp->T * sc; it.cW = sp->img.w * sc; it.cH = sp->img.h * sc;
    it.spr = sp; it.dbl = dbl; it.groundLevel = ground;
    if (ground) { it.key += 1; it.pickKey -= 1; }
    it.cat = fr ? fr->category : 0; it.id = fr ? fr->spriteId : 0;
    // Animatable building (sprite scripts and/or attached child sprites): recorded before the early cull, because
    // the animation cap picks among the visible ones and the cached list is culled per frame.
    if (cs && fr && (!cs->scripts.empty() || !cs->children.empty())) {
      AnimCand ac;
      ac.key = (uint64_t(uint32_t(x)) * 0x9E3779B1ull) ^ (uint64_t(uint32_t(zz)) << 32) ^ (uint64_t(appearance.instance) << 17) ^ (ground ? 1u : 0u);
      ac.appearance = appearance; ac.cs = cs;
      ac.orient = (cs->zoomSetCount >= 4 && cs->rotCount == 4) ? ((rot + cam.rot) & 3) : 0;
      ac.base = it; ac.tileX = x; ac.tileZ = zz; ac.alt = y;
      animCands.push_back(ac);
    }
    // Uncached builds cull right away (cheap rejection of the ~90% off-screen items); the cached list keeps them.
    if (!itemCacheOn_ && cullOut(originX_ + it.ax, originY_ + it.ay, sp->img.w * sc, sp->img.h * sc, sp->L * sc, sp->T * sc)) return;
    // A ground-level piece whose image stays near the tile diamond (road, rail, slope; not pylons, ramps or
    // bridges) lies on the surface: nothing moving can be behind it, so it must never paint over a vehicle that
    // overhangs from the tile behind. The anchor is the diamond centre: flat images reach about one tile height
    // above it, tall pieces several.
    // A plain road piece reaches about Hh above its anchor, steep slopes a couple of altitude steps more; a tree-lined
    // avenue (trees baked into the piece, T about 2.5 Hh) is taller than that and must order against movers by depth.
    const bool surface = ground && sp->T * 4 <= 9 * Hh;
    (surface ? flat : statics).push_back(it);
  };

  auto ownFlag = [&]() { return (opt.ownImageFlags & (0x10 << z)) != 0; };

  auto addOccupant = [&](uint32_t occId, bool net, bool port, int x, int zz, int y, int rot, int sizeX, int sizeZ, bool ground) {
    if (!data_) return;
    const OccAttr* oa = data_->occupant(occId, net, port);
    if (!oa) return;
    addStatic(oa->appearance, ownFlag(), std::max(sizeX, sizeZ), x, zz, y, rot, sizeX, sizeZ, ground);
  };

  // World-derived items: cached while nothing they depend on changed.
  bool itemHit = false;
  uint64_t itemKey = 0;
  if (itemCacheOn_) { itemKey = itemListKey(cam, opt); itemHit = itemCacheValid_ && itemKey == itemCacheKey_; }
  if (itemHit) {
    flat = cachedFlat_;
    statics = cachedStatics_;
    animCands = cachedAnim_;
  } else {
    const bool net = true;
    auto addNets = [&](const std::vector<NetPiece>& v, bool filter) {
      for (const auto& np : v)
        if (!filter || !opt.showNet || opt.showNet(np)) addOccupant(np.pieceId, net, false, np.x, np.z, np.y, np.rot, 1, 1, true);
    };
    if (opt.underground) {
      if (opt.pipes) addNets(w.pipes, false);
      if (opt.subways) addNets(w.subway, false);
    } else {
      addNets(w.surface, true);
      for (const auto& o : w.occupants) {
        if (opt.showOccupant && !opt.showOccupant(o)) continue;
        bool port = o.keyType == T_OCC_ATTR_PORT;
        uint32_t id = o.occId;
        const uint32_t ost = o.state & ~0x200u;   // bit 0x200 = protected, not a lot state
        if (opt.constructionSites && ost != 1 && ost != 2 && data_) {
          ResKey rk;
          if (siteOccupant(ost, std::max(o.sizeX, o.sizeZ), o.agentType, rk)) { id = rk.instance; port = rk.type == T_OCC_ATTR_PORT; }
        }
        addOccupant(id, false, port, o.x, o.z, o.y, o.rot, o.sizeX, o.sizeZ, false);
      }
      for (int x = 0; x < w.W && opt.flora; x++)
        for (int zz = 0; zz < w.H; zz++) {
          uint8_t c = w.flora[w.tIdx(x, zz)];
          if (!c) continue;
          uint32_t inst = floraInstance(c, uint32_t(x * 73856093u ^ zz * 19349663u) >> 7);
          if (inst) addOccupant(inst, false, false, x, zz, w.altAt(x, zz), 0, 1, 1, false);
        }
    }
    std::stable_sort(flat.begin(), flat.end(), [](const Item& a, const Item& b) { return a.key > b.key; });
    std::stable_sort(statics.begin(), statics.end(), [](const Item& a, const Item& b) { return a.key > b.key; });
    if (itemCacheOn_) { cachedFlat_ = flat; cachedStatics_ = statics; cachedAnim_ = animCands; itemCacheKey_ = itemKey; itemCacheValid_ = true; }
  }
  // Cull against this frame's screen and move to screen coordinates (cached lists are origin-relative).
  {
    auto visibleAt = [&](const Item& it) {
      const int x0 = originX_ + it.cAx - it.cL, y0 = originY_ + it.cAy - it.cT;
      return !(x0 > fbW_ || x0 + it.cW < 0 || y0 > fbH_ || y0 + it.cH < 0);
    };
    auto cullShift = [&](std::vector<Item>& v) {
      size_t o = 0;
      for (size_t i = 0; i < v.size(); i++) {
        if (!visibleAt(v[i])) continue;
        if (o != i) v[o] = v[i];
        v[o].ax += originX_; v[o].ay += originY_;
        o++;
      }
      v.resize(o);
    };
    cullShift(flat);
    cullShift(statics);
  }
  animCands_ = std::move(animCands);
  buildAnimItems(cam, opt, sceneStatics);   // overlay frames and child sprites of animating buildings, merged below

  // Extra sprites from other modules.
  if (scene && opt.automata) {
    for (const auto& ss : scene->sprites) {
      uint32_t cat = ss.category, id = ss.spriteId;
      bool dbl = ss.magnify2x;
      if (ss.useAppearance && data_) {
        const CsAttrib* cs = data_->csattrib(ss.appearance);
        if (!cs) continue;
        const CsFrame* fr = frameFor(cs, ss.animFrame, ss.orient);
        if (!fr) continue;
        cat = fr->category; id = fr->spriteId; dbl = dbl || fr->flags == 2;
      }
      const SpriteImage* sp = getSprite(cat, id);
      if (!sp) continue;
      Item it;
      it.spr = sp; it.dbl = dbl; it.cat = cat; it.id = id; it.m2 = ss.globalLevel; it.pickable = false;  // set below for statics
      if (ss.hasAlpha) it.plane = getPlane(ss.alphaCategory == 0xFFFFFFFFu ? cat : ss.alphaCategory, ss.alphaId);
      int32_t a, b;
      worldToViewSub(cam.rot, w.W, w.H, ss.x, ss.z, a, b);
      a = std::max<int32_t>(0, a); b = std::max<int32_t>(0, b);
      it.ax = originX_ + Wh * (b - a) / 256;
      it.ay = originY_ + Hh * (a + b) / 256 - A * ss.height / 256;
      int p = a >> 8, q = b >> 8;
      it.key = 0x3FFFC000u - 0x4000u * uint32_t(p + q) - 0x100u * uint32_t(std::max(0, ss.height / 1024));
      int sc = dbl ? 2 : 1;
      if (cullOut(it.ax, it.ay, sp->img.w * sc, sp->img.h * sc, sp->L * sc, sp->T * sc)) continue;
      if (ss.moving) {
        it.moving = true; it.posA = a; it.posB = b;
        movers.push_back(it);
      } else {
        int s = std::max(1, ss.footprint), ins = ss.inset ? 100 : 0;
        it.boxA0 = (p - s + 1) * 256 + ins; it.boxA1 = (p + 1) * 256 - ins;
        it.boxB0 = q * 256 + ins; it.boxB1 = (q + s) * 256 - ins;
        // Pickable like a static object: anchor tile = view tile (p,q) mapped back to world.
        int tx, tz;
        switch (cam.rot & 3) {
          case 0: tx = p; tz = q; break;
          case 1: tx = q; tz = w.W - 1 - p; break;
          case 2: tx = w.W - 1 - p; tz = w.H - 1 - q; break;
          default: tx = w.H - 1 - q; tz = p; break;
        }
        it.pickable = true;
        it.u0 = p - s + 1; it.u1 = p + 1; it.v0 = q; it.v1 = q + s;
        it.tileX = tx; it.tileZ = tz; it.subX = ss.x; it.subZ = ss.z; it.height = ss.height;
        it.pickKey = 0x40000000u - 0x4000u * uint32_t(p + q) - 0x40u * uint32_t(std::max(0, ss.height / 256));
        sceneStatics.push_back(it);
      }
    }
  }

  // Scene statics were gathered after the world's, so a stable merge equals the stable sort of the concatenation.
  if (!sceneStatics.empty()) {
    std::stable_sort(sceneStatics.begin(), sceneStatics.end(), [](const Item& a, const Item& b) { return a.key > b.key; });
    const size_t n0 = statics.size();
    statics.insert(statics.end(), sceneStatics.begin(), sceneStatics.end());
    std::inplace_merge(statics.begin(), statics.begin() + ptrdiff_t(n0), statics.end(), [](const Item& a, const Item& b) { return a.key > b.key; });
  }
  std::stable_sort(movers.begin(), movers.end(), [](const Item& a, const Item& b) { return a.key > b.key; });

  // Merge moving objects (spec 5.2): after statics wholly behind, before statics wholly in front,
  // otherwise by key; ties draw static first (spec 29 item 3.3).
  std::vector<size_t> pos(movers.size());
  auto movePos = [&](size_t m) {
    const Item& mv = movers[m];
    size_t lo = 0, hi = statics.size(), kpos = statics.size();
    bool haveKpos = false, haveHi = false;
    for (size_t i = 0; i < statics.size(); i++) {
      const Item& s = statics[i];
      // Depth keys have whole-tile granularity, and the median trees sit on the road tile itself, so a car on the far
      // lane shares its key with the tree it should be behind. Break key ties by the actual sub-tile depth: a static
      // whose centre is nearer the viewer than the mover is drawn after it.
      if (!haveKpos && (s.key < mv.key || (s.key - (s.groundLevel ? 1u : 0u) == mv.key && (int64_t(s.boxA0) + s.boxA1 + s.boxB0 + s.boxB1) / 2 > int64_t(mv.posA) + mv.posB))) { kpos = i; haveKpos = true; }
      // A mover inside the tile of a tall ground-level piece (a tree-lined road, say) is ordered against it by sub-tile
      // depth: the piece's depth key is biased so that it always drew first, which put far-lane cars over the trees.
      if (s.groundLevel && mv.posA >= s.boxA0 && mv.posA < s.boxA1 && mv.posB >= s.boxB0 && mv.posB < s.boxB1) {
        if ((int64_t(s.boxA0) + s.boxA1 + s.boxB0 + s.boxB1) / 2 > int64_t(mv.posA) + mv.posB) { if (!haveHi) { hi = i; haveHi = true; } }
        else lo = i + 1;
      }
      if (s.boxA1 <= mv.posA && s.boxB1 <= mv.posB) lo = i + 1;
      if (!haveHi && s.boxA0 >= mv.posA && s.boxB0 >= mv.posB) { hi = i; haveHi = true; }
    }
    pos[m] = lo <= hi ? std::max(lo, std::min(kpos, hi)) : kpos;
  };
  // Each mover's position is independent of the others (read-only statics), so split them over the pool.
  if (movers.size() * statics.size() >= 200000) {
    WorkerPool& mp = WorkerPool::shared();
    const int chunks = int(std::min<size_t>(movers.size(), size_t(mp.threads()) * 2));
    mp.run(chunks, [&](int c) {
      for (size_t m = movers.size() * size_t(c) / size_t(chunks); m < movers.size() * size_t(c + 1) / size_t(chunks); m++) movePos(m);
    });
  } else {
    for (size_t m = 0; m < movers.size(); m++) movePos(m);
  }
  items_.reserve(flat.size() + statics.size() + movers.size());
  items_.insert(items_.end(), flat.begin(), flat.end());
  size_t mi = 0;
  std::vector<size_t> order(movers.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return pos[a] < pos[b]; });
  for (size_t i = 0; i <= statics.size(); i++) {
    while (mi < order.size() && pos[order[mi]] == i) items_.push_back(movers[order[mi++]]);
    if (i < statics.size()) items_.push_back(statics[i]);
  }

  // Tile occupancy map for picking (non-ground objects only), later draw order wins.
  occ_.assign(size_t(w.W) * w.H, -1);
  for (size_t i = 0; i < items_.size(); i++) {
    const Item& it = items_[i];
    if (it.moving || it.groundLevel || !it.pickable) continue;
    for (int u = std::max(0, it.u0); u < std::min(w.W, it.u1); u++)
      for (int v = std::max(0, it.v0); v < std::min(w.H, it.v1); v++) occ_[size_t(u) * w.H + v] = int(i);
  }
}

void Renderer::drawSprite(const Item& it, const RenderOptions& opt, int bandY0, int bandY1) {
  const SpriteImage& s = *it.spr;
  const int sc = it.dbl ? 2 : 1;
  const int x0 = it.ax - s.L * sc, y0 = it.ay - s.T * sc;
  const Plane* pl = (it.plane && it.plane->w == s.img.w && it.plane->h == s.img.h) ? it.plane : nullptr;
  const bool blendAll = pl || it.m2 < 32;
  const int yb = std::max(0, bandY0 - y0), ye = std::min(s.img.h * sc, bandY1 - y0);
  for (int y = yb; y < ye; y++) {
    int dy = y0 + y;
    int sy = y / sc;
    const uint32_t* row = &s.img.px[size_t(sy) * s.img.w];
    for (int x = 0; x < s.img.w * sc; x++) {
      int dx = x0 + x;
      if (dx < 0 || dx >= fbW_) continue;
      int sx = x / sc;
      uint32_t p = row[sx];
      if (!(p >> 24)) continue;
      if (it.tinted) p = (p & 0xFF000000u) | pack((int(p & 255) + it.tintR) / 2, (int((p >> 8) & 255) + it.tintG) / 2, (int((p >> 16) & 255) + it.tintB) / 2);
      uint32_t& d = fb_[size_t(dy) * fbW_ + dx];
      if (!blendAll) { d = p; continue; }
      int mask = pl ? pl->v[size_t(sy) * pl->w + sx] : 31;
      int alpha = std::min(31, (mask * it.m2) >> 5);
      if (alpha <= 0) continue;
      int sr = p & 255, sg = (p >> 8) & 255, sb = (p >> 16) & 255;
      int dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255;
      int r, g, b;
      if (opt.exact5bit) {
        auto q = [&](int sv, int dv) { int o = blend5(sv >> 3, dv >> 3, alpha); return (o << 3) | (o >> 2); };
        r = q(sr, dr); g = q(sg, dg); b = q(sb, db);
      } else { r = blend8(sr, dr, alpha); g = blend8(sg, dg, alpha); b = blend8(sb, db, alpha); }
      d = pack(r, g, b);
    }
  }
}

void Renderer::drawItems(const RenderOptions& opt, int y0, int y1) {
  for (const auto& it : items_) drawSprite(it, opt, y0, y1);
}

// ======================================================================================
// Overlays
// ======================================================================================

// Marks the draw-list items hit by a scene tint. Items persist between frames (item list cache), so every frame resets them.
void Renderer::applySceneTints(const Camera& cam, const RenderScene* scene) {
  for (Item& it : items_) it.tinted = false;
  if (!scene || scene->tints.empty() || !world_) return;
  const World& w = *world_;
  for (const SpriteTint& t : scene->tints) {
    int us[4], vs[4];
    const int xa = std::min(t.x0, t.x1), xb = std::max(t.x0, t.x1) + 1, za = std::min(t.z0, t.z1), zb = std::max(t.z0, t.z1) + 1;
    int cx[4] = {xa, xb, xa, xb}, cz[4] = {za, za, zb, zb};
    for (int i = 0; i < 4; i++) worldToViewVertex(cam.rot, w.W, w.H, cx[i], cz[i], us[i], vs[i]);
    const int u0 = *std::min_element(us, us + 4), u1 = *std::max_element(us, us + 4);
    const int v0 = *std::min_element(vs, vs + 4), v1 = *std::max_element(vs, vs + 4);
    for (Item& it : items_) {
      if (it.moving || (!it.pickable && !it.groundLevel)) continue;
      if (it.u0 < u1 && u0 < it.u1 && it.v0 < v1 && v0 < it.v1) { it.tinted = true; it.tintR = t.r; it.tintG = t.g; it.tintB = t.b; }
    }
  }
}

// Flat terrain tiles of `ground` tints, blended 50/50 before the sprites go on top (so roads and trees on the tile are
// tinted by their own pass and not twice).
void Renderer::drawGroundTints(const Camera& cam, const RenderScene& scene, int y0, int y1) {
  if (scene.tints.empty() || !world_) return;
  const World& w = *world_;
  const float A = float(1 << cam.zoom);
  Ctx ctx{fb_.data(), fbW_, y1, cam.zoom, noiseLand_, noiseEdge_, y0};
  for (const SpriteTint& t : scene.tints) {
    if (!t.ground) continue;
    for (int x = std::max(0, std::min(t.x0, t.x1)); x <= std::min(w.W - 1, std::max(t.x0, t.x1)); x++)
      for (int zz = std::max(0, std::min(t.z0, t.z1)); zz <= std::min(w.H - 1, std::max(t.z0, t.z1)); zz++) {
        int u = 1 << 30, v = 1 << 30;
        for (int i = 0; i < 4; i++) {
          int uu, vv;
          worldToViewVertex(cam.rot, w.W, w.H, x + (i & 1), zz + (i >> 1), uu, vv);
          u = std::min(u, uu); v = std::min(v, vv);
        }
        float qx[4], qy[4]; int wx[4], wz[4];
        tileQuad(cam, u, v, qx, qy, wx, wz);
        float P[4][5];
        for (int i = 0; i < 4; i++) {
          int h = std::max(w.altAt(wx[i], wz[i]), w.waterAt(wx[i], wz[i]));
          P[i][0] = qx[i]; P[i][1] = qy[i] - float(h) * A;
        }
        blendTri(ctx, P[0], P[2], P[3], t.r, t.g, t.b, 128);
        blendTri(ctx, P[0], P[3], P[1], t.r, t.g, t.b, 128);
      }
  }
}

void Renderer::drawPreviews(const Camera& cam, const RenderScene& scene) {
  const World& w = *world_;
  const float A = float(1 << cam.zoom);
  Ctx ctx{fb_.data(), fbW_, fbH_, cam.zoom, noiseLand_, noiseEdge_};
  for (const auto& pv : scene.previews) {
    int xa = std::max(0, std::min(pv.x0, pv.x1)), xb = std::min(w.W - 1, std::max(pv.x0, pv.x1));
    int za = std::max(0, std::min(pv.z0, pv.z1)), zb = std::min(w.H - 1, std::max(pv.z0, pv.z1));
    int r = pv.customColour ? pv.r : pv.valid ? 0 : 240, g = pv.customColour ? pv.g : pv.valid ? 140 : 20, b = pv.customColour ? pv.b : pv.valid ? 205 : 40;
    for (int x = xa; x <= xb; x++)
      for (int zz = za; zz <= zb; zz++) {
        // View tile = min corner over the tile's four vertices.
        int u = 1 << 30, v = 1 << 30;
        for (int i = 0; i < 4; i++) {
          int uu, vv;
          worldToViewVertex(cam.rot, w.W, w.H, x + (i & 1), zz + (i >> 1), uu, vv);
          u = std::min(u, uu); v = std::min(v, vv);
        }
        float qx[4], qy[4]; int wx[4], wz[4];
        tileQuad(cam, u, v, qx, qy, wx, wz);
        float P[4][5];
        for (int i = 0; i < 4; i++) {
          int h = std::max(w.altAt(wx[i], wz[i]), w.waterAt(wx[i], wz[i]));
          P[i][0] = qx[i]; P[i][1] = qy[i] - float(h) * A;
        }
        blendTri(ctx, P[0], P[2], P[3], r, g, b, pv.alpha);
        blendTri(ctx, P[0], P[3], P[1], r, g, b, pv.alpha);
      }
  }
}

// ======================================================================================
// Frame
// ======================================================================================

void Renderer::drawFallbackBoxes(const Camera& cam) {
  const World& w = *world_;
  const int z = cam.zoom, Wh = 4 << z, Hh = 2 << z, A = 1 << z;
  auto fillConvex = [&](const float* px, const float* py, int n, uint32_t col) {
    float ymin = py[0], ymax = py[0];
    for (int i = 1; i < n; i++) { ymin = std::min(ymin, py[i]); ymax = std::max(ymax, py[i]); }
    for (int y = std::max(0, int(std::ceil(ymin))); y <= std::min(fbH_ - 1, int(std::floor(ymax))); y++) {
      float xl = 1e9f, xr = -1e9f;
      for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float y0 = py[i], y1 = py[j];
        if ((y0 <= y && y1 >= y) || (y1 <= y && y0 >= y)) {
          float x = (y1 == y0) ? px[i] : px[i] + (px[j] - px[i]) * (float(y) - y0) / (y1 - y0);
          xl = std::min(xl, x); xr = std::max(xr, x);
          if (y1 == y0) { xl = std::min(xl, px[j]); xr = std::max(xr, px[j]); }
        }
      }
      for (int x = std::max(0, int(std::ceil(xl))); x <= std::min(fbW_ - 1, int(std::floor(xr))); x++) fb_[size_t(y) * fbW_ + x] = col;
    }
  };
  struct Box { int u0, u1, v0, v1, y, hgt; uint32_t col; int key; };
  std::vector<Box> boxes;
  for (const Occupant& o : w.occupants) {
    int us[4], vs[4];
    int cxs[4] = {o.x, o.x + o.sizeX, o.x, o.x + o.sizeX}, czs[4] = {o.z, o.z, o.z + o.sizeZ, o.z + o.sizeZ};
    for (int i = 0; i < 4; i++) worldToViewVertex(cam.rot, w.W, w.H, cxs[i], czs[i], us[i], vs[i]);
    Box b;
    b.u0 = *std::min_element(us, us + 4); b.u1 = *std::max_element(us, us + 4);
    b.v0 = *std::min_element(vs, vs + 4); b.v1 = *std::max_element(vs, vs + 4);
    b.y = o.y;
    int tiles = o.sizeX;
    uint32_t t = o.agentType;
    uint32_t r = 170, g = 170, bl = 170;
    int hgt = 4 * tiles;
    if (t >= 3000 && t < 5000) { r = 90; g = 200; bl = 90; hgt = 3 * tiles + (t >= 4300 ? 8 : t >= 3700 ? 4 : 0); }
    else if (t >= 5000 && t < 8000) { r = 90; g = 130; bl = 230; hgt = 4 * tiles + (t >= 6300 ? 10 : 2); }
    else if (t >= 8000 && t < 10000) { r = 220; g = 200; bl = 70; hgt = 3 * tiles + 2; }
    else if (t >= 12000 && t < 13000) { r = 200; g = 90; bl = 90; hgt = 6 * tiles; }
    if (o.state == 4) { r /= 2; g /= 2; bl /= 2; hgt = 2; }
    else if (o.state == 2) { r = g = bl = 120; }
    else if (o.state == 8) { r = 110; g = 80; bl = 60; hgt = 1; }
    else if (o.state == 16) { r = 230; g = 90; bl = 20; hgt = 2; }
    b.hgt = hgt * A / 2;
    b.col = rgba(uint8_t(r), uint8_t(g), uint8_t(bl));
    b.key = b.u1 + b.v1;
    boxes.push_back(b);
  }
  std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return a.key < b.key; });
  for (const Box& b : boxes) {
    // Footprint diamond corners (view vertices): top (u0,v0), right (u0,v1), left (u1,v0), bottom (u1,v1).
    auto sx = [&](int u, int v) { return float(originX_ + (v - u) * Wh); };
    auto sy = [&](int u, int v) { return float(originY_ + (u + v) * Hh - b.y * A); };
    float tx[4] = {sx(b.u0, b.v0), sx(b.u0, b.v1), sx(b.u1, b.v1), sx(b.u1, b.v0)};
    float ty[4] = {sy(b.u0, b.v0), sy(b.u0, b.v1), sy(b.u1, b.v1), sy(b.u1, b.v0)};
    auto shade = [](uint32_t c, float f) { return rgba(uint8_t((c & 255) * f), uint8_t(((c >> 8) & 255) * f), uint8_t(((c >> 16) & 255) * f)); };
    float h = float(b.hgt);
    // Left face (left->bottom edge), right face (bottom->right edge), then the roof.
    { float px[4] = {tx[3], tx[2], tx[2], tx[3]}, py[4] = {ty[3], ty[2], ty[2] - h, ty[3] - h}; fillConvex(px, py, 4, shade(b.col, 0.7f)); }
    { float px[4] = {tx[2], tx[1], tx[1], tx[2]}, py[4] = {ty[2], ty[1], ty[1] - h, ty[2] - h}; fillConvex(px, py, 4, shade(b.col, 0.5f)); }
    { float px[4] = {tx[0], tx[1], tx[2], tx[3]}, py[4] = {ty[0] - h, ty[1] - h, ty[2] - h, ty[3] - h}; fillConvex(px, py, 4, b.col); }
  }
}

// ---- terrain layer cache -------------------------------------------------------------------------------------
namespace {
// Fast 64-bit content hash (8 bytes per step); only used to detect change, not for anything security related.
uint64_t hashBytes(uint64_t h, const void* data, size_t n) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  while (n >= 8) {
    uint64_t v; std::memcpy(&v, p, 8);
    h = (h ^ v) * 0x9E3779B97F4A7C15ull;
    h ^= h >> 29;
    p += 8; n -= 8;
  }
  uint64_t tail = 0;
  if (n) std::memcpy(&tail, p, n);
  h = (h ^ tail ^ (uint64_t(n) << 56)) * 0x9E3779B97F4A7C15ull;
  return h ^ (h >> 32);
}
template <class T> uint64_t hashVec(uint64_t h, const std::vector<T>& v) {
  return hashBytes(h ^ uint64_t(v.size()), v.data(), v.size() * sizeof(T));
}
}  // namespace

uint64_t Renderer::terrainLayerKey(const Camera& cam, const RenderOptions& opt) const {
  const World& w = *world_;
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { h = (h ^ v) * 0x9E3779B97F4A7C15ull; h ^= h >> 31; };
  // No camera position: the integer screen origin carries it (compared by the caller, see render()).
  mix(uint64_t(cam.zoom)); mix(uint64_t(cam.rot));
  mix(uint64_t(fbW_)); mix(uint64_t(fbH_)); mix(uint64_t(w.W)); mix(uint64_t(w.H));
  const bool netSprites = opt.objects && (data_ || spriteFn_);
  mix(uint64_t(opt.terrain) | uint64_t(opt.underground) << 1 | uint64_t(opt.zonePaint) << 2 | uint64_t(opt.background) << 3 |
      uint64_t(opt.grid) << 4 | uint64_t(opt.edgeFaces) << 5 | uint64_t(netSprites) << 6);
  h = hashVec(h, w.alt); h = hashVec(h, w.water); h = hashVec(h, w.zone); h = hashVec(h, tint_);
  // The road mask (grey network paint) is drawn from the surface pieces only in this mode.
  if (opt.zonePaint && !opt.underground && !netSprites)
    for (const auto& np : w.surface) { mix(uint64_t(uint32_t(np.x)) << 32 | uint32_t(np.z)); }
  if (opt.overlay) { mix(uint64_t(opt.overlay->slot) + 1); h = hashVec(h, opt.overlay->row); } else mix(0);
  return h;
}

uint64_t Renderer::itemListKey(const Camera& cam, const RenderOptions& opt) const {
  const World& w = *world_;
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { h = (h ^ v) * 0x9E3779B97F4A7C15ull; h ^= h >> 31; };
  // No camera position or window size: the list is origin-relative and culled per frame.
  mix(uint64_t(cam.zoom)); mix(uint64_t(cam.rot));
  mix(uint64_t(w.W)); mix(uint64_t(w.H)); mix(itemsGen_);
  mix(uint64_t(opt.underground) | uint64_t(opt.constructionSites) << 1 | uint64_t(opt.flora) << 2 | uint64_t(opt.pipes) << 3 |
      uint64_t(opt.subways) << 4 | uint64_t(bool(opt.showNet)) << 5 | uint64_t(bool(opt.showOccupant)) << 6);
  mix(opt.ownImageFlags); mix(opt.filterEpoch);   // animation time is not part of the key: overlays are built per frame
  if (opt.underground) {
    if (opt.pipes) h = hashVec(h, w.pipes);
    if (opt.subways) h = hashVec(h, w.subway);
  } else {
    h = hashVec(h, w.surface);
    for (const Occupant& o : w.occupants) {   // only the fields buildItems reads
      mix(uint64_t(uint32_t(o.x)) << 32 | uint32_t(o.z));
      mix(uint64_t(uint32_t(o.y)) << 32 | uint32_t(o.rot));
      mix(uint64_t(uint32_t(o.sizeX)) << 32 | uint32_t(o.sizeZ));
      mix(uint64_t(o.occId) << 32 | o.keyType);
      mix(uint64_t(o.state) << 32 | o.agentType);
    }
    if (opt.flora) { h = hashVec(h, w.flora); h = hashVec(h, w.alt); }
  }
  return h;
}

bool Renderer::renderForGpu(const Camera& cam, const RenderOptions& opt, const RenderScene* scene, GpuCityFrame& out) {
  if (!world_ || fbW_ <= 0 || fbH_ <= 0 || !terrainCacheOn_ || !opt.terrain || opt.exact5bit) return false;
  if (gpuTerrainWanted_ && gpuTerrainFn_) return false;
  if (opt.objects && !(data_ || spriteFn_)) return false;                    // fallback boxes are CPU drawn
  if (scene) {
    if (!scene->previews.empty() || !scene->tints.empty()) return false;     // tool previews and sprite tints blend into the frame
    if (scene->cost.show && !textHook_) return false;                        // cost label would draw text into it
  }
  gpuPass_ = true;
  render(cam, opt, scene);
  gpuPass_ = false;
  if (!terrainLayerValid_ || terrainLayer_.size() != size_t(fbW_) * fbH_) return false;
  gpuItems_.clear();
  gpuItems_.reserve(items_.size());
  for (const Item& it : items_) {
    const SpriteImage& s = *it.spr;
    GpuSpriteItem g;
    g.spr = it.spr;
    g.plane = (it.plane && it.plane->w == s.img.w && it.plane->h == s.img.h) ? it.plane : nullptr;
    g.sc = it.dbl ? 2 : 1;
    g.x0 = it.ax - s.L * g.sc; g.y0 = it.ay - s.T * g.sc;
    g.w = s.img.w * g.sc; g.h = s.img.h * g.sc;
    g.m2 = it.m2;
    g.blendAll = g.plane || it.m2 < 32;
    gpuItems_.push_back(g);
  }
  out.w = fbW_; out.h = fbH_;
  out.base = terrainLayer_.data();
  out.baseGen = terrainLayerGen_;
  out.items = &gpuItems_;
  return true;
}

void Renderer::render(const Camera& cam, const RenderOptions& opt, const RenderScene* scene) {
  if (fbW_ <= 0 || fbH_ <= 0) return;
  if (!world_) { std::fill(fb_.begin(), fb_.end(), 0xFF302820u); return; }
  const int zoom = std::max(0, std::min(4, cam.zoom));
  computeOrigin(cam);
  using StageClock = std::chrono::steady_clock;
  auto stageMs = [](StageClock::time_point a, StageClock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  const auto st0 = StageClock::now();
  // Serial setup: caches, shared terrain state and the sorted draw list.
  if (opt.background) loadBackground(zoom);
  // Terrain layer cache: decide hit/miss before prepareTerrain, which only feeds the terrain pass.
  const bool gpuTerrainWillRun = opt.terrain && gpuTerrainWanted_ && gpuTerrainFn_;
  bool layerHit = false, layerStore = false, layerShift = false;
  int sdx = 0, sdy = 0;
  double keyMs = 0, itemsBuildMs = 0;
  if (terrainCacheOn_ && opt.terrain && !gpuTerrainWillRun) {
    const auto k0 = StageClock::now();
    const uint64_t key = terrainLayerKey(cam, opt);
    keyMs = stageMs(k0, StageClock::now());
    if (terrainLayerValid_ && key == terrainLayerKey_ && terrainLayer_.size() == fb_.size()) {
      // Same content, zoom, rotation and size. The terrain pass is exactly translation-invariant (screen geometry is
      // an integer lattice around the integer origin), so a pan is the cached pixels moved by the origin delta plus
      // freshly drawn strips where new ground came into view.
      sdx = originX_ - terrainLayerOX_; sdy = originY_ - terrainLayerOY_;
      if (sdx == 0 && sdy == 0) layerHit = true;
      else {
        const double keptX = std::max(0.0, 1.0 - double(std::abs(sdx)) / double(fbW_));
        const double keptY = std::max(0.0, 1.0 - double(std::abs(sdy)) / double(fbH_));
        if (keptX * keptY >= 0.4) layerShift = true;   // below that a full redraw is as cheap as copy + strips
      }
    }
    if (!layerHit && !layerShift) {
      layerStore = true;
      terrainLayerKey_ = key;
      terrainLayerValid_ = false;
      if (terrainLayer_.size() != fb_.size()) terrainLayer_.assign(fb_.size(), 0);
    }
    if (layerShift && terrainLayerNew_.size() != fb_.size()) terrainLayerNew_.assign(fb_.size(), 0);
  }
  if (opt.terrain) { if (layerHit) ensureShade(); else prepareTerrain(opt); }
  const auto bi0 = StageClock::now();
  buildItems(cam, opt, scene);
  applySceneTints(cam, scene);
  itemsBuildMs = stageMs(bi0, StageClock::now());
  stages_ = StageTimes();
  stages_.layerKeyMs = keyMs; stages_.buildItemsMs = itemsBuildMs;
  stages_.terrainLayerHit = layerHit;
  stages_.terrainLayerShift = layerShift;
  stages_.setupMs = stageMs(st0, StageClock::now());

  // Phase 2: optional GPU terrain fill (CPU builds triangle list; host compute writes pixels).
  bool gpuTerrain = false;
  if (opt.terrain && gpuTerrainWanted_ && gpuTerrainFn_) {
    std::fill(fb_.begin(), fb_.end(), 0xFF302820u);
    if (opt.background) drawBackground(zoom, 0, fbH_);
    gpuTris_.clear();
    collectTerrainTris(cam, opt, gpuTris_);
    gpuTerrain = gpuTerrainFn_(fb_.data(), fbW_, fbH_, zoom, noiseLand_, noiseEdge_,
                               gpuTris_.data(), int(gpuTris_.size()));
  }

  // Rasterise in horizontal bands on the worker pool. Each band replays the same primitives in the same
  // order, clipped to its rows, so the image is identical to a single-threaded render.
  WorkerPool& pool = WorkerPool::shared();
  const int bands = bands_ > 0 ? std::min(bands_, fbH_) : fbH_ < 128 ? 1 : std::min(fbH_ / 32, pool.threads() * 2);
  std::atomic<int64_t> bgNs{0}, terrainNs{0}, itemsNs{0};
  auto nsSince = [](StageClock::time_point a) { return std::chrono::duration_cast<std::chrono::nanoseconds>(StageClock::now() - a).count(); };
  const auto sb0 = StageClock::now();
  pool.run(bands, [&](int b) {
    const int y0 = fbH_ * b / bands, y1 = fbH_ * (b + 1) / bands;
    auto t = StageClock::now();
    const size_t rowOff = size_t(y0) * fbW_, rowN = size_t(y1 - y0) * fbW_;
    if (layerHit) {
      // Cached background + terrain + grid + edge faces for these rows (the GPU pass reads the layer directly).
      if (!gpuPass_) std::memcpy(fb_.data() + rowOff, terrainLayer_.data() + rowOff, rowN * sizeof(uint32_t));
      terrainNs += nsSince(t); t = StageClock::now();
    } else if (layerShift) {
      // Panned: B(x, y) = A(x - sdx, y - sdy). Copy what stays visible, redraw the exposed rows and columns.
      const int xa = std::max(0, sdx), xb = std::min(fbW_, fbW_ + sdx);
      for (int y = y0; y < y1; y++) {
        const int sy = y - sdy;
        if (sy < 0 || sy >= fbH_) continue;
        std::memcpy(fb_.data() + size_t(y) * fbW_ + xa, terrainLayer_.data() + size_t(sy) * fbW_ + (xa - sdx), size_t(xb - xa) * sizeof(uint32_t));
      }
      auto region = [&](int ya, int yb, int xs, int xe) {
        if (ya >= yb || xs >= xe) return;
        for (int y = ya; y < yb; y++)
          std::fill(fb_.begin() + ptrdiff_t(y) * fbW_ + std::max(0, xs), fb_.begin() + ptrdiff_t(y) * fbW_ + std::min(fbW_, xe), 0xFF302820u);
        if (opt.background) drawBackground(zoom, ya, yb, xs, xe);
        drawTerrain(cam, opt, ya, yb, xs, xe);
        if (opt.edgeFaces) drawEdgeFaces(cam, opt, ya, yb, xs, xe);
      };
      // Exposed rows span the full width; the exposed columns cover the remaining rows.
      const int ry0 = sdy > 0 ? 0 : fbH_ + sdy, ry1 = sdy > 0 ? sdy : fbH_;
      const int ra = std::max(y0, ry0), rb = std::min(y1, ry1);
      if (sdy != 0) region(ra, rb, 0, fbW_);
      if (sdx != 0) {
        const int cxs = sdx > 0 ? 0 : fbW_ + sdx, cxe = sdx > 0 ? sdx : fbW_;
        if (sdy == 0) region(y0, y1, cxs, cxe);
        else if (sdy > 0) region(std::max(y0, sdy), y1, cxs, cxe);
        else region(y0, std::min(y1, fbH_ + sdy), cxs, cxe);
      }
      std::memcpy(terrainLayerNew_.data() + rowOff, fb_.data() + rowOff, rowN * sizeof(uint32_t));
      terrainNs += nsSince(t); t = StageClock::now();
    } else {
      if (!gpuTerrain) {
        std::fill(fb_.begin() + ptrdiff_t(y0) * fbW_, fb_.begin() + ptrdiff_t(y1) * fbW_, 0xFF302820u);
        if (opt.background) drawBackground(zoom, y0, y1);
      }
      bgNs += nsSince(t); t = StageClock::now();
      if (opt.terrain) {
        if (gpuTerrain) {
          if (opt.grid) drawTerrainGrid(cam, opt, y0, y1);
          if (opt.edgeFaces) drawEdgeFaces(cam, opt, y0, y1);
        } else {
          drawTerrain(cam, opt, y0, y1);
          if (opt.edgeFaces) drawEdgeFaces(cam, opt, y0, y1);
        }
      }
      if (layerStore) std::memcpy(terrainLayer_.data() + rowOff, fb_.data() + rowOff, rowN * sizeof(uint32_t));
      terrainNs += nsSince(t); t = StageClock::now();
    }
    if (!gpuPass_) {
      if (scene) drawGroundTints(cam, *scene, y0, y1);
      drawItems(opt, y0, y1);
    }
    itemsNs += nsSince(t);
  });
  stages_.bandsWallMs = stageMs(sb0, StageClock::now());
  if (layerStore) { terrainLayerValid_ = true; terrainLayerOX_ = originX_; terrainLayerOY_ = originY_; ++terrainLayerGen_; }
  if (layerShift) { terrainLayer_.swap(terrainLayerNew_); terrainLayerOX_ = originX_; terrainLayerOY_ = originY_; ++terrainLayerGen_; }
  stages_.bgCpuMs = double(bgNs.load()) / 1e6;
  stages_.terrainCpuMs = double(terrainNs.load()) / 1e6;
  stages_.itemsCpuMs = double(itemsNs.load()) / 1e6;
  const auto stail = StageClock::now();
  if (opt.objects && !(data_ || spriteFn_)) drawFallbackBoxes(cam);
  label_ = CostLabel();
  if (scene) {
    drawPreviews(cam, *scene);
    if (scene->cost.show && world_->inTiles(scene->cost.tileX, scene->cost.tileZ)) {
      label_ = scene->cost;
      int alt = world_->altAt(scene->cost.tileX, scene->cost.tileZ);
      projectSub(cam, scene->cost.tileX * 256 + 128, scene->cost.tileZ * 256 + 128, alt * 256, labelX_, labelY_);
      labelX_ -= 2; labelY_ += 40;   // observed: label box top-left sits (-2, +40) from the footprint centre
      std::string text = "$" + groupDigits(scene->cost.cost);
      if (textHook_) textHook_(labelX_, labelY_, text, scene->cost.affordable);
      else if (costFont_ && costFont_->loaded())
        costFont_->draw(fb_, fbW_, fbH_, labelX_, labelY_, text, scene->cost.affordable ? rgba(255, 255, 255) : rgba(255, 80, 80));
    }
  }
  stages_.tailMs = stageMs(stail, StageClock::now());
}

// ======================================================================================
// Picking (spec 3)
// ======================================================================================

namespace {
bool inQuad(const float* px, const float* py, float x, float y) {
  // Order: top(0), right(1), bottom(3), left(2).
  static const int ord[4] = {0, 1, 3, 2};
  bool pos = false, neg = false;
  for (int i = 0; i < 4; i++) {
    int a = ord[i], b = ord[(i + 1) & 3];
    float cr = (px[b] - px[a]) * (y - py[a]) - (py[b] - py[a]) * (x - px[a]);
    if (cr > 0) pos = true; else if (cr < 0) neg = true;
  }
  return !(pos && neg);
}
}  // namespace

bool Renderer::pickGround(const Camera& cam, int sx, int sy, int& tx, int& tz) {
  if (!world_) return false;
  computeOrigin(cam);
  ensureShade();
  const World& w = *world_;
  const float Wt = 8.f * float(1 << cam.zoom), Ht = Wt / 2, A = float(1 << cam.zoom);
  uint32_t bestKey = 0xFFFFFFFFu; bool found = false; int bx = 0, bz = 0;
  for (int hh = 0; hh <= maxAlt_ + 1; hh++) {
    float py = float(sy - originY_) + float(hh) * A, px = float(sx - originX_);
    float uf = (py / (Ht / 2) - px / (Wt / 2)) / 2, vf = (py / (Ht / 2) + px / (Wt / 2)) / 2;
    int iu = int(std::floor(uf)), iv = int(std::floor(vf));
    for (int du = -1; du <= 1; du++)
      for (int dv = -1; dv <= 1; dv++) {
        int u = iu + du, v = iv + dv;
        if (u < 0 || v < 0 || u >= w.W || v >= w.H) continue;
        float qx[4], qy[4]; int wx[4], wz[4];
        tileQuad(cam, u, v, qx, qy, wx, wz);
        float hsum = 0;
        for (int i = 0; i < 4; i++) {
          int h = std::max(w.altAt(wx[i], wz[i]), w.waterAt(wx[i], wz[i]));
          qy[i] -= float(h) * A; hsum += float(h);
        }
        if (!inQuad(qx, qy, float(sx), float(sy) + 0.5f)) continue;
        uint32_t key = 0x40000000u - 0x4000u * uint32_t(u + v) - 0x40u * uint32_t(hsum / 4);
        if (key < bestKey) {
          bestKey = key; found = true;
          int tx0 = std::min(std::min(wx[0], wx[1]), std::min(wx[2], wx[3])), tz0 = std::min(std::min(wz[0], wz[1]), std::min(wz[2], wz[3]));
          bx = tx0; bz = tz0;
        }
      }
  }
  if (!found) return false;
  tx = bx; tz = bz;
  return true;
}

bool Renderer::pick(const Camera& cam, int sx, int sy, PickResult& out) {
  out = PickResult();
  if (!world_) return false;
  computeOrigin(cam);
  ensureShade();
  const World& w = *world_;
  if (!itemsValid_ || itemsCam_.u != cam.u || itemsCam_.v != cam.v || itemsCam_.zoom != cam.zoom || itemsCam_.rot != cam.rot)
    buildItems(cam, itemsOpt_, nullptr);

  // Terrain candidate, skipping tiles occupied by non-ground objects.
  const float Wt = 8.f * float(1 << cam.zoom), Ht = Wt / 2, A = float(1 << cam.zoom);
  uint32_t bestKey = 0xFFFFFFFFu;
  bool found = false;
  PickResult best;
  for (int hh = 0; hh <= maxAlt_ + 1; hh++) {
    float py = float(sy - originY_) + float(hh) * A, px = float(sx - originX_);
    float uf = (py / (Ht / 2) - px / (Wt / 2)) / 2, vf = (py / (Ht / 2) + px / (Wt / 2)) / 2;
    int iu = int(std::floor(uf)), iv = int(std::floor(vf));
    for (int du = -1; du <= 1; du++)
      for (int dv = -1; dv <= 1; dv++) {
        int u = iu + du, v = iv + dv;
        if (u < 0 || v < 0 || u >= w.W || v >= w.H) continue;
        if (!occ_.empty() && occ_[size_t(u) * w.H + v] >= 0) continue;  // handled by sprite test
        float qx[4], qy[4]; int wx[4], wz[4];
        tileQuad(cam, u, v, qx, qy, wx, wz);
        float hsum = 0;
        for (int i = 0; i < 4; i++) {
          int h = std::max(w.altAt(wx[i], wz[i]), w.waterAt(wx[i], wz[i]));
          qy[i] -= float(h) * A; hsum += float(h);
        }
        if (!inQuad(qx, qy, float(sx), float(sy) + 0.5f)) continue;
        uint32_t key = 0x40000000u - 0x4000u * uint32_t(u + v) - 0x40u * uint32_t(hsum / 4);
        if (key < bestKey) {
          bestKey = key; found = true;
          int tx0 = std::min(std::min(wx[0], wx[1]), std::min(wx[2], wx[3])), tz0 = std::min(std::min(wz[0], wz[1]), std::min(wz[2], wz[3]));
          best = PickResult();
          best.hit = true; best.tileX = tx0; best.tileZ = tz0; best.subX = tx0 * 256; best.subZ = tz0 * 256;
          best.height = int32_t(w.altAt(tx0, tz0)) * 256;
        }
      }
  }
  // Sprite objects: per-pixel opacity.
  for (const auto& it : items_) {
    if (it.moving || it.groundLevel || !it.pickable || !it.spr) continue;
    if (it.pickKey > bestKey) continue;  // farther than the current best
    const SpriteImage& s = *it.spr;
    int sc = it.dbl ? 2 : 1;
    int lx = sx - (it.ax - s.L * sc), ly = sy - (it.ay - s.T * sc);
    if (lx < 0 || ly < 0 || lx >= s.img.w * sc || ly >= s.img.h * sc) continue;
    uint32_t p = s.img.px[size_t(ly / sc) * s.img.w + lx / sc];
    if (!(p >> 24)) continue;
    if (it.pickKey <= bestKey) {
      bestKey = it.pickKey; found = true;
      best = PickResult();
      best.hit = true; best.object = true; best.tileX = it.tileX; best.tileZ = it.tileZ;
      best.subX = it.subX; best.subZ = it.subZ; best.height = it.height; best.category = it.cat; best.spriteId = it.id;
    }
  }
  if (!found) return false;
  out = best;
  return w.inTiles(out.tileX, out.tileZ);
}

bool Renderer::pickTile(const Camera& cam, int sx, int sy, int& tx, int& tz) {
  PickResult r;
  if (!pick(cam, sx, sy, r)) return false;
  tx = r.tileX; tz = r.tileZ;
  return true;
}

}  // namespace m3k
