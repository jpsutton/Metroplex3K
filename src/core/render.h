// Software isometric renderer. Output is an RGBA8 framebuffer (u32 = A<<24|B<<16|G<<8|R); the app
// blits it with SDL. Follows spec/25-rendering.md: terrain (section 6), sprites/draw order
// (sections 4-5), overlays (section 9), picking (section 3).
#pragma once
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "core/font.h"
#include "core/gamedata.h"
#include "core/image.h"
#include "core/frame_backend.h"
#include "core/sprite_script.h"
#include "core/world.h"

namespace m3k {

struct Camera {
  float u = 64, v = 64;  // view-space vertex coordinate at the screen centre (fractional)
  int zoom = 2;          // 0..4
  int rot = 0;           // 0..3
};

// ---- Data-map overlays (spec 25 section 9.2, 09 section 7.2) -------------------------------
enum class DataLayer {
  Aura, Zones, Crime, Density, Power, Fire, LandValue, Pollution, Traffic, Water
};

// Per-tile solid-colour overlay. Each world tile (x*H+z) carries a palette row; 0xFF = no overlay.
// The vertex colour is T_slot[row][shade/2] (no noise, replaces terrain colour).
struct DataOverlay {
  int slot = 3;  // 0 = palBasic (expanded), 1..3 = data1Pal..data3Pal
  std::vector<uint8_t> row;  // W*H
};
// Default mapping (spec 25 9.2): continuous layers use row = 31 - value/8 on data2Pal (Crime, Fire,
// Pollution, Traffic) or data1Pal (Aura, Density, LandValue);
// categorical layers (Zones, Power, Water) use `value` as category index, row = 4*category, on
// data1Pal. Value 0xFF in `values` = no overlay for that tile on categorical layers.
DataOverlay makeDataOverlay(DataLayer layer, const std::vector<uint8_t>& values, int W, int H);
// Zones layer straight from World::zone (R=0, C=1, I=2, other non-zero=3, none = no overlay).
DataOverlay makeZoneOverlay(const World& w);

// ---- Scene input (extra sprites, previews) --------------------------------------------------
struct SceneSprite {
  // Either a direct image (category + spriteId) or a sprite-instance record (useAppearance).
  uint32_t category = 0, spriteId = 0;
  bool useAppearance = false;
  ResKey appearance;     // T_CSATTRIB record key
  int orient = 0;        // orientation slot (record frame index o)
  int animFrame = 0;     // animation frame a
  // World sub-tile position, 256 units per tile (spec 25 section 2.2); height in 1/256 altitude units.
  int32_t x = 0, z = 0, height = 0;
  bool magnify2x = false;       // draw image doubled (nearest neighbour); span doubled
  // Optional alpha plane (5-bit opacity levels, spec 04 section 3.5).
  bool hasAlpha = false;
  uint32_t alphaCategory = 0xFFFFFFFFu;  // 0xFFFFFFFF = same category as the colour image
  uint32_t alphaId = 0;
  int globalLevel = 32;  // m2: effective level = (mask * m2) >> 5; 32 = mask unchanged
  // Depth ordering. moving=true: ordered against static boxes (spec 25 section 5.2);
  // moving=false: ordered like a static object with footprint `footprint` anchored at the tile.
  bool moving = true;
  int footprint = 1;
  bool inset = false;
};

// Translucent tile tint over a world-tile rectangle (inclusive): selection or placement preview.
struct TilePreview {
  int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
  bool valid = true;     // cyan when valid, red when not (spec 25 9.4 [OBSERVED])
  uint8_t alpha = 140;   // 0..255
  bool customColour = false;
  uint8_t r = 0, g = 0, b = 0;
};

// Per-pixel sprite tint (spec 25 9.4, query hover / occupied target): every static or ground-level sprite whose footprint
// overlaps the world-tile rectangle (inclusive) has its opaque pixels blended 50/50 with the colour. `ground` also tints the
// flat terrain tiles of the rectangle (drawn under the sprites).
struct SpriteTint {
  int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
  uint8_t r = 0, g = 0, b = 0;
  bool ground = false;
};

struct CostLabel {
  bool show = false;
  int tileX = 0, tileZ = 0;
  long cost = 0;
  bool affordable = true;
};

struct RenderScene {
  std::vector<SceneSprite> sprites;
  std::vector<TilePreview> previews;
  std::vector<SpriteTint> tints;
  CostLabel cost;
};

// Length of one animation tick in milliseconds (RenderOptions::animTick counts these; ~15 Hz).
constexpr int kAnimTickMs = 67;

struct RenderOptions {
  bool grid = false;
  bool objects = true;
  bool terrain = true;
  bool underground = false;   // palUnder + pipes/subway pieces, surface objects hidden
  bool zonePaint = true;      // legacy zone/road tint when no data overlay is active
  bool background = true;     // BACK<z>.BMP off-map tiling
  bool edgeFaces = true;      // map-edge slab faces (spec 6.5)
  bool exact5bit = false;     // quantise blended sprite pixels to 5-bit channels like the original
  bool constructionSites = true;
  const DataOverlay* overlay = nullptr;
  // Per-zoom "draw own image" flags (spec 25 section 7): masks 0x10,0x20,0x40,0x80,0x100 for z=0..4.
  // Default draws the object's own image at every zoom. A clear bit draws the registered
  // substitute (setSubstituteSprite) by footprint size if one exists.
  uint16_t ownImageFlags = 0x1F0;
  int animTick = 0;           // animation clock in kAnimTickMs ticks (the script clock); < 0 disables building animations
  int dateMD = 0;             // game date as month*100+day (date windows in sprite scripts)
  // View City Layers filters (spec 09 7.2). Null predicates show everything.
  std::function<bool(const NetPiece&)> showNet;        // surface network pieces (roads, rail, power lines)
  std::function<bool(const Occupant&)> showOccupant;   // buildings
  bool flora = true;
  bool pipes = true, subways = true;                    // pieces drawn by the underground view
  bool automata = true;                                 // scene sprites (vehicles, pedestrians)
  // Host bumps this whenever showNet / showOccupant change meaning (std::function cannot be hashed); part of the
  // item list cache key.
  uint32_t filterEpoch = 0;
};

// The nine terrain palette slots (spec 04 section 7.1), stored as top-down tables.
struct TerrainPalettes {
  Image basic;   // 32 columns x N rows after expansion (palBasic)
  Image data[3]; // data1Pal..data3Pal, 64 x 32
  Image land;    // 32 x 512
  Image under;   // 32 x 512
  Image water;   // 1 x 256
  Image edgeL, edgeD;  // 1 x 512
  bool fromGame = false;
};
// palBasic expansion (spec 6.1): each raster pixel -> 32 columns, column k = colour * cos((31-k)*pi/62).
Image expandPalBasic(const Image& raw);

// Far-zoom colour adjustment (spec 6.2), z < 3 only. Channels are R, G, B.
void farZoomAdjust(int zoom, int& r, int& g, int& b);
// 5-bit blend (spec 04 section 3.5): floor((32*d + a*(s-d)) / 32), clamped to 31.
int blend5(int s, int d, int alpha);
// Same arithmetic on 8-bit channels (framebuffer is RGBA8).
int blend8(int s, int d, int alpha);
// Vertex slope shade per spec 25 section 6.6; (W+1)*(H+1) bytes indexed like World::vIdx.
std::vector<uint8_t> computeVertexShade(const World& w);

struct PickResult {
  bool hit = false;
  int tileX = 0, tileZ = 0;      // world tile
  int32_t subX = 0, subZ = 0;    // tile*256
  int32_t height = 0;            // altitude*256
  bool object = false;           // hit a sprite object (else ground)
  uint32_t category = 0, spriteId = 0;
};

// Host/game surface for the city view. Renderer is the software implementation; Phase 2 may wrap it.
class ICityRasterizer {
 public:
  virtual ~ICityRasterizer() = default;
  virtual void setWorld(const World* w, GameData* data) = 0;
  virtual void resize(int w, int h) = 0;
  virtual void render(const Camera& cam, const RenderOptions& opt, const RenderScene* scene = nullptr) = 0;
  virtual const std::vector<uint32_t>& pixels() const = 0;
  virtual void swapPixels(std::vector<uint32_t>& out) = 0;
  virtual void blitTo(uint32_t* dst, int pitchPx) const = 0;
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual void screenToView(const Camera& cam, int sx, int sy, float& u, float& v) const = 0;
  virtual void projectSub(const Camera& cam, int32_t x, int32_t z, int32_t height, int& sx, int& sy) = 0;
  virtual bool pick(const Camera& cam, int sx, int sy, PickResult& out) = 0;
  virtual bool pickTile(const Camera& cam, int sx, int sy, int& tx, int& tz) = 0;
  virtual bool pickGround(const Camera& cam, int sx, int sy, int& tx, int& tz) = 0;
  virtual void centreOnWorld(Camera& cam, float x, float z) const = 0;
  virtual void invalidateShade() = 0;
  virtual void setBandCount(int n) = 0;
  using TextHook = std::function<void(int sx, int sy, const std::string& text, bool affordable)>;
  virtual void setCostTextHook(TextHook h) = 0;
  virtual void setCostFont(const Font* f) = 0;
};

// Triangle record for the Phase 2 compute terrain fill (CPU setup → GPU raster). Layout must match the GLSL SSBO.
struct GpuTerrainTri {
  float p0[5], p1[5], p2[5];   // x,y,r,g,b per vertex
  int32_t seed, ytop, mode;    // mode: 0 gouraud-rgb, 1 gouraud-mono, 2 gouraud-none, 3 shore
  int32_t key[3];              // shore colour-key
  int32_t rep[3];              // shore water replacement
};

// Sprite instance a flora cell draws (the choice between two look-alikes is a per-tile hash); 0 = none.
uint32_t floraSpriteInstance(uint8_t cell, int x, int z);

class Renderer : public ICityRasterizer {
 public:
  // `data` may be null (no game files): procedural fallback palettes are used and no sprites drawn.
  void setWorld(const World* w, GameData* data) override;
  void resize(int w, int h) override;
  void render(const Camera& cam, const RenderOptions& opt, const RenderScene* scene = nullptr) override;

