#include "sdl_presenter.h"

namespace m3k {

SdlPresenter::SdlPresenter(SDL_Renderer* ren) : ren_(ren) {}

SdlPresenter::~SdlPresenter() {
  if (locked_ && tex_) SDL_UnlockTexture(tex_);
  if (tex_) SDL_DestroyTexture(tex_);
}

void SdlPresenter::setVsync(bool on) {
  vsync_ = on;
  if (ren_) SDL_RenderSetVSync(ren_, on ? 1 : 0);
}

bool SdlPresenter::ensureTexture(int w, int h) {
  if (!ren_ || w <= 0 || h <= 0) return false;
  if (tex_ && tw_ == w && th_ == h) return true;
  if (locked_ && tex_) { SDL_UnlockTexture(tex_); locked_ = false; }
  if (tex_) SDL_DestroyTexture(tex_);
  tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
  tw_ = w; th_ = h;
  return tex_ != nullptr;
}

bool SdlPresenter::beginFrame(int w, int h, FrameBuffer& out) {
  out = {};
  if (locked_) { SDL_UnlockTexture(tex_); locked_ = false; }
  if (!ensureTexture(w, h)) return false;
  void* pixels = nullptr;
  int pitch = 0;
  if (SDL_LockTexture(tex_, nullptr, &pixels, &pitch) != 0) return false;
  locked_ = true;
  out.pixels = static_cast<uint32_t*>(pixels);
  out.pitchBytes = pitch;
  out.w = w;
  out.h = h;
  return true;
}

void SdlPresenter::endFrame() {
  if (locked_ && tex_) {
    SDL_UnlockTexture(tex_);
    locked_ = false;
  }
}

void SdlPresenter::upload(const uint32_t* px, int w, int h) {
  if (!px || !ensureTexture(w, h)) return;
  if (locked_) { SDL_UnlockTexture(tex_); locked_ = false; }
  SDL_UpdateTexture(tex_, nullptr, px, w * int(sizeof(uint32_t)));
}

void SdlPresenter::present() {
  if (!ren_ || !tex_) return;
  SDL_RenderClear(ren_);
  SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
  SDL_RenderPresent(ren_);
}

}  // namespace m3k
