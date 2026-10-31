// Software isometric renderer. Output is an RGBA8 framebuffer (u32 = A<<24|B<<16|G<<8|R); the app
// blits it with SDL. Follows spec/25-rendering.md: terrain (section 6), sprites/draw order
// (sections 4-5), overlays (section 9), picking (section 3).
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core/font.h"
#include "core/gamedata.h"
#include "core/image.h"
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

struct CostLabel {
  bool show = false;
  int tileX = 0, tileZ = 0;
  long cost = 0;
  bool affordable = true;
};

struct RenderScene {
  std::vector<SceneSprite> sprites;
  std::vector<TilePreview> previews;
  CostLabel cost;
};

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
  int animTick = 0;           // animation frame counter; < 0 disables overlay frames
  // View City Layers filters (spec 09 7.2). Null predicates show everything.
  std::function<bool(const NetPiece&)> showNet;        // surface network pieces (roads, rail, power lines)
  std::function<bool(const Occupant&)> showOccupant;   // buildings
  bool flora = true;
  bool pipes = true, subways = true;                    // pieces drawn by the underground view
  bool automata = true;                                 // scene sprites (vehicles, pedestrians)
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

class Renderer {
 public:
  // `data` may be null (no game files): procedural fallback palettes are used and no sprites drawn.
  void setWorld(const World* w, GameData* data);
  void resize(int w, int h);
  void render(const Camera& cam, const RenderOptions& opt, const RenderScene* scene = nullptr);

  const std::vector<uint32_t>& pixels() const { return fb_; }
  int width() const { return fbW_; }
  int height() const { return fbH_; }

  // Coordinate helpers (view space <-> world space <-> screen).
  static void viewToWorldVertex(int rot, int W, int H, int u, int v, int& x, int& z);
  static void worldToViewVertex(int rot, int W, int H, int x, int z, int& u, int& v);
  // Sub-tile (256/tile) world -> view, spec 25 section 2.2.
  static void worldToViewSub(int rot, int W, int H, int32_t x, int32_t z, int32_t& a, int32_t& b);
  static void viewToWorldSub(int rot, int W, int H, int32_t a, int32_t b, int32_t& x, int32_t& z);
  void screenToView(const Camera& cam, int sx, int sy, float& u, float& v) const;
  // Projected screen position of a world sub-tile point (spec 2.3, integer truncation).
  void projectSub(const Camera& cam, int32_t x, int32_t z, int32_t height, int& sx, int& sy);

  // Spec section 3 picking: sprite objects by per-pixel opacity, otherwise terrain polygons;
  // nearest depth key wins. Returns false if nothing/off-map.
  bool pick(const Camera& cam, int sx, int sy, PickResult& out);
  bool pickTile(const Camera& cam, int sx, int sy, int& tx, int& tz);
  // Terrain only (ignores objects): tile whose raised polygon contains the point.
  bool pickGround(const Camera& cam, int sx, int sy, int& tx, int& tz);
  void centreOnWorld(Camera& cam, float x, float z) const;

  void invalidateShade() { shadeValid_ = false; }

  // ---- configuration / injection ----
  void setPalettes(const TerrainPalettes& p) { pal_ = p; palOverride_ = true; }
  const TerrainPalettes& palettes() const { return pal_; }
  void setNoiseSeed(uint32_t seed);  // regenerates the two noise tables (spec 6.3)
  // Replace sprite lookup (tests / non-GameData sources). Null functions restore GameData lookup.
  using SpriteFn = std::function<const SpriteImage*(uint32_t, uint32_t)>;
  using PlaneFn = std::function<const Plane*(uint32_t, uint32_t)>;
  void setSpriteLookup(SpriteFn s, PlaneFn p) { spriteFn_ = std::move(s); planeFn_ = std::move(p); }
  // Generic substitute sprite by footprint size (spec 25 section 7 per-zoom substitution).
  void setSubstituteSprite(int footprint, uint32_t category, uint32_t spriteId);
  // palBasic tint row for a world tile (0 = none, 1..52); spec 6.4 tints.
  void setTileTint(int x, int z, uint8_t basicRow);
  void clearTileTints() { tint_.clear(); }
  // Cost text: called each render with the screen anchor, the text and affordability.
  using TextHook = std::function<void(int sx, int sy, const std::string& text, bool affordable)>;
  void setCostTextHook(TextHook h) { textHook_ = std::move(h); }
  void setCostFont(const Font* f) { costFont_ = f; }  // used when no hook is set
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
    int32_t boxA0 = 0, boxA1 = 0, boxB0 = 0, boxB1 = 0;  // static boxes (view, 1/256)
    int32_t posA = 0, posB = 0;                           // moving objects
    int tileX = 0, tileZ = 0;
    int32_t subX = 0, subZ = 0, height = 0;
    int u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // footprint view tile range [u0,u1) x [v0,v1)
    uint32_t pickKey = 0;
    uint32_t cat = 0, id = 0;
  };
  void loadPalettes();
  void makeFallbackPalettes();
  void ensureShade();
  void computeOrigin(const Camera& cam);
  void drawBackground(int zoom);
  void drawTerrain(const Camera& cam, const RenderOptions& opt);
  void drawEdgeFaces(const Camera& cam, const RenderOptions& opt);
  void drawPreviews(const Camera& cam, const RenderScene& scene);
  void drawFallbackBoxes(const Camera& cam);   // data-less demo: coloured boxes instead of sprites
  void buildItems(const Camera& cam, const RenderOptions& opt, const RenderScene* scene);
  void drawItems(const RenderOptions& opt);
  void drawSprite(const Item& it, const RenderOptions& opt);
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

  std::vector<Item> items_;
  std::vector<int> occ_;  // view tile -> index into items_ (last drawn), -1 none
  bool itemsValid_ = false;
  Camera itemsCam_;
  RenderOptions itemsOpt_;
};

}  // namespace m3k