  const std::vector<uint32_t>& pixels() const override { return fb_; }
  // Hand the rendered frame to `out` without copying; the renderer takes `out`'s old storage (render()
  // clears it). Afterwards pixels() holds stale data until the next render().
  void swapPixels(std::vector<uint32_t>& out) override {
    if (out.size() != fb_.size()) out.assign(fb_.size(), 0);
    fb_.swap(out);
  }
  // Copy the rendered frame into a (possibly pitched) destination. Used when the host locks a streaming
  // texture and wants the city view written straight into GPU staging memory.
  void blitTo(uint32_t* dst, int pitchPx) const override {
    if (!dst || fbW_ <= 0 || fbH_ <= 0) return;
    if (pitchPx <= 0) pitchPx = fbW_;
    const int rowBytes = fbW_ * int(sizeof(uint32_t));
    if (pitchPx == fbW_) {
      std::memcpy(dst, fb_.data(), size_t(rowBytes) * size_t(fbH_));
    } else {
      for (int y = 0; y < fbH_; y++)
        std::memcpy(dst + size_t(y) * pitchPx, fb_.data() + size_t(y) * fbW_, size_t(rowBytes));
    }
  }
  int width() const override { return fbW_; }
  int height() const override { return fbH_; }

  // Phase 2: host-registered GPU terrain fill. Receives collected GpuTerrainTri[]; writes into fb.
  // Return false to fall back to the CPU band path for this frame.
  using GpuTerrainFn = std::function<bool(uint32_t* fb, int w, int h, int zoom,
                                          const int8_t* noiseLand, const int8_t* noiseEdge,
                                          const GpuTerrainTri* tris, int triCount)>;
  void setGpuTerrainFn(GpuTerrainFn fn) { gpuTerrainFn_ = std::move(fn); }
  void setGpuTerrainWanted(bool on) { gpuTerrainWanted_ = on; }

