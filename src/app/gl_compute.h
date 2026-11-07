// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Hidden OpenGL 4.3+ compute device for Phase 2 terrain fill. Independent of SdlPresenter.
#pragma once
#include <cstdint>
#include "core/render.h"

namespace m3k {

class GlComputeTerrain {
 public:
  GlComputeTerrain() = default;
  ~GlComputeTerrain();

  // Create a hidden GL window/context and compile the terrain compute program. Safe to call once.
  bool init();
  bool available() const { return ok_; }

  // Rasterise `tris` into `fb` (RGBA8, size w*h), preserving untouched pixels (background).
  // Returns false on failure (caller falls back to CPU terrain).
  bool fill(uint32_t* fb, int w, int h, int zoom,
            const int8_t* noiseLand, const int8_t* noiseEdge,
            const GpuTerrainTri* tris, int triCount);

 private:
  bool ok_ = false;
  bool initTried_ = false;
  void* window_ = nullptr;   // SDL_Window*
  void* glctx_ = nullptr;    // SDL_GLContext
  unsigned prog_ = 0;
  unsigned ssboTris_ = 0, ssboNoiseL_ = 0, ssboNoiseE_ = 0, ssboFb_ = 0, ssboRows_ = 0;
  int fbCap_ = 0, triCap_ = 0;
};

}  // namespace m3k
