// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Tiny built-in 5x7 bitmap font used when game fonts are unavailable (no game data yet).
#pragma once
namespace m3k {
const unsigned char* builtinGlyphColumns(int ch);  // 5 column bytes, bit 0 = top row
}
