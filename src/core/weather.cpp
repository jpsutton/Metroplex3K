#include "core/weather.h"

#include <algorithm>
#include <cmath>

namespace m3k {

Season Weather::seasonOf(int month, double latDeg) {
  int m = month;
  if (m < 1) m = 1;
  if (m > 12) m = 12;
  // Spec 14 §6.1: southern hemisphere replaces month by (month+6) mod 12 (1..12, 0→12).
  if (latDeg < 0) {
    m = (m + 6) % 12;
    if (m == 0) m = 12;
  }
  return Season((m - 1) / 3);   // 1–3→0, 4–6→1, 7–9→2, 10–12→3
}

namespace {
// Spec 14 §6.1 Q-AU [STATIC]: (A,B,C, D,E,F, G,H,I); H unused by the sampler.
struct Coeffs { float A, B, C, D, E, F, G, H, I; };
const Coeffs kSeason[4] = {
  { 7, -10, 10, 13, -6, 10, 16, -6, 15},  // 0: months 1–3
  {13, -10, 10, 17, -6, 10, 21, -6, 15},  // 1: 4–6
  {15, -11, 14, 20, -6, 11, 28, -6, 17},  // 2: 7–9
  {13, -15, 10, 17, -6,  8, 21, -6, 15},  // 3: 10–12
};

constexpr float kKelvin = 273.15f;

float lerp(float lo, float hi, float t) { return lo + t * (hi - lo); }

void orderKelvinADG(float& A, float& D, float& G) {
  float aK = A + kKelvin, dK = D + kKelvin, gK = G + kKelvin;
  dK = std::max(dK, aK * 1.007f);
  gK = std::max(gK, dK * 1.007f);
  A = aK - kKelvin; D = dK - kKelvin; G = gK - kKelvin;
}

void smoothKelvinSamples(float& T0, float& T1, float& T2) {
  float t0 = T0 + kKelvin, t1 = T1 + kKelvin, t2 = T2 + kKelvin;
  if (t2 <= t0 * 1.015f) t2 = t0 * 1.015f;
  if (t1 <= t0 * 1.007f) t1 = t0 * 1.007f;
  if (t2 >= t1 * 1.007f) t2 = t1 * 1.007f;
  T0 = t0 - kKelvin; T1 = t1 - kKelvin; T2 = t2 - kKelvin;
}
}  // namespace

bool Weather::monthEnd(int month, SimRng* rng) {
  Season next = seasonOf(month, lat());
  if (hasSeason && next == season) return false;

  season = next;
  hasSeason = true;
  Coeffs c = kSeason[int(season)];
  float A = c.A, B = c.B, C = c.C, D = c.D, E = c.E, F = c.F, G = c.G, I = c.I;

  // Post-table adjustment of A (RandRange); shipped rows always take the A < 57 branch.
  auto randRange = [&](double lo, double hi) -> float {
    if (!rng) return float(0.5 * (lo + hi));
    return float(rng->randRange(lo, hi));
  };
  if (A < -40.f) A = randRange(-2, 2) - 40.f;
  else if (A < 57.f) A = randRange(-2, 2) + 55.f;

  orderKelvinADG(A, D, G);

  float t = 0.5f;
  if (rng) {
    double U = rng->nextNormal();
    t = float(std::min(11.99, std::max(0.0, U + 6.0)) / 12.0);
  }

  float T0 = lerp(A + B, A + C, t);
  float T1 = lerp(D + E, D + F, t);
  float T2 = lerp(B + G, G + I, t);
  smoothKelvinSamples(T0, T1, T2);

  tempLowC = T0; tempMidC = T1; tempHighC = T2;
  if (onSeasonChange) onSeasonChange(season);
  return true;
}

}  // namespace m3k
