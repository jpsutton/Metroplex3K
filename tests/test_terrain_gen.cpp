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
  TEST_MAIN_END();
}
