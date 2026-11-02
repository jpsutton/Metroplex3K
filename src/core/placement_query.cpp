#include "core/placement_query.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace m3k {

bool PlacementQuery::matches(const Occupant& o, const OccupantFilter& f) const {
  if (f.bits == 0) return true;
  if ((f.bits & kFiltAgentType) && o.agentType != f.agentType) return false;
  if ((f.bits & kFiltOccId) && o.occId != f.occId) return false;
  return true;
}

bool PlacementQuery::siteOk(int x, int z) const {
  const World* w = layers_.world;
  if (!w || !w->inTiles(x, z)) return false;
  int a = w->altAt(x, z), b = w->altAt(x + 1, z), c = w->altAt(x, z + 1), d = w->altAt(x + 1, z + 1);
  int mx = std::max(std::max(a, b), std::max(c, d)), mn = std::min(std::min(a, b), std::min(c, d));
  if (mx - mn > 2) return false;
  int lo = std::min(std::min(a, b), std::min(c, d));
  if (lo < w->waterAt(x, z)) return false;
  return true;
}

ClearanceResult PlacementQuery::clearance(int x0, int z0, int x1, int z1, uint32_t modes,
                                          bool accumulateCost, int64_t tileCost) const {
  ClearanceResult r;
  const World* w = layers_.world;
  if (!w) return r;
  if (x0 > x1) std::swap(x0, x1);
  if (z0 > z1) std::swap(z0, z1);
  const int H = w->H;
  auto at = [&](int x, int z) -> size_t { return size_t(x) * H + z; };

  for (int x = x0; x <= x1; x++) {
    for (int z = z0; z <= z1; z++) {
      if (!w->inTiles(x, z)) return r;  // out of map → not clear
      const size_t i = at(x, z);
      if ((modes & kPlaceOcc) && layers_.occ && layers_.occ[i] >= 0) return r;
      if ((modes & kPlaceNetwork) && layers_.transport && layers_.transport[i]) return r;
      if ((modes & kPlaceNetwork) && layers_.powerLine && layers_.powerLine[i]) return r;
      if ((modes & kPlaceSubsurface) && layers_.pipe && layers_.pipe[i]) return r;
      if ((modes & kPlaceSite) && !siteOk(x, z)) return r;
      if (accumulateCost) r.cost += tileCost;
    }
  }
  r.clear = true;
  return r;
}

std::vector<std::pair<int, int>> PlacementQuery::collectFootprint(const OccupantFilter& filter) const {
  std::vector<std::pair<int, int>> cells;
  const World* w = layers_.world;
  if (!w) return cells;
  for (const Occupant& o : w->occupants) {
    if (!matches(o, filter)) continue;
    const int sx = std::max(1, o.sizeX), sz = std::max(1, o.sizeZ);
    for (int dx = 0; dx < sx; dx++)
      for (int dz = 0; dz < sz; dz++) {
        const int x = o.x + dx, z = o.z + dz;
        if (w->inTiles(x, z)) cells.emplace_back(x, z);
      }
  }
  std::sort(cells.begin(), cells.end());
  cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
  return cells;
}

NearestMatch PlacementQuery::nearest(int x, int z, const OccupantFilter& filter) const {
  NearestMatch m;
  const World* w = layers_.world;
  if (!w) return m;
  int bestDist = -1;
  for (size_t i = 0; i < w->occupants.size(); i++) {
    const Occupant& o = w->occupants[i];
    if (!matches(o, filter)) continue;
    // Distance to footprint: Chebyshev to nearest covered tile.
    const int sx = std::max(1, o.sizeX), sz = std::max(1, o.sizeZ);
    const int cx = std::clamp(x, o.x, o.x + sx - 1);
    const int cz = std::clamp(z, o.z, o.z + sz - 1);
    const int d = std::max(std::abs(x - cx), std::abs(z - cz));
    if (!m.found || d < bestDist) {
      m.found = true;
      m.occIndex = int(i);
      m.bbox = occupantBBox(o);
      bestDist = d;
    }
  }
  if (m.found) raiseRegionHighlight(m.bbox);
  return m;
}

}  // namespace m3k
