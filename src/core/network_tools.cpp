#include "core/network_tools.h"
#include <algorithm>
#include <map>
#include <string>
#include <cstdlib>

namespace m3k {

// ======================= tool parameters / costs =======================
static const ToolParams kTools[kNumTools] = {
    {TOOL_ROAD, 0xC2B5DE77u, NK_ROAD, 16000, false, 3, true, true},
    {TOOL_HIGHWAY, 0x42B5DE86u, NK_HIGHWAY, 16001, true, 3, true, true},
    {TOOL_RAIL, 0x42B5DE98u, NK_RAIL, 17000, false, 3, true, true},
    {TOOL_SUBWAY, 0xC2B5DEA6u, NK_SUBWAY, 17400, false, 0, true, true},
    {TOOL_ONRAMP, 0x02B5DEBBu, NK_HIGHWAY, 16001, false, 3, false, false},
    {TOOL_POWER, 0x42C2F8BFu, NK_POWER, 17100, false, 1, false, true},
    {TOOL_PIPE, 0x22C2F8C5u, NK_PIPE, 17300, false, 0, true, true},
};
const ToolParams& toolParams(Tool t) { return kTools[t]; }

int PlacementCosts::cost(uint32_t type) const {
  for (int depth = 0; depth < 32; depth++) {
    auto it = byType.find(type);
    if (it != byType.end()) return it->second;
    auto p = parent.find(type);
    if (p == parent.end() || p->second == type) return 0;
    type = p->second;
  }
  return 0;
}

uint32_t connectionCostKey(int kind) {
  switch (kind) {
    case NK_ROAD: return 16017;
    case NK_RAIL: return 17006;
    case NK_POWER: return 12102;
    case NK_HIGHWAY: return 17207;
    case NK_PIPE: return 17310;
    case NK_SUBWAY: return 17410;
  }
  return 0;
}

// ======================= filters =======================
bool placementAccepts(int kind, const TileOcc& o) {
  if (!o.present) return true;
  if (!o.removable) return false;
  return (o.flags & OF_IGNORABLE) || flagsMatchKind(kind, o.flags);
}
bool crossingAccepts(int kind, const TileOcc& o) {
  if (!o.present || !o.removable || !(o.flags & OF_NETPIECE)) return false;
  return isCrossableId(kind, o.id);
}

// ======================= routing =======================
namespace {
int sgn1(int v) { return v >= 1 ? 1 : -1; }

bool slopesAlong(const INetworkMap& m, int x, int z, bool alongX) {
  int c[4];
  m.tileCorners(x, z, c);
  return alongX ? (c[1] != c[0] || c[2] != c[3]) : (c[3] != c[0] || c[2] != c[1]);
}

// Strip grader [STATIC SIMNTWRK manager vt+0xC0, reconstruction checked against the Mount Herrang drags; the decompile is partly garbled]. The drag is
// split into straight runs (breaks at water and at skipped tiles; a turn tile belongs to both runs). A run of >= 3 tiles, or >= 2 for the final run, is
// graded as a strip: the vertex rows along the run are clamped into a window anchored by the start and end heights, mirror vertices copy their main
// vertex, and every changed vertex needs all four tiles around it free of occupants. A failing run invalidates itself and everything after it.
// Not modelled yet [OPEN]: the |dh| <= 4 ring smoothing around the strip and the vt+0x158 run break.
struct GradeVertex { int x, z, h; };
struct StripGrade {
  std::vector<GradeVertex> verts;   // changed vertices only
  int raised = 0, lowered = 0;
  size_t failFrom = size_t(-1);     // first tile index of the failing run, -1 = none
  std::string why;                  // debug text of the failure
};

static StripGrade planStripGrade(const INetworkMap& m, const std::vector<std::pair<int, int>>& tiles, const std::vector<char>& skip) {
  StripGrade g;
  const size_t N = tiles.size();
  std::vector<std::pair<size_t, size_t>> runs;   // inclusive tile index ranges
  auto breaker = [&](size_t i) { return (i < skip.size() && skip[i]) || !m.onMap(tiles[i].first, tiles[i].second) || m.isWaterTile(tiles[i].first, tiles[i].second); };
  size_t i = 0;
  while (i < N) {
    if (breaker(i)) { i++; continue; }
    const size_t a = i;
    int pdx = 0, pdz = 0;
    size_t b = i;
    while (b + 1 < N && !breaker(b + 1)) {
      const int dx = tiles[b + 1].first - tiles[b].first, dz = tiles[b + 1].second - tiles[b].second;
      if (b > a && (dx != pdx || dz != pdz)) break;   // a turn: this tile ends the run and starts the next
      pdx = dx; pdz = dz; b++;
    }
    runs.push_back({a, b});
    i = (b + 1 < N && !breaker(b + 1) && b > a) ? b : b + 1;   // the turn tile starts the next run
    if (b == a) i = b + 1;
  }
  std::map<std::pair<int, int>, int> target;   // vertex -> graded height (later runs override)
  for (size_t r = 0; r < runs.size(); r++) {
    const size_t a = runs[r].first, b = runs[r].second;
    const size_t n = b - a + 1;
    const bool finalRun = r + 1 == runs.size() && b + 1 == N;
    if (n < (finalRun ? 2u : 3u)) continue;
    bool collinear = true;
    for (size_t k = a; k <= b; k++) {
      if (tiles[k].first != tiles[a].first && tiles[k].second != tiles[a].second) collinear = false;
      if (tiles[k].first != tiles[a].first && tiles[k].second != tiles[a].second) collinear = false;
    }
    const bool alongZ = tiles[a].first == tiles[b].first;
    if (alongZ) { for (size_t k = a; k <= b; k++) if (tiles[k].first != tiles[a].first) collinear = false; }
    else { for (size_t k = a; k <= b; k++) if (tiles[k].second != tiles[a].second) collinear = false; }
    if (!collinear) { g.failFrom = a; g.why = "run not collinear"; return g; }
    // all tiles already a valid slope (or flat): nothing to grade
    bool allValid = true;
    for (size_t k = a; k <= b; k++) { int c[4]; m.tileCorners(tiles[k].first, tiles[k].second, c); if (!(isValidSlope(c) || (c[0] == c[1] && c[1] == c[2] && c[2] == c[3]))) allValid = false; }
    if (allValid) continue;
    int c0[4], c1[4];
    m.tileCorners(tiles[a].first, tiles[a].second, c0);
    m.tileCorners(tiles[b].first, tiles[b].second, c1);
    const bool sFlat = c0[0] == c0[1] && c0[1] == c0[2] && c0[2] == c0[3], eFlat = c1[0] == c1[1] && c1[1] == c1[2] && c1[2] == c1[3];
    int startH = sFlat ? c0[0] : (c0[0] + c0[1] + c0[2] + c0[3]) / 4, endH = eFlat ? c1[0] : (c1[0] + c1[1] + c1[2] + c1[3]) / 4;
    const int s = sFlat ? 1 : 0, last = int(n) - 1 - (eFlat ? 1 : 0);
    const bool negative = alongZ ? tiles[b].second < tiles[a].second : tiles[b].first < tiles[a].first;
    if (negative) std::swap(startH, endH);
    const int minX = std::min(tiles[a].first, tiles[b].first), minZ = std::min(tiles[a].second, tiles[b].second);
    int prev = 0;
    std::vector<GradeVertex> main, mirror;
    bool fail = false;
    for (int k = s; k <= last + 1; k++) {
      int lo = std::max(0, endH - 2 - 2 * (last - k)), hi = std::min(255, endH + 2 + 2 * (last - k));
      if (k == s) { lo = std::max(lo, startH); hi = std::min(hi, startH); }
      else { lo = std::max(lo, prev - 2); hi = std::min(hi, prev + 2); }
      if (hi < lo) { fail = true; g.why = "window empty at k=" + std::to_string(k) + " lo " + std::to_string(lo) + " hi " + std::to_string(hi); break; }
      const int mx = alongZ ? tiles[a].first + 1 : minX + k, mz = alongZ ? minZ + k : tiles[a].second;
      const int qx = alongZ ? tiles[a].first : minX + k, qz = alongZ ? minZ + k : tiles[a].second + 1;
      if (mx < 0 || mz < 0 || mx > m.width() || mz > m.height()) { prev = std::max(lo, std::min(hi, prev)); continue; }
      const int h = std::max(lo, std::min(hi, m.cornerHeight(mx, mz)));
      prev = h;
      main.push_back({mx, mz, h});
      if (qx >= 0 && qz >= 0 && qx <= m.width() && qz <= m.height()) mirror.push_back({qx, qz, h});
    }
    if (!fail) {
      std::vector<GradeVertex> all = main; all.insert(all.end(), mirror.begin(), mirror.end());
      for (const auto& v : all)
        if (v.h != m.cornerHeight(v.x, v.z) && !m.canChangeCorner(v.x, v.z)) { fail = true; g.why = "occupant next to vertex (" + std::to_string(v.x) + "," + std::to_string(v.z) + ") " + std::to_string(m.cornerHeight(v.x, v.z)) + "->" + std::to_string(v.h); break; }   // permission: occupants around every changed vertex
      if (!fail) for (const auto& v : all) target[{v.x, v.z}] = v.h;
    }
    if (fail) { g.failFrom = a; return g; }
  }
  for (const auto& kv : target) {
    const int cur = m.cornerHeight(kv.first.first, kv.first.second);
    if (cur == kv.second) continue;
    g.verts.push_back({kv.first.first, kv.first.second, kv.second});
    if (kv.second > cur) g.raised += kv.second - cur; else g.lowered += cur - kv.second;
  }
  return g;
}

}  // namespace

std::string describeStripGrade(const INetworkMap& m, const std::vector<std::pair<int, int>>& tiles) {
  StripGrade g = planStripGrade(m, tiles, {});
  std::string out = "strip grade: ";
  if (g.failFrom != size_t(-1)) out += "FAILS from tile " + std::to_string(g.failFrom) + " (" + g.why + ")\n";
  else out += "ok, raised " + std::to_string(g.raised) + " lowered " + std::to_string(g.lowered) + "\n";
  for (const auto& v : g.verts) out += "  vertex (" + std::to_string(v.x) + "," + std::to_string(v.z) + ") " + std::to_string(m.cornerHeight(v.x, v.z)) + " -> " + std::to_string(v.h) + "\n";
  return out;
}

RoutedPath routePath(const INetworkMap& m, Tool tool, std::pair<int, int> S, std::pair<int, int> E) {
  const ToolParams& P = toolParams(tool);
  RoutedPath rp;
  const NetLayer layer = layerOfKind(P.kind);
  const int W = m.width(), H = m.height();

  if (tool == TOOL_ONRAMP) {  // point tool
    RoutedTile t; t.x = E.first; t.z = E.second;
    t.valid = m.onMap(t.x, t.z);
    rp.tiles.push_back(t);
    rp.validPrefix = t.valid ? 1 : 0;
    rp.blocked = !t.valid;
    return rp;
  }

  int cx = S.first, cz = S.second;
  int dx = 0, dz = 0;
  {
    int ddx = E.first - cx, ddz = E.second - cz;
    if (std::abs(ddz) < std::abs(ddx)) dx = sgn1(ddx); else dz = sgn1(ddz);
  }
  const bool initAlongX = dx != 0;
  // width offset
  int wx = 0, wz = 0;
  if (P.wide) {
    if (initAlongX) { wz = (S.second >= H - 1) ? -1 : 1; }
    else { wx = (S.first >= W - 1) ? -1 : 1; }
    rp.pairDx = wx; rp.pairDz = wz;
  }

  bool blocked = false;
  int waterEntries = 0;
  bool prevWater = false;
  int run = 0;       // consecutive steps in the current direction (steps taken)
  bool turned = false;
  bool prevAlongX = initAlongX;
  int stepsTaken = 0;

  auto validate = [&](int x, int z, bool alongX, bool isPair, bool primaryWaterOk) {
    if (!m.onMap(x, z)) return false;
    int c[4];
    m.tileCorners(x, z, c);
    bool flat = c[1] == c[0] && c[2] == c[0] && c[3] == c[0];
    if (!P.anySlope && !flat && !isValidSlope(c)) return false;
    // A water tile must be flat or a valid slope whatever AnySlope says: the original blocks the path at a sloping shore row, and every shipped
    // bridge end has flat banks [OBSERVED: Roadless Paradise highway drags blocked at (40,164) corners 82 82 81 80; INFERRED rule].
    if (P.wide && P.minRun > 0 && !flat && !isValidSlope(c) && m.isWaterTile(x, z)) return false;   // highway only: a road drag across the Roadless Paradise river is valid [OBSERVED]
    TileOcc o = m.occupantAt(x, z, layer);
    bool crossing = false;
    if (o.present) {
      if (placementAccepts(P.kind, o)) {}
      else if (crossingAccepts(P.kind, o)) {
        if (!flat && !slopesAlong(m, x, z, alongX)) return false;
        crossing = true;
      } else return false;
    }
    if (crossing && P.wide && turned) return false;
    if (crossing) rp.crossed = true;
    if (P.minRun > 0 && m.isWaterTile(x, z)) {
      if (isPair) return primaryWaterOk;
    }
    return true;
  };

  auto addTile = [&](int x, int z, bool alongX) {
    RoutedTile t; t.x = x; t.z = z;
    bool ok = !blocked && validate(x, z, alongX, false, true);
    bool water = m.onMap(x, z) && m.isWaterTile(x, z);
    if (ok && P.minRun > 0 && water) {
      if (!prevWater) {
        waterEntries++;
        // straight steps made immediately before entering (excluding the entering step)
        int prior = (stepsTaken == 0) ? 0 : run - 1;
        if (waterEntries > 1 || prior < P.minRun) ok = false;
      }
    }
    prevWater = water;
    if (!ok) blocked = true;
    t.valid = !blocked;
    const size_t primaryIdx = rp.tiles.size();
    rp.tiles.push_back(t);
    if (P.wide) {
      RoutedTile u; u.x = x + wx; u.z = z + wz;
      bool uok = !blocked && validate(u.x, u.z, alongX, true, water ? !blocked : true);
      if (!uok) blocked = true;
      u.carriage = 1;
      u.valid = !blocked;
      // A carriageway pair is valid only when both tiles are: a failing pair tile invalidates the whole row [OBSERVED].
      if (!uok) rp.tiles[primaryIdx].valid = false;
      rp.tiles.push_back(u);
    }
  };

  addTile(cx, cz, initAlongX);
  while (!(cx == E.first && cz == E.second)) {
    if ((int)rp.tiles.size() >= kMaxPathTiles) { rp.truncated = true; break; }
    int ddx = E.first - cx, ddz = E.second - cz;
    int ndx = 0, ndz = 0;
    bool curFlat = m.onMap(cx, cz) && m.isFlat(cx, cz);
    bool curAlongX = dx != 0;
    auto major = [&]() {
      if (std::abs(ddx) > std::abs(ddz)) { ndx = sgn1(ddx); ndz = 0; } else { ndz = sgn1(ddz); ndx = 0; }
    };
    if (!curFlat && !blocked && stepsTaken > 0) {
      if (curAlongX) ndx = sgn1(ddx); else ndz = sgn1(ddz);
    } else if (!P.staircase && !blocked) {
      bool reached = curAlongX ? ddx == 0 : ddz == 0;
      if (!reached) { ndx = dx; ndz = dz; } else major();
    } else if (std::abs(ddx) == std::abs(ddz)) {
      if (prevAlongX) ndz = sgn1(ddz); else ndx = sgn1(ddx);
    } else {
      major();
    }
    int nx = cx + ndx, nz = cz + ndz;
    if (!m.onMap(nx, nz)) break;
    bool alongX = ndx != 0;
    if (alongX != initAlongX) turned = true;
    if (stepsTaken == 0 || ndx != dx || ndz != dz) run = 1; else run++;
    dx = ndx; dz = ndz;
    prevAlongX = alongX;
    cx = nx; cz = nz;
    stepsTaken++;
    addTile(cx, cz, alongX);
  }
  // Water crossing countdown [STATIC, the drag-path builder 0x1000488D]: after the water, minRun land tiles are counted down, and the water tiles stay pending
  // until a valid land tile (flat or a valid slope) is reached: the last tile of the countdown or a later one (Roadless Paradise (35,155)->(35,176): the
  // third land tile (35,175) clears the pending tiles, the drag's own last tile (35,176) need not be valid). A drag that ends (or turns invalid) first sets every pending tile red; only the
  // leading buildable tiles remain (Roadless Paradise (34,158)->(34,175): the water at z162..173 and two land tiles after it, all red).
  if (!P.wide && P.minRun > 0 && tool != TOOL_ONRAMP) {
    size_t w0 = rp.tiles.size();
    for (size_t k = 0; k < rp.tiles.size(); k++) if (rp.tiles[k].valid && m.onMap(rp.tiles[k].x, rp.tiles[k].z) && m.isWaterTile(rp.tiles[k].x, rp.tiles[k].z)) { w0 = k; break; }
    if (w0 < rp.tiles.size()) {
      size_t j = w0;
      while (j < rp.tiles.size() && rp.tiles[j].valid && m.onMap(rp.tiles[j].x, rp.tiles[j].z) && m.isWaterTile(rp.tiles[j].x, rp.tiles[j].z)) j++;
      bool confirmed = false;
      for (size_t k = j + size_t(P.minRun) - 1; k < rp.tiles.size() && rp.tiles[k].valid && !confirmed; k++) {
        if (m.isWaterTile(rp.tiles[k].x, rp.tiles[k].z)) break;
        int c[4]; m.tileCorners(rp.tiles[k].x, rp.tiles[k].z, c);
        if ((c[0] == c[1] && c[1] == c[2] && c[2] == c[3]) || isValidSlope(c)) confirmed = true;
      }
      if (!confirmed) {
        for (size_t k = w0; k < rp.tiles.size(); k++) rp.tiles[k].valid = false;
        blocked = true;
      }
    }
  }
  if (!P.wide && tool != TOOL_ONRAMP) {   // the strip grader runs with the validation, in the preview and at commit
    std::vector<std::pair<int, int>> ts;
    for (const RoutedTile& t : rp.tiles) { if (!t.valid) break; ts.push_back({t.x, t.z}); }
    StripGrade g = planStripGrade(m, ts, {});
    if (g.failFrom != size_t(-1)) { for (size_t k = g.failFrom; k < rp.tiles.size(); k++) rp.tiles[k].valid = false; blocked = true; }
  }
  rp.blocked = blocked;
  rp.validPrefix = 0;
  for (auto& t : rp.tiles) { if (!t.valid) break; rp.validPrefix++; }
  return rp;
}

PathPrice previewPrice(RoutedPath& p, int tileCost, long funds, int buildablePrefix) {
  PathPrice r;
  if (buildablePrefix >= 0)
    for (size_t i = size_t(buildablePrefix); i < p.tiles.size(); i++) p.tiles[i].valid = false;
  int n = 0;
  for (auto& t : p.tiles) if (t.valid) n++;
  r.price = n * tileCost;
  if (r.price != 0 && r.price > funds) {
    for (auto& t : p.tiles) t.valid = false;
    r.insufficient = true;
  }
  return r;
}

std::vector<PathTile> toPathTiles(const RoutedPath& p, bool onlyValid) {
  std::vector<PathTile> v;
  for (auto& t : p.tiles) {
    if (onlyValid && !t.valid) continue;
    PathTile pt; pt.x = t.x; pt.z = t.z; pt.carriage = t.carriage;
    v.push_back(pt);
  }
  return v;
}

// ======================= bridges =======================
std::vector<std::pair<int, int>> BridgePlan::coveredTiles() const {
  std::vector<std::pair<int, int>> v;
  if (!ok) return v;
  for (int i = -3; i <= W + 2; i++) v.push_back({x0 + i * ax, z0 + i * az});
  return v;
}

bool isBridgePierId(uint32_t id) {
  static const uint32_t ids[] = {83, 87, 90, 91, 11265, 11275, 11300, 16042, 16043, 16049, 16061, 16062, 18057, 18058, 18067, 18068};
  return std::find(std::begin(ids), std::end(ids), id) != std::end(ids);
}

BridgePlan planBridge(const INetworkMap& m, const NetworkRules& nr, int kind, const std::vector<PathTile>& path,
                      const PlacementCosts& costs, int carriagePass) {
  BridgePlan pl;
  pl.kind = kind;
  if (!(kind == NK_ROAD || kind == NK_RAIL || kind == NK_POWER || kind == NK_HIGHWAY)) return pl;
  if (path.size() < 4 || nr.bridges.empty()) return pl;
  // 1. span
  size_t i0 = path.size();
  for (size_t i = 0; i < path.size(); i++)
    if (m.onMap(path[i].x, path[i].z) && m.isWaterTile(path[i].x, path[i].z)) { i0 = i; break; }
  if (i0 == path.size()) return pl;
  size_t j = i0;
  while (j < path.size() && m.isWaterTile(path[j].x, path[j].z)) j++;
  if (j == path.size()) return pl;  // no land after the water
  for (auto& t : path)
    if (m.isWaterTile(t.x, t.z) && m.occupantAt(t.x, t.z, LAYER_SURFACE).present) return pl;
  for (size_t i = i0; i < j; i++) pl.spanTiles.push_back({path[i].x, path[i].z});
  int hw = 1 << 30;
  int bx0 = 1 << 30, bx1 = -1, bz0 = 1 << 30, bz1 = -1;
  for (auto& t : pl.spanTiles) {
    hw = std::min(hw, kind == NK_HIGHWAY ? m.cornerHeight(t.first, t.second) : m.waterLevel(t.first, t.second));   // roads/rails/power: the river's water level [OBSERVED hw = 80 on Roadless Paradise x=36]
    bx0 = std::min(bx0, t.first); bx1 = std::max(bx1, t.first);
    bz0 = std::min(bz0, t.second); bz1 = std::max(bz1, t.second);
  }
  pl.hw = hw;
  bool alongZ = (bx1 - bx0 < 2) && (bz1 != bz0) &&
                ((bz1 - bz0 > 1) || bx1 == bx0 || (pl.spanTiles.size() > 1 && pl.spanTiles[1].first != pl.spanTiles[0].first));
  pl.axis = alongZ ? 1 : 0;
  pl.W = (alongZ ? bz1 - bz0 : bx1 - bx0) + 1;
  pl.ax = alongZ ? 0 : 1; pl.az = alongZ ? 1 : 0;
  pl.px = alongZ ? 1 : 0; pl.pz = alongZ ? 0 : 1;
  pl.x0 = bx0; pl.z0 = bz0;

  // 2. design
  const BridgeDesign* d = nullptr;
  for (auto& b : nr.bridges) {
    if (kind == NK_HIGHWAY) {
      int dir = carriagePass == 0 ? (pl.axis == 0 ? 0 : 3) : (pl.axis == 0 ? 2 : 1);
      if (b.axis == dir && b.n <= pl.W) { d = &b; break; }
    } else if (b.axis == pl.axis && b.n <= pl.W) { d = &b; break; }
  }
  if (!d) return pl;
  pl.design = *d;
  pl.price = (kind == NK_HIGHWAY) ? (carriagePass == 0 ? pl.W * costs.cost(kBridgeCostHighway) : 0)
                                  : pl.W * costs.cost(kBridgeCostRoadRailPower);

  // 4. fitting
  int k = int(d->seg[2].size());
  pl.m = (k == 0 || k > pl.W) ? 0 : std::min(pl.W / k, 256);
  int r = pl.W - pl.m * k;
  pl.leftFill = r / 2;
  pl.rightFill = r - r / 2;

  // 5. abutment terrain
  const int D = kind == NK_POWER ? 1 : (kind == NK_HIGHWAY ? 2 : 5), R = kind == NK_POWER ? 1 : (kind == NK_HIGHWAY ? 2 : 3);
  auto vOn = [&](int vx, int vz) { return vx >= 0 && vz >= 0 && vx <= m.width() && vz <= m.height(); };
  const int ex = pl.x0 + pl.W * pl.ax, ez = pl.z0 + pl.W * pl.az;  // E0
  auto hAt = [&](int vx, int vz, int dflt) { return vOn(vx, vz) ? m.cornerHeight(vx, vz) : dflt; };
  auto bank = [&](int bx, int bz, int sgn, int dflt) {
    int c1x = bx + sgn * 3 * pl.ax, c1z = bz + sgn * 3 * pl.az;
    int c2x = c1x + pl.px, c2z = c1z + pl.pz;
    int B = dflt;
    if (kind == NK_HIGHWAY) {   // rows of three corners (+0, +p, +2p): B = floor(sum / 3) (spec 12 6.4 step 5a)
      const int c3x = c2x + pl.px, c3z = c2z + pl.pz;
      if (vOn(c1x, c1z) && vOn(c2x, c2z) && vOn(c3x, c3z)) B = (m.cornerHeight(c1x, c1z) + m.cornerHeight(c2x, c2z) + m.cornerHeight(c3x, c3z)) / 3;
    } else if (vOn(c1x, c1z) && vOn(c2x, c2z)) B = (m.cornerHeight(c1x, c1z) + m.cornerHeight(c2x, c2z)) / 2;
    int lo = kind == NK_HIGHWAY ? hw - 2 : hw + D - 4;
    int hi = kind == NK_HIGHWAY ? hw + 6 : hw + D + 4;
    return std::min(std::max(B, lo), hi);
  };
  const int rows = kind == NK_HIGHWAY ? 3 : 2;
  int grp = 0;
  auto addCorner = [&](int vx, int vz, int h) { if (vOn(vx, vz)) pl.corners.push_back({vx, vz, h, grp}); };
  auto side = [&](int bx, int bz, int sgn, int B) {
    // sgn = -1: start side (W0 - n a), +1: far side (E0 + n a)
    // Order [OBSERVED, Roadless Paradise road bridge refused next to a train station: the R and D rows were already graded, the B rows were not]: the
    // row at the water edge, the next row, then the two bank rows.
    for (int q = 0; q < rows; q++) addCorner(bx + q * pl.px, bz + q * pl.pz, hw + R);
    for (int q = 0; q < rows; q++) addCorner(bx + sgn * pl.ax + q * pl.px, bz + sgn * pl.az + q * pl.pz, hw + D);
    for (int n : {3, 4})
      for (int q = 0; q < rows; q++) addCorner(bx + sgn * n * pl.ax + q * pl.px, bz + sgn * n * pl.az + q * pl.pz, B);
  };
  int Bs = bank(pl.x0, pl.z0, -1, 20), Bf = bank(ex, ez, +1, hw + 1);
  if (kind == NK_HIGHWAY) {
    // Two defects of the original's highway builders [STATIC, disassembly]: the start-side B corners take the raw height of the far-bank
    // corner E0 + 3a (23 off the map) because a byte variable is overwritten by the first far-bank read, and the far B averages
    // h(E0+3a), h(E0+3a+2p) and h(W0-3a+2p). The permission checks use the real B.
    const int fx = ex + 3 * pl.ax, fz = ez + 3 * pl.az;
    const int h1 = hAt(fx, fz, 23), h2 = hAt(fx + 2 * pl.px, fz + 2 * pl.pz, 23), h3 = hAt(pl.x0 - 3 * pl.ax + 2 * pl.px, pl.z0 - 3 * pl.az + 2 * pl.pz, 23);
    Bf = std::min(std::max((h1 + h2 + h3) / 3, hw - 2), hw + 6);
    Bs = h1;
  }
  grp = 0;
  side(pl.x0, pl.z0, -1, Bs);
  grp = 1;
  // River bed: after the start side and before the far side, the interior corners of the water rows that stand above the water level go down
  // to it (spec 12 6.4; k = 1 .. W-1 along the span, the same rows as the abutments).
  for (int k = 1; k < pl.W; k++)
    for (int q = 0; q < rows; q++) {
      const int vx = pl.x0 + k * pl.ax + q * pl.px, vz = pl.z0 + k * pl.az + q * pl.pz;
      if (vOn(vx, vz) && m.cornerHeight(vx, vz) > hw) addCorner(vx, vz, hw);
    }
  grp = 2;
  side(ex, ez, +1, Bf);
  // The permission check covers the start-bank and far-bank corners (8 each) and skips a corner whose height already equals its target [STATIC];
  // the river-bed corners are written later without a check of their own.
  for (auto& c : pl.corners) {
    if (kind != NK_HIGHWAY && (c.group == 1 || m.cornerHeight(c.vx, c.vz) == c.h)) continue;
    if (!m.canChangeCorner(c.vx, c.vz)) { pl.terrainBlocked = true; break; }
  }

  // 6. pieces
  const int rampY = kind == NK_POWER ? hw + 1 : kind == NK_HIGHWAY ? hw + 2 : (pl.axis == 0 ? hw + 5 : hw + 7);   // highway ramps at hw + 2 (spec 12 6.4)
  auto put = [&](int idx, uint32_t packed, int y) {
    Piece p = unpackPiece(packed);
    PlacedPiece pp; pp.x = pl.x0 + idx * pl.ax; pp.z = pl.z0 + idx * pl.az; pp.y = y; pp.rot = p.rot; pp.id = p.id;
    pl.pieces.push_back(pp);
  };
  int idx = -1;
  if (!d->seg[0].empty()) put(-1, d->seg[0][0], rampY);
  idx = 0;
  for (int i = 0; i < pl.leftFill; i++, idx++) if (!d->seg[1].empty()) put(idx, d->seg[1][0], hw);
  for (int rep = 0; rep < pl.m; rep++)
    for (uint32_t pk : d->seg[2]) { put(idx, pk, hw); idx++; }
  for (int i = 0; i < pl.rightFill; i++, idx++) if (!d->seg[3].empty()) put(idx, d->seg[3][0], hw);
  if (!d->seg[4].empty()) put(pl.W, d->seg[4][0], rampY);
  pl.stubTiles = {{pl.x0 - 3 * pl.ax, pl.z0 - 3 * pl.az}, {pl.x0 - 2 * pl.ax, pl.z0 - 2 * pl.az},
                  {ex + pl.ax, ez + pl.az}, {ex + 2 * pl.ax, ez + 2 * pl.az}};
  pl.ok = true;
  return pl;
}

// Approach strip: the rows along the axis from the bank row (pinned) to the deck-side row (pinned) with the strip grader's window; every row copies to
// its perpendicular partner. The whole block must be changeable when any approach tile is not a valid slope [permission over the affected block: the
// original's failure at the train station in the Roadless Paradise x=36 case; the exact block is INFERRED].
static bool gradeApproach(INetworkMap& m, const BridgePlan& pl, int baseX, int baseZ, int sgn, int startH, int endH, int firstRow, int rowStep, int rowsN, int tileFirst, int tilesN) {
  auto vOn = [&](int vx, int vz) { return vx >= 0 && vz >= 0 && vx <= m.width() && vz <= m.height(); };
  bool need = false;
  std::vector<std::pair<int, int>> block;
  for (int t = 0; t < tilesN; t++) {
    const int tx = baseX + sgn * (tileFirst + t) * pl.ax, tz = baseZ + sgn * (tileFirst + t) * pl.az;
    if (!m.onMap(tx, tz)) continue;
    int c[4]; m.tileCorners(tx, tz, c);
    if (!(c[0] == c[1] && c[1] == c[2] && c[2] == c[3]) && !isValidSlope(c)) need = true;
    for (int dx = 0; dx <= 1; dx++) for (int dz = 0; dz <= 1; dz++) block.push_back({tx + dx, tz + dz});
  }
  if (!need) return true;
  for (const auto& v : block) if (vOn(v.first, v.second) && !m.canChangeCorner(v.first, v.second)) return false;
  const int last = rowsN - 2;
  int prev = 0;
  for (int k = 0; k < rowsN; k++) {
    const int rx = baseX + sgn * (firstRow + rowStep * k) * pl.ax, rz = baseZ + sgn * (firstRow + rowStep * k) * pl.az;
    int lo = std::max(0, endH - 2 - 2 * (last - k)), hi = std::min(255, endH + 2 + 2 * (last - k));
    if (k == 0) { lo = std::max(lo, startH); hi = std::min(hi, startH); }
    else { lo = std::max(lo, prev - 2); hi = std::min(hi, prev + 2); }
    if (hi < lo) return false;
    if (!vOn(rx, rz)) { prev = std::max(lo, std::min(hi, prev)); continue; }
    const int h = std::max(lo, std::min(hi, m.cornerHeight(rx, rz)));
    prev = h;
    for (int q = 0; q < 2; q++) { const int vx = rx + q * pl.px, vz = rz + q * pl.pz; if (vOn(vx, vz)) m.setCornerHeight(vx, vz, h); }
  }
  return true;
}

bool buildBridgeTerrain(INetworkMap& m, BridgePlan& pl) {
  if (!pl.ok || pl.terrainBlocked) return false;
  const int D = pl.kind == NK_POWER ? 1 : 5;
  const int ex = pl.x0 + pl.W * pl.ax, ez = pl.z0 + pl.W * pl.az;
  int Bs = 0, Bf = 0;   // the banks' B heights as planned (the B rows of each side)
  for (const auto& c : pl.corners) {
    const bool startSide = c.group == 0;
    const int d = startSide ? std::abs((c.vx - pl.x0) * pl.ax + (c.vz - pl.z0) * pl.az) : std::abs((c.vx - ex) * pl.ax + (c.vz - ez) * pl.az);
    if (d >= 3) { if (startSide) Bs = c.h; else Bf = c.h; }
  }
  pl.terrainDone = true;
  for (const auto& c : pl.corners) if (c.group == 0) m.setCornerHeight(c.vx, c.vz, c.h);   // start bank, written at once
  // start approach: rows W0-4a .. W0-a (B -> hw + D), tiles W0-4a .. W0-a
  if (!gradeApproach(m, pl, pl.x0, pl.z0, -1, Bs, pl.hw + D, 4, -1, 4, 1, 4)) return false;
  for (const auto& c : pl.corners) if (c.group == 1) m.setCornerHeight(c.vx, c.vz, c.h);   // river bed
  for (const auto& c : pl.corners) if (c.group == 2) m.setCornerHeight(c.vx, c.vz, c.h);   // far bank
  // far approach: rows E0+a .. E0+4a (hw + D -> B), tiles E0 .. E0+3a
  if (!gradeApproach(m, pl, ex, ez, +1, pl.hw + D, Bf, 1, +1, 4, 0, 4)) return false;
  return true;
}

bool applyBridge(INetworkMap& m, BridgePlan& pl) {
  if (!pl.ok || pl.terrainBlocked) return false;
  if (!pl.terrainDone) for (auto& c : pl.corners) m.setCornerHeight(c.vx, c.vz, c.h);
  for (auto& pp : pl.pieces) {
    if (!m.onMap(pp.x, pp.z)) continue;
    m.placePiece(pp.x, pp.z, layerOfKind(pl.kind), pp.id, pp.rot, pp.y);
    if (isBridgePierId(pp.id)) m.setPierFlag(pp.x, pp.z, true);
  }
  for (auto& t : pl.stubTiles) {
    if (!m.onMap(t.first, t.second)) continue;
    TileOcc o = m.occupantAt(t.first, t.second, layerOfKind(pl.kind));
    if (o.present && !placementAccepts(pl.kind, o) && !crossingAccepts(pl.kind, o)) continue;
    int c[4];
    m.tileCorners(t.first, t.second, c);
    Piece sp;
    int p = isValidSlope(c) ? slopePatternIndex(c) : -1;
    SlopeCtx sc;
    if (p >= 0 && slopePiece(pl.kind, t.first, t.second, p, sc, sp)) {
      m.placePiece(t.first, t.second, layerOfKind(pl.kind), sp.id, sp.rot, c[0]);
    } else if (pl.kind != NK_HIGHWAY) {
      uint32_t id = pl.kind == NK_ROAD ? 29 : pl.kind == NK_RAIL ? 44 : 92;
      m.placePiece(t.first, t.second, layerOfKind(pl.kind), id, pl.axis, c[0]);
    }
  }
  return true;
}

// ======================= tunnels =======================
TunnelPlan planTunnel(const INetworkMap& m, int kind, const std::vector<PathTile>& path, const PlacementCosts& costs) {
  // Spec 12 s6.4 tunnels [STATIC]: road, rail, and highway (Q-AN).
  TunnelPlan tp;
  if (!(kind == NK_ROAD || kind == NK_RAIL || kind == NK_HIGHWAY) || path.size() < 2) return tp;
  // Primary path: carriage 0 for highway (both carriageways are checked via the pair offset).
  std::vector<PathTile> primary;
  for (const auto& t : path) if (kind != NK_HIGHWAY || t.carriage == 0) primary.push_back(t);
  if (primary.size() < 2) return tp;
  int dx = primary[1].x - primary[0].x, dz = primary[1].z - primary[0].z;
  if (std::abs(dx) + std::abs(dz) != 1) return tp;
  for (size_t i = 1; i < primary.size(); i++)
    if (primary[i].x - primary[i - 1].x != dx || primary[i].z - primary[i - 1].z != dz) return tp;
  tp.dx = dx; tp.dz = dz;
  tp.dir = dx < 0 ? 0 : dz < 0 ? 1 : dx > 0 ? 2 : 3;
  tp.highway = kind == NK_HIGHWAY;
  if (tp.highway) {
    for (const auto& a : path)
      if (a.carriage == 0)
        for (const auto& b : path)
          if (b.carriage == 1 && (b.x - a.x) * dx + (b.z - a.z) * dz == 0) {
            tp.pairDx = b.x - a.x;
            tp.pairDz = b.z - a.z;
            break;
          }
    if (tp.pairDx == 0 && tp.pairDz == 0) return tp;  // need a second carriageway
  }
  const int hillAdd = tp.highway ? 8 : 4;
  auto lowest = [&](int x, int z) { int c[4]; m.tileCorners(x, z, c); return *std::min_element(c, c + 4); };
  auto lowestPair = [&](int x, int z) {
    int h = lowest(x, z);
    if (tp.highway) h = std::min(h, lowest(x + tp.pairDx, z + tp.pairDz));
    return h;
  };
  auto nearFar = [&](int x, int z, int& n0, int& n1, int& f0, int& f1) {
    int c[4];
    m.tileCorners(x, z, c);
    if (dx == 1) { n0 = c[0]; n1 = c[3]; f0 = c[1]; f1 = c[2]; }
    else if (dx == -1) { n0 = c[1]; n1 = c[2]; f0 = c[0]; f1 = c[3]; }
    else if (dz == 1) { n0 = c[0]; n1 = c[1]; f0 = c[3]; f1 = c[2]; }
    else { n0 = c[3]; n1 = c[2]; f0 = c[0]; f1 = c[1]; }
  };
  auto rising = [&](int x, int z) {
    int n0, n1, f0, f1;
    nearFar(x, z, n0, n1, f0, f1);
    if (!(f0 > n0 && f1 > n1)) return false;
    if (!tp.highway) return true;
    nearFar(x + tp.pairDx, z + tp.pairDz, n0, n1, f0, f1);
    return f0 > n0 && f1 > n1;
  };
  for (size_t i = 0; i < primary.size(); i++) {
    if (!m.onMap(primary[i].x, primary[i].z)) return tp;
    if (tp.highway && !m.onMap(primary[i].x + tp.pairDx, primary[i].z + tp.pairDz)) return tp;
    if (!rising(primary[i].x, primary[i].z)) continue;
    const int ex = primary[i].x, ez = primary[i].z, h = lowestPair(ex, ez);
    int L = 1, px = ex + dx, pz = ez + dz;
    if (!m.onMap(px, pz) || (tp.highway && !m.onMap(px + tp.pairDx, pz + tp.pairDz))) return tp;
    bool off = false;
    for (;;) {
      int nx = px + dx, nz = pz + dz;
      if (!m.onMap(nx, nz) || (tp.highway && !m.onMap(nx + tp.pairDx, nz + tp.pairDz))) { off = true; break; }
      px = nx; pz = nz; L++;
      if (lowestPair(px, pz) < h + hillAdd) break;
    }
    if (off || L < 3) return tp;
    tp.ok = true;
    tp.entrance = {ex, ez};
    tp.exit = {ex + L * dx, ez + L * dz};
    tp.length = L;
    tp.spanLength = tp.highway ? L + 1 : L;
    tp.hillTiles = L - 1;
    for (int q = 1; q < L; q++) {
      tp.hidden.push_back({ex + q * dx, ez + q * dz});
      if (tp.highway) tp.hidden.push_back({ex + q * dx + tp.pairDx, ez + q * dz + tp.pairDz});
    }
    const int unit = costs.cost(kTunnelCostKey);
    tp.price = tp.highway ? 2 * L * unit : (L - 1) * unit;
    return tp;
  }
  return tp;
}

// ======================= on-ramp =======================
OnRampPlan planOnRamp(const INetworkMap& m, int ax, int az) {
  OnRampPlan plan;
  auto idAt = [&](int x, int z) -> uint32_t {
    if (!m.onMap(x, z)) return 0;
    TileOcc o = m.occupantAt(x, z, LAYER_SURFACE);
    return o.present ? o.id : 0;
  };
  auto buildable = [&](int x, int z) {
    if (!m.onMap(x, z)) return false;
    TileOcc o = m.occupantAt(x, z, LAYER_SURFACE);
    return !o.present || (o.removable && (o.flags & OF_IGNORABLE));
  };
  auto isRoad = [&](int x, int z) {
    return m.onMap(x, z) && flagsMatchKind(NK_ROAD, m.occupantAt(x, z, LAYER_SURFACE).flags);
  };
  const int ox[5] = {0, 1, 0, -1, 0}, oz[5] = {1, 0, -1, 0, 0};
  for (int a = 0; a < 5; a++) {
    int cx = ax + ox[a], cz = az + oz[a];
    for (int orient = 0; orient < 2; orient++) {
      uint32_t hwy = orient == 0 ? 73 : 15051, under = orient == 0 ? 11258 : 15098;
      int ex = orient == 0 ? 0 : 1, ez = orient == 0 ? 1 : 0;  // along-footprint unit
      int fx0 = cx - ex, fz0 = cz - ez, fx1 = cx + ex, fz1 = cz + ez;
      if (!(buildable(cx, cz) && buildable(fx0, fz0) && buildable(fx1, fz1))) continue;
      // road beyond at least one end
      bool road = isRoad(cx - 2 * ex, cz - 2 * ez) || isRoad(cx + 2 * ex, cz + 2 * ez);
      if (!road) continue;
      for (int s : {-1, 1}) {
        int px = ez * s, pz = ex * s;  // perpendicular to footprint, towards the highway side
        uint32_t h0 = idAt(cx - 2 * ex + px, cz - 2 * ez + pz), h1 = idAt(cx + 2 * ex + px, cz + 2 * ez + pz);
        bool ok = (h0 == hwy && (h1 == hwy || h1 == under)) || (h1 == hwy && (h0 == hwy || h0 == under));
        if (!ok) continue;
        plan.ok = true; plan.anchorX = cx; plan.anchorZ = cz; plan.orientation = orient;
        plan.footprint = {{fx0, fz0}, {cx, cz}, {fx1, fz1}};
        return plan;
      }
    }
  }
  // diagonal ramp: curve piece at the diagonal neighbour, highway pieces two tiles beyond it [INFERRED geometry]
  for (int qx : {-1, 1})
    for (int qz : {-1, 1}) {
      uint32_t c = idAt(ax + qx, az + qz);
      if (c < 15111 || c > 15114) continue;
      uint32_t b0 = idAt(ax + 2 * qx, az + qz), b1 = idAt(ax + qx, az + 2 * qz);
      auto hw = [](uint32_t v) { return v == 73 || v == 15051; };
      if (hw(b0) && hw(b1)) {
        plan.ok = true; plan.diagonal = true; plan.anchorX = ax; plan.anchorZ = az;
        plan.footprint = {{ax, az}};
        return plan;
      }
    }
  return plan;
}

// ======================= neighbour connections =======================
int edgeNeighbour(int x, int z, int W, int H) {
  if (x == 0) return (z == H - 1) ? 3 : 0;
  if (z == 0) return 1;
  if (x == W - 1) return 2;
  if (z == H - 1) return 3;
  return -1;
}

int connectionSoundEvent(int kind) {
  switch (kind) {
    case NK_ROAD: case NK_HIGHWAY: return 36;
    case NK_RAIL: case NK_SUBWAY: return 37;
    case NK_POWER: return 46;
    case NK_PIPE: return 45;
  }
  return 0;
}

ConnectionOfferPlan planConnectionOffer(const INetworkMap& m, const ToolHost& host, const PlacementCosts& costs,
                                        int kind, const std::vector<PathTile>& path, int dragPrice) {
  ConnectionOfferPlan pl;
  pl.kind = kind;
  if (path.empty()) return pl;
  const int W = m.width(), H = m.height();
  auto isEdge = [&](int x, int z) { return x == 0 || z == 0 || x == W - 1 || z == H - 1; };
  if (kind == NK_HIGHWAY) {
    auto scan = [&](size_t from, bool fwd) {
      for (int i = 0; i < 4; i++) {
        long idx = fwd ? long(from) + i : long(path.size()) - 1 - i;
        if (idx < 0 || idx >= long(path.size())) break;
        auto& t = path[size_t(idx)];
        if (isEdge(t.x, t.z) && pl.tiles.size() < 2 &&
            std::find(pl.tiles.begin(), pl.tiles.end(), std::make_pair(t.x, t.z)) == pl.tiles.end())
          pl.tiles.push_back({t.x, t.z});
      }
    };
    scan(0, true);
    scan(0, false);
    if (pl.tiles.empty()) return pl;
    for (auto& t : pl.tiles) if (m.hasConnection(kind, t.first, t.second)) { pl.tiles.clear(); return pl; }
    pl.edge = edgeNeighbour(pl.tiles[0].first, pl.tiles[0].second, W, H);
    if (pl.edge < 0 || !host.neighbourExists(pl.edge)) { pl.tiles.clear(); return pl; }
  } else {
    // ends of the drag; only one connection per drag
    std::vector<std::pair<int, int>> ends = {{path.front().x, path.front().z}};
    if (path.back().x != path.front().x || path.back().z != path.front().z) ends.push_back({path.back().x, path.back().z});
    for (auto& e : ends) {
      if (!isEdge(e.first, e.second)) continue;
      int ed = edgeNeighbour(e.first, e.second, W, H);
      if (ed < 0 || !host.neighbourExists(ed)) continue;
      if (m.hasConnection(kind, e.first, e.second)) continue;
      pl.tiles.push_back(e);
      pl.edge = ed;
      break;
    }
    if (pl.tiles.empty()) return pl;
  }
  pl.price = costs.cost(connectionCostKey(kind));
  if (pl.price != 0 && host.funds() < long(dragPrice) + pl.price) {
    pl.insufficientFunds = true;
    pl.tiles.clear();
    return pl;
  }
  pl.ok = true;
  return pl;
}

int offerConnections(INetworkMap& m, ToolHost& host, const PlacementCosts& costs, int kind,
                     const std::vector<PathTile>& path, int dragPrice) {
  ConnectionOfferPlan pl = planConnectionOffer(m, host, costs, kind, path, dragPrice);
  if (pl.insufficientFunds) {
    host.message("insufficient funds: neighbour connection");
    return 0;
  }
  if (!pl.ok) return 0;
  if (!host.confirmConnection(kind, pl.price)) return 0;
  for (auto& t : pl.tiles) m.setConnection(kind, t.first, t.second, true);
  if (pl.price) host.charge(pl.price);
  host.sound(connectionSoundEvent(kind));
  return int(pl.tiles.size());
}

int removeConnectionsAt(INetworkMap& m, int x, int z) {
  int n = 0;
  for (int k : {NK_ROAD, NK_HIGHWAY, NK_RAIL, NK_SUBWAY, NK_PIPE, NK_POWER})
    if (m.hasConnection(k, x, z)) { m.setConnection(k, x, z, false); n++; }
  return n;
}

// ======================= commit =======================
// Network endpoint levelling (spec 12 6.2.6 step 3): at a path end the tiles A (the end tile) and B (the next tile for a highway, else A)
// are levelled to the average of their corner rectangle's perimeter, unless an occupant stands on them, both tiles already have a valid slope
// (flat or a straight ramp of rise <= 2) or both are flat. No money is charged and a refusal is silent: the network is built anyway.
// Returns the sum of the absolute corner changes (0 = terrain unchanged).
static int levelEndpoint(INetworkMap& m, const std::vector<PathTile>& path, bool start, bool highway) {
  if (path.empty()) return 0;
  // Highway: A and B are consecutive tiles of the first carriageway (A the end tile, B the previous / next one along the drag), not the pair partner
  // [OBSERVED: the original's levelled corners lie along the drag, x 40..41 and z 162..164, for an end at (40,163)].
  std::vector<PathTile> lane;
  for (const PathTile& t : path) if (!highway || t.carriage == 0) lane.push_back(t);
  if (lane.empty()) return 0;
  const PathTile& a = start ? lane.front() : lane.back();
  const PathTile& b = highway && lane.size() > 1 ? (start ? lane[1] : lane[lane.size() - 2]) : a;
  for (const PathTile* t : {&a, &b}) if (!m.onMap(t->x, t->z) || m.occupantAt(t->x, t->z, LAYER_SURFACE).present) return 0;
  bool bothValid = true, bothFlat = true;
  for (const PathTile* t : {&a, &b}) {
    int c[4]; m.tileCorners(t->x, t->z, c);
    if (!isValidSlope(c)) bothValid = false;
    if (!(c[0] == c[1] && c[1] == c[2] && c[2] == c[3])) bothFlat = false;
  }
  if (bothValid || bothFlat) return 0;
  const int x0 = std::min(a.x, b.x) - 8, x1 = std::max(a.x, b.x) + 9, z0 = std::min(a.z, b.z) - 8, z1 = std::max(a.z, b.z) + 9;
  auto snap = [&]() {
    std::vector<int> v;
    for (int vx = x0; vx <= x1; vx++)
      for (int vz = z0; vz <= z1; vz++) v.push_back(vx >= 0 && vz >= 0 && vx <= m.width() && vz <= m.height() ? m.cornerHeight(vx, vz) : 0);
    return v;
  };
  const std::vector<int> before = snap();
  m.levelRectToAverage(std::min(a.x, b.x), std::min(a.z, b.z), std::max(a.x, b.x), std::max(a.z, b.z));
  const std::vector<int> after = snap();
  int k = 0;
  for (size_t i = 0; i < before.size(); i++) k += std::abs(after[i] - before[i]);
  return k;
}

// Highway cross-section levelling [OBSERVED once, Roadless Paradise drag B; INFERRED rule]: every vertex row across the two carriageways of the built
// path (3 vertices) is set to its minimum when its heights differ. Refused as a whole when a changing vertex is not changeable. True when terrain changed.
static int levelHighwayCrossRows(INetworkMap& m, const std::vector<PathTile>& lane, int pairDx, int pairDz) {
  if (lane.empty() || (pairDx == 0 && pairDz == 0)) return 0;
  std::set<std::vector<std::pair<int, int>>> rows;
  for (const PathTile& t : lane) {
    if (!m.onMap(t.x, t.z)) continue;
    const int ox = std::min(t.x, t.x + pairDx), oz = std::min(t.z, t.z + pairDz);
    for (int k = 0; k < 2; k++) {
      std::vector<std::pair<int, int>> row;
      if (pairDx != 0) { const int vz = t.z + k; for (int i = 0; i < 3; i++) row.push_back({ox + i, vz}); }
      else { const int vx = t.x + k; for (int i = 0; i < 3; i++) row.push_back({vx, oz + i}); }
      rows.insert(row);
    }
  }
  std::vector<std::pair<std::pair<int, int>, int>> plan;
  for (const auto& row : rows) {
    int lo = 1 << 30; bool differ = false;
    for (const auto& v : row) { if (v.first < 0 || v.second < 0 || v.first > m.width() || v.second > m.height()) { lo = -1; break; } const int h = m.cornerHeight(v.first, v.second); if (h != m.cornerHeight(row[0].first, row[0].second)) differ = true; lo = std::min(lo, h); }
    if (lo < 0 || !differ) continue;
    for (const auto& v : row) if (m.cornerHeight(v.first, v.second) != lo) plan.push_back({v, lo});
  }
  if (plan.empty()) return 0;
  for (const auto& p : plan) if (!m.canChangeCorner(p.first.first, p.first.second)) return 0;
  int k = 0;
  for (const auto& p : plan) { k += std::abs(m.cornerHeight(p.first.first, p.first.second) - p.second); m.setCornerHeight(p.first.first, p.first.second, p.second); }
  return k;
}

CommitResult commitPath(INetworkMap& m, const TilingRuleSet& rules, const PlacementCosts& costs, ToolHost& host,
                        Tool tool, const RoutedPath& rp) {
  CommitResult res;
  const ToolParams& P = toolParams(tool);
  const int tileCost = costs.cost(P.costKey);
  int valid = 0;
  for (auto& t : rp.tiles) if (t.valid) valid++;
  // A blocked drag builds its valid prefix (nothing at all when the prefix is empty); the price is that of every routed tile, built or not
  // [OBSERVED: Roadless Paradise, 42 tiles x 150 charged for a 12-tile prefix; spec 6.2.5 said invalid tiles are free].
  if (valid == 0) { host.sound(30); res.status = COMMIT_INVALID; return res; }
  int dragPrice = int(rp.tiles.size()) * tileCost;
  if (dragPrice > host.funds()) { host.sound(32); res.status = COMMIT_NO_MONEY; return res; }

  if (tool == TOOL_ONRAMP) {
    OnRampPlan plan = planOnRamp(m, rp.tiles[0].x, rp.tiles[0].z);
    if (!plan.ok || !host.buildOnRamp(plan)) { host.sound(30); res.status = COMMIT_INVALID; return res; }
    host.charge(dragPrice);
    res.spent = dragPrice; res.rampPlaced = 1;
    host.sound(14);
    return res;
  }

  std::vector<PathTile> path = toPathTiles(rp, true);
  const NetworkRules& nr = rules.nets[netKindToTable(P.kind)];
  std::set<std::pair<int, int>> skip;
  std::vector<std::pair<int, int>> context;
  int extraSpent = 0;

  // bridge attempt
  std::vector<BridgePlan> bridges;
  if (path.size() >= 4 && !nr.bridges.empty() &&
      (P.kind == NK_ROAD || P.kind == NK_RAIL || P.kind == NK_POWER || P.kind == NK_HIGHWAY)) {
    int passes = P.kind == NK_HIGHWAY ? 2 : 1;
    int total = 0;
    for (int pass = 0; pass < passes; pass++) {
      std::vector<PathTile> sub;
      for (auto& t : path) if (P.kind != NK_HIGHWAY || t.carriage == pass) sub.push_back(t);
      BridgePlan bp = planBridge(m, nr, P.kind, sub, costs, pass);
      if (!bp.ok) continue;
      total += bp.price;
      bridges.push_back(bp);
    }
    if (!bridges.empty()) {
      if (total > host.funds()) {
        host.message("insufficient funds: bridge");
        res.status = COMMIT_NO_MONEY;
        return res;
      }
      if (!host.confirmBridge(total, P.kind == NK_POWER)) { res.status = COMMIT_CANCELLED; return res; }
      for (auto& b : bridges)
        if (b.terrainBlocked) {   // a bank corner with an occupant next to it: nothing is written
          host.message("construction discontinued");
          res.status = COMMIT_CANCELLED;
          return res;
        }
      if (P.kind != NK_HIGHWAY)
        for (auto& b : bridges)
          if (!buildBridgeTerrain(m, b)) {   // refused at a grade: what was written stays, nothing is built or charged [OBSERVED]
            host.message("construction discontinued");
            res.status = COMMIT_CANCELLED;
            return res;
          }
      for (auto& b : bridges) {
        applyBridge(m, b);
        for (auto& c : b.coveredTiles()) { skip.insert(c); context.push_back(c); }
        res.bridges++;
      }
      host.charge(total);
      extraSpent += total;
    }
  }
  // tunnel attempt (road, rail, highway — spec 12 s6.4 / Q-AN)
  if (res.bridges == 0 && (P.kind == NK_ROAD || P.kind == NK_RAIL || P.kind == NK_HIGHWAY)) {
    TunnelPlan tp = planTunnel(m, P.kind, path, costs);
    if (tp.ok && tp.price <= host.funds() && host.confirmTunnel(tp.price)) {
      const int dIn = tp.dir, dOut = (tp.dir + 2) & 3;
      auto altAt = [&](int x, int z) {
        int c[4]; m.tileCorners(x, z, c); return *std::min_element(c, c + 4);
      };
      if (tp.highway) {
        // Four pieces per end (spec 12 s6.4 item 7 / Q-AN). Tile offsets of the four pieces within the
        // portal remain [OPEN]; interim: plains one tile before the portal face, spans on the face.
        static const uint32_t spanA[4] = {18030, 18029, 18030, 18029}, spanB[4] = {18028, 18027, 18028, 18027};
        static const uint32_t plainA[4] = {18091, 18090, 18091, 18090}, plainB[4] = {18089, 18088, 18089, 18088};
        static const int rotOf[4] = {0, 0, 2, 2};
        auto end = [&](std::pair<int, int> face, int d) {
          // Outside the portal face = opposite the travel vector of d; exact offsets remain [OPEN].
          static const int tdx[4] = {-1, 0, 1, 0}, tdz[4] = {0, -1, 0, 1};
          const int fx = face.first, fz = face.second;
          const int bx = fx - tdx[d], bz = fz - tdz[d];
          const int px = tp.pairDx, pz = tp.pairDz;
          struct P { int x, z; uint32_t id; bool span; };
          const P order[4] = {{bx, bz, plainA[d], false},
                              {bx + px, bz + pz, plainB[d], false},
                              {fx, fz, spanA[d], true},
                              {fx + px, fz + pz, spanB[d], true}};
          for (auto& o : order) {
            if (!m.onMap(o.x, o.z)) continue;
            m.placePiece(o.x, o.z, LAYER_SURFACE, o.id, rotOf[d], altAt(o.x, o.z));
            if (o.span) m.setPieceSpan(o.x, o.z, LAYER_SURFACE, d, tp.spanLength);
            skip.insert({o.x, o.z});
          }
        };
        end(tp.entrance, dIn);
        end(tp.exit, dOut);
      } else {
        // Two portal pieces only: entrance along d, exit back, both with length L.
        static const uint32_t roadId[4] = {64, 63, 64, 63}, railId[4] = {18024, 18019, 18024, 18019};
        static const int rotOf[4] = {0, 0, 2, 2};
        auto portal = [&](std::pair<int, int> t, int d) {
          m.placePiece(t.first, t.second, LAYER_SURFACE, P.kind == NK_RAIL ? railId[d] : roadId[d], rotOf[d],
                       altAt(t.first, t.second));
          m.setPieceSpan(t.first, t.second, LAYER_SURFACE, d, tp.spanLength ? tp.spanLength : tp.length);
        };
        portal(tp.entrance, dIn);
        portal(tp.exit, dOut);
        skip.insert(tp.entrance);
        skip.insert(tp.exit);
      }
      host.buildTunnel(P.kind, tp);
      host.charge(tp.price);
      extraSpent += tp.price;
      res.tunnels++;
      for (auto& t : tp.hidden) skip.insert(t);
      for (auto& t : skip) context.push_back(t);
    }
  }
  // Endpoint levelling: at the start always, at the end only when neither a bridge nor a tunnel was built.
  // Terrain operations are charged LevelTerrain x k >> 2, k = the sum of the absolute corner changes [STATIC SIMDIRT level plan 0x1000A750; observed 25
  // for four corners moving 1 each].
  int levelK = 0;
  if (P.kind != NK_HIGHWAY) {   // strip grade of the straight runs (charged: LevelTerrain x raised/lowered >> 2)
    std::vector<std::pair<int, int>> ts; std::vector<char> sk;
    for (const PathTile& t : path) { ts.push_back({t.x, t.z}); sk.push_back(skip.count({t.x, t.z}) ? 1 : 0); }
    StripGrade g = planStripGrade(m, ts, sk);
    if (g.failFrom == size_t(-1)) for (const auto& v : g.verts) m.setCornerHeight(v.x, v.z, v.h);
    levelK += g.raised + g.lowered;
  }
  if (P.kind == NK_HIGHWAY) {
    std::vector<PathTile> lane0;
    for (const PathTile& t : path) if (t.carriage == 0 && !skip.count({t.x, t.z})) lane0.push_back(t);
    levelK += levelHighwayCrossRows(m, lane0, rp.pairDx, rp.pairDz);
  }
  levelK += levelEndpoint(m, path, true, P.kind == NK_HIGHWAY);
  if (res.bridges == 0 && res.tunnels == 0) levelK += levelEndpoint(m, path, false, P.kind == NK_HIGHWAY);
  if (levelK > 0 && costs.levelTerrain > 0) { const int c = (costs.levelTerrain * levelK) >> 2; host.charge(c); extraSpent += c; }
  // connections
  res.connections = offerConnections(m, host, costs, P.kind, path, dragPrice);

  // tile
  std::vector<PathTile> tilePath;
  bool wetCut = false;
  for (auto& t : path) {
    if (skip.count({t.x, t.z})) continue;
    // A water tile no bridge or tunnel covered is not built, nor is anything after it (roads and rails now validate across water; the bridge decides).
    if (P.kind != NK_HIGHWAY && m.onMap(t.x, t.z) && m.isWaterTile(t.x, t.z)) wetCut = true;
    if (wetCut) continue;
    tilePath.push_back(t);
  }
  TilerOptions opt;
  opt.pairDx = rp.pairDx; opt.pairDz = rp.pairDz;
  opt.contextPath = context;
  Tiler tiler(rules, m);
  res.tiled = tiler.run(P.kind, tilePath, opt);
  if (P.kind == NK_HIGHWAY) {
    std::set<std::pair<int, int>> pathSet;
    for (auto& t : path) pathSet.insert({t.x, t.z});
    std::set<std::pair<int, int>> roads;
    static const int dx[4] = {-1, 0, 1, 0}, dz[4] = {0, -1, 0, 1};
    for (auto& t : path)
      for (int d = 0; d < 4; d++) {
        int nx = t.x + dx[d], nz = t.z + dz[d];
        if (!m.onMap(nx, nz) || pathSet.count({nx, nz})) continue;
        if (flagsMatchKind(NK_ROAD, m.occupantAt(nx, nz, LAYER_SURFACE).flags)) roads.insert({nx, nz});
      }
    std::vector<PathTile> rp2;
    for (auto& r : roads) { PathTile pt; pt.x = r.first; pt.z = r.second; rp2.push_back(pt); }
    if (!rp2.empty()) {
      TilerResult r2 = tiler.run(NK_ROAD, rp2, TilerOptions());
      res.tiled.changes.insert(res.tiled.changes.end(), r2.changes.begin(), r2.changes.end());
    }
  }
  host.charge(dragPrice);
  res.spent = dragPrice + extraSpent;
  host.sound(tool == TOOL_RAIL ? 51 : tool == TOOL_SUBWAY ? 53 : tool == TOOL_POWER ? 16 : tool == TOOL_PIPE ? 50 : 14);  // spec 09 table 5.2
  return res;
}

TilerResult demolish(INetworkMap& m, const TilingRuleSet& rules, int kind, const std::vector<std::pair<int, int>>& tiles) {
  NetLayer layer = layerOfKind(kind);
  std::set<std::pair<int, int>> removed;
  for (auto& t : tiles) {
    if (!m.onMap(t.first, t.second)) continue;
    TileOcc o = m.occupantAt(t.first, t.second, layer);
    if (!flagsMatchKind(kind, o.flags) || !o.present) continue;
    const std::vector<uint32_t>* g = rules.collapseGroupOf(o.id);
    std::vector<std::pair<int, int>> stack{t};
    std::set<std::pair<int, int>> seen{t};
    while (!stack.empty()) {
      auto cur = stack.back(); stack.pop_back();
      removed.insert(cur);
      if (!g) continue;
      for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++) {
          std::pair<int, int> n{cur.first + dx, cur.second + dz};
          if (seen.count(n) || !m.onMap(n.first, n.second)) continue;
          TileOcc no = m.occupantAt(n.first, n.second, layer);
          if (no.present && std::find(g->begin(), g->end(), no.id) != g->end()) { seen.insert(n); stack.push_back(n); }
        }
    }
  }
  std::vector<PathTile> path;
  TilerResult out;
  for (auto& t : removed) {
    m.removePiece(t.first, t.second, layer);
    removeConnectionsAt(m, t.first, t.second);
    TileChange ch; ch.x = t.first; ch.z = t.second; ch.layer = layer; ch.y = m.cornerHeight(t.first, t.second);
    out.changes.push_back(ch);
    PathTile pt; pt.x = t.first; pt.z = t.second;
    path.push_back(pt);
  }
  if (path.empty()) return out;
  TilerOptions opt;
  opt.demolition = true;
  Tiler tiler(rules, m);
  TilerResult r = tiler.run(kind, path, opt);
  out.changes.insert(out.changes.end(), r.changes.begin(), r.changes.end());
  out.simplePasses = r.simplePasses; out.complexPasses = r.complexPasses;
  return out;
}

