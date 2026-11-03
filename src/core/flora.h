// Flora layer conversion (spec/10-save-format.md 6.3) and the tree-planting tool (spec/11 section 6).
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "core/ini.h"
#include "core/world.h"

namespace m3k {

class Lcg;

// Injected RNG for the flora subsystem: each call returns 0 or 1.
using FloraRng = std::function<int()>;
FloraRng floraRngFromLcg(Lcg& lcg);

// A flora occupant created from a layer cell.
struct FloraObject {
  int x = 0, z = 0;
  uint8_t cell = 0;       // kind*16 + level
  uint32_t instance = 0;  // occupant instance id (group FFD30C03:80F48961); 0 = no object for this cell
};

inline bool floraCellValid(uint8_t cell) { return (cell >> 4) <= 4 && (cell & 15) <= 9; }

// Instance for a cell. Consumes exactly one rng draw for every valid non-zero cell (even when both
// table entries are equal, and for kind 0 which yields no object); none otherwise. Returns 0 if the
// cell is zero/invalid or yields no object.
uint32_t floraInstanceForCell(uint8_t cell, const FloraRng& rng);

// Load: visit cells in storage order (x descending from W-1, then z ascending); one object per
// non-zero valid cell with a non-zero instance.
std::vector<FloraObject> floraObjectsFromLayer(const World& w, const FloraRng& rng);
// Save: zero grid W*H (index x*H+z), each object's cell stored at its anchor; later objects win;
// out of range skipped.
std::vector<uint8_t> floraLayerFromObjects(int W, int H, const std::vector<FloraObject>& objs);

// Generator density (0-255 per vertex) -> cell: d < 32 gives 0, else generic deciduous (kind 3)
// level floor(d/32)-1, i.e. occupant 5 + floor(d/32) (spec 11 6.2).
uint8_t floraCellFromDensity(int density);

// ---- Tree planting tool ----
struct TreeTool {
  int64_t cost = 0;  // [BuildingPlacementCosts] key 300 (0 if absent)
  static TreeTool fromIni(const IniFile& budgetLayer);
};

enum class TreeReason { Ok, OutOfMap, Water, Occupied, SameFlora /* unused: kept for ABI of tests */, BadCell };

struct TreePlan {
  bool legal = false;
  TreeReason reason = TreeReason::Ok;
  int64_t cost = 0;
  int x = 0, z = 0;
  uint8_t cell = 0;
};

using TileOccupiedFn = std::function<bool(int x, int z)>;

// Rules (the original's are undecoded, spec 11 section 6 [OPEN]; these are a sensible choice):
// tile in map; cell valid, non-zero, kind 1-4; all four corners above water surface (kind 1
// coastal wetlands exempt); no occupant on the tile (callback, default: World::occupants footprint);
// replacing flora (a different cell or the same one) is allowed and charged (spec 11 4.1). Cost = tool cost.
TreePlan planTree(const World& w, int tx, int tz, uint8_t cell, const TreeTool& tool, const TileOccupiedFn& occupied = nullptr);
// Writes the cell; returns false (no change) if the plan is illegal.
bool commitTree(World& w, const TreePlan& p);
// Random cell of `kind` (1-4), level 1-5, from three rng draws.
uint8_t randomTreeCell(int kind, const FloraRng& rng);

}  // namespace m3k
