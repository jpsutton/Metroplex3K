#include "core/terrain_gen.h"
#include "testutil.h"
using namespace m3k;

int main() {
  World a, b, c;
  TerrainGenOptions o; o.seed = 12345;
  generateTerrain(a, 128, 128, o);
  generateTerrain(b, 128, 128, o);
  o.seed = 999;
  generateTerrain(c, 128, 128, o);
  CHECK(a.alt == b.alt && a.water == b.water && a.flora == b.flora);  // deterministic
  CHECK(a.alt != c.alt);
  CHECK(a.alt.size() == 129 * 129);
  int below = 0, above = 0;
  for (size_t i = 0; i < a.alt.size(); i++) { if (a.alt[i] <= a.seaLevel) below++; else above++; }
  std::printf("sea=%d below=%d above=%d\n", a.seaLevel, below, above);
  CHECK(above > 0 && below > 0);
  // Edge slope sanity: most adjacent vertices differ little.
  int steep = 0, total = 0;
  for (int x = 0; x < 128; x++) for (int z = 0; z < 128; z++) {
    int d = std::abs(int(a.altAt(x, z)) - int(a.altAt(x + 1, z))); total++; if (d > 8) steep++;
  }
  std::printf("steep=%d/%d\n", steep, total);
  CHECK(steep < total / 10);
  // Large map runs.
  World big; o.seed = 7; generateTerrain(big, 256, 256, o);
  CHECK(big.W == 256);
  // Timer seed (0xFFFFFFFF) works and yields valid terrain.
  { World t; TerrainGenOptions ot; ot.seed = 0xFFFFFFFFu; generateTerrain(t, 128, 128, ot); CHECK(t.alt.size() == 129 * 129); }
  // RNG per spec 2: state = state*0x41C64E6D+0x3039, output = bits 16..47 of the 64-bit product.
  { Lcg l(1); uint64_t v = uint64_t(1) * 0x41C64E6Du + 0x3039u; CHECK(l.next() == uint32_t(v >> 16)); CHECK(l.below(0) == 0); }
  // Flag 0x01 coast: column-0 edge is lowered (mostly at or below sea level).
  { World t; TerrainGenOptions ot; ot.seed = 77; ot.flags = 0x01; generateTerrain(t, 128, 128, ot);
    int low = 0; for (int x = 0; x <= 128; x++) if (t.altAt(x, 0) <= t.seaLevel + 3) low++;
    CHECK(low > 100); }
  // Water level: at least ~9/10 of vertices wet when water >= 128.
  { World t; TerrainGenOptions ot; ot.seed = 3; ot.water = 200; ot.flags = 0; generateTerrain(t, 128, 128, ot);
    size_t wet = 0; for (uint8_t a : t.alt) if (a <= t.seaLevel) wet++;
    CHECK(wet >= t.alt.size() * 9 / 10); }
  TEST_MAIN_END();
}