// ======================= power / water =======================
std::vector<uint8_t> boundedFloodFill(int W, int H, const std::vector<uint8_t>& cond, int sx, int sz, int maxIter) {
  std::vector<uint8_t> g(size_t(W) * H, 0);
  if (sx < 0 || sz < 0 || sx >= W || sz >= H) return g;
  g[size_t(sx) * H + sz] = 1;
  std::vector<uint8_t> next;
  for (int it = 0; it < maxIter; it++) {
    bool changed = false;
    // horizontal: +-1 in x
    next = g;
    for (int x = 0; x < W; x++)
      for (int z = 0; z < H; z++) {
        if (!g[size_t(x) * H + z]) continue;
        if (x > 0 && cond[size_t(x - 1) * H + z] && !next[size_t(x - 1) * H + z]) { next[size_t(x - 1) * H + z] = 1; changed = true; }
        if (x + 1 < W && cond[size_t(x + 1) * H + z] && !next[size_t(x + 1) * H + z]) { next[size_t(x + 1) * H + z] = 1; changed = true; }
      }
    g.swap(next);
    next = g;
    for (int x = 0; x < W; x++)
      for (int z = 0; z < H; z++) {
        if (!g[size_t(x) * H + z]) continue;
        if (z > 0 && cond[size_t(x) * H + z - 1] && !next[size_t(x) * H + z - 1]) { next[size_t(x) * H + z - 1] = 1; changed = true; }
        if (z + 1 < H && cond[size_t(x) * H + z + 1] && !next[size_t(x) * H + z + 1]) { next[size_t(x) * H + z + 1] = 1; changed = true; }
      }
    g.swap(next);
    if (!changed) break;
  }
  return g;
}

