// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/city_rasterizer.h"

#include <cstdlib>
#include <cstring>

namespace m3k {

RenderBackendKind renderBackendFromEnv() {
  const char* v = std::getenv("M3K_RENDER");
  if (!v || !*v) return RenderBackendKind::Cpu;
  if (std::strcmp(v, "gpu") == 0 || std::strcmp(v, "GPU") == 0) return RenderBackendKind::Gpu;
  return RenderBackendKind::Cpu;
}

}  // namespace m3k
