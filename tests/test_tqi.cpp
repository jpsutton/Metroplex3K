// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <cstring>
#include <random>
#include "core/bytes.h"
#include "core/intro.h"
#include "core/tqi.h"
#include "core/video.h"
#include "testutil.h"
using namespace m3k;

// ---- minimal TQI intra encoder (test only) ----
struct BitW {
  Bytes b; int nb = 0;
  void put(uint32_t v, int n) { for (int i = n - 1; i >= 0; i--) { if (nb % 8 == 0) b.push_back(0); if ((v >> i) & 1) b.back() |= uint8_t(0x80 >> (nb % 8)); nb++; } }
  Bytes words() const { Bytes o = b; while (o.size() % 4) o.push_back(0); for (size_t i = 0; i < o.size(); i += 4) { std::swap(o[i], o[i + 3]); std::swap(o[i + 1], o[i + 2]); } return o; }
};

static void fdct(const float in[64], float out[64]) {
  const double pi = 3.14159265358979323846;
  for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) {
    double s = 0;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
      s += in[y * 8 + x] * std::cos((2 * x + 1) * u * pi / 16) * std::cos((2 * y + 1) * v * pi / 16);
    out[v * 8 + u] = float(0.25 * (u ? 1 : std::sqrt(0.5)) * (v ? 1 : std::sqrt(0.5)) * s);
  }
}

static void putAc(BitW& w, int run, int level) {
  int mag = std::abs(level);
  for (auto& c : tqiAcCodes())
    if (c.run == run && c.level == mag) { w.put(c.code, c.len); w.put(level < 0, 1); return; }
  w.put(kTqiEscCode, kTqiEscLen); w.put(uint32_t(run), 6);
  if (level >= -127 && level <= 127 && level != 0) w.put(uint32_t(level) & 0xFF, 8);
  else if (level > 0) { w.put(0, 8); w.put(uint32_t(level), 8); }
  else { w.put(0x80, 8); w.put(uint32_t(level + 256), 8); }
}

static void encBlock(BitW& w, const float px[64], int comp, int quant, int& lastDc) {
  float T[64]; fdct(px, T);
  int dc = int(std::lround(T[0] / 8.0f));
  int diff = dc - lastDc; lastDc = dc;
  int size = 0; while ((1 << size) <= std::abs(diff)) size++;
  uint16_t code; int len; tqiDcCode(comp != 0, size, code, len);
  w.put(code, len);
  if (size) w.put(uint32_t(diff > 0 ? diff : diff + (1 << size) - 1), size);
  int run = 0;
  for (int i = 1; i < 64; i++) {
    int ri = kTqiZigzag[i];
    float step = float(tqiDequantAc(1, ri, quant)) / 2.0f;  // T per level (approx)
    int lv = int(std::lround(T[ri] / step));
    lv = std::max(-255, std::min(255, lv));
    if (lv == 0) { run++; continue; }
    putAc(w, run, lv); run = 0;
  }
  w.put(kTqiEobCode, kTqiEobLen);
}

