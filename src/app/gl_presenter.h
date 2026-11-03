// OpenGL FrameBackend (Phase 3). The SDL window owns a GL context. Stage 1: the CPU frame is uploaded to a texture
// and drawn as a full-window triangle. With a 4.3 context it also acts as a CityGpuSink: the city is composited by
// a compute shader (sprite atlas, tiled draw lists) over the CPU-drawn terrain base, and the UI overlay is
// composited on top in the final pass. Falls back to SdlPresenter when init() fails (main.cpp).
#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "core/frame_backend.h"

struct SDL_Window;

namespace m3k {

class GlPresenter : public FrameBackend, public CityGpuSink {
 public:
  GlPresenter() = default;
  ~GlPresenter() override;
  // `win` must have been created with SDL_WINDOW_OPENGL. Prefers a 4.3 core context (GPU city), else 3.3.
  bool init(SDL_Window* win);
  bool ok() const { return ok_; }
  bool hasGpuCity() const { return compute_; }
  void setGpuCityEnabled(bool on) { cityEnabled_ = on && compute_; }

  void setVsync(bool on) override;
  bool beginFrame(int w, int h, FrameBuffer& out) override;
  void endFrame() override;
  void present() override;

  bool submitCity(const GpuCityFrame& f) override;
  void invalidateSprites() override;
  void setCityVisible(bool on) override { cityVisible_ = on; }

  // Test hook: composite the last frame into an offscreen target and read it back (RGBA8, row 0 = top).
  bool readback(std::vector<uint32_t>& out);

  // Stats for the last submitCity (ms): atlas upload, tile binning, dispatch (includes GPU wait only in tests).
  struct Stats { double atlasMs = 0, binMs = 0, uploadMs = 0; int items = 0, tileEntries = 0, blendItems = 0, planeItems = 0; };
  const Stats& stats() const { return stats_; }

 private:
  struct Slot { int x = 0, y = 0; };
  struct Packer { int x = 0, y = 0, shelf = 0; };
  void ensureTextures(int w, int h);
  void draw(unsigned fbo, int w, int h);
  bool packSprite(const SpriteImage* s, Slot& out);
  bool packPlane(const Plane* p, Slot& out);
  bool ensureAtlas(const std::vector<GpuSpriteItem>& items, std::vector<int>& atlasPos);
  void resetAtlas();

  SDL_Window* win_ = nullptr;
  void* ctx_ = nullptr;
  bool ok_ = false, compute_ = false, cityEnabled_ = false, cityVisible_ = false;
  unsigned finalProg_ = 0, composeProg_ = 0, vao_ = 0;
  unsigned overlayTex_ = 0, baseTex_ = 0, cityTex_ = 0, atlasTex_ = 0, planeTex_ = 0;
  unsigned ssboItems_ = 0, ssboTileStart_ = 0, ssboTileIdx_ = 0;
  int tw_ = 0, th_ = 0, atlasSize_ = 0;
  uint64_t baseGen_ = ~0ull;
  bool baseValid_ = false;
  std::vector<uint32_t> stage_;   // CPU frame the game draws into (the UI overlay when the city is on the GPU)
  std::unordered_map<const SpriteImage*, Slot> spriteSlots_;
  std::unordered_map<const Plane*, Slot> planeSlots_;
  Packer spritePack_, planePack_;
  size_t itemsCap_ = 0, tileStartCap_ = 0, tileIdxCap_ = 0;
  Stats stats_;
};

}  // namespace m3k
