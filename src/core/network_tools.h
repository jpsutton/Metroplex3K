// Network placement tools, bridges/tunnels, ramps, neighbour connections, power/water topology,
// road condition and persisted network records. spec/12-networks.md sections 6-10 and Persisted
// state. No SDL; all world access goes through INetworkMap / ToolHost.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "core/bytes.h"
#include "core/tiler.h"
#include "core/world.h"

namespace m3k {

// ======================= 6.2.1 tool parameters =======================
enum Tool { TOOL_ROAD = 0, TOOL_HIGHWAY, TOOL_RAIL, TOOL_SUBWAY, TOOL_ONRAMP, TOOL_POWER, TOOL_PIPE, kNumTools };

struct ToolParams {
  Tool tool;
  uint32_t menuItemId;  // MenuItem.INI id
  int kind;             // NetKind
  uint32_t costKey;     // agent type id in [BuildingPlacementCosts]
  bool wide;
  int minRun;
  bool staircase;
  bool anySlope;
};
const ToolParams& toolParams(Tool t);

// ======================= 6.1 costs =======================
struct PlacementCosts {
  std::map<uint32_t, int> byType;        // [BuildingPlacementCosts]
  std::map<uint32_t, uint32_t> parent;   // agent type hierarchy for fallback
  int demolish = 0, demolishRubble = 0;  // [SpecialCosts]
  int levelTerrain = 0;                  // [SpecialCosts] LevelTerrain: charged once per endpoint levelling that changes terrain
  int cost(uint32_t agentType) const;    // nearest ancestor with an entry, else 0
};
// Neighbour-connection price keys (NetKind -> agent type).
uint32_t connectionCostKey(int kind);
// Bridge price keys.
constexpr uint32_t kBridgeCostRoadRailPower = 16002;
constexpr uint32_t kBridgeCostHighway = 16004;
constexpr uint32_t kTunnelCostKey = 16006;

// ======================= 6.2.3 path routing =======================
struct RoutedTile {
  int x = 0, z = 0;
  int carriage = 0;
  bool valid = true;
  bool gradeInvalid = false;   // invalidated by the strip grader (a tunnel may still carry it)
};
struct RoutedPath {
  std::vector<RoutedTile> tiles;
  int validPrefix = 0;     // tiles before the path became blocked
  bool blocked = false;
  bool truncated = false;  // 512-tile limit hit
  int pairDx = 0, pairDz = 0;
  bool crossed = false;    // at least one tile is a crossing
};
constexpr int kMaxPathTiles = 512;
RoutedPath routePath(const INetworkMap& m, Tool tool, std::pair<int, int> start, std::pair<int, int> end);
// Debug text of the strip grade planned for these tiles (vertex changes or the failure reason); non-highway kinds.
std::string describeStripGrade(const INetworkMap& m, const std::vector<std::pair<int, int>>& tiles);

// Placement / crossing filters of a tool (section 6.2.1; sets are the crossing lists, INFERRED).
bool placementAccepts(int kind, const TileOcc& o);
bool crossingAccepts(int kind, const TileOcc& o);

struct PathPrice {
  int price = 0;
  bool insufficient = false;
};
// 6.2.5: marks tiles after `buildablePrefix` invalid, price = valid tiles * tileCost, whole path
// invalid when price > funds.
// Ripple pass after a strip grade [STATIC FUN_100076E0; the clamp rule fits the hill road's five outer corners]: every vertex next to a changed vertex is
// clamped into [max - 4, min + 4] of its changed neighbours' heights, and a vertex that moves joins the changed set (cascade). Each moved vertex is charged
// LevelTerrain x |dh| >> 2 on its own. Returns the total charge; vertices with an occupant next to them are left alone.
int rippleRing(INetworkMap& m, const std::vector<std::pair<int, int>>& changed, int levelTerrain);
PathPrice previewPrice(RoutedPath& p, int tileCost, long funds, int buildablePrefix = -1);

std::vector<PathTile> toPathTiles(const RoutedPath& p, bool onlyValid = true);

// ======================= host callbacks =======================
struct ToolHost {
  virtual ~ToolHost() = default;
  virtual long funds() const = 0;
  virtual void charge(int amount) = 0;
  virtual bool confirmBridge(int price, bool power) { (void)price; (void)power; return true; }
  virtual bool confirmTunnel(int price) { (void)price; return true; }
  virtual bool confirmConnection(int kind, int price) { (void)kind; (void)price; return true; }
  virtual bool neighbourExists(int edge) const { (void)edge; return true; }
  // Sound / message notifications (event ids of spec 12 section 6.2.2 / 8).
  virtual void sound(int eventId) { (void)eventId; }
  virtual void message(const std::string& what) { (void)what; }
  // Tunnel portals / on-ramp lot are buildings; the host creates them.
  virtual bool buildTunnel(int kind, const struct TunnelPlan& plan) { (void)kind; (void)plan; return false; }
  virtual bool buildOnRamp(const struct OnRampPlan& plan) { (void)plan; return false; }
};

// ======================= 6.3 slopes / 6.4 bridges =======================
struct BridgePlan {
  bool ok = false;
  int kind = 0;
  int axis = 0;        // 0 x, 1 z
  int W = 0;           // span length
  int ax = 0, az = 0;  // unit step along axis
  int px = 0, pz = 0;  // perpendicular
  int x0 = 0, z0 = 0;  // W0
  int hw = 0;          // lowest water-tile corner height
  int m = 0, leftFill = 0, rightFill = 0;
  int price = 0;       // W * cost
  BridgeDesign design;
  std::vector<std::pair<int, int>> spanTiles;
  // Terrain corner changes (vertex coordinates).
  struct Corner { int vx, vz, h; int group = 0; };   // group 0 start bank, 1 river bed, 2 far bank
  std::vector<Corner> corners;
  bool terrainBlocked = false;  // some bank corner may not change ("construction discontinued")
  bool terrainDone = false;     // buildBridgeTerrain wrote the corners (applyBridge must not rewrite them)
  // Pieces to place (slope/straight stubs are filled in by applyBridge after terrain changes).
  std::vector<PlacedPiece> pieces;
  std::vector<std::pair<int, int>> stubTiles;  // W0-3a, W0-2a, E0+a, E0+2a
  std::vector<std::pair<int, int>> coveredTiles() const;
};
// carriagePass: 0 or 1 (highway only; selects design direction). The path is the tile list of
// that carriageway (or the whole path for other nets).
BridgePlan planBridge(const INetworkMap& m, const NetworkRules& nr, int kind, const std::vector<PathTile>& path,
                      const PlacementCosts& costs, int carriagePass = 0);
// Sets terrain and places pieces. Returns false (and changes nothing) when terrainBlocked.
bool applyBridge(INetworkMap& m, BridgePlan& plan);
// Staged terrain of a road/rail/power bridge [STATIC SIMNTWRK 0x1000122A, per the original's decode]: the start-bank corners are written at once, the start
// approach is graded, then the river bed, far bank and far approach. false = refused at a grade (what was written stays; no pieces, no charge).
bool buildBridgeTerrain(INetworkMap& m, BridgePlan& plan);
// Body occupant ids that set the per-tile pier flag (section 6.4 step 6).
bool isBridgePierId(uint32_t id);

struct TunnelPlan {
  bool ok = false;
  bool highway = false;
  int dx = 0, dz = 0;
  int pairDx = 0, pairDz = 0;               // second carriageway offset (highway); (0,0) for road/rail
  std::pair<int, int> entrance, exit;
  int hillTiles = 0;
  int dir = 0, length = 0;                  // portal distance L (entrance → exit)
  int spanLength = 0;                       // length written on span pieces (L road/rail; L+1 highway)
  std::vector<std::pair<int, int>> hidden;  // tiles strictly between portals (both carriageways for highway)
  int price = 0;
  struct Corner { int vx, vz, h; };
  std::vector<Corner> corners;              // road/rail shaping of the entrance and exit [STATIC 0x1001C102]
  bool blocked = false;                     // a shaped corner has an occupant next to it: message 0x284, nothing placed
  std::pair<int, int> continueTile{-1, -1}; // road/rail: the tile one past the exit that replaces the dragged end
};
TunnelPlan planTunnel(const INetworkMap& m, int kind, const std::vector<PathTile>& path, const PlacementCosts& costs);

// ======================= 7 on-ramps =======================
struct OnRampPlan {
  bool ok = false;
  bool diagonal = false;
  int anchorX = 0, anchorZ = 0;
  int orientation = 0;  // 0: footprint along z, 1: along x
  std::vector<std::pair<int, int>> footprint;  // 3 tiles
};
OnRampPlan planOnRamp(const INetworkMap& m, int ax, int az);

// ======================= 8 neighbour connections =======================
// Neighbour index of an edge tile or -1: x=0 -> 0 (except (0,H-1) -> 3), z=0 -> 1, x=W-1 -> 2, z=H-1 -> 3.
int edgeNeighbour(int x, int z, int W, int H);
// Sound event for a created connection (36 road/highway, 37 rail/subway, 46 power, 45 pipe).
int connectionSoundEvent(int kind);
// Dry-run of §8 offer eligibility (no confirm, charge, or map write).
struct ConnectionOfferPlan {
  bool ok = false;
  bool insufficientFunds = false;
  int kind = 0;
  int price = 0;
  int edge = -1;
  std::vector<std::pair<int, int>> tiles;
};
ConnectionOfferPlan planConnectionOffer(const INetworkMap& m, const ToolHost& host, const PlacementCosts& costs,
                                        int kind, const std::vector<PathTile>& path, int dragPrice);
// Offers/creates connections at drag ends (section 8). Returns number of connections added.
int offerConnections(INetworkMap& m, ToolHost& host, const PlacementCosts& costs, int kind,
                     const std::vector<PathTile>& path, int dragPrice);
// Removes every connection at a tile (kinds 1,4,2,6,5,3 order).
int removeConnectionsAt(INetworkMap& m, int x, int z);

// ======================= 6.2.6 commit =======================
enum CommitStatus { COMMIT_OK = 0, COMMIT_INVALID, COMMIT_NO_MONEY, COMMIT_CANCELLED };
struct CommitResult {
  CommitStatus status = COMMIT_OK;
  int spent = 0;
  int bridges = 0, tunnels = 0, connections = 0, rampPlaced = 0;
  TilerResult tiled;
};
// Release-time commit of a routed path. The on-ramp tool is a point tool: pass a one-tile path.
CommitResult commitPath(INetworkMap& m, const TilingRuleSet& rules, const PlacementCosts& costs, ToolHost& host,
                        Tool tool, const RoutedPath& path);

// 6.5 demolition: removes the pieces of `kind` at tiles (collapse groups too), connections, then re-tiles.
TilerResult demolish(INetworkMap& m, const TilingRuleSet& rules, int kind, const std::vector<std::pair<int, int>>& tiles);

// ======================= 9 power / water topology =======================
constexpr int kPowerFloodIterations = 600;
constexpr int kWaterFloodIterations = 500;
struct Rect { int x = 0, z = 0, w = 1, h = 1; };

// Bounded flood fill on W*H byte grids (index x*H+z). Returns the reached grid (1 = reached).
std::vector<uint8_t> boundedFloodFill(int W, int H, const std::vector<uint8_t>& conductor, int sx, int sz, int maxIter);
// Marks `r` expanded by `margin` (clamped) in grid.
void markRect(std::vector<uint8_t>& grid, int W, int H, const Rect& r, int margin, uint8_t value = 1);

struct PowerSource { int x = 0, z = 0; int supply = 0; };  // plant (capacity) or edge feed
struct PowerResult {
  std::vector<uint8_t> powered;  // 1 = powered, W*H
  std::vector<int> networkSupply;  // supply per flood network, in discovery order
};
PowerResult computePower(int W, int H, const std::vector<uint8_t>& conductor, const std::vector<uint8_t>& demand,
                         const std::vector<PowerSource>& plants, const std::vector<PowerSource>& feeds);

struct WaterNet {
  std::vector<uint8_t> reached, coverage;
  int output = 0;
  std::vector<int> sources;  // indices into the source list
};
// Dilation (Chebyshev) of a grid `steps` times (x then z each step).
std::vector<uint8_t> dilate(int W, int H, const std::vector<uint8_t>& g, int steps);
std::vector<WaterNet> computeWater(int W, int H, const std::vector<uint8_t>& pipeConductor,
                                   const std::vector<PowerSource>& sources, int waterFromPipe);
inline int effectiveWaterFromPipe(int v) { return v == 0 ? 3 : v; }

// ======================= 10 ageing: road condition =======================
struct PotholeConfig {
  std::vector<uint32_t> ordinary = {29};     // [OPEN] replaceable shapes
  std::vector<uint32_t> second;              // [OPEN] second replaceable shape (becomes 9051)
  std::vector<uint32_t> damagedOrdinary = {15040, 15041, 15042};
  uint32_t damagedSecond = 9051;
  uint32_t repairedOrdinary = 29;            // [OPEN]
  uint32_t repairedSecond = 0;               // [OPEN]
};
// Fraction of damaged road pieces over road tiles (0 when no road).
double roadCondition(const INetworkMap& m, const PotholeConfig& cfg = PotholeConfig());
// Sets the condition (v in 0..1). `rnd(n)` returns an integer in [0, n). Returns pieces changed.
int setRoadCondition(INetworkMap& m, double v, const std::function<int(int)>& rnd, const PotholeConfig& cfg = PotholeConfig());

// ======================= persisted records =======================
struct NetworkRecords {
  std::vector<NetPiece> layer[3];  // surface, pipe, subway
};
constexpr uint32_t kNetRecType = 0x206C6E7C, kNetRecGroup = 0x2147C2DD;
uint32_t packPlace(int x, int z, int y, int rot);
uint32_t makePieceWord(uint32_t id);  // id | 0x30000000
// Parses instance 0..3 records out of the inner database. Throws FormatError for a bad header.
// Absent / version-mismatched header: throws; absent lists with count 0 are fine.
NetworkRecords parseNetworkRecords(const InnerDb& db);
// Lower-level: parse header (counts) and a list payload.
bool parseNetworkHeader(const uint8_t* p, size_t n, uint32_t counts[3]);
std::vector<NetPiece> parseNetworkList(const uint8_t* p, size_t n, uint32_t count);
// Records to write: (key, payload). Lists with zero entries are omitted.
std::vector<std::pair<ResKey, Bytes>> serializeNetworkRecords(const NetworkRecords& r);

}  // namespace m3k
