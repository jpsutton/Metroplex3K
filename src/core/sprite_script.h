// Sprite-script virtual machine (spec/25-rendering.md section 7.1). One ScriptRun per running script; update()
// executes one non-label entry per call, like the original's view update.
#pragma once
#include <cstdint>
#include "core/gamedata.h"

namespace m3k {

// Small deterministic generator (the animations only need desynchronised, repeatable randomness).
struct AnimRng {
  uint64_t s = 0x9E3779B97F4A7C15ull;
  explicit AnimRng(uint64_t seed = 1) : s(seed * 0x2545F4914F6CDD1Dull + 0x9E3779B97F4A7C15ull) { if (!s) s = 1; next(); next(); }
  uint32_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return uint32_t((s * 0x2545F4914F6CDD1Dull) >> 32); }
};

struct ScriptRun {
  int step = 0;
  int64_t nextMs = 0;        // do not execute before this script time
  int frame = -1;            // current overlay frame (-1: nothing drawn yet)
  bool alpha = false;        // frame is drawn with frame+1 as its alpha plane
  int dx = 0, dy = 0;        // current draw offset (added to the script offset)
  bool armed = false;        // date window armed
  int startMD = 0, endMD = 0, windowTarget = 0;
  bool windowAbs = false;
  int windowLabel = -1;      // label id of the most recently armed WL (target of GL)
};

// Execute one update of `sc` at script time `nowMs`. `j` (0..3) is the per-building wait slowdown, `dateMD` the game
// date as month*100+day.
void stepScript(const CsScript& sc, ScriptRun& run, int64_t nowMs, int j, AnimRng& rng, int dateMD);

// First frame the script shows when it is not running: execute with zero-length waits until the first Draw.
void initialScriptFrame(const CsScript& sc, ScriptRun& run, AnimRng& rng, int dateMD);

}  // namespace m3k
