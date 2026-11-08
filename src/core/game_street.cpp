// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Street automata (vehicles, pedestrians, trains, boats) glue: host adapter, source sync, view and sprites.
#include <algorithm>
#include <cmath>

#include "core/game.h"

namespace m3k {
using namespace automata_ids;

void Game::StreetHost::rebuild() {
  if (!w) return;
  piece.assign(size_t(w->W) * w->H, -1);
  for (size_t i = 0; i < w->surface.size(); i++) {
    const NetPiece& p = w->surface[i];
    if (w->inTiles(p.x, p.z)) piece[w->tIdx(p.x, p.z)] = int32_t(i);
  }
}

bool Game::StreetHost::pieceAt(int x, int y, AutoNet k, PieceRef& out) const {
  if (!w || !w->inTiles(x, y) || piece.empty()) return false;
  int i = piece[w->tIdx(x, y)];
  if (i < 0) return false;
  // Rail vehicles use rail pieces (crossings included); road vehicles road or highway pieces; walkers anything but a bare power line.
  const uint32_t id = w->surface[i].pieceId;
  if (c) {
    if (k == AutoNet::Rail && !c->pieceInKind(id, NetworkKind::Rail)) return false;
    if (k == AutoNet::Road && !c->pieceInKind(id, NetworkKind::Road) && !c->pieceInKind(id, NetworkKind::Highway)) return false;
    if (k == AutoNet::Walk && c->pieceInKind(id, NetworkKind::Power) && !c->pieceInKind(id, NetworkKind::Road) && !c->pieceInKind(id, NetworkKind::Rail)) return false;
  } else if (k == AutoNet::Rail && id < 1000) return false;
  out.pieceId = w->surface[i].pieceId; out.rotation = w->surface[i].rot;
  return true;
}

bool Game::StreetHost::isWater(int x, int y) const { return w && w->inTiles(x, y) && w->altAt(x, y) < w->waterAt(x, y); }

void Game::StreetHost::refreshCaches() {
  if (!w || !c) return;
  const size_t n = size_t(w->W) * w->H;
  roadPieceCache.assign(n, 0);
  densityCache.assign(n, 0);
  for (int x = 0; x < w->W; x++)
    for (int y = 0; y < w->H; y++) {
      const size_t i = size_t(w->tIdx(x, y));
      roadPieceCache[i] = c->hasRoadPiece(x, y) ? 1 : 0;
      densityCache[i] = uint8_t(liveDensity(x, y));
    }
}

bool Game::StreetHost::pedEligible(int x, int y) const {
  if (!roadPieceCache.empty()) return w && w->inTiles(x, y) && roadPieceCache[size_t(w->tIdx(x, y))];
  // Spec 26 §5.8 / 13 §3.1a [STATIC]: pedestrian spawn needs occupant flag 0x8000 = road piece.
  return w && c && c->hasRoadPiece(x, y);
}

int Game::StreetHost::trafficDensity(int x, int y) const {
  if (!densityCache.empty()) return w && w->inTiles(x, y) ? densityCache[size_t(w->tIdx(x, y))] : 0;
  return liveDensity(x, y);
}

int Game::StreetHost::liveDensity(int x, int y) const {
  if (!w || !c) return 0;
  if (int live = c->trafficDensityAt(x, y)) return live;
  // Cheap stand-in for the traffic layer where the simulator has no live data: density of road pieces around the tile.
  int n = 0;
  for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) if (c->isRoadTile(x + dx, y + dy)) n++;
  return std::min(255, n * 12);
}

int Game::StreetHost::groundHeight(int x, int y) const {
  if (!w || !w->W || !w->H) return 0;
  x = std::clamp(x, 0, w->W - 1); y = std::clamp(y, 0, w->H - 1);
  if (!piece.empty()) {
    int i = piece[w->tIdx(x, y)];
    if (i >= 0) {   // vehicle paths run from the highest corner down (h = 0 at the high end), unlike the sprite's front corner
      const NetPiece& np = w->surface[size_t(i)];
      const OccAttr* oa = data ? data->occupant(np.pieceId, true, false) : nullptr;
      return w->surfaceTopY(np, oa ? oa->agentType : 0) * 256;
    }
  }
  return std::max(w->altAt(x, y), w->waterAt(x, y)) * 256;
}

