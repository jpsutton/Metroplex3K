#include "core/weather.h"

#include <algorithm>
#include <cmath>

namespace m3k {

Season Weather::seasonOf(int month, double latDeg) {
  int m = month;
  if (m < 1) m = 1;
  if (m > 12) m = 12;
  if (latDeg < 0) m = ((m - 1 + 6) % 12) + 1;   // southern: shift +6
  if (m <= 3) return Season::Winter;
  if (m <= 6) return Season::Spring;
  if (m <= 9) return Season::Summer;
  return Season::Autumn;
}

namespace {
// Season base low/mid/high °C [INFERRED]: tables not in the Q-AT answer; temperate mid-latitude.
struct Bases { float lo, mid, hi; };
const Bases kSeasonBase[4] = {
  {-2.f, 4.f, 10.f},   // winter
  {6.f, 13.f, 19.f},   // spring
  {16.f, 23.f, 29.f},  // summer
  {7.f, 14.f, 20.f},   // autumn
};

float jitter(SimRng* rng, float base) {
  if (!rng) return base;
  // Small ±2 °C jitter [INFERRED magnitude].
  int j = int(rng->rand(5)) - 2;   // -2..+2
  return base + float(j);
}
}  // namespace

bool Weather::monthEnd(int month, SimRng* rng) {
  Season next = seasonOf(month, lat());
  if (hasSeason && next == season) return false;

  season = next;
  hasSeason = true;
  const Bases& b = kSeasonBase[int(season)];
  float lo = jitter(rng, b.lo), mid = jitter(rng, b.mid), hi = jitter(rng, b.hi);
  // Mild smoothing / order: sort then pull mid toward the mean of lo/hi.
  float vals[3] = {lo, mid, hi};
  std::sort(vals, vals + 3);
  lo = vals[0]; hi = vals[2];
  mid = 0.5f * (vals[1] + 0.5f * (lo + hi));
  if (mid < lo) mid = lo;
  if (mid > hi) mid = hi;
  tempLowC = lo; tempMidC = mid; tempHighC = hi;
  if (onSeasonChange) onSeasonChange(season);
  return true;
}

}  // namespace m3k
