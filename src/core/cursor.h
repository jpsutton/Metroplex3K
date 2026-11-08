// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Mouse cursors (spec/08 section 8): load a cursor by file stem from Res/UI/Shared/Cursors/. The game shows the .cur file
// itself (monochrome: black / white pixels with a transparency mask, plus the hotspot). The .bmp beside it is a colour
// design preview that differs from the .cur in more than half of the files (tools/probes/probe_cursors), so it is only used
// for `anchor point cursor` (spec 04 8.3 loads that .bmp as an image) and as a fallback when a .cur cannot be decoded.
#pragma once
#include <string>
#include "core/gamedata.h"
#include "core/image.h"

namespace m3k {

struct CursorImage {
  Image image;       // RGBA, alpha 0 where transparent
  int hotX = 0, hotY = 0;
  bool ok() const { return !image.empty(); }
};

// `name` without extension ("QUERY CURSOR"); matched case-insensitively. False if missing. colourTwin = use the .bmp picture
// (magenta = transparent) with the .cur hotspot instead of the .cur picture.
bool loadCursor(GameData& g, const std::string& name, CursorImage& out, bool colourTwin = false);

// Decodes a Windows .cur / .ico file (first image; 1, 4 or 8 bits per pixel with palette and AND mask) to RGBA, alpha 0
// where the AND mask is set. The hotspot is the cursor's (type 2) or 0,0 (icon type). False if the file is not decodable.
bool decodeCursorFile(const uint8_t* p, size_t n, CursorImage& out);

// Hotspot of a Windows .cur file (ICONDIR entry 0). False if not a cursor file.
bool curHotspot(const uint8_t* p, size_t n, int& hx, int& hy);

// Drag-scroll classification of spec 8.3. d = (pointer - anchor) / 2 per axis (truncated toward zero).
int dragScrollVelocity(int d);                 // 80*sign if |d|>80; d if 12<|d|<=80; else 0
const char* panCursorName(int dx, int dy);     // d values; both neutral -> "EARLY MOVER CURSOR"

}  // namespace m3k
