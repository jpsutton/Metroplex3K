// Player-facing simulation facade. All simulation work lives in City (city.h); Sim keeps the small
// API the Game shell uses (funds, valves, tools) and exposes City& for everything else.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "core/city.h"

namespace m3k {

struct Valve { int supply = 0, demand = 0, value = 0; };

class Sim {
 public:
  // `cfg` may be null: a synthetic (data-less) configuration is used.
  void init(World* w, GameData* g, const CityConfig* cfg, uint32_t seed = 1);
  void rebuildIndex() { city_.rebuildIndex(); }

  // Player actions (return false if rejected). Costs are charged to funds.
  bool paintZoneType(int x0, int z0, int x1, int z1, int zoneType);  // 0 = dezone
  bool plop(uint32_t occupantId, int x, int z, int cost);
  bool buildRoad(const std::vector<std::pair<int, int>>& path) { return buildNetwork(NetworkKind::Road, path); }
  bool buildNetwork(NetworkKind k, const std::vector<std::pair<int, int>>& path);
  bool bulldoze(int x0, int z0, int x1, int z1);

  void tickDay();
  void tickMonth() {}   // months are driven by the calendar inside City

  // Queries
  bool isRoadTile(int x, int z) const { return city_.isRoadTile(x, z); }
  int occupantAt(int x, int z) const { return city_.occupantAt(x, z); }
  int population() const { return city_.population(); }
  int roadCount() const { return city_.roadCount(); }

  City& city() { return city_; }
  const City& city() const { return city_; }

  City city_;
  int64_t& funds = city_.budget.funds;
  int day = 0;  // days simulated since the city was opened
  int taxRate = 7;
  Valve valveR, valveC, valveI;

  // Called after every action / day: updates `day` and the valve mirrors.
  void sync();

 private:
  int lastTax_ = -1;
};

}  // namespace m3k
