// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/xa_codec.h"
#include <algorithm>
#include <cctype>
#include "core/bytes.h"

namespace m3k {

namespace {
const int kC1[4] = {0, 240, 460, 392};
const int kC2[4] = {0, 0, -208, -220};

inline int64_t floorDiv256(int64_t v) { return v >> 8; }  // arithmetic shift = floor

inline int64_t scaled(int code, int shift) {
  int v = code < 8 ? code : code - 16;
  if (shift > 12) return 0;
  return int64_t(v) * (int64_t(1) << (12 - shift));
}
}  // namespace

bool parseXaHeader(const uint8_t* p, size_t n, XaHeader& h) {
  if (n < kXaHeaderSize || p[0] != 'X' || p[1] != 'A') return false;
  h.outSize = rd32(p + 4);
  h.formatTag = rd16(p + 8);
  h.channels = rd16(p + 10);
  h.sampleRate = rd32(p + 12);
  h.avgBytesPerSec = rd32(p + 16);
  h.blockAlign = rd16(p + 20);
  h.bitsPerSample = rd16(p + 22);
  return h.formatTag == 1;
}

int64_t xaDecodeSample(XaChannelState& st, int predictor, int shift, int code, bool tiesEven) {
  int pi = (predictor >= 0 && predictor <= 3) ? predictor : 0;
  int64_t num = 256 * scaled(code & 15, shift) + int64_t(kC1[pi]) * st.h1 + int64_t(kC2[pi]) * st.h2;
  int64_t y = floorDiv256(num);
  int64_t r = num - y * 256;  // 0..255
  if (r > 128) y++;
  else if (r == 128) { if (tiesEven) { if (y & 1) y++; } else y++; }
  st.h2 = st.h1;
  st.h1 = y;
  return y;
}

bool decodeXa(const uint8_t* p, size_t n, PcmBuffer& out, bool tiesEven) {
  XaHeader h;
  if (!parseXaHeader(p, n, h)) return false;
  out.channels = h.channels == 2 ? 2 : 1;
  out.sampleRate = int(h.sampleRate);
  size_t total = h.outSize / 2;  // samples (all channels)
  out.samples.assign(total, 0);
  if (h.channels != 1 && h.channels != 2) return true;
  const int ch = h.channels;
  size_t frames = total / size_t(ch);
  size_t pos = kXaHeaderSize;
  const size_t blockBytes = size_t(15) * ch;
  XaChannelState st[2];
  size_t f = 0;
  while (f < frames && pos + blockBytes <= n) {
    const uint8_t* b = p + pos;
    int pr[2], sh[2];
    for (int c = 0; c < ch; c++) { pr[c] = b[c] >> 4; sh[c] = b[c] & 15; }
    const uint8_t* d = b + ch;
    for (int i = 0; i < 14; i++) {
      for (int half = 0; half < 2; half++) {
        size_t fi = f + size_t(i) * 2 + half;
        if (fi >= frames) break;
        for (int c = 0; c < ch; c++) {
          uint8_t byte = d[i * ch + c];
          int code = half == 0 ? (byte >> 4) : (byte & 15);
          int64_t y = xaDecodeSample(st[c], pr[c], sh[c], code, tiesEven);
          out.samples[fi * ch + c] = int16_t(uint16_t(uint64_t(y) & 0xFFFF));
        }
      }
    }
    f += kXaFramesPerBlock;
    pos += blockBytes;
  }
  return true;
}

bool decodeEaAdpcmChunk(const uint8_t* p, size_t n, int channels, std::vector<int16_t>& out) {
  if (n < 4 || channels < 1 || channels > 2) return false;
  uint32_t N = rd32(p);
  size_t pos = 4;
  if (n < pos + 4 * 2) return false;
  int64_t h1[2] = {0, 0}, h2[2] = {0, 0};
  for (int c = 0; c < 2; c++) {  // left pair then right pair
    h1[c] = int16_t(rd16(p + pos));
    h2[c] = int16_t(rd16(p + pos + 2));
    pos += 4;
  }
  size_t groups = (size_t(N) + 27) / 28;
  size_t base = out.size();
  out.resize(base + size_t(N) * channels, 0);
  size_t f = 0;
  bool ok = true;
  for (size_t g = 0; g < groups; g++) {
    if (pos + 30 > n) { ok = false; break; }
    int pr[2] = {p[pos] >> 4, p[pos] & 15};
    int sh[2] = {p[pos + 1] >> 4, p[pos + 1] & 15};
    const uint8_t* d = p + pos + 2;
    for (int i = 0; i < 28 && f + i < N; i++) {
      for (int c = 0; c < channels; c++) {
        int code = c == 0 ? (d[i] >> 4) : (d[i] & 15);
        int pi = pr[c] <= 3 ? pr[c] : 0;
        int64_t num = 256 * scaled(code, sh[c]) + int64_t(kC1[pi]) * h1[c] + int64_t(kC2[pi]) * h2[c] + 128;
        int64_t y = std::clamp<int64_t>(floorDiv256(num), -32768, 32767);
        h2[c] = h1[c];
        h1[c] = y;
        out[base + (f + i) * channels + c] = int16_t(y);
      }
    }
    f += 28;
    pos += 30;
  }
  return ok;
}

bool loadWav(const uint8_t* p, size_t n, PcmBuffer& out) {
  if (n < 12 || std::string(reinterpret_cast<const char*>(p), 4) != "RIFF" ||
      std::string(reinterpret_cast<const char*>(p + 8), 4) != "WAVE")
    return false;
  size_t pos = 12;
  uint16_t tag = 0, ch = 0, bits = 0;
  uint32_t rate = 0;
  bool haveFmt = false;
  while (pos + 8 <= n) {
    std::string id(reinterpret_cast<const char*>(p + pos), 4);
    size_t sz = rd32(p + pos + 4);
    size_t body = pos + 8;
    size_t avail = std::min(sz, n - body);
    if (id == "fmt " && avail >= 16) {
      tag = rd16(p + body); ch = rd16(p + body + 2); rate = rd32(p + body + 4); bits = rd16(p + body + 14);
      haveFmt = true;
    } else if (id == "data") {
      if (!haveFmt || tag != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16)) return false;
      out.channels = ch;
      out.sampleRate = int(rate);
      size_t bps = bits / 8;
      size_t cnt = avail / bps;
      cnt -= cnt % ch;
      out.samples.resize(cnt);
      for (size_t i = 0; i < cnt; i++) {
        if (bits == 16) out.samples[i] = int16_t(rd16(p + body + i * 2));
        else out.samples[i] = int16_t((int(p[body + i]) - 128) * 256);
      }
      return true;
    }
    pos = body + sz + (sz & 1);
  }
  return false;
}

bool loadSoundFile(const std::string& name, const std::vector<uint8_t>& data, PcmBuffer& out) {
  std::string ext;
  size_t dot = name.rfind('.');
  if (dot != std::string::npos)
    for (size_t i = dot; i < name.size(); i++) ext += char(std::tolower(static_cast<unsigned char>(name[i])));
  if (ext == ".xa") return decodeXa(data, out);
  if (ext == ".wav") return loadWav(data, out);
  return false;
}

}  // namespace m3k
