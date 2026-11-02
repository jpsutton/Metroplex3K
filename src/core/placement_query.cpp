#include "core/placement_query.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace m3k {

const std::vector<int> OccupancyScanner::kEmpty_;

void OccupancyScanner::reset(int mapW, int mapH, int stride) {
  stride_ = std::max(1, stride);
  mapW_ = mapW;
  mapH_ = mapH;
  cellsX_ = (mapW + stride_ - 1) / stride_;
  cellsZ_ = (mapH + stride_ - 1) / stride_;
  cells_.assign(size_t(cellsX_) * size_t(cellsZ_), {});
}

void OccupancyScanner::clear() {
  for (auto& c : cells_) c.clear();
}

void OccupancyScanner::add(const Occupant& o, int occIndex) {
  if (stride_ <= 0 || cells_.empty()) return;
  const int sx = std::max(1, o.sizeX), sz = std::max(1, o.sizeZ);
  const int x0 = o.x, z0 = o.z, x1 = o.x + sx - 1, z1 = o.z + sz - 1;
  const int bx0 = std::max(0, x0) / stride_, bz0 = std::max(0, z0) / stride_;
  const int bx1 = std::min(mapW_ - 1, x1) / stride_, bz1 = std::min(mapH_ - 1, z1) / stride_;
  if (bx0 > bx1 || bz0 > bz1) return;
  for (int bx = bx0; bx <= bx1; bx++)
    for (int bz = bz0; bz <= bz1; bz++) {
      auto& cell = cells_[size_t(bx) * size_t(cellsZ_) + size_t(bz)];
      cell.push_back(occIndex);
    }
}

const std::vector<int>& OccupancyScanner::atTile(int x, int z) const {
  if (!ready() || x < 0 || z < 0 || x >= mapW_ || z >= mapH_) return kEmpty_;
  const int bx = x / stride_, bz = z / stride_;
  return cells_[size_t(bx) * size_t(cellsZ_) + size_t(bz)];
}

bool PlacementQuery::passesFilter(const Occupant& o, int filterMode) const {
  // f = 1 → removable; any other value → alternate clearable (callers use 0).
  const bool base = (filterMode == kOccFiltRemovable) ? o.removable : o.altClearable;
  if (!base) return false;
  // Filler + lot-protected override: base true still fails.
  if ((o.buildingFlags & kOccFillerLot) && (o.state & kOccLotProtected)) return false;
  return true;
}

void PlacementQuery::rebuild(const World* world) {
  world_ = world;
  if (!world_) {
    scan1_.reset(0, 0, 1);
    scan2_.reset(0, 0, 2);
    scan4_.reset(0, 0, 4);
    return;
  }
  scan1_.reset(world_->W, world_->H, 1);
  scan2_.reset(world_->W, world_->H, 2);
  scan4_.reset(world_->W, world_->H, 4);
  for (size_t i = 0; i < world_->occupants.size(); i++) {
    const Occupant& o = world_->occupants[i];
    scan1_.add(o, int(i));
    scan2_.add(o, int(i));
    scan4_.add(o, int(i));
  }
}

ClearanceResult PlacementQuery::clearance(int x0, int z0, int x1, int z1, int mode,
                                          int filterMode,
                                          bool accumulateCost, int64_t hitCost) const {
  ClearanceResult r;
  if (!world_) return r;
  if (mode < 0 || mode > 3) return r;
  if (x0 > x1) std::swap(x0, x1);
  if (z0 > z1) std::swap(z0, z1);

  const bool use1 = clearanceUses1(mode);
  const bool use2 = clearanceUses2(mode);
  const bool use4 = clearanceUses4(mode);

  for (int x = x0; x <= x1; x++) {
    for (int z = z0; z <= z1; z++) {
      // Always walk terrain cells of the rectangle.
      if (!world_->inTiles(x, z)) return r;

      auto checkCoarse = [&](const OccupancyScanner& sc) -> bool {
        for (int oi : sc.atTile(x, z)) {
          if (oi < 0 || oi >= int(world_->occupants.size())) continue;
          return false;  // any hit fails
        }
        return true;
      };

      if (use2 && !checkCoarse(scan2_)) return r;
      if (use4 && !checkCoarse(scan4_)) return r;

      if (use1) {
        for (int oi : scan1_.atTile(x, z)) {
          if (oi < 0 || oi >= int(world_->occupants.size())) continue;
          const Occupant& o = world_->occupants[size_t(oi)];
          // Fail unless capability 0x400 and passes occupancy filter mode f.
          if ((o.caps & kPlaceCapIgnorable) == 0 || !passesFilter(o, filterMode)) return r;
          if (accumulateCost) r.cost += hitCost;
        }
      }
    }
  }
  r.clear = true;
  return r;
}

std::vector<std::pair<int, int>> PlacementQuery::collectFootprint(int filterMode) const {
  std::vector<std::pair<int, int>> cells;
  if (!world_) return cells;
  for (const Occupant& o : world_->occupants) {
    if (!passesFilter(o, filterMode)) continue;
    const int sx = std::max(1, o.sizeX), sz = std::max(1, o.sizeZ);
    for (int dx = 0; dx < sx; dx++)
      for (int dz = 0; dz < sz; dz++) {
        const int x = o.x + dx, z = o.z + dz;
        if (world_->inTiles(x, z)) cells.emplace_back(x, z);
      }
  }
  std::sort(cells.begin(), cells.end());
  cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
  return cells;
}

NearestMatch PlacementQuery::nearest(int x, int z, int filterMode) const {
  NearestMatch m;
  if (!world_) return m;
  int bestDist = -1;
  for (size_t i = 0; i < world_->occupants.size(); i++) {
    const Occupant& o = world_->occupants[i];
    if (!passesFilter(o, filterMode)) continue;
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
