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

static void testClearanceModes() {
  World w = flatWorld(16, 16);
  CityConfig cfg;
  City c;
  c.init(&w, nullptr, cfg, 1);

  const uint32_t all = kPlaceOcc | kPlaceNetwork | kPlaceSubsurface | kPlaceSite;
  auto ok = c.placement.clearance(2, 2, 3, 3, all, true, 10);
  CHECK(ok.clear && ok.cost == 40);

  Occupant o;
  o.x = 2; o.z = 2; o.sizeX = 2; o.sizeZ = 2; o.agentType = 12111; o.occId = 99;
  c.plopAgent(99, 12111, 2, 2, 2, 2, 0);
  auto blocked = c.placement.clearance(2, 2, 3, 3, kPlaceOcc);
  CHECK(!blocked.clear);

  // Occupants ignored when mode omits kPlaceOcc.
  auto ignoreOcc = c.placement.clearance(2, 2, 3, 3, kPlaceSite);
  CHECK(ignoreOcc.clear);

  // Network mode: lay a power line tile.
  NetPiece p;
  p.x = 5; p.z = 5; p.pieceId = kFallbackPowerPiece;
  w.surface.push_back(p);
  c.rebuildIndex();
  CHECK(!c.placement.clearance(5, 5, 5, 5, kPlaceNetwork).clear);
  CHECK(c.placement.clearance(5, 5, 5, 5, kPlaceOcc).clear);  // only network occupies it
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
  auto cells = c.placement.collectFootprint(any);
  CHECK(cells.size() == 5);  // 2x2 + 1x1

  OccupantFilter onlyR;
  onlyR.bits = kFiltAgentType;
  onlyR.agentType = 3000;
  auto rCells = c.placement.collectFootprint(onlyR);
  CHECK(rCells.size() == 4);

  TileRect highlighted{};
  int highlights = 0;
  c.placement.onRegionHighlight = [&](TileRect r) { highlighted = r; highlights++; };

  auto n = c.placement.nearest(11, 11, onlyR);
  CHECK(n.found && n.occIndex == 0);
  CHECK(n.bbox.x0 == 10 && n.bbox.z0 == 10 && n.bbox.x1 == 11 && n.bbox.z1 == 11);
  CHECK(highlights == 1);
  CHECK(highlighted.x0 == 10 && highlighted.x1 == 11);

  // Utilities-style region highlight (same notification).
  c.placement.raiseRegionHighlight({1, 2, 3, 4});
  CHECK(highlights == 2 && highlighted.x0 == 1 && highlighted.z1 == 4);

  auto far = c.placement.nearest(0, 0, onlyR);
  CHECK(far.found && far.occIndex == 0);

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
  testClearanceModes();
  testFootprintAndNearest();
  testSubsystemNumber();
  TEST_MAIN_END();
}