static int clampi(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static Bytes encodeFrame(const std::vector<uint32_t>& rgb, int W, int H, int quant) {
  int mbw = (W + 15) / 16, mbh = (H + 15) / 16, pw = mbw * 16, ph = mbh * 16;
  std::vector<float> Y(size_t(pw) * ph), Cb(size_t(pw / 2) * ph / 2), Cr(Cb.size());
  std::vector<float> cbf(size_t(pw) * ph), crf(cbf.size());
  for (int y = 0; y < ph; y++) for (int x = 0; x < pw; x++) {
    uint32_t p = rgb[size_t(std::min(y, H - 1)) * W + std::min(x, W - 1)];
    float r = float((p >> 16) & 255), g = float((p >> 8) & 255), b = float(p & 255);
    Y[size_t(y) * pw + x] = 16 + 0.257f * r + 0.504f * g + 0.098f * b;
    cbf[size_t(y) * pw + x] = 128 - 0.148f * r - 0.291f * g + 0.439f * b;
    crf[size_t(y) * pw + x] = 128 + 0.439f * r - 0.368f * g - 0.071f * b;
  }
  for (int y = 0; y < ph / 2; y++) for (int x = 0; x < pw / 2; x++) {
    auto avg = [&](const std::vector<float>& s) { size_t i = size_t(2 * y) * pw + 2 * x; return (s[i] + s[i + 1] + s[i + pw] + s[i + pw + 1]) / 4; };
    Cb[size_t(y) * (pw / 2) + x] = avg(cbf); Cr[size_t(y) * (pw / 2) + x] = avg(crf);
  }
  BitW w; int last[3] = {0, 0, 0};
  for (int my = 0; my < mbh; my++) for (int mx = 0; mx < mbw; mx++)
    for (int b = 0; b < 6; b++) {
      float px[64]; const float* src; int stride, ox, oy;
      if (b < 4) { src = Y.data(); stride = pw; ox = mx * 16 + (b & 1) * 8; oy = my * 16 + (b >> 1) * 8; }
      else { src = (b == 4 ? Cb : Cr).data(); stride = pw / 2; ox = mx * 8; oy = my * 8; }
      for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) px[y * 8 + x] = src[size_t(oy + y) * stride + ox + x];
      encBlock(w, px, b < 4 ? 0 : b - 3, quant, last[b < 4 ? 0 : b - 3]);
    }
  Bytes out; put16(out, uint16_t(W)); put16(out, uint16_t(H)); out.push_back(uint8_t(quant)); out.push_back(0); out.push_back(0); out.push_back(0);
  Bytes body = w.words(); out.insert(out.end(), body.begin(), body.end());
  return out;
}

static std::vector<uint32_t> testImage(int W, int H, int t = 0) {
  std::vector<uint32_t> v(size_t(W) * H);
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    int r = clampi(x * 255 / W + t * 4), g = clampi(y * 255 / H), b = clampi(255 - (x + y) * 255 / (W + H));
    if (x > W / 2 && x < W / 2 + W / 6 && y > H / 3 && y < H / 3 + H / 6) { r = 230; g = 40; b = 40; }  // flat patch
    v[size_t(y) * W + x] = 0xFF000000u | uint32_t(r << 16) | uint32_t(g << 8) | uint32_t(b);
  }
  return v;
}

static double psnr(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
  double se = 0; size_t n = 0;
  for (size_t i = 0; i < a.size(); i++) for (int s = 0; s <= 16; s += 8) { double d = double((a[i] >> s) & 255) - double((b[i] >> s) & 255); se += d * d; n++; }
  if (se == 0) return 99;
  return 10 * std::log10(255.0 * 255.0 * double(n) / se);
}

static void testRoundTrip() {
  for (int q : {100, 98}) for (auto [W, H] : {std::pair{64, 48}, std::pair{50, 38}, std::pair{320, 48}}) {
    auto img = testImage(W, H);
    Bytes f = encodeFrame(img, W, H, q);
    std::vector<uint32_t> out;
    bool ok = decodeTqiFrame(f.data(), f.size(), W, H, out);
    CHECK(ok);
    CHECK(out.size() == size_t(W) * H);
    if (out.size() == img.size()) { double p = psnr(img, out); if (p < 27) std::fprintf(stderr, "psnr %.1f q%d %dx%d\n", p, q, W, H); CHECK(p > 27); }
  }
  {  // smooth gradient (no sharp chroma edge): high fidelity at a fine quantiser (real INTRO.TGQ uses q ~96-100)
    int W = 64, H = 48; std::vector<uint32_t> v(size_t(W) * H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) v[size_t(y) * W + x] = 0xFF000000u | uint32_t(x * 255 / W) << 16 | uint32_t(y * 255 / H) << 8 | 128u;
    Bytes g = encodeFrame(v, W, H, 100); std::vector<uint32_t> o;
    CHECK(decodeTqiFrame(g.data(), g.size(), W, H, o)); CHECK(psnr(v, o) > 35);
    Bytes g2 = encodeFrame(v, W, H, 60); std::vector<uint32_t> o2;  // coarser -> smaller and worse
    CHECK(decodeTqiFrame(g2.data(), g2.size(), W, H, o2)); CHECK(g2.size() <= g.size()); CHECK(psnr(v, o2) < psnr(v, o) && psnr(v, o2) > 22);
  }
  // flat colour: near exact
  std::vector<uint32_t> flat(32 * 32, 0xFF3070B0u);
  Bytes f = encodeFrame(flat, 32, 32, 100);
  std::vector<uint32_t> out; CHECK(decodeTqiFrame(f.data(), f.size(), 32, 32, out));
  CHECK(psnr(flat, out) > 30);
}

