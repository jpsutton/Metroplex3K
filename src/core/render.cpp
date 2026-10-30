#include "core/render.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace m3k {

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

void Renderer::setWorld(const World* w, GameData* data) {
  world_ = w; data_ = data; shadeValid_ = false;
  pal_ = Palettes();
  loadPalettes();
  if (!noiseInit_) {
    uint32_t s = 12345;
    for (auto& n : noise_) { s = s * 1664525u + 1013904223u; n = float(int((s >> 16) % 3) - 1) * 8.f; }
    noiseInit_ = true;
  }
}

void Renderer::resize(int w, int h) { fbW_ = w; fbH_ = h; fb_.assign(size_t(w) * h, 0xFF000000u); }

void Renderer::loadPalettes() {
  if (!data_ || !world_) return;
  // Landscape -> palette instance from SC3CityScheme.ini [LandScapes] (4th field).
  uint32_t inst = 1;
  IniFile ini;
  if (data_->ini().load("SC3CityScheme.ini", ini)) {
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
  if (r) { try { pal_.land = decodeUiImage(r.data, r.available, false); } catch (const FormatError&) {} }
  fs::path wp = resolveCI(data_->appsDir(), "Res/Dirt/Palette/palWater.bmp");
  if (!wp.empty()) {
    Bytes b;
    if (readFile(wp, b)) { try { pal_.water = decodeBmp(b.data(), b.size(), false); } catch (const FormatError&) {} }
  }
  pal_.loaded = !pal_.land.empty() && pal_.land.w >= 32 && pal_.land.h >= 512;
}

void Renderer::computeShade() {
  const World& w = *world_;
  shade_.assign(size_t(w.W + 1) * (w.H + 1), 15);
  auto h = [&](int x, int z) { return float(w.altAt(x, z)); };
  const float s = w.heightScale, d = 16.0f;
  const float inv = 1.0f / std::sqrt(2.0f);
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
      const float PI = 3.14159265f;
      int t1 = int(std::acos(std::min(1.f, d1)) * 62.f / PI), t2 = int(std::acos(std::min(1.f, d2)) * 62.f / PI);
      shade_[w.vIdx(x, z)] = uint8_t(std::min(31, std::max(t1, t2)));
    }
  shadeValid_ = true;
}

void Renderer::farZoom(int z, float& r, float& g, float& b) const {
  if (z >= 3) return;
  static const int K[3][3] = {{49, 51, 53}, {33, 34, 35}, {16, 17, 18}};
  float* ch[3] = {&r, &g, &b};
  for (int i = 0; i < 3; i++) {
    int c = int(*ch[i]);
    int S = (c + 5) / 10;
    c = c + K[z][i] - S * (3 - z);
    *ch[i] = float(std::max(0, std::min(255, c)));
  }
}

void Renderer::fillTri(const Vtx& a, const Vtx& b, const Vtx& c, int seed, bool noise) {
  const Vtx* v[3] = {&a, &b, &c};
  std::sort(v, v + 3, [](const Vtx* p, const Vtx* q) { return p->y < q->y; });
  int y0 = int(std::ceil(v[0]->y)), y2 = int(std::floor(v[2]->y));
  y0 = std::max(y0, 0); y2 = std::min(y2, fbH_ - 1);
  for (int y = y0; y <= y2; y++) {
    float fy = float(y);
    auto edge = [&](const Vtx* p, const Vtx* q, float out[5]) {
      if (q->y == p->y) return false;
      float t = (fy - p->y) / (q->y - p->y);
      if (t < 0 || t > 1) return false;
      out[0] = p->x + (q->x - p->x) * t; out[1] = p->r + (q->r - p->r) * t;
      out[2] = p->g + (q->g - p->g) * t; out[3] = p->b + (q->b - p->b) * t;
      return true;
    };
    float e[3][5]; int n = 0;
    if (edge(v[0], v[2], e[n])) n++;
    if (edge(v[0], v[1], e[n])) n++;
    if (edge(v[1], v[2], e[n])) n++;
    if (n < 2) continue;
    // Pick the two extreme edge crossings.
    int li = 0, ri = 0;
    for (int i = 1; i < n; i++) { if (e[i][0] < e[li][0]) li = i; if (e[i][0] > e[ri][0]) ri = i; }
    if (li == ri) continue;
    float* L = e[li]; float* R = e[ri];
    int xa = int(std::ceil(L[0] - 0.5f)), xb = int(std::ceil(R[0] - 0.5f)) - 1;
    float span = float(xb - xa + 1);
    if (span <= 0) continue;
    int xs = std::max(xa, 0), xe = std::min(xb, fbW_ - 1);
    int ni = ((seed + 143 * (y & 1023)) % 1024 + 1024) % 1024;
    for (int x = xs; x <= xe; x++) {
      float t = (x - xa) / span;
      float r = L[1] + (R[1] - L[1]) * t, g = L[2] + (R[2] - L[2]) * t, bl = L[3] + (R[3] - L[3]) * t;
      if (noise) { r += noise_[(ni + x * 3) & 1023]; g += noise_[(ni + x * 3 + 1) & 1023]; bl += noise_[(ni + x * 3 + 2) & 1023]; }
      auto cl = [](float f) { return uint32_t(std::max(0.f, std::min(255.f, f))); };
      fb_[size_t(y) * fbW_ + x] = 0xFF000000u | (cl(bl) << 16) | (cl(g) << 8) | cl(r);
    }
  }
}

