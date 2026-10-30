// Synthetic UI art for skin tests and the skin demo: builds IXF image resources of type 62B9DA24
// (raw RGB565, kind 1) with recognisable colours.
#pragma once
#include <functional>
#include "core/bytes.h"
#include "core/ixf.h"
#include "core/skin.h"

namespace skinfix {
using namespace m3k;

inline uint16_t c565(int r, int g, int b) { return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
constexpr uint16_t MAGENTA = 0xF81F;

inline Bytes uiImage(int w, int h, const std::function<uint16_t(int, int)>& px) {
  Bytes b;
  put32(b, 1); put32(b, uint32_t(w) * h * 2); put32(b, uint32_t(w)); put32(b, uint32_t(h)); put32(b, 7);
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) put16(b, px(x, y));
  return b;
}
inline void addImg(IxfWriter& wr, uint32_t g, uint32_t i, int w, int h, const std::function<uint16_t(int, int)>& px) {
  wr.add({T_UI_IMAGE, g, i}, uiImage(w, h, px));
}
inline void addSolid(IxfWriter& wr, uint32_t g, uint32_t i, int w, int h, uint16_t c) {
  addImg(wr, g, i, w, h, [c](int, int) { return c; });
}
// Strip of `cells` cells, cell k filled with colour palette[k % 8]; one-pixel magenta border on top row.
inline uint16_t cellCol(int k) {
  static const uint16_t p[8] = {c565(80, 80, 80), c565(200, 200, 210), c565(60, 90, 200), c565(240, 200, 60),
                                c565(200, 60, 60), c565(60, 180, 60), c565(160, 60, 180), c565(60, 180, 180)};
  return p[k & 7];
}
inline void addStrip(IxfWriter& wr, uint32_t g, uint32_t i, int w, int h, int cells) {
  int cw = w / cells;
  addImg(wr, g, i, w, h, [=](int x, int y) { return (y == 0 && x % cw == 0) ? MAGENTA : cellCol(x / cw); });
}

// Whole synthetic skin: every group the loader looks at.
inline Bytes build() {
  using namespace skin_group;
  IxfWriter w;
  // Tool menu column: 480 set (00640480) and big set (02F78C6D / 01024768).
  addImg(w, Ui640, 0xFFFF, 96, 334, [](int x, int y) { return (x < 2 || y < 2) ? c565(255, 255, 255) : c565(90, 100, 140); });
  addImg(w, UiBig, 0xFFFF, 96, 417, [](int x, int y) { return (x < 2 || y < 2) ? c565(255, 255, 255) : c565(100, 90, 140); });
  addSolid(w, UiBig, 0xFFFE, 96, 25, c565(70, 70, 110));
  addSolid(w, Ui1024, 0xFFFD, 96, 97, c565(60, 60, 90));
  addSolid(w, Ui1024, 0xFFFE, 96, 96, c565(70, 70, 100));
  addSolid(w, Ui1024, 0xFFFC, 96, 32, c565(80, 80, 110));
  for (uint32_t n = 1; n <= 9; n++) {
    uint32_t base = 0x10000000u | (n << 16);
    addSolid(w, UiBig, base | 1, 56, 32, c565(255, 255, 0));
    addSolid(w, UiBig, base | 5, 56, 32, c565(255, 128, 0));
    addSolid(w, UiBig, base | 3, 96, 120 + 20 * int(n), c565(40, 120, 60));
    addSolid(w, Ui640, base | 3, 96, 100 + 20 * int(n), c565(40, 100, 80));
  }
  auto tools = toolMenuLayout(1000);
  for (int n = 0; n < 9; n++)
    for (uint32_t id : tools.subIds[n]) { addStrip(w, UiBig, id, 144, 36, 4); addStrip(w, Ui640, id, 144, 36, 4); }
  // Bottom bars.
  addImg(w, Ui640, 0x82790741, 440, 64, [](int x, int y) { return y == 0 ? c565(255, 255, 255) : c565(30, 40, 90); });
  addImg(w, Ui800, 0x82790741, 600, 56, [](int x, int y) { return y == 0 ? c565(255, 255, 255) : c565(30, 50, 90); });
  addImg(w, Ui1024, 0x82790741, 824, 64, [](int x, int y) { return y == 0 ? c565(255, 255, 255) : c565(30, 60, 90); });
  addSolid(w, UiBig, 0x82790742, 16, 64, c565(20, 30, 60));
  addImg(w, UiBig, 0x42FB77F6, 41, 88, [](int x, int y) { return y == 51 ? c565(255, 255, 255) : c565(20, 20, 30); });
  for (uint32_t k = 0; k < 6; k++) addStrip(w, UiBig, 0x52FB06E4 + k, k < 2 ? 192 : 40, k < 2 ? 23 : 11, k < 2 ? 4 : 4);
  addStrip(w, UiBig, 0x52FB06EA, 160, 39, 4);
  // Controls.
  addStrip(w, Sys, 0xE2B66DB8, 216, 33, 4);
  addStrip(w, Sys, 0xC2D6E93A, 104, 13, 8);
  addStrip(w, Sys, 0x0000000A, 96, 12, 8);
  addStrip(w, Gonzo, 0x01, 216, 31, 4);
  // Front end.
  addSolid(w, Main, 0x22721000, 640, 480, c565(20, 60, 120));
  addSolid(w, Main, 0x01140000, 800, 600, c565(20, 90, 120));
  for (uint32_t k = 0; k < 9; k++) { addStrip(w, Main, 0x22729930 + k, 160, 24, 4); addStrip(w, Main, 0x01140001 + k, 200, 30, 4); }
  return w.build();
}
}  // namespace skinfix