  // Coordinate helpers (view space <-> world space <-> screen).
  static void viewToWorldVertex(int rot, int W, int H, int u, int v, int& x, int& z);
  static void worldToViewVertex(int rot, int W, int H, int x, int z, int& u, int& v);
  // Sub-tile (256/tile) world -> view, spec 25 section 2.2.
  static void worldToViewSub(int rot, int W, int H, int32_t x, int32_t z, int32_t& a, int32_t& b);
  static void viewToWorldSub(int rot, int W, int H, int32_t a, int32_t b, int32_t& x, int32_t& z);
  void screenToView(const Camera& cam, int sx, int sy, float& u, float& v) const override;
  // Projected screen position of a world sub-tile point (spec 2.3, integer truncation).
  void projectSub(const Camera& cam, int32_t x, int32_t z, int32_t height, int& sx, int& sy) override;

  // Spec section 3 picking: sprite objects by per-pixel opacity, otherwise terrain polygons;
  // nearest depth key wins. Returns false if nothing/off-map.
  bool pick(const Camera& cam, int sx, int sy, PickResult& out) override;
  bool pickTile(const Camera& cam, int sx, int sy, int& tx, int& tz) override;
  // Terrain only (ignores objects): tile whose raised polygon contains the point.
  bool pickGround(const Camera& cam, int sx, int sy, int& tx, int& tz) override;
  void centreOnWorld(Camera& cam, float x, float z) const override;

