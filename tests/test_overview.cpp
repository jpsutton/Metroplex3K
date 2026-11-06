// Overview map colours (core/overview.h): water rule, land bands, zones over terrain, region image.
#include "core/overview.h"
#include "core/image.h"
#include "testutil.h"
using namespace m3k;

int main() {
  World w; w.init(8, 8);
  w.seaLevel = 30;
  for (auto& a : w.alt) a = 40;
  for (auto& v : w.water) v = 30;
  const uint32_t water = rgba(106, 105, 156);

  // Land bands by the height of corner (x, z) above its water surface (default landscape).
  auto at = [&](int d) { w.alt[w.vIdx(1, 1)] = uint8_t(30 + d); return overviewTerrainColour(w, 1, 1); };
  CHECK(at(1) == rgba(123, 148, 106));
  CHECK(at(2) == rgba(139, 157, 106) && at(3) == rgba(139, 157, 106));
  CHECK(at(4) == rgba(156, 161, 115));
  CHECK(at(7) == rgba(156, 149, 115));
  CHECK(at(8) == rgba(148, 133, 98) && at(25) == rgba(148, 133, 98));
  CHECK(at(26) == rgba(131, 113, 90));
  CHECK(at(154) == rgba(123, 121, 123) && at(200) == rgba(123, 121, 123));

  // Water: any corner at or below its water surface, even when corner (x, z) is high.
  w.alt[w.vIdx(1, 1)] = 60;
  w.alt[w.vIdx(2, 2)] = 30;
  CHECK(overviewTerrainColour(w, 1, 1) == water);
  CHECK(overviewTerrainColour(w, 2, 2) == water);       // corner (2, 2) is that tile's (x, z) corner too
  CHECK(overviewTerrainColour(w, 3, 3) != water);
  w.alt[w.vIdx(2, 2)] = 40;

  // Other landscapes; bands past the last measured one keep its colour; unknown landscapes use the default.
  w.scheme.landscape = 0xE55B8055;
  CHECK(at(2) == rgba(74, 137, 65));
  CHECK(at(120) == rgba(82, 117, 57));
  w.scheme.landscape = 0x12345678;
  CHECK(at(2) == rgba(139, 157, 106));
  w.scheme.landscape = 0;

  // Zones win over terrain (also over water); unknown zone types show terrain.
  w.zone[w.tIdx(1, 1)] = 1;
  w.zone[w.tIdx(2, 2)] = 22;
  w.zone[w.tIdx(3, 3)] = 4;
  w.alt[w.vIdx(2, 2)] = 30;
  CHECK(overviewTileColour(w, 1, 1) == rgba(0, 255, 0));
  CHECK(overviewTileColour(w, 2, 2) == rgba(239, 93, 0));
  CHECK(overviewTileColour(w, 3, 3) == overviewTerrainColour(w, 3, 3));
  CHECK(overviewZoneColour(0) == 0 && overviewZoneColour(11) == rgba(148, 150, 0));

  // Neighbour region image (spec 11 section 9).
  CHECK(neighbourRegionColourIndex(-20) == 0 && neighbourRegionColourIndex(0) == 0);
  CHECK(neighbourRegionColourIndex(1) == 1 && neighbourRegionColourIndex(9) == 5);
  CHECK(neighbourRegionColourIndex(10) == 5 && neighbourRegionColourIndex(26) == 6 && neighbourRegionColourIndex(127) == 9);
  World c; c.init(128, 128);
  for (auto& v : c.water) v = 30;
  for (int x = 0; x <= 128; x++) for (int z = 0; z <= 128; z++) c.alt[c.vIdx(x, z)] = uint8_t(31 + (x + z) / 32);
  const std::array<bool, 4> land{}, sea0{true, false, false, false};
  std::vector<int> h1 = neighbourRegionHeights(c, land), h2 = neighbourRegionHeights(c, land), hs = neighbourRegionHeights(c, sea0);
  CHECK(h1.size() == 256u * 256u);
  CHECK(h1 == h2);                                       // fixed seed: same city, same image
  // Middle block: city heights above water, row q = 85 + i follows x, column p = 85 + j follows z.
  CHECK(h1[size_t(85) * 256 + 85] == 1);
  CHECK(h1[size_t(170) * 256 + 170] == c.altAt(85 * 129 / 86, 85 * 129 / 86) - 30);
  CHECK(h1[size_t(85) * 256 + 170] == c.altAt(0, 85 * 129 / 86) - 30);
  // Sea edge 0: the trapezoid between row 0 and the city block is -20.
  CHECK(hs[size_t(0) * 256 + 0] == -20 && hs[size_t(85) * 256 + 128] == -20 && hs[size_t(40) * 256 + 200] == -20);
  CHECK(hs != h1);
  // Sea edges from the flag at the edge-midpoint tiles.
  auto e = neighbourSeaEdges(c, [](int x, int z) { return x == 0 && z == 64; });
  CHECK(e[0] && !e[1] && !e[2] && !e[3]);
  e = neighbourSeaEdges(c, [](int x, int z) { return x == 127 && z == 64; });
  CHECK(!e[0] && e[2]);
  CHECK(!neighbourSeaEdges(c, nullptr)[1]);
  Image im = neighbourRegionImage(c, sea0, neighbourRegionDefaultRamp());
  CHECK(im.w == 256 && im.h == 256 && im.px[0] == water);
  TEST_MAIN_END();
}
