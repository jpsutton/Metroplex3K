// End-to-end City test on a synthetic city (no game data): roads, zones, power plant, power coverage,
// several simulated years, ordinance, save/load round trip with unknown raw records.
#include "core/city.h"
#include "core/records.h"
#include "core/sim.h"
#include "testutil.h"
using namespace m3k;

static CityConfig makeConfig() {
  CityConfig c;
  c.growth.parseZoneDescriptions(IniFile::fromText(
      "[ZoneDeveloperDescriptions]\n1=0x619BA64E,3000:3100,1,255,0;3300,1,255,100\n5=0x41A3ADC1,5000:5100,1,255,0\n9=0xE1A53B30,8000:8100,D,1,255,0\n"));
  auto binds = c.growth.parseRuleBindings(IniFile::fromText(
      "[ZoneDeveloperRules]\n1,0xA22653C0=81F53D09,01F58D3D,4;81F53D09,01F58D3D,11\n5,0x422C9D52=81F53D09,01F58D3D,7\n9,0x422C9D53=81F53D09,01F58D3D,9\n"));
  c.parseFamilyText(4, "TXT! r\r\nLandUse: 3300\r\nKey: 1406,545184782,100,1\r\n");
  c.parseFamilyText(11, "TXT! r\r\nLandUse: 3100\r\nKey: 1406,545184782,101,2\r\n");
  c.parseFamilyText(7, "TXT! r\r\nLandUse: 5100\r\nKey: 1406,545184782,200,1\r\n");
  c.parseFamilyText(9, "TXT! r\r\nLandUse: 8100\r\nKey: 1406,545184782,300,1\r\n");
  c.bindFamilies(binds);
  c.valve.parseAgentValveEffects(IniFile::fromText(
      "[AgentValveEffects]\n3100=3000:10:0+7000:0:0\n3300=3000:10:0+7000:0:0\n5100=5000:5:0+7000:0:8\n8100=8000:4:0+7000:0:6\n"));
  c.expandEffects();
  c.applySyntheticDefaults();
  return c;
}

static std::vector<std::pair<int, int>> line(int x0, int z0, int x1, int z1) {
  std::vector<std::pair<int, int>> p;
  int x = x0, z = z0;
  while (x != x1) { p.push_back({x, z}); x += x1 > x ? 1 : -1; }
  while (z != z1) { p.push_back({x, z}); z += z1 > z ? 1 : -1; }
  p.push_back({x1, z1});
  return p;
}

static World makeWorld() {
  World w; w.init(96, 96);
  for (auto& a : w.alt) a = 100;
  for (auto& a : w.water) a = 50;
  w.info.year = 1950; w.info.foundingYear = 1950; w.info.dayOfYear = 1;
  w.info.cityName = "Synth"; w.info.mayorName = "Test";
  return w;
}

