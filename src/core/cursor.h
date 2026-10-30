// Mouse cursors (spec/08 section 8): load a cursor by file stem from Res/UI/Shared/Cursors/. The colour twin (.bmp,
// magenta = transparent) supplies the picture and the .cur file supplies the hotspot.
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

// `name` without extension ("QUERY CURSOR"); matched case-insensitively. False if missing.
bool loadCursor(GameData& g, const std::string& name, CursorImage& out);

// Hotspot of a Windows .cur file (ICONDIR entry 0). False if not a cursor file.
bool curHotspot(const uint8_t* p, size_t n, int& hx, int& hy);

// Drag-scroll classification of spec 8.3. d = (pointer - anchor) / 2 per axis (truncated toward zero).
int dragScrollVelocity(int d);                 // 80*sign if |d|>80; d if 12<|d|<=80; else 0
const char* panCursorName(int dx, int dy);     // d values; both neutral -> "EARLY MOVER CURSOR"

}  // namespace m3k
