#include "core/placement_query.h"
#include "core/city.h"
#include "testutil.h"

using namespace m3k;

static World flatWorld(int W, int H) {
  World w;
  w.init(W, H);
  for (auto& a : w.alt) a = 40;
  for (auto& s : w.water) s = 30;
  return w;
}

static void testModeTable() {
  CHECK(clearanceUses1(0) && clearanceUses2(0) && clearanceUses4(0));
  CHECK(clearanceUses1(1) && !clearanceUses2(1) && !clearanceUses4(1));
  CHECK(clearanceUses1(2) && !clearanceUses2(2) && clearanceUses4(2));
  CHECK(clearanceUses1(3) && clearanceUses2(3) && !clearanceUses4(3));
}

static void testClearanceScanners() {
  World w = flatWorld(16, 16);
  CityConfig cfg;
  City c;
  c.init(&w, nullptr, cfg, 1);

  // Empty rect is clear in every mode; cost only accrues on 1-tile passing hits (none here).
  for (int m = 0; m <= 3; m++) {
    auto r = c.placement.clearance(2, 2, 3, 3, m, {}, true, 10);
    CHECK(r.clear && r.cost == 0);
  }

  // Hard occupant (no 0x400): fails 1-tile scanner in every mode.
  Occupant hard;
  hard.x = 2; hard.z = 2; hard.sizeX = 1; hard.sizeZ = 1;
  hard.agentType = 12111; hard.occId = 99; hard.caps = 0;
  w.occupants.push_back(hard);
  c.rebuildIndex();
  for (int m = 0; m <= 3; m++) CHECK(!c.placement.clearance(2, 2, 2, 2, m).clear);

  // Ignorable + filter pass: mode 1 (1-tile only) clears and can accumulate cost.
  w.occupants.clear();
  Occupant soft;
  soft.x = 4; soft.z = 4; soft.sizeX = 1; soft.sizeZ = 1;
  soft.agentType = 300; soft.occId = 7; soft.caps = kPlaceCapIgnorable;
  w.occupants.push_back(soft);
  c.rebuildIndex();
  OccupantFilter flora;
  flora.bits = kFiltAgentType;
  flora.agentType = 300;
  auto pass = c.placement.clearance(4, 4, 4, 4, 1, flora, true, 5);
  CHECK(pass.clear && pass.cost == 5);

  // Same soft occupant but filter rejects → fail.
  OccupantFilter other;
  other.bits = kFiltAgentType;
  other.agentType = 999;
  CHECK(!c.placement.clearance(4, 4, 4, 4, 1, other).clear);

  // Coarse scanners: a 1×1 occupant also occupies its 2- and 4-tile cells → modes 0/2/3 fail
  // even for ignorable (2/4-tile hits never filter).
  CHECK(!c.placement.clearance(4, 4, 4, 4, 0, flora).clear);
  CHECK(!c.placement.clearance(4, 4, 4, 4, 2, flora).clear);
  CHECK(!c.placement.clearance(4, 4, 4, 4, 3, flora).clear);
  // Mode 1 only uses 1-tile → still clear.
  CHECK(c.placement.clearance(4, 4, 4, 4, 1, flora).clear);
}

static void testFootprintAndNearest() {
  World w = flatWorld(32, 32);
  CityConfig cfg;
  City c;
  c.init(&w, nullptr, cfg, 1);

  Occupant a; a.x = 10; a.z = 10; a.sizeX = 2; a.sizeZ = 2; a.agentType = 3000; a.occId = 1;
  Occupant b; b.x = 20; b.z = 20; b.sizeX = 1; b.sizeZ = 1; b.agentType = 5000; b.occId = 2;
  w.occupants.push_back(a);
  w.occupants.push_back(b);
  c.rebuildIndex();

  OccupantFilter any;
  CHECK(c.placement.collectFootprint(any).size() == 5);

  OccupantFilter onlyR;
  onlyR.bits = kFiltAgentType;
  onlyR.agentType = 3000;
  CHECK(c.placement.collectFootprint(onlyR).size() == 4);

  TileRect highlighted{};
  int highlights = 0;
  c.placement.onRegionHighlight = [&](TileRect r) { highlighted = r; highlights++; };

  auto n = c.placement.nearest(11, 11, onlyR);
  CHECK(n.found && n.occIndex == 0);
  CHECK(n.bbox.x0 == 10 && n.bbox.x1 == 11);
  CHECK(highlights == 1);

  c.placement.raiseRegionHighlight({1, 2, 3, 4});
  CHECK(highlights == 2 && highlighted.z1 == 4);

  OccupantFilter miss;
  miss.bits = kFiltOccId;
  miss.occId = 999;
  CHECK(!c.placement.nearest(0, 0, miss).found);
}

static void testSubsystemNumber() {
  CHECK(int(Subsystem::PlacementQuery) == 28);
  CHECK(int(Subsystem::Weather) == 27);
}

int main() {
  testModeTable();
  testClearanceScanners();
  testFootprintAndNearest();
  testSubsystemNumber();
  TEST_MAIN_END();
}
