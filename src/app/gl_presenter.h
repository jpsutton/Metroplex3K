// OpenGL FrameBackend (Phase 3 stage 1): the SDL window owns a GL 3.3 core context; the CPU frame is uploaded to
// a texture and drawn as a full-window triangle. Later stages draw the cached terrain and sprites on the GPU
// into the same context. Falls back to SdlPresenter when init() fails (main.cpp).
#pragma once
#include <vector>
#include "core/frame_backend.h"

struct SDL_Window;

namespace m3k {

class GlPresenter : public FrameBackend {
 public:
  GlPresenter() = default;
  ~GlPresenter() override;
  // `win` must have been created with SDL_WINDOW_OPENGL. Creates the context and programs.
  bool init(SDL_Window* win);
  bool ok() const { return ok_; }

  void setVsync(bool on) override;
  bool beginFrame(int w, int h, FrameBuffer& out) override;
  void endFrame() override;
  void present() override;

  // Test hook: draw the last frame into an offscreen w*h target and read it back (RGBA8, row 0 = top).
  bool readback(std::vector<uint32_t>& out);

 private:
  void ensureTexture(int w, int h);
  void draw(unsigned fbo, int w, int h);

  SDL_Window* win_ = nullptr;
  void* ctx_ = nullptr;
  bool ok_ = false;
  unsigned tex_ = 0, prog_ = 0, vao_ = 0;
  int tw_ = 0, th_ = 0;
  std::vector<uint32_t> stage_;   // CPU frame the game draws into
};

}  // namespace m3k