// Sea route of a seaport's ships (spec 26 5.4): a water path from the port's shore to a map edge. The spawner is "not ready" (retries
// in 6 minutes) when no water edge is reachable. The path is found by breadth-first search over water tiles from the water nearest
// the port and reduced to its turning points. [INFERRED: the original's route construction is untraced.]
std::vector<std::pair<double, double>> Game::StreetHost::seaRouteFrom(int, int px, int py) const {
  std::vector<std::pair<double, double>> route;
  if (!w || !w->W) return route;
  const int W = w->W, H = w->H;
  auto water = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && isWater(x, y); };
  std::vector<int> prev(size_t(W) * H, -2);
  std::vector<int> queue;
  for (int r = 1; r <= 6 && queue.empty(); r++)   // nearest water to the port, ring by ring
    for (int dy = -r; dy <= r; dy++)
      for (int dx = -r; dx <= r; dx++) {
        if (std::max(std::abs(dx), std::abs(dy)) != r || !water(px + dx, py + dy)) continue;
        const int i = (py + dy) * W + (px + dx);
        if (prev[size_t(i)] == -2) { prev[size_t(i)] = -1; queue.push_back(i); }
      }
  int goal = -1;
  for (size_t h = 0; h < queue.size() && goal < 0; h++) {
    const int i = queue[h], x = i % W, y = i / W;
    if (x == 0 || y == 0 || x == W - 1 || y == H - 1) { goal = i; break; }
    static const int d8[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (auto& d : d8) {
      const int nx = x + d[0], ny = y + d[1];
      if (!water(nx, ny)) continue;
      if (d[0] && d[1] && (!water(x + d[0], y) || !water(x, y + d[1]))) continue;   // no corner cutting across land
      const int ni = ny * W + nx;
      if (prev[size_t(ni)] != -2) continue;
      prev[size_t(ni)] = i; queue.push_back(ni);
    }
  }
  if (goal < 0) return route;
  std::vector<int> path;
  for (int i = goal; i >= 0; i = prev[size_t(i)]) path.push_back(i);
  std::reverse(path.begin(), path.end());
  int pdx = 0, pdy = 0;
  for (size_t k = 0; k < path.size(); k++) {
    const int x = path[k] % W, y = path[k] / W;
    int dx = 0, dy = 0;
    if (k + 1 < path.size()) { dx = path[k + 1] % W - x; dy = path[k + 1] / W - y; }
    if (k == 0 || k + 1 == path.size() || dx != pdx || dy != pdy) route.push_back({x + 0.5, y + 0.5});
    pdx = dx; pdy = dy;
  }
  return route;
}

void Game::setupStreetSim() {
  fx_.clear();   // a new city shows no disaster effects of the previous one
  street_.reset();
  streetHost_.reset();
  streetSources_.clear();
  streetSpawners_.clear();
  streetSig_ = 0; streetSigSerial_ = ~0ull;   // the first frame builds the sources
  if (audio_) audio_->detachAllSounds();   // the old automata are gone without their Stop events
  if (!haveData_) return;   // without game data there are no vehicle/people sprites to draw
  if (!automataLoaded_) {
    automata_ = AutomataData();
    static const uint32_t types[] = {kVAttribType, kVclProdType, kPAttribType, kPplProdType, kAnSprAttType, kPPathType,
                                     kVPathType, kTrBlkType, kScriptType, kTrnProdType, kAirTrMgrType, kSpriteInstType};
    for (uint32_t t : types)
      for (const ResKey& k : data_.db().keysOfType(t)) {
        ResourceRef r = data_.db().find(k);
        if (!r) continue;
        size_t n = r.declared && r.declared < r.available ? r.declared : r.available;
        automata_.addRecord(k, Bytes(r.data, r.data + n));
      }
    { IniFile st; if (data_.ini().load("SC3StrtSimLayer.ini", st)) automata_.tunables = StreetSimTunables::fromIni(st); }   // key / emergency vehicle lists
    automataLoaded_ = true;
  }
  streetHost_ = std::make_unique<StreetHost>();
  streetHost_->w = asyncSim_ ? &renderWorld_ : &world_; streetHost_->c = &city(); streetHost_->data = haveData_ ? &data_ : nullptr;
  streetHost_->rebuild();
  streetCacheDirty_ = true;
  StreetSim::Config cfg;
  cfg.seed = 1u + uint32_t(world_.W);
  street_ = std::make_unique<StreetSim>(automata_, *streetHost_, cfg);
}

