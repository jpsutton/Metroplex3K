// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Render backend selection (Phase 2). Default is the software Renderer; M3K_RENDER=gpu asks the
// host-registered OpenGL compute terrain path to fill terrain bands when available.
#pragma once
#include "core/render.h"

namespace m3k {

enum class RenderBackendKind { Cpu, Gpu };
RenderBackendKind renderBackendFromEnv();

}  // namespace m3k
