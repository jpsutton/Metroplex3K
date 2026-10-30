// Minimal PNG writer (stored deflate blocks) for debugging output.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

inline void writePng(const std::string& path, int w, int h, const std::vector<uint32_t>& rgba) {
  static uint32_t table[256]; static bool init = false;
  if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } init = true; }
  auto crc = [&](const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) { for (size_t i = 0; i < n; i++) c = table[(c ^ d[i]) & 255] ^ (c >> 8); return c; };
  std::vector<uint8_t> raw;
  for (int y = 0; y < h; y++) { raw.push_back(0); for (int x = 0; x < w; x++) { uint32_t p = rgba[size_t(y) * w + x]; raw.push_back(p & 255); raw.push_back((p >> 8) & 255); raw.push_back((p >> 16) & 255); } }
  std::vector<uint8_t> z = {0x78, 0x01};
  size_t pos = 0;
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  while (pos < raw.size() || raw.empty()) {
    size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.push_back(n & 255); z.push_back(n >> 8); z.push_back(~n & 255); z.push_back((~n >> 8) & 255);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n; if (raw.empty()) break;
  }
  uint32_t ad = (b << 16) | a;
  for (int i = 3; i >= 0; i--) z.push_back((ad >> (i * 8)) & 255);
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  auto be = [&](uint32_t v) { uint8_t t[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; std::fwrite(t, 1, 4, f); };
  auto chunk = [&](const char* type, const std::vector<uint8_t>& d) {
    be(uint32_t(d.size()));
    std::vector<uint8_t> td(type, type + 4); td.insert(td.end(), d.begin(), d.end());
    std::fwrite(td.data(), 1, td.size(), f); be(crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
  };
  const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10}; std::fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr; for (int i = 3; i >= 0; i--) ihdr.push_back((uint32_t(w) >> (i * 8)) & 255);
  for (int i = 3; i >= 0; i--) ihdr.push_back((uint32_t(h) >> (i * 8)) & 255);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk("IHDR", ihdr); chunk("IDAT", z); chunk("IEND", {});
  std::fclose(f);
}
