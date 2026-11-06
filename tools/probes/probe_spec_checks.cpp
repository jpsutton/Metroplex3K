// Checks rules of the growth and utility spec revisions against the saved state of each shipped city, without running
// the simulation forward (issue #78):
//   * land value: recompute every block right after load and compare with the saved grid (mean ratio, correlation);
//   * Href and the city centre used by the land-value pass;
//   * the valve cap rule (spec 15 s3.2) from the saved amenity valves vs the saved capPct;
//   * unemployment (spec 20 s6) from the saved valves and cohort shares vs the saved chart sample;
//   * pump outputs after one water update vs the outputs saved with each pump (property 8351FDF3, high u16).
//   probe_spec_checks [GAME_DIR] [city-name-substring ...]
#include <cstddef>
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
                    out.size(), rec->data.size(), nd, first == SIZE_MAX ? std::ptrdiff_t(-1) : std::ptrdiff_t(first));
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
    if (std::getenv("M3K_PIPEFLAG")) {   // which occupants carry the pipe flag 0x800000 (spec 12 s5.2, s9.2)
      std::map<uint32_t, int> byAgent; int surf = 0, sub = 0;
      for (const auto& o : s.game.world().occupants)
        if (const OccAttr* a = c.gameData()->occupant(o.occId, false, o.keyType == T_OCC_ATTR_PORT)) if (a->flags & 0x800000) byAgent[o.agentType]++;
      for (const auto& p : s.game.world().surface) if (const OccAttr* a = c.gameData()->occupant(p.pieceId, true)) if (a->flags & 0x800000) surf++;
      for (const auto& p : s.game.world().subway) if (const OccAttr* a = c.gameData()->occupant(p.pieceId, true)) if (a->flags & 0x800000) sub++;
      int pipeFlag = 0, pipeAll = 0;
      for (const auto& p : s.game.world().pipes) { pipeAll++; if (const OccAttr* a = c.gameData()->occupant(p.pieceId, true)) if (a->flags & 0x800000) pipeFlag++; }
      std::printf("   pipe pieces with the flag: %d of %d\n", pipeFlag, pipeAll);
      std::printf("   pipe-flagged: surface pieces %d, subway pieces %d, buildings:", surf, sub);
      for (const auto& kv : byAgent) std::printf(" %u x%d", kv.first, kv.second);
      std::printf("\n");
      continue;
    }
    if (const char* bb = std::getenv("M3K_FACBOX")) {   // water facilities in a box "x0,z0,x1,z1" after one month
      int x0, z0, x1, z1;
      if (std::sscanf(bb, "%d,%d,%d,%d", &x0, &z0, &x1, &z1) == 4) {
        for (int d = 0; d < 35; d++) s.game.sim().tickDay();
        int n = 0; long long sum = 0;
        for (const auto& f : c.water.facilities()) {
          if (f.x < x0 || f.x > x1 || f.z < z0 || f.z > z1) continue;
          n++; sum += f.output;
          std::printf("     fac kind %d (%d,%d) out %d status %d pipeAtTile %d powered %d\n", int(f.kind), f.x, f.z, f.output, int(f.status),
                      c.isPipe(f.x, f.z) ? 1 : 0, c.power.powered(f.x, f.z) ? 1 : 0);
        }
        std::printf("   facilities in box: %d, output sum %lld\n", n, sum);
        if (const char* sd = std::getenv("M3K_FLOODSEED")) {   // spec 16 s3.4 flood from a seed tile over pipes + facility lots
          int fx, fz; std::sscanf(sd, "%d,%d", &fx, &fz);
          const int W = s.game.world().W, H = s.game.world().H;
          std::vector<uint8_t> mask(size_t(W) * H, 0), filled(size_t(W) * H, 0);
          for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) mask[size_t(x) * H + z] = c.isPipe(x, z);
          for (const auto& o : s.game.world().occupants)   // every water-facility lot, as City::monthlyWater
            if (o.agentType == 12810 || o.agentType == 12840 || o.agentType == 12830)
              for (int dx = 0; dx < o.sizeX; dx++) for (int dz = 0; dz < o.sizeZ; dz++) mask[size_t(o.x + dx) * H + o.z + dz] = 1;
          std::vector<int> needH{fx * H + fz}, needV{fx * H + fz}; filled[size_t(fx) * H + fz] = 1;
          int passes = 0;
          for (; passes < 500; passes++) {
            bool added = false; std::vector<int> nh, nv;
            for (int t : needH) { int x = t / H, z = t % H; for (int nx : {x - 1, x + 1}) if (nx >= 0 && nx < W && mask[size_t(nx) * H + z] && !filled[size_t(nx) * H + z]) { filled[size_t(nx) * H + z] = 1; added = true; nh.push_back(nx * H + z); needV.push_back(nx * H + z); } }
            for (int t : needV) { int x = t / H, z = t % H; for (int nz : {z - 1, z + 1}) if (nz >= 0 && nz < H && mask[size_t(x) * H + nz] && !filled[size_t(x) * H + nz]) { filled[size_t(x) * H + nz] = 1; added = true; nv.push_back(x * H + nz); nh.push_back(x * H + nz); } }
            needH.swap(nh); needV.swap(nv);
            if (!added) break;
          }
          int cnt = 0, bx0 = W, bx1 = -1, bz0 = H, bz1 = -1, nf = 0; long long S = 0;
          for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) if (filled[size_t(x) * H + z]) { cnt++; bx0 = std::min(bx0, x); bx1 = std::max(bx1, x); bz0 = std::min(bz0, z); bz1 = std::max(bz1, z); }
          for (const auto& f : c.water.facilities()) if (f.output > 0 && filled[size_t(f.x) * H + f.z]) { nf++; S += f.output; }
          std::printf("   flood from (%d,%d): %d tiles in %d passes, bbox x %d-%d z %d-%d, %d producing facilities, S %lld\n", fx, fz, cnt, passes, bx0, bx1, bz0, bz1, nf, S);
          {  // uncapped 4-connected component of the seed over the same mask, and the closest mask gap to the next component
            std::vector<int> comp(size_t(W) * H, 0); std::vector<int> q{fx * H + fz}; comp[size_t(fx) * H + fz] = 1;
            for (size_t h = 0; h < q.size(); h++) { int t = q[h], x = t / H, z = t % H; const int d4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
              for (auto& d : d4) { int nx = x + d[0], nz = z + d[1]; if (nx < 0 || nz < 0 || nx >= W || nz >= H || comp[size_t(nx) * H + nz] || !mask[size_t(nx) * H + nz]) continue; comp[size_t(nx) * H + nz] = 1; q.push_back(nx * H + nz); } }
            std::printf("   uncapped component of the seed: %zu tiles\n", q.size());
            // single-tile gaps: a non-mask tile with component tiles and other-mask tiles on opposite/adjacent sides
            int shown = 0;
            for (int x = 1; x < W - 1 && shown < 12; x++) for (int z = 1; z < H - 1 && shown < 12; z++) {
              size_t i = size_t(x) * H + z; if (mask[i]) continue;
              bool inC = false, other = false; const int d4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
              for (auto& d : d4) { size_t j = size_t(x + d[0]) * H + z + d[1]; if (comp[j]) inC = true; else if (mask[j]) other = true; }
              if (inC && other) { std::printf("     gap tile (%d,%d) road %d occ %d\n", x, z, c.isRoadTile(x, z) ? 1 : 0, c.occupantAt(x, z) >= 0 ? int(s.game.world().occupants[size_t(c.occupantAt(x, z))].agentType) : -1); shown++; }
            }
          }
          for (const auto& f : c.water.facilities()) {
            if (f.output <= 0 || filled[size_t(f.x) * H + f.z] || f.x < bx0 - 10 || f.x > bx1 + 10 || f.z < bz0 || f.z > bz1) continue;
            int near = 99;
            for (int x = std::max(0, f.x - 6); x <= std::min(W - 1, f.x + 6); x++) for (int z = std::max(0, f.z - 6); z <= std::min(H - 1, f.z + 6); z++)
              if (filled[size_t(x) * H + z]) near = std::min(near, std::max(std::abs(x - f.x), std::abs(z - f.z)));
            std::string nb;
            for (int dz = -2; dz <= 2; dz++) { for (int dx = -2; dx <= 2; dx++) { int x = f.x + dx, z = f.z + dz; nb += (x < 0 || z < 0 || x >= W || z >= H) ? '#' : filled[size_t(x) * H + z] ? 'F' : mask[size_t(x) * H + z] ? 'm' : c.isRoadTile(x, z) ? 'r' : '.'; } nb += '|'; }
            std::printf("     outside: fac (%d,%d) out %d, nearest filled %d, 5x5 %s\n", f.x, f.z, f.output, near, nb.c_str());
          }
        }
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
      {  // classify the lots that are dry after our distribution but were watered in the saved bitmap
        const int W = s.game.world().W, H = s.game.world().H, r = c.water.params().waterFromPipe;
        int nearPipe = 0, farPipe = 0, unpowered = 0, shown = 0;
        for (const auto& o : s.game.world().occupants) {
          if (!(o.agentType >= 3000 && o.agentType < 10000 && (o.state & ~0x200u) == 1)) continue;
          if (c.water.tileServiced(o.x, o.z)) continue;
          bool near = false;
          for (int x = o.x - r; x < o.x + o.sizeX + r && !near; x++)
            for (int z = o.z - r; z < o.z + o.sizeZ + r; z++)
              if (x >= 0 && z >= 0 && x < W && z < H && c.isPipe(x, z)) { near = true; break; }
          (near ? nearPipe : farPipe)++;
          if (!c.power.powered(o.x, o.z)) unpowered++;
          if (near && shown++ < 5) std::printf("     dry lot near pipe: (%d,%d) %dx%d agent %u\n", o.x, o.z, o.sizeX, o.sizeZ, o.agentType);
        }
        std::printf("   dry after month: %d within WaterFromPipe=%d of a pipe, %d farther; %d unpowered\n", nearPipe, r, farPipe, unpowered);
        // reached coverage: pipes (+ facility footprints) flood-filled from producing facilities, dilated by r
        std::vector<uint8_t> pipe(size_t(W) * H, 0), reached(size_t(W) * H, 0), cover(size_t(W) * H, 0);
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) pipe[size_t(x) * H + z] = c.isPipe(x, z);
        for (const auto& f : c.water.facilities()) pipe[size_t(f.x) * H + f.z] = 1;
        std::vector<int> q;
        int producing = 0, comps = 0;
        for (const auto& f : c.water.facilities()) {
          if (f.output <= 0) continue;
          producing++;
          int st = f.x * H + f.z;
          if (reached[st]) continue;
          comps++; reached[st] = 1; q.assign(1, st);
          for (size_t h = 0; h < q.size(); h++) {
            int cc = q[h], x = cc / H, z = cc % H;
            const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& d : d4) {
              int nx = x + d[0], nz = z + d[1];
              if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
              int n = nx * H + nz;
              if (reached[n] || !pipe[n]) continue;
              reached[n] = 1; q.push_back(n);
            }
          }
        }
        {  // per component: supply vs consumption of the lots inside its coverage
          std::vector<int> compOf(size_t(W) * H, -1);
          std::vector<int64_t> supply;
          int id = 0;
          for (const auto& f : c.water.facilities()) {
            if (f.output <= 0) continue;
            int st = f.x * H + f.z;
            if (compOf[st] >= 0) { supply[size_t(compOf[st])] += f.output; continue; }
            supply.push_back(f.output); compOf[st] = id; q.assign(1, st);
            for (size_t h = 0; h < q.size(); h++) {
              int cc = q[h], x = cc / H, z = cc % H;
              static const bool diag = std::getenv("M3K_PIPE8") != nullptr;
              const int d8[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
              for (int k = 0; k < (diag ? 8 : 4); k++) {
                const auto& d = d8[k];
                int nx = x + d[0], nz = z + d[1];
                if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
                int n = nx * H + nz;
                if (compOf[n] >= 0 || !pipe[n]) continue;
                compOf[n] = id; q.push_back(n);
              }
            }
            id++;
          }
          std::vector<int> cov(size_t(W) * H, -1);
          for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) { int cid = compOf[size_t(x) * H + z]; if (cid < 0) continue;
            for (int xx = std::max(0, x - r); xx <= std::min(W - 1, x + r); xx++)
              for (int zz = std::max(0, z - r); zz <= std::min(H - 1, z + r); zz++) if (cov[size_t(xx) * H + zz] < 0) cov[size_t(xx) * H + zz] = cid; }
          std::vector<int64_t> demand(supply.size(), 0); std::vector<int> dryIn(supply.size(), 0), lots(supply.size(), 0);
          for (const auto& o : s.game.world().occupants) {
            if ((o.state & ~0x200u) != 1) continue;
            int cid = cov[size_t(o.x) * H + o.z]; if (cid < 0) continue;
            demand[size_t(cid)] += int64_t(c.water.params().consumptionPerTile(o.agentType)) * o.sizeX * o.sizeZ; lots[size_t(cid)]++;
            if (o.agentType >= 3000 && o.agentType < 10000 && !c.water.tileServiced(o.x, o.z)) dryIn[size_t(cid)]++;
          }
          for (size_t i = 0; i < supply.size(); i++)
            if (dryIn[i] > 0) std::printf("     component %zu: supply %lld demand %lld lots %d dry %d\n", i, (long long)supply[i], (long long)demand[i], lots[i], dryIn[i]);
        }
        int pipeTiles = 0, reachedTiles = 0;
        for (size_t i = 0; i < pipe.size(); i++) { pipeTiles += pipe[i]; reachedTiles += reached[i]; }
        for (int x = 0; x < W; x++) for (int z = 0; z < H; z++) if (reached[size_t(x) * H + z])
          for (int xx = std::max(0, x - r); xx <= std::min(W - 1, x + r); xx++)
            for (int zz = std::max(0, z - r); zz <= std::min(H - 1, z + r); zz++) cover[size_t(xx) * H + zz] = 1;
        int dryCovered = 0, dryUncovered = 0;
        for (const auto& o : s.game.world().occupants) {
          if (!(o.agentType >= 3000 && o.agentType < 10000 && (o.state & ~0x200u) == 1) || c.water.tileServiced(o.x, o.z)) continue;
          (cover[size_t(o.x) * H + o.z] ? dryCovered : dryUncovered)++;
        }
        std::printf("   pipes %d tiles, %d reached from %d producing facilities in %d components; dry lots covered %d, uncovered %d\n",
                    pipeTiles, reachedTiles, producing, comps, dryCovered, dryUncovered);
      }
      std::printf("   water after month: dry %d of %d; supply %lld used %lld\n", b.first, b.second, (long long)c.water.stats.supply, (long long)c.water.stats.used);
      continue;
    }
    if (std::getenv("M3K_ZONES")) {   // zone-type tile counts (for the constant code-9 count per pass)
      std::map<int, int> zc; const auto& w = s.game.world();
      for (uint8_t z : w.zone) zc[z]++;
      std::printf("   zones:"); for (const auto& kv : zc) if (kv.first) std::printf(" %d:%d", kv.first, kv.second); std::printf("\n");
      int uncovered = 0;
      for (int x = 0; x < w.W; x++) for (int z = 0; z < w.H; z++) if (w.zone[size_t(w.tIdx(x, z))] == 14 && c.occupantAt(x, z) < 0) { if (uncovered++ < 6) std::printf("     uncovered airport tile (%d,%d)\n", x, z); }
      std::printf("   uncovered zone-14 tiles: %d\n", uncovered);
      {  // powered state of the uncovered airport tiles and their distance to the nearest occupant tile
        int pw = 0, far2 = 0;
        for (int x = 0; x < w.W; x++) for (int z = 0; z < w.H; z++) {
          if (w.zone[size_t(w.tIdx(x, z))] != 14 || c.occupantAt(x, z) >= 0) continue;
          if (c.power.powered(x, z)) pw++;
          int best = 99;
          for (int xx = std::max(0, x - 4); xx <= std::min(w.W - 1, x + 4); xx++) for (int zz = std::max(0, z - 4); zz <= std::min(w.H - 1, z + 4); zz++)
            if (c.occupantAt(xx, zz) >= 0 || c.isPowerLine(xx, zz)) best = std::min(best, std::max(std::abs(xx - x), std::abs(zz - z)));
          if (best > 2) far2++;
        }
        std::printf("   uncovered airport tiles powered %d of %d; farther than 2 from any building or line: %d\n", pw, uncovered, far2);
      }
      for (const auto& o : w.occupants) if (o.agentType == 12310 && (o.sizeX > 1 || o.sizeZ > 1)) std::printf("     multi-tile airport part (%d,%d) %dx%d occ %08X key %08X\n", o.x, o.z, o.sizeX, o.sizeZ, o.occId, o.keyType);
      continue;
    }
    if (std::getenv("M3K_PWRTRACE")) {   // power and pump state after every month start for ~120 days
      int64_t lastCons = -1;
      for (int d = 1; d <= 130; d++) {
        s.game.sim().tickDay();
        const auto& ps = c.power.stats();
        if (ps.consumed == lastCons) continue;
        lastCons = ps.consumed;
        int64_t demand = 0, demandByState[17] = {};
        for (const auto& o : c.powerConsumers()) { demand += o.powerUse; demandByState[(o.state & ~0x200u) & 15] += o.powerUse; }
        std::printf("     day %3d total demand %lld (state1 %lld, 2 %lld, 4 %lld)\n", d, (long long)demand, (long long)demandByState[1], (long long)demandByState[2], (long long)demandByState[4]);
        int zero = 0, pumps = 0; for (const auto& f : c.water.facilities()) if (f.kind == us::WaterKind::Pump) { pumps++; if (f.output == 0) zero++; }
        int st[17] = {}; for (const auto& o : s.game.world().occupants) if (o.agentType >= 3000 && o.agentType < 10000) st[(o.state & ~0x200u) & 15]++;
        std::printf("     day %3d pop %7d power produced %lld consumed %lld unpoweredTiles %d | pumps zero %d/%d | water S %lld U %lld | lots 1:%d 2:%d 4:%d\n", d, c.population(),
                    (long long)ps.produced, (long long)ps.consumed, ps.consumerTiles - ps.poweredConsumerTiles, zero, pumps, (long long)c.water.stats.supply,
                    (long long)c.water.stats.used, st[1], st[2], st[4]);
      }
      continue;
    }
    if (std::getenv("M3K_CONNTEST")) {   // road drag to the map edge creates and keeps a neighbour connection (spec 12 s8)
      const World& wd = s.game.world();
      for (int x = 2; x < wd.W - 2; x++) {
        bool ok = true;
        for (int z = 0; z < 4 && ok; z++) ok = c.occupantAt(x, z) < 0 && !c.isRoadTile(x, z) && !c.isPowerLine(x, z) && wd.waterAt(x, z) < wd.altAt(x, z) && wd.zone[size_t(x) * wd.H + z] == 0 && wd.altAt(x, z) == wd.altAt(x + 1, z) && wd.altAt(x, z) == wd.altAt(x, z + 1) && wd.altAt(x, z) == wd.altAt(x + 1, z + 1);
        if (!ok) continue;
        size_t before = c.neighbours.conn[1].size(); uint32_t nb = c.neighbours.neighbours[1].nRoad;
        bool built = c.buildNetwork(NetworkKind::Road, {{x, 3}, {x, 0}});
        std::printf("   conn test x=%d built %d: road conn tiles %zu -> %zu, neighbour 1 nRoad %u -> %u (exists %d)\n", x, built ? 1 : 0, before,
                    c.neighbours.conn[1].size(), nb, c.neighbours.neighbours[1].nRoad, c.neighbours.neighbours[1].exists ? 1 : 0);
        bool again = c.buildNetwork(NetworkKind::Road, {{x, 3}, {x, 0}});
        std::printf("   second drag built %d: conn tiles %zu nRoad %u\n", again ? 1 : 0, c.neighbours.conn[1].size(), c.neighbours.neighbours[1].nRoad);
        s.game.sim().tickDay();
        for (const auto& pc : s.game.world().surface) if (pc.x == x && pc.z <= 4) std::printf("   piece (%d,%d) id %u\n", pc.x, pc.z, pc.pieceId);
        std::printf("   road at (x,0) %d,(x,1) %d; conn has 0x%x %d\n", c.isRoadTile(x, 0) ? 1 : 0, c.isRoadTile(x, 1) ? 1 : 0, (unsigned(x) << 16), int(c.neighbours.conn[1].count(uint32_t(x) << 16)));
        c.bulldoze(x, 0, x, 0);
        std::printf("   after bulldoze: conn tiles %zu\n", c.neighbours.conn[1].size());
        break;
      }
    }
    if (std::getenv("M3K_TRIPCAT")) {
      int hist[5] = {};
      std::map<uint32_t, int> uncat;
      for (const auto& o : s.game.world().occupants) {
        int cat = categoryOfTypeChain(c.agentHierarchy().chain(o.agentType));
        if (cat < 0) { hist[0]++; uncat[o.agentType]++; }
        else if (cat <= 3) hist[cat + 1]++;
      }
      std::printf("   trip cats -1/R/C/I/O: %d %d %d %d %d\n", hist[0], hist[1], hist[2], hist[3], hist[4]);
      int shown = 0;
      for (auto& kv : uncat) {
        if (shown++ >= 25) break;
        auto ch = c.agentHierarchy().chain(kv.first);
        std::printf("     uncat agent %u n=%d chain", kv.first, kv.second);
        for (uint32_t id : ch) std::printf(" %u", id);
        std::printf("\n");
      }
    }
    if (std::getenv("M3K_ONRAMP")) {
      int n=0;
      for (const auto& o : s.game.world().occupants) if (o.agentType == 17202) {
        std::printf("     offramp occId=%u key=%08X size=%dx%d at (%d,%d) rot=%u\n", o.occId, o.keyType, o.sizeX, o.sizeZ, o.x, o.z, o.rot);
        n++;
      }
      int p=0; for (const auto& x : s.game.world().surface) if (x.pieceId>=15130 && x.pieceId<=15141) {
        if (p<30) std::printf("     ramp-piece %u at (%d,%d) rot=%u\n", x.pieceId, x.x, x.z, x.rot); p++;
      }
      // resolve agent 17202 from GameData by scanning common id ranges
      if (GameData* gd = s.game.city().gameData()) {
        for (uint32_t id = 1; id < 0x10000; id++) {
          if (const OccAttr* a = gd->occupant(id, true)) if (a->agentType == 17202)
            std::printf("     net-occ %u agent %u size %ux%u\n", id, a->agentType, a->sizeX, a->sizeZ);
          if (const OccAttr* a = gd->occupant(id, false)) if (a->agentType == 17202)
            std::printf("     bldg-occ %u agent %u size %ux%u\n", id, a->agentType, a->sizeX, a->sizeZ);
        }
      }
      std::printf("     offramp occupants %d ramp-pieces %d\n", n, p);
    }
    if (std::getenv("M3K_LANDMARKS")) {   // landmark picker membership: agent types under 18000 in the hierarchy
      int n = 0, lo = 1 << 30, hi = 0;
      for (const auto& kv : c.config().parent) if (c.agentHierarchy().isAncestorOrSelf(kv.first, 18000)) { n++; lo = std::min<int>(lo, int(kv.first)); hi = std::max<int>(hi, int(kv.first)); }
      std::printf("   landmark agent types under 18000: %d (ids %d..%d); 19999 under 18000: %d\n", n, lo, hi, c.agentHierarchy().isAncestorOrSelf(19999, 18000) ? 1 : 0);
    }
    if (std::getenv("M3K_POTHOLES")) {   // road condition walk (spec 12 s10): underfund roads, then overfund
      auto count = [&] { int n = 0; for (const auto& p : s.game.world().surface) n += (p.pieceId >= 15040 && p.pieceId <= 15042) || p.pieceId == 9051; return n; };
      std::printf("   potholes at load %d, condition %.4f\n", count(), c.roadCondition());
      c.budget.setFunding(dept::Roads, 20);
      for (int d = 0; d < 120; d++) { s.game.sim().tickDay(); if (d % 30 == 29) std::printf("   roads 20%%: day %d potholes %d condition %.4f\n", d + 1, count(), c.roadCondition()); }
      c.budget.setFunding(dept::Roads, 200);
      for (int d = 0; d < 120; d++) { s.game.sim().tickDay(); if (d % 30 == 29) std::printf("   roads 200%%: day %d potholes %d condition %.4f\n", d + 1, count(), c.roadCondition()); }
    }
    if (std::getenv("M3K_PLANTS")) {   // power plants after the first month start
      for (int d = 0; d < 35; d++) s.game.sim().tickDay();
      int now = 12 * c.clock.date().year + c.clock.date().month;
      for (const auto& p : c.power.plants())
        std::printf("     plant %u at (%d,%d) cap %d output %d age %d lmax %d ld %d expired %d\n", p.agentType, p.x, p.z, p.capacity, p.output, now - p.buildAbsMonth, p.lmax, p.ld, p.expired ? 1 : 0);
      std::printf("   produced %lld consumed %lld\n", (long long)c.power.stats().produced, (long long)c.power.stats().consumed);
      {
        std::map<uint32_t, std::pair<int, long long>> byType;   // agent type -> plants, summed output
        for (const auto& p : c.power.plants()) { byType[p.agentType].first++; byType[p.agentType].second += p.output; }
        for (const auto& kv : byType) std::printf("   type %u: %d plants, output %lld\n", kv.first, kv.second.first, kv.second.second);
        for (const auto& k : c.power.components()) {
          std::printf("   component seed (%d,%d): %d tiles, %d plants, raw S %lld, S' %lld, supplied %lld, r %.4f%s  types:", k.sx, k.sz, k.tiles, k.plants, (long long)k.rawS, (long long)k.S, (long long)k.supplied, k.rawS > 0 ? double(k.supplied) / double(k.rawS) : 0.0, k.brownOut ? " BROWNOUT" : "");
          for (auto t : k.types) std::printf(" %u", t);
          std::printf("\n");
        }
        std::printf("   production row (our category order, 9 = neighbours):");
        for (int k = 0; k < 10; k++) std::printf(" %d", c.power.production[c.power.monthIndex][k]);
        std::printf("   (slot %d)\n", c.power.monthIndex);
      }
      continue;
    }
    if (const char* tl = std::getenv("M3K_LVTILES")) {   // land value at "x,z;x,z;..." after N days (M3K_LVDAYS, default 40)
      int days = std::getenv("M3K_LVDAYS") ? std::atoi(std::getenv("M3K_LVDAYS")) : 40;
      for (int d = 0; d < days; d++) s.game.sim().tickDay();
      std::string t = tl; size_t pos = 0;
      while (pos < t.size()) {
        size_t e = t.find(';', pos); if (e == std::string::npos) e = t.size();
        int x, z; if (std::sscanf(t.substr(pos, e - pos).c_str(), "%d,%d", &x, &z) == 2)
          std::printf("     LV (%d,%d) = %d   %s\n", x, z, c.landValue.landValueAtTile(x, z), c.lvExplain(x & ~1, z & ~1).c_str());
        pos = e + 1;
      }
      continue;
    }
    if (std::getenv("M3K_POLSRC")) {   // saved pollution source grids vs the ones we rebuild from the occupants at load
      const ResKey key{0x206C6E7C, 0xC0A81498, 0};
      const auto* rec = s.game.world().raw ? s.game.world().raw->findInner(key) : nullptr;
      if (!rec) continue;
      us::PollutionLayer saved; saved.parse(rec->data.data(), rec->data.size(), s.game.world().W, s.game.world().H);
      const auto& ours = c.pollution;
      long long sa = 0, oa = 0, sw = 0, ow = 0; int diffA = 0, diffW = 0;
      for (size_t i = 0; i < saved.airSource.size() && i < ours.airSource.size(); i++) {
        sa += saved.airSource[i]; oa += ours.airSource[i]; sw += saved.waterSource[i]; ow += ours.waterSource[i];
        diffA += saved.airSource[i] != ours.airSource[i]; diffW += saved.waterSource[i] != ours.waterSource[i];
      }
      std::printf("   air source: saved sum %lld ours %lld, %d cells differ; water source: saved %lld ours %lld, %d differ; table entries saved %zu ours %zu\n",
                  sa, oa, diffA, sw, ow, diffW, saved.table.size(), ours.table.size());
      for (const auto& kv : ours.table)
        if (!saved.table.count(kv.first)) std::printf("     only ours: type %u air %d water %d rA %d rW %d garbage %d\n", kv.first, kv.second.air, kv.second.water, kv.second.rA, kv.second.rW, kv.second.garbage);
        else if (!(saved.table.at(kv.first) == kv.second)) std::printf("     differs: type %u\n", kv.first);
      for (const auto& kv : saved.table)
        if (!ours.table.count(kv.first)) std::printf("     only saved: type %u air %d water %d\n", kv.first, kv.second.air, kv.second.water);
      const int nz = s.game.world().H / 2;
      for (auto t : std::vector<std::pair<int, int>>{{60, 144}, {20, 223}, {50, 192}, {11, 216}, {61, 71}}) {
        size_t ci = size_t(t.first / 2) * nz + t.second / 2;
        std::printf("     cell of (%d,%d): air source saved %d ours %d, water source saved %d ours %d, state air saved %d\n", t.first, t.second,
                    saved.airSource[ci], ours.airSource[ci], saved.waterSource[ci], ours.waterSource[ci], saved.airAt(t.first, t.second));
      }
      continue;
    }
    if (std::getenv("M3K_PASSES")) {   // result codes of each completed pass over ~250 days, plus population
      auto codes = [](const growth::ZoneManager::ResultMap& m) {
        std::map<int, uint32_t> byCode; for (const auto& kv : m) if (kv.second) byCode[kv.first.first] += kv.second;
        std::string o; for (const auto& kv : byCode) o += " " + std::to_string(kv.first) + ":" + std::to_string(kv.second);
        return o;
      };
      std::string last = codes(c.zones.resultPrev);
      std::printf("     day   0 pop %7d prev pass%s\n", c.population(), last.c_str());
      for (int d = 1; d <= 250; d++) {
        s.game.sim().tickDay();
        std::string now = codes(c.zones.resultPrev);
        if (now != last) { std::printf("     day %3d pop %7d prev pass%s\n", d, c.population(), now.c_str()); last = now; }
      }
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
