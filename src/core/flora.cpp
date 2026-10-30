#include "core/flora.h"

#include <algorithm>
#include <cstdlib>

#include "core/terrain_gen.h"

namespace m3k {

FloraRng floraRngFromLcg(Lcg& lcg) {
  return [&lcg]() { return int(lcg.below(2)); };
}

uint32_t floraInstanceForCell(uint8_t cell, const FloraRng& rng) {
  if (cell == 0 || !floraCellValid(cell)) return 0;
  const int kind = cell >> 4, level = cell & 15;
  const int r = rng() ? 1 : 0;  // one draw per valid non-zero cell
  switch (kind) {
    case 1:
      if (level == 0) return 7054;
      if (level <= 7) return r ? 7055 : 7054;
      return 7057;
    case 2:
      if (level <= 5) return 7040 + level;
      if (level <= 7) return r ? 7053 : 7046;
      return 9200;
    case 3:
      if (level <= 5) return 6 + level;
      if (level <= 7) return r ? 7056 : 12;
      return 7057;
    case 4:
      if (level <= 5) return 7000 + level;
      if (level <= 7) return r ? 7052 : 7006;
      return 9201;
    default: return 0;
  }
}

std::vector<FloraObject> floraObjectsFromLayer(const World& w, const FloraRng& rng) {
  std::vector<FloraObject> out;
  for (int x = w.W - 1; x >= 0; x--)
    for (int z = 0; z < w.H; z++) {
      uint8_t c = w.flora[w.tIdx(x, z)];
      if (!c) continue;
      uint32_t inst = floraInstanceForCell(c, rng);
      if (inst) out.push_back({x, z, c, inst});
    }
  return out;
}

std::vector<uint8_t> floraLayerFromObjects(int W, int H, const std::vector<FloraObject>& objs) {
  std::vector<uint8_t> g(size_t(W) * H, 0);
  for (const FloraObject& o : objs)
    if (o.x >= 0 && o.z >= 0 && o.x < W && o.z < H) g[size_t(o.x) * H + o.z] = o.cell;
  return g;
}

uint8_t floraCellFromDensity(int d) {
  if (d < 32) return 0;
  int lvl = std::min(d / 32, 7);
  return uint8_t(0x30 + lvl - 1);
}

TreeTool TreeTool::fromIni(const IniFile& ini) {
  TreeTool t;
  std::string v;
  if (ini.get("BuildingPlacementCosts", "300", v)) {
    v = trimSpaces(v);
    if (!v.empty()) t.cost = int64_t(parseIdNumber(v));
  }
  return t;
}

TreePlan planTree(const World& w, int tx, int tz, uint8_t cell, const TreeTool& tool, const TileOccupiedFn& occupied) {
  TreePlan p;
  p.x = tx; p.z = tz; p.cell = cell;
  if (!w.inTiles(tx, tz)) { p.reason = TreeReason::OutOfMap; return p; }
  int kind = cell >> 4, level = cell & 15;
  if (cell == 0 || kind < 1 || kind > 4 || level > 9) { p.reason = TreeReason::BadCell; return p; }
  if (kind != 1) {
    for (int dx = 0; dx < 2; dx++)
      for (int dz = 0; dz < 2; dz++)
        if (w.altAt(tx + dx, tz + dz) <= w.waterAt(tx + dx, tz + dz)) { p.reason = TreeReason::Water; return p; }
  }
  bool occ = false;
  if (occupied) occ = occupied(tx, tz);
  else
    for (const Occupant& o : w.occupants)
      if (tx >= o.x && tx < o.x + std::max(o.sizeX, 1) && tz >= o.z && tz < o.z + std::max(o.sizeZ, 1)) { occ = true; break; }
  if (occ) { p.reason = TreeReason::Occupied; return p; }
  if (w.flora[w.tIdx(tx, tz)] == cell) { p.reason = TreeReason::SameFlora; return p; }
  p.legal = true;
  p.cost = tool.cost;
  return p;
}

bool commitTree(World& w, const TreePlan& p) {
  if (!p.legal || !w.inTiles(p.x, p.z)) return false;
  w.flora[w.tIdx(p.x, p.z)] = p.cell;
  return true;
}

uint8_t randomTreeCell(int kind, const FloraRng& rng) {
  // Level 1..5 from three 0/1 draws (value 0..7 mod 5).
  int v = (rng() ? 4 : 0) + (rng() ? 2 : 0) + (rng() ? 1 : 0);  // 0..7
  int level = 1 + (v % 5);
  return uint8_t(std::max(1, std::min(kind, 4)) * 16 + level);
}

}  // namespace m3k
