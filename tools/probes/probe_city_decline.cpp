// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loads saved cities and runs them 450 days. A shipped city is a snapshot of a settled game, so population should
// stay roughly flat; a large drop means a rule differs from the original (issue #78).
//   probe_city_decline [GAME_DIR] [city-name-substring ...]
#include <algorithm>
#include <cstdint>
#include <map>
#include "probe_common.h"
using namespace probe;

static void valves(const City& c, char* buf, size_t n) {
  const auto& v = c.valves;
  std::snprintf(buf, n, "R V=%d S=%d D=%d | C V=%d S=%d D=%d | I V=%d S=%d D=%d", v.v3000.V, v.v3000.Slast, v.v3000.Dlast, v.v5000.V,
                v.v5000.Slast, v.v5000.Dlast, v.v8000.V, v.v8000.Slast, v.v8000.Dlast);
}

int main(int argc, char** argv) {
  fs::path root;
  int used = gameDirFromArgs(argc, argv, root);
  if (needInstall(root, "probe_city_decline GAME_DIR [city ...]")) return 1;
  std::vector<std::string> filters;
  for (int i = 1 + used; i < argc; i++) filters.push_back(argv[i]);
  auto cities = findCities(root, filters);
  if (cities.empty()) { std::fprintf(stderr, "no matching cities\n"); return 1; }

  for (const auto& path : cities) {
    Session s;
    if (!s.open(root)) return 2;
    if (!s.load(path)) { std::printf("%-28s could not be loaded\n", path.filename().string().c_str()); continue; }
    City& c = s.game.city();
    // Saved vs recomputed supply and demand after the first monthly valve cycle: a big ratio shift means a building
    // class is counted differently from the original.
    struct Pair { int s, d; };
    const growth::Valve* vs[3] = {&c.valves.v3000, &c.valves.v5000, &c.valves.v8000};
    Pair before[3];
    for (int i = 0; i < 3; i++) before[i] = {vs[i]->Slast, vs[i]->Dlast};
    std::printf("== %s\n", path.filename().string().c_str());
    // Population band over the run, sampled every 30 days, for comparison with the original's reference trajectories
    // (spec 28 s5.1: shipped / min / max / last over ~480 days).
    int popMin = INT32_MAX, popMax = 0, popLast = 0;
    for (int day = 0; day <= 480; day++) {
      if (day % 30 == 0 || day == 480) {
        int p = s.game.sim().population();
        popMin = std::min(popMin, p); popMax = std::max(popMax, p); popLast = p;
      }
      if (day == 45) {
        std::printf("   first month, recomputed/saved:");
        const char* nm[3] = {"R", "C", "I"};
        for (int i = 0; i < 3; i++)
          std::printf("  %s S %.2f D %.2f", nm[i], before[i].s ? double(vs[i]->Slast) / before[i].s : 0.0, before[i].d ? double(vs[i]->Dlast) / before[i].d : 0.0);
        std::printf("\n");
      }
      if (day % 150 == 0) {
        int st[8] = {0};
        for (const auto& o : s.game.world().occupants) st[(o.state & ~0x200u) & 7]++;
        char vb[200];
        valves(c, vb, sizeof vb);
        std::printf("   day %3d  pop %8d  occupied %6d abandoned %5d construction %5d  %s\n", day, s.game.sim().population(), st[1], st[2], st[4], vb);
      }
      s.game.sim().tickDay();
    }
    std::printf("   band (0..480, every 30 days): min %d max %d last %d\n", popMin, popMax, popLast);
  }
  return 0;
}
