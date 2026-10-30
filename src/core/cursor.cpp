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

bool loadCursor(GameData& g, const std::string& name, CursorImage& out) {
  fs::path dir = resolveCI(g.appsDir(), "Res/UI/Shared/Cursors");
  if (dir.empty()) return false;
  fs::path bmp = resolveCI(dir, name + ".bmp");
  if (bmp.empty()) return false;
  Bytes b;
  if (!readFile(bmp, b)) return false;
  try { out.image = decodeBmp(b.data(), b.size(), true); } catch (const FormatError&) { return false; }
  // (254,0,254) is also used as a key colour by one file.
  for (auto& px : out.image.px) if ((px & 0xFFFFFF) == 0xFE00FE) px = 0;
  out.hotX = out.hotY = 0;
  fs::path cur = resolveCI(dir, name + ".cur");
  Bytes c;
  if (!cur.empty() && readFile(cur, c)) curHotspot(c.data(), c.size(), out.hotX, out.hotY);
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
