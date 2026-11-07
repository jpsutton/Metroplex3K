// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The schedule's hold wait (spec 14 6.3 rule 6) must never block the frame thread: with a modal window open the sim just does not
// advance. A regression here hangs (ctest times out) instead of failing.
#include "core/game.h"
#include "testutil.h"
using namespace m3k;

int main() {
  for (bool async : {false, true}) {
    Game game; game.setPersistence(false); game.startDemo(48);
    game.setAsyncSim(async);
    Input in;
    auto frames = [&](int n, uint32_t dt) { for (int i = 0; i < n; i++) { game.frame(in, 1024, 768, dt); in.keyDowns.clear(); } };
    frames(3, 16);
    game.setPaused(false);
    frames(30, 500);
    const int day0 = game.city().clock.jdn();
    game.openWindow("budget");               // a modal window raises the pending count
    frames(10, 16);
    CHECK(game.city().hold());
    frames(60, 2000);                         // two minutes of real time at speed 1: must neither hang nor advance the date
    CHECK(game.city().clock.jdn() == day0);
    game.closeAllWindows(); frames(3, 16);    // closes it
    game.setPaused(false);
    frames(60, 2000);
    CHECK(game.city().clock.jdn() > day0);   // time moves again
  }
  TEST_MAIN_END();
}
