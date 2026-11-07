// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Overview map colours, measured from the preview thumbnails of every shipped .sc3 and .sct file (36 files):
// with the rules below every pixel of every thumbnail is reproduced (16-bit display rounding aside).
#include "core/overview.h"
#include <algorithm>
#include "core/terrain_gen.h"
#include <vector>

namespace m3k {

namespace {
// Land bands by d = altitude of the tile's (x, z) corner above that corner's water surface (d >= 1 on land).
constexpr int kBandStart[11] = {1, 2, 4, 6, 8, 26, 52, 77, 103, 128, 154};

struct LandBands { uint32_t id; int n; uint32_t c[11]; };
// Per landscape (SC3CityScheme.ini [LandScapes] key). Bands above the last listed one keep its colour: the
// shipped files have no land that high in those landscapes.
constexpr LandBands kLand[5] = {
    {0x00000000, 11, {rgba(123, 148, 106), rgba(139, 157, 106), rgba(156, 161, 115), rgba(156, 149, 115), rgba(148, 133, 98),
                      rgba(131, 113, 90), rgba(123, 101, 90), rgba(115, 101, 90), rgba(106, 97, 98), rgba(115, 109, 115),
                      rgba(123, 121, 123)}},
    {0xC55A6D62, 7, {rgba(213, 214, 222), rgba(197, 210, 222), rgba(164, 186, 213), rgba(148, 165, 197), rgba(131, 145, 180),
                     rgba(123, 131, 156), rgba(115, 115, 131)}},
    {0xC55A6D84, 5, {rgba(139, 93, 65), rgba(156, 117, 74), rgba(180, 145, 90), rgba(189, 165, 106), rgba(180, 170, 106)}},
    {0x655B8076, 6, {rgba(139, 117, 41), rgba(139, 117, 49), rgba(139, 115, 49), rgba(148, 125, 49), rgba(139, 121, 49),
                     rgba(131, 105, 57)}},
    {0xE55B8055, 6, {rgba(82, 153, 74), rgba(74, 137, 65), rgba(65, 121, 57), rgba(57, 117, 57), rgba(65, 117, 57),
                     rgba(82, 117, 57)}},
};
constexpr uint32_t kWater = rgba(106, 105, 156);

const LandBands& bandsFor(uint32_t landscape) {
  for (const auto& b : kLand) if (b.id == landscape) return b;
  return kLand[0];
}

// Neighbour region image (spec 11 section 9): 256 x 256 cells, 16-bit, 0xFFFF = unset; heights are read back as the
// signed low byte. Cell (p, q) = column p (follows z), row q (follows x).
class RegionGrid {
 public:
  RegionGrid() : I_(size_t(kSide) * kSide, 0xFFFF) {}
  static constexpr int kSide = 256;
  uint16_t raw(int p, int q) const { return I_[size_t(q) * kSide + p]; }
  void setRaw(int p, int q, uint16_t v) { I_[size_t(q) * kSide + p] = v; }
  int get(int p, int q) const { return int(int8_t(uint8_t(raw(p, q) & 0xFF))); }
  void set(int p, int q, int h) { setRaw(p, q, uint16_t(h)); }
  bool unset(int p, int q) const { return raw(p, q) == 0xFFFF; }

  // Midpoint displacement over the rectangle (x0, y0)-(x1, y1), x = p, y = q (section 9.5).
  void fill(int x0, int y0, int x1, int y1) {
    for (;;) {
      if (x0 + 1 >= x1 && y0 + 1 >= y1) return;
      int mx = (x0 + x1) / 2;
      if (((x0 + x1) & 1) && rand(2) != 0) mx++;
      int my = (y0 + y1) / 2;
      if (((y0 + y1) & 1) && rand(2) != 0) my++;
      int d = std::min(x1 - x0, y1 - y0) / 2;
      if (unset(x0, my)) set(x0, my, mix(get(x0, y0), get(x0, y1), d));
      if (unset(x1, my)) set(x1, my, mix(get(x1, y0), get(x1, y1), d));
      if (unset(mx, y0)) set(mx, y0, mix(get(x0, y0), get(x1, y0), d));
      if (unset(mx, y1)) set(mx, y1, mix(get(x0, y1), get(x1, y1), d));
      if (unset(mx, my)) set(mx, my, rand(2) != 0 ? mix(get(x0, y0), get(x1, y1), d) : mix(get(x0, y1), get(x1, y0), d));
      fill(x0, y0, mx, my);
      fill(mx, y0, x1, my);
      fill(x0, my, mx, y1);
      x0 = mx; y0 = my;
    }
  }

  Lcg rng;
  int rand(int n) { return n <= 0 ? 0 : int(rng.below(uint32_t(n))); }
  int centreRand(int lo, int hi) {   // spec 11 section 3.2: three draws
    int half = (hi - lo) / 2;
    int a = rand(half), b = rand(half), m = std::min(a, b);
    if (rand(2) != 0) m = -m;
    return lo + half + m;
  }