static void testRobust() {
  auto img = testImage(64, 48);
  Bytes f = encodeFrame(img, 64, 48, 100);
  std::vector<uint32_t> out;
  for (size_t n = 0; n < f.size(); n += 3) {  // truncation at every length
    bool ok = decodeTqiFrame(f.data(), n, 64, 48, out);
    if (n < f.size() - 8) CHECK(!ok || n == f.size());
  }
  CHECK(!decodeTqiFrame(f.data(), 5, 64, 48, out));
  CHECK(!decodeTqiFrame(nullptr, 0, 0, 0, out));
  // truncated mid-stream still yields a full-size partial picture
  CHECK(!decodeTqiFrame(f.data(), f.size() / 2, 64, 48, out)); CHECK(out.size() == 64 * 48);
  std::mt19937 rng(7);
  for (int it = 0; it < 300; it++) {  // garbage and bit-flipped streams
    Bytes g = f;
    if (it % 3 == 0) { for (auto& b : g) b = uint8_t(rng()); }
    else for (int k = 0; k < 10; k++) g[8 + rng() % (g.size() - 8)] ^= uint8_t(1 << (rng() % 8));
    if (it % 5 == 0) { g[0] = uint8_t(rng()); g[1] = uint8_t(rng()); g[2] = uint8_t(rng()); g[3] = uint8_t(rng()); }
    decodeTqiFrame(g.data(), g.size(), 0, 0, out);
  }
  Bytes huge = f; huge[0] = 0xFF; huge[1] = 0xFF; huge[2] = 0xFF; huge[3] = 0xFF;
  CHECK(!decodeTqiFrame(huge.data(), huge.size(), 0, 0, out)); CHECK(out.empty());
}

// ---- movie + player ----
static void chunk(Bytes& b, const char* tag, const Bytes& body) {
  for (int i = 0; i < 4; i++) b.push_back(uint8_t(tag[i]));
  put32(b, uint32_t(body.size() + 8)); b.insert(b.end(), body.begin(), body.end());
}

