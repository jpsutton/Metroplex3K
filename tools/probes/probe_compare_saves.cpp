// Compares our simulation with a save written by the original after running the same shipped city (spec 28 s5.1
// reference runs). Loads the shipped city, runs it to the observed save's date, loads the observed save in a second
// session and diffs the layers: land value, air and water pollution, crime, power and water service, lot states.
//   probe_compare_saves GAME_DIR <shipped-city-name> <observed-save.sc3> [x,z ...]
// The optional tiles are printed side by side.
#include <cmath>
#include <cstdlib>
#include <map>
#include "probe_common.h"
using namespace probe;

namespace {
struct Stat {
  const char* name;
  long long n = 0, diff = 0, oursHigh = 0;
  double sumOurs = 0, sumTheirs = 0, sumAbs = 0;
  void add(int ours, int theirs) {
    n++; sumOurs += ours; sumTheirs += theirs; sumAbs += std::abs(ours - theirs);
    if (ours != theirs) diff++;
    if (ours > theirs) oursHigh++;
  }
  void print() const {
    std::printf("   %-14s cells %lld: mean ours %.1f theirs %.1f, mean |diff| %.2f, differ %lld (ours higher %lld)\n", name, n,
                n ? sumOurs / n : 0.0, n ? sumTheirs / n : 0.0, n ? sumAbs / n : 0.0, diff, oursHigh);
  }
};
int absDay(const m3k::cal::Date& d) { return d.year * 400 + d.month * 32 + d.day; }
}  // namespace

