// The game's simulation generators (spec/14-sim-clock.md section 8): integer LCG with Rand/RandInt,
// plus the separate float generator behind RandRange. Both states are seeded with the same value.
#pragma once
#include <cstdint>

namespace m3k {

class GameRng {
 public:
  explicit GameRng(uint32_t seedValue = 1) { seed(seedValue); }
  void seed(uint32_t x) { s_ = x; t_ = x; }

  // Raw integer output R (bits 16..47 of s*0x41C64E6D+0x3039).
  uint32_t next() {
    uint64_t v = uint64_t(s_) * 0x41C64E6Du + 0x3039u;
    s_ = uint32_t(v);
    return uint32_t(v >> 16);
  }
  // Rand(n): 0 for n == 0; R mod n for n < 0xFFFFFF; otherwise rejection sampling in [0, n].
  uint32_t rand(uint32_t n) {
    if (n == 0) return 0;
    if (n < 0xFFFFFFu) return next() % n;
    uint32_t r;
    do { r = next(); } while (r > n);
    return r;
  }
  // RandInt(a, b) = a + Rand(b - a): [a, b-1].
  int randInt(int a, int b) { return a + int(rand(uint32_t(b - a))); }
  // Float generator step: u in [0,1).
  double nextFloat() {
    if (t_ == 0) t_ = 0x12345678u;
    t_ = uint32_t(uint64_t(t_) * 0x278DDE6Du);
    return double(t_) / 4294967296.0;
  }
  // RandRange(a, b) = a + (b-a)*u (float).
  float randRange(float a, float b) { return a + (b - a) * float(nextFloat()); }
  // Integer use of the range generator (exclusive upper bound, truncated).
  int rangeInt(int a, int b) { return a + int(float(b - a) * float(nextFloat())); }
  // Centre-weighted range [a,b] (spec 21 section 13).
  int centreWeighted(int a, int b) {
    int m = (b - a) / 2;
    int r = int(rand(uint32_t(m)));
    int r2 = int(rand(uint32_t(m)));
    if (r2 < r) r = r2;
    if (rand(2) == 1) r = -r;
    return a + m + r;
  }
  uint32_t stateInt() const { return s_; }
  uint32_t stateFloat() const { return t_; }

 private:
  uint32_t s_ = 1, t_ = 1;
};

}  // namespace m3k