static void testPlayer() {
  Bytes file;
  Bytes sch = {'P', 'T', 0, 0, 0x82, 1, 2, 0x83, 1, 7, 0xFF};
  chunk(file, "SCHl", sch);
  const int W = 32, H = 32, NF = 4;
  std::vector<std::vector<uint32_t>> imgs;
  for (int i = 0; i < NF; i++) {
    imgs.push_back(testImage(W, H, i * 10));
    chunk(file, "pIQT", encodeFrame(imgs.back(), W, H, 100));
    // audio chunk: 28 frames, stereo, silent codes
    Bytes a; put32(a, 28); for (int k = 0; k < 4; k++) put16(a, 0);
    a.push_back(0); a.push_back(0); for (int k = 0; k < 28; k++) a.push_back(0);
    chunk(file, "SCDl", a);
  }
  chunk(file, "SCEl", {});
  TgqMovie m; CHECK(m.parse(file));
  CHECK(m.frames().size() == size_t(NF));
  CHECK(m.frames()[0].dataOffset == m.frames()[0].offset + 8);
  std::vector<uint32_t> px;
  CHECK(m.decodeFrame(1, px)); CHECK(psnr(px, imgs[1]) > 27);
  CHECK(m.decodeFrame(2, defaultTqiDecoder(), px));

  IntroPlayer p(m);
  CHECK(p.state() == IntroState::NotStarted);
  p.start(); CHECK(p.state() == IntroState::Playing);
  int idx = -1;
  auto* f0 = p.frameAt(0, &idx); CHECK(f0 && idx == 0);
  CHECK(p.frameWidth() == W && p.frameHeight() == H);
  auto* f3 = p.frameAt(3 * 1000 / 15 + 1, &idx); CHECK(f3 && idx == 3);
  CHECK(psnr(*f3, imgs[3]) > 27);
  std::vector<int16_t> pcm;
  size_t n = p.pullAudio(0, 1.f, pcm); CHECK(n == 28 && pcm.size() == 56);
  n = p.pullAudio(100, 1.f, pcm); CHECK(n == 28 * 3);  // remaining 3 chunks all due by 100 ms
  CHECK(p.pullAudio(100, 1.f, pcm) == 0);
  CHECK(p.update(10) == IntroState::Playing);
  CHECK(p.update(m.durationMs()) == IntroState::Finished);
  IntroPlayer p2(m); p2.start(); p2.skip(); CHECK(p2.state() == IntroState::Finished);
  TgqMovie empty; IntroPlayer p3(empty); p3.start(); CHECK(p3.state() == IntroState::Finished);

  // scaling: 32x32 into a 800x600 window -> rect at (80,60) 640x480, black elsewhere
  auto win = composeIntroWindow(*f0, W, H, 800, 600);
  CHECK(win.size() == 800 * 600);
  CHECK(win[0] == 0xFF000000u && win[size_t(59) * 800 + 100] == 0xFF000000u);
  CHECK(win[size_t(60) * 800 + 80] == (*f0)[0]);
  CHECK(win[size_t(60) * 800 + 81] == (*f0)[0] || W != 320);
  CHECK(win[size_t(60 + 479) * 800 + 80 + 639] == (*f0)[size_t(H - 1) * W + W - 1]);
  auto small = composeIntroWindow(*f0, W, H, 320, 240);  // window smaller than 640x480: clipped, no crash
  CHECK(small.size() == 320 * 240);
  // real 2x stretch for coded 320-wide frames
  std::vector<uint32_t> big(320 * 4, 0xFF112233u); big[0] = 0xFFAA0000u;
  auto w2 = composeIntroWindow(big, 320, 4, 640, 480);
  CHECK(w2[0] == 0xFFAA0000u && w2[1] == 0xFFAA0000u && w2[2] == 0xFF112233u);
}

// Real INTRO.TGQ SCHl payload: tag 0xFD opens the audio sub-header and has no length byte.
static void testRealAudioHeader() {
  Bytes file;
  Bytes sch = {'P', 'T', 0, 0, 0x00, 1, 0x02, 0x06, 1, 0x65, 0xFD, 0x85, 3, 0x18, 0x68, 0x0C,
               0x82, 1, 2, 0x83, 1, 7, 0x8A, 4, 0, 0, 0, 0, 0xFF, 0, 0, 0};
  chunk(file, "SCHl", sch);
  Bytes a; put32(a, 28); for (int k = 0; k < 4; k++) put16(a, 0);
  a.push_back(0); a.push_back(0); for (int k = 0; k < 28; k++) a.push_back(0);
  chunk(file, "SCDl", a);
  chunk(file, "SCEl", {});
  TgqMovie m; CHECK(m.parse(file));
  CHECK(m.audio().present && m.audio().channels == 2 && m.audio().compression == 7);
  CHECK(m.audio().totalFrames == 1599500 && m.audio().sampleRate == 22050);
  PcmBuffer pcm; CHECK(m.decodeAudio(pcm)); CHECK(pcm.samples.size() == 56);
}

int main() {
  testRoundTrip();
  testRobust();
  testPlayer();
  testRealAudioHeader();
  TEST_MAIN_END();
}
