// Checks every shipped city-view cursor (spec 08 section 8): the .cur decodes (loadCursor shows the .cur picture), its hotspot lies
// inside the image, and how the colour design preview (.bmp twin) differs from it (informational: the game shows the .cur).
//   probe_cursors [GAME_DIR] [sheet.png]
// The optional PNG is a contact sheet (8 per row, 4x scale) with each hotspot marked by a red cross.
#include <map>
#include "core/cursor.h"
#include "core/gamedata.h"
#include "probe_common.h"
#include "../png.h"
using namespace probe;

int main(int argc, char** argv) {
  fs::path root;
  int used = gameDirFromArgs(argc, argv, root);
  if (needInstall(root, "probe_cursors GAME_DIR [sheet.png]")) return 1;
  const std::string sheet = argc > 1 + used ? argv[1 + used] : "";
  GameData g; std::string err;
  if (!g.open(root, err)) { std::fprintf(stderr, "open: %s\n", err.c_str()); return 1; }
  fs::path dir = resolveCI(g.appsDir(), "Res/UI/Shared/Cursors");
  std::vector<std::string> names;
  for (const auto& e : fs::directory_iterator(dir)) if (lower(e.path().extension().string()) == ".cur") names.push_back(e.path().stem().string());
  std::sort(names.begin(), names.end());

  int invertCursors = 0, ok = 0, maskDiff = 0, hotOutside = 0, noImage = 0, nonStd = 0, nonZeroHot = 0;
  struct Cell { std::string name; CursorImage ci; };
  std::vector<Cell> cells;
  for (const auto& n : names) {
    CursorImage ci;
    fs::path cur = resolveCI(dir, n + ".cur");
    Bytes c; readFile(cur, c);
    uint16_t type = c.size() > 5 ? rd16(c.data() + 2) : 0;
    if (type != 2) { nonStd++; std::printf("%-34s icon-type .cur (type %u): no hotspot\n", n.c_str(), type); }
    if (!loadCursor(g, n, ci) || !ci.ok()) { noImage++; std::printf("%-34s NO IMAGE\n", n.c_str()); continue; }   // ci = the .cur picture
    bool bad = false;
    if (ci.hotX < 0 || ci.hotY < 0 || ci.hotX >= ci.image.w || ci.hotY >= ci.image.h) { hotOutside++; bad = true; std::printf("%-34s hotspot (%d,%d) outside %dx%d\n", n.c_str(), ci.hotX, ci.hotY, ci.image.w, ci.image.h); }
    if (ci.hotX || ci.hotY) nonZeroHot++;
    // 1-bpp type-2 file: ICONDIR(6) + entry(16) + BITMAPINFOHEADER(40) + 2-entry palette(8), then XOR (128) and AND (128) masks, bottom-up.
    CursorImage tw;   // the colour .bmp twin
    if (type == 2 && c.size() >= 326 && loadCursor(g, n, tw, true) && tw.image.w == 32 && tw.image.h == 32) {
      const uint8_t* xorm = c.data() + 22 + 40 + 8;
      const uint8_t* andm = xorm + 128;
      int opaqueMissing = 0, transparentDrawn = 0, inverted = 0, invertedDrawn = 0;
      for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
          const int bit = 7 - x % 8, row = (31 - y) * 4 + x / 8;
          const bool a = (andm[row] >> bit) & 1, xr = (xorm[row] >> bit) & 1;
          const bool imgOpaque = (tw.image.px[size_t(y) * 32 + x] >> 24) != 0;
          if (a && xr) { inverted++; invertedDrawn += imgOpaque; }          // AND=1 XOR=1: the pixel inverts what is under it
          else if (a) transparentDrawn += imgOpaque;                         // AND=1 XOR=0: transparent in the .cur
          else opaqueMissing += !imgOpaque;                                  // AND=0: drawn in the .cur
        }
      // Inverting pixels cannot be shown in a colour image; the .bmp draws them or leaves them out. Only a mismatch that is
      // not an inverting pixel counts as a difference.
      if (opaqueMissing || transparentDrawn) { maskDiff++; std::printf("%-34s .bmp preview differs from the .cur: %d .cur-opaque px transparent in the .bmp, %d .cur-transparent px drawn in it (%d inverting px)\n", n.c_str(), opaqueMissing, transparentDrawn, inverted); }
      else if (inverted) invertCursors++;
      (void)invertedDrawn;
    }
    if (!bad) ok++;
    cells.push_back({n, ci});
  }
  std::printf("%zu cursors: %d consistent, %d whose .bmp preview differs from the .cur, %d hotspots outside the image, %d without image, %d icon-type; %d have a non-zero hotspot; %d use inverting pixels (the .bmp twin cannot show them)\n",
              names.size(), ok, maskDiff, hotOutside, noImage, nonStd, nonZeroHot, invertCursors);
  if (!sheet.empty() && !cells.empty()) {
    const int S = 4, cw = 32 * S + 4, per = 8, rows = (int(cells.size()) + per - 1) / per;
    std::vector<uint32_t> px(size_t(cw) * per * (32 * S + 4) * rows, rgba(200, 200, 200));
    const int W = cw * per;
    for (size_t i = 0; i < cells.size(); i++) {
      const int ox = int(i % per) * cw + 2, oy = int(i / per) * (32 * S + 4) + 2;
      const Image& im = cells[i].ci.image;
      for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) {
        const uint32_t p = im.px[size_t(y) * im.w + x];
        if (!(p >> 24)) continue;
        for (int dy = 0; dy < S; dy++) for (int dx = 0; dx < S; dx++) px[size_t(oy + y * S + dy) * W + ox + x * S + dx] = p | 0xFF000000u;
      }
      const int hx = ox + cells[i].ci.hotX * S + S / 2, hy = oy + cells[i].ci.hotY * S + S / 2;
      for (int k = -5; k <= 5; k++) {
        if (hx + k >= 0 && hx + k < W) px[size_t(hy) * W + hx + k] = rgba(255, 0, 0);
        if (hy + k >= 0) px[size_t(hy + k) * W + hx] = rgba(255, 0, 0);
      }
    }
    writePng(sheet, W, (32 * S + 4) * rows, px);
    std::printf("contact sheet: %s (row-major, alphabetical)\n", sheet.c_str());
    for (size_t i = 0; i < cells.size(); i++) std::printf("%s%s", i % per ? " | " : "\n", cells[i].name.c_str());
    std::printf("\n");
  }
  return 0;
}
