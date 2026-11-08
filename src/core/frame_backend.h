// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Host frame present interface (Phase 0). Core stays free of SDL / GPU APIs; the desktop host
// (and later a compute-backed host) supply a FrameBackend. Headless tools call Game::frame
// without one and read Game::screen().
#pragma once
#include <cstdint>
#include <vector>

namespace m3k {

struct SpriteImage;
struct Plane;

// One sprite of the city draw list, in draw order (Phase 3). Pixel rules match Renderer::drawSprite: image pixel
// (x/sc, y/sc) with alpha 0 is skipped; when !blendAll it overwrites; otherwise it is blended with
// alpha = min(31, (plane * m2) >> 5) (plane = 31 without a plane), floor((32 d + alpha (s - d)) / 32) per channel.
struct GpuSpriteItem {
  const SpriteImage* spr = nullptr;
  const Plane* plane = nullptr;   // null, or same size as the image
  int x0 = 0, y0 = 0, w = 0, h = 0, sc = 1, m2 = 32;
  bool blendAll = false;
};

// The city for one frame, split for the GPU: the CPU-drawn base (background + terrain + grid + edge faces; it only
// changes when baseGen changes) and the sprites to composite over it.
struct GpuCityFrame {
  int w = 0, h = 0;
  const uint32_t* base = nullptr;   // w*h RGBA8, valid until the next render
  uint64_t baseGen = 0;
  const std::vector<GpuSpriteItem>* items = nullptr;
};

// Implemented by a host that can composite the city on the GPU. The Game draws the UI into the frame buffer as an
// overlay: untouched pixels have alpha 0 (city shows through), opaque pixels alpha 255, blended ones in between.
class CityGpuSink {
 public:
  virtual ~CityGpuSink() = default;
  virtual bool submitCity(const GpuCityFrame& f) = 0;   // false: the frame must be drawn on the CPU instead
  virtual void invalidateSprites() = 0;                 // sprite images may have been freed or replaced (world / data change)
  virtual void setCityVisible(bool on) = 0;             // overlay is shown over the GPU city (true) or alone (false)
};

// Destination for one Game::frame. pixels may be GPU staging memory (LockTexture) or any pitched
// RGBA8 buffer (A<<24|B<<16|G<<8|R). pitchBytes 0 means tightly packed w * 4.
struct FrameBuffer {
  uint32_t* pixels = nullptr;
  int pitchBytes = 0;
  int w = 0, h = 0;
  CityGpuSink* citySink = nullptr;   // set by backends that can composite the city on the GPU
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