void markRect(std::vector<uint8_t>& grid, int W, int H, const Rect& r, int margin, uint8_t value) {
  int x0 = std::max(0, r.x - margin), x1 = std::min(W - 1, r.x + r.w - 1 + margin);
  int z0 = std::max(0, r.z - margin), z1 = std::min(H - 1, r.z + r.h - 1 + margin);
  for (int x = x0; x <= x1; x++)
    for (int z = z0; z <= z1; z++) grid[size_t(x) * H + z] = value;
}

PowerResult computePower(int W, int H, const std::vector<uint8_t>& cond, const std::vector<uint8_t>& demand,
                         const std::vector<PowerSource>& plants, const std::vector<PowerSource>& feeds) {
  PowerResult res;
  res.powered.assign(size_t(W) * H, 0);
  std::vector<uint8_t> visited(plants.size(), 0);
  for (size_t i = 0; i < plants.size(); i++) {
    if (visited[i]) continue;
    auto reached = boundedFloodFill(W, H, cond, plants[i].x, plants[i].z, kPowerFloodIterations);
    long supply = 0;
    for (size_t j = 0; j < plants.size(); j++) {
      if (visited[j]) continue;
      if (plants[j].x >= 0 && plants[j].z >= 0 && plants[j].x < W && plants[j].z < H &&
          reached[size_t(plants[j].x) * H + plants[j].z]) {
        visited[j] = 1;
        supply += plants[j].supply;
      }
    }
    for (auto& f : feeds)
      if (f.x >= 0 && f.z >= 0 && f.x < W && f.z < H && reached[size_t(f.x) * H + f.z]) supply += f.supply;
    res.networkSupply.push_back(int(supply));
    long remaining = supply;
    bool exhausted = false;
    for (int z = 0; z < H; z++)
      for (int x = 0; x < W; x++) {
        size_t k = size_t(x) * H + z;
        if (!reached[k]) continue;
        if (exhausted) continue;
        int d = demand.empty() ? 0 : demand[k];
        res.powered[k] = 1;
        if (d == 0) continue;
        if (d <= remaining) remaining -= d;
        else { remaining = 0; exhausted = true; }
      }
  }
  return res;
}

