// Software isometric renderer. Output is an RGBA8 framebuffer; the app blits it with SDL.
// Terrain follows spec/25-rendering.md section 6; objects follow section 4-5.
#pragma once
#include <memory>
#include <vector>
#include "core/gamedata.h"
#include "core/image.h"
#include "core/world.h"

namespace m3k {

struct Camera {
  float u = 64, v = 64;  // view-space tile at the screen centre (fractional)
  int zoom = 2;          // 0..4
  int rot = 0;           // 0..3
};

struct RenderOptions {
  bool grid = false;
  bool objects = true;
  bool terrain = true;
};

struct Palettes {
  bool loaded = false;
  Image land;   // 32 x 512, top-down rows
  Image water;  // 1 x 256 (or any width, uses column 0)
  bool have() const { return loaded; }
};

class Renderer {
 public:
  // `data` may be null (no game files): a procedural fallback palette is used and no sprites drawn.
  void setWorld(const World* w, GameData* data);
  void resize(int w, int h);
  void render(const Camera& cam, const RenderOptions& opt);

  const std::vector<uint32_t>& pixels() const { return fb_; }
  int width() const { return fbW_; }
  int height() const { return fbH_; }

  // Coordinate helpers (view space <-> world space <-> screen).
  static void viewToWorldVertex(int rot, int W, int H, int u, int v, int& x, int& z);
  static void worldToViewVertex(int rot, int W, int H, int x, int z, int& u, int& v);
  void screenToView(const Camera& cam, int sx, int sy, float& u, float& v) const;
  // Pick the world tile under a screen point (flat-ground approximation). Returns false if off-map.
  bool pickTile(const Camera& cam, int sx, int sy, int& tx, int& tz) const;
  void centreOnWorld(Camera& cam, float x, float z) const;

  void invalidateShade() { shadeValid_ = false; }

 private:
  struct Vtx { float x, y; float r, g, b; };
  void fillTri(const Vtx& a, const Vtx& b, const Vtx& c, int noiseSeed, bool noise);
  void drawTerrain(const Camera& cam, const RenderOptions& opt);
  void drawObjects(const Camera& cam);
  void blitSprite(const SpriteImage& s, int ax, int ay, bool double2x);
  void computeShade();
  void loadPalettes();
  void farZoom(int z, float& r, float& g, float& b) const;

  const World* world_ = nullptr;
  GameData* data_ = nullptr;
  std::vector<uint32_t> fb_;
  int fbW_ = 0, fbH_ = 0;
  std::vector<uint8_t> shade_;
  bool shadeValid_ = false;
  Palettes pal_;
  float noise_[1024];
  bool noiseInit_ = false;
  int originX_ = 0, originY_ = 0;  // screen position of view vertex (0,0) at altitude 0
  int curZoom_ = 2;
};

}  // namespace m3k