void Game::syncStreetSources() {
  if (!street_) return;
  streetHost_->rebuild();
  street_->removeBuildings(streetSources_);   // one pass; per-id removal was O(n^2), ~34 ms for 6500 sources
  streetSources_.clear();
  // Timers (ports, airports, marinas, stations) persist across syncs so their intervals and live counts are not reset.
  std::map<uint64_t, int> spawners;
  auto spawner = [&](int kind, int sx, int sz) {
    const uint64_t key = uint64_t(kind) << 48 | uint64_t(uint32_t(sx)) << 24 | uint64_t(uint32_t(sz));
    auto it = streetSpawners_.find(key);
    int id;
    if (it != streetSpawners_.end()) { id = it->second; streetSpawners_.erase(it); }
    else id = kind == 0 ? street_->addSeaport(sx, sz) : kind == 1 ? street_->addAirport(sx, sz) : kind == 2 ? street_->addMarina(sx, sz)
            : street_->addStation(sx, sz, kind == 4);
    spawners[key] = id;
  };
  // Spec 26 5.1: trains spawn from rail end caps (agent 17002, TRNPROD 1) and subway end caps (17420, TRNPROD 2); stations are not train sources.
  // An end cap is a network piece whose occupant record has that agent type.
  for (const NetPiece& p : world_.surface) {
    const OccAttr* a = data_.occupant(p.pieceId, true);
    if (a && a->agentType == 17002) spawner(3, p.x, p.z);
  }
  for (const NetPiece& p : world_.subway) {
    const OccAttr* a = data_.occupant(p.pieceId, true);
    if (a && a->agentType == 17420) spawner(4, p.x, p.z);
  }
  for (const Occupant& o : world_.occupants) {
    if ((o.state & ~0x300u) != 1) continue;
    const OccAttr* a = data_.occupant(o.occId, false, o.keyType == T_OCC_ATTR_PORT);
    if (!a) continue;
    if (a->agentType == 14430) spawner(2, o.x, o.z);   // leisure boats (spec 26 5.3); the marina is also an ordinary building
    if (a->agentType == 12330) spawner(0, o.x, o.z);   // seaport ships (5.4)
    if (a->agentType == 12310) spawner(1, o.x, o.z);   // airport aircraft
    // Every building with a pedestrian table (stations and ports included) gets a pedestrian manager; its vehicle table only weights the road
    // vehicle mix of its 32 x 32 cell (addBuilding), buildings are not vehicle spawn points.
    if (a->pedProdKey.instance || a->vehProdKey.instance) streetSources_.push_back(street_->addBuilding(o.x, o.z, a->pedProdKey, a->vehProdKey));
  }
  for (auto& kv : streetSpawners_) street_->removeSpawner(kv.second);   // sources that are gone
  streetSpawners_ = std::move(spawners);
}

// ---------------------------------------------------------------- parade event (spec 26 5.7)

namespace {
struct ParadeCfg {
  int roadTiles = 6, spectators = 3, cost = 100, minAura = 30;
  int minCount = 8, maxCount = 10;
  std::vector<int> lead, middle, tail;
  std::map<int, std::pair<uint32_t, int>> blocks;   // block id -> (VATTRIB, spacing in 1/256 tiles)
};
std::vector<int> intList(const std::string& s) {
  std::vector<int> v; size_t p = 0;
  while (p < s.size()) { size_t e = s.find(',', p); if (e == std::string::npos) e = s.size(); const std::string t = s.substr(p, e - p); if (!t.empty()) v.push_back(std::atoi(t.c_str())); p = e + 1; }
  return v;
}
}  // namespace

