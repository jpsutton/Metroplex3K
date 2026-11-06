// Network tiler (spec/12-networks.md sections 4.6, 5, 6.3). Operates on the abstract
// INetworkMap so it can be tested with synthetic rule text and maps.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>
#include "core/tiling_rules.h"

namespace m3k {

// Occupant flag bits (12 §5.2; run-time word of 13 §3.1a).
enum OccFlags : uint32_t {
  OF_IGNORABLE = 0x400,
  OF_NETPIECE = 0x800,
  OF_TRANSPORT = 0x4000,  // transport/network-occupant bit (not pipes / plain power); 18 §6.1 fare gate
  OF_ROAD = 0x8000,
  OF_HIGHWAY = 0x10000,
  OF_RAIL = 0x20000,
  OF_SUBWAY = 0x40000,
  OF_POWER = 0x400000,
  OF_PIPE = 0x800000,
};

// Spec 13 §3.1a class flag words for non-network Occupant instances [STATIC].
constexpr uint32_t kOccFlagGenericBuilding = 0x104;     // zoned / ordinary / other building classes (Q-BC)
constexpr uint32_t kOccFlagFlora = 0x404;
constexpr uint32_t kOccFlagPowerPlant = 0x400104;
constexpr uint32_t kOccFlagWaterFacility = 0x800104;
constexpr uint32_t kOccFlagTransitStation = 0x4104;
constexpr uint32_t kOccFlagLandmarkDeal = 0x40000104;   // a few landmark/deal classes
constexpr uint32_t kOccFlagAutomata = 0x100A204;

// Run-time occupant flag word for a building/prop Occupant by agent type (spec 13 §3.1a).
// Generic (zoned and ordinary) buildings are a constant 0x104 — no per-agent-type table (Q-BC).
uint32_t runtimeOccupantFlags(uint32_t agentType);

// Spec 13 Occupant kind / 17 §8.4 [STATIC Q-BH / Q-BR]: fixed per-class small integer (not lot state, not 0x72).
// Buildings/facilities = 1; network tile pieces = 2; flora = 5. Other non-1 values exist on some classes.
constexpr int kOccKindBuilding = 1;
constexpr int kOccKindNetwork = 2;
constexpr int kOccKindFlora = 5;
int occupantKind(uint32_t agentType);
int occupantKindFromFlags(uint32_t flags);

enum NetLayer { LAYER_SURFACE = 0, LAYER_PIPE = 1, LAYER_SUBWAY = 2 };
constexpr int kNumLayers = 3;
NetLayer layerOfKind(int kind);

// Flag test of section 5.2 for NetKind `kind`: does an occupant with `flags` belong to the net?
bool flagsMatchKind(int kind, uint32_t flags);
// Default flags for pieces of a kind (used to synthesise flags from Set files).
uint32_t defaultFlagsForKind(int kind);

struct TileOcc {
  bool present = false;
  uint32_t id = 0;
  int rot = 0;
  uint32_t flags = 0;
  bool removable = true;
  bool fixed = false;   // an occupant that is not a piece (a subway station in the subway layer): never overwritten
};

struct PlacedPiece {
  int x = 0, z = 0, y = 0, rot = 0;
  uint32_t id = 0;
};

// Abstract map the tiler and the tools work on. Tile coordinates (x, z); vertex coordinates
// (vx, vz) in 0..W, 0..H. Altitudes in terrain height units.
class INetworkMap {
 public:
  virtual ~INetworkMap() = default;
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual int cornerHeight(int vx, int vz) const = 0;
  virtual void setCornerHeight(int vx, int vz, int h) = 0;
  virtual bool isWaterTile(int x, int z) const = 0;
  // Surface layer returns the network piece or any other occupant on the tile. The pipe and
  // subway layers return only that layer's piece.
  virtual TileOcc occupantAt(int x, int z, NetLayer layer) const = 0;
  // Replaces whatever network piece or crossable occupant is in `layer` at (x,z).
  virtual void placePiece(int x, int z, NetLayer layer, uint32_t id, int rot, int altitude) = 0;
  virtual void removePiece(int x, int z, NetLayer layer) = 0;
  // Neighbour connection of kind (NetKind numbering) at an edge tile.
  virtual bool hasConnection(int kind, int x, int z) const = 0;
  virtual void setConnection(int kind, int x, int z, bool on) = 0;
  // Connection-side mask of a piece (bit0 west/-x, 1 north/-z, 2 east/+x, 3 south/+z) after
  // rotation. Default: exposes everything.
  // `kind` is the NetKind being tiled: it picks the matrix (0x136 road/highway, 0x137 rail, 0x138 subway), since crossings belong to several sets.
  virtual unsigned exitMask(uint32_t id, int rot, int kind) const { (void)id; (void)rot; (void)kind; return 0xF; }
  // May a terrain corner change? (no building / network piece on any of the up to 4 tiles)
  virtual bool canChangeCorner(int vx, int vz) const { (void)vx; (void)vz; return true; }
  // Network endpoint levelling (spec 12 6.2.6 step 3): level the tile rectangle to the average of its perimeter corners with the level
  // tool's re-sloping; false = not possible (the network is built anyway).
  virtual bool levelRectToAverage(int x0, int z0, int x1, int z1) { (void)x0; (void)z0; (void)x1; (void)z1; return false; }
  virtual void setPierFlag(int x, int z, bool on) { (void)x; (void)z; (void)on; }
  // Span record of the piece at (x,z) (spec 10 s6.2.6 piece-word bits 16-26): dir 0 -x, 1 -z, 2 +x, 3 +z; length in tiles.
  virtual void setPieceSpan(int x, int z, NetLayer layer, int dir, int len) { (void)x; (void)z; (void)layer; (void)dir; (void)len; }
  // All pieces of a layer in map order (x ascending, then z).
  virtual std::vector<PlacedPiece> pieces(NetLayer layer) const = 0;