std::vector<uint8_t> dilate(int W, int H, const std::vector<uint8_t>& g, int steps) {
  std::vector<uint8_t> cur = g, nx;
  for (int s = 0; s < steps; s++) {
    nx = cur;
    for (int x = 0; x < W; x++)
      for (int z = 0; z < H; z++)
        if (cur[size_t(x) * H + z]) {
          if (x > 0) nx[size_t(x - 1) * H + z] = 1;
          if (x + 1 < W) nx[size_t(x + 1) * H + z] = 1;
        }
    cur = nx;
    for (int x = 0; x < W; x++)
      for (int z = 0; z < H; z++)
        if (cur[size_t(x) * H + z]) {
          if (z > 0) nx[size_t(x) * H + z - 1] = 1;
          if (z + 1 < H) nx[size_t(x) * H + z + 1] = 1;
        }
    cur = nx;
  }
  return cur;
}

std::vector<WaterNet> computeWater(int W, int H, const std::vector<uint8_t>& cond, const std::vector<PowerSource>& src,
                                   int waterFromPipe) {
  std::vector<WaterNet> nets;
  std::vector<uint8_t> visited(src.size(), 0);
  for (size_t i = 0; i < src.size(); i++) {
    if (visited[i] || src[i].supply <= 0) continue;
    WaterNet n;
    n.reached = boundedFloodFill(W, H, cond, src[i].x, src[i].z, kWaterFloodIterations);
    for (size_t j = 0; j < src.size(); j++) {
      if (visited[j] || src[j].supply <= 0) continue;
      if (src[j].x >= 0 && src[j].z >= 0 && src[j].x < W && src[j].z < H && n.reached[size_t(src[j].x) * H + src[j].z]) {
        visited[j] = 1;
        n.output += src[j].supply;
        n.sources.push_back(int(j));
      }
    }
    // coverage: dilate the reached *pipe* tiles
    std::vector<uint8_t> pipes(n.reached.size(), 0);
    for (size_t k = 0; k < pipes.size(); k++) pipes[k] = n.reached[k] && cond[k];
    n.coverage = dilate(W, H, pipes, effectiveWaterFromPipe(waterFromPipe));
    nets.push_back(std::move(n));
  }
  return nets;
}

