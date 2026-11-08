// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/world.h"
#include "core/city.h"
#include "testutil.h"

using namespace m3k;

static void testTableKeying() {
  World w;
  w.init(8, 8);
  w.seaLevel = 40;
  for (auto& a : w.alt) a = 100;
  for (auto& v : w.water) v = 0;

  // Deep at (3,5): water=40 (==sea), ground=20 (<= 40-8), water>=8.
  w.water[w.vIdx(3, 5)] = 40;
  w.alt[w.vIdx(3, 5)] = 20;

  // Shallow / wrong sea: water==sea but ground too high.
  w.water[w.vIdx(1, 1)] = 40;
  w.alt[w.vIdx(1, 1)] = 35;  // 35 > 40-8

  // Inland lake above sea level.
  w.water[w.vIdx(2, 2)] = 50;
  w.alt[w.vIdx(2, 2)] = 30;

  CHECK(w.deepWaterAt(3, 5));
  CHECK(!w.deepWaterAt(1, 1));
  CHECK(!w.deepWaterAt(2, 2));
  CHECK(!w.deepWaterAt(0, 0));

  const auto& t = w.deepWaterTable();
  CHECK(t.size() == 8 * 8);
  CHECK(t[size_t(5) * 8 + 3] == 1);
  CHECK(t[size_t(1) * 8 + 1] == 0xFFFF);
  CHECK(t[0] == 0xFFFF);

  // Out of W×W / map.
  CHECK(!w.deepWaterAt(-1, 0));
  CHECK(!w.deepWaterAt(8, 0));

  // Invalidate rebuilds after terrain edit.
  w.alt[w.vIdx(3, 5)] = 100;
  w.invalidateDeepWater();
  CHECK(!w.deepWaterAt(3, 5));
}

static void testViaCityAdapter() {
  World w;
  w.init(16, 16);
  w.seaLevel = 30;
  for (auto& a : w.alt) a = 50;
  for (auto& v : w.water) v = 0;
  w.water[w.vIdx(7, 4)] = 30;
  w.alt[w.vIdx(7, 4)] = 10;

  CityConfig cfg;
  City c;
  c.init(&w, nullptr, cfg, 1);
  // Growth adapter is private; deepWaterAt on World is the source of truth used by it.
  CHECK(w.deepWaterAt(7, 4));
  CHECK(c.world()->deepWaterTable()[size_t(4) * 16 + 7] == 1);
}

int main() {
  testTableKeying();
  testViaCityAdapter();
  TEST_MAIN_END();
}
