// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// City overview map colours: the one-pixel-per-tile map stored as the save file's preview thumbnail
// (spec/10 section 8.4) and shown by the navigator mini-map; and the Neighbors Map region image
// (spec/11 section 9).
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
#include "core/image.h"
#include "core/world.h"

namespace m3k {

// Terrain colour of tile (x, z): water when any corner is at or below its water surface, else the
// landscape's band colour for the height of corner (x, z) above its water surface.
uint32_t overviewTerrainColour(const World& w, int x, int z);
// Colour of a zone type on the overview map; 0 for no zone (or a zone type without a colour).
uint32_t overviewZoneColour(int zoneType);
// Full thumbnail colour of a tile: its zone colour, else its terrain colour. Buildings, networks and flora on
// unzoned land are not drawn.
uint32_t overviewTileColour(const World& w, int x, int z);

// ---- Neighbors Map region image (spec 11 section 9) ----
// Sea-water flags of the four edge-midpoint tiles (neighbour order 0-3, section 9.3). `seaWaterAt` reads the
// per-cell sea-water flag (pollution state bit 30); null means no flags.
std::array<bool, 4> neighbourSeaEdges(const World& w, const std::function<bool(int x, int z)>& seaWaterAt);
// The 256 x 256 signed heights after steps 1-4 (fixed seed, so deterministic), index q * 256 + p:
// column p follows z, row q follows x.
std::vector<int> neighbourRegionHeights(const World& w, const std::array<bool, 4>& seaEdge);
// Colour index 0-11 of a region height (section 9.6).
int neighbourRegionColourIndex(int h);
// Coloured 256 x 256 region image (same layout) using `ramp` = columns 0-11 of row 3 of image 000005E5:21.
Image neighbourRegionImage(const World& w, const std::array<bool, 4>& seaEdge, const std::array<uint32_t, 12>& ramp);
// That ramp row without game data (its values equal the default landscape's thumbnail colours).
std::array<uint32_t, 12> neighbourRegionDefaultRamp();

}  // namespace m3k
