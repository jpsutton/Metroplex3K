#include "core/terrain_gen.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace m3k {
namespace {

struct Gen {
  int R, C, M, qR, qC, L = 0;
  std::vector<uint8_t> g, w, t, m;
  Lcg rng;
  uint32_t s2 = 0;

  int rand(int n) { return n <= 0 ? 0 : int(rng.below(uint32_t(n))); }
  int randRange(int lo, int hi) { return lo + rand(hi - lo); }
  int centreRand(int lo, int hi) {
    int half = (hi - lo) / 2;
    int a = rand(half), b = rand(half);
    int mm = std::min(a, b);
    if (rand(2) != 0) mm = -mm;
    return lo + half + mm;
  }
  int lowRand(int lo, int hi) { int a = rand(hi - lo), b = rand(hi - lo); return lo + std::min(a, b); }
  static int clamp(int v) { return std::max(16, std::min(240, v)); }
  int jitter2(int a, int b, int s) {
    if (s < 2) return a;
    int d = s - 1, x = std::max(a, b) - d, y = std::min(a, b) + d, v;
    if (x < y) v = centreRand(x, y + 1); else v = (x + y) / 2;
    return clamp(v);
  }
  int jitter4(int a, int b, int c, int e, int s) {
    if (s < 2) return a;
    int d = s - 1, x = std::max(std::max(a, b), std::max(c, e)) - d, y = std::min(std::min(a, b), std::min(c, e)) + d, v;
    if (x < y) v = centreRand(x, y + 1); else v = (x + y) / 2;
    return clamp(v);
  }
  double uniformReal(double a, double b) {
    if (s2 == 0) s2 = 0x12345678;
    s2 = s2 * 0x278DDE6Du;
    return a + (b - a) * double(s2) / 4294967296.0;
  }
  uint8_t& G(int r, int c) { return g[size_t(r) * C + c]; }
  bool in(int r, int c) const { return r >= 0 && c >= 0 && r < R && c < C; }

  void subdivide(int r0, int c0, int r1, int c1, int s, int depth) {
    int mr = (r0 + r1) / 2;
    if (std::abs(r1 - r0) & 1) mr += (rand(2) != 0) ? 1 : 0;
    int mc = (c0 + c1) / 2;
    if (std::abs(c1 - c0) & 1) mc += (rand(2) != 0) ? 1 : 0;
    int A = G(r0, c0), B = G(r0, c1), P = G(r1, c0), Q = G(r1, c1);
    if (G(r0, mc) == 0) G(r0, mc) = uint8_t(jitter2(A, B, s));
    int top = G(r0, mc);
    if (G(r1, mc) == 0) G(r1, mc) = uint8_t(jitter2(P, Q, s));
    int bottom = G(r1, mc);
    if (G(mr, c0) == 0) G(mr, c0) = uint8_t(jitter2(A, P, s));
    int left = G(mr, c0);
    if (G(mr, c1) == 0) G(mr, c1) = uint8_t(jitter2(B, Q, s));
    int right = G(mr, c1);
    if (G(mr, mc) == 0) G(mr, mc) = uint8_t(jitter4(top, bottom, left, right, s));
    if (depth == 0) return;
    depth--; s /= 2;
    subdivide(r0, c0, mr, mc, s, depth);
    subdivide(r0, mc, mr, c1, s, depth);
    subdivide(mr, c0, r1, mc, s, depth);
    subdivide(mr, mc, r1, c1, s, depth);
  }

  // Walk helper for coast edges. Returns (start r, c, along dr, dc, inward dr, dc, length).
  struct Edge { int r, c, ar, ac, ir, ic, len; };
  Edge edge(int bit) const {
    switch (bit) {
      case 0: return {0, 0, 1, 0, 0, 1, R};
      case 1: return {R - 1, 0, 0, 1, -1, 0, C};
      case 2: return {0, C - 1, 1, 0, 0, -1, R};
      default: return {0, 0, 0, 1, 1, 0, C};
    }
  }
};

}  // namespace

