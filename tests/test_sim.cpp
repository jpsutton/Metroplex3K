#include "core/sim.h"
#include "core/terrain_gen.h"
#include "testutil.h"
using namespace m3k;

int main() {
  SimData d;
  d.parseZoneDescriptions(IniFile::fromText("[ZoneDeveloperDescriptions]\n1=0x619BA64E,3000:3100,1,255,0;3300,1,255,100\n5=0x41A3ADC1,5000:5100,1,255,0\n"));
  d.parseZoneRules(IniFile::fromText("[ZoneDeveloperRules]\n1,0xA22653C0=0,0,4;0,0,11\n5,0x422C9D52=0,0,7\n"));
  d.parseValveEffects(IniFile::fromText("[AgentValveEffects]\n3000=3000:10:0+7000:0:0\n5000=5000:5:0+7000:0:8 ; c\n"));
  d.parseFamilyText(4, "TXT! r\r\nLandUse: 3300\r\nBuildingCount: 1\r\nKey: 1406,545184782,100,1   x\r\n");
  d.parseFamilyText(11, "TXT! r\r\nLandUse: 3100\r\nKey: 1406,545184782,101,2\r\n");
  d.parseFamilyText(7, "TXT! r\r\nLandUse: 5100\r\nKey: 1406,545184782,200,1\r\n");
  d.parseRuleFinal("0 16 1 0 2 0 4 1 5 255 11225 1 5 2 0 4 1 5 255 7424");
  CHECK(d.zones.at(1).cats.size() == 2 && d.zones.at(1).cats[1].minLV == 100);
  CHECK(d.zones.at(1).families.count(4) && d.families.at(11).members[0].weight == 2);
  CHECK(d.effectsFor(3000)->size() == 2);
  CHECK(d.roadFinal.at(0) == 11225 && d.roadFinal.at(5) == 7424);

  World w; w.init(64, 64);
  for (auto& a : w.alt) a = 100;
  for (auto& a : w.water) a = 50;
  Sim s; s.init(&w, nullptr, &d, 7);
  std::vector<std::pair<int, int>> path;
  for (int x = 5; x < 40; x++) path.push_back({x, 20});
  CHECK(s.buildRoad(path));
  CHECK(w.surface.size() == 35);
  CHECK(s.paintZone(8, 21, 30, 22, Tool::ZoneRL));
  CHECK(s.paintZone(8, 18, 30, 19, Tool::ZoneCL));
  for (int m = 0; m < 24; m++) { for (int dd = 0; dd < 31; dd++) s.tickDay(); s.tickMonth(); }
  int res = 0, com = 0, occ = 0;
  for (auto& o : w.occupants) { if (o.occId == 100 || o.occId == 101) res++; if (o.occId == 200) com++; if (o.state == 1) occ++; }
  std::printf("res=%d com=%d occupied=%d pop=%d funds=%lld\n", res, com, occ, s.population(), (long long)s.funds);
  CHECK(res > 0 && com > 0 && occ > 0);
  CHECK(s.bulldoze(5, 20, 39, 20));
  CHECK(w.surface.empty());
  TEST_MAIN_END();
}
