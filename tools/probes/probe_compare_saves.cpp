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
  // Cells still holding their loaded value after the run (sweep coverage check): air+water state and land value at load.
  std::vector<int> air0, water0, lv0;
  { const auto& w0 = ours.game.world();
    for (int x = 0; x < w0.W; x += 2) for (int z = 0; z < w0.H; z += 2) {
      air0.push_back(c.pollution.airAt(x, z)); water0.push_back(c.pollution.waterAt(x, z)); lv0.push_back(c.landValue.landValueAtTile(x, z)); } }
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
  {
    int oa = 0, ta = 0, ol = 0, tl = 0; size_t i = 0;
    for (int x = 0; x < W; x += 2) for (int z = 0; z < H; z += 2, i++) {
      bool oSame = c.pollution.airAt(x, z) == air0[i] && c.pollution.waterAt(x, z) == water0[i];
      bool tSame = t.pollution.airAt(x, z) == air0[i] && t.pollution.waterAt(x, z) == water0[i];
      oa += oSame; ta += tSame; ol += c.landValue.landValueAtTile(x, z) == lv0[i]; tl += t.landValue.landValueAtTile(x, z) == lv0[i];
    }
    std::printf("   cells unchanged since load: pollution ours %d theirs %d; land value ours %d theirs %d (of %zu)\n", oa, ta, ol, tl, i);
  }
  if (c.traffic && t.traffic) {  // traffic snapshots (what other subsystems read) and trip bitmaps, on tiles with a network in either city
    Stat ss{"snap surface"}, sh{"snap highway"}, ls{"live surface"}, tok{"trip ok"}, ttr{"trip tried"};
    const auto &A = c.traffic->rec(), &B = t.traffic->rec();
    for (int x = 0; x < W; x++)
      for (int z = 0; z < H; z++) {
        size_t i = size_t(x) * H + z;
        uint16_t fa = c.traffic->flags(x, z), fb = t.traffic->flags(x, z);
        if ((fa | fb) & 0x0015) { ss.add(A.snapSurface[i], B.snapSurface[i]); ls.add(A.liveSurface[i], B.liveSurface[i]); }
        if ((fa | fb) & 0x0002) sh.add(A.snapHighway[i], B.snapHighway[i]);
        if (A.tripTried[i] || B.tripTried[i]) { tok.add(A.tripOk[i], B.tripOk[i]); ttr.add(A.tripTried[i], B.tripTried[i]); }
      }
    { Stat em{"effect map"}; size_t n = std::min(c.valves.effectMap.size(), t.valves.effectMap.size());
      for (size_t i = 0; i < n; i++) if (c.valves.effectMap[i] || t.valves.effectMap[i]) em.add(c.valves.effectMap[i], t.valves.effectMap[i]);
      em.print(); }
    Stat sroad{"snap road only"}, srail{"snap rail/stn"};
    for (int x = 0; x < W; x++)
      for (int z = 0; z < H; z++) {
        size_t i = size_t(x) * H + z; uint16_t fb = t.traffic->flags(x, z);
        if ((fb & 0x0001) && !(fb & 0x0014)) sroad.add(A.snapSurface[i], B.snapSurface[i]);
        else if (fb & 0x0014) srail.add(A.snapSurface[i], B.snapSurface[i]);
      }
    for (const Stat* s : {&ss, &sroad, &srail, &sh, &ls, &tok, &ttr}) s->print();
    std::printf("   trips attempted/failed ours %u/%u theirs %u/%u; failure ratio ours %.3f theirs %.3f\n", A.tripsAttempted, A.tripsFailed,
                B.tripsAttempted, B.tripsFailed, A.failureRatio, B.failureRatio);
    {  // trip generator census of their city: spec 18 4.1 categories by parent chain vs our type/1000 rule
      const auto& hier = t.agentHierarchy();
      std::map<int, int> spec, mine, specFunctional;
      for (const auto& o : theirs.game.world().occupants) {
        std::vector<uint32_t> chain{o.agentType};
        uint32_t p = o.agentType;
        for (int g = 0; g < 32 && hier.parentOf(p, p); g++) chain.push_back(p);
        int cs = categoryOfTypeChain(chain);
        uint32_t k = o.agentType / 1000;
        int cm = k == 3 ? 0 : k == 5 ? 1 : k == 8 ? 2 : -1;
        spec[cs]++; mine[cm]++;
        if ((o.state & ~0x200u) == 1) specFunctional[cs]++;
      }
      std::printf("   trip generators (their city) by category -1/R/C/I/Other: spec");
      for (int k = -1; k < 4; k++) std::printf(" %d", spec[k]);
      std::printf(" (functional");
      for (int k = -1; k < 4; k++) std::printf(" %d", specFunctional[k]);
      std::printf(") | ours");
      for (int k = -1; k < 4; k++) std::printf(" %d", mine[k]);
      std::printf("; transit funding ours %d theirs %d, mass chance %d", c.traffic->transitFunding(), t.traffic->transitFunding(), t.traffic->massChance());
      std::printf("; TrafficTripThresholdPercent %d; radii R/C/I/D %d/%d/%d/%d\n", t.traffic->params().tripThresholdPercent, t.traffic->radius(0), t.traffic->radius(1), t.traffic->radius(2), t.traffic->radius(3));
    }
    if (std::getenv("M3K_NETCENSUS")) {  // occupant types with a transport ancestor, and network piece ids, in their city
      const auto& hier = t.agentHierarchy();
      std::map<uint32_t, int> occ, surf, sub;
      for (const auto& o : theirs.game.world().occupants)
        for (uint32_t a : {16000u, 16001u, 17202u, 17000u, 17400u, 12200u})
          if (hier.isAncestorOrSelf(o.agentType, a)) { occ[o.agentType]++; break; }
      for (const auto& p : theirs.game.world().surface) surf[p.pieceId]++;
      for (const auto& p : theirs.game.world().subway) sub[p.pieceId]++;
      std::printf("   transport occupants:"); for (auto& kv : occ) std::printf(" %u:%d", kv.first, kv.second);
      std::printf("\n   surface pieces (%zu kinds):", surf.size()); for (auto& kv : surf) std::printf(" %u:%d", kv.first, kv.second);
      std::printf("\n   subway pieces:"); for (auto& kv : sub) std::printf(" %u:%d", kv.first, kv.second);
      for (uint32_t a : {17202u, 16001u, 16000u, 17000u, 17400u}) {
        std::printf("\n   surface pieces under %u:", a);
        for (auto& kv : surf) if (hier.isAncestorOrSelf(kv.first, a)) std::printf(" %u:%d", kv.first, kv.second);
      }
      { std::printf("\n   chains:"); for (uint32_t id : {73u, 11258u, 15051u, 15098u, 14010u, 15045u, 15046u, 15106u, 29u}) { std::printf(" [%u", id); for (uint32_t c : hier.chain(id)) if (c != id) std::printf("<%u", c); std::printf("]"); } }
      {  // surface pieces that got no road/highway/rail flag, and pieces over water
        const auto& tw = theirs.game.world();
        std::map<uint32_t, int> unflagged, overWater;
        for (const auto& p : tw.surface) {
          if (p.x < 0 || p.z < 0 || p.x >= W || p.z >= H) continue;
          if (!(t.traffic->flags(p.x, p.z) & 0x7)) unflagged[p.pieceId]++;
          bool wet = false;
          for (int dx = 0; dx < 2; dx++) for (int dz = 0; dz < 2; dz++) if (tw.waterAt(p.x + dx, p.z + dz) > tw.altAt(p.x + dx, p.z + dz)) wet = true;
          if (wet) overWater[p.pieceId]++;
        }
        std::printf("\n   surface pieces without road/hwy/rail flag:"); for (auto& kv : unflagged) std::printf(" %u:%d", kv.first, kv.second);
        std::printf("\n   surface pieces over water:"); for (auto& kv : overWater) std::printf(" %u:%d", kv.first, kv.second);
      }
      {  // our failed lots (first tile) vs theirs
        const auto& A = c.traffic->rec(); const auto& B = t.traffic->rec();
        int bothFail = 0, oursOnly = 0, theirsOnly = 0; std::vector<std::pair<int, int>> ex;
        for (const auto& o : w.occupants) {
          size_t i = size_t(o.x) * H + o.z;
          if (i >= A.tripTried.size()) continue;
          bool fo = A.tripTried[i] && !A.tripOk[i], ft = B.tripTried[i] && !B.tripOk[i];
          if (fo && ft) bothFail++; else if (fo) { oursOnly++; if (ex.size() < 12) { ex.push_back({o.x, o.z}); std::printf("\n   ours-only fail lot (%d,%d) type %u size %dx%d state %u", o.x, o.z, o.agentType, o.sizeX, o.sizeZ, o.state); } } else if (ft) theirsOnly++;
        }
        if (const char* r = std::getenv("M3K_TRAFDUMP")) {  // x0,z0,x1,z1: our flags (hex), trip result o/X/., and connection tiles
          int x0, z0, x1, z1;
          if (std::sscanf(r, "%d,%d,%d,%d", &x0, &z0, &x1, &z1) == 4) {
            std::printf("\n   our flags / result rows z, columns x %d..%d:", x0, x1);
            for (int z = z0; z <= z1; z++) {
              std::printf("\n   z%3d ", z);
              for (int x = x0; x <= x1; x++) {
                size_t i = size_t(x) * H + z; uint16_t f = c.traffic->flags(x, z);
                char res = A.tripTried[i] ? (A.tripOk[i] ? 'o' : 'X') : '.';
                char rt = B.tripTried[i] ? (B.tripOk[i] ? 'o' : 'X') : '.';
                std::printf(" %04X%c%c", f, res, rt);
              }
            }
          }
        }
        if (std::getenv("M3K_TRIPTEST")) {  // route from every functional generator in their city with our router, car only, per (origin, destination)
          long long n[4][3] = {}, ok[4][3] = {}; std::map<uint32_t, std::pair<int, int>> failTypes;
          for (const auto& o : theirs.game.world().occupants) {
            int cat = categoryOfTypeChain(t.agentHierarchy().chain(o.agentType));
            if (cat < 0 || (o.state & ~0x200u) != 1) continue;
            for (int dest = 0; dest < 3; dest++) {
              TripRequest q; q.origin = TripRect{o.x, o.z, o.x + o.sizeX - 1, o.z + o.sizeZ - 1}; q.originCategory = cat; q.dest = dest;
              q.rail = q.bus = q.subway = false;
              { bool sub; TrafficSim::permissions(cat, q.rail, q.bus, q.subway, sub); if (sub) q.rail = q.bus = q.subway = false; }
              n[cat][dest]++;
              bool good = t.traffic->routeTrip(q).success;
              if (good) ok[cat][dest]++;
              auto& ft = failTypes[o.agentType]; ft.first++; if (!good) ft.second++;
            }
          }
          static const char* cn[] = {"res", "com", "ind", "other"};
          for (int c2 = 0; c2 < 4; c2++) {
            std::printf("\n   our router on their city, %s ->", cn[c2]);
            for (int d = 0; d < 3; d++) std::printf(" %s %.3f", cn[d], double(ok[c2][d]) / std::max(1LL, n[c2][d]));
            std::printf(" (n %lld)", n[c2][0]);
          }
          {  // their and our saved trip results on the lots of the worst types
            const auto& A2 = c.traffic->rec(); const auto& B2 = t.traffic->rec();
            for (uint32_t ty : {12310u, 12850u, 12330u, 5500u}) {
              int tried = 0, okk = 0, otried = 0, ook = 0, cnt = 0;
              for (const auto& o : theirs.game.world().occupants) {
                if (o.agentType != ty) continue;
                cnt++; size_t i = size_t(o.x) * H + o.z;
                tried += B2.tripTried[i]; okk += B2.tripTried[i] && B2.tripOk[i];
                otried += A2.tripTried[i]; ook += A2.tripTried[i] && A2.tripOk[i];
              }
              std::printf("\n   type %u lots %d: theirs tried %d ok %d | ours tried %d ok %d", ty, cnt, tried, okk, otried, ook);
            }
          }
          std::vector<std::pair<int, uint32_t>> worst;
          for (auto& kv : failTypes) if (kv.second.second) worst.push_back({kv.second.second, kv.first});
          std::sort(worst.rbegin(), worst.rend());
          std::printf("\n   most failing types (fails/tries):");
          for (size_t i = 0; i < worst.size() && i < 12; i++) std::printf(" %u:%d/%d", worst[i].second, worst[i].first, failTypes[worst[i].second].first);
        }
        std::printf("\n   failed lots: both %d ours only %d theirs only %d; ours-only examples:", bothFail, oursOnly, theirsOnly);
        for (auto& e : ex) std::printf(" (%d,%d)", e.first, e.second);
      }
      std::printf("\n   flag census theirs: road %d hwy %d rail %d subway %d station %d access %d span %d propA %d\n", [&] { int n = 0; for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) n += (t.traffic->flags(x, z) & 1) != 0; return n; }(),
                  [&] { int n = 0; for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) n += (t.traffic->flags(x, z) & 2) != 0; return n; }(),
                  [&] { int n = 0; for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) n += (t.traffic->flags(x, z) & 4) != 0; return n; }(),
                  [&] { int n = 0; for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) n += (t.traffic->flags(x, z) & 8) != 0; return n; }(),
                  [&] { int n = 0; for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) n += (t.traffic->flags(x, z) & 0x10) != 0; return n; }(),
                  [&] { int n = 0; for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) n += (t.traffic->flags(x, z) & 0x40) != 0; return n; }(),
                  [&] { int n = 0; for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) n += (t.traffic->flags(x, z) & 0x4000) != 0; return n; }(),
                  [&] { int n = 0; for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) n += (t.traffic->flags(x, z) & 0x100) != 0; return n; }());
    }
  }

  auto lots = [](const World& wd) {
    std::map<int, int> m;
    for (const auto& o : wd.occupants) if (o.agentType >= 3000 && o.agentType < 10000) m[int(o.state & ~0x200u)]++;
    return m;
  };
  auto lo = lots(w), lt = lots(theirs.game.world());
  {  // abandoned (state 2) lots by agent type
    std::map<uint32_t, std::pair<int, int>> ab;
    for (const auto& o : w.occupants) if ((o.state & ~0x200u) == 2) ab[o.agentType].first++;
    for (const auto& o : theirs.game.world().occupants) if ((o.state & ~0x200u) == 2) ab[o.agentType].second++;
    std::printf("   abandoned by type (ours/theirs):");
    for (auto& kv : ab) std::printf(" %u:%d/%d", kv.first, kv.second.first, kv.second.second);
    std::printf("\n");
  }
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

  {  // Their saved crime vs our crime rule applied to their saved potential and our police coverage of their city.
    long long n = 0, hi = 0, lo = 0; double sum = 0;
    auto pol = [&](int x, int z) { return t.police.coverageAt(x, z); };
    for (int cx = 0; cx < W / 2; cx++)
      for (int cz = 0; cz < H / 2; cz++) {
        if (t.crime.potentialAt(cx, cz) <= 0) continue;
        int saved = t.crime.crimeAt(cx * 2, cz * 2);
        int mine = t.crime.cellValue(cx, cz, pol, t.crimeOrdinances());
        n++; sum += mine - saved; if (mine > saved) hi++; else if (mine < saved) lo++;
      }
    std::printf("   crime rule on their save: %lld cells with P > 0, mean (ours - saved) %.2f, higher %lld, lower %lld\n", n, n ? sum / n : 0.0, hi, lo);
    // Implied police term from the saved cells with 0 < C < 255 (no ordinance term): P - 4C (+-3) vs our term P - v.
    double si = 0, so = 0; long long m = 0; std::map<int, int> ratioHist;
    for (int cx = 0; cx < W / 2; cx++)
      for (int cz = 0; cz < H / 2; cz++) {
        int P = t.crime.potentialAt(cx, cz), C = t.crime.crimeAt(cx * 2, cz * 2);
        if (P <= 0 || C <= 0 || C >= 255) continue;
        int mine = t.crime.cellValue(cx, cz, pol, us::OrdinanceSet());
        double implied = P - 4.0 * C - 1.5, ourTerm = P - 4.0 * mine - 1.5;
        if (ourTerm <= 0) continue;
        si += implied; so += ourTerm; m++;
        ratioHist[int(std::lround(implied / ourTerm * 10))]++;
      }
    { int ji = 0, jc = 0; for (const auto& j : c.police.jails()) { ji += j.inmates; jc += j.capacity; }
      int ti = 0, tc = 0; for (const auto& j : t.police.jails()) { ti += j.inmates; tc += j.capacity; }
      std::printf("   police jails ours %zu inmates %d/%d unjailed %d | theirs %zu inmates %d/%d unjailed %d\n", c.police.jails().size(), ji, jc, c.police.unjailed,
                  t.police.jails().size(), ti, tc, t.police.unjailed); }
    { us::IniReader pr{&c.config().ini.police}; std::printf("   SC3Police NoJailPenalty %lld ConvictionRate %lld DailyJailReleaseRate %lld MaxJailOvercrowding %lld\n",
        (long long)pr.num("TuningParameters", "NoJailPenalty", -1), (long long)pr.num("TuningParameters", "ConvictionRate", -1),
        (long long)pr.num("TuningParameters", "DailyJailReleaseRate", -1), (long long)pr.num("TuningParameters", "MaxJailOvercrowding", -1)); }
    {  // mean police D over developed 4x4 cells (any occupant tile) vs the saved average coverage of the last full sweep
      auto meanD = [&](const City& cc, const World& wd) {
        long long sum = 0, n = 0;
        for (int x = 0; x < W; x += 4) for (int z = 0; z < H; z += 4) {
          bool dev = false;
          for (int dx = 0; dx < 4 && !dev; dx++) for (int dz = 0; dz < 4; dz++) if (cc.occupantAt(x + dx, z + dz) >= 0) { dev = true; break; }
          if (!dev) continue;
          sum += cc.police.coverageAt(x, z); n++;
        }
        (void)wd; return n ? double(sum) / n : 0.0;
      };
      std::printf("   police mean D over developed cells: ours %.1f, our model on their save %.1f; their saved avgCoverage %u pct %u\n",
                  meanD(c, w), meanD(t, theirs.game.world()), t.police.avgCoverage, t.police.pctCovered);
    }
    for (const auto& o : theirs.game.world().occupants)
      if (o.agentType == 13881) { std::printf("   jail (%d,%d) props:", o.x, o.z); for (const auto& kv : o.intProps) std::printf(" %08X=%u", kv.first, kv.second); std::printf("\n"); break; }
    std::printf("   police: ours striking %d prisoners-unjailed? / theirs striking %d; funding ours %d theirs %d; stations %d/%d\n", c.police.strike.striking ? 1 : 0,
                t.police.strike.striking ? 1 : 0, c.police.funding(), t.police.funding(), c.police.stationCount(), t.police.stationCount());
    std::printf("   police term (sum of 4 tiles / PoliceFactor): implied %.1f vs ours %.1f over %lld cells; ratio x10 histogram:", m ? si / m : 0, m ? so / m : 0, m);
    int shown = 0; for (const auto& kv : ratioHist) if (kv.second > m / 50 && shown++ < 12) std::printf(" %d:%d", kv.first, kv.second);
    std::printf("\n");
  }
  if (t.traffic && std::getenv("M3K_AIRFP")) {  // Air fixed-point test on their save: run their pollution layer for two sweep periods with each candidate
                    // traffic input (spec 17 7.2.4 step 3) and measure how far their saved air moves. The right input moves it least.
    const auto& R = t.traffic->rec();
    const size_t G = size_t(W) * H;
    auto pick = [&](int k) {
      std::vector<uint8_t> v(G, 0);
      for (size_t i = 0; i < G; i++) {
        switch (k) {
          case 1: v[i] = R.snapSurface[i]; break;
          case 2: v[i] = std::max({R.snapSurface[i], R.snapHighway[i], R.snapSubway[i]}); break;
          case 3: v[i] = uint8_t(std::min(255, R.snapSurface[i] + R.snapHighway[i])); break;
          case 4: v[i] = R.liveSurface[i]; break;
          case 5: v[i] = std::max({R.liveSurface[i], R.liveHighway[i], R.liveSubway[i]}); break;
          case 6: case 7: {  // surface on road (not rail / station) tiles, + highway for 7
            int x = int(i / H), z = int(i % H);
            uint16_t f = t.traffic->flags(x, z);
            int s = ((f & 0x0001) && !(f & 0x0014)) ? R.snapSurface[i] : 0;
            v[i] = uint8_t(std::min(255, s + (k == 7 ? R.snapHighway[i] : 0)));
            break;
          }
          case 8: case 9: {  // spec 18 7.1 tile value, mask road 0x01 (+ highway 0x02 for 9): max over the requested layers
            int x = int(i / H), z = int(i % H);
            uint16_t f = t.traffic->flags(x, z);
            int s = (f & 0x0001) ? R.snapSurface[i] : 0;
            if (k == 9 && (f & 0x0002)) s = std::max<int>(s, R.snapHighway[i]);
            v[i] = uint8_t(s);
            break;
          }
          default: break;
        }
      }
      return v;
    };
    static const char* names[] = {"none", "snap surface", "snap max(s,h,sub)", "snap s+h", "live surface", "live max", "snap road", "snap road+h", "tile(0x01)", "tile(0x03)"};
    std::printf("   air fixed point on their save (TrafficAirFactor %d, UpdatePeriod %d):", t.pollution.params().trafficAirFactor,
                t.pollution.params().updatePeriod);
    for (int k = 0; k < 10; k++) {
      us::PollutionLayer p = t.pollution;
      std::vector<uint8_t> tr = pick(k);
      us::PollutionEnv env; env.zone = &theirs.game.world().zone; env.traffic = &tr; env.ordinances = t.crimeOrdinances();
      for (int d = 0; d < 2 * std::max(1, t.pollution.params().updatePeriod); d++) p.run(env);
      double s = 0, sd = 0; long long n = 0;
      double sdNo = 0, sdHw = 0, sdSf = 0; long long nNo = 0, nHw = 0, nSf = 0;
      for (int x = 0; x < W; x += 2) for (int z = 0; z < H; z += 2) {
        int a = p.airAt(x, z), b = t.pollution.airAt(x, z); s += std::abs(a - b); sd += a - b; n++;
        int sf = 0, hw = 0;
        for (int dx = 0; dx < 2; dx++) for (int dz = 0; dz < 2; dz++) { size_t i = size_t(x + dx) * H + z + dz; sf += R.snapSurface[i]; hw += R.snapHighway[i] + R.snapSubway[i]; }
        if (!sf && !hw) { sdNo += a - b; nNo++; } else if (!sf) { sdHw += a - b; nHw++; } else if (!hw) { sdSf += a - b; nSf++; }
      }
      std::printf(" %s |d| %.1f mean %+.1f [no traffic %+.1f /%lld, highway/subway only %+.1f /%lld, surface only %+.1f /%lld];", names[k], s / n,
                  sd / n, nNo ? sdNo / nNo : 0, nNo, nHw ? sdHw / nHw : 0, nHw, nSf ? sdSf / nSf : 0, nSf);
    }
    std::printf("\n");
    {  // Water fixed point (independent of traffic): split by the cell's water source sign.
      us::PollutionLayer p = t.pollution;
      us::PollutionEnv env; env.zone = &theirs.game.world().zone; env.ordinances = t.crimeOrdinances();
      for (int d = 0; d < 2 * std::max(1, t.pollution.params().updatePeriod); d++) p.run(env);
      const auto src = t.pollution.sourceGrids();
      double sd[3] = {}; long long n[3] = {};
      for (int x = 0; x < W; x += 2) for (int z = 0; z < H; z += 2) {
        int a = p.waterAt(x, z), b = t.pollution.waterAt(x, z);
        int ws = src.water[size_t(x / 2) * (H / 2) + z / 2];
        int k = ws > 0 ? 0 : ws < 0 ? 1 : 2;
        sd[k] += a - b; n[k]++;
      }
      std::printf("   water fixed point on their save: source>0 %+.1f /%lld, source<0 %+.1f /%lld, source=0 %+.1f /%lld\n", n[0] ? sd[0] / n[0] : 0,
                  n[0], n[1] ? sd[1] / n[1] : 0, n[1], n[2] ? sd[2] / n[2] : 0, n[2]);
      std::printf("   water treatment: plants theirs %zu ours %zu, cap/plant %d; saved rawLast %d treatedLast %d sumWater %d | after run rawLast %d "
                  "treatedLast %d sumWater %d; ordinances CC %d LCB %d IPF %d\n",
                  t.pollution.treatmentPlants.size(), c.pollution.treatmentPlants.size(), t.pollution.params().treatmentCap,
                  t.pollution.rawWaterLast, t.pollution.treatedLast, t.pollution.sumWater, p.rawWaterLast, p.treatedLast, p.sumWater,
                  env.ordinances.active(0xA2F6E7DA) ? 1 : 0, env.ordinances.active(0x62F6E808) ? 1 : 0, env.ordinances.active(0x02F6E7E2) ? 1 : 0);
    }
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
