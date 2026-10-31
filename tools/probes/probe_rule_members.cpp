// Lists the members of the non-scatter developer rules (spec 15 s5.5) with their size and building flag word, so the
// member-list filing by flag bits can be checked against the shipped families.
//   probe_rule_members [GAME_DIR] [ruleIdHex ...]
#include <cstdlib>
#include <map>
#include "probe_common.h"
using namespace probe;

int main(int argc, char** argv) {
  fs::path root;
  int used = gameDirFromArgs(argc, argv, root);
  if (needInstall(root, "probe_rule_members GAME_DIR [ruleIdHex ...]")) return 1;
  std::vector<uint32_t> ids;
  for (int i = 1 + used; i < argc; i++) ids.push_back(uint32_t(std::strtoul(argv[i], nullptr, 16)));
  if (ids.empty()) ids = {0x61F44687, 0xE22F4A83, 0x630EA608, 0x81FFC3CE, 0xC2D3FD7E};
  Session s;
  if (!s.open(root)) return 2;
  auto cities = findCities(root, {"madison"});
  if (cities.empty() || !s.load(cities[0])) return 3;
  const auto& cfg = s.game.city().config().growth;
  for (uint32_t id : ids) {
    for (const auto& zr : cfg.rules)
      for (const auto& r : zr.second) {
        if (r.id != id) continue;
        std::printf("== rule %08X zone %d params %d,%d,%d,%d\n", r.id, zr.first, r.a, r.b, r.c, r.d);
        for (const auto& kv : r.lists) {
          std::map<std::tuple<uint32_t, int, int, uint32_t, int, int>, int> seen;
          for (const auto* v : {&kv.second.filler, &kv.second.small, &kv.second.large})
            for (const auto& b : *v) seen[{b.occId, b.sizeX, b.sizeZ, b.flagWord, b.line, b.family}]++;
          std::printf("   category %u: %zu distinct members\n", kv.first, seen.size());
          for (const auto& m : seen)
            std::printf("     occ %08X %dx%d flags %04X part %d family %d x%d\n", std::get<0>(m.first), std::get<1>(m.first), std::get<2>(m.first),
                        std::get<3>(m.first), std::get<4>(m.first), std::get<5>(m.first), m.second);
        }
      }
  }
  return 0;
}