  void invalidateShade() override { shadeValid_ = false; }
  // Number of horizontal bands render() rasterises in parallel; 0 = automatic (by height and pool size).
  void setBandCount(int n) override { bands_ = n; }
  // Where the last render() spent its time. setup/bandsWall/tail are wall-clock; the *Cpu values are summed over
  // all bands (thread time), so they exceed bandsWall when the bands ran in parallel.
  struct StageTimes {
    double setupMs = 0;      // serial: background load, prepareTerrain, buildItems
    double bandsWallMs = 0;  // wall time of the parallel band pass
    double bgCpuMs = 0, terrainCpuMs = 0, itemsCpuMs = 0;   // background, terrain (+grid/edges), sprites
    double tailMs = 0;       // serial: fallback boxes, previews, cost label
    double layerKeyMs = 0, buildItemsMs = 0;   // part of setupMs: cache key hash, sprite draw-list build
    bool terrainLayerHit = false;     // layer served unchanged
    bool terrainLayerShift = false;   // layer shifted by whole pixels (camera pan) and only the exposed strips redrawn   // terrain layer cache served this frame (bg + terrain + grid + edges not redrawn)
  };
  const StageTimes& lastStageTimes() const { return stages_; }
  // Terrain layer cache (off by default): remember the background + terrain + grid + edge-face pixels and, while
  // nothing they depend on changed, copy them back instead of redrawing, so a frame only draws sprites. The cache
  // key is a hash of everything that pass reads (camera, size, flags, altitude/water/zone/tint arrays, overlay rows,
  // surface pieces when the road mask is drawn), so it needs no invalidation calls from the host.
  void setTerrainLayerCache(bool on) { terrainCacheOn_ = on; terrainLayerValid_ = false; }
  bool terrainLayerCache() const { return terrainCacheOn_; }
  // Phase 3: render the base (background + terrain) and the sprite list for a GPU compositor instead of drawing
  // sprites. Needs the terrain layer cache and no CPU-drawn overlays (tool previews, text fallback); returns false
  // without touching the frame when the frame must be drawn with render() instead.
  bool renderForGpu(const Camera& cam, const RenderOptions& opt, const RenderScene* scene, GpuCityFrame& out);

  // ---- configuration / injection ----
  void setPalettes(const TerrainPalettes& p) { pal_ = p; palOverride_ = true; }
  const TerrainPalettes& palettes() const { return pal_; }
  void setNoiseSeed(uint32_t seed);  // regenerates the two noise tables (spec 6.3)
  // Replace sprite lookup (tests / non-GameData sources). Null functions restore GameData lookup.
  using SpriteFn = std::function<const SpriteImage*(uint32_t, uint32_t)>;
  using PlaneFn = std::function<const Plane*(uint32_t, uint32_t)>;
  void setSpriteLookup(SpriteFn s, PlaneFn p) { spriteFn_ = std::move(s); planeFn_ = std::move(p); ++itemsGen_; itemCacheValid_ = false; }
  // Generic substitute sprite by footprint size (spec 25 section 7 per-zoom substitution).
  void setSubstituteSprite(int footprint, uint32_t category, uint32_t spriteId);
  // palBasic tint row for a world tile (0 = none, 1..52); spec 6.4 tints.
  void setTileTint(int x, int z, uint8_t basicRow);
  void clearTileTints() { tint_.clear(); }
  // Tiles whose water counts as polluted (water-pollution amount >= WaterPollutedThreshold): their water gets palBasic row 51 (spec 25 6.4).
  void setPollutedWater(const std::vector<uint8_t>& tiles) { if (tiles != pollutedWater_) { pollutedWater_ = tiles; terrainLayerValid_ = false; ++itemsGen_; } }
  void setTileTints(const std::vector<uint8_t>& rows) { if (world_ && rows.size() == size_t(world_->W) * world_->H) tint_ = rows; }   // one palBasic row per tile (0 = none)
  // Cost text: called each render with the screen anchor, the text and affordability.
  void setCostTextHook(TextHook h) override { textHook_ = std::move(h); }
  void setCostFont(const Font* f) override { costFont_ = f; }  // used when no hook is set
  const CostLabel& lastCostLabel() const { return label_; }
  int lastCostScreenX() const { return labelX_; }
  int lastCostScreenY() const { return labelY_; }

