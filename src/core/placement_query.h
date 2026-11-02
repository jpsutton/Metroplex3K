// Placement query (#28): on-demand occupancy / clearance helper (spec 14 §6.1 / Q-AV / Q-AW).
// No save records, no periodic work. Holds live refs to the layers it queries.
#pragma once
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

// Occupant filter bits (exact caller predicates still [OPEN]).
enum OccupantFilterBit : uint32_t {
  kFiltAgentType = 1u << 0,
  kFiltOccId = 1u << 1,
  // bits == 0 → every occupant matches
};

struct OccupantFilter {
  uint32_t bits = 0;
  uint32_t agentType = 0;
  uint32_t occId = 0;
};

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
  // filter applies only to 1-tile scanner hits that have capability 0x400.
  // accumulateCost: each 1-tile passing hit adds hitCost.
  ClearanceResult clearance(int x0, int z0, int x1, int z1, int mode,
                            const OccupantFilter& filter = {},
                            bool accumulateCost = false, int64_t hitCost = 0) const;

  std::vector<std::pair<int, int>> collectFootprint(const OccupantFilter& filter) const;
  NearestMatch nearest(int x, int z, const OccupantFilter& filter) const;

  static TileRect occupantBBox(const Occupant& o) {
    return {o.x, o.z, o.x + std::max(1, o.sizeX) - 1, o.z + std::max(1, o.sizeZ) - 1};
  }

  bool matches(const Occupant& o, const OccupantFilter& f) const;

 private:
  const World* world_ = nullptr;
  OccupancyScanner scan1_, scan2_, scan4_;
};

}  // namespace m3k
