// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Random terrain generator for New City. Follows spec/11-world-terrain.md section 3 exactly
// (same RNG, same call order), so equal seeds give the original's terrain.
#pragma once
#include <cstdint>
#include "core/world.h"

namespace m3k {

struct TerrainGenOptions {
  uint32_t seed = 1;
  int difficulty = 1;  // 1 easy, 2 medium, 3 hard
  int hills = 64, water = 64, trees = 64;
  int flags = 0x24;    // New City dialog default: coast on last column + river across columns
  // State of the flora service's private generator at the start of the apply step (spec 11 6.2). Only bit 16 of each new state
  // matters, so the low 17 bits are enough to reproduce an original terrain; 0xFFFFFFFF = derive it from `seed`.
  uint32_t floraRng = 0xFFFFFFFFu;
};

// Generates a W x H terrain into `w` (calls w.init). Fills altitude, water surface, sea level, flora.
void generateTerrain(World& w, int W, int H, const TerrainGenOptions& opt);

// The game's linear congruential generator (spec 11 section 2).
class Lcg {
 public:
  explicit Lcg(uint32_t seed = 0) : s_(seed) {}
  void seed(uint32_t s) { s_ = s; }
  uint32_t next() { uint64_t v = uint64_t(s_) * 0x41C64E6Du + 0x3039u; s_ = uint32_t(v); return uint32_t(v >> 16); }
  // Uniform in [0, n); n = 0 gives 0 without stepping.
  uint32_t below(uint32_t n) { return n == 0 ? 0 : next() % n; }
 private:
  uint32_t s_;
};

}  // namespace m3k
