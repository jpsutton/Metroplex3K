// Re-runs the network tiler over each saved road, rail and power network and compares the result with the pieces the
// original game stored (issue #47). Rotation never differs in shipped data; differences are piece ids.
//   probe_retile [GAME_DIR] [city-name-substring ...]
#include <algorithm>
#include <map>
#include "core/tiler.h"
#include "core/world_network_map.h"
#include "probe_common.h"
using namespace probe;

int main(int argc, char** argv) {
  fs::path root;
  int used = gameDirFromArgs(argc, argv, root);
  if (needInstall(root, "probe_retile GAME_DIR [city ...]")) return 1;
  std::vector<std::string> filters;
  for (int i = 1 + used; i < argc; i++) filters.push_back(argv[i]);
  auto cities = findCities(root, filters);
  if (cities.empty()) { std::fprintf(stderr, "no matching cities\n"); return 1; }

  Session s;
  if (!s.open(root)) return 2;
  const char* kindName[] = {"", "road", "rail", "power"};
  for (const auto& path : cities) {
    if (!s.load(path)) continue;
    const TilingRuleSet& rules = s.game.city().config().tiling;
    const World& w = s.game.world();
    auto flags = flagsFromRuleSets(rules);
    std::printf("== %s\n", path.filename().string().c_str());
    for (int kind = 1; kind <= 3; kind++) {
      World copy = w;
      WorldNetworkMap map(copy, flags);
      s.game.city().configureMap(map);   // the game path: exit sides from the connection matrices, neighbour connections
      std::vector<PathTile> drawn;
      std::map<std::pair<int, int>, NetPiece> stored;
      for (const auto& p : w.surface) {
        if (!rules.inSet(netKindToTable(kind), p.pieceId)) continue;
        PathTile t; t.x = p.x; t.z = p.z;
        drawn.push_back(t);
        stored[{p.x, p.z}] = p;
      }
      if (stored.empty()) continue;
      Tiler(rules, map).run(kind, drawn);
      std::map<std::pair<int, int>, NetPiece> now;
      for (const auto& p : copy.surface) now[{p.x, p.z}] = p;
      int same = 0;
      std::map<std::pair<uint32_t, uint32_t>, int> confusion;
      for (const auto& kv : stored) {
        auto it = now.find(kv.first);
        if (it != now.end() && it->second.pieceId == kv.second.pieceId && it->second.rot == kv.second.rot) same++;
        else confusion[{kv.second.pieceId, it == now.end() ? 0u : it->second.pieceId}]++;
      }
      std::printf("   %-5s %6zu tiles, identical %6d (%.1f%%)\n", kindName[kind], stored.size(), same, 100.0 * same / double(stored.size()));
      std::vector<std::pair<int, std::pair<uint32_t, uint32_t>>> top;
      for (const auto& kv : confusion) top.push_back({kv.second, kv.first});
      std::sort(top.rbegin(), top.rend());
      for (size_t i = 0; i < top.size() && i < 3; i++) std::printf("         stored %u -> ours %u : %d\n", top[i].second.first, top[i].second.second, top[i].first);
    }
  }
  return 0;
}
