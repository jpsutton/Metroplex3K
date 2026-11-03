#include <cmath>
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

  // View keys (spec 08 5.4, [OBSERVED]): PageDown turns the map clockwise as seen: the world corner that was at the screen's
  // bottom goes to the left corner. PageUp undoes it. Keypad 1 = zoom 0 (farthest) ... keypad 5 = zoom 4. Ctrl+G opens the layers dialog.
  {
    Game v; v.setPersistence(false); v.startDemo(32);
    Input vi;
    auto vf = [&](int n = 1) { for (int i = 0; i < n; i++) { v.frame(vi, 1280, 800, 16); vi.keyDowns.clear(); } };
    vf(2);
    auto screenX = [&](int rot, int wx, int wz) { int u, w; Renderer::worldToViewVertex(rot, 32, 32, wx, wz, u, w); return w - u; };   // < 0 = left
    CHECK(v.camera().rot == 0 && screenX(0, 32, 32) == 0);                  // the bottom corner (32,32) sits at the bottom
    vi.keyDowns.push_back({34, 0}); vf();
    CHECK(screenX(v.camera().rot, 32, 32) < 0);                              // now at the left corner
    vi.keyDowns.push_back({33, 0}); vf();
    CHECK(v.camera().rot == 0);
    for (int k = 0; k < 5; k++) { vi.keyDowns.push_back({97 + k, 0}); vf(); CHECK(v.camera().zoom == k); }
    CHECK(!v.layersDialogOpen());
    vi.keyDowns.push_back({'G', HK_CTRL}); vf(); CHECK(v.layersDialogOpen());
    vi.keyDowns.push_back({'G', HK_CTRL}); vf(); CHECK(!v.layersDialogOpen());
    // F12 hides the panels; the round "show UI" button (bottom-right) brings them back.
    CHECK(!v.panelsHidden());
    vi.keyDowns.push_back({123, 0}); vf(2); CHECK(v.panelsHidden());
    vi.mx = 1280 - 16; vi.my = 800 - 16; vf();
    vi.pressed = vi.down = true; vf(); vi.pressed = false; vi.released = true; vi.down = false; vf();
    CHECK(!v.panelsHidden());
    vi.released = false;
    vi.keyDowns.push_back({123, 0}); vf(); CHECK(v.panelsHidden());
    vi.keyDowns.push_back({123, 0}); vf(); CHECK(!v.panelsHidden());
  }

  // Keyboard scroll (spec 25 10 [STATIC]): 32 px per view update at a fixed 60 Hz; one key or two perpendicular keys scroll,
  // two opposite keys or three keys do not.
  {
    Game v; v.setPersistence(false); v.startDemo(64);
    v.setView(32, 32, 2);
    Input vi;
    auto vf = [&](int n = 1) { for (int i = 0; i < n; i++) { v.frame(vi, 1280, 800, 16); vi.keyDowns.clear(); } };
    vf(3);
    auto pos = [&] { return std::make_pair(v.camera().u, v.camera().v); };
    auto moved = [&](bool l, bool r, bool u, bool d) {
      auto a = pos(); vi.held[0] = l; vi.held[1] = r; vi.held[2] = u; vi.held[3] = d; vf(30);
      auto b = pos(); vi.held[0] = vi.held[1] = vi.held[2] = vi.held[3] = false; vf(1);
      v.setView(32, 32, 2); vf(2);
      return std::fabs(b.first - a.first) + std::fabs(b.second - a.second);
    };
    CHECK(moved(false, true, false, false) > 0.5f);
    CHECK(moved(true, false, false, false) > 0.5f);
    CHECK(moved(false, true, true, false) > 0.5f);                      // perpendicular pair: diagonal
    CHECK(moved(true, true, false, false) == 0.f);                      // opposite keys
    CHECK(moved(false, false, true, true) == 0.f);
    CHECK(moved(true, true, true, false) == 0.f);                       // three keys
    // Fixed rate: the same wall-clock time scrolls the same distance whatever the frame length.
    auto run = [&](int frames, uint32_t dt) {
      v.setView(32, 32, 2); v.frame(vi, 1280, 800, 16);
      auto a = pos(); vi.held[1] = true;
      for (int i = 0; i < frames; i++) v.frame(vi, 1280, 800, dt);
      vi.held[1] = false; auto b = pos();
      return b.second - a.second;
    };
    const float slow = run(15, 16), fast = run(30, 8);                  // both 0.24 s
    CHECK(slow > 0 && std::fabs(slow - fast) < slow * 0.12f);
  }
  TEST_MAIN_END();
}
