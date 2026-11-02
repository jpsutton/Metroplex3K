// Placement query (#28): on-demand occupancy / clearance helper (spec 14 §6.1 / Q-AV).
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

// Four mode flags for clearance (exact caller combinations still [OPEN] in the spec).
enum PlacementMode : uint32_t {
  kPlaceOcc = 1u << 0,       // occupant layer
  kPlaceNetwork = 1u << 1,   // surface transport + power lines
  kPlaceSubsurface = 1u << 2,  // pipes (related underground network)
  kPlaceSite = 1u << 3,      // related site: water / non-flat terrain
};

// Occupant filter bits (exact caller bits still [OPEN]).
enum OccupantFilterBit : uint32_t {
  kFiltAgentType = 1u << 0,  // match agentType
  kFiltOccId = 1u << 1,      // match occId
  // bits == 0 → every occupant matches
};

struct OccupantFilter {
  uint32_t bits = 0;
  uint32_t agentType = 0;
  uint32_t occId = 0;
};

struct ClearanceResult {
  bool clear = false;
  int64_t cost = 0;  // sum of per-tile costs when accumulateCost was requested
};

struct NearestMatch {
  bool found = false;
  int occIndex = -1;
  TileRect bbox;
};

// Layer views PlacementQuery reads. City rebinds these after rebuildIndex.
struct PlacementLayers {
  const World* world = nullptr;
  const int32_t* occ = nullptr;          // W*H, -1 empty
  const uint8_t* transport = nullptr;    // W*H
  const uint8_t* powerLine = nullptr;    // W*H
  const uint8_t* pipe = nullptr;         // W*H
};

class PlacementQuery {
 public:
  void bind(const PlacementLayers& layers) { layers_ = layers; }
  const PlacementLayers& layers() const { return layers_; }

  // Map-region highlight notification (same style utilities raise for their regions).
  std::function<void(TileRect)> onRegionHighlight;

  void raiseRegionHighlight(TileRect r) const {
    if (onRegionHighlight) onRegionHighlight(r);
  }

  // Clearance over [x0..x1]×[z0..z1]. modes selects participating layers.
  // When accumulateCost, each clear in-bounds tile adds tileCost to the result.
  ClearanceResult clearance(int x0, int z0, int x1, int z1, uint32_t modes,
                            bool accumulateCost = false, int64_t tileCost = 0) const;

  // All cells covered by occupants matching `filter` (unique tiles, row-major).
  std::vector<std::pair<int, int>> collectFootprint(const OccupantFilter& filter) const;

  // Nearest matching occupant to (x,z) by Chebyshev distance (ties: lower index).
  // On success raises a map-region highlight for the occupant's tile bbox.
  NearestMatch nearest(int x, int z, const OccupantFilter& filter) const;

  static TileRect occupantBBox(const Occupant& o) {
    return {o.x, o.z, o.x + std::max(1, o.sizeX) - 1, o.z + std::max(1, o.sizeZ) - 1};
  }

 private:
  bool matches(const Occupant& o, const OccupantFilter& f) const;
  bool siteOk(int x, int z) const;
  PlacementLayers layers_;
};

}  // namespace m3k
