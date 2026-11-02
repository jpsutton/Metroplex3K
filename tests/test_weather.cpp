#include <cmath>
#include "core/weather.h"
#include "testutil.h"

using namespace m3k;

static void testSeasonOf() {
  CHECK(Weather::seasonOf(1, 37) == Season::Winter);
  CHECK(Weather::seasonOf(4, 37) == Season::Spring);
  CHECK(Weather::seasonOf(7, 37) == Season::Summer);
  CHECK(Weather::seasonOf(10, 37) == Season::Autumn);
  // Southern hemisphere: month +6 before quartering.
  CHECK(Weather::seasonOf(1, -33) == Season::Summer);   // Jan → Jul
  CHECK(Weather::seasonOf(7, -33) == Season::Winter);   // Jul → Jan
}

static void testMonthEnd() {
  Weather w;
  CHECK(w.lon() == 130.0 && w.lat() == 37.0);
  SimRng rng(1);
  CHECK(w.monthEnd(1, &rng));   // first run always regenerates
  CHECK(w.hasSeason && w.season == Season::Winter);
  CHECK(w.tempLowC <= w.tempMidC && w.tempMidC <= w.tempHighC);
  float lo0 = w.tempLowC;
  CHECK(!w.monthEnd(2, &rng));  // same season → temps unchanged
  CHECK(w.tempLowC == lo0);
  CHECK(w.monthEnd(4, &rng));   // spring
  CHECK(w.season == Season::Spring);
  // Fahrenheit conversion.
  CHECK(std::abs(w.tempMidF() - (w.tempMidC * 1.8f + 32.f)) < 0.01f);
  int changes = 0;
  w.onSeasonChange = [&](Season) { changes++; };
  w.monthEnd(7, &rng);
  CHECK(changes == 1 && w.season == Season::Summer);
}

int main() {
  testSeasonOf();
  testMonthEnd();
  TEST_MAIN_END();
}
