// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/sim.h"

namespace m3k {

void Sim::init(World* w, GameData* g, const CityConfig* cfg, uint32_t seed) {
  CityConfig local;
  city_.init(w, g, cfg ? *cfg : local, seed);
  taxRate = lastTax_ = city_.budget.tax[0].rate;
  day = 0;
  sync();
}

void Sim::sync() {
  day = city_.daysElapsed();
  auto fill = [&](Valve& v, int id) {
    const growth::Valve& s = city_.valves.valve(id);
    v.supply = s.Slast; v.demand = s.Dlast; v.value = s.V;
  };
  fill(valveR, growth::kValveRes);
  fill(valveC, growth::kValveCom);
  fill(valveI, growth::kValveInd);
}

namespace {
int failSound(const City& c) { return c.lastFailNoMoney ? 32 : 30; }
}

bool Sim::paintZoneType(int x0, int z0, int x1, int z1, int zoneType) {
  bool r = city_.paintZoneType(x0, z0, x1, z1, zoneType);
  if (city_.onSound) city_.onSound(r ? (zoneType == 0 ? 44 : 38) : failSound(city_));
  return r;
}

bool Sim::plop(uint32_t id, int x, int z, int cost) {
  bool r = city_.plop(id, x, z, cost);
  if (!r && city_.onSound) city_.onSound(failSound(city_));
  return r;
}

bool Sim::buildNetwork(NetworkKind k, const std::vector<std::pair<int, int>>& path) {
  bool r = city_.buildNetwork(k, path);
  if (city_.onSound && !city_.usingTilingRules()) {   // with tiling rules the tool host raises the sounds
    static const int ok[7] = {0, 14, 51, 16, 14, 50, 53};
    city_.onSound(r ? ok[int(k)] : failSound(city_));
  }
  return r;
}

bool Sim::bulldoze(int x0, int z0, int x1, int z1) {
  bool r = city_.bulldoze(x0, z0, x1, z1);
  if (city_.onSound) city_.onSound(r ? 5 : 30);
  return r;
}

void Sim::tickDay() {
  if (taxRate != lastTax_) { city_.setTaxRate(taxRate); lastTax_ = taxRate; }
  // Stepping a day by hand never waits on the schedule's hold (a loaded city starts paused, which raises it): the caller owns the
  // pacing, as Game::daySimStep does for the worker.
  auto savedHold = std::move(city_.sched.isHold);
  city_.sched.isHold = nullptr;
  city_.tickDay();
  city_.sched.isHold = std::move(savedHold);
  sync();
}

}  // namespace m3k