void Renderer::screenToView(const Camera& cam, int sx, int sy, float& u, float& v) const {
  const float Wt = 8.f * float(1 << cam.zoom), Ht = Wt / 2;
  float px = float(sx - originX_), py = float(sy - originY_);
  u = (py / (Ht / 2) - px / (Wt / 2)) / 2;
  v = (py / (Ht / 2) + px / (Wt / 2)) / 2;
}

bool Renderer::pickTile(const Camera& cam, int sx, int sy, int& tx, int& tz) const {
  if (!world_) return false;
  float u, v; screenToView(cam, sx, sy, u, v);
  // Refine for terrain height: iterate a few times using altitude at the guess.
  const float A = float(1 << cam.zoom);
  float hh = 0;
  for (int it = 0; it < 4; it++) {
    const float Wt = 8.f * float(1 << cam.zoom), Ht = Wt / 2;
    float py = float(sy - originY_) + hh * A, px = float(sx - originX_);
    u = (py / (Ht / 2) - px / (Wt / 2)) / 2; v = (py / (Ht / 2) + px / (Wt / 2)) / 2;
    int iu = int(std::floor(u)), iv = int(std::floor(v));
    int x, z; viewToWorldVertex(cam.rot, world_->W, world_->H, std::max(0, std::min(world_->W, iu)), std::max(0, std::min(world_->H, iv)), x, z);
    hh = world_->altAt(std::max(0, std::min(world_->W, x)), std::max(0, std::min(world_->H, z)));
  }
  int iu = int(std::floor(u)), iv = int(std::floor(v));
  if (iu < 0 || iv < 0 || iu >= world_->W || iv >= world_->H) return false;
  // View tile (iu, iv) -> world tile: map the tile's top vertex and adjust for rotation.
  int x, z;
  switch (cam.rot & 3) {
    case 0: x = iu; z = iv; break;
    case 1: x = iv; z = world_->W - 1 - iu; break;
    case 2: x = world_->W - 1 - iu; z = world_->H - 1 - iv; break;
    default: x = world_->H - 1 - iv; z = iu; break;
  }
  tx = x; tz = z;
  return world_->inTiles(x, z);
}

void Renderer::centreOnWorld(Camera& cam, float x, float z) const {
  // Convert world tile centre to view tile coordinates (continuous).
  float W = float(world_->W), H = float(world_->H);
  switch (cam.rot & 3) {
    case 0: cam.u = x; cam.v = z; break;
    case 1: cam.u = W - z; cam.v = x; break;
    case 2: cam.u = W - x; cam.v = H - z; break;
    default: cam.u = z; cam.v = H - x; break;
  }
}

