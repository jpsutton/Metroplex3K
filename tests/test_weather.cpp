// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include "core/weather.h"
#include "testutil.h"

using namespace m3k;

static void testSeasonOf() {
  CHECK(Weather::seasonOf(1, 37) == Season::Winter);
  CHECK(Weather::seasonOf(4, 37) == Season::Spring);
  CHECK(Weather::seasonOf(7, 37) == Season::Summer);
  CHECK(Weather::seasonOf(10, 37) == Season::Autumn);
  CHECK(Weather::seasonOf(12, 37) == Season::Autumn);
  // Southern: (month+6) mod 12.
  CHECK(Weather::seasonOf(1, -33) == Season::Summer);   // 1→7
  CHECK(Weather::seasonOf(7, -33) == Season::Winter);   // 7→1
  CHECK(Weather::seasonOf(12, -33) == Season::Spring);  // 12→6
}

static void testMonthEndTables() {
  Weather w;
  CHECK(w.lon() == 130.0 && w.lat() == 37.0);
  SimRng rng(1);
  CHECK(w.monthEnd(1, &rng));
  CHECK(w.hasSeason && w.season == Season::Winter);
  // After A←55±2 and kelvin order + sample smooth, expect ordered low≤mid≤high in a sensible band.
  CHECK(w.tempLowC <= w.tempMidC + 0.01f);
  CHECK(w.tempMidC <= w.tempHighC + 0.01f);
  // Post-table A≈55±2 dominates; readings land in the mid-50s–low-60s °C band.
  CHECK(w.tempLowC > 40.f && w.tempHighC < 80.f);
  float lo0 = w.tempLowC;
  CHECK(!w.monthEnd(2, &rng));
  CHECK(w.tempLowC == lo0);
  CHECK(w.monthEnd(4, &rng));
  CHECK(w.season == Season::Spring);
  CHECK(std::abs(w.tempMidF() - (w.tempMidC * 1.8f + 32.f)) < 0.01f);
  // Deterministic path (no rng): A becomes 55, t=0.5.
  Weather d;
  CHECK(d.monthEnd(7, nullptr));
  CHECK(d.season == Season::Summer);
  CHECK(d.tempLowC <= d.tempMidC && d.tempMidC <= d.tempHighC);
}

int main() {
  testSeasonOf();
  testMonthEndTables();
  TEST_MAIN_END();
}