  // Terrain vertex colour (spec 6.2) at world vertex, incl. far-zoom adjustment; for tests.
  void landVertexColour(int x, int z, int zoom, bool underground, int& r, int& g, int& b);
  uint8_t shadeAt(int x, int z);

 private:
  struct Vtx { float x, y, a, b, c; };
  // Draw-list entry (internal).
  struct Item {
    uint32_t key = 0;
    const SpriteImage* spr = nullptr;
    const Plane* plane = nullptr;
    int ax = 0, ay = 0;
    bool dbl = false;
    int m2 = 32;
    bool moving = false, pickable = true, groundLevel = false;
    bool deck = false;   // bridge deck / incline piece: movers overlapping its tile draw over it (spec 25 5.2)
    bool tinted = false; uint8_t tintR = 0, tintG = 0, tintB = 0;   // scene tint for this frame (applySceneTints)
    int32_t boxA0 = 0, boxA1 = 0, boxB0 = 0, boxB1 = 0;  // static boxes (view, 1/256)
    int32_t posA = 0, posB = 0;                           // moving objects
    int tileX = 0, tileZ = 0;
    int32_t subX = 0, subZ = 0, height = 0;
    int u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // footprint view tile range [u0,u1) x [v0,v1)
    uint32_t pickKey = 0;
    uint32_t cat = 0, id = 0;
    // World-derived items are built origin-relative and culled per frame (item list cache): cull box of the base sprite.
    int cAx = 0, cAy = 0, cL = 0, cT = 0, cW = 0, cH = 0;
  };
  uint64_t itemListKey(const Camera& cam, const RenderOptions& opt) const;
  // Building animations (sprite scripts + child sprites, spec/25 section 7): candidates are the animatable buildings the
  // draw-list build saw; up to maxAnimations_ visible ones run, the rest show their static first frame.
  struct AnimCand {
    uint64_t key = 0;
    ResKey appearance;
    const CsAttrib* cs = nullptr;
    int orient = 0;
    Item base;                     // origin-relative geometry of the building's own sprite
    int tileX = 0, tileZ = 0, alt = 0;
    int occRot = 0, sizeX = 1, sizeZ = 1;   // occupant rotation and placed footprint (child offsets turn with it)
  };
  struct AnimChild { const CsAttrib* cs = nullptr; CsChild geo; std::vector<const CsScript*> scripts; std::vector<ScriptRun> runs; AnimRng rng; int j = 0; };
  struct AnimInst {
    int64_t startMs = 0, lastUpdMs = 0;
    int j = 0;
    AnimRng rng;
    int slotZoom = -1, slotRot = -1;
    std::vector<const CsScript*> scripts;
    std::vector<ScriptRun> runs;
    std::vector<AnimChild> children;
  };
  bool resolveFrame(const CsAttrib* cs, int frame, bool alpha, const SpriteImage*& img, const Plane*& plane, bool& dbl);
  void bindAnim(AnimInst& a, const AnimCand& c, int zoom, int rot, int dateMD);
  void emitAnimOverlay(const AnimCand& c, const CsScript& sc, const ScriptRun& run, std::vector<Item>& out);
  void emitChild(const AnimCand& c, const AnimChild& ch, const ScriptRun& run, const CsScript& sc, std::vector<Item>& out);
  void buildAnimItems(const Camera& cam, const RenderOptions& opt, std::vector<Item>& out);
  std::vector<AnimCand> animCands_, cachedAnim_;
  std::unordered_map<uint64_t, AnimInst> anim_;
  uint64_t animSelSig_ = 0;
  AnimRng animSelRng_{12345};
  int maxAnimations_ = 31;   // MaxAnimationsBase (10) + 3 * machine class (7)
  bool itemCacheOn_ = false, itemCacheValid_ = false;
  uint64_t itemCacheKey_ = 0, itemsGen_ = 0;
  std::vector<Item> cachedFlat_, cachedStatics_;
  void loadPalettes();
  void makeFallbackPalettes();
  void ensureShade();
  void computeOrigin(const Camera& cam);
  // Band functions draw only rows [y0, y1); render() runs them concurrently, one band per task, so they
  // must not write shared state (prepareTerrain / loadBackground / buildItems do that beforehand).
  void loadBackground(int zoom);
  // Band functions: rows [y0, y1), columns [xs, xe) (defaults: all columns).
  void drawBackground(int zoom, int y0, int y1, int xs = 0, int xe = 1 << 30);
  void prepareTerrain(const RenderOptions& opt);
  void drawTerrain(const Camera& cam, const RenderOptions& opt, int y0, int y1, int xs = 0, int xe = 1 << 30);
  void collectTerrainTris(const Camera& cam, const RenderOptions& opt, std::vector<GpuTerrainTri>& out);
  void applySceneTints(const Camera& cam, const RenderScene* scene);
  void drawGroundTints(const Camera& cam, const RenderScene& scene, int y0, int y1);
  void drawTerrainGrid(const Camera& cam, const RenderOptions& opt, int y0, int y1, int xs = 0, int xe = 1 << 30);
  void drawEdgeFaces(const Camera& cam, const RenderOptions& opt, int y0, int y1, int xs = 0, int xe = 1 << 30);
  void drawPreviews(const Camera& cam, const RenderScene& scene);
  void drawFallbackBoxes(const Camera& cam);   // data-less demo: coloured boxes instead of sprites
  void buildItems(const Camera& cam, const RenderOptions& opt, const RenderScene* scene);
  void drawItems(const RenderOptions& opt, int y0, int y1);
  void drawSprite(const Item& it, const RenderOptions& opt, int y0, int y1);
  void tileQuad(const Camera& cam, int u, int v, float sx[4], float sy[4], int wx[4], int wz[4]) const;
  const SpriteImage* getSprite(uint32_t c, uint32_t i);
  const Plane* getPlane(uint32_t c, uint32_t i);
  void quadPoly(const Vtx p[4]);

