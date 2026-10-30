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
  TEST_MAIN_END();
}
