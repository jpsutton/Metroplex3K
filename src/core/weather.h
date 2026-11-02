// Weather subsystem (#27): monthly season + temperature update (spec 14 §6.1 item 13 / Q-AT).
// No save records; state is runtime-only from calendar + map location.
#pragma once
#include <cstdint>
#include <functional>
#include "core/clock.h"   // SimRng

namespace m3k {

enum class Season : int { Winter = 0, Spring = 1, Summer = 2, Autumn = 3 };

struct Weather {
  // Map location; unset (NaN) → defaults lon 130°, lat 37° (spec 14 §6.1).
  double longitude = 130.0;
  double latitude = 37.0;
  bool locationSet = false;   // false → use the defaults above when reading

  Season season = Season::Winter;
  bool hasSeason = false;     // false until the first monthly run
  // Celsius internally [STATIC].
  float tempLowC = 0, tempMidC = 0, tempHighC = 0;

  std::function<void(Season)> onSeasonChange;

  void setLocation(double lon, double lat) {
    longitude = lon; latitude = lat; locationSet = true;
  }
  double lon() const { return locationSet ? longitude : 130.0; }
  double lat() const { return locationSet ? latitude : 37.0; }

  // Season quarter from calendar month 1–12; southern hemisphere (lat < 0) shifts month +6 first.
  static Season seasonOf(int month /*1..12*/, double latDeg);

  // Monthly cascade step: regenerate temps only when the season changes.
  // rng provides jitter; may be null (no jitter).
  bool monthEnd(int month /*1..12*/, SimRng* rng = nullptr);

  float tempLowF() const { return tempLowC * 1.8f + 32.f; }
  float tempMidF() const { return tempMidC * 1.8f + 32.f; }
  float tempHighF() const { return tempHighC * 1.8f + 32.f; }
};

}  // namespace m3k