int main() {
  CityConfig cfg = makeConfig();
  World w = makeWorld();
  Sim s;
  s.init(&w, nullptr, &cfg, 7);
  City& c = s.city();
  std::vector<int> sounds;
  c.onSound = [&](int id) { sounds.push_back(id); };

  // --- tools ---
  CHECK(!c.usingTilingRules());
  CHECK(s.buildRoad(line(10, 40, 70, 40)));
  CHECK(w.surface.size() == 61 && s.roadCount() == 61);
  CHECK(s.isRoadTile(30, 40) && !s.isRoadTile(30, 41));
  CHECK(!sounds.empty() && sounds.back() == 14);
  CHECK(s.paintZoneType(12, 41, 40, 43, 1));
  CHECK(s.paintZoneType(12, 37, 40, 39, 5));
  CHECK(s.paintZoneType(50, 41, 66, 43, 9));
  CHECK(sounds.back() == 38);
  CHECK(w.zone[w.tIdx(20, 42)] == 1);
  // power plant (no game data: plopAgent) + power line to the road
  CHECK(c.plopAgent(900, 12111, 4, 4, 42, 30, 100));
  // line crosses the road at z=40 (roads do not conduct, lines may cross them) and touches the zones
  CHECK(s.buildNetwork(NetworkKind::Power, line(44, 34, 44, 43)));
  CHECK(s.buildNetwork(NetworkKind::Power, line(41, 38, 43, 38)));
  CHECK(s.buildNetwork(NetworkKind::Power, line(41, 42, 43, 42)));
  CHECK(s.buildNetwork(NetworkKind::Power, line(45, 42, 49, 42)));
  CHECK(c.isPowerLine(44, 36) && c.isPowerLine(44, 40));
  CHECK(!c.isRoadTile(44, 36) && c.isRoadTile(44, 40));
  int64_t fundsBefore = s.funds;
  // broke player: no-money sound
  int64_t keep = s.funds; s.funds = 0;
  CHECK(!s.buildNetwork(NetworkKind::Pipe, line(20, 50, 25, 50)));
  CHECK(sounds.back() == 32);
  s.funds = keep;
  CHECK(s.buildNetwork(NetworkKind::Pipe, line(20, 50, 25, 50)));
  CHECK(c.isPipe(22, 50));

  // --- simulate 5 years ---
  const int startYear = w.info.year;
  for (int d = 0; d < 365 * 5; d++) s.tickDay();
  CHECK(w.info.year >= startYear + 4);
  CHECK(s.day == 365 * 5);
  int bld = 0, occupied = 0;
  for (auto& o : w.occupants) { if (o.occId == 100 || o.occId == 101 || o.occId == 200 || o.occId == 300) bld++; if (o.state == 1) occupied++; }
  std::printf("year=%d bld=%d occupied=%d pop=%d funds=%lld R=%d C=%d I=%d\n", w.info.year, bld, occupied, s.population(),
              (long long)s.funds, s.valveR.value, s.valveC.value, s.valveI.value);
  std::printf("%s", c.debugStats().c_str());
  CHECK(bld > 0);
  CHECK(s.funds != fundsBefore);
  CHECK(c.powered(44, 40) || c.powered(20, 42) || c.power.stats().produced > 0);
  CHECK(c.lastPowerProduced() > 0);
  CHECK(s.population() > 0);

  // --- ordinance ---
  uint32_t ord = 0;
  for (const auto& kv : c.ordinances.catalogue()) if (c.ordinances.available(kv.first)) { ord = kv.first; break; }
  CHECK(ord != 0);
  CHECK(c.ordinances.enact(ord));
  for (int d = 0; d < 90; d++) s.tickDay();
  CHECK(c.ordinances.enacted(ord));

  // --- tax rate flows to budget ---
  s.taxRate = 12;
  s.tickDay();
  CHECK(c.budget.tax[0].rate == 12 && c.budget.tax[2].rate == 12);

  // --- bulldoze ---
  CHECK(s.bulldoze(10, 40, 20, 40));
  CHECK(!s.isRoadTile(15, 40) && s.isRoadTile(25, 40));
  CHECK(c.bulldoze(42, 30, 45, 33));   // plant
  CHECK(s.occupantAt(43, 31) < 0);

  // --- save / load round trip with unknown raw records ---
  const ResKey unkInner{0x12345678, 0x9ABCDEF0, 7};
  const ResKey unkOuter{0x0BADF00D, 0x0BADF00D, 3};
  Bytes unk = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  if (!w.raw) w.raw = std::make_shared<RawRecordStore>();
  w.raw->setInner(unkInner, unk);
  c.flush();
  Bytes f1 = saveCity(w);
  World w2 = loadCity(f1);
  CHECK(w2.raw && w2.raw->findInner(unkInner) && w2.raw->findInner(unkInner)->data == unk);
  CHECK(w2.occupants.size() == w.occupants.size() && w2.surface.size() == w.surface.size());
  CHECK(w2.zone == w.zone && w2.info.year == w.info.year && w2.info.dayOfYear == w.info.dayOfYear);
  // layers present in the file
  CHECK(w2.raw->findInner(keys::PowerLayer) && w2.raw->findInner(keys::WaterLayer) && w2.raw->findInner(keys::LandValue));
  CHECK(w2.raw->findInner(keys::ResLayer) && w2.raw->findInner(keys::ZoneMgr));
  Bytes f2 = saveCity(w2);
  CHECK(f2 == f1);  // loaded city re-saves identically
  // outer unknown record survives too
  w2.raw->setOuter(unkOuter, Bytes{9, 9, 9});
  Bytes f3 = saveCity(w2);
  World w3 = loadCity(f3);
  CHECK(w3.raw->findOuter(unkOuter) && w3.raw->findOuter(unkOuter)->data == Bytes({9, 9, 9}));
  CHECK(w3.raw->findInner(unkInner) != nullptr);

  // reload into a fresh City: budget / ordinances restored
  Sim s2;
  s2.init(&w2, nullptr, &cfg, 9);
  CHECK(s2.funds == s.funds);
  CHECK(s2.city().ordinances.enacted(ord));
  CHECK(s2.city().budget.tax[0].rate == 12);
  CHECK(s2.city().roadCount() == s.roadCount());
  for (int d = 0; d < 400; d++) s2.tickDay();
  CHECK(s2.day == 400);
  s2.city().flush();
  Bytes f4 = saveCity(w2);
  CHECK(loadCity(f4).raw->findInner(unkInner) != nullptr);
  TEST_MAIN_END();
}