void Renderer::drawTerrain(const Camera& cam, const RenderOptions& opt) {
  const World& w = *world_;
  if (!shadeValid_) computeShade();
  const int z = cam.zoom;
  const float Wt = 8.f * float(1 << z), Ht = Wt / 2, A = float(1 << z);
  // Visible view-tile range: project the screen corners back to view space (flat) with margin
  // for altitude.
  float u0, v0, u1, v1, u2, v2, u3, v3;
  screenToView(cam, 0, 0, u0, v0); screenToView(cam, fbW_, 0, u1, v1);
  screenToView(cam, 0, fbH_, u2, v2); screenToView(cam, fbW_, fbH_, u3, v3);
  int pad = 4 + int(256 * A / Ht) / 2;  // altitude margin
  int umin = int(std::floor(std::min(std::min(u0, u1), std::min(u2, u3)))) - 2;
  int umax = int(std::ceil(std::max(std::max(u0, u1), std::max(u2, u3)))) + 2 + pad;
  int vmin = int(std::floor(std::min(std::min(v0, v1), std::min(v2, v3)))) - 2 - pad;
  int vmax = int(std::ceil(std::max(std::max(v0, v1), std::max(v2, v3)))) + 2 + pad;
  umin = std::max(umin, 0); vmin = std::max(vmin, 0);
  umax = std::min(umax, w.W - 1); vmax = std::min(vmax, w.H - 1);

  auto landColour = [&](int x, int zz, float& r, float& g, float& b) {
    int alt = w.altAt(x, zz), wat = w.waterAt(x, zz);
    int shade = shade_[w.vIdx(x, zz)];
    if (pal_.have()) {
      int row = std::max(0, std::min(511, 256 + wat - alt));
      int col = std::min(31, shade / 2);
      uint32_t c = pal_.land.px[size_t(row) * pal_.land.w + col];
      r = float(c & 255); g = float((c >> 8) & 255); b = float((c >> 16) & 255);
    } else {
      // Fallback procedural palette: below water blue-ish sea bed, above water green to white.
      float lit = 0.55f + 0.45f * float(shade) / 31.f;
      if (alt <= wat) { float d = std::min(1.f, float(wat - alt) / 60.f); r = 70 * lit * (1 - d) + 20; g = 110 * lit * (1 - d) + 30; b = 130 * lit * (1 - d) + 60; }
      else {
        float hgt = std::min(1.f, float(alt - wat) / 120.f);
        r = (60 + 160 * hgt) * lit; g = (130 + 80 * hgt) * lit; b = (50 + 170 * hgt) * lit;
      }
    }
    farZoom(z, r, g, b);
  };
  auto waterColour = [&](int depth, float& r, float& g, float& b) {
    depth = std::max(0, std::min(255, depth));
    if (pal_.water.w > 0 && pal_.water.h >= 256) {
      uint32_t c = pal_.water.px[size_t(depth) * pal_.water.w];
      r = float(c & 255); g = float((c >> 8) & 255); b = float((c >> 16) & 255);
    } else { float d = 1.f - std::min(1.f, depth / 120.f); r = 40 + 30 * d; g = 90 + 60 * d; b = 150 + 70 * d; }
    farZoom(z, r, g, b);
  };

  for (int s = umin + vmin; s <= umax + vmax; s++) {
    for (int u = std::max(umin, s - vmax); u <= std::min(umax, s - vmin); u++) {
      int v = s - u;
      // Corners in view space: top (u,v), right (u,v+1), left (u+1,v), bottom (u+1,v+1).
      static const int cu[4] = {0, 0, 1, 1}, cv[4] = {0, 1, 0, 1};  // top, right, left, bottom
      int wx[4], wz[4];
      for (int i = 0; i < 4; i++) viewToWorldVertex(cam.rot, w.W, w.H, u + cu[i], v + cv[i], wx[i], wz[i]);
      Vtx vt[4], vw[4];
      bool allWet = true, anyWet = false;
      for (int i = 0; i < 4; i++) {
        int alt = w.altAt(wx[i], wz[i]), wat = w.waterAt(wx[i], wz[i]);
        float sx = float(originX_) + float((v + cv[i]) - (u + cu[i])) * (Wt / 2);
        float sy = float(originY_) + float((u + cu[i]) + (v + cv[i])) * (Ht / 2);
        vt[i] = {sx, sy - alt * A, 0, 0, 0};
        landColour(wx[i], wz[i], vt[i].r, vt[i].g, vt[i].b);
        bool wet = alt < wat;
        anyWet |= wet; allWet &= (alt <= wat);
        vw[i] = {sx, sy - std::max(alt, wat) * A, 0, 0, 0};
        waterColour(wat - alt, vw[i].r, vw[i].g, vw[i].b);
      }
      // cull quickly
      float minY = std::min(std::min(vt[0].y, vt[1].y), std::min(vt[2].y, vt[3].y));
      float maxY = std::max(std::max(vt[0].y, vt[1].y), std::max(vt[2].y, vt[3].y));
      if (maxY < 0 || minY > fbH_) continue;
      int wx0, wz0; viewToWorldVertex(cam.rot, w.W, w.H, u, v, wx0, wz0);
      int seed = ((-169 * wz0 - 327 * wx0) % 1024 + 1024) % 1024;
      // Split: top(0), right(1), left(2), bottom(3). Left/right equal height -> split along L-R.
      auto fillQuad = [&](const Vtx* P, bool noise) {
        float hl = float(w.altAt(wx[2], wz[2])), hr = float(w.altAt(wx[1], wz[1]));
        if (hl == hr) { fillTri(P[2], P[0], P[1], seed, noise); fillTri(P[2], P[1], P[3], seed, noise); }
        else { fillTri(P[0], P[2], P[3], seed, noise); fillTri(P[0], P[3], P[1], seed, noise); }
      };
      fillQuad(vt, true);
      const Vtx* P = vt;
      if (allWet) { fillQuad(vw, false); P = vw; }
      if (opt.grid) {
        auto line = [&](const Vtx& a, const Vtx& b) {
          int steps = int(std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y)));
          for (int i = 0; i <= steps; i++) {
            float t = steps ? float(i) / steps : 0;
            int px = int(a.x + (b.x - a.x) * t), py = int(a.y + (b.y - a.y) * t);
            if (px < 0 || py < 0 || px >= fbW_ || py >= fbH_) continue;
            uint32_t& p = fb_[size_t(py) * fbW_ + px];
            auto dk = [](uint32_t c) { return c > 16 ? c - 16 : 0; };
            p = 0xFF000000u | (dk((p >> 16) & 255) << 16) | (dk((p >> 8) & 255) << 8) | dk(p & 255);
          }
        };
        line(P[2], P[0]); line(P[0], P[1]);
      }
    }
  }
}

