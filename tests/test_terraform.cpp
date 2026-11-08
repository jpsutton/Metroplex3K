// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/terraform.h"
#include "testutil.h"
using namespace m3k;

static World flat(int n, int alt, int sea = 10) {
  World w; w.init(n, n);
  std::fill(w.alt.begin(), w.alt.end(), uint8_t(alt));
  std::fill(w.water.begin(), w.water.end(), uint8_t(sea));
  w.seaLevel = uint8_t(sea);
  return w;
}

int main() {
  // Costs from ini + defaults.
  {
    TerraformCosts d = TerraformCosts::fromIni(IniFile::fromText(""));
    CHECK(d.raise == 25 && d.lower == 25 && d.level == 25 && d.water == 150);
    TerraformCosts c = TerraformCosts::fromIni(IniFile::fromText("[SpecialCosts]\nRaiseTerrain=40\nLowerTerrain=8\nLevelTerrain=12\nDecorativeWater=200\n"));
    CHECK(c.raise == 40 && c.lower == 8 && c.level == 12 && c.water == 200);
  }
  // Raise single tile on flat land: cost 25, 4 vertices +1, water reset.
  {
    World w = flat(16, 50);
    w.water[w.vIdx(5, 5)] = 99;
    Terraformer t(w);
    TerraPlan p = t.planRaise(5, 5);
    CHECK(p.legal && p.cost == 25);
    CHECK(p.altChanges.size() == 4);
    CHECK(w.altAt(5, 5) == 50);  // plan does not write
    TerraCommit c = t.commit(p);
    CHECK(w.altAt(5, 5) == 51 && w.altAt(6, 5) == 51 && w.altAt(5, 6) == 51 && w.altAt(6, 6) == 51);
    CHECK(w.altAt(4, 5) == 50);
    CHECK(w.waterAt(5, 5) == 10);  // reset to sea level
    CHECK(p.affected.x0 == 4 && p.affected.z0 == 4 && p.affected.x1 == 6 && p.affected.z1 == 6);
    CHECK(c.derivedVerts.x0 == 2 && c.derivedVerts.z0 == 2);
    t.commit(t.planLower(5, 5));
    CHECK(w.altAt(5, 5) == 50);
  }
  // Raising repeatedly drags terrain in a cone of gradient 4.
  {
    World w = flat(32, 50);
    Terraformer t(w);
    for (int i = 0; i < 12; i++) { TerraPlan p = t.planRaise(10, 10); CHECK(p.legal); t.commit(p); }
    CHECK(w.altAt(10, 10) == 62);
    int steepest = 0;
    for (int x = 0; x < 32; x++) for (int z = 0; z < 32; z++)
      for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++)
        if (w.inTiles(x + dx, z + dz) || (x + dx <= 32 && z + dz <= 32 && x + dx >= 0 && z + dz >= 0))
          steepest = std::max(steepest, std::abs(int(w.altAt(x, z)) - int(w.altAt(x + dx, z + dz))));
    CHECK(steepest <= 4);
    CHECK(w.altAt(7, 10) < 62 && w.altAt(7, 10) >= 50);
    CHECK(w.altAt(20, 20) == 50);
  }
  // Too-steep edge illegal.
  {
    World w = flat(8, 50);
    w.alt[w.vIdx(3, 3)] = 60;
    Terraformer t(w);
    TerraPlan p = t.planRaise(3, 3);
    CHECK(!p.legal && p.reason == TerraReason::StepTooSteep);
    CHECK(t.commit(p).derivedVerts.empty());
  }
  // Floors/caps.
  {
    World w = flat(8, 255);
    Terraformer t(w);
    TerraPlan p = t.planRaise(2, 2);
    CHECK(p.legal && p.cost == 0 && p.altChanges.empty());
    World w2 = flat(8, 0);
    Terraformer t2(w2);
    TerraPlan q = t2.planLower(2, 2);
    CHECK(q.legal && q.cost == 0);
    TerraPlan o = t2.planRaise(8, 0);
    CHECK(!o.legal && o.reason == TerraReason::OutOfMap);
  }
  // Corner of map works; propagation stays in grid.
  {
    World w = flat(8, 50);
    Terraformer t(w);
    for (int i = 0; i < 20; i++) t.commit(t.planRaise(0, 0));
    CHECK(w.altAt(0, 0) == 70);
    CHECK(w.altAt(8, 8) == 50);
  }
  // Level.
  {
    World w = flat(16, 50);
    w.alt[w.vIdx(3, 3)] = 52; w.alt[w.vIdx(4, 4)] = 47;
    Terraformer t(w);
    TerraPlan p = t.planLevel(3, 3, 5, 5);  // start corner alt 52
    CHECK(p.legal);
    // vertices (3..6)x(3..6) = 16, target 52: 15 vertices differ by 2, (4,4) by 5 -> sum = 14*2+5 = 33
    CHECK(p.cost == (25 * 33) / 4);
    t.commit(p);
    for (int x = 3; x <= 6; x++) for (int z = 3; z <= 6; z++) CHECK(w.altAt(x, z) == 52);
    // reversed drag: start at (5,5) with alt 52
    World w2 = flat(16, 50);
    Terraformer t2(w2);
    TerraPlan q = t2.planLevel(5, 5, 2, 2);
    CHECK(q.legal && q.cost == 0 && q.altChanges.empty());
  }
  // Level propagates.
  {
    World w = flat(32, 50);
    w.alt[w.vIdx(10, 10)] = 80; w.alt[w.vIdx(11, 10)] = 80;
    for (int x = 0; x <= 32; x++) for (int z = 0; z <= 32; z++) {}
    Terraformer t(w);
    TerraPlan p = t.planLevel(10, 10, 12, 12);  // L = 80
    CHECK(p.legal);
    t.commit(p);
    CHECK(w.altAt(11, 11) == 80);
    CHECK(w.altAt(8, 11) <= 80 && w.altAt(8, 11) >= 72);
    CHECK(w.altAt(10, 5) >= 60);
  }
  // Water.
  {
    World w = flat(8, 50, 10);
    Terraformer t(w);
    TerraPlan p = t.planWater(3, 3);
    CHECK(p.legal && p.cost == 150);
    t.commit(p);
    CHECK(w.waterAt(3, 3) == 50 && w.waterAt(4, 4) == 50);
    TerraPlan again = t.planWater(3, 3);
    CHECK(!again.legal && again.reason == TerraReason::AlreadyWet);
    // neighbouring tile shares 2 wet vertices -> cost 2*150/4 = 75
    TerraPlan n = t.planWater(4, 3);
    CHECK(n.legal && n.cost == 75);
    // occupant blocks
    World w2 = flat(8, 50, 10);
    Occupant o; o.x = 2; o.z = 2; w2.occupants.push_back(o);
    Terraformer t2(w2);
    CHECK(!t2.planWater(3, 3).legal);
    CHECK(t2.planWater(3, 3).reason == TerraReason::Occupied);
    CHECK(t2.planWater(5, 5).legal);
    t2.setTileBlocked([](int, int) { return false; });
    CHECK(t2.planWater(3, 3).legal);
  }
  // Validators + disaster validator + clearing/funds.
  {
    World w = flat(32, 50);
    Terraformer t(w);
    int id = t.addValidator([](const IRect&, int64_t, int) { return false; });
    CHECK(!t.planRaise(5, 5).legal && t.planRaise(5, 5).reason == TerraReason::ValidatorRefused);
    t.removeValidator(id);
    CHECK(t.planRaise(5, 5).legal);
    t.addValidator(makeDisasterValidator(5 * 256 + 128, 5 * 256 + 128));
    CHECK(!t.planRaise(8, 8).legal);   // R x 7..9 within 5 tiles
    CHECK(t.planRaise(20, 20).legal);
    CHECK(t.planWater(8, 8).legal);    // water not refused
    int64_t funds = 100;
    bool cleared = false;
    t.setClearing([](const IRect&) { return ClearQuote{true, 10}; }, [&](const IRect&) { cleared = true; });
    TerraPlan p = t.planRaise(20, 20);
    ApplyResult r = t.apply(p, funds);
    CHECK(r.status == ApplyStatus::Applied && r.total == 35 && funds == 65 && cleared);
    CHECK(w.altAt(20, 20) == 51);
    funds = 20;
    r = t.apply(t.planRaise(25, 25), funds);
    CHECK(r.status == ApplyStatus::NoMoney && funds == 20 && w.altAt(25, 25) == 50);
    r = t.apply(t.planRaise(8, 8), funds);
    CHECK(r.status == ApplyStatus::NotAllowed);
    t.setClearing([](const IRect&) { return ClearQuote{false, 0}; }, nullptr);
    r = t.apply(t.planRaise(25, 25), funds);
    CHECK(r.status == ApplyStatus::NotAllowed);
  }
  // Changed-tiles preview.
  {
    World w = flat(8, 50);
    Terraformer t(w);
    auto ct = t.changedTiles(t.planRaise(3, 3));
    CHECK(ct.size() == 9);
  }
  // Cheats.
  {
    World w = flat(4, 250);
    bool salt = true; IRect d;
    CHECK(applyTerrainCheat(w, "Terrain ten up", &salt, &d) == TerrainCheatResult::Applied);
    CHECK(w.altAt(0, 0) == 255 && !d.empty());
    applyTerrainCheat(w, "terrain ten down", &salt);
    CHECK(w.altAt(0, 0) == 245);
    applyTerrainCheat(w, "Terrain one down", &salt);
    CHECK(w.altAt(1, 1) == 244);
    applyTerrainCheat(w, "Terrain one up", &salt);
    CHECK(w.altAt(1, 1) == 245);
    applyTerrainCheat(w, "Salt off", &salt); CHECK(!salt);
    applyTerrainCheat(w, "SALT ON", &salt); CHECK(salt);
    CHECK(applyTerrainCheat(w, "Load terrain", &salt) == TerrainCheatResult::NeedsLoad);
    CHECK(applyTerrainCheat(w, "nope", &salt) == TerrainCheatResult::Unknown);
  }
  TEST_MAIN_END();
}
