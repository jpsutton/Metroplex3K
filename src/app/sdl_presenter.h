// SDL2 FrameBackend: streaming texture lock → Game draws in place → copy to the window.
#pragma once
#include <SDL.h>
#include "core/frame_backend.h"

namespace m3k {

class SdlPresenter : public FrameBackend {
 public:
  explicit SdlPresenter(SDL_Renderer* ren);
  ~SdlPresenter() override;

  void setVsync(bool on) override;
  bool beginFrame(int w, int h, FrameBuffer& out) override;
  void endFrame() override;
  void present() override;

  // Fallback when beginFrame fails: upload a packed CPU framebuffer then present as usual.
  void upload(const uint32_t* px, int w, int h);

 private:
  bool ensureTexture(int w, int h);

  SDL_Renderer* ren_ = nullptr;
  SDL_Texture* tex_ = nullptr;
  int tw_ = 0, th_ = 0;
  bool locked_ = false;
  bool vsync_ = true;
};

}  // namespace m3k