 private:
  int mix(int a, int b, int d) {
    int m = (a + b) / 2, lo = std::max(m - d, -127), hi = std::min(m + d, 127);
    if (lo < hi - 2) { int v = centreRand(lo, hi); return v < 0 ? (2 * v) / 3 : v; }
    return (lo + hi) / 2;
  }
  std::vector<uint16_t> I_;
};
}  // namespace

uint32_t overviewTerrainColour(const World& w, int x, int z) {
  x = std::max(0, std::min(w.W - 1, x)); z = std::max(0, std::min(w.H - 1, z));
  for (int c = 0; c < 4; c++) {
    int vx = x + (c & 1), vz = z + (c >> 1);
    if (w.altAt(vx, vz) <= w.waterAt(vx, vz)) return kWater;
  }
  const LandBands& lb = bandsFor(w.scheme.landscape);
  int d = int(w.altAt(x, z)) - int(w.waterAt(x, z)), band = 0;
  for (int i = 0; i < 11; i++) if (d >= kBandStart[i]) band = i;
  return lb.c[std::min(band, lb.n - 1)];
}

// Zone colours as the original's minimap shows them [OBSERVED: the frame's exact values]; matches the spec's pure zone colours.
uint32_t overviewZoneColour(int zoneType) {
  switch (zoneType) {
    case 1: return rgba(0, 255, 0);       // residential low / medium / high
    case 2: return rgba(0, 203, 0);
    case 3: return rgba(0, 150, 0);
    case 5: return rgba(0, 0, 255);       // commercial
    case 6: return rgba(0, 0, 206);
    case 7: return rgba(0, 0, 148);
    case 9: return rgba(255, 251, 0);     // industrial
    case 10: return rgba(206, 203, 0);
    case 11: return rgba(148, 150, 0);
    case 14: return rgba(49, 206, 255);   // airport
    case 15: return rgba(49, 255, 156);   // seaport
    case 17: return rgba(156, 101, 49);    // landfill
    case 22: return rgba(239, 93, 0);     // civic / government buildings
    default: return 0;
  }
}

uint32_t overviewTileColour(const World& w, int x, int z) {
  if (w.inTiles(x, z)) {
    size_t i = size_t(w.tIdx(x, z));
    if (i < w.zone.size()) if (uint32_t c = overviewZoneColour(w.zone[i])) return c;
  }
  return overviewTerrainColour(w, x, z);
}

int neighbourRegionColourIndex(int h) {
  if (h < 1) return 0;
  if (h < 10) return h / 2 + 1;
  return std::min(10 * h / 256 + 5, 11);
}

std::vector<int> neighbourRegionHeights(const World& w, const std::array<bool, 4>& seaEdge) {
  RegionGrid g;
  const int R = w.W + 1, C = w.H + 1;
  // Step 1: the city's heights above water, 86 x 86 samples into the middle block.
  if (w.W > 0 && w.H > 0)
    for (int i = 0; i <= 85; i++)
      for (int j = 0; j <= 85; j++) {
        int a = i * R / 86, b = j * C / 86;
        int h = std::max(0, std::min(255, int(w.altAt(a, b)) - int(w.waterAt(a, b))));
        g.set(j + 85, i + 85, h);
      }
  // Step 3: edges in the order neighbour 0, 2, 1, 3; sea edges become a trapezoid of -20, land edges copy the
  // city's edge row outward (raw cells, including the off-by-one source rows of the original).
  g.rng.seed(0x83010C56u);
  if (seaEdge[0]) { for (int r = 0; r <= 85; r++) for (int p = r; p <= 255 - r; p++) g.set(p, r, -20); }
  else for (int p = 86; p <= 171; p++) g.setRaw(p, 0, g.raw(p, 86));
  if (seaEdge[2]) { for (int r = 0; r <= 85; r++) for (int p = r; p <= 255 - r; p++) g.set(p, 255 - r, -20); }
  else for (int p = 86; p <= 171; p++) g.setRaw(p, 255, g.raw(p, 169));
  if (seaEdge[1]) { for (int r = 0; r <= 85; r++) for (int q = r; q <= 255 - r; q++) g.set(r, q, -20); }
  else for (int q = 86; q <= 171; q++) g.setRaw(0, q, g.raw(86, q));
  if (seaEdge[3]) { for (int r = 0; r <= 85; r++) for (int q = r; q <= 255 - r; q++) g.set(255 - r, q, -20); }
  else for (int q = 86; q <= 171; q++) g.setRaw(255, q, g.raw(169, q));
  // Step 4: corners, then midpoint displacement over the whole image.
  const int corner[4][2] = {{0, 0}, {0, 255}, {255, 0}, {255, 255}};
  for (const auto& c : corner) if (g.unset(c[0], c[1])) g.set(c[0], c[1], g.centreRand(-2, 10));
  g.fill(0, 0, 255, 255);
  std::vector<int> out(size_t(RegionGrid::kSide) * RegionGrid::kSide);
  for (int q = 0; q < RegionGrid::kSide; q++)
    for (int p = 0; p < RegionGrid::kSide; p++) out[size_t(q) * RegionGrid::kSide + p] = g.get(p, q);
  return out;
}

std::array<bool, 4> neighbourSeaEdges(const World& w, const std::function<bool(int x, int z)>& seaWaterAt) {
  const int R = w.W + 1, C = w.H + 1;
  const int t[4][2] = {{0, C / 2}, {R / 2, 0}, {R - 2, C / 2}, {R / 2, C - 2}};
  std::array<bool, 4> s{};
  for (int i = 0; i < 4; i++) s[size_t(i)] = w.inTiles(t[i][0], t[i][1]) && seaWaterAt && seaWaterAt(t[i][0], t[i][1]);
  return s;
}

Image neighbourRegionImage(const World& w, const std::array<bool, 4>& seaEdge, const std::array<uint32_t, 12>& ramp) {
  std::vector<int> h = neighbourRegionHeights(w, seaEdge);
  Image im;
  im.w = im.h = RegionGrid::kSide;
  im.px.resize(h.size());
  for (size_t i = 0; i < h.size(); i++) im.px[i] = ramp[size_t(neighbourRegionColourIndex(h[i]))];
  return im;
}

std::array<uint32_t, 12> neighbourRegionDefaultRamp() {
  std::array<uint32_t, 12> r{};
  r[0] = kWater;
  for (int k = 1; k < 12; k++) r[size_t(k)] = kLand[0].c[k - 1];
  return r;
}

}  // namespace m3k
