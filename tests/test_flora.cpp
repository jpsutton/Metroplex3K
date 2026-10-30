#include "core/flora.h"
#include "core/terrain_gen.h"
#include "testutil.h"
using namespace m3k;

int main() {
  int draws = 0, nextVal = 0;
  FloraRng rng = [&]() { draws++; return nextVal; };
  // Table.
  nextVal = 0;
  CHECK(floraInstanceForCell(0x20, rng) == 7040);
  CHECK(floraInstanceForCell(0x25, rng) == 7045);
  CHECK(floraInstanceForCell(0x26, rng) == 7046);
  CHECK(floraInstanceForCell(0x28, rng) == 9200);
  CHECK(floraInstanceForCell(0x30, rng) == 6);
  CHECK(floraInstanceForCell(0x35, rng) == 11);
  CHECK(floraInstanceForCell(0x37, rng) == 12);
  CHECK(floraInstanceForCell(0x39, rng) == 7057);
  CHECK(floraInstanceForCell(0x40, rng) == 7000);
  CHECK(floraInstanceForCell(0x46, rng) == 7006);
  CHECK(floraInstanceForCell(0x49, rng) == 9201);
  CHECK(floraInstanceForCell(0x10, rng) == 7054);
  CHECK(floraInstanceForCell(0x13, rng) == 7054);
  CHECK(floraInstanceForCell(0x18, rng) == 7057);
  nextVal = 1;
  CHECK(floraInstanceForCell(0x26, rng) == 7053);
  CHECK(floraInstanceForCell(0x27, rng) == 7053);
  CHECK(floraInstanceForCell(0x36, rng) == 7056);
  CHECK(floraInstanceForCell(0x46, rng) == 7052);
  CHECK(floraInstanceForCell(0x11, rng) == 7055);
  CHECK(floraInstanceForCell(0x17, rng) == 7055);
  CHECK(floraInstanceForCell(0x10, rng) == 7054);  // level 0 always 7054
  CHECK(floraInstanceForCell(0x24, rng) == 7044);  // equal-entry levels still not randomised
  // Draw accounting: one per valid non-zero cell.
  draws = 0;
  floraInstanceForCell(0, rng);
  floraInstanceForCell(0x50, rng);   // kind 5 invalid
  floraInstanceForCell(0x2A, rng);   // level 10 invalid
  CHECK(draws == 0);
  floraInstanceForCell(0x24, rng);
  floraInstanceForCell(0x10, rng);
  CHECK(draws == 2);
  CHECK(!floraCellValid(0x50) && !floraCellValid(0x1A) && floraCellValid(0x49));

  // Layer load order and save.
  World w; w.init(4, 3);
  w.flora[w.tIdx(0, 0)] = 0x26;
  w.flora[w.tIdx(3, 2)] = 0x36;
  w.flora[w.tIdx(3, 0)] = 0x21;
  w.flora[w.tIdx(1, 1)] = 0x60;  // invalid, skipped
  std::vector<int> seq;
  FloraRng rec = [&]() { seq.push_back(1); return 0; };
  auto objs = floraObjectsFromLayer(w, rec);
  CHECK(objs.size() == 3);
  CHECK(objs[0].x == 3 && objs[0].z == 0 && objs[1].x == 3 && objs[1].z == 2 && objs[2].x == 0);  // x desc, z asc
  CHECK(seq.size() == 3);
  CHECK(objs[0].instance == 7041 && objs[1].instance == 12 && objs[2].instance == 7046);
  auto layer = floraLayerFromObjects(4, 3, objs);
  CHECK(layer[3 * 3 + 0] == 0x21 && layer[0] == 0x26 && layer[3 * 3 + 2] == 0x36 && layer[4] == 0);
  std::vector<FloraObject> dup = {{1, 1, 0x21, 7041}, {1, 1, 0x32, 8}, {9, 9, 0x21, 1}, {-1, 0, 0x21, 1}};
  auto l2 = floraLayerFromObjects(4, 3, dup);
  CHECK(l2[4] == 0x32);
  int nz = 0; for (uint8_t c : l2) nz += c != 0;
  CHECK(nz == 1);

  // Lcg adapter yields 0/1.
  Lcg lcg(5); FloraRng lr = floraRngFromLcg(lcg);
  int ones = 0; for (int i = 0; i < 100; i++) { int v = lr(); CHECK(v == 0 || v == 1); ones += v; }
  CHECK(ones > 10 && ones < 90);

  // Density.
  CHECK(floraCellFromDensity(31) == 0 && floraCellFromDensity(32) == 0x30 && floraCellFromDensity(255) == 0x36);
  { FloraRng z0 = []() { return 0; }; CHECK(floraInstanceForCell(floraCellFromDensity(32), z0) == 6); CHECK(floraInstanceForCell(floraCellFromDensity(255), z0) == 12); }

  // Tree tool.
  {
    TreeTool tt = TreeTool::fromIni(IniFile::fromText("[BuildingPlacementCosts]\n300=7\n"));
    CHECK(tt.cost == 7);
    CHECK(TreeTool::fromIni(IniFile::fromText("")).cost == 0);
    World f; f.init(8, 8);
    std::fill(f.alt.begin(), f.alt.end(), uint8_t(50));
    std::fill(f.water.begin(), f.water.end(), uint8_t(10));
    TreePlan p = planTree(f, 2, 2, 0x32, tt);
    CHECK(p.legal && p.cost == 7);
    CHECK(commitTree(f, p) && f.flora[f.tIdx(2, 2)] == 0x32);
    CHECK(!planTree(f, 2, 2, 0x32, tt).legal && planTree(f, 2, 2, 0x32, tt).reason == TreeReason::SameFlora);
    CHECK(planTree(f, 2, 2, 0x41, tt).legal);  // replace
    CHECK(planTree(f, 8, 2, 0x32, tt).reason == TreeReason::OutOfMap);
    CHECK(planTree(f, 3, 3, 0x00, tt).reason == TreeReason::BadCell);
    CHECK(planTree(f, 3, 3, 0x52, tt).reason == TreeReason::BadCell);
    f.water[f.vIdx(5, 5)] = 60;
    CHECK(planTree(f, 4, 4, 0x32, tt).reason == TreeReason::Water);
    CHECK(planTree(f, 4, 4, 0x12, tt).legal);
    Occupant o; o.x = 6; o.z = 6; o.sizeX = 2; o.sizeZ = 2; f.occupants.push_back(o);
    CHECK(planTree(f, 7, 7, 0x32, tt).reason == TreeReason::Occupied);
    CHECK(planTree(f, 7, 7, 0x32, tt, [](int, int) { return false; }).legal);
    CHECK(!commitTree(f, planTree(f, 7, 7, 0x32, tt)));
    FloraRng r1 = []() { return 1; };
    uint8_t c = randomTreeCell(4, r1);
    CHECK((c >> 4) == 4 && (c & 15) >= 1 && (c & 15) <= 5);
  }
  TEST_MAIN_END();
}