// Starts the holiday parade: pays ParadeCost, searches a straight road run in a spiral from the city centre (every tile a road
// with aura >= MinAura, at least 3 commercial tiles around the run [INFERRED reading]), builds the lineup from the country's
// block lists and spawns the vehicles and SpectatorDensity pedestrians per route tile. Returns false when it cannot run.
bool Game::startParadeEvent() {
  if (!street_ || !haveData_ || !simReady_) return false;
  IniFile ini;
  if (!data_.ini().load("SC3DisasterLayer.ini", ini)) return false;
  ParadeCfg cfg;
  auto num = [&](const char* k, int def) { std::string v; return ini.get("ParadeEventTunables", k, v) ? std::atoi(v.c_str()) : def; };
  cfg.roadTiles = num("RoadTilesRequired", 6); cfg.spectators = num("SpectatorDensity", 3); cfg.cost = num("ParadeCost", 100); cfg.minAura = num("MinAura", 30);
  for (const auto& kv : ini.section("ParadeEventParadeBlocks")) {
    const size_t c = kv.second.find(',');
    if (c == std::string::npos) continue;
    cfg.blocks[std::atoi(kv.first.c_str())] = {uint32_t(std::strtoul(kv.second.c_str(), nullptr, 0)), std::atoi(kv.second.c_str() + c + 1)};
  }
  // Country config by language [INFERRED: the original keys it by the user's country].
  const std::string lang = data_.language;
  const char* key = lang.find("UK") != std::string::npos ? "gb" : lang == "German" ? "de" : lang == "Spanish" ? "es" : lang == "Swedish" ? "se" :
                    lang == "Dutch" ? "nl" : lang.find("Portug") != std::string::npos ? "br" : "us";
  std::string line;
  if (!ini.get("ParadeEventParadeConfigs", key, line) && !ini.get("ParadeEventParadeConfigs", "us", line)) return false;
  {   // M/D:nameString:min,max:lead:middle:tail
    std::vector<std::string> f; size_t p = 0;
    while (p <= line.size()) { size_t e = line.find(':', p); if (e == std::string::npos) e = line.size(); f.push_back(line.substr(p, e - p)); p = e + 1; }
    if (f.size() < 6) return false;
    const auto mm = intList(f[2]); if (mm.size() == 2) { cfg.minCount = mm[0]; cfg.maxCount = std::max(mm[0], mm[1]); }
    cfg.lead = intList(f[3]); cfg.middle = intList(f[4]); cfg.tail = intList(f[5]);
  }
  if (cfg.lead.empty() || cfg.middle.empty() || cfg.tail.empty()) return false;
  City& c = sim_.city();
  if (fundsRef() <= cfg.cost) return false;
  // Route search.
  const World& w = world_;
  int cx = w.W / 2, cz = w.H / 2;
  c.lvCentre(cx, cz);
  cx = std::max(2, std::min(w.W - 3, cx)); cz = std::max(2, std::min(w.H - 3, cz));
  auto roadOk = [&](int x, int z) { return w.inTiles(x, z) && c.hasRoadPiece(x, z) && (c.aura.grid.empty() || c.aura.aura(x, z) >= cfg.minAura); };
  auto commercial = [&](int x, int z) { if (!w.inTiles(x, z)) return 0; const int zt = w.zone[w.tIdx(x, z)]; return (zt >= 5 && zt <= 7) ? 1 : 0; };
  int rx0 = -1, rz0 = -1, rx1 = -1, rz1 = -1;
  const int maxR = std::max(w.W, w.H);
  for (int r = 0; r <= maxR && rx0 < 0; r++)
    for (int dz = -r; dz <= r && rx0 < 0; dz++)
      for (int dx = -r; dx <= r && rx0 < 0; dx++) {
        if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
        const int x = cx + dx, z = cz + dz;
        for (int axis0 = int(rng_.rand(2)), k = 0; k < 2; k++) {
          const int axis = (axis0 + k) & 1, sx = axis == 0 ? 1 : 0, sz = axis == 0 ? 0 : 1;
          int comm = 0; bool ok = true;
          for (int i = 0; i < cfg.roadTiles && ok; i++) {
            const int tx = x + sx * i, tz = z + sz * i;
            if (!roadOk(tx, tz)) { ok = false; break; }
            for (int a = -1; a <= 1; a++) for (int b = -1; b <= 1; b++) comm += commercial(tx + a, tz + b);
          }
          if (!ok || comm < 3) continue;
          int len = cfg.roadTiles;   // extend the run while it stays a road
          while (roadOk(x + sx * len, z + sz * len) && len < 64) len++;
          const bool rev = rng_.rand(2) == 1;   // the direction is chosen at random
          rx0 = rev ? x + sx * (len - 1) : x; rz0 = rev ? z + sz * (len - 1) : z;
          rx1 = rev ? x : x + sx * (len - 1); rz1 = rev ? z : z + sz * (len - 1);
          break;
        }
      }
  if (rx0 < 0) return false;
  // Lineup: one lead, then uniform[min, max] - 1 followers from the middle blocks except the last, from the tail blocks.
  std::vector<StreetSim::ParadeVehicle> lineup;
  auto add = [&](const std::vector<int>& list) {
    const int b = list[rng_.rand(uint32_t(list.size()))];
    auto it = cfg.blocks.find(b);
    if (it == cfg.blocks.end()) return;
    StreetSim::ParadeVehicle v; v.vattrib = it->second.first; v.spacingTiles = it->second.second / 256.0;
    lineup.push_back(v);
  };
  add(cfg.lead);
  const int total = cfg.minCount + int(rng_.rand(uint32_t(cfg.maxCount - cfg.minCount + 1)));
  for (int i = 1; i < total; i++) add(i + 1 == total ? cfg.tail : cfg.middle);
  const double ox = rx0 + 0.5, oz = rz0 + 0.5, ex = rx1 + 0.5, ez = rz1 + 0.5;
  if (street_->startParade(ox, oz, ex, ez, lineup) == 0) return false;
  fundsRef() -= cfg.cost;
  const int n = std::max(std::abs(rx1 - rx0), std::abs(rz1 - rz0)) + 1;
  const int dx = (rx1 > rx0) - (rx1 < rx0), dz = (rz1 > rz0) - (rz1 < rz0);
  for (int i = 0; i < n; i++) street_->pointSpawnPedestrians(rx0 + dx * i, rz0 + dz * i, cfg.spectators);   // spectators along the route
  paradeRunning_ = true; paradeX_ = rx0; paradeZ_ = rz0;
  return true;
}