int main(int argc, char** argv) {
  fs::path root;
  int used = gameDirFromArgs(argc, argv, root);
  if (needInstall(root, "probe_compare_saves GAME_DIR <city> <observed.sc3> [x,z ...]")) return 1;
  if (argc < 3 + used) { std::fprintf(stderr, "usage: probe_compare_saves GAME_DIR <city> <observed.sc3> [x,z ...]\n"); return 1; }
  auto cities = findCities(root, {argv[1 + used]});
  if (cities.empty()) { std::fprintf(stderr, "no matching city\n"); return 1; }
  fs::path observed = argv[2 + used];

  Session theirs;
  if (!theirs.open(root) || !theirs.load(observed)) { std::fprintf(stderr, "cannot load %s\n", observed.string().c_str()); return 2; }
  City& t = theirs.game.city();
  const auto target = t.clock.date();

  Session ours;
  if (!ours.open(root) || !ours.load(cities[0])) return 2;
  City& c = ours.game.city();
  int days = 0;
  while (absDay(c.clock.date()) < absDay(target) && days < 2000) { ours.game.sim().tickDay(); days++; }
  std::printf("== %s after %d days (date %d-%02d-%02d) vs %s\n", cities[0].filename().string().c_str(), days, c.clock.date().year,
              c.clock.date().month, c.clock.date().day, observed.filename().string().c_str());
  std::printf("   population ours %d theirs %d\n", c.population(), t.population());

  const auto& w = ours.game.world();
  const int W = w.W, H = w.H;
  Stat lv{"land value"}, air{"air"}, water{"water"}, crime{"crime"}, pw{"powered"}, wt{"watered"};
  for (int x = 0; x < W; x += 2)
    for (int z = 0; z < H; z += 2) {
      lv.add(c.landValue.landValueAtTile(x, z), t.landValue.landValueAtTile(x, z));
      air.add(c.pollution.airAt(x, z), t.pollution.airAt(x, z));
      water.add(c.pollution.waterAt(x, z), t.pollution.waterAt(x, z));
      crime.add(c.crime.crimeAt(x, z), t.crime.crimeAt(x, z));
    }
  for (int x = 0; x < W; x++)
    for (int z = 0; z < H; z++) {
      pw.add(c.power.powered(x, z) ? 1 : 0, t.power.powered(x, z) ? 1 : 0);
      wt.add(c.water.tileServiced(x, z) ? 1 : 0, t.water.tileServiced(x, z) ? 1 : 0);
    }
  for (const Stat* s : {&lv, &air, &water, &crime, &pw, &wt}) s->print();

  auto lots = [](const World& wd) {
    std::map<int, int> m;
    for (const auto& o : wd.occupants) if (o.agentType >= 3000 && o.agentType < 10000) m[int(o.state & ~0x200u)]++;
    return m;
  };
  auto lo = lots(w), lt = lots(theirs.game.world());
  std::printf("   R/C/I lots by state (ours/theirs):");
  for (int st : {1, 2, 4, 8, 16}) std::printf(" %d:%d/%d", st, lo[st], lt[st]);
  std::printf("\n");
  {
    const auto& tune = c.valves.tuning();
    for (const auto& kv : tune.connectionEffects) {
      std::printf("   connection kind %d:", kv.first);
      for (const auto& vi : kv.second) std::printf(" valve %d supply %d demand %d;", vi.valve, vi.supply, vi.demand);
      std::printf("\n");
    }
    std::printf("   our connection sets (rail, road, pipe, power, highway, subway):");
    for (int k = 0; k < 6; k++) std::printf(" %zu", c.neighbours.conn[k].size());
    std::printf("\n   neighbour counts nRoad/nRail/nHighway/nSubway:");
    for (const auto& n : c.neighbours.neighbours) std::printf(" [%u %u %u %u]", n.nRoad, n.nRail, n.nHighway, n.nSubway);
    std::printf("\n   initial connection supply com %d ind %d\n", tune.comInitialConnection, tune.indInitialConnection);
  }
  for (int id : {3000, 3001, 5000, 5001, 7000, 8000, 8001}) {
    const auto& a = c.valves.valve(id); const auto& b = t.valves.valve(id);
    std::printf("   valve %d: ours V %d cap %d S %d D %d | theirs V %d cap %d S %d D %d\n", id, a.V, a.capPct, a.Slast, a.Dlast, b.V, b.capPct, b.Slast, b.Dlast);
  }

  // Largest crime and air differences.
  struct D { int d, x, z; };
  for (int which = 0; which < 2; which++) {
    std::vector<D> v;
    for (int x = 0; x < W; x += 2)
      for (int z = 0; z < H; z += 2) {
        int a = which ? c.crime.crimeAt(x, z) : c.pollution.airAt(x, z), b = which ? t.crime.crimeAt(x, z) : t.pollution.airAt(x, z);
        v.push_back({a - b, x, z});
      }
    std::sort(v.begin(), v.end(), [](const D& a, const D& b) { return std::abs(a.d) > std::abs(b.d); });
    std::printf("   largest %s differences (ours - theirs):", which ? "crime" : "air");
    for (int i = 0; i < 6 && i < int(v.size()); i++) std::printf(" (%d,%d) %+d", v[size_t(i)].x, v[size_t(i)].z, v[size_t(i)].d);
    std::printf("\n");
  }
  for (int i = 3 + used; i < argc; i++) {
    int x, z;
    if (std::sscanf(argv[i], "%d,%d", &x, &z) != 2) continue;
    std::printf("   tile (%d,%d): crime potential %d/%d\n", x, z, c.crime.potentialAt(x / 2, z / 2), t.crime.potentialAt(x / 2, z / 2));
    std::printf("   tile (%d,%d): LV %d/%d air %d/%d water %d/%d crime %d/%d police %d/%d powered %d/%d watered %d/%d\n", x, z,
                c.landValue.landValueAtTile(x, z), t.landValue.landValueAtTile(x, z), c.pollution.airAt(x, z), t.pollution.airAt(x, z),
                c.pollution.waterAt(x, z), t.pollution.waterAt(x, z), c.crime.crimeAt(x, z), t.crime.crimeAt(x, z),
                c.police.coverageAt(x, z), t.police.coverageAt(x, z), c.power.powered(x, z), t.power.powered(x, z),
                c.water.tileServiced(x, z), t.water.tileServiced(x, z));
  }
  return 0;
}
