#include "core/tqi.h"
#include <algorithm>
#include <cmath>

namespace m3k {

namespace {
// MPEG-1 table B.14 (standard data), listed as code/len + run/level in code-length order.
const uint16_t kAcCode[113][2] = {
 { 0x3, 2 }, { 0x4, 4 }, { 0x5, 5 }, { 0x6, 7 }, { 0x26, 8 }, { 0x21, 8 }, { 0xa, 10 }, { 0x1d, 12 },
 { 0x18, 12 }, { 0x13, 12 }, { 0x10, 12 }, { 0x1a, 13 }, { 0x19, 13 }, { 0x18, 13 }, { 0x17, 13 }, { 0x1f, 14 },
 { 0x1e, 14 }, { 0x1d, 14 }, { 0x1c, 14 }, { 0x1b, 14 }, { 0x1a, 14 }, { 0x19, 14 }, { 0x18, 14 }, { 0x17, 14 },
 { 0x16, 14 }, { 0x15, 14 }, { 0x14, 14 }, { 0x13, 14 }, { 0x12, 14 }, { 0x11, 14 }, { 0x10, 14 }, { 0x18, 15 },
 { 0x17, 15 }, { 0x16, 15 }, { 0x15, 15 }, { 0x14, 15 }, { 0x13, 15 }, { 0x12, 15 }, { 0x11, 15 }, { 0x10, 15 },
 { 0x3, 3 }, { 0x6, 6 }, { 0x25, 8 }, { 0xc, 10 }, { 0x1b, 12 }, { 0x16, 13 }, { 0x15, 13 }, { 0x1f, 15 },
 { 0x1e, 15 }, { 0x1d, 15 }, { 0x1c, 15 }, { 0x1b, 15 }, { 0x1a, 15 }, { 0x19, 15 }, { 0x13, 16 }, { 0x12, 16 },
 { 0x11, 16 }, { 0x10, 16 }, { 0x5, 4 }, { 0x4, 7 }, { 0xb, 10 }, { 0x14, 12 }, { 0x14, 13 }, { 0x7, 5 },
 { 0x24, 8 }, { 0x1c, 12 }, { 0x13, 13 }, { 0x6, 5 }, { 0xf, 10 }, { 0x12, 12 }, { 0x7, 6 }, { 0x9, 10 },
 { 0x12, 13 }, { 0x5, 6 }, { 0x1e, 12 }, { 0x14, 16 }, { 0x4, 6 }, { 0x15, 12 }, { 0x7, 7 }, { 0x11, 12 },
 { 0x5, 7 }, { 0x11, 13 }, { 0x27, 8 }, { 0x10, 13 }, { 0x23, 8 }, { 0x1a, 16 }, { 0x22, 8 }, { 0x19, 16 },
 { 0x20, 8 }, { 0x18, 16 }, { 0xe, 10 }, { 0x17, 16 }, { 0xd, 10 }, { 0x16, 16 }, { 0x8, 10 }, { 0x15, 16 },
 { 0x1f, 12 }, { 0x1a, 12 }, { 0x19, 12 }, { 0x17, 12 }, { 0x16, 12 }, { 0x1f, 13 }, { 0x1e, 13 }, { 0x1d, 13 },
 { 0x1c, 13 }, { 0x1b, 13 }, { 0x1f, 16 }, { 0x1e, 16 }, { 0x1d, 16 }, { 0x1c, 16 }, { 0x1b, 16 },
 { 0x1, 6 }, { 0x2, 2 } };
const uint8_t kAcLevel[111] = {
  1,  2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  1,  2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
  1,  2,  3,  4,  5,  1,  2,  3,  4,  1,  2,  3,  1,  2, 3,  1,  2,  3,  1,  2,  1,  2,
  1,  2,  1,  2,  1,  2,  1,  2, 1,  2,  1,  2,  1,  2,  1,  2,
  1,  1,  1,  1,  1,  1,  1,  1, 1,  1,  1,  1,  1,  1,  1 };
const uint8_t kAcRun[111] = {
  0,  0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,  0,  0,  0,  0,
  1,  1,  1,  1,  1,  1,  1,  1, 1,  1,  1,  1,  1,  1,  1,  1, 1,  1,
  2,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,  5,  5, 5,  6,  6,  6,  7,  7,  8,  8,
  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16,
 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };

const uint16_t kDcLum[12][2] = {{0x4,3},{0x0,2},{0x1,2},{0x5,3},{0x6,3},{0xe,4},{0x1e,5},{0x3e,6},{0x7e,7},{0xfe,8},{0x1fe,9},{0x1ff,9}};
const uint16_t kDcChr[12][2] = {{0x0,2},{0x1,2},{0x2,2},{0x6,3},{0xe,4},{0x1e,5},{0x3e,6},{0x7e,7},{0xfe,8},{0x1fe,9},{0x3fe,10},{0x3ff,10}};

const uint8_t kIntra[64] = {
  8, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37,
 19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40,
 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58,
 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83 };

struct Luts {
  std::vector<TqiAcCode> codes;
  uint16_t ac[65536];       // 0 invalid; else index+1 (index 111 = ESC, 112 = EOB; stored +1)
  uint16_t dc[2][1024];     // size+1 (peek 10 bits)
  uint8_t acLen[113];
  uint8_t dcLen[2][12];
  float basis[8][8];        // [u][x] = C(u)/2 * cos((2x+1) u pi / 16)
  Luts() {
    for (int i = 0; i < 111; i++) codes.push_back({kAcCode[i][0], uint8_t(kAcCode[i][1]), kAcRun[i], kAcLevel[i]});
    std::fill(ac, ac + 65536, uint16_t(0));
    for (int i = 0; i < 113; i++) {
      int len = kAcCode[i][1];
      acLen[i] = uint8_t(len);
      uint32_t base = uint32_t(kAcCode[i][0]) << (16 - len);
      for (uint32_t k = 0; k < (1u << (16 - len)); k++) ac[base + k] = uint16_t(i + 1);
    }
    for (int t = 0; t < 2; t++) {
      std::fill(dc[t], dc[t] + 1024, uint16_t(0));
      const uint16_t (*tab)[2] = t ? kDcChr : kDcLum;
      for (int s = 0; s < 12; s++) {
        int len = tab[s][1];
        dcLen[t][s] = uint8_t(len);
        uint32_t base = uint32_t(tab[s][0]) << (10 - len);
        for (uint32_t k = 0; k < (1u << (10 - len)); k++) dc[t][base + k] = uint16_t(s + 1);
      }
    }
    const double pi = 3.14159265358979323846;
    for (int u = 0; u < 8; u++)
      for (int x = 0; x < 8; x++)
        basis[u][x] = float((u == 0 ? std::sqrt(0.5) : 1.0) * 0.5 * std::cos((2 * x + 1) * u * pi / 16.0));
  }
};
const Luts& luts() { static const Luts l; return l; }

// MSB-first reader over the word-swapped copy of the stream; zeros past the end.
struct Bits {
  std::vector<uint8_t> d;
  size_t nbits = 0, pos = 0;
  void init(const uint8_t* p, size_t n) {
    size_t padded = (n + 3) / 4 * 4;
    d.assign(padded + 8, 0);
    for (size_t i = 0; i < n; i++) d[(i & ~size_t(3)) + 3 - (i & 3)] = p[i];
    nbits = n * 8;
  }
  uint32_t peek(int n) const {  // n <= 24
    size_t byte = pos >> 3;
    if (byte + 4 > d.size()) return 0;
    uint32_t v = (uint32_t(d[byte]) << 24) | (uint32_t(d[byte + 1]) << 16) | (uint32_t(d[byte + 2]) << 8) | d[byte + 3];
    v <<= (pos & 7);
    return v >> (32 - n);
  }
  uint32_t get(int n) { if (n == 0) return 0; uint32_t v = peek(n); pos += size_t(n); return v; }
  bool over() const { return pos > nbits; }
};

void idct8(const int coef[64], float out[64]) {  // coef in "2x" units; out = pixel
  const auto& L = luts();
  float tmp[64];
  for (int v = 0; v < 8; v++)       // rows: along u
    for (int x = 0; x < 8; x++) {
      float s = 0;
      for (int u = 0; u < 8; u++) if (coef[v * 8 + u]) s += L.basis[u][x] * float(coef[v * 8 + u]);
      tmp[v * 8 + x] = s * 0.5f;
    }
  for (int x = 0; x < 8; x++)
    for (int y = 0; y < 8; y++) {
      float s = 0;
      for (int v = 0; v < 8; v++) s += L.basis[v][y] * tmp[v * 8 + x];
      out[y * 8 + x] = s;
    }
}
inline uint8_t clip8(float v) { int i = int(std::lround(v)); return uint8_t(i < 0 ? 0 : i > 255 ? 255 : i); }
}  // namespace

const uint8_t kTqiZigzag[64] = {
  0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

const std::vector<TqiAcCode>& tqiAcCodes() { return luts().codes; }

void tqiDcCode(bool chroma, int size, uint16_t& code, int& len) {
  size = std::clamp(size, 0, 11);
  const auto& t = chroma ? kDcChr : kDcLum;
  code = t[size][0]; len = t[size][1];
}

int tqiQScale(int quant) { return std::max(5, (215 - 2 * quant) * 5); }

int tqiDequantAc(int level, int i, int quant) {
  if (level == 0) return 0;
  int mag = level < 0 ? -level : level;
  int64_t v = (int64_t(mag) * kIntra[i & 63] * tqiQScale(quant)) >> 6;
  v = (v - 1) | 1;
  return int(level < 0 ? -v : v);
}

uint32_t tqiYuvToArgb(int y, int cb, int cr) {
  float yy = 1.164383f * float(y - 16), u = float(cb - 128), v = float(cr - 128);
  auto c = [](float f) { int i = int(std::lround(f)); return uint32_t(i < 0 ? 0 : i > 255 ? 255 : i); };
  return 0xFF000000u | (c(yy + 1.596027f * v) << 16) | (c(yy - 0.391762f * u - 0.812968f * v) << 8) | c(yy + 2.017232f * u);
}

bool decodeTqiPlanes(const uint8_t* p, size_t n, TqiPlanes& out) {
  out = TqiPlanes();
  if (!p || n < kTqiHeaderSize) return false;
  int w = p[0] | (p[1] << 8), h = p[2] | (p[3] << 8), quant = p[4];
  if (w <= 0 || h <= 0 || w > kTqiMaxDim || h > kTqiMaxDim) return false;
  const auto& L = luts();
  int mbw = (w + 15) / 16, mbh = (h + 15) / 16;
  int pw = mbw * 16, ph = mbh * 16, cw = pw / 2, ch = ph / 2;
  std::vector<uint8_t> Y(size_t(pw) * ph, 128), Cb(size_t(cw) * ch, 128), Cr(size_t(cw) * ch, 128);
  Bits br;
  br.init(p + kTqiHeaderSize, n - kTqiHeaderSize);
  int lastDc[3] = {0, 0, 0};
  bool ok = true;
  for (int my = 0; my < mbh && ok; my++)
    for (int mx = 0; mx < mbw && ok; mx++) {
      for (int b = 0; b < 6 && ok; b++) {
        int comp = b < 4 ? 0 : b - 3;
        int coef[64] = {0};
        uint16_t e = L.dc[comp ? 1 : 0][br.peek(10)];
        if (!e) {  ok = false; break; }
        int size = e - 1;
        br.pos += L.dcLen[comp ? 1 : 0][size];
        int diff = 0;
        if (size) {
          int v = int(br.get(size));
          diff = v < (1 << (size - 1)) ? v - (1 << size) + 1 : v;
        }
        lastDc[comp] += diff;
        coef[0] = tqiDequantDc(lastDc[comp]);
        int i = 1;
        for (;;) {
          uint16_t a = L.ac[br.peek(16)];
          if (!a) {  ok = false; break; }
          br.pos += L.acLen[a - 1];
          if (a == 113) break;  // EOB
          int run, level;
          if (a == 112) {  // escape
            run = int(br.get(6));
            level = int(br.get(8));
            if (level >= 128) level -= 256;
            if (level == -128) level = int(br.get(8)) - 256;
            else if (level == 0) level = int(br.get(8));
          } else {
            run = kAcRun[a - 1]; level = kAcLevel[a - 1];
            if (br.get(1)) level = -level;
          }
          i += run;
          if (i > 63 || level == 0) {  ok = false; break; }
          int ri = kTqiZigzag[i++];
          coef[ri] = tqiDequantAc(level, ri, quant);
          if (br.over()) { ok = false; break; }
        }
        if (br.over()) ok = false;
        if (!ok) break;
        float px[64];
        idct8(coef, px);
        uint8_t* dst; int stride;
        if (comp == 0) { stride = pw; dst = &Y[size_t(my * 16 + (b >> 1) * 8) * pw + mx * 16 + (b & 1) * 8]; }
        else { stride = cw; dst = &(comp == 1 ? Cb : Cr)[size_t(my * 8) * cw + mx * 8]; }
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) dst[y * stride + x] = clip8(px[y * 8 + x]);
      }
    }
  out.w = w; out.h = h;
  int cw2 = (w + 1) / 2, ch2 = (h + 1) / 2;
  out.y.resize(size_t(w) * h); out.cb.resize(size_t(cw2) * ch2); out.cr.resize(size_t(cw2) * ch2);
  for (int y = 0; y < h; y++) std::copy_n(&Y[size_t(y) * pw], w, &out.y[size_t(y) * w]);
  for (int y = 0; y < ch2; y++) {
    std::copy_n(&Cb[size_t(y) * cw], cw2, &out.cb[size_t(y) * cw2]);
    std::copy_n(&Cr[size_t(y) * cw], cw2, &out.cr[size_t(y) * cw2]);
  }
  return ok;
}

bool decodeTqiFrame(const uint8_t* p, size_t n, int, int, std::vector<uint32_t>& argb) {
  TqiPlanes pl;
  bool ok = decodeTqiPlanes(p, n, pl);
  argb.clear();
  if (pl.w == 0) return false;
  argb.resize(size_t(pl.w) * pl.h);
  int cw = (pl.w + 1) / 2;
  for (int y = 0; y < pl.h; y++)
    for (int x = 0; x < pl.w; x++) {
      size_t c = size_t(y / 2) * cw + x / 2;
      argb[size_t(y) * pl.w + x] = tqiYuvToArgb(pl.y[size_t(y) * pl.w + x], pl.cb[c], pl.cr[c]);
    }
  return ok;
}

}  // namespace m3k
