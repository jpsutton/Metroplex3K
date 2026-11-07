// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/image.h"
#include "core/peicon.h"
#include "core/refpack.h"
#include "testutil.h"
using namespace m3k;

// Build a type-0 RLE sprite payload for a tiny image (format 7).
static Bytes makeSprite(int w, int h, const std::vector<uint16_t>& pix /*w*h, 0xF81F=transparent*/) {
  Bytes body;
  std::vector<uint16_t> stored; Bytes rows;
  for (int y = 0; y < h; y++) {
    int a = 0, b = w;
    while (a < w && pix[y * w + a] == 0xF81F) a++;
    while (b > a && pix[y * w + b - 1] == 0xF81F) b--;
    uint16_t cf = uint16_t(b - a);
    bool solid = true; for (int x = a; x < b; x++) if (pix[y * w + x] == 0xF81F) solid = false;
    if (solid) cf |= 0x8000;
    put32(rows, uint32_t(stored.size())); put16(rows, uint16_t(a)); put16(rows, cf);
    for (int x = a; x < b; x++) stored.push_back(pix[y * w + x]);
  }
  put32(body, uint32_t(0x10 + rows.size() + stored.size() * 2));
  put16(body, w); put16(body, h); put16(body, 4); put16(body, 7); put16(body, 0xF81F); put16(body, 0);
  body.insert(body.end(), rows.begin(), rows.end());
  for (auto v : stored) put16(body, v);
  Bytes comp = refpackEncode(body);
  Bytes out = {7, 1, 0, 0};
  put32(out, 0x00080000); put32(out, w); put32(out, h); put32(out, uint32_t(comp.size() + 4));
  out.insert(out.end(), comp.begin(), comp.end());
  return out;
}

int main() {
  std::vector<uint16_t> pix = {0xF81F, 0xF800, 0xF81F, 0x07E0, 0x001F, 0xFFFF};
  Bytes sp = makeSprite(3, 2, pix);
  auto d = decodeSprite(sp.data(), sp.size());
  CHECK(!d.isPlane && d.image.w == 3 && d.image.h == 2);
  CHECK(d.image.px[0] == 0);                         // key colour transparent
  CHECK(d.image.px[1] == rgba(255, 0, 0));           // red
  CHECK(d.image.px[3] == rgba(0, 255, 0));           // green
  CHECK(d.image.px[5] == rgba(255, 255, 255));

  // Alpha plane.
  Bytes raw = {0, 31, 16, 8};
  Bytes comp = refpackEncode(raw);
  Bytes pl = {3, 0, 0, 0}; put32(pl, 0x10080000); put32(pl, 2); put32(pl, 2); put32(pl, uint32_t(comp.size() + 4));
  pl.insert(pl.end(), comp.begin(), comp.end());
  auto p = decodeSprite(pl.data(), pl.size());
  CHECK(p.isPlane && p.plane.v[1] == 31 && p.plane.v[3] == 8);

  // BMP 24-bit 2x1, bottom-up, magenta key.
  Bytes bmp(54 + 8, 0);
  bmp[0] = 'B'; bmp[1] = 'M'; wr32(&bmp[10], 54); wr32(&bmp[14], 40); wr32(&bmp[18], 2); wr32(&bmp[22], 1);
  wr16(&bmp[26], 1); wr16(&bmp[28], 24);
  uint8_t px[8] = {0, 0, 255, 255, 0, 255, 0, 0};  // red (BGR), magenta, 2 pad
  for (int i = 0; i < 8; i++) bmp[54 + i] = px[i];
  auto im = decodeBmp(bmp.data(), bmp.size(), true);
  CHECK(im.px[0] == rgba(255, 0, 0) && im.px[1] == 0);

  // Icon DIB, 4 bpp 2x2 (height doubled for the AND mask), palette 0 = blue, 1 = red; bottom-up rows.
  {
    Bytes dib(40 + 16 * 4 + 4 * 2 + 4 * 2, 0);
    wr32(&dib[0], 40); wr32(&dib[4], 2); wr32(&dib[8], 4); wr16(&dib[12], 1); wr16(&dib[14], 4);
    dib[40 + 0] = 255;               // entry 0: B=255 (blue)
    dib[40 + 4 + 2] = 255;           // entry 1: R=255 (red)
    size_t xo = 40 + 64, ao = xo + 8;
    dib[xo + 0] = 0x01;              // bottom row: blue, red
    dib[xo + 4] = 0x10;              // top row: red, blue
    dib[ao + 0] = 0x40;              // bottom row: second pixel transparent
    Image ic;
    CHECK(decodeIconDib(dib.data(), dib.size(), ic) && ic.w == 2 && ic.h == 2);
    CHECK(ic.px[0] == rgba(255, 0, 0) && ic.px[1] == rgba(0, 0, 255));   // top row
    CHECK(ic.px[2] == rgba(0, 0, 255) && ic.px[3] == 0);                 // bottom row, masked pixel
    Bytes notPe = {'M', 'Z'};
    CHECK(!loadPeIcon(notPe, 1001, 16, ic));
  }
  TEST_MAIN_END();
}