// ======================= road condition =======================
namespace {
bool inList(const std::vector<uint32_t>& l, uint32_t id) { return std::find(l.begin(), l.end(), id) != l.end(); }
bool isDamagedId(const PotholeConfig& c, uint32_t id) { return inList(c.damagedOrdinary, id) || id == c.damagedSecond; }
}  // namespace

double roadCondition(const INetworkMap& m, const PotholeConfig& cfg) {
  int n = 0, d = 0;
  for (auto& p : m.pieces(LAYER_SURFACE)) {
    TileOcc o = m.occupantAt(p.x, p.z, LAYER_SURFACE);
    bool dmg = isDamagedId(cfg, p.id);
    if (flagsMatchKind(NK_ROAD, o.flags) || dmg) { n++; if (dmg) d++; }
  }
  return n ? double(d) / n : 0.0;
}

int setRoadCondition(INetworkMap& m, double v, const std::function<int(int)>& rnd, const PotholeConfig& cfg) {
  auto pcs = m.pieces(LAYER_SURFACE);
  int N = 0, D = 0;
  for (auto& p : pcs) {
    TileOcc o = m.occupantAt(p.x, p.z, LAYER_SURFACE);
    bool dmg = isDamagedId(cfg, p.id);
    if (flagsMatchKind(NK_ROAD, o.flags) || dmg) { N++; if (dmg) D++; }
  }
  int T = int(v * N);
  if (T == D) return 0;
  bool damage = T > D;
  int k = damage ? T - D : D - T;
  int s = N / k - 1;
  auto gap = [&]() { return s >= 1 ? rnd(s) : 0; };
  int skip = gap(), changed = 0;
  for (auto& p : pcs) {
    bool dmg = isDamagedId(cfg, p.id);
    bool elig = damage ? (!dmg && (inList(cfg.ordinary, p.id) || inList(cfg.second, p.id))) : dmg;
    if (!elig) continue;
    if (skip > 0) { skip--; continue; }
    uint32_t nid;
    if (damage) nid = inList(cfg.second, p.id) ? cfg.damagedSecond : cfg.damagedOrdinary[size_t(rnd(int(cfg.damagedOrdinary.size())))];
    else nid = (p.id == cfg.damagedSecond && cfg.repairedSecond) ? cfg.repairedSecond : cfg.repairedOrdinary;
    m.placePiece(p.x, p.z, LAYER_SURFACE, nid, p.rot, p.y);
    changed++;
    if (--k == 0) break;
    skip = gap();
  }
  return changed;
}

