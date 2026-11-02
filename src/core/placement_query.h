// Placement query (#28): on-demand occupancy / clearance helper (spec 14 §6.1 / Q-AV / Q-AW / Q-BA).
// No save records, no periodic work. Holds live refs to the layers it queries.
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>
#include "core/world.h"

namespace m3k {

// Inclusive tile rectangle.
struct TileRect {
  int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
  int width() const { return x1 - x0 + 1; }
  int height() const { return z1 - z0 + 1; }
};

// Clearance mode m ∈ {0,1,2,3}: which occupancy scanners participate (spec 14 §6.1).
// Mode 0: 1+2+4; 1: 1 only; 2: 1+4; 3: 1+2.
inline bool clearanceUses1(int m) { return m >= 0 && m <= 3; }
inline bool clearanceUses2(int m) { return m == 0 || m == 3; }
inline bool clearanceUses4(int m) { return m == 0 || m == 2; }

// Capability bit that lets a 1-tile scanner hit pass (with filter) instead of failing.
// Same value as OF_IGNORABLE (tiler.h / occupant attr 0x72).
constexpr uint32_t kPlaceCapIgnorable = 0x400;

// Occupancy-filter modes (spec 14 §6.1 Q-BA). Callers pass int f, not an arbitrary predicate.
// f = 1 → removable query; any other value (callers use 0) → alternate clearable.
constexpr int kOccFiltRemovable = 1;
constexpr int kOccFiltAltClearable = 0;
constexpr uint32_t kOccFillerLot = 0x100;       // attr 0xCA
constexpr uint32_t kOccLotProtected = 0x200;    // lot state bit

struct ClearanceResult {
  bool clear = false;
  int64_t cost = 0;  // optional: sum of costs for 1-tile passing hits
};

struct NearestMatch {
  bool found = false;
  int occIndex = -1;
  TileRect bbox;
};

// Occupancy scanner: indexes occupants into stride×stride cells (created with the city; not saved).
class OccupancyScanner {
 public:
  void reset(int mapW, int mapH, int stride);
  void clear();
  void add(const Occupant& o, int occIndex);
  // Occupant indices covering the stride-cell that contains tile (x,z).
  const std::vector<int>& atTile(int x, int z) const;
  int stride() const { return stride_; }
  bool ready() const { return stride_ > 0 && !cells_.empty(); }

 private:
  int stride_ = 0, cellsX_ = 0, cellsZ_ = 0, mapW_ = 0, mapH_ = 0;
  std::vector<std::vector<int>> cells_;
  static const std::vector<int> kEmpty_;
};

class PlacementQuery {
 public:
  // Bind world + rebuild the three scanners (strides 1 / 2 / 4). Call after occupant index changes.
  void rebuild(const World* world);

  const World* world() const { return world_; }
  const OccupancyScanner& scanner1() const { return scan1_; }
  const OccupancyScanner& scanner2() const { return scan2_; }
  const OccupancyScanner& scanner4() const { return scan4_; }

  // Map-region highlight notification (same style utilities raise for their regions).
  std::function<void(TileRect)> onRegionHighlight;

  void raiseRegionHighlight(TileRect r) const {
    if (onRegionHighlight) onRegionHighlight(r);
  }

  // Clearance over inclusive rect. Always walks terrain cells; mode selects scanners.
  // filterMode applies only to 1-tile scanner hits that have capability 0x400.
  // accumulateCost: each 1-tile passing hit adds hitCost.
  ClearanceResult clearance(int x0, int z0, int x1, int z1, int mode,
                            int filterMode = kOccFiltAltClearable,
                            bool accumulateCost = false, int64_t hitCost = 0) const;

  std::vector<std::pair<int, int>> collectFootprint(int filterMode = kOccFiltAltClearable) const;
  NearestMatch nearest(int x, int z, int filterMode = kOccFiltAltClearable) const;

  static TileRect occupantBBox(const Occupant& o) {
    return {o.x, o.z, o.x + std::max(1, o.sizeX) - 1, o.z + std::max(1, o.sizeZ) - 1};
  }

  // Occupancy filter (spec 14 §6.1): f==1 → removable; else → altClearable; then
  // fail if filler (0xCA 0x100) AND lot protected (state 0x200).
  bool passesFilter(const Occupant& o, int filterMode) const;

 private:
  const World* world_ = nullptr;
  OccupancyScanner scan1_, scan2_, scan4_;
};

}  // namespace m3k
