// Checks rules of the growth and utility spec revisions against the saved state of each shipped city, without running
// the simulation forward (issue #78):
//   * land value: recompute every block right after load and compare with the saved grid (mean ratio, correlation);
//   * Href and the city centre used by the land-value pass;
//   * the valve cap rule (spec 15 s3.2) from the saved amenity valves vs the saved capPct;
//   * unemployment (spec 20 s6) from the saved valves and cohort shares vs the saved chart sample;
//   * pump outputs after one water update vs the outputs saved with each pump (property 8351FDF3, high u16).
//   probe_spec_checks [GAME_DIR] [city-name-substring ...]
#include <cmath>
#include <map>
#include <cstring>
#include "probe_common.h"
#include "core/landvalue.h"
#include "core/records.h"
using namespace probe;

int main(int argc, char** argv) {
  fs::path root;
  int used = gameDirFromArgs(argc, argv, root);
  if (needInstall(root, "probe_spec_checks GAME_DIR [city ...]")) return 1;
  std::vector<std::string> filters;
  for (int i = 1 + used; i < argc; i++) filters.push_back(argv[i]);
  auto cities = findCities(root, filters);
  for (const auto& path : cities) {
    Session s;
    if (!s.open(root)) return 2;
    if (!s.load(path)) continue;
    City& c = s.game.city();
    if (c.valves.v3000.Slast <= 0) continue;
    std::printf("== %s\n", path.filename().string().c_str());

    {  // pollution record (garbage counters, landfill store, sea flags) must re-serialise byte-identically before any run
      const ResKey key{0x206C6E7C, 0xC0A81498, 0};
      const auto* rec = s.game.world().raw ? s.game.world().raw->findInner(key) : nullptr;
      if (rec) {
        Bytes out = c.pollution.serialize();
        size_t i = 0, nd = 0, first = SIZE_MAX;
        for (; i < out.size() && i < rec->data.size(); i++) if (out[i] != rec->data[i]) { nd++; if (first == SIZE_MAX) first = i; }
        std::printf("   pollution record round trip: %s (size %zu vs %zu, %zu bytes differ, first at %zd)\n", out == rec->data ? "identical" : "DIFFERENT",
                    out.size(), rec->data.size(), nd, first == SIZE_MAX ? ssize_t(-1) : ssize_t(first));
      }
    }
    // Saved values before anything runs.
    std::vector<uint8_t> saved = c.landValue.lv;
    uint8_t capSaved[3] = {c.valves.v3000.capPct, c.valves.v5000.capPct, c.valves.v8000.capPct};
    int capRule[3] = {growth::capFromAmenity(c.valves.v3001), growth::capFromAmenity(c.valves.v5001), growth::capFromAmenity(c.valves.v8001)};
    double w = c.pop.workforceFraction();
    int unSaved = c.valves.unemploymentPct();
    int unRule = growth::unemploymentPercent(c.valves.v3000.Slast, w, c.valves.v7000.Dlast);
    std::printf("   capPct saved R/C/I %d/%d/%d, rule %d/%d/%d;  unemployment saved %d rule %d (w %.4f, jobs S saved %d vs R*w+150 %.0f)\n",
                capSaved[0], capSaved[1], capSaved[2], capRule[0], capRule[1], capRule[2], unSaved, unRule, w,
                c.valves.world.v[0], double(c.valves.v3000.Slast) * w + 150);
    std::printf("   amenity demand multipliers 5001 %.2f 8001 %.2f\n", c.valves.amenityDemandMul[0], c.valves.amenityDemandMul[1]);

    if (std::getenv("M3K_GARBAGE")) {
      const auto& P = c.pollution;
      auto row = [](const char* nm, const int64_t* v) { std::printf("     %-8s", nm); for (int i = 0; i < 7; i++) std::printf(" %11lld", (long long)v[i]); std::printf("\n"); };
      row("totals", P.disp.totals); row("month", P.disp.month); row("hist[0]", P.disp.history[0]); row("hist[11]", P.disp.history[11]);
      const auto& st = P.disp.store;
      std::printf("     store Slast %d Dlast %d S %lld D %lld V %d; ratio %g burnA %u burnB %u landfillTiles %u\n", st.Slast, st.Dlast,
                  (long long)st.S, (long long)st.D, st.V, P.ratio, P.burnCapA, P.burnCapB, P.landfillTiles);
      int bit30 = 0; for (uint32_t v : P.state) if (v & 0x40000000u) bit30++;
      std::printf("     cells with sea flag %d\n", bit30);
    }
    {  // recycling ratio rule (spec 16 s4.3) at 100% funding vs the saved ratio
      us::IniReader t{&c.config().ini.tune};
      int64_t pct = t.num("RecyclingCenter", "OptimalPctReduction", 0), served = t.num("RecyclingCenter", "OptimalPopServed", 0);
      std::vector<us::PollutionLayer::RecyclingCentre> cs(c.pollution.recyclingCentres.size(), {pct, served});
      std::printf("   recycling: %zu centres, saved ratio %.4f, rule %.4f\n", cs.size(), c.pollution.ratio,
                  us::PollutionLayer::recyclingRatio(cs, c.valves.v3000.Slast));
    }
    // Land value.
    c.recomputeLandValue();
    const auto& ours = c.landValue.lv;
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, ratio = 0; int n = 0, nr = 0;
    double hiSaved = 0, hiOurs = 0; int nHi = 0;
    for (size_t i = 0; i < saved.size() && i < ours.size(); i++) {
      double a = saved[i], b = ours[i];
      sx += a; sy += b; sxx += a * a; syy += b * b; sxy += a * b; n++;
      if (a >= 8) { ratio += b / a; nr++; }
      if (a >= 200) { hiSaved += a; hiOurs += b; nHi++; }
    }
    double cov = sxy / n - (sx / n) * (sy / n), vx = sxx / n - (sx / n) * (sx / n), vy = syy / n - (sy / n) * (sy / n);
    int cx, cz; c.lvCentre(cx, cz);
    std::printf("   land value: mean saved %.1f ours %.1f, ratio ours/saved (saved>=8) %.3f, corr %.3f, high blocks (>=200) %d: %.1f vs %.1f;"
                " Href %d centre (%d,%d)\n",
                sx / n, sy / n, nr ? ratio / nr : 0.0, (vx > 0 && vy > 0) ? cov / std::sqrt(vx * vy) : 0.0, nHi, nHi ? hiSaved / nHi : 0.0,
                nHi ? hiOurs / nHi : 0.0, c.landValue.hillRef(), cx, cz);

    if (std::getenv("M3K_LV_EXPLAIN")) {   // the blocks with the largest shortfall
      std::vector<std::pair<int, size_t>> gap;
      for (size_t i = 0; i < saved.size(); i++) gap.push_back({int(saved[i]) - int(ours[i]), i});
      std::sort(gap.rbegin(), gap.rend());
      int bh = s.game.world().H / 2;
      for (int k = 0; k < 8 && k < int(gap.size()); k++) {
        size_t i = gap[size_t(k)].second; int bx = int(i) / bh, bz = int(i) % bh;
        std::printf("     block (%d,%d) saved %d ours %d: %s\n", bx * 2, bz * 2, saved[i], ours[i], c.lvExplain(bx * 2, bz * 2).c_str());
      }
    }
    // Pumps: saved output vs one monthly water update.
    std::map<std::pair<int, int>, int> savedOut;
    for (const auto& o : s.game.world().occupants)
      if (o.agentType == 12810)
        for (const auto& kv : o.intProps)
          if (kv.first == 0x8351FDF3) savedOut[{o.x, o.z}] = int(kv.second >> 16);
    long long savedSupply = (long long)c.water.stats.supply;
    c.runMonthlyWater();
    int pumps = 0, match = 0, zeroOurs = 0, zeroSaved = 0;
    for (const auto& f : c.water.facilities()) {
      if (f.kind != us::WaterKind::Pump) continue;
      auto it = savedOut.find({f.x, f.z});
      if (it == savedOut.end()) continue;
      pumps++;
      if (std::abs(it->second - f.output) <= 5) match++;
      if (f.output == 0) zeroOurs++;
      if (it->second == 0) zeroSaved++;
    }
    std::printf("   pumps %d: output within 5 of saved %d; zero output ours %d saved %d; supply ours %lld saved stat %lld\n", pumps, match,
                zeroOurs, zeroSaved, (long long)c.water.stats.supply, savedSupply);
  }
  return 0;
}