// ======================= persisted records =======================
uint32_t packPlace(int x, int z, int y, int rot) {
  return uint32_t(x & 0x7FF) | (uint32_t(z & 0x7FF) << 11) | (uint32_t(y & 0xFF) << 22) | (uint32_t(rot & 3) << 30);
}
uint32_t makePieceWord(uint32_t id) { return (id & 0xFFFF) | 0x30000000u; }

namespace {
// Returns header end; sets version.
size_t readNetHdr(Reader& r, uint16_t& version) {
  version = r.u16();
  uint8_t flags = r.u8();
  if (flags & 2) r.u8();
  uint32_t s = r.u32();
  if (s != 0xDEADBEEF) throw FormatError("network record: bad sentinel");
  if (flags & 1) { uint32_t l = r.u32(); r.skip(l); }
  return r.pos();
}
void netHdr(Bytes& b) {
  put16(b, 4); b.push_back(2); b.push_back(0);
  put32(b, 0xDEADBEEF);
}
}  // namespace

bool parseNetworkHeader(const uint8_t* p, size_t n, uint32_t counts[3]) {
  Reader r(p, n);
  uint16_t v;
  readNetHdr(r, v);
  if (v != 4) throw FormatError("network header: unsupported version");
  counts[0] = r.u32(); counts[1] = r.u32(); counts[2] = r.u32();
  return r.u32() == 1;
}