  const World* world_ = nullptr;
  GameData* data_ = nullptr;
  std::vector<uint32_t> fb_;
  int fbW_ = 0, fbH_ = 0;
  std::vector<uint8_t> shade_;
  bool shadeValid_ = false;
  int maxAlt_ = 255;
  std::vector<uint8_t> roadMask_;
  std::vector<uint8_t> tint_;
  std::vector<uint8_t> pollutedWater_;
  TerrainPalettes pal_;
  bool palOverride_ = false;
  Image back_[5];
  bool backTried_[5] = {false, false, false, false, false};
  int8_t noiseLand_[1024];
  int8_t noiseEdge_[1024];
  bool noiseInit_ = false;
  int originX_ = 0, originY_ = 0;  // screen position of view vertex (0,0) at altitude 0
  int curZoom_ = 2, curRot_ = 0;
  SpriteFn spriteFn_;
  PlaneFn planeFn_;
  struct Subst { uint32_t cat, id; };
  std::vector<Subst> subst_;  // index footprint
  IniFile bldIni_;
  bool bldIniTried_ = false, bldIniOk_ = false;
  std::map<std::string, ResKey> siteCache_;
  bool siteOccupant(uint32_t state, int size, uint32_t agent, ResKey& out);
  TextHook textHook_;
  const Font* costFont_ = nullptr;
  CostLabel label_;
  int labelX_ = 0, labelY_ = 0;

  int bands_ = 0;
  StageTimes stages_;
  uint64_t terrainLayerKey(const Camera& cam, const RenderOptions& opt) const;
  // Item list cache (off by default): the world-derived part of the draw list (surface pieces, buildings, flora)
  // is kept while camera, flags, animation tick and the hashed world content are unchanged; scene sprites (moving
  // vehicles, previews) are merged in every frame.
public:
  void setItemListCache(bool on) { itemCacheOn_ = on; itemCacheValid_ = false; }
  bool itemListCache() const { return itemCacheOn_; }
private:
  bool terrainCacheOn_ = false, terrainLayerValid_ = false, gpuPass_ = false;
  uint64_t terrainLayerGen_ = 0;
  std::vector<GpuSpriteItem> gpuItems_;
  uint64_t terrainLayerKey_ = 0;
  std::vector<uint32_t> terrainLayer_, terrainLayerNew_;
  int terrainLayerOX_ = 0, terrainLayerOY_ = 0;   // originX_/originY_ the layer was drawn with   // fb_-sized snapshot taken after the terrain pass, before sprites
  std::vector<Item> items_;
  std::vector<int> occ_;  // view tile -> index into items_ (last drawn), -1 none
  bool itemsValid_ = false;
  Camera itemsCam_;
  RenderOptions itemsOpt_;

  GpuTerrainFn gpuTerrainFn_;
  bool gpuTerrainWanted_ = false;
  std::vector<GpuTerrainTri> gpuTris_;
};

}  // namespace m3k
