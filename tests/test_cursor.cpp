#include <cstring>
#include "core/cursor.h"
#include "testutil.h"
using namespace m3k;

int main() {
  CHECK(dragScrollVelocity(0) == 0 && dragScrollVelocity(12) == 0 && dragScrollVelocity(13) == 13);
  CHECK(dragScrollVelocity(-13) == -13 && dragScrollVelocity(80) == 80 && dragScrollVelocity(81) == 80);
  CHECK(dragScrollVelocity(-500) == -80);
  // Thresholds differ on purpose (spec 8.3 rule 4): at |d| == 12 the cursor is a PAN arrow, velocity 0.
  CHECK(std::strcmp(panCursorName(12, 0), "PAN RIGHT ARROW") == 0 && dragScrollVelocity(12) == 0);
  CHECK(std::strcmp(panCursorName(11, -11), "EARLY MOVER CURSOR") == 0);
  CHECK(std::strcmp(panCursorName(0, -12), "PAN UP ARROW") == 0);
  CHECK(std::strcmp(panCursorName(-20, -20), "PAN UP LEFT ARROW") == 0);
  CHECK(std::strcmp(panCursorName(20, 20), "PAN DOWN RIGHT ARROW") == 0);
  // .cur header: reserved 0, type 2, count 1, entry: w h colors res hotX hotY size offset.
  uint8_t cur[22] = {0, 0, 2, 0, 1, 0, 32, 32, 2, 0, 5, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  int hx = -1, hy = -1;
  CHECK(curHotspot(cur, sizeof cur, hx, hy) && hx == 5 && hy == 7);
  cur[2] = 1;  // icon type: no hotspot
  CHECK(!curHotspot(cur, sizeof cur, hx, hy));
  // .cur decode: 1-bpp, palette black / white, AND mask transparent. 4x4 image (stored bottom-up), hotspot (1, 2).
  {
    std::vector<uint8_t> f(22 + 40 + 8 + 4 * 4 + 4 * 4, 0);
    f[2] = 2; f[4] = 1; f[6] = 4; f[7] = 4; f[10] = 1; f[12] = 2;        // type 2, 1 image, 4x4, hotspot (1,2)
    const uint32_t sz = uint32_t(f.size() - 22);
    for (int i = 0; i < 4; i++) f[14 + i] = uint8_t(sz >> (8 * i));
    f[18] = 22;                                                          // data offset
    uint8_t* d = &f[22];
    d[0] = 40; d[4] = 4; d[8] = 8; d[12] = 1; d[14] = 1;                 // header: 4 wide, 4+4 high, 1 plane, 1 bpp
    d[40 + 4] = 255; d[40 + 5] = 255; d[40 + 6] = 255;                   // palette[1] = white, palette[0] = black
    uint8_t* xr = d + 48; uint8_t* an = xr + 16;
    // file row 0 = image row 3 (bottom): XOR 1111 = white, AND 0000 (opaque). Image row 0 (file row 3): all transparent.
    xr[0] = 0xF0; an[0] = 0x00;
    xr[12] = 0x00; an[12] = 0xF0;
    CursorImage ci;
    CHECK(decodeCursorFile(f.data(), f.size(), ci) && ci.image.w == 4 && ci.image.h == 4 && ci.hotX == 1 && ci.hotY == 2);
    CHECK((ci.image.px[0] >> 24) == 0);                                  // top row transparent
    CHECK(ci.image.px[3 * 4] == rgba(255, 255, 255));                    // bottom row white
    CHECK(ci.image.px[1 * 4] == rgba(0, 0, 0));                          // XOR 0 / AND 0 = opaque black
    f[2] = 9;
    CHECK(!decodeCursorFile(f.data(), f.size(), ci));
  }
  TEST_MAIN_END();
}
