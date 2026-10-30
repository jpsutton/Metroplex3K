#include "core/world.h"
#include "testutil.h"
using namespace m3k;

int main() {
  World w; w.init(128, 128);
  w.info.cityName = "Testville"; w.info.mayorName = "Mayor"; w.info.year = 1950;
  w.seaLevel = 40;
  for (int x = 0; x <= 128; x++) for (int z = 0; z <= 128; z++) { w.alt[w.vIdx(x, z)] = uint8_t(30 + (x * 3 + z) % 50); w.water[w.vIdx(x, z)] = 40; }
  w.flora[w.tIdx(5, 9)] = 0x34; w.zone[w.tIdx(100, 3)] = 2;
  Occupant o; o.x = 10; o.z = 20; o.y = 33; o.rot = 2; o.sizeX = 3; o.sizeZ = 3; o.occId = 3000; o.keyType = 0x207EDC0E; o.agentType = 3300; o.name = "Home";
  w.occupants.push_back(o);
  NetPiece np; np.x = 7; np.z = 8; np.y = 31; np.rot = 1; np.pieceId = 29; w.surface.push_back(np);
  w.view = {64, 64, 3, 1, true};

  Bytes file = saveCity(w);
  CHECK(rd32(file.data()) == kIxfMagic);
  World r = loadCity(file);
  CHECK(r.W == 128 && r.H == 128 && r.info.cityName == "Testville" && r.info.year == 1950);
  CHECK(r.seaLevel == 40 && r.alt == w.alt && r.water == w.water);
  CHECK(r.flora[r.tIdx(5, 9)] == 0x34 && r.zone[r.tIdx(100, 3)] == 2);
  CHECK(r.occupants.size() == 1 && r.occupants[0].x == 10 && r.occupants[0].z == 20 && r.occupants[0].y == 33);
  CHECK(r.occupants[0].rot == 2 && r.occupants[0].sizeX == 3 && r.occupants[0].name == "Home" && r.occupants[0].occId == 3000);
  CHECK(r.surface.size() == 1 && r.surface[0].pieceId == 29 && r.surface[0].rot == 1);
  CHECK(r.view.valid && r.view.zoom == 3 && r.view.rotation == 1);
  // Re-save is stable.
  CHECK(saveCity(r) == file);
  TEST_MAIN_END();
}