std::vector<NetPiece> parseNetworkList(const uint8_t* p, size_t n, uint32_t count) {
  Reader r(p, n);
  uint16_t v;
  readNetHdr(r, v);
  if (v != 4) throw FormatError("network list: unsupported version");
  if (count > r.remaining() / 8) throw FormatError("network list: truncated");
  std::vector<NetPiece> out;
  out.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    NetPiece np;
    uint32_t place = r.u32();
    np.pieceWord = r.u32();
    np.x = int(place & 0x7FF); np.z = int((place >> 11) & 0x7FF);
    np.y = int((place >> 22) & 0xFF); np.rot = int(place >> 30);
    np.pieceId = np.pieceWord & 0xFFFF;
    out.push_back(np);
  }
  return out;
}

NetworkRecords parseNetworkRecords(const InnerDb& db) {
  NetworkRecords nr;
  const uint8_t* p; size_t n;
  if (!db.find({kNetRecType, kNetRecGroup, 0}, p, n)) return nr;
  uint32_t counts[3];
  if (!parseNetworkHeader(p, n, counts)) return nr;
  for (int i = 0; i < 3; i++) {
    if (!counts[i]) continue;
    if (!db.find({kNetRecType, kNetRecGroup, uint32_t(i + 1)}, p, n)) continue;
    nr.layer[i] = parseNetworkList(p, n, counts[i]);
  }
  return nr;
}

std::vector<std::pair<ResKey, Bytes>> serializeNetworkRecords(const NetworkRecords& r) {
  std::vector<std::pair<ResKey, Bytes>> out;
  Bytes h;
  netHdr(h);
  for (int i = 0; i < 3; i++) put32(h, uint32_t(r.layer[i].size()));
  put32(h, 1);
  out.push_back({ResKey{kNetRecType, kNetRecGroup, 0}, h});
  for (int i = 0; i < 3; i++) {
    if (r.layer[i].empty()) continue;
    Bytes b;
    netHdr(b);
    for (auto& p : r.layer[i]) {
      put32(b, packPlace(p.x, p.z, p.y, p.rot));
      put32(b, p.pieceWord ? p.pieceWord : makePieceWord(p.pieceId));
    }
    out.push_back({ResKey{kNetRecType, kNetRecGroup, uint32_t(i + 1)}, b});
  }
  return out;
}

}  // namespace m3k
