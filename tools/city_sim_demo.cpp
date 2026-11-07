// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Headless growth check: generated terrain, a flattened building site with a road grid, R/C/I zones and
// a power plant wired through the Sim facade; runs N simulated years and prints yearly stats + a PNG.
//   city_sim_demo [--game DIR] [--seed N] [--years N] [--size 128] out.png
// Without --game a synthetic family/valve configuration is used (see test_city.cpp), so buildings are
// placeholder occupants without sprites; the PNG then shows zone/road tint only.
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include "core/render.h"
#include "core/sim.h"
#include "core/terrain_gen.h"
#include "png.h"
using namespace m3k;

static CityConfig syntheticConfig() {
  CityConfig c;
  c.growth.parseZoneDescriptions(IniFile::fromText(
      "[ZoneDeveloperDescriptions]\n1=0x619BA64E,3000:3100,1,255,0;3300,1,255,100\n5=0x41A3ADC1,5000:5100,1,255,0\n"
      "9=0xE1A53B30,8000:8100,D,1,255,0\n"));
  auto binds = c.growth.parseRuleBindings(IniFile::fromText(
      "[ZoneDeveloperRules]\n1,0xA22653C0=81F53D09,01F58D3D,4;81F53D09,01F58D3D,11\n5,0x422C9D52=81F53D09,01F58D3D,7\n"
      "9,0x422C9D53=81F53D09,01F58D3D,9\n"));
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

int main(int argc, char** argv) {
  std::string game, out = "city_sim.png";
  int seed = 1, years = 10, size = 128;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--game" && i + 1 < argc) game = argv[++i];
    else if (a == "--seed" && i + 1 < argc) seed = atoi(argv[++i]);
    else if (a == "--years" && i + 1 < argc) years = atoi(argv[++i]);
    else if (a == "--size" && i + 1 < argc) size = atoi(argv[++i]);
    else out = a;
  }
  GameData data; GameData* dp = nullptr;
  CityConfig cfg = syntheticConfig();
  if (!game.empty()) {
    std::string err;
    if (data.open(game, err)) { dp = &data; cfg = CityConfig(); cfg.load(data); } else std::cerr << err << "\n";
  }
  World w;
  TerrainGenOptions to; to.seed = uint32_t(seed);
  generateTerrain(w, size, size, to);
  w.info.year = 1950; w.info.foundingYear = 1950; w.info.dayOfYear = 1; w.info.cityName = "Demo"; w.info.mayorName = "Mayor";
  // Flatten a building site in the middle: generated terrain is hilly and buildings need flat dry land.
  const int cx = size / 2, S = 36;
  const int x0 = cx - S, x1 = cx + S, z0 = cx - S / 2, z1 = cx + S / 2;
  const int base = std::max<int>(w.seaLevel + 10, w.altAt(cx, cx));
  for (int x = x0 - 2; x <= x1 + 2; x++)
    for (int z = z0 - 2; z <= z1 + 2; z++)
      if (x >= 0 && z >= 0 && x <= size && z <= size) { w.alt[size_t(w.vIdx(x, z))] = uint8_t(base); w.water[size_t(w.vIdx(x, z))] = 0; }
  for (int x = x0 - 2; x <= x1 + 2; x++)
    for (int z = z0 - 2; z <= z1 + 2; z++)
      if (w.inTiles(x, z)) w.flora[w.tIdx(x, z)] = 0;
  Sim s; s.init(&w, dp, &cfg, uint32_t(seed));
  City& c = s.city();

  int ok = 0;
  ok += s.buildRoad(line(x0, z0 + 6, x1, z0 + 6));
  ok += s.buildRoad(line(x0, z0 + 18, x1, z0 + 18));
  for (int x = x0 + 2; x <= x1; x += 12) ok += s.buildRoad(line(x, z0 + 6, x, z0 + 18));
  // R west, C centre, I east; each block above and below the roads
  const int third = (x1 - x0) / 3;
  const int zr[2][2] = {{z0 + 7, z0 + 9}, {z0 + 15, z0 + 17}};
  for (int b = 0; b < 2; b++) {
    ok += s.paintZoneType(x0, zr[b][0], x0 + third, zr[b][1], 1);
    ok += s.paintZoneType(x0 + third + 3, zr[b][0], x0 + 2 * third, zr[b][1], 5);
    ok += s.paintZoneType(x0 + 2 * third + 3, zr[b][0], x1, zr[b][1], 9);
  }
  // plant north of the roads, line crossing both roads into the zone blocks
  const int px = cx, pz = z0 - 1;
  c.plopAgent(900, 12111, 4, 4, px, pz, 0);
  ok += s.buildNetwork(NetworkKind::Power, line(px + 1, pz + 4, px + 1, z0 + 17));
  ok += s.buildNetwork(NetworkKind::Power, line(x0, z0 + 8, x1, z0 + 8));
  ok += s.buildNetwork(NetworkKind::Power, line(x0, z0 + 16, x1, z0 + 16));
  std::cout << "setup ok actions=" << ok << " roads=" << s.roadCount() << " funds=" << s.funds << "\n";

  for (int y = 0; y < years; y++) {
    for (int d = 0; d < 365; d++) s.tickDay();
    int r = 0, cm = 0, in = 0;
    for (auto& o : w.occupants) { uint32_t k = o.agentType / 1000; if (k == 3) r++; else if (k == 5) cm++; else if (k == 8) in++; }
    std::printf("year %d: pop=%d R/C/I bld=%d/%d/%d valves=%d/%d/%d funds=%lld powered=%d\n", w.info.year, s.population(), r, cm, in,
                s.valveR.value, s.valveC.value, s.valveI.value, (long long)s.funds, c.powered(x0 + 2, z0 + 8) ? 1 : 0);
  }
  std::printf("%s", c.debugStats().c_str());

  Renderer r; r.setWorld(&w, dp); r.resize(1280, 800);
  Camera cam; cam.zoom = 2; cam.rot = 0;
  r.centreOnWorld(cam, float(cx), float(cx));
  RenderOptions opt;
  r.render(cam, opt, nullptr);
  writePng(out, r.width(), r.height(), r.pixels());
  std::cout << "wrote " << out << "\n";
  return 0;
}
