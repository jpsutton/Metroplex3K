#include "core/render.h"

#include <algorithm>
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
  if (int(subst_.size()) <= footprint) subst_.resize(footprint + 1, Subst{0xFFFFFFFFu, 0});
  subst_[footprint] = {category, spriteId};
}

void Renderer::setTileTint(int x, int z, uint8_t row) {
  if (!world_ || !world_->inTiles(x, z)) return;
  if (tint_.size() != size_t(world_->W) * world_->H) tint_.assign(size_t(world_->W) * world_->H, 0);
  tint_[world_->tIdx(x, z)] = row;
}

void Renderer::setWorld(const World* w, GameData* data) {
  world_ = w; data_ = data; shadeValid_ = false; itemsValid_ = false;
  tint_.clear(); siteCache_.clear(); bldIniTried_ = false; bldIniOk_ = false;
  for (int i = 0; i < 5; i++) { back_[i] = Image(); backTried_[i] = false; }
  pal_ = TerrainPalettes(); palOverride_ = false;
  makeFallbackPalettes();
  loadPalettes();
  if (!noiseInit_) setNoiseSeed(0x6D33B1u);
}

void Renderer::resize(int w, int h) { fbW_ = w; fbH_ = h; fb_.assign(size_t(w) * h, 0xFF000000u); }

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
    float dr = (R[0] - L[0]) * inv, dg = (R[1] - L[1]) * inv, db = (R[2] - L[2]) * inv;
    int ni = ((seed + 143 * (y - ytop)) % 1024 + 1024) % 1024;
    int x0 = std::max(xa, 0), x1 = std::min(xe, c.W);
    for (int x = x0; x < x1; x++) {
      int i = x - xa;
      int r = int(L[0] + dr * i), g = int(L[1] + dg * i), b = int(L[2] + db * i);
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
    float dr = (R[0] - L[0]) * inv, dg = (R[1] - L[1]) * inv, db = (R[2] - L[2]) * inv;
    int ni = ((seed + 143 * (y - ytop)) % 1024 + 1024) % 1024;
    int x0 = std::max(xa, 0), x1 = std::min(xe, c.W);
    for (int x = x0; x < x1; x++) {
      int i = x - xa;
      int P[3] = {int(L[0] + dr * i), int(L[1] + dg * i), int(L[2] + db * i)};
      int d2 = 0;
      for (int k = 0; k < 3; k++) { int d = P[k] - key[k]; d2 += d * d; }
      int o[3];
      if (d2 <= 256) {
        int nz = c.noiseE[(ni + 3 * i) & 1023];
        for (int k = 0; k < 3; k++) o[k] = rep[k] + nz;
      } else {
        for (int k = 0; k < 3; k++) {
          int v = d2 < 1024 ? (rep[k] * (1024 - d2) + P[k] * (d2 - 256)) / 768 : P[k];
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
    int x0 = std::max(xa, 0), x1 = std::min(xe, c.W);
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
    int x0 = std::max(xa, 0), x1 = std::min(xe, c.W);
    for (int x = x0; x < x1; x++) {
      uint32_t& d = c.fb[size_t(y) * c.W + x];
      int dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255;
      d = pack(dr + (r - dr) * alpha / 255, dg + (g - dg) * alpha / 255, db + (b - db) * alpha / 255);
    }
  });
}

void darken(const Ctx& c, int x, int y, int delta) {
  if (x < 0 || y < c.Y0 || x >= c.W || y >= c.H) return;
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

void Renderer::drawBackground(int zoom, int y0, int y1) {
  const Image& im = back_[zoom];
  if (im.empty()) return;
  for (int y = y0; y < y1; y++) {
    int ty = ((y - originY_) % im.h + im.h) % im.h;
    const uint32_t* row = &im.px[size_t(ty) * im.w];
    uint32_t* dst = &fb_[size_t(y) * fbW_];
    int tx = ((0 - originX_) % im.w + im.w) % im.w;
    for (int x = 0; x < fbW_; x++) {
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

void Renderer::drawTerrain(const Camera& cam, const RenderOptions& opt, int y0, int y1) {
  const World& w = *world_;
  const int z = cam.zoom;
  const float A = float(1 << z);
  Ctx ctx{fb_.data(), fbW_, y1, z, noiseLand_, noiseEdge_, y0};

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
      if (maxY < float(y0) || minY > float(y1) || maxX < 0 || minX > fbW_) continue;
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
        for (int i = 0; i < 4; i++) { dry[i] = alt[i] > wat[i]; anyDry |= dry[i]; allDry &= dry[i]; }
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

// ======================================================================================
// Map-edge faces (spec 6.5)
// ======================================================================================

void Renderer::drawEdgeFaces(const Camera& cam, int y0, int y1) {
  const World& w = *world_;
  const int z = cam.zoom;
  const float Wt = 8.f * float(1 << z), Ht = Wt / 2, A = float(1 << z);
  Ctx ctx{fb_.data(), fbW_, y1, z, noiseLand_, noiseEdge_, y0};
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
    bool waterPart = wat[0] > alt[0] || wat[1] > alt[1];
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

void Renderer::buildItems(const Camera& cam, const RenderOptions& opt, const RenderScene* scene) {
  items_.clear();
  occ_.clear();
  itemsCam_ = cam; itemsOpt_ = opt; itemsValid_ = true;
  if (!world_ || !opt.objects) return;
  const World& w = *world_;
  const int z = cam.zoom;
  const int Wh = 4 << z, Hh = 2 << z, A = 1 << z;
  const bool haveSprites = data_ || spriteFn_;
  if (!haveSprites) return;

  // `flat` holds ground-level network pieces (spec 25 5.1: surface, drawn before every object); `statics` the
  // depth-sorted objects that moving sprites are merged into.
  std::vector<Item> statics, movers, flat;
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
    it.ax = originX_ + (vmin - umax) * Wh;
    it.ay = originY_ + (umax + vmin) * Hh - y * A;
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
    if (cullOut(it.ax, it.ay, sp->img.w * sc, sp->img.h * sc, sp->L * sc, sp->T * sc)) return;
    it.spr = sp; it.dbl = dbl; it.groundLevel = ground;
    if (ground) { it.key += 1; it.pickKey -= 1; }
    it.cat = fr ? fr->category : 0; it.id = fr ? fr->spriteId : 0;
    // A ground-level piece whose image stays near the tile diamond (road, rail, slope; not pylons, ramps or
    // bridges) lies on the surface: nothing moving can be behind it, so it must never paint over a vehicle that
    // overhangs from the tile behind. The anchor is the diamond centre: flat images reach about one tile height
    // above it, tall pieces several.
    const bool surface = ground && sp->T <= 3 * Hh;
    (surface ? flat : statics).push_back(it);
    if (!cs || !fr) return;
    // Scripted overlay (SprInstCLSID 0x7315): the base image has a hole the script's frames fill, so draw
    // the first frame even when animation is off. These records do not use the a * AnimSetCount layout.
    if (!cs->scripts.empty()) {
      int slot = z;
      if (cs->zoomCount < 5) slot = z - (5 - cs->zoomCount);
      int orient = (cs->zoomSetCount >= 4 && cs->rotCount == 4) ? ((rot + cam.rot) & 3) : 0;
      const CsScript* scr = cs->script(slot, orient);
      if (!scr) return;
      int idx = scr->start + (opt.animTick >= 0 ? opt.animTick % scr->count : 0);
      if (idx < 0 || idx >= int(cs->frames.size())) return;
      const CsFrame& of = cs->frames[size_t(idx)];
      const SpriteImage* os = getSprite(of.category, of.spriteId);
      if (!os) return;
      Item ov = it;
      ov.spr = os; ov.dbl = of.flags == 2; ov.pickable = false;
      ov.ax += scr->offX; ov.ay += scr->offY;
      statics.push_back(ov);
      return;
    }
    // Animated overlay frame (spec 7): extra frame drawn over the base image.
    if (cs->animFrames > 1 && opt.animTick >= 0) {
      int a = 1 + opt.animTick % (cs->animFrames - 1);
      int orient = (cs->zoomSetCount >= 4 && cs->rotCount == 4) ? ((rot + cam.rot) & 3) : 0;
      const CsFrame* of = frameFor(cs, a, orient);
      if (of) {
        const SpriteImage* os = getSprite(of->category, of->spriteId);
        if (os) {
          Item ov = it;
          ov.spr = os; ov.dbl = of->flags == 2; ov.pickable = false;
          statics.push_back(ov);
        }
      }
    }
  };

  auto ownFlag = [&]() { return (opt.ownImageFlags & (0x10 << z)) != 0; };

  auto addOccupant = [&](uint32_t occId, bool net, bool port, int x, int zz, int y, int rot, int sizeX, int sizeZ, bool ground) {
    if (!data_) return;
    const OccAttr* oa = data_->occupant(occId, net, port);
    if (!oa) return;
    addStatic(oa->appearance, ownFlag(), std::max(sizeX, sizeZ), x, zz, y, rot, sizeX, sizeZ, ground);
  };

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
      if (opt.constructionSites && o.state != 1 && o.state != 2 && data_) {
        ResKey rk;
        if (siteOccupant(o.state, std::max(o.sizeX, o.sizeZ), o.agentType, rk)) { id = rk.instance; port = rk.type == T_OCC_ATTR_PORT; }
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
        statics.push_back(it);
      }
    }
  }

  std::stable_sort(flat.begin(), flat.end(), [](const Item& a, const Item& b) { return a.key > b.key; });
  std::stable_sort(statics.begin(), statics.end(), [](const Item& a, const Item& b) { return a.key > b.key; });
  std::stable_sort(movers.begin(), movers.end(), [](const Item& a, const Item& b) { return a.key > b.key; });

  // Merge moving objects (spec 5.2): after statics wholly behind, before statics wholly in front,
  // otherwise by key; ties draw static first (spec 29 item 3.3).
  std::vector<size_t> pos(movers.size());
  for (size_t m = 0; m < movers.size(); m++) {
    const Item& mv = movers[m];
    size_t lo = 0, hi = statics.size(), kpos = statics.size();
    bool haveKpos = false, haveHi = false;
    for (size_t i = 0; i < statics.size(); i++) {
      const Item& s = statics[i];
      if (!haveKpos && s.key < mv.key) { kpos = i; haveKpos = true; }
      if (s.boxA1 <= mv.posA && s.boxB1 <= mv.posB) lo = i + 1;
      if (!haveHi && s.boxA0 >= mv.posA && s.boxB0 >= mv.posB) { hi = i; haveHi = true; }
    }
    pos[m] = lo <= hi ? std::max(lo, std::min(kpos, hi)) : kpos;
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

void Renderer::render(const Camera& cam, const RenderOptions& opt, const RenderScene* scene) {
  if (fbW_ <= 0 || fbH_ <= 0) return;
  if (!world_) { std::fill(fb_.begin(), fb_.end(), 0xFF302820u); return; }
  const int zoom = std::max(0, std::min(4, cam.zoom));
  computeOrigin(cam);
  // Serial setup: caches, shared terrain state and the sorted draw list.
  if (opt.background) loadBackground(zoom);
  if (opt.terrain) prepareTerrain(opt);
  buildItems(cam, opt, scene);
  // Rasterise in horizontal bands on the worker pool. Each band replays the same primitives in the same
  // order, clipped to its rows, so the image is identical to a single-threaded render.
  WorkerPool& pool = WorkerPool::shared();
  const int bands = bands_ > 0 ? std::min(bands_, fbH_) : fbH_ < 128 ? 1 : std::min(fbH_ / 32, pool.threads() * 2);
  pool.run(bands, [&](int b) {
    const int y0 = fbH_ * b / bands, y1 = fbH_ * (b + 1) / bands;
    std::fill(fb_.begin() + ptrdiff_t(y0) * fbW_, fb_.begin() + ptrdiff_t(y1) * fbW_, 0xFF302820u);
    if (opt.background) drawBackground(zoom, y0, y1);
    if (opt.terrain) {
      drawTerrain(cam, opt, y0, y1);
      if (opt.edgeFaces) drawEdgeFaces(cam, y0, y1);
    }
    drawItems(opt, y0, y1);
  });
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