void generateTerrain(World& world, int W, int H, const TerrainGenOptions& opt) {
  Gen z;
  z.R = W + 1; z.C = H + 1;
  z.M = std::min(z.R - 1, z.C - 1);
  z.qR = (z.R - 1) / 4; z.qC = (z.C - 1) / 4;
  size_t N = size_t(z.R) * z.C;
  z.g.assign(N, 0); z.w.assign(N, 0); z.t.assign(N, 0); z.m.assign(N, 0);
  uint32_t seed = opt.seed;
  if (seed == 0xFFFFFFFFu)  // spec 2/3.1: millisecond system timer
    seed = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
  z.rng.seed(seed); z.s2 = seed;
  const int flags = opt.flags, R = z.R, C = z.C, M = z.M, qR = z.qR, qC = z.qC;

  // 2. Control lattice.
  bool lowered[25] = {}, rowMid[25] = {}, colMid[25] = {};
  for (int k = 0; k < 25; k++) {
    int ci = k / 5, ri = k % 5;
    if ((flags & 0x01) && ci == 0) lowered[k] = true;
    if ((flags & 0x02) && ri == 4) lowered[k] = true;
    if ((flags & 0x04) && ci == 4) lowered[k] = true;
    if ((flags & 0x08) && ri == 0) lowered[k] = true;
    if ((flags & 0x40) && k == 12) lowered[k] = true;
    if ((flags & 0x01) && k <= 3) rowMid[k] = true;
    if ((flags & 0x04) && k >= 20 && k <= 23) rowMid[k] = true;
    if ((flags & 0x02) && (k == 4 || k == 9 || k == 14 || k == 19)) colMid[k] = true;
    if ((flags & 0x08) && (k == 0 || k == 5 || k == 10 || k == 15)) colMid[k] = true;
  }
  for (int k = 0; k < 25; k++) {
    int ci = k / 5, ri = k % 5;
    int base = lowered[k] ? 96 - M / 4 : 96;
    z.G(ri * qR, ci * qC) = uint8_t(z.jitter2(base, base, 16));
  }
  if (flags & 0x80) { int b = 2 * (M / 4) + 96; z.G(2 * qR, 2 * qC) = uint8_t(z.jitter2(b, b, 16)); }
  // 3. Edge midpoints.
  for (int k = 0; k < 25; k++) {
    int ci = k / 5, ri = k % 5;
    if (rowMid[k]) z.G(ri * qR + qR / 2, ci * qC) = uint8_t((z.G(ri * qR + qR, ci * qC) + z.G(ri * qR, ci * qC)) / 2);
    if (colMid[k]) z.G(ri * qR, ci * qC + qC / 2) = uint8_t((z.G(ri * qR, ci * qC + qC) + z.G(ri * qR, ci * qC)) / 2);
  }
  // 4. Midpoint displacement.
  z.subdivide(0, 0, R - 1, C - 1, M, 8);

  // 5. Water level.
  uint32_t hist[256] = {};
  for (uint8_t v : z.g) hist[v]++;
  int water = opt.water & 0xFF;
  int nFlags = ((flags & 1) != 0) + ((flags & 2) != 0) + ((flags & 4) != 0) + ((flags & 8) != 0);
  uint32_t a = uint32_t(std::max(water, 4));
  uint32_t kk = 1 + 2 * nFlags + ((flags & 0x40) ? 3 : 0);
  uint32_t T = uint32_t((uint32_t(R) * uint32_t(C) * a * kk) / 256u) / 6u;
  if (water >= 128) T = uint32_t(R) * uint32_t(C) * 9u / 10u;
  if (T == 0) T = 1;
  int L = 255;
  { int mn = 0; while (mn < 255 && hist[mn] == 0) mn++; L = mn - 1; }  // provisional: min altitude - 1
  { uint32_t cum = 0; for (int v = 0; v < 256; v++) { cum += hist[v]; if (cum >= T) { L = v; break; } } }
  z.L = L;
  std::fill(z.w.begin(), z.w.end(), uint8_t(L));  // stores wrap modulo 256

  // 6. Hill thresholds.
  double Dd = (flags & 0x80) ? 128.0 : 256.0;
  double f = 1.0 - std::sqrt((opt.hills & 0xFF) / Dd * 0.5);
  double Nn = double(N);
  uint32_t t1 = uint32_t(Nn * f), t2 = uint32_t(Nn * f * f * f), t3 = uint32_t(Nn * f * f * f * f);
  auto findB = [&](int from, uint32_t target) {
    uint32_t sum = 0;
    for (int v = from + 1; v < 256; v++) { sum += hist[v]; if (sum >= target) return v; }
    return 255;
  };
  int b1 = findB(L, t1), b2 = findB(b1, t2), b3 = findB(b2, t3);

  // 7. Band compression.
  int d1, d2, d3; bool quant;
  switch (opt.difficulty) {
    case 2: d1 = 10; d2 = 5; d3 = 3; quant = false; break;
    case 3: d1 = 6; d2 = 3; d3 = 2; quant = false; break;
    default: d1 = 12; d2 = 6; d3 = 3; quant = true; break;
  }
  {
    int p = L + 2, c1 = p + (b1 - p) / d1, c2 = c1 + (b2 - b1) / d2;
    for (auto& cell : z.g) {
      int v = cell;
      if (v < p) continue;
      int nv;
      if (v < b1) nv = p + (v - p) / d1;
      else if (v < b2) nv = c1 + (v - b1) / d2;
      else if (v < b3) nv = c2 + (v - b2) / d3;
      else nv = v + (c2 + (b3 - b2) / d3 - b3);
      cell = uint8_t(nv);
    }
  }

  // 8. Beaches.
  for (int bit = 0; bit < 4; bit++) {
    if (!(flags & (1 << bit))) continue;
    Gen::Edge e = z.edge(bit);
    int k = 3;
    for (int i = 0; i < e.len; i++) {
      int r = e.r + e.ar * i, c = e.c + e.ac * i;
      if (z.G(r, c) <= L) continue;
      int u = z.centreRand(0, 7);
      if (u > k) k++; else if (u < k) k--;
      for (int j = 0; j < k; j++) {
        int rr = r + e.ir * j, cc = c + e.ic * j;
        if (!z.in(rr, cc)) break;
        int cap = L + 3 - (k - j);
        if (z.G(rr, cc) > cap) z.G(rr, cc) = uint8_t(std::max(cap, 0));
      }
    }
  }

  // 9. River.
  if (flags & 0x30) {
    int D = int(std::sqrt(double(R)) * 0.5) * (water + 64) / 128;
    if (D >= L) D = L - 1;
    int startD = D, endD = D;
    struct Pt { int r = 0, c = 0; };
    Pt P[4];
    auto pickMid = [&]() {
      P[1].r = z.randRange(R / 8, 7 * R / 8); P[1].c = z.randRange(C / 8, 7 * C / 8);
      P[2].r = z.randRange(R / 8, 7 * R / 8); P[2].c = z.randRange(C / 8, 7 * C / 8);
    };
    if (flags & 0x20) {
      P[0].r = z.randRange(R / 4, 3 * R / 4); P[0].c = 0;
      P[3].r = z.randRange(R / 4, 3 * R / 4); P[3].c = C - 1;
      pickMid();
      if (flags & 0x01) startD = std::max(D, L - z.G(P[0].r, P[0].c));
      if (flags & 0x04) endD = std::max(D, L - z.G(P[3].r, P[3].c));
    }
    if (flags & 0x10) {
      P[0].r = 0; P[0].c = z.randRange(C / 4, 3 * C / 4);
      P[3].r = R - 1; P[3].c = z.randRange(C / 4, 3 * C / 4);
      pickMid();
      if (flags & 0x08) startD = std::max(D, L - z.G(P[0].r, P[0].c));
      if (flags & 0x02) endD = std::max(D, L - z.G(P[3].r, P[3].c));
    }
    int pr = -1, pc = -1;
    const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
    for (int n = 0; n < 1024; n++) {
      double tt = double(n + 1) / 1024.0;
      double b0 = 1 - 3 * tt + 3 * tt * tt - tt * tt * tt, b1w = 3 * tt - 6 * tt * tt + 3 * tt * tt * tt;
      double b2w = 3 * tt * tt - 3 * tt * tt * tt, b3w = tt * tt * tt;
      int sr = int(b0 * P[0].r + b1w * P[1].r + b2w * P[2].r + b3w * P[3].r);
      int sc = int(b0 * P[0].c + b1w * P[1].c + b2w * P[2].c + b3w * P[3].c);
      if (sr == pr && sc == pc) continue;
      pr = sr; pc = sc;
      int num = (endD - startD) * n;
      int fl = (num >= 0) ? num / 1024 : -((-num + 1023) / 1024);
      int e = ((startD + fl) % 256 + 256) % 256;
      for (int d = 0; d < 4; d++) {
        int x = z.randRange(0, 3);
        int lam = (((L - (e + x)) % 256) + 256) % 256;
        int r = sr, c = sc;
        while (z.in(r, c)) {
          if (lam < z.G(r, c)) z.G(r, c) = uint8_t(lam);
          r += dr[d]; c += dc[d];
          lam += (lam < L) ? 2 : 1;
          if (lam >= 252) break;
        }
      }
    }
  }

  // 10. Sea mask.
  for (int bit = 0; bit < 4; bit++) {
    if (!(flags & (1 << bit))) continue;
    Gen::Edge e = z.edge(bit);
    for (int i = 0; i < e.len; i++) {
      int n = water * R / 256, land = 5;
      int r = e.r + e.ar * i, c = e.c + e.ac * i;
      while (z.in(r, c)) {
        if (n == 0) break;
        if (z.w[size_t(r) * C + c] < int(z.G(r, c)) - 1) land--;
        n--;
        z.m[size_t(r) * C + c] = 1;
        if (land == 0) break;
        r += e.ir; c += e.ic;
      }
    }
  }

  // 11. Trees.
  {
    int trees = opt.trees & 0xFF;
    uint32_t clusters = uint32_t(trees) * uint32_t(R) * uint32_t(C) / 65536u;
    int rhoMax = std::max(trees / 4, 2);
    for (uint32_t cl = 0; cl < clusters; cl++) {
      int cr = z.rand(R), cc = z.rand(C);
      int rho = z.centreRand(0, rhoMax);
      int count = (rho <= 1) ? 1 : z.centreRand(0, rho * rho);
      for (int i = 0; i < count; i++) {
        int d = (rho > 1) ? z.rand(rho) : 0;
        double th = z.uniformReal(0, 6.2832);
        int r = cr + int(std::cos(th) * d), c = cc + int(std::sin(th) * d);
        if (z.in(r, c) && z.G(r, c) > z.w[size_t(r) * C + c])
          z.t[size_t(r) * C + c] = uint8_t(z.lowRand(0, z.m[size_t(r) * C + c] ? 128 : 256));
      }
    }
  }

  // 12. Quantise.
  if (quant) {
    for (auto& cell : z.g) if (cell > L + 2) cell = uint8_t(((cell - L - 2) & ~1) + L + 2);
  }

  // Apply.
  world.init(W, H);
  world.seaLevel = uint8_t(L);
  world.alt = z.g;
  world.water = z.w;
  for (int x = 0; x < W; x++)
    for (int zz = 0; zz < H; zz++) {
      int d = z.t[size_t(x) * C + zz];
      if (d >= 32) { int lvl = d / 32; world.flora[world.tIdx(x, zz)] = uint8_t(0x30 + (lvl - 1)); }
    }
}

}  // namespace m3k
