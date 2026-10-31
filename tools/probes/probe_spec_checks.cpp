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
    for (const auto& d : c.neighbours.active)
      std::printf("   deal: type %d neighbour %d quantity %d monthly %d %s\n", d.dealType, d.neighbourIndex, d.quantity, d.monthlyAmount,
                  d.terminated() ? "terminated" : "");
    if (std::getenv("M3K_POWER")) {
      int tx, tz; bool ok = c.connectionTile(3, 0, tx, tz);
      std::printf("   power conn to neighbour 0: %d (%d,%d); conn sets sizes power %zu pipe %zu\n", ok, tx, tz, c.neighbours.conn[3].size(), c.neighbours.conn[2].size());
      const auto& ps = c.power.stats();
      std::printf("   power before: produced %lld consumed %lld\n", (long long)ps.produced, (long long)ps.consumed);
      for (int d = 0; d < 35; d++) s.game.sim().tickDay();
      const auto& p2 = c.power.stats();
      std::printf("   power after month: produced %lld consumed %lld\n", (long long)p2.produced, (long long)p2.consumed);
      for (const auto& d : c.power.deals()) std::printf("   deal slot (%d,%d) amount %d reached %d active %d unused %lld\n", d.tx, d.tz, d.amount, d.reached, d.active, (long long)d.unused);
      int unp = 0, tot = 0;
      for (const auto& o : s.game.world().occupants) if (o.agentType >= 3000 && o.agentType < 10000 && (o.state & ~0x200u) == 1) { tot++; if (!c.power.powered(o.x, o.z)) unp++; }
      std::printf("   occupied RCI %d unpowered %d\n", tot, unp);
      continue;
    }
    if (std::getenv("M3K_TRIPS") && c.traffic) {   // orientation check of the saved trip bitmaps: tried bits on building anchors
      const auto& rec = c.traffic->rec();
      const int W = s.game.world().W, H = s.game.world().H;
      std::vector<uint8_t> anchor(size_t(W) * H, 0);
      for (const auto& o : s.game.world().occupants) anchor[size_t(o.x) * H + o.z] = 1;
      int tried = 0; for (auto v : rec.tripTried) tried += v;
      for (int v = 0; v < 8; v++) {
        int hit = 0, failed = 0;
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) {
          if (!rec.tripTried[size_t(x) * H + z]) continue;
          int tx = x, tz = z;
          if (v & 1) tx = W - 1 - tx;
          if (v & 2) tz = H - 1 - tz;
          if (v & 4) std::swap(tx, tz);
          if (anchor[size_t(tx) * H + tz]) { hit++; if (!rec.tripOk[size_t(x) * H + z]) failed++; }
        }
        std::printf("     trips variant %d: %d of %d tried bits on anchors (%d failed)\n", v, hit, tried, failed);
      }
      continue;
    }
    if (std::getenv("M3K_WATER")) {   // watered occupied R/C/I lots: saved bitmap at load vs after our month-start distribution
      auto dry = [&]() {
        int n = 0, tot = 0;
        for (const auto& o : s.game.world().occupants)
          if (o.agentType >= 3000 && o.agentType < 10000 && (o.state & ~0x200u) == 1) { tot++; if (!c.water.tileServiced(o.x, o.z)) n++; }
        return std::make_pair(n, tot);
      };
      {  // orientation check of the saved bitmap: dry counts under the 8 square symmetries
        const int W = s.game.world().W, H = s.game.world().H;
        for (int v = 0; v < 8; v++) {
          int n = 0;
          for (const auto& o : s.game.world().occupants) {
            if (!(o.agentType >= 3000 && o.agentType < 10000 && (o.state & ~0x200u) == 1)) continue;
            int x = o.x, z = o.z;
            if (v & 1) x = W - 1 - x;
            if (v & 2) z = H - 1 - z;
            if (v & 4) std::swap(x, z);
            if (!c.water.tileServiced(x, z)) n++;
          }
          std::printf("     variant %d dry %d\n", v, n);
        }
      }
      auto a = dry();
      std::printf("   water at load: dry %d of %d occupied RCI; supply %lld used %lld\n", a.first, a.second, (long long)c.water.stats.supply, (long long)c.water.stats.used);
      for (int d = 0; d < 35; d++) s.game.sim().tickDay();
      auto b = dry();
      std::printf("   water after month: dry %d of %d; supply %lld used %lld\n", b.first, b.second, (long long)c.water.stats.supply, (long long)c.water.stats.used);
      continue;
    }
    if (std::getenv("M3K_RESULTS")) {   // zone-manager result codes: saved previous pass vs our first full pass (10 s6.4)
      auto dump = [](const char* nm, const growth::ZoneManager::ResultMap& m) {
        std::map<int, uint32_t> byCode;
        for (const auto& kv : m) byCode[kv.first.first] += kv.second;
        std::printf("     %-12s", nm);
        for (const auto& kv : byCode) std::printf(" %d:%u", kv.first, kv.second);
        std::printf("\n");
      };
      std::printf("     committed P: 3000 %d 5000 %d 8000 %d\n", c.zones.committed(3000), c.zones.committed(5000), c.zones.committed(8000));
      dump("saved prev", c.zones.resultPrev);
      const auto savedRes = c.zones.resultPrev;
      const int savedV[3] = {c.valves.v3000.V, c.valves.v5000.V, c.valves.v8000.V};
      for (int d = 0; d < 62; d++) {
        s.game.sim().tickDay();
        if (d % 10 == 9)
          std::printf("     day %2d: I V %5d P %6d  (V-P %6d)  C V %5d P %6d  R V %5d P %6d\n", d + 1, c.valves.v8000.V, c.zones.committed(8000),
                      c.valves.v8000.V - c.zones.committed(8000), c.valves.v5000.V, c.zones.committed(5000), c.valves.v3000.V, c.zones.committed(3000));
      }
      dump("ours prev", c.zones.resultPrev);
      std::printf("     code 3 by zone (saved -> ours):");
      std::map<int, std::pair<uint32_t, uint32_t>> z3;
      for (const auto& kv : savedRes) if (kv.first.first == 3) z3[kv.first.second].first += kv.second;
      for (const auto& kv : c.zones.resultPrev) if (kv.first.first == 3) z3[kv.first.second].second += kv.second;
      for (const auto& kv : z3) if (kv.second.first || kv.second.second) std::printf(" z%d %u->%u", kv.first, kv.second.first, kv.second.second);
      std::printf("\n     valves after: R V %d C V %d I V %d (saved R %d C %d I %d)\n", c.valves.v3000.V, c.valves.v5000.V, c.valves.v8000.V, savedV[0], savedV[1], savedV[2]);
      continue;
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
    {  // recycling ratio rule (spec 16 s4.3): per-centre working state and age vs the saved ratio
      us::IniReader t{&c.config().ini.tune};
      std::vector<us::PollutionLayer::RecyclingCentre> cs;
      int now = 12 * c.clock.date().year + c.clock.date().month;
      std::string detail;
      for (const auto& o : s.game.world().occupants) {
        if (o.agentType != 12851) continue;
        int built = o.hasDate && o.dateYear > 0 ? 12 * o.dateYear + std::max(0, std::min(11, (std::max(1, o.dateDay) - 1) * 12 / 366)) + 1 : now;
        bool powered = c.power.powered(o.x, o.z), occ = (o.state & ~0x200u) == 1;
        cs.push_back(c.pollution.recyclingCentreValues(occ && powered, std::max(0, now - built)));
        int trip = c.traffic ? c.traffic->tripResult(o.x, o.z) : -1, tripAny = -1;
        if (c.traffic) { TripRect tr{o.x, o.z, o.x + o.sizeX - 1, o.z + o.sizeZ - 1}; tripAny = c.traffic->tripResultArea(tr); }
        char b[120]; std::snprintf(b, sizeof b, " [age %d%s%s trip %d/%d]", now - built, powered ? "" : " unpowered", occ ? "" : " not-occupied", trip, tripAny); detail += b;
      }
      std::printf("   recycling: %zu centres, saved ratio %.4f, rule %.4f%s\n", cs.size(), c.pollution.ratio,
                  us::PollutionLayer::recyclingRatio(cs, c.valves.v3000.Slast), detail.c_str());
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
    if (std::getenv("M3K_PUMPS")) {
      int shown = 0;
      for (const auto& f : c.water.facilities()) {
        if (f.kind != us::WaterKind::Pump) continue;
        auto it = savedOut.find({f.x, f.z});
        if (it == savedOut.end() || shown++ >= 12) continue;
        int st = -1, built = 0; std::string props;
        for (const auto& o : s.game.world().occupants)
          if (o.x == f.x && o.z == f.z) {
            st = int(o.state); built = o.dateYear;
            for (const auto& kv : o.intProps) { char b[40]; std::snprintf(b, sizeof b, " %08X=%08X", kv.first, kv.second); props += b; }
          }
        std::printf("     pump (%d,%d) saved %d ours %d: powered %d waterPol %d state %d built %d props%s trip %d\n", f.x, f.z, it->second, f.output,
                    c.power.powered(f.x, f.z) ? 1 : 0, c.pollution.waterAt(f.x, f.z), st, built, props.c_str(),
                    c.traffic ? c.traffic->tripResult(f.x, f.z) : -9);
      }
    }
    std::printf("   pumps %d: output within 5 of saved %d; zero output ours %d saved %d; supply ours %lld saved stat %lld\n", pumps, match,
                zeroOurs, zeroSaved, (long long)c.water.stats.supply, savedSupply);
  }
  return 0;
}