  bool onMap(int x, int z) const { return x >= 0 && z >= 0 && x < width() && z < height(); }
  // Corner heights c0..c3 of a tile (section 6.3 order).
  void tileCorners(int x, int z, int c[4]) const;
  bool isFlat(int x, int z) const;
};

// Straightforward concrete map used by tests and as a scratch copy for adapters.
class SimpleNetworkMap : public INetworkMap {
 public:
  SimpleNetworkMap(int w, int h, int baseHeight = 30);
  int width() const override { return W; }
  int height() const override { return H; }
  int cornerHeight(int vx, int vz) const override { return alt[size_t(vx) * (H + 1) + vz]; }
  void setCornerHeight(int vx, int vz, int hgt) override { alt[size_t(vx) * (H + 1) + vz] = hgt; }
  bool isWaterTile(int x, int z) const override { return water.count({x, z}) != 0; }
  TileOcc occupantAt(int x, int z, NetLayer layer) const override;
  void placePiece(int x, int z, NetLayer layer, uint32_t id, int rot, int altitude) override;
  void removePiece(int x, int z, NetLayer layer) override;
  bool hasConnection(int kind, int x, int z) const override { return conns.count({kind, x, z}) != 0; }
  void setConnection(int kind, int x, int z, bool on) override;
  unsigned exitMask(uint32_t id, int rot, int kind) const override;
  bool canChangeCorner(int vx, int vz) const override;
  void setPierFlag(int x, int z, bool on) override;
  std::vector<PlacedPiece> pieces(NetLayer layer) const override;

  // Test helpers.
  void setFlags(uint32_t id, uint32_t f) { flags[id] = f; }
  uint32_t flagsOf(uint32_t id) const;
  void addForeign(int x, int z, uint32_t id, uint32_t f, bool removable = true);
  void setWater(int x, int z, bool on) { if (on) water.insert({x, z}); else water.erase({x, z}); }
  void setExits(uint32_t id, unsigned maskRot0) { exits[id] = maskRot0; }

  int W, H;
  std::vector<int> alt;
  std::set<std::pair<int, int>> water;
  std::map<std::pair<int, int>, PlacedPiece> layerPieces[kNumLayers];
  std::map<std::pair<int, int>, TileOcc> foreign;
  std::set<std::tuple<int, int, int>> conns;
  std::set<std::pair<int, int>> piers;
  std::map<uint32_t, uint32_t> flags;
  std::map<uint32_t, unsigned> exits;
};

// ---- slopes (section 6.3) ----
bool isValidSlope(const int c[4]);
// Pattern index p (0..7) or -1.
int slopePatternIndex(const int c[4]);
struct SlopeCtx {
  const std::set<std::pair<int, int>>* path = nullptr;  // drawn path tiles (power/highway choice)
};
// Slope piece for a net kind at tile (x,z); false when the tile is not a matchable slope.
bool slopePiece(int kind, int x, int z, int p, const SlopeCtx& ctx, Piece& out);

// ---- crossings (section 5.3) ----
const std::vector<uint32_t>& crossingList(int kind);
bool isCrossableId(int kind, uint32_t id);
// Class of an existing occupant for the crossing handler (1 road, 2 rail, 3 power, 4/5 highway, 0 none).
int crossingClassOfId(uint32_t id);
// Combined piece T[c][r] (c, r = 1..5); returns false for "-".
bool crossingTable(int c, int r, Piece& out);

struct PathTile {
  int x = 0, z = 0;
  int carriage = 0;  // highway: 0 first carriageway, 1 second
};

struct TilerOptions {
  bool demolition = false;  // path tiles are cleared but not treated as "being drawn"
  bool isBridge = false;    // allows crossing handler on water
  int pairDx = 0, pairDz = 0;  // highway carriageway pairing offset (section 6.2.4)
  // Extra tiles treated as "part of the path being drawn" for neighbour descriptions but not tiled
  // (e.g. bridge pieces placed separately).
  std::vector<std::pair<int, int>> contextPath;
};

struct TileChange {
  int x = 0, z = 0;
  NetLayer layer = LAYER_SURFACE;
  uint32_t id = 0;  // 0 = removed
  int rot = 0;
  int y = 0;
};

struct TilerResult {
  std::vector<TileChange> changes;
  int simplePasses = 0, complexPasses = 0;
  int crossings = 0;
};

class Tiler {
 public:
  Tiler(const TilingRuleSet& rules, INetworkMap& map) : rules_(rules), map_(map) {}
  // kind: NetKind 1..6.
  TilerResult run(int kind, const std::vector<PathTile>& path, const TilerOptions& opt = TilerOptions());

 private:
  struct Ctx;
  bool tryCrossing(Ctx& c, const PathTile& pt);
  bool evalTile(Ctx& c, int pass, int x, int z, std::vector<std::pair<int, int>>& targets);
  void describe(Ctx& c, int pass, int x, int z, std::vector<RuleCond>& entries, int& mask);
  bool isNetOcc(int kind, const TileOcc& o) const;
  bool canOverwrite(const Ctx& c, int x, int z) const;
  void note(Ctx& c, int x, int z, uint32_t id, int rot, int y);

  const TilingRuleSet& rules_;
  INetworkMap& map_;
};

}  // namespace m3k