void Renderer::blitSprite(const SpriteImage& s, int ax, int ay, bool dbl) {
  int sc = dbl ? 2 : 1;
  int x0 = ax - s.L * sc, y0 = ay - s.T * sc;
  for (int y = 0; y < s.img.h * sc; y++) {
    int dy = y0 + y;
    if (dy < 0 || dy >= fbH_) continue;
    const uint32_t* row = &s.img.px[size_t(y / sc) * s.img.w];
    for (int x = 0; x < s.img.w * sc; x++) {
      int dx = x0 + x;
      if (dx < 0 || dx >= fbW_) continue;
      uint32_t p = row[x / sc];
      if (p >> 24) fb_[size_t(dy) * fbW_ + dx] = p;
    }
  }
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

void Renderer::drawObjects(const Camera& cam) {
  if (!data_ || !world_) return;
  const World& w = *world_;
  const int z = cam.zoom;
  const float Wt = 8.f * float(1 << z), Ht = Wt / 2, A = float(1 << z);

  struct Item { uint32_t key; int ax, ay; const SpriteImage* spr; bool dbl; };
  std::vector<Item> items;
  auto vertexScreen = [&](int u, int v, int alt, int& sx, int& sy) {
    sx = originX_ + int(float(v - u) * (Wt / 2));
    sy = originY_ + int(float(u + v) * (Ht / 2)) - int(alt * A);
  };
  auto add = [&](uint32_t occId, bool net, bool port, int x, int zz, int y, int rot, int sizeX, int sizeZ, uint32_t keyBias) {
    const OccAttr* oa = data_->occupant(occId, net, port);
    if (!oa) return;
    const CsAttrib* cs = data_->csattrib(oa->appearance);
    if (!cs) return;
    int slot = z;
    if (cs->zoomCount < 5) { slot = z - (5 - cs->zoomCount); if (slot < 0) return; }
    if (slot >= cs->zoomCount) slot = cs->zoomCount - 1;
    int orient = (cs->zoomSetCount >= 4 && cs->rotCount == 4) ? ((rot + cam.rot) & 3) : 0;
    const CsFrame* fr = cs->frame(0, slot, orient);
    if (!fr) return;
    const SpriteImage* sp = data_->sprite(fr->category, fr->spriteId);
    if (!sp) return;
    // Footprint corners in view space.
    int us[4], vs[4];
    int cxs[4] = {x, x + sizeX, x, x + sizeX}, czs[4] = {zz, zz, zz + sizeZ, zz + sizeZ};
    for (int i = 0; i < 4; i++) worldToViewVertex(cam.rot, w.W, w.H, cxs[i], czs[i], us[i], vs[i]);
    int umin = *std::min_element(us, us + 4), umax = *std::max_element(us, us + 4);
    int vmin = *std::min_element(vs, vs + 4), vmax = *std::max_element(vs, vs + 4);
    int ax, ay;
    vertexScreen(umax, vmin, y, ax, ay);  // left corner of the footprint diamond
    // Cull.
    int pw = sp->img.w * (fr->flags == 2 ? 2 : 1), ph = sp->img.h * (fr->flags == 2 ? 2 : 1);
    if (ax + pw < -64 || ax - pw > fbW_ + 64 || ay - ph > fbH_ + 64 || ay + ph < -64) return;
    int front = (umax - 1) + (vmax - 1);
    uint32_t key = 0x3FFFC000u - 0x4000u * uint32_t(front + 0) - 0x100u * uint32_t(y / 4) + keyBias;
    (void)umin;
    items.push_back({key, ax, ay, sp, fr->flags == 2});
  };

  for (const auto& p : w.pipes) (void)p;  // underground layers not drawn in the surface view
  for (const auto& np : w.surface) add(np.pieceId, true, false, np.x, np.z, np.y, np.rot, 1, 1, 1);
  for (const auto& o : w.occupants) {
    if (o.keyType == T_OCC_ATTR_PORT) add(o.occId, false, true, o.x, o.z, o.y, o.rot, o.sizeX, o.sizeZ, 0);
    else add(o.occId, false, false, o.x, o.z, o.y, o.rot, o.sizeX, o.sizeZ, 0);
  }
  for (int x = 0; x < w.W; x++)
    for (int zz = 0; zz < w.H; zz++) {
      uint8_t c = w.flora[w.tIdx(x, zz)];
      if (!c) continue;
      uint32_t inst = floraInstance(c, uint32_t(x * 73856093u ^ zz * 19349663u) >> 7);
      if (inst) add(inst, false, false, x, zz, w.altAt(x, zz), 0, 1, 1, 0);
    }
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.key > b.key; });
  for (const auto& it : items) blitSprite(*it.spr, it.ax, it.ay, it.dbl);
}

void Renderer::render(const Camera& cam, const RenderOptions& opt) {
  if (fbW_ <= 0 || fbH_ <= 0) return;
  std::fill(fb_.begin(), fb_.end(), 0xFF302820u);
  if (!world_) return;
  curZoom_ = cam.zoom;
  const float Wt = 8.f * float(1 << cam.zoom), Ht = Wt / 2;
  // Screen position of view vertex (u,v): origin + ((v-u)*Wt/2, (u+v)*Ht/2). Centre the camera.
  originX_ = fbW_ / 2 - int((cam.v - cam.u) * (Wt / 2));
  int cx, cz;
  viewToWorldVertex(cam.rot, world_->W, world_->H, std::max(0, std::min(world_->W, int(cam.u))), std::max(0, std::min(world_->H, int(cam.v))), cx, cz);
  cx = std::max(0, std::min(world_->W, cx)); cz = std::max(0, std::min(world_->H, cz));
  originY_ = fbH_ / 2 - int((cam.u + cam.v) * (Ht / 2)) + int(world_->altAt(cx, cz) * float(1 << cam.zoom));
  if (opt.terrain) drawTerrain(cam, opt);
  if (opt.objects) drawObjects(cam);
}

}  // namespace m3k
