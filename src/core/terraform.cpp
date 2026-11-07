// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/terraform.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace m3k {
namespace {

struct RE { int a, b, alt; };

// Direction codes: 0=(-1,-1) 1=(0,-1) 2=(+1,-1) 3=(+1,0) 4=(+1,+1) 5=(0,+1) 6=(-1,+1) 7=(-1,0).
const int kDa[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
const int kDb[8] = {-1, -1, -1, 0, 1, 1, 1, 0};

int dirCode(int da, int db) {
  if (da < -1 || da > 1 || db < -1 || db > 1 || (da == 0 && db == 0)) return -1;
  for (int c = 0; c < 8; c++) if (kDa[c] == da && kDb[c] == db) return c;
  return -1;
}

bool edgeAdj(int a1, int b1, int a2, int b2) { return std::abs(a1 - a2) + std::abs(b1 - b2) == 1; }

int64_t layerCost(int price1, int price2, int64_t u1, int64_t u2) { return (int64_t(price1) * u1 + int64_t(price2) * u2) / 4; }

// Build the closed ring around vertex rect [vx0..vx1]x[vz0..vz1] (spec 4.3 step 1).
std::vector<std::pair<int, int>> ringOrder(int vx0, int vz0, int vx1, int vz1) {
  std::vector<std::pair<int, int>> r;
  if (vx0 == vx1 && vz0 == vz1) { r.push_back({vx0, vz0}); return r; }
  for (int a = vx0; a <= vx1; a++) r.push_back({a, vz0});
  for (int b = vz0 + 1; b <= vz1; b++) r.push_back({vx1, b});
  for (int a = vx1 - 1; a >= vx0; a--) r.push_back({a, vz1});
  for (int b = vz1 - 1; b > vz0; b--) r.push_back({vx0, b});
  return r;
}

struct Bounds {
  int a0 = 1 << 30, b0 = 1 << 30, a1 = -(1 << 30), b1 = -(1 << 30);
  void add(int a, int b) { a0 = std::min(a0, a); b0 = std::min(b0, b); a1 = std::max(a1, a); b1 = std::max(b1, b); }
};

struct Propagated {
  int64_t cost = 0;
  std::vector<int> touched;  // vertex indices written in scratch
  Bounds box;
};

// Slope propagation (spec 4.4). `g` is a scratch copy of the altitude grid, modified in place.
void propagate(const World& w, std::vector<uint8_t>& g, std::vector<RE> ring, const TerraformCosts& c, Propagated& out) {
  const int W = w.W, H = w.H;
  for (const RE& e : ring) out.box.add(e.a, e.b);
  for (int layer = 0; layer < 2048; layer++) {
    const uint32_t n = uint32_t(ring.size());
    struct NE { int a, b, alt, sa, sb, salt; };  // entry plus its source inner entry
    std::vector<NE> nr;
    int64_t raised = 0, lowered = 0;
    for (uint32_t i = 0; i < n; i++) {
      const RE& cur = ring[i];
      const RE& prev = ring[(i + n - 1) % n];
      const RE& next = ring[(i + 1) % n];
      int ks[4], nk = 0, p = 3;
      if (n == 1) {
        ks[0] = 2; ks[1] = 4; ks[2] = 6; ks[3] = 8; nk = 4;
      } else {
        p = dirCode(prev.a - cur.a, prev.b - cur.b);
        int q = dirCode(next.a - cur.a, next.b - cur.b);
        if (p >= 0 && q >= 0) {
          int t = ((q - p) % 8 + 8) % 8;
          if (t == 4) { ks[0] = 2; nk = 1; }
          else if (t == 6) { ks[0] = 2; ks[1] = 3; ks[2] = 4; nk = 3; }
          else if (t == 0) { ks[0] = 2; ks[1] = 4; ks[2] = 6; nk = 3; }
        }
      }
      if (nk == 0) { nr.push_back({cur.a, cur.b, cur.alt, cur.a, cur.b, cur.alt}); continue; }
      for (int ki = 0; ki < nk; ki++) {
        int d = (p + ks[ki]) & 7;
        int va = cur.a + kDa[d], vb = cur.b + kDb[d];
        if (va < 0 || vb < 0 || va > W || vb > H) { nr.push_back({cur.a, cur.b, cur.alt, cur.a, cur.b, cur.alt}); continue; }
        int hi = -1, lo = 1000;
        for (int j = -3; j <= 2; j++) {
          uint32_t idx = (uint32_t(i) + uint32_t(j)) % n;  // unsigned wrap quirk (spec 4.4 step 2)
          const RE& e = ring[idx];
          if (std::abs(e.a - va) <= 1 && std::abs(e.b - vb) <= 1) { hi = std::max(hi, e.alt); lo = std::min(lo, e.alt); }
        }
        if (!nr.empty()) {
          const NE& l = nr.back();
          if (std::abs(l.a - va) <= 1 && std::abs(l.b - vb) <= 1) { hi = std::max(hi, l.alt); lo = std::min(lo, l.alt); }
        }
        int vi = w.vIdx(va, vb);
        int cv = g[vi];
        bool changed = false;
        if (hi >= 0) {
          int lower = std::max(hi - 4, 0), upper = std::min(lo + 4, 255);
          int nv = cv;
          if (cv < lower) { nv = lower; raised += nv - cv; }
          else if (cv > upper) { nv = upper; lowered += cv - nv; }
          if (nv != cv) { g[vi] = uint8_t(nv); out.touched.push_back(vi); out.box.add(va, vb); changed = true; cv = nv; }
        }
        if (changed) nr.push_back({va, vb, cv, cur.a, cur.b, cur.alt});
        else nr.push_back({cur.a, cur.b, cur.alt, cur.a, cur.b, cur.alt});
      }
    }
    out.cost += layerCost(c.raise, c.lower, raised, lowered);

    // Merge duplicates, then keep the ring edge-connected by inserting inner entries.
    auto dedupe = [](std::vector<NE>& v) {
      std::vector<NE> r;
      for (const NE& e : v) if (r.empty() || r.back().a != e.a || r.back().b != e.b) r.push_back(e);
      while (r.size() > 1 && r.front().a == r.back().a && r.front().b == r.back().b) r.pop_back();
      v.swap(r);
    };
    dedupe(nr);
    {
      std::vector<NE> fixed;
      const size_t m = nr.size();
      for (size_t i = 0; i < m; i++) {
        const NE& u = nr[i];
        const NE& v = nr[(i + 1) % m];
        fixed.push_back(u);
        if (m > 1 && !edgeAdj(u.a, u.b, v.a, v.b)) {
          if (u.sa != u.a || u.sb != u.b) fixed.push_back({u.sa, u.sb, u.salt, u.sa, u.sb, u.salt});
          if (v.sa != v.a || v.sb != v.b) fixed.push_back({v.sa, v.sb, v.salt, v.sa, v.sb, v.salt});
        }
      }
      dedupe(fixed);
      nr.swap(fixed);
    }
    std::vector<RE> next;
    next.reserve(nr.size());
    for (const NE& e : nr) next.push_back({e.a, e.b, e.alt});
    ring.swap(next);
    for (const RE& e : ring) out.box.add(e.a, e.b);
    if (raised + lowered == 0) break;
    if (ring.size() > 1020) break;
  }
}

}  // namespace

TerraformCosts TerraformCosts::fromIni(const IniFile& ini) {
  TerraformCosts c;
  auto rd = [&](const char* key, int& dst) {
    std::string v;
    if (ini.get("SpecialCosts", key, v)) {
      v = trimSpaces(v);
      if (!v.empty()) dst = int(parseIdNumber(v));
    }
  };
  rd("RaiseTerrain", c.raise);
  rd("LowerTerrain", c.lower);
  rd("LevelTerrain", c.level);
  rd("DecorativeWater", c.water);
  return c;
}

int Terraformer::addValidator(TerraValidator v) {
  int id = nextId_++;
  validators_.push_back({id, std::move(v)});
  return id;
}

void Terraformer::removeValidator(int id) {
  validators_.erase(std::remove_if(validators_.begin(), validators_.end(), [&](auto& p) { return p.first == id; }), validators_.end());
}

// Shared tail: derived-data rect, notify rect, validators.
void Terraformer::finish(TerraPlan& p) const {
  for (auto& v : validators_)
    if (!v.second(p.affected, p.cost, p.op)) { p.legal = false; p.reason = TerraReason::ValidatorRefused; return; }
}

TerraPlan Terraformer::planRaiseLower(int op, int tx, int tz) const {
  TerraPlan p;
  p.op = op;
  if (!w_.inTiles(tx, tz)) { p.reason = TerraReason::OutOfMap; return p; }
  const bool raise = op == kTerraRaise;
  p.footprint = {tx, tz, tx, tz};
  auto order = ringOrder(tx, tz, tx + 1, tz + 1);
  std::vector<RE> ring;
  int64_t nChange = 0;
  for (auto& v : order) {
    int cur = w_.altAt(v.first, v.second);
    int tgt = raise ? std::min(cur + 1, 255) : std::max(cur - 1, 0);
    if (raise ? cur < 255 : cur > 0) nChange++;
    ring.push_back({v.first, v.second, tgt});
  }
  for (size_t i = 0; i < ring.size(); i++) {
    const RE& a = ring[i];
    const RE& b = ring[(i + 1) % ring.size()];
    if (std::abs(a.alt - b.alt) > 4) { p.reason = TerraReason::StepTooSteep; return p; }
  }
  std::vector<uint8_t> g = w_.alt;
  Propagated pr;
  propagate(w_, g, ring, costs_, pr);
  p.cost = pr.cost + int64_t(raise ? costs_.raise : costs_.lower) * nChange / 4;
  for (auto& e : ring) { pr.touched.push_back(w_.vIdx(e.a, e.b)); g[w_.vIdx(e.a, e.b)] = uint8_t(e.alt); pr.box.add(e.a, e.b); }
  // Affected rect (4.4 step 5).
  p.affected = {std::max(pr.box.a0 - 1, 0), std::max(pr.box.b0 - 1, 0), std::min(pr.box.a1, w_.W - 1), std::min(pr.box.b1, w_.H - 1)};
  // Unique alt changes.
  std::sort(pr.touched.begin(), pr.touched.end());
  pr.touched.erase(std::unique(pr.touched.begin(), pr.touched.end()), pr.touched.end());
  for (int vi : pr.touched)
    if (g[vi] != w_.alt[vi]) p.altChanges.push_back({vi / (w_.H + 1), vi % (w_.H + 1), g[vi]});
  // Water reset to sea level over the affected rect's vertices (4.6.1).
  for (int x = p.affected.x0; x <= std::min(p.affected.x1 + 1, w_.W); x++)
    for (int z = p.affected.z0; z <= std::min(p.affected.z1 + 1, w_.H); z++)
      if (w_.waterAt(x, z) != w_.seaLevel) p.waterChanges.push_back({x, z, w_.seaLevel});
  p.derivedVerts = {std::max(tx - 3, 0), std::max(tz - 3, 0), std::min(p.affected.x1 + 1, w_.W), std::min(p.affected.z1 + 1, w_.H)};
  p.derivedVerts.x0 = std::min(p.derivedVerts.x0, std::max(pr.box.a0, 0));
  p.derivedVerts.z0 = std::min(p.derivedVerts.z0, std::max(pr.box.b0, 0));
  p.derivedVerts.x1 = std::max(p.derivedVerts.x1, std::min(pr.box.a1, w_.W));
  p.derivedVerts.z1 = std::max(p.derivedVerts.z1, std::min(pr.box.b1, w_.H));
  p.notifyTiles = {p.derivedVerts.x0, p.derivedVerts.z0, std::min(p.derivedVerts.x1, w_.W - 1), std::min(p.derivedVerts.z1, w_.H - 1)};
  p.legal = true;
  p.reason = TerraReason::Ok;
  finish(p);
  return p;
}

TerraPlan Terraformer::planRaise(int tx, int tz) const { return planRaiseLower(kTerraRaise, tx, tz); }
TerraPlan Terraformer::planLower(int tx, int tz) const { return planRaiseLower(kTerraLower, tx, tz); }

TerraPlan Terraformer::planLevel(int sx, int sz, int cx, int cz) const {
  if (!w_.inTiles(sx, sz) || !w_.inTiles(cx, cz)) { TerraPlan p; p.op = kTerraLevel; p.reason = TerraReason::OutOfMap; return p; }
  return planLevelTo(std::min(sx, cx), std::min(sz, cz), std::max(sx, cx), std::max(sz, cz), w_.altAt(sx, sz));
}

TerraPlan Terraformer::planLevelTo(int x0, int z0, int x1, int z1, int L) const {
  TerraPlan p;
  p.op = kTerraLevel;
  if (!w_.inTiles(x0, z0) || !w_.inTiles(x1, z1)) { p.reason = TerraReason::OutOfMap; return p; }
  p.footprint = {x0, z0, x1, z1};
  auto order = ringOrder(x0, z0, x1 + 1, z1 + 1);
  if (order.size() > 1024) { p.reason = TerraReason::RingTooLarge; return p; }
  std::vector<RE> ring;
  for (auto& v : order) ring.push_back({v.first, v.second, L});
  std::vector<uint8_t> g = w_.alt;
  Propagated pr;
  propagate(w_, g, ring, costs_, pr);
  int64_t sum = 0;
  for (int x = x0; x <= x1 + 1; x++)
    for (int z = z0; z <= z1 + 1; z++) {
      sum += std::abs(int(w_.altAt(x, z)) - L);
      int vi = w_.vIdx(x, z);
      g[vi] = uint8_t(L);
      pr.touched.push_back(vi);
      pr.box.add(x, z);
    }
  p.cost = pr.cost + int64_t(costs_.level) * sum / 4;
  p.affected = {std::max(pr.box.a0 - 1, 0), std::max(pr.box.b0 - 1, 0), std::min(pr.box.a1, w_.W - 1), std::min(pr.box.b1, w_.H - 1)};
  std::sort(pr.touched.begin(), pr.touched.end());
  pr.touched.erase(std::unique(pr.touched.begin(), pr.touched.end()), pr.touched.end());
  for (int vi : pr.touched)
    if (g[vi] != w_.alt[vi]) p.altChanges.push_back({vi / (w_.H + 1), vi % (w_.H + 1), g[vi]});
  for (int x = p.affected.x0; x <= std::min(p.affected.x1 + 1, w_.W); x++)
    for (int z = p.affected.z0; z <= std::min(p.affected.z1 + 1, w_.H); z++)
      if (w_.waterAt(x, z) != w_.seaLevel) p.waterChanges.push_back({x, z, w_.seaLevel});
  p.derivedVerts = {std::max(x0 - 3, 0), std::max(z0 - 3, 0), std::min(p.affected.x1 + 1, w_.W), std::min(p.affected.z1 + 1, w_.H)};
  p.derivedVerts.x0 = std::min(p.derivedVerts.x0, std::max(pr.box.a0, 0));
  p.derivedVerts.z0 = std::min(p.derivedVerts.z0, std::max(pr.box.b0, 0));
  p.derivedVerts.x1 = std::max(p.derivedVerts.x1, std::min(pr.box.a1, w_.W));
  p.derivedVerts.z1 = std::max(p.derivedVerts.z1, std::min(pr.box.b1, w_.H));
  p.notifyTiles = {p.derivedVerts.x0, p.derivedVerts.z0, std::min(p.derivedVerts.x1, w_.W - 1), std::min(p.derivedVerts.z1, w_.H - 1)};
  p.legal = true;
  p.reason = TerraReason::Ok;
  finish(p);
  return p;
}

TerraPlan Terraformer::planWater(int tx, int tz) const {
  TerraPlan p;
  p.op = kTerraWater;
  if (!w_.inTiles(tx, tz)) { p.reason = TerraReason::OutOfMap; return p; }
  p.footprint = {tx, tz, tx, tz};
  p.affected = {std::max(tx - 1, 0), std::max(tz - 1, 0), std::min(tx + 1, w_.W - 1), std::min(tz + 1, w_.H - 1)};
  int wet = 0;
  int64_t dry = 0;
  for (int dx = 0; dx < 2; dx++)
    for (int dz = 0; dz < 2; dz++) {
      int x = tx + dx, z = tz + dz;
      if (w_.altAt(x, z) <= w_.waterAt(x, z)) wet++;
      else { dry++; p.waterChanges.push_back({x, z, w_.altAt(x, z)}); }
    }
  if (wet == 4) { p.waterChanges.clear(); p.reason = TerraReason::AlreadyWet; return p; }
  for (int x = p.affected.x0; x <= p.affected.x1; x++)
    for (int z = p.affected.z0; z <= p.affected.z1; z++) {
      bool blk;
      if (blocked_) blk = blocked_(x, z);
      else {
        blk = false;
        for (const Occupant& o : w_.occupants)
          if (x >= o.x && x < o.x + std::max(o.sizeX, 1) && z >= o.z && z < o.z + std::max(o.sizeZ, 1)) { blk = true; break; }
      }
      if (blk) { p.waterChanges.clear(); p.reason = TerraReason::Occupied; return p; }
    }
  p.cost = int64_t(costs_.water) * dry / 4;
  p.derivedVerts = {std::max(tx - 3, 0), std::max(tz - 3, 0), std::min(tx + 1 + 3, w_.W), std::min(tz + 1 + 3, w_.H)};
  p.notifyTiles = {p.derivedVerts.x0, p.derivedVerts.z0, std::min(p.derivedVerts.x1, w_.W - 1), std::min(p.derivedVerts.z1, w_.H - 1)};
  p.legal = true;
  p.reason = TerraReason::Ok;
  finish(p);
  return p;
}

TerraCommit Terraformer::commit(const TerraPlan& p) {
  TerraCommit c;
  if (!p.legal) return c;
  for (const VertexChange& v : p.altChanges) w_.alt[w_.vIdx(v.x, v.z)] = v.value;
  for (const VertexChange& v : p.waterChanges) w_.water[w_.vIdx(v.x, v.z)] = v.value;
  if (!p.altChanges.empty() || !p.waterChanges.empty()) w_.invalidateDeepWater();
  c.derivedVerts = p.derivedVerts;
  c.notifyTiles = p.notifyTiles;
  return c;
}

int64_t Terraformer::totalCost(const TerraPlan& p, bool* clearPossible) const {
  int64_t total = p.cost;
  bool ok = true;
  if (quote_) {
    ClearQuote q = quote_(p.affected);
    total += q.cost;
    ok = q.possible;
  }
  if (clearPossible) *clearPossible = ok;
  return total;
}

ApplyResult Terraformer::apply(const TerraPlan& p, int64_t& funds) {
  ApplyResult r{ApplyStatus::NotAllowed, 0, {}};
  if (!p.legal) return r;
  bool ok = true;
  int64_t total = totalCost(p, &ok);
  r.total = total;
  if (!ok) return r;
  if (total != 0 && total > funds) { r.status = ApplyStatus::NoMoney; return r; }
  if (clear_) clear_(p.affected);
  r.commit = commit(p);
  funds -= total;
  r.status = ApplyStatus::Applied;
  return r;
}

std::vector<std::pair<int, int>> Terraformer::changedTiles(const TerraPlan& p) const {
  std::vector<std::pair<int, int>> out;
  auto add = [&](const std::vector<VertexChange>& v) {
    for (const VertexChange& c : v)
      for (int dx = -1; dx <= 0; dx++)
        for (int dz = -1; dz <= 0; dz++)
          if (w_.inTiles(c.x + dx, c.z + dz)) out.push_back({c.x + dx, c.z + dz});
  };
  if (!p.legal) return out;
  add(p.altChanges);
  if (p.op == kTerraWater) add(p.waterChanges);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

TerraValidator makeDisasterValidator(int px, int pz) {
  return [px, pz](const IRect& r, int64_t, int op) {
    if (op != 1 && op != 2 && op != 4 && op != 8 && op != 16) return true;
    auto dist = [](int p, int lo, int hi) { return p < lo ? lo - p : (p > hi ? p - hi : 0); };
    int dx = dist(px, r.x0 * 256, (r.x1 + 1) * 256), dz = dist(pz, r.z0 * 256, (r.z1 + 1) * 256);
    return !(dx <= 1280 && dz <= 1280);
  };
}

TerrainCheatResult applyTerrainCheat(World& w, const std::string& command, bool* salt, IRect* dirty) {
  std::string s;
  for (char ch : command) s += char(std::tolower((unsigned char)ch));
  while (!s.empty() && s.back() == ' ') s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  if (s == "load terrain") return TerrainCheatResult::NeedsLoad;
  if (s == "salt on" || s == "salt off") { if (salt) *salt = (s == "salt on"); return TerrainCheatResult::Applied; }
  int delta = 0;
  if (s == "terrain one up") delta = 1;
  else if (s == "terrain one down") delta = -1;
  else if (s == "terrain ten up") delta = 10;
  else if (s == "terrain ten down") delta = -10;
  else return TerrainCheatResult::Unknown;
  for (auto& a : w.alt) a = uint8_t(std::max(0, std::min(255, int(a) + delta)));
  if (dirty) *dirty = {0, 0, w.W, w.H};
  return TerrainCheatResult::Applied;
}

}  // namespace m3k
