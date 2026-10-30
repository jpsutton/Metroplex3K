#include "core/game.h"
#include "core/hotkeys.h"
#include "testutil.h"
using namespace m3k;

int main() {
  HotkeyTable t;
  int n = t.parse("0 0 0 81 = 583838163 1 0 0\r\n"      // Q -> cmd 1
                  "1 0 0 66 = 583838163 20 0 0\r\n"      // Ctrl+B -> 20
                  "0 0 0 81 = 583838163 4 0 0\r\n"       // replaces Q
                  "2 0 0 66 = 583838163 20 0 0\r\n"      // modifier > 1: ignored
                  "0 0 0 300 = 583838163 20 0 0\r\n"     // vk > 255: ignored
                  "garbage line\r\n");
  CHECK(n == 3);
  CHECK(t.find(81, 0) && t.find(81, 0)->cmd == 4);
  CHECK(t.find(66, HK_CTRL) && t.find(66, HK_CTRL)->cmd == 20);
  CHECK(!t.find(66, 0));
  HotkeyTable d; d.loadDefaults();
  CHECK(d.find('Q', 0)->cmd == 1);
  CHECK(d.find('R', HK_ALT)->cmd == 14);
  CHECK(d.find('S', HK_CTRL | HK_ALT)->cmd == 31);
  uint32_t p = 0;
  CHECK(hotkeyToolClass(1, &p) == 0x42E88586);
  CHECK(hotkeyToolClass(28, &p) == 0x610438A4 && p == 0x83172AD7);
  CHECK(hotkeyToolClass(999, &p) == 0);

  // Through Game: pressing Q arms residential, Esc-binding (cmd 85) cancels.
  Game g; g.setPersistence(false); g.startDemo(32);
  Input in;
  auto frame = [&]() { g.frame(in, 1280, 800, 16); in.keyDowns.clear(); };
  frame();
  in.keyDowns.push_back({'Q', 0}); frame();
  in.keyDowns.push_back({27, 0}); frame();
  in.keyDowns.push_back({188, 0}); frame();   // ',' slower
  in.keyDowns.push_back({191, 0}); frame();   // '/' pause toggle
  CHECK(g.paused() == false);
  TEST_MAIN_END();
}