// The parade is over when its last vehicle has finished (how the original ends it is untraced [OPEN]).
void Game::paradeTick() {
  if (!paradeRunning_ || !street_) return;
  if (street_->paradeVehicleCount() > 0) return;
  paradeRunning_ = false;
  DisasterOutput out;
  disasters_.stop(DisasterType::Parade, out);
}

void Game::streetSimFrame(uint32_t dtMs) {
  scene_.sprites.clear();
  if (!street_ || !world_.W) return;
  // Detail preferences: traffic/sims off disables the system (spec 08 preferences).
  if (prefsState_.traffic == 3 && prefsState_.sims == 2) return;
  // Full source rebuild is ~30+ ms on a large city (remove+re-add every spawner). Never run it in the
  // same frame as a sim day tick, and at most every kStreetSyncMinFrames when the city changed.
  constexpr int kStreetSyncMinFrames = 30;   // ~500 ms at 60 Hz
  const World& vw = viewWorld();
  // Sources follow the city by events in the original (occupant add / remove notifications; spec 26 5.1), with no periodic scan.
  // Here the equivalent is a signature of every occupant (anchor, id, state) and network piece, recomputed only when the city
  // changed visibly (an edit or a sim day) and compared with the one the sources were built from.
  if (cityVisualSerial_ != streetSigSerial_) {
    streetSigSerial_ = cityVisualSerial_;
    uint64_t sig = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { sig ^= v; sig *= 1099511628211ull; };
    for (const Occupant& o : vw.occupants) mix(uint64_t(uint32_t(o.x)) << 40 ^ uint64_t(uint32_t(o.z)) << 20 ^ o.occId ^ uint64_t(o.state & ~0x300u) << 56);
    for (const NetPiece& p : vw.surface) mix(uint64_t(uint32_t(p.x)) << 32 ^ uint32_t(p.z) ^ uint64_t(p.pieceId) << 12 ^ uint64_t(p.rot) << 60);
    if (sig != streetSig_) {
      streetSig_ = sig;
      streetSyncDue_ = true;
      if (streetSyncCooldown_ < 1) streetSyncCooldown_ = 1;   // at least the next frame (after any day tick)
    }
  }
  if (streetSyncCooldown_ > 0) streetSyncCooldown_--;
  if (streetSyncDue_ && streetSyncCooldown_ == 0 && !simDayThisFrame_ && !simBusy()) {
    syncStreetSources();
    streetSyncDue_ = false;
    streetSyncCooldown_ = kStreetSyncMinFrames;
  }
  const HudSnap& hs = hudNow();
  streetHost_->month = hs.month;
  streetHost_->day = hs.day;
  // Visible tile rectangle from the screen corners.
  AutoView v;
  float us[4], vs[4];
  const int cx[4] = {0, W_, 0, W_}, cy[4] = {0, 0, H_, H_};
  for (int i = 0; i < 4; i++) renderer_.screenToView(cam_, cx[i], cy[i], us[i], vs[i]);
  float umin = *std::min_element(us, us + 4), umax = *std::max_element(us, us + 4);
  float vmin = *std::min_element(vs, vs + 4), vmax = *std::max_element(vs, vs + 4);
  int xa, za, xb, zb;
  Renderer::viewToWorldVertex(cam_.rot, world_.W, world_.H, int(umin) - 4, int(vmin) - 4, xa, za);
  Renderer::viewToWorldVertex(cam_.rot, world_.W, world_.H, int(umax) + 4, int(vmax) + 4, xb, zb);
  v.x0 = std::max(0, std::min(xa, xb)); v.x1 = std::min(world_.W - 1, std::max(xa, xb));
  v.y0 = std::max(0, std::min(za, zb)); v.y1 = std::min(world_.H - 1, std::max(za, zb));
  v.zoom = cam_.zoom; v.rotation = cam_.rot;
  street_->setView(v);
  if (!anyBlockingWindow()) {   // cars and trains keep moving while the game is paused (spec 26 [OBSERVED])
    street_->update(int64_t(dtMs) * 1000);
  }
  paradeTick();
  for (const AutomatonSprite& s : street_->sprites()) {
    SceneSprite sp;
    sp.category = s.category; sp.spriteId = s.spriteId;
    sp.x = int32_t(s.tileX * 256.0); sp.z = int32_t(s.tileY * 256.0);
    sp.height = int32_t(s.height);
    sp.magnify2x = s.flags == 2;
    sp.moving = true;
    scene_.sprites.push_back(sp);
  }
  streetSounds(street_->takeSoundEvents());
}

