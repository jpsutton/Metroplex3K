#include "core/cursor.h"

#include <cstdlib>

namespace m3k {

bool curHotspot(const uint8_t* p, size_t n, int& hx, int& hy) {
  if (n < 22) return false;
  uint16_t type = rd16(p + 2), count = rd16(p + 4);
  if (type != 2 || count < 1) return false;
  hx = rd16(p + 6 + 4); hy = rd16(p + 6 + 6);
  return true;
}

bool decodeCursorFile(const uint8_t* p, size_t n, CursorImage& out) {
  if (n < 22) return false;
  const uint16_t type = rd16(p + 2), count = rd16(p + 4);
  if ((type != 1 && type != 2) || count < 1) return false;
  const uint32_t size = rd32(p + 6 + 8), off = rd32(p + 6 + 12);
  if (off + 40 > n || size < 40 || off + size > n) return false;
  const uint8_t* d = p + off;
  const uint32_t hdr = rd32(d);
  const int w = int(rd32(d + 4)), h2 = int(rd32(d + 8)), bpp = rd16(d + 14);
  if (hdr < 40 || w <= 0 || w > 256 || h2 <= 0 || h2 % 2 || (bpp != 1 && bpp != 4 && bpp != 8)) return false;
  const int h = h2 / 2;
  uint32_t colors = rd32(d + 32);
  if (!colors) colors = 1u << bpp;
  const size_t palOff = hdr, xorOff = palOff + size_t(colors) * 4;
  const size_t xorStride = ((size_t(w) * bpp + 31) / 32) * 4, andStride = ((size_t(w) + 31) / 32) * 4;
  const size_t andOff = xorOff + xorStride * size_t(h);
  if (andOff + andStride * size_t(h) > size) return false;
  out.image = Image(); out.image.w = w; out.image.h = h; out.image.px.assign(size_t(w) * h, 0);
  for (int y = 0; y < h; y++) {
    const int row = h - 1 - y;   // bottom-up
    for (int x = 0; x < w; x++) {
      const bool transparent = (d[andOff + andStride * size_t(row) + size_t(x) / 8] >> (7 - x % 8)) & 1;
      const uint8_t* xr = d + xorOff + xorStride * size_t(row);
      int idx = bpp == 8 ? xr[x] : bpp == 4 ? ((xr[x / 2] >> (x % 2 ? 0 : 4)) & 15) : ((xr[x / 8] >> (7 - x % 8)) & 1);
      if (uint32_t(idx) >= colors) idx = 0;
      const uint8_t* q = d + palOff + size_t(idx) * 4;   // BGRx
      // AND = 1 with a non-black pixel would invert the screen; none of the shipped cursors does, so it draws as the pixel.
      if (transparent && (bpp != 1 || idx == 0)) continue;
      out.image.px[size_t(y) * w + x] = rgba(q[2], q[1], q[0]);
    }
  }
  out.hotX = out.hotY = 0;
  if (type == 2) curHotspot(p, n, out.hotX, out.hotY);
  return true;
}

bool loadCursor(GameData& g, const std::string& name, CursorImage& out, bool colourTwin) {
  fs::path dir = resolveCI(g.appsDir(), "Res/UI/Shared/Cursors");
  if (dir.empty()) return false;
  fs::path cur = resolveCI(dir, name + ".cur");
  Bytes c;
  const bool haveCur = !cur.empty() && readFile(cur, c);
  if (haveCur && !colourTwin && decodeCursorFile(c.data(), c.size(), out)) return true;
  fs::path bmp = resolveCI(dir, name + ".bmp");
  if (bmp.empty()) return false;
  Bytes b;
  if (!readFile(bmp, b)) return false;
  try { out.image = decodeBmp(b.data(), b.size(), true); } catch (const FormatError&) { return false; }
  // (254,0,254) is also used as a key colour by one file.
  for (auto& px : out.image.px) if ((px & 0xFFFFFF) == 0xFE00FE) px = 0;
  out.hotX = out.hotY = 0;
  if (haveCur) curHotspot(c.data(), c.size(), out.hotX, out.hotY);
  return true;
}

int dragScrollVelocity(int d) {
  int a = std::abs(d), s = d < 0 ? -1 : 1;
  if (a > 80) return 80 * s;
  if (a > 12) return d;
  return 0;
}

const char* panCursorName(int dx, int dy) {
  int xs = dx <= -12 ? -1 : dx >= 12 ? 1 : 0, ys = dy <= -12 ? -1 : dy >= 12 ? 1 : 0;
  if (!xs && !ys) return "EARLY MOVER CURSOR";
  if (!xs) return ys < 0 ? "PAN UP ARROW" : "PAN DOWN ARROW";
  if (!ys) return xs < 0 ? "PAN LEFT ARROW" : "PAN RIGHT ARROW";
  if (xs < 0) return ys < 0 ? "PAN UP LEFT ARROW" : "PAN DOWN LEFT ARROW";
  return ys < 0 ? "PAN UP RIGHT ARROW" : "PAN DOWN RIGHT ARROW";
}

}  // namespace m3k
