// Builds a small city from empty land with the public game API (roads, zones, coal plant, pump, power, pipes) and runs
// it for 10 years. Prints population each year; zero or collapsing population means the basic growth loop is broken.
// The generated save goes to the temp directory; the install is only read.
//   probe_play_smoke [GAME_DIR]
#include <fstream>
#include "probe_common.h"
using namespace probe;

static std::vector<std::pair<int, int>> line(int x0, int z0, int x1, int z1) {
  std::vector<std::pair<int, int>> p;
  if (x0 == x1) for (int z = z0; z <= z1; z++) p.push_back({x0, z});
  else for (int x = x0; x <= x1; x++) p.push_back({x, z0});
  return p;
}

int main(int argc, char** argv) {
  fs::path root;
  gameDirFromArgs(argc, argv, root);
  if (needInstall(root, "probe_play_smoke GAME_DIR")) return 1;
  Session s;
  if (!s.open(root)) return 2;

  World w;
  w.init(96, 96);
  for (auto& a : w.alt) a = 100;
  w.seaLevel = 80;
  for (int x = 0; x <= 96; x++) for (int z = 0; z <= 6; z++) { w.alt[w.vIdx(x, z)] = 60; w.water[w.vIdx(x, z)] = 80; }   // a strip of sea
  w.info.cityName = "Probeville"; w.info.mayorName = "M"; w.info.year = 1950; w.info.foundingYear = 1950; w.info.fileKind = 3;
  w.view = {48, 48, 2, 0, true};
  fs::path file = fs::temp_directory_path() / "m3k_probe_play.sc3";
  { Bytes b = saveCity(w); std::ofstream f(file, std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()), long(b.size())); }
  if (!s.load(file)) { std::fprintf(stderr, "could not load the generated city\n"); return 2; }
  Sim& sim = s.game.sim();
  sim.funds = 2000000;

  for (int z = 20; z <= 80; z += 12) sim.buildNetwork(NetworkKind::Road, line(10, z, 86, z));
  for (int x = 10; x <= 86; x += 12) sim.buildNetwork(NetworkKind::Road, line(x, 20, x, 80));
  int zones = 0;
  for (int bz = 20; bz < 80; bz += 12)
    for (int bx = 10; bx < 86; bx += 12) {
      int type = ((bx / 12) + (bz / 12)) % 3;
      zones += sim.paintZoneType(bx + 1, bz + 1, bx + 11, bz + 11, type == 0 ? 1 : type == 1 ? 5 : 9);
    }
  bool plant = sim.plop(207, 14, 10, 0);    // coal power plant
  bool pump = sim.plop(220, 40, 7, 0);      // water pump
  sim.buildNetwork(NetworkKind::Power, line(14, 14, 14, 21));
  for (int z = 21; z <= 81; z += 12) sim.buildNetwork(NetworkKind::Power, line(9, z, 87, z));
  for (int x = 11; x <= 87; x += 12) sim.buildNetwork(NetworkKind::Power, line(x, 21, x, 81));
  sim.buildNetwork(NetworkKind::Pipe, line(40, 8, 40, 20));
  for (int x = 10; x <= 86; x += 12) sim.buildNetwork(NetworkKind::Pipe, line(x, 20, x, 80));
  for (int z = 20; z <= 80; z += 12) sim.buildNetwork(NetworkKind::Pipe, line(10, z, 86, z));
  std::printf("zones painted %d, plant %s, pump %s\n", zones, plant ? "ok" : "FAILED", pump ? "ok" : "FAILED");

  s.game.setPaused(false);
  for (int year = 0; year <= 10; year++) {
    int st[8] = {0};
    for (const auto& o : s.game.world().occupants) st[(o.state & ~0x200u) & 7]++;
    std::printf("year %2d  pop %6d  funds %9ld  occupied %5d abandoned %4d construction %4d\n", year, sim.population(), long(sim.funds), st[1], st[2], st[4]);
    for (int d = 0; d < 365; d++) sim.tickDay();
  }
  std::error_code ec;
  fs::remove(file, ec);
  return 0;
}
