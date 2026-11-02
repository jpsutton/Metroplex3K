#include "core/tiler.h"
#include <algorithm>
#include <cstdlib>

namespace m3k {

NetLayer layerOfKind(int kind) {
  if (kind == NK_PIPE) return LAYER_PIPE;
  if (kind == NK_SUBWAY) return LAYER_SUBWAY;
  return LAYER_SURFACE;
}

bool flagsMatchKind(int kind, uint32_t f) {
  auto has = [&](uint32_t m) { return (f & m) == m; };
  switch (kind) {
    case NK_ROAD: return has(OF_ROAD | OF_NETPIECE);
    case NK_HIGHWAY: return has(OF_HIGHWAY | OF_NETPIECE);
    case NK_RAIL: return has(OF_RAIL | OF_NETPIECE);
    case NK_SUBWAY: return has(OF_SUBWAY);
    case NK_PIPE: return has(OF_PIPE);
    case NK_POWER: return has(OF_POWER | OF_NETPIECE);
  }
  return false;
}

uint32_t defaultFlagsForKind(int kind) {
  // Spec 13 §3.1a [STATIC]: every network piece has 0x800; all except pipes and plain power also
  // have 0x4000 (OF_TRANSPORT). Membership bits follow.
  switch (kind) {
    case NK_ROAD: return OF_ROAD | OF_NETPIECE | OF_TRANSPORT;
    case NK_HIGHWAY: return OF_HIGHWAY | OF_NETPIECE | OF_TRANSPORT;
    case NK_RAIL: return OF_RAIL | OF_NETPIECE | OF_TRANSPORT;
    case NK_SUBWAY: return OF_SUBWAY | OF_NETPIECE | OF_TRANSPORT;
    case NK_PIPE: return OF_PIPE | OF_NETPIECE;
    case NK_POWER: return OF_POWER | OF_NETPIECE;
  }
  return 0;
}

uint32_t runtimeOccupantFlags(uint32_t agentType) {
  // Spec 13 §3.1a [STATIC]. Utility / station / landmark-deal words unchanged; generic buildings
  // are a constant 0x104 with no per-agent-type table (Q-BC).
  if ((agentType >= 12111 && agentType <= 12118) || agentType == 12853) return kOccFlagPowerPlant;
  if (agentType == 12810 || agentType == 12820 || agentType == 12830 || agentType == 12840)
    return kOccFlagWaterFacility;
  if (agentType == 12210 || agentType == 12230 || agentType == 12360 || agentType == 12361)
    return kOccFlagTransitStation;
  // A few landmark / business-deal classes (spec 13 §3.1a / §8).
  if ((agentType >= 18000 && agentType <= 18045) || agentType == 19999 ||
      (agentType >= 19000 && agentType <= 19004))
    return kOccFlagLandmarkDeal;
  return kOccFlagGenericBuilding;
}

void INetworkMap::tileCorners(int x, int z, int c[4]) const {
  c[0] = cornerHeight(x, z);
  c[1] = cornerHeight(x + 1, z);
  c[2] = cornerHeight(x + 1, z + 1);
  c[3] = cornerHeight(x, z + 1);
}
bool INetworkMap::isFlat(int x, int z) const {
  int c[4];
  tileCorners(x, z, c);
  return c[1] == c[0] && c[2] == c[0] && c[3] == c[0];
}

// ---------------- SimpleNetworkMap ----------------
SimpleNetworkMap::SimpleNetworkMap(int w, int h, int base) : W(w), H(h), alt(size_t(w + 1) * (h + 1), base) {}

uint32_t SimpleNetworkMap::flagsOf(uint32_t id) const {
  auto it = flags.find(id);
  return it == flags.end() ? 0 : it->second;
}

TileOcc SimpleNetworkMap::occupantAt(int x, int z, NetLayer layer) const {
  TileOcc o;
  auto it = layerPieces[layer].find({x, z});
  if (it != layerPieces[layer].end()) {
    o.present = true; o.id = it->second.id; o.rot = it->second.rot; o.flags = flagsOf(o.id);
    return o;
  }
  if (layer == LAYER_SURFACE) {
    auto f = foreign.find({x, z});
    if (f != foreign.end()) return f->second;
  }
  return o;
}

void SimpleNetworkMap::placePiece(int x, int z, NetLayer layer, uint32_t id, int rot, int altitude) {
  if (layer == LAYER_SURFACE) foreign.erase({x, z});
  PlacedPiece p; p.x = x; p.z = z; p.y = altitude; p.rot = rot; p.id = id;
  layerPieces[layer][{x, z}] = p;
}
void SimpleNetworkMap::removePiece(int x, int z, NetLayer layer) { layerPieces[layer].erase({x, z}); }

void SimpleNetworkMap::setConnection(int kind, int x, int z, bool on) {
  if (on) conns.insert({kind, x, z}); else conns.erase({kind, x, z});
}

unsigned SimpleNetworkMap::exitMask(uint32_t id, int rot) const {
  auto it = exits.find(id);
  if (it == exits.end()) return 0xF;
  unsigned m = it->second & 0xF, r = unsigned(rot) & 3;
  return ((m << r) | (m >> (4 - r))) & 0xF;
}

bool SimpleNetworkMap::canChangeCorner(int vx, int vz) const {
  for (int dx = -1; dx <= 0; dx++)
    for (int dz = -1; dz <= 0; dz++) {
      int x = vx + dx, z = vz + dz;
      if (!onMap(x, z)) continue;
      if (layerPieces[LAYER_SURFACE].count({x, z})) return false;
      auto f = foreign.find({x, z});
      if (f != foreign.end() && !(f->second.flags & OF_IGNORABLE)) return false;
    }
  return true;
}

void SimpleNetworkMap::setPierFlag(int x, int z, bool on) {
  if (on) piers.insert({x, z}); else piers.erase({x, z});
}

std::vector<PlacedPiece> SimpleNetworkMap::pieces(NetLayer layer) const {
  std::vector<PlacedPiece> v;
  for (auto& kv : layerPieces[layer]) v.push_back(kv.second);
  return v;
}

void SimpleNetworkMap::addForeign(int x, int z, uint32_t id, uint32_t f, bool removable) {
  TileOcc o; o.present = true; o.id = id; o.flags = f; o.removable = removable;
  foreign[{x, z}] = o;
}

// ---------------- slopes ----------------
bool isValidSlope(const int c[4]) {
  int d = c[2] - c[0];
  if (d < 0) d = -d;
  if (c[1] == c[0] && c[3] == c[2] && d <= 2) return true;
  if (c[2] == c[1] && c[3] == c[0] && d <= 2) return true;
  return false;
}

int slopePatternIndex(const int c[4]) {
  static const int pat[8][3] = {{-1, -1, 0}, {0, -1, -1}, {1, 1, 0}, {0, 1, 1},
                                {-2, -2, 0}, {0, -2, -2}, {2, 2, 0}, {0, 2, 2}};
  int d1 = c[1] - c[0], d2 = c[2] - c[0], d3 = c[3] - c[0];
  for (int i = 0; i < 8; i++)
    if (pat[i][0] == d1 && pat[i][1] == d2 && pat[i][2] == d3) return i;
  return -1;
}

struct PR { uint32_t id; int rot; };
static const PR kRoadSl[8] = {{11202, 0}, {11201, 0}, {11202, 2}, {11201, 2}, {32, 0}, {31, 0}, {32, 2}, {31, 2}};
static const PR kRailSl[8] = {{16008, 0}, {16009, 0}, {16008, 2}, {16009, 2}, {46, 0}, {47, 0}, {46, 2}, {47, 2}};
static const PR kSubwSl[8] = {{11616, 0}, {11617, 0}, {11616, 2}, {11617, 2}, {11614, 0}, {11615, 0}, {11614, 2}, {11615, 2}};
static const PR kPipeSl[8] = {{11650, 0}, {11649, 0}, {11650, 2}, {11649, 2}, {11648, 0}, {11647, 0}, {11648, 2}, {11647, 2}};
static const PR kWireSl[8] = {{11262, 0}, {11262, 1}, {11262, 2}, {11262, 3}, {11263, 0}, {11263, 1}, {11263, 2}, {11263, 3}};
static const PR kPylonSl[8] = {{18081, 0}, {18080, 0}, {18081, 2}, {18080, 2}, {18083, 0}, {18082, 0}, {18083, 2}, {18082, 2}};
static const PR kHwyA[8] = {{15102, 0}, {15105, 0}, {15104, 2}, {15103, 2}, {14094, 0}, {15101, 0}, {15100, 2}, {15099, 2}};
static const PR kHwyB[8] = {{15104, 0}, {15103, 0}, {15102, 2}, {15105, 2}, {15100, 0}, {15099, 0}, {14094, 2}, {15101, 2}};

bool slopePiece(int kind, int x, int z, int p, const SlopeCtx& ctx, Piece& out) {
  if (p < 0 || p > 7) return false;
  const PR* t = nullptr;
  auto inPath = [&](int px, int pz) { return ctx.path && ctx.path->count({px, pz}) != 0; };
  switch (kind) {
    case NK_ROAD: t = kRoadSl; break;
    case NK_RAIL: t = kRailSl; break;
    case NK_SUBWAY: t = kSubwSl; break;
    case NK_PIPE: t = kPipeSl; break;
    case NK_POWER: {
      bool both = (p % 2 == 0) ? (inPath(x - 1, z) && inPath(x + 1, z)) : (inPath(x, z - 1) && inPath(x, z + 1));
      bool pylon = ((x + z) % 5 == 0) || !both;
      t = pylon ? kPylonSl : kWireSl;
      break;
    }
    case NK_HIGHWAY: {
      bool a = (p % 2 == 0) ? inPath(x, z + 1) : inPath(x + 1, z);
      t = a ? kHwyA : kHwyB;
      break;
    }
    default: return false;
  }
  out = Piece{t[p].id, t[p].rot};
  return true;
}

// ---------------- crossings ----------------
const std::vector<uint32_t>& crossingList(int kind) {
  static const std::vector<uint32_t> road = {14, 15, 44, 45, 73, 92, 10001, 10036, 10037, 15051};
  static const std::vector<uint32_t> rail = {14, 15, 29, 30, 73, 92, 10001, 10036, 10037, 11204, 15051};
  static const std::vector<uint32_t> hwy = {14, 15, 29, 30, 44, 45, 92, 10001, 10036, 10037, 11204};
  static const std::vector<uint32_t> pwr = {29, 30, 44, 45, 73, 11204, 15051};
  static const std::vector<uint32_t> none;
  switch (kind) {
    case NK_ROAD: return road;
    case NK_RAIL: return rail;
    case NK_HIGHWAY: return hwy;
    case NK_POWER: return pwr;
  }
  return none;
}
bool isCrossableId(int kind, uint32_t id) {
  auto& l = crossingList(kind);
  return std::find(l.begin(), l.end(), id) != l.end();
}

int crossingClassOfId(uint32_t id) {
  switch (id) {
    case 29: case 11204: return 1;
    case 44: return 2;
    case 14: case 15: case 92: return 3;
    case 73: return 4;
    case 15051: return 5;
  }
  return 0;
}

bool crossingTable(int c, int r, Piece& out) {
  static const struct { int id, rot; } T[5][5] = {
      {{0, -1}, {69, 0}, {67, 0}, {11258, 2}, {15098, 2}},
      {{69, 1}, {0, -1}, {71, 0}, {16006, 2}, {16007, 2}},
      {{67, 1}, {71, 1}, {0, -1}, {15036, 2}, {15037, 2}},
      {{11258, 0}, {16006, 0}, {15036, 0}, {0, -1}, {0, -1}},
      {{15098, 0}, {16007, 0}, {15037, 0}, {0, -1}, {0, -1}}};
  if (c < 1 || c > 5 || r < 1 || r > 5) return false;
  auto& e = T[c - 1][r - 1];
  if (e.rot < 0) return false;
  out = Piece{uint32_t(e.id), e.rot};
  return true;
}

// ---------------- tiler ----------------
using Tile = std::pair<int, int>;

struct Tiler::Ctx {
  int kind = 0, table = 0;
  NetLayer layer = LAYER_SURFACE;
  const NetworkRules* nr = nullptr;
  TilerOptions opt;
  std::set<Tile> path;       // tiles counted as "being drawn"
  std::set<Tile> set;        // re-tile set
  std::set<Tile> finalQ;     // queued for the final pass
  std::set<Tile> protQ;      // power: protected tiles queued for the complex pass
  TilerResult res;
};

bool Tiler::isNetOcc(int kind, const TileOcc& o) const { return o.present && flagsMatchKind(kind, o.flags); }

void Tiler::note(Ctx& c, int x, int z, uint32_t id, int rot, int y) {
  TileChange ch; ch.x = x; ch.z = z; ch.layer = c.layer; ch.id = id; ch.rot = rot; ch.y = y;
  c.res.changes.push_back(ch);
}

bool Tiler::canOverwrite(const Ctx& c, int x, int z) const {
  TileOcc o = map_.occupantAt(x, z, c.layer);
  if (!o.present) return true;
  if (c.layer != LAYER_SURFACE) return isNetOcc(c.kind, o) && !c.nr->isProtected(o.id);
  if (isNetOcc(c.kind, o)) return !c.nr->isProtected(o.id);
  if (!o.removable) return false;
  if (o.flags & OF_IGNORABLE) return true;
  return (o.flags & OF_NETPIECE) && isCrossableId(c.kind, o.id) && !c.nr->isProtected(o.id);
}

bool Tiler::tryCrossing(Ctx& c, const PathTile& pt) {
  if (c.layer != LAYER_SURFACE) return false;
  TileOcc o = map_.occupantAt(pt.x, pt.z, LAYER_SURFACE);
  if (!o.present || !(o.flags & OF_NETPIECE) || isNetOcc(c.kind, o)) return false;
  if (!c.opt.isBridge && map_.isWaterTile(pt.x, pt.z)) return false;
  if (!o.removable || !isCrossableId(c.kind, o.id) || c.nr->isProtected(o.id)) return false;
  int E = crossingClassOfId(o.id);
  if (!E) return false;
  int N;
  switch (c.kind) {
    case NK_ROAD: N = 1; break;
    case NK_RAIL: N = 2; break;
    case NK_POWER: N = 3; break;
    case NK_HIGHWAY: N = c.opt.pairDz != 0 ? 5 : 4; break;
    default: return false;
  }
  bool useNE;  // true: T[N][E], false: T[E][N]
  if (c.kind == NK_HIGHWAY) {
    int ox = c.opt.pairDx, oy = c.opt.pairDz;
    if (pt.carriage == 0) useNE = !(ox < 0 || oy < 0);
    else useNE = !(ox > 0 || oy > 0);
  } else {
    useNE = o.rot != 0;
    if (o.id == 15 && o.rot == 0) useNE = true;
    if (o.id == 11204 && o.rot == 2) useNE = false;
  }
  Piece p;
  if (!(useNE ? crossingTable(N, E, p) : crossingTable(E, N, p))) return false;
  int y = map_.cornerHeight(pt.x, pt.z);
  map_.removePiece(pt.x, pt.z, LAYER_SURFACE);
  map_.placePiece(pt.x, pt.z, LAYER_SURFACE, p.id, p.rot, y);
  note(c, pt.x, pt.z, p.id, p.rot, y);
  c.res.crossings++;
  return true;
}

// Pass: 0 simple, 1 complex, 2 final.
void Tiler::describe(Ctx& c, int pass, int x, int z, std::vector<RuleCond>& entries, int& mask) {
  mask = 0;
  const int radius = pass == 1 ? 2 : 1;
  const int dirLimit = pass == 0 ? 8 : (pass == 2 ? 4 : 0);
  const ConvertTable& cv = pass == 1 ? c.nr->complexConvert : c.nr->convert;
  const int W = map_.width(), H = map_.height();
  auto edgeConn = [&](int ex, int ez) { return map_.hasConnection(c.kind, ex, ez); };

  // Offsets for all directions within radius (center = 31).
  auto visit = [&](int dir, int dx, int dz) {
    int nx = x + dx, nz = z + dz;
    bool inMap = map_.onMap(nx, nz);
    if (!inMap) {
      if (pass != 1) return;  // clipped
      RuleCond e; e.dir = dir; e.id = 0; e.rot = 5;
      // exactly one step outside on exactly one axis, side holds a connection
      int ex = std::min(std::max(nx, 0), W - 1), ez = std::min(std::max(nz, 0), H - 1);
      bool outX = nx < 0 || nx >= W, outZ = nz < 0 || nz >= H;
      bool one = (outX != outZ) && (outX ? (nx == -1 || nx == W) : (nz == -1 || nz == H));
      if (one && edgeConn(ex, ez)) e.rot = 1;
      entries.push_back(e);
      return;
    }
    TileOcc o = map_.occupantAt(nx, nz, c.layer);
    // Tiles awaiting re-tiling (the re-tile set) count as part of the path being drawn: they are
    // empty now but will hold network pieces, so neighbours must connect to them.
    bool onPath = c.path.count({nx, nz}) != 0 || c.set.count({nx, nz}) != 0;
    RuleCond e; e.dir = dir;
    bool bit = false;
    if (isNetOcc(c.kind, o)) {
      Piece p = cv.apply(Piece{o.id, o.rot});
      e.id = p.id; e.rot = p.rot;
      bit = true;
      if (pass == 2 && dir < 4 && (c.kind == NK_ROAD || c.kind == NK_HIGHWAY || c.kind == NK_RAIL || c.kind == NK_SUBWAY)) {
        bool uncond = c.kind == NK_SUBWAY && (o.flags & 0x40100) == 0x40100;
        if (!uncond) bit = (map_.exitMask(o.id, o.rot) >> ((dir + 2) & 3)) & 1;
      }
    } else if (o.present) {
      e.id = 0; e.rot = (o.flags & OF_IGNORABLE) ? 5 : 0;
      if (onPath) bit = true;
    } else {
      e.id = 0; e.rot = onPath ? 1 : 5;
      if (onPath) bit = true;
    }
    if (dir < dirLimit && bit) mask |= 1 << dir;
    entries.push_back(e);
  };

  visit(31, 0, 0);
  int maxDir = pass == 1 ? 24 : 8;
  for (int d = 0; d < maxDir; d++) {
    int dx, dz;
    dirOffset(d, dx, dz);
    visit(d, dx, dz);
  }
  if (pass != 1) {
    if (x == 0 && edgeConn(x, z)) mask |= 1;
    if (z == 0 && edgeConn(x, z)) mask |= 2;
    if (x == W - 1 && edgeConn(x, z)) mask |= 4;
    if (z == H - 1 && edgeConn(x, z)) mask |= 8;
    if (pass == 2) mask &= 0xF;
  }
  (void)radius;
}

bool Tiler::evalTile(Ctx& c, int pass, int x, int z) {
  if (!map_.onMap(x, z) || !map_.isFlat(x, z)) return false;
  std::vector<RuleCond> entries;
  int mask = 0;
  describe(c, pass, x, z, entries, mask);
  const RuleTable& tbl = pass == 0 ? c.nr->simple : (pass == 1 ? c.nr->complex : c.nr->final_);
  const Rule* hit = nullptr;
  for (auto& r : tbl.rules) {
    if (r.key != 256 && r.key != mask) continue;
    bool ok = true;
    for (auto& cd : r.conds) {
      int dir = cd.dir >= 24 ? 31 : cd.dir;
      bool found = false;
      for (auto& e : entries) {
        if (e.dir != dir || e.id != cd.id) continue;
        if (e.rot == cd.rot || (cd.rot == 0 && (e.rot == 0 || e.rot == 5))) { found = true; break; }
      }
      if (!found) { ok = false; break; }
    }
    if (ok) { hit = &r; break; }
  }
  if (!hit) return false;
  // validate
  for (auto& rs : hit->results) {
    int dx, dz;
    dirOffset(rs.dir, dx, dz);
    int tx = x + dx, tz = z + dz;
    if (!map_.onMap(tx, tz) || !map_.isFlat(tx, tz) || !canOverwrite(c, tx, tz)) return false;
  }
  for (auto& rs : hit->results) {
    int dx, dz;
    dirOffset(rs.dir, dx, dz);
    int tx = x + dx, tz = z + dz;
    if (rs.id == 0) {
      TileOcc o = map_.occupantAt(tx, tz, c.layer);
      if (isNetOcc(c.kind, o)) {
        map_.removePiece(tx, tz, c.layer);
        note(c, tx, tz, 0, 0, map_.cornerHeight(tx, tz));
      }
    } else {
      int y = map_.cornerHeight(tx, tz);
      map_.placePiece(tx, tz, c.layer, rs.id, rs.rot, y);
      note(c, tx, tz, rs.id, rs.rot, y);
    }
    if (rs.id == 0 && rs.rot == 0) c.finalQ.insert({tx, tz});
    else { c.set.erase({tx, tz}); c.finalQ.erase({tx, tz}); }
  }
  c.set.erase({x, z});
  return true;
}

TilerResult Tiler::run(int kind, const std::vector<PathTile>& path, const TilerOptions& opt) {
  Ctx c;
  int table = netKindToTable(kind);
  if (table < 0) return TilerResult();
  c.kind = kind; c.table = table; c.layer = layerOfKind(kind);
  c.nr = &rules_.nets[table];
  c.opt = opt;
  const int radius = (kind == NK_ROAD || kind == NK_HIGHWAY || kind == NK_SUBWAY) ? 5 : 2;

  std::set<Tile> pathSet;
  for (auto& p : path) if (map_.onMap(p.x, p.z)) pathSet.insert({p.x, p.z});
  if (!opt.demolition) {
    c.path = pathSet;
    for (auto& t : opt.contextPath) c.path.insert(t);
  }

  // 1. region
  for (auto& pt : path) {
    if (!map_.onMap(pt.x, pt.z)) continue;
    c.set.insert({pt.x, pt.z});
    std::set<Tile> seen;
    std::vector<Tile> stack{{pt.x, pt.z}};
    seen.insert({pt.x, pt.z});
    while (!stack.empty()) {
      Tile t = stack.back(); stack.pop_back();
      static const int dx[4] = {-1, 0, 1, 0}, dz[4] = {0, -1, 0, 1};
      for (int d = 0; d < 4; d++) {
        int nx = t.first + dx[d], nz = t.second + dz[d];
        if (!map_.onMap(nx, nz) || std::abs(nx - pt.x) > radius || std::abs(nz - pt.z) > radius) continue;
        if (seen.count({nx, nz})) continue;
        seen.insert({nx, nz});
        if (isNetOcc(kind, map_.occupantAt(nx, nz, c.layer))) {
          c.set.insert({nx, nz});
          stack.push_back({nx, nz});
        }
      }
    }
  }

  // 2. clearing
  {
    std::vector<Tile> snapshot(c.set.begin(), c.set.end());
    std::map<Tile, PathTile> pathTiles;
    for (auto& p : path) pathTiles[{p.x, p.z}] = p;
    for (auto& t : snapshot) {
      auto pit = pathTiles.find(t);
      if (pit != pathTiles.end() && !opt.demolition) {
        TileOcc o = map_.occupantAt(t.first, t.second, c.layer);
        if (c.layer == LAYER_SURFACE && o.present && (o.flags & OF_NETPIECE) && !isNetOcc(kind, o)) {
          if (tryCrossing(c, pit->second)) { c.set.erase(t); continue; }
        }
      }
      TileOcc o = map_.occupantAt(t.first, t.second, c.layer);
      if (isNetOcc(kind, o)) {
        if (c.nr->isProtected(o.id)) {
          c.set.erase(t);
          if (kind == NK_POWER) c.protQ.insert(t);
        } else {
          map_.removePiece(t.first, t.second, c.layer);
          note(c, t.first, t.second, 0, 0, map_.cornerHeight(t.first, t.second));
        }
      }
    }
  }
  if (opt.demolition) for (auto& t : pathSet) { c.set.erase(t); }

  // 3. slopes
  {
    SlopeCtx sc; sc.path = &pathSet;
    std::vector<Tile> snapshot(c.set.begin(), c.set.end());
    for (auto& t : snapshot) {
      if (map_.isFlat(t.first, t.second)) continue;
      int cs[4];
      map_.tileCorners(t.first, t.second, cs);
      if (!isValidSlope(cs)) continue;
      int p = slopePatternIndex(cs);
      Piece sp;
      if (p < 0 || !slopePiece(kind, t.first, t.second, p, sc, sp)) continue;
      if (!canOverwrite(c, t.first, t.second)) continue;
      map_.placePiece(t.first, t.second, c.layer, sp.id, sp.rot, cs[0]);
      note(c, t.first, t.second, sp.id, sp.rot, cs[0]);
      c.set.erase(t);
    }
  }

  // 4. simple pass
  for (;;) {
    size_t before = c.set.size();
    std::vector<Tile> snap(c.set.begin(), c.set.end());
    for (auto& t : snap) {
      if (!c.set.count(t)) continue;
      evalTile(c, 0, t.first, t.second);
    }
    c.res.simplePasses++;
    if (c.set.size() == before) break;
  }

  // 5. complex pass
  if (kind == NK_POWER) for (auto& t : c.protQ) c.set.insert(t);
  for (int it = 0; it < 5; it++) {
    bool changed = false;
    std::vector<Tile> snap(c.set.begin(), c.set.end());
    for (auto& t : snap) {
      if (!c.set.count(t)) continue;
      if (evalTile(c, 1, t.first, t.second)) changed = true;
    }
    c.res.complexPasses++;
    if (!changed) break;
  }

  // 6. final pass
  for (auto& t : c.finalQ) c.set.insert(t);
  {
    std::vector<Tile> snap(c.set.begin(), c.set.end());
    for (auto& t : snap) evalTile(c, 2, t.first, t.second);
  }
  return c.res;
}

}  // namespace m3k
