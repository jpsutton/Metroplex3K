// Replays one network drag on a saved city and prints what our placement tools do (issue #47): the routed tiles with their validity, the quote,
// the funds charged, every surface piece added / changed / removed and every terrain corner that moved.
//   probe_drag GAME_DIR SAVE.sc3 KIND X0,Z0 X1,Z1
//   KIND: road | rail | power | highway | pipe | subway
// Nothing is written: the city is loaded in memory only.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include "probe_common.h"
using namespace probe;

static bool parsePair(const char* s, int& a, int& b) { return std::sscanf(s, "%d,%d", &a, &b) == 2; }

int main(int argc, char** argv) {
  fs::path root;
  int used = gameDirFromArgs(argc, argv, root);
  if (needInstall(root, "probe_drag GAME_DIR SAVE.sc3 KIND X0,Z0 X1,Z1") || argc < 5 + used) { std::fprintf(stderr, "usage: probe_drag GAME_DIR SAVE.sc3 KIND X0,Z0 X1,Z1\n"); return 1; }
  const char* save = argv[1 + used];
  const std::string kindArg = lower(argv[2 + used]);
  int x0, z0, x1, z1;
  if (!parsePair(argv[3 + used], x0, z0) || !parsePair(argv[4 + used], x1, z1)) { std::fprintf(stderr, "bad coordinates\n"); return 1; }
  struct K { const char* name; NetworkKind nk; };
  static const K kinds[] = {{"road", NetworkKind::Road}, {"rail", NetworkKind::Rail}, {"power", NetworkKind::Power},
                            {"highway", NetworkKind::Highway}, {"pipe", NetworkKind::Pipe}, {"subway", NetworkKind::Subway}};
  const K* kind = nullptr;
  for (const K& k : kinds) if (kindArg == k.name) kind = &k;
  if (!kind) { std::fprintf(stderr, "unknown kind %s\n", kindArg.c_str()); return 1; }

  Session s;
  if (!s.open(root)) return 2;
  if (!s.load(save)) { std::fprintf(stderr, "cannot load %s\n", save); return 2; }
  City& city = s.game.city();
  const World& w = s.game.world();

  std::printf("%s", city.describeRoute(kind->nk, x0, z0, x1, z1).c_str());
  std::vector<std::pair<int, int>> path = city.routeNetworkPath(kind->nk, x0, z0, x1, z1);
  City::Quote q = city.quoteNetwork(kind->nk, path);
  City::NetworkConfirm nc = city.peekNetworkConfirm(kind->nk, path);
  std::printf("path %zu tiles, quote valid %d cost %lld, first confirm %d (price %d)\n", path.size(), int(q.valid), (long long)q.cost, int(nc.kind), nc.price);

  using Key = std::pair<int, int>;
  auto snapshot = [&](const World& ww) { std::map<std::pair<Key, int>, NetPiece> m; for (const auto& p : ww.surface) m[{{p.x, p.z}, 0}] = p; return m; };
  const auto before = snapshot(w);
  const std::vector<uint8_t> altBefore = w.alt;
  // M3K_CORNERS=x0,z0,x1,z1 prints the vertex heights of that window before and after.
  int cx0 = -1, cz0 = -1, cx1 = -1, cz1 = -1;
  if (const char* e = std::getenv("M3K_CORNERS")) std::sscanf(e, "%d,%d,%d,%d", &cx0, &cz0, &cx1, &cz1);
  auto window = [&](const World& ww, const char* tag) {
    if (cx0 < 0) return;
    std::printf("corners %s (rows = z, columns = x %d..%d):\n", tag, cx0, cx1);
    for (int z = cz0; z <= cz1; z++) { std::printf("  z%-4d", z); for (int x = cx0; x <= cx1; x++) std::printf(" %3d", int(ww.altAt(x, z))); std::printf("\n"); }
    if (std::getenv("M3K_WATER")) {
      std::printf("water %s:\n", tag);
      for (int z = cz0; z <= cz1; z++) { std::printf("  z%-4d", z); for (int x = cx0; x <= cx1; x++) std::printf(" %3d", int(ww.waterAt(x, z))); std::printf("\n"); }
    }
  };
  window(w, "before");
  const int64_t fundsBefore = s.game.sim().funds;
  city.connectionConfirmOverride = true;
  const bool built = city.buildNetwork(kind->nk, path);
  const World& after = s.game.world();
  window(after, "after");
  std::printf("buildNetwork -> %d, charged %lld\n", int(built), (long long)(fundsBefore - s.game.sim().funds));
  const auto now = snapshot(after);
  for (const auto& kv : now) {
    auto it = before.find(kv.first);
    if (it == before.end()) std::printf("  + (%d,%d) piece %u rot %d y %d\n", kv.second.x, kv.second.z, kv.second.pieceId, kv.second.rot, kv.second.y);
    else if (it->second.pieceId != kv.second.pieceId || it->second.rot != kv.second.rot || it->second.y != kv.second.y)
      std::printf("  ~ (%d,%d) piece %u rot %d y %d  (was %u rot %d y %d)\n", kv.second.x, kv.second.z, kv.second.pieceId, kv.second.rot, kv.second.y, it->second.pieceId, it->second.rot, it->second.y);
  }
  for (const auto& kv : before) if (!now.count(kv.first)) std::printf("  - (%d,%d) piece %u removed\n", kv.second.x, kv.second.z, kv.second.pieceId);
  for (int x = 0; x <= after.W; x++)
    for (int z = 0; z <= after.H; z++)
      if (after.alt[size_t(after.vIdx(x, z))] != altBefore[size_t(after.vIdx(x, z))])
        std::printf("  corner (%d,%d) %d -> %d\n", x, z, int(altBefore[size_t(after.vIdx(x, z))]), int(after.alt[size_t(after.vIdx(x, z))]));
  return 0;
}
