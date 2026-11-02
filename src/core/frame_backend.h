// Host frame present interface (Phase 0). Core stays free of SDL / GPU APIs; the desktop host
// (and later a compute-backed host) supply a FrameBackend. Headless tools call Game::frame
// without one and read Game::screen().
#pragma once
#include <cstdint>

namespace m3k {

// Destination for one Game::frame. pixels may be GPU staging memory (LockTexture) or any pitched
// RGBA8 buffer (A<<24|B<<16|G<<8|R). pitchBytes 0 means tightly packed w * 4.
struct FrameBuffer {
  uint32_t* pixels = nullptr;
  int pitchBytes = 0;
  int w = 0, h = 0;
};

// Acquire → Game draws → release → present. Concrete backends live in the host (src/app).
class FrameBackend {
 public:
  virtual ~FrameBackend() = default;
  virtual void setVsync(bool on) = 0;
  // Prepare a buffer for size (w,h). On true, out is valid until endFrame(); on false the host
  // should fall back to Game::frame without an external buffer (and may upload screen() itself).
  virtual bool beginFrame(int w, int h, FrameBuffer& out) = 0;
  virtual void endFrame() = 0;
  // Show the last completed frame. May block when vsync is enabled.
  virtual void present() = 0;
};

}  // namespace m3k
