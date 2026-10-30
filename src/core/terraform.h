// Terrain editing tools: raise, lower, level, decorative water, cheat shifts.
// Implements spec/11-world-terrain.md section 4 (plan/commit phases, ring, slope propagation).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "core/ini.h"
#include "core/world.h"

namespace m3k {

// Operation codes passed to validators (spec 11 4.2). Codes 8 and 16 exist but no tool here uses them.
enum TerraOp : int { kTerraRaise = 1, kTerraLower = 2, kTerraLevel = 4, kTerraWater = 32 };

// Inclusive rectangle (tile or vertex units, stated per use). Empty when x1 < x0.
struct IRect {
  int x0 = 0, z0 = 0, x1 = -1, z1 = -1;
  bool empty() const { return x1 < x0 || z1 < z0; }
};

// [SpecialCosts] prices (money per altitude unit per vertex; every cost is floor(price*units/4)).
struct TerraformCosts {
  int raise = 25, lower = 25, level = 25, water = 150;
  static TerraformCosts fromIni(const IniFile& budgetLayer);  // SC3BudgetLayer.ini
};

enum class TerraReason { Ok, OutOfMap, RingTooLarge, StepTooSteep, AlreadyWet, Occupied, ValidatorRefused };

struct VertexChange { int x, z; uint8_t value; };

struct TerraPlan {
  int op = 0;
  bool legal = false;
  TerraReason reason = TerraReason::Ok;
  int64_t cost = 0;                      // terrain cost C_t (without clearing price)
  IRect affected;                        // tile rect R (4.4 step 5; water: 3x3 block, clipped)
  IRect footprint;                       // tile rect of the tool footprint
  std::vector<VertexChange> altChanges;  // new altitudes (propagation + footprint), unique vertices
  std::vector<VertexChange> waterChanges;  // new water surface values
  IRect derivedVerts;                    // vertex rect whose derived data must be recomputed
  IRect notifyTiles;                     // tile rect to announce as changed
};

// Result of commit(): what the rest of the game must refresh.
struct TerraCommit {
  IRect derivedVerts;  // vertex rect: recompute slopes/textures/shore flags
  IRect notifyTiles;   // tile rect: networks, occupants and display update
};

// Clearing/bulldoze service hooks (rules owned by the demolition subsystem).
struct ClearQuote { bool possible = true; int64_t cost = 0; };
using ClearQuoteFn = std::function<ClearQuote(const IRect& tiles)>;
using ClearDoFn = std::function<void(const IRect& tiles)>;
// Validator: return true to allow, false to refuse (spec 4.7). Args: affected tile rect, cost, op.
using TerraValidator = std::function<bool(const IRect&, int64_t, int)>;
// Occupant query for water placement: true if tile holds an occupant that blocks (not removable).
using TileBlockedFn = std::function<bool(int x, int z)>;

enum class ApplyStatus { Applied, NotAllowed /*sound 30*/, NoMoney /*sound 32*/ };
struct ApplyResult { ApplyStatus status; int64_t total = 0; TerraCommit commit; };

// Tool sound IDs (spec 4.1).
constexpr int kSoundRaise = 18, kSoundLower = 19, kSoundWater = 20, kSoundNotAllowed = 30, kSoundNoMoney = 32;

class Terraformer {
 public:
  explicit Terraformer(World& w) : w_(w) {}
  void setCosts(const TerraformCosts& c) { costs_ = c; }
  const TerraformCosts& costs() const { return costs_; }
  void setClearing(ClearQuoteFn quote, ClearDoFn doClear) { quote_ = std::move(quote); clear_ = std::move(doClear); }
  void setTileBlocked(TileBlockedFn f) { blocked_ = std::move(f); }
  int addValidator(TerraValidator v);   // returns id
  void removeValidator(int id);

  // Plan phase: no writes to the world.
  TerraPlan planRaise(int tx, int tz) const;
  TerraPlan planLower(int tx, int tz) const;
  // Level: rectangle spanned by drag start tile and current tile; target = altitude of start tile's low corner.
  TerraPlan planLevel(int startX, int startZ, int curX, int curZ) const;
  TerraPlan planWater(int tx, int tz) const;

  // Commit phase: writes the plan's changes. Returns refresh rectangles.
  TerraCommit commit(const TerraPlan& p);

  // Clearing price of the plan's affected rect plus terrain cost (spec 4.2 steps 3-4).
  int64_t totalCost(const TerraPlan& p, bool* clearPossible = nullptr) const;
  // Full apply (spec 4.2 step 6): illegal -> NotAllowed, unaffordable -> NoMoney, else clear,
  // commit and deduct total from `funds`.
  ApplyResult apply(const TerraPlan& p, int64_t& funds);

  // Hover preview: tiles adjacent to any vertex the plan would change.
  std::vector<std::pair<int, int>> changedTiles(const TerraPlan& p) const;

 private:
  TerraPlan planRaiseLower(int op, int tx, int tz) const;
  void finish(TerraPlan& p) const;
  World& w_;
  TerraformCosts costs_;
  ClearQuoteFn quote_;
  ClearDoFn clear_;
  TileBlockedFn blocked_;
  std::vector<std::pair<int, TerraValidator>> validators_;
  int nextId_ = 1;
};

// Validator for an active disaster at fixed-point position (px, pz) (256 units per tile):
// refuses ops 1,2,4,8,16 (not water) when R is within 5 tiles (1280 units) on both axes.
TerraValidator makeDisasterValidator(int px, int pz);

// Cheat commands (spec 4.8). Matching is case-insensitive.
enum class TerrainCheatResult { Unknown, Applied, NeedsLoad /*"Load terrain": caller loads*/ };
// "Terrain one up/down", "Terrain ten up/down" shift every vertex altitude (clamped 0..255);
// "Salt on"/"Salt off" set *salt (World has no salt field). Water surface is left untouched.
// `dirty` (optional) gets the whole-map vertex rect when terrain changed.
TerrainCheatResult applyTerrainCheat(World& w, const std::string& command, bool* salt, IRect* dirty = nullptr);

}  // namespace m3k