// Vehicle sounds (spec 26 3.2; gating and volume traced from the original, spec 06): MotionSound and SirenSound attach to the automaton, follow
// it and stop when it is removed. Audible only above ground, at audio zoom 4 or 5 (the original's hard-coded "zoom > 3", not VehicleMinZoom),
// and only while the vehicle is inside the screen: leaving the view ends its voice. Volume = 1024 - (5 - zoom) x VehicleAttenuation; panned by
// screen x. One voice per (automaton, sound); the safety cap keeps a crowded view bounded.
void Game::streetSounds(const std::vector<SoundEvent>& events) {
  if (!audio_) return;
  const int az = cam_.zoom + 1;
  if (ropt_.underground || ropt_.overlay || az <= 3) { audio_->detachAllSounds(); return; }   // gate: above ground, normal (non-overlay) view, audio zoom > 3
  constexpr size_t kMaxVehicleVoices = 24;
  const float att = float(std::max(0, 1024 - (5 - az) * std::max(0, audio_->ini().options.get("VehicleAttenuation", 0)))) / 1024.f;
  auto place = [&](const SoundEvent& e, float& pan) {
    int sx = 0, sy = 0;
    const int tx = std::max(0, std::min(world_.W - 1, int(e.tileX))), tz = std::max(0, std::min(world_.H - 1, int(e.tileY)));
    renderer_.projectSub(cam_, int32_t(e.tileX * 256.0), int32_t(e.tileY * 256.0), int32_t(world_.altAt(tx, tz)) * 256, sx, sy);
    pan = std::max(-1.f, std::min(1.f, (float(sx) - float(W_) / 2.f) / (float(W_) / 2.f)));
    return sx >= 0 && sy >= 0 && sx < W_ && sy < H_;
  };
  for (const SoundEvent& e : events) {
    const uint64_t key = AudioSystem::soundKey(e.automatonId, e.soundId);
    float pan = 0;
    switch (e.type) {
      case SoundEvent::Start: case SoundEvent::Siren: case SoundEvent::Move: {
        const bool on = place(e, pan);
        if (!on) { audio_->detachSound(key); break; }   // off screen: the voice is released
        if (audio_->hasSound(key)) { if (e.type == SoundEvent::Move) audio_->moveSound(key, pan, att); else audio_->attachSound(key, e.soundId, pan, att); }
        else if (audio_->attachedSounds() < kMaxVehicleVoices) audio_->attachSound(key, e.soundId, pan, att);   // appeared, or came back into view
        break;
      }
      case SoundEvent::Stop: audio_->detachSound(key); break;
    }
  }
}

}  // namespace m3k
