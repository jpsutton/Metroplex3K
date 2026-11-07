// Disaster visuals (see disaster_fx.h). Sprite ID layouts, frame periods, speeds and sequences follow spec 21 section 12 [STATIC][DATA];
// anything that spec leaves open is marked [INFERRED].
#include "core/disaster_fx.h"

#include <algorithm>
#include <cmath>

namespace m3k {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTilesPerSecPerWuMs = 1000.0 / 256.0;   // 1 world unit per ms = 3.90625 tiles per second

// ---- sprite sets (colour ID of the image for zoom index k; alpha plane = colour + delta) ----------------------------------------------
constexpr uint32_t kExplFire = 0xFFFD1000u, kExplPlasma = 0xFFFD1020u, kExplDirt = 0xFFFD1040u;   // + size * 5 + z (dirt: + z); alpha +0x200
constexpr uint32_t kJunkBlast = 0x3FB70000u;                                // + rotation + z * 4, 13 frames, alpha +0x14
constexpr uint32_t kSplash = 0xFFFE0000u;                                   // + z, 8 frames, alpha +0x100
constexpr uint32_t kSplashWater1 = 0x2D490000u, kSplashWater2 = 0x48640000u;   // 9 / 10 frames, alpha +5
constexpr uint32_t kFlames = 0xFFFE1000u;                                   // + size * 5 + z, 15 frames, alpha +0x20
constexpr uint32_t kTornadoEnds = 0xFFFE5000u, kTornadoA = 0xFFFE5200u, kTornadoB = 0xFFFE5400u;   // shifted zoom, 8 frames, alpha +0x100
constexpr uint32_t kDirtSpray = 0xFFFE5600u, kWaterSpray = 0xFFFE5800u;    // shifted zoom, 11 frames, alpha +0x100
constexpr uint32_t kPuff = 0xFFFC1400u;                                     // shifted zoom, one static cell, alpha +0x100
constexpr uint32_t kUfo = 0xFFFE1040u;                                      // + z, 5 frames, no plane
constexpr uint32_t kBeamPlane = 0xFFFE1070u;                                // + z, 5 frames, plane only
constexpr uint32_t kPlasmaBall = 0xFFFE1300u;                               // + z, 8 frames, alpha +0x80
constexpr uint32_t kLocustFly = 0x3FAC0000u, kLocustEat = 0x3FAD0000u;      // + z, alpha +5
constexpr uint32_t kPieces[4] = {0x3FB20000u, 0x3FB30000u, 0x3FB40000u, 0x3FB50000u};   // + z * 4 + rotation, 6 frames, alpha +0x14
constexpr uint32_t kCraters[4] = {0x3FC80000u, 0x3FC90000u, 0x3FCA0000u, 0x3FC50000u};   // + z, one frame, alpha +5
constexpr uint32_t kVortexOpen = 0x48590000u, kVortexLoop = 0x4859200Au;    // shifted zoom, 11 frames, alpha +0x1005

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Explosion frame timing (spec 21 s12.3): frame k (k >= 1) appears once elapsed passes T[k - 1] microseconds.
int explThresholdMs(int k) {
  static const int t[5] = {20, 100, 150, 300, 600};   // 20 480 µs rounded down to ms
  return k < 5 ? t[k] : 600 + 300 * (k - 4);
}
int explFrames(int set, int size) { return set == 0 ? (size < 2 ? 9 : size == 2 ? 11 : 13) : set == 1 ? 9 : set == 2 ? 12 : 13; }
}  // namespace

void DisasterFx::clear() {
  fx_.clear(); incoming_.clear(); tornadoes_.clear(); puffs_.clear(); flames_.clear(); crowds_.clear(); vortices_.clear();
  impacts_.clear(); launcher_ = Launcher();
}

int32_t DisasterFx::groundAt(double x, double z) const {
  if (!ground_ || W_ <= 0 || H_ <= 0) return 0;
  return ground_(clampi(int(std::floor(x)), 0, W_ - 1), clampi(int(std::floor(z)), 0, H_ - 1));
}
bool DisasterFx::isWaterAt(double x, double z) const {
  return water_ && W_ > 0 && H_ > 0 && water_(clampi(int(std::floor(x)), 0, W_ - 1), clampi(int(std::floor(z)), 0, H_ - 1));
}

bool DisasterFx::pendingImpacts() const {
  for (const Fx& f : fx_) {
    if (f.kind == Kind::Plasma || f.kind == Kind::Piece) return true;
    if (f.kind == Kind::Saucer || f.kind == Kind::Swarm)
      for (size_t i = f.cur; i < f.q.size(); i++) if (f.q[i].type >= 1 && f.q[i].type <= 4) return true;
  }
  return !incoming_.empty();
}

bool DisasterFx::focus(double& x, double& z) const {
  for (const TornadoView& v : tornadoes_) if (!v.gone) { x = v.x; z = v.z; return true; }
  for (const VortexView& v : vortices_) { x = v.x; z = v.z; return true; }
  for (const Fx& f : fx_) if (f.delay <= 0 && (f.kind == Kind::Saucer || f.kind == Kind::Swarm || f.kind == Kind::Plasma || f.kind == Kind::Piece)) { x = f.x; z = f.z; return true; }
  for (const Fx& f : fx_) if (f.delay <= 0) { x = f.x; z = f.z; return true; }
  if (!crowds_.empty()) { x = crowds_[0].x; z = crowds_[0].z; return true; }
  if (!puffs_.empty()) { x = puffs_[0].x; z = puffs_[0].z; return true; }
  if (!flames_.empty()) { x = flames_[0].x; z = flames_[0].z; return true; }
  return false;
}

void DisasterFx::edgePoint(double& x, double& z, int edge) {
  const double w = std::max(1, W_), h = std::max(1, H_);
  switch (edge & 3) {
    case 0: x = 0; z = rndf() * h; break;
    case 1: x = w; z = rndf() * h; break;
    case 2: x = rndf() * w; z = 0; break;
    default: x = rndf() * w; z = h; break;
  }
}

void DisasterFx::spawnExplosion(double x, double z, int set, int size) {
  Fx f; f.kind = Kind::Explosion; f.x = x; f.z = z; f.set = set; f.size = set == 2 ? 0 : clampi(size, 0, 3);
  const int n = explFrames(set, f.size);
  f.life = explThresholdMs(n - 2) + 300;   // the last frame stays for 300 ms
  spawn(f);
}

void DisasterFx::spawnSplash(double x, double z, int set) {
  Fx f; f.kind = Kind::Splash; f.x = x; f.z = z; f.set = set;
  f.life = (set == 0 ? 8 : set == 1 ? 9 : 10) * 200;   // 200 ms per frame, one shot
  spawn(f);
}

void DisasterFx::spawnSaucers(const DisasterCommand& c) {
  const int n = std::max(1, c.a);
  const size_t first = fx_.size() + incoming_.size();
  std::vector<Fx> sau;
  for (int i = 0; i < n; i++) {
    Fx s; s.kind = Kind::Saucer; s.life = 1 << 30; s.variant = i; s.back = rndi(2) == 1; s.passive = c.b != 0;
    s.x = 1.0 + rndf() * std::max(1, W_ - 2); s.z = 1.0 + rndf() * std::max(1, H_ - 2);
    s.y = double(rndRange(5120, 25600)) / 256.0;   // terrain + 5120..25600 world units
    s.bombMs = rndRange(10000, 20000);
    // Waypoints first (every saucer gets its own two), then the shared commands go to the saucer with the fewest queued (ties: lowest index).
    if (size_t(2 * i + 1) < c.pts3.size()) {
      for (int k = 0; k < 2; k++) { Task t; t.type = 0; t.x = c.pts3[size_t(2 * i + k)].first + 0.5; t.z = c.pts3[size_t(2 * i + k)].second + 0.5; s.q.push_back(t); }
    }
    sau.push_back(s);
  }
  auto give = [&](const Task& t) {
    size_t best = 0;
    for (size_t i = 1; i < sau.size(); i++) if (sau[i].q.size() < sau[best].q.size()) best = i;
    sau[best].q.push_back(t);
  };
  for (const auto& p : c.pts) { Task t; t.type = 1; t.x = p.first + 0.5; t.z = p.second + 0.5; give(t); }
  for (const auto& p : c.pts2) { Task t; t.type = 2; t.x = p.first + 0.5; t.z = p.second + 0.5; give(t); }
  for (int i = 0; i < c.c; i++) { Task t; t.type = 3; t.x = 2 + rndf() * std::max(1, W_ - 4); t.z = 2 + rndf() * std::max(1, H_ - 4); give(t); }
  (void)first;
  for (Fx& s : sau) spawn(s);
}

void DisasterFx::spawnPiece() {
  Fx f; f.kind = Kind::Piece; f.life = 1 << 30; f.variant = rndi(4);
  const double px = 2.0 + rndf() * std::max(1, W_ - 4), pz = 2.0 + rndf() * std::max(1, H_ - 4);   // uniform over the map minus a 2-tile border
  f.x = px - 5000.0 / 256.0; f.z = pz;
  f.y = 80000.0 / 256.0;                                 // starts 80000 world units above the ground
  f.vx = 0.65 * kTilesPerSecPerWuMs; f.vy = -8.94 * kTilesPerSecPerWuMs;   // straight line: x += 0.65 u/ms, y -= 8.94 u/ms
  spawn(f);
}

void DisasterFx::onCommand(const DisasterCommand& c) {
  switch (c.kind) {
    case DisasterCommand::Explosion: spawnExplosion(c.x + 0.5, c.z + 0.5, c.b, c.c); break;
    case DisasterCommand::Splash: spawnSplash(c.x + 0.5, c.z + 0.5, 0); break;
    case DisasterCommand::VortexSpawn: {
      VortexView v; v.x = c.x + 0.5; v.z = c.z + 0.5; v.life = std::max(1, c.a); vortices_.push_back(v); break;
    }
    case DisasterCommand::UfoSwarm: spawnSaucers(c); break;
    case DisasterCommand::UfoLeave: {
      // Each saucer gets an exit command after its queue: explode (flag) or fade over 3 s [STATIC].
      size_t k = 0;
      for (Fx& f : fx_) {
        if (f.kind != Kind::Saucer) continue;
        Task t; t.type = 5; t.aux = k < c.ints.size() ? c.ints[k] : 0; k++;
        f.q.push_back(t);
      }
      break;
    }
    case DisasterCommand::LocustSwarm: {
      const int n = std::max(1, c.a);
      locustEdge_ = rndi(4);   // one map edge for the whole attack
      std::vector<Fx> sw;
      for (int i = 0; i < n; i++) {
        Fx s; s.kind = Kind::Swarm; s.life = 1 << 30; s.variant = i; s.back = rndi(2) == 1;
        edgePoint(s.x, s.z, locustEdge_); s.y = double(rndRange(5120, 10240)) / 256.0;
        sw.push_back(s);
      }
      // Crop tiles are dealt round-robin, each swarm taking the nearest remaining crop to its previous target [STATIC].
      std::vector<char> used(c.pts.size(), 0);
      std::vector<std::pair<double, double>> last(static_cast<size_t>(n));
      for (int i = 0; i < n; i++) last[size_t(i)] = {sw[size_t(i)].x, sw[size_t(i)].z};
      for (size_t left = c.pts.size(), s = 0; left > 0; s = (s + 1) % size_t(n)) {
        size_t best = c.pts.size(); double bd = 1e18;
        for (size_t j = 0; j < c.pts.size(); j++) {
          if (used[j]) continue;
          const double dx = c.pts[j].first + 0.5 - last[s].first, dz = c.pts[j].second + 0.5 - last[s].second, d = dx * dx + dz * dz;
          if (d < bd) { bd = d; best = j; }
        }
        used[best] = 1; --left;
        Task t; t.type = 4; t.x = c.pts[best].first + 0.5; t.z = c.pts[best].second + 0.5;
        sw[s].q.push_back(t); last[s] = {t.x, t.z};
      }
      for (Fx& s : sw) spawn(s);
      break;
    }
    case DisasterCommand::Crater: {
      if (c.pts.size() < 2) break;
      const double strength = c.a / 10.0;                        // crater strength c in [0, 10)
      Fx f; f.kind = Kind::Crater; f.x = (c.pts[0].first + c.pts[1].first + 1) * 0.5; f.z = (c.pts[0].second + c.pts[1].second + 1) * 0.5;
      f.variant = clampi(int(strength * 100.0 / 255.0), 0, 3);   // class k = floor(c * 100 / 255): 2x2, 3x3, 4x4, 5x5
      f.life = 307000;                                           // 0x44860916 counted down by dt / 1024: about 307 s
      spawn(f);
      break;
    }
    default: break;
  }
}

void DisasterFx::syncState(const DisasterManager& m) {
  // Tornado funnels: one per touched-down instance; the effect carries the tornado (spec 21 s12.5). An instance that is gone stops its funnel.
  for (TornadoView& v : tornadoes_) v.gone = v.gone || v.stage == 2;
  for (const TornadoInst& t : m.tornadoes()) {
    if (t.phase < 5) continue;
    bool found = false;
    for (TornadoView& v : tornadoes_) if (v.uid == t.uid) found = true;
    if (found) continue;
    TornadoView v; v.uid = t.uid; v.x = t.px; v.z = t.pz; v.dx = t.dx; v.dz = t.dz; v.speed = t.speed; v.variant = t.variant;
    tornadoes_.push_back(v);
  }
  for (TornadoView& v : tornadoes_) {
    if (v.stage == 2) continue;
    bool alive = false;
    for (const TornadoInst& t : m.tornadoes()) if (t.uid == v.uid) alive = true;
    if (!alive) { v.stage = 2; v.stageMs = 0; }
  }
  // Toxic puffs: static (never drift); a puff whose cloud is gone fades out.
  {
    std::vector<std::pair<double, double>> cur;
    for (const CloudInst& c : m.clouds()) for (const auto& p : c.puffs) cur.push_back(p);
    size_t live = 0;
    for (const PuffView& p : puffs_) if (p.closing < 0) live++;
    size_t seen = 0;
    for (PuffView& p : puffs_) {
      if (p.closing >= 0) continue;
      if (seen < cur.size()) { p.x = cur[seen].first; p.z = cur[seen].second; } else p.closing = 0;
      seen++;
    }
    const int lo = m.tune.toxMinHeight, hi = std::max(m.tune.toxMaxHeight, lo + 1);
    for (size_t i = live; i < cur.size(); i++) {
      PuffView p; p.x = cur[i].first; p.z = cur[i].second; p.h = double(rndRange(lo * 256, hi * 256)) / 256.0;   // MinHeightAboveTerrain..Max (stored x 256)
      puffs_.push_back(p);
    }
  }
  // Flames: one per burning occupant, size = intensity + 1 (spec 21 s12.2).
  {
    std::vector<FlameView> keep;
    for (const BurnRecord& b : m.burning()) {
      FlameView f; f.x = b.x + 0.5; f.z = b.z + 0.5; f.size = clampi(b.intensity, 0, 2) + 1;
      const uint32_t h = uint32_t(b.x) * 73856093u ^ uint32_t(b.z) * 19349663u;
      f.period = 400 + int(h % 100); f.phase = int((h >> 8) % 15);   // random per flame: 0.4-0.5 s per frame
      keep.push_back(f);
    }
    flames_.swap(keep);
  }
  // Riot crowds: one per riot that has broken out (phase 2), starting on the riot site's road piece (spec 21 s12.10).
  {
    std::vector<std::pair<int, int>> sites;
    for (const RiotInst& r : m.riots()) if (r.phase == 2) sites.emplace_back(r.x, r.z);
    while (crowds_.size() > sites.size()) crowds_.pop_back();
    for (size_t i = crowds_.size(); i < sites.size(); i++) { CrowdView cv; cv.x = sites[i].first + 0.5; cv.z = sites[i].second + 0.5; crowds_.push_back(cv); }
  }
  // Space-junk launcher: runs through the attack window T3..T4 (spec 21 s12.11); one piece per RandRange(768n, 1536n) µs, n = floor(0x44860913 / 1024).
  launcher_.on = false;
  for (const JunkInst& j : m.junk()) if (j.phase == 4 && j.counter < j.T[4]) launcher_.on = true;
  launcher_.scale = std::max(1, m.tune.junkDuration / 1024);
}

void DisasterFx::updateSaucer(Fx& f, int dt, bool& done) {
  const double dts = dt / 1000.0, speed = 2.6 * kTilesPerSecPerWuMs;   // 2.6 world units/ms; 1 tile = 1 altitude unit = 256 world units
  if (f.fadeMs >= 0) { f.fadeLeft -= dt; if (f.fadeLeft <= 0) done = true; return; }
  // Plasma bombs (attack kind only): every 10-20 s while not hovering, towards a ground point 3-10 tiles off on each axis.
  if (!f.passive && !f.hovering) {
    f.bombMs -= dt;
    if (f.bombMs <= 0) {
      f.bombMs = rndRange(10000, 20000);
      Fx b; b.kind = Kind::Plasma; b.life = 1 << 30; b.x = f.x; b.z = f.z; b.y = f.y;
      b.tx = f.x + double(rndRange(3, 11)) * (rndi(2) ? 1 : -1); b.tz = f.z + double(rndRange(3, 11)) * (rndi(2) ? 1 : -1);
      const double flight = 3.0 + rndf() * 3.0;   // about 3-6 s
      b.vx = (b.tx - b.x) / flight; b.vz = (b.tz - b.z) / flight; b.vy = -std::max(1.0, b.y) / flight;
      spawn(b);
    }
  }
  if (f.hovering) {
    f.hover -= dt;
    if (f.hover > 0) return;
    f.hovering = false; f.beam = false;
    const Task& t = f.q[f.cur];
    report(t.type == 1 ? FxImpact::UfoBeam : FxImpact::UfoCrop, int(std::floor(t.x)), int(std::floor(t.z)));
    f.cur++;
    f.y += 20.0;   // then climbs 5120 world units
    return;
  }
  if (f.cur >= f.q.size()) {
    // Idle: hop to a random point within +-5 tiles at terrain + 5120..10240 units; after the 6th idle hop the saucer leaves by fading.
    if (f.idleHops >= 6) { f.fadeMs = f.fadeLeft = 3000; return; }
    Task t; t.type = 0;
    t.x = clampi(int(f.x) + rndRange(-5, 6), 1, std::max(1, W_ - 2)) + 0.5; t.z = clampi(int(f.z) + rndRange(-5, 6), 1, std::max(1, H_ - 2)) + 0.5;
    t.aux = rndRange(5120, 10240);
    f.q.push_back(t); f.idleHops++;
    return;
  }
  Task& t = f.q[f.cur];
  if (t.type == 3) { report(FxImpact::UfoAbduction, int(t.x), int(t.z)); f.cur++; return; }
  if (t.type == 5) {
    if (t.aux) { spawnExplosion(f.x, f.z, 1, 3); done = true; }   // explodes on exit: plasma explosion (variant 5), removed at once
    else { f.fadeMs = f.fadeLeft = 3000; f.cur++; }
    return;
  }
  // Straight 3-D line to the leg target; never lower than terrain + 30 units while travelling.
  const double ty = t.type == 0 ? std::max(20.0, double(t.aux) / 256.0) : 16.0;
  const double dx = t.x - f.x, dz = t.z - f.z, dy = ty - f.y;
  const double dist = std::sqrt(dx * dx + dz * dz + dy * dy), step = speed * dts;
  if (dist > step && dist > 1e-6) {
    f.x += dx / dist * step; f.z += dz / dist * step; f.y += dy / dist * step;
    if (t.type == 0 && f.y < 30.0 && dist > 30.0) f.y = 30.0;
    return;
  }
  f.x = t.x; f.z = t.z; f.y = ty;
  if (t.type == 0) { f.cur++; return; }
  f.hovering = true; f.hover = 5001; f.beam = (t.type == 1);   // 5001 ms over the lot centre, with the beam for a destroy target
}

void DisasterFx::updateSwarm(Fx& f, int dt, bool& done) {
  const double dts = dt / 1000.0, speed = 0.4 * kTilesPerSecPerWuMs;   // 0.4 world units/ms
  if (f.eating) {   // arrived on the last update: report "eat" once and move on
    f.eating = false;
    const Task& t = f.q[f.cur];
    report(FxImpact::LocustEat, int(std::floor(t.x)), int(std::floor(t.z)));
    f.cur++;
    return;
  }
  if (f.cur >= f.q.size()) { done = true; return; }   // a swarm with an empty queue is removed on the next update
  const Task& t = f.q[f.cur];
  const double dx = t.x - f.x, dz = t.z - f.z, dh = std::sqrt(dx * dx + dz * dz), step = speed * dts;
  if (dh > step && dh > 1e-6) {
    f.x += dx / dh * step; f.z += dz / dh * step;
    const double want = dh > 6.0 ? 30.0 : 6.0;                    // cruise at terrain + 30, drop to terrain + 6 at the crop
    f.y += std::max(-step, std::min(step, want - f.y));
  } else { f.x = t.x; f.z = t.z; f.y = 6.0; f.eating = true; }
  // Flora that drops out of the swarm's 5 x 5 neighbourhood is damaged (0x20) whenever the swarm changes tile [STATIC].
  const int cx = int(std::floor(f.x)), cz = int(std::floor(f.z));
  if (f.ox == -1) { f.ox = cx; f.oz = cz; }
  if (cx != f.ox || cz != f.oz) {
    for (int z = f.oz - 2; z <= f.oz + 2; z++)
      for (int x = f.ox - 2; x <= f.ox + 2; x++)
        if ((x < cx - 2 || x > cx + 2 || z < cz - 2 || z > cz + 2) && x >= 0 && z >= 0 && x < W_ && z < H_) report(FxImpact::LocustFlora, x, z);
    f.ox = cx; f.oz = cz;
  }
}

void DisasterFx::update(uint32_t dtMs) {
  const int dt = int(std::min<uint32_t>(dtMs, 250));   // a long frame must not teleport the flyers
  const double dts = dt / 1000.0;
  flameClockMs_ += dt;
  const size_t n0 = fx_.size();
  updating_ = true;
  for (size_t i = 0; i < n0; i++) {
    Fx& f = fx_[i];
    if (f.delay > 0) { f.delay -= dt; if (f.delay > 0) continue; f.delay = 0; }
    f.age += dt;
    bool done = false;
    switch (f.kind) {
      case Kind::Explosion: case Kind::Splash: case Kind::Crater:
        if (f.age >= f.life) done = true;
        break;
      case Kind::Saucer: updateSaucer(f, dt, done); break;
      case Kind::Swarm: updateSwarm(f, dt, done); break;
      case Kind::Plasma: case Kind::Piece: {
        if (f.kind == Kind::Plasma) { f.x += f.vx * dts; f.z += f.vz * dts; } else { f.x += f.vx * dts; }
        f.y += f.vy * dts;
        if (f.y <= 0.0) {
          const int tx = int(std::floor(f.kind == Kind::Plasma ? f.tx : f.x)), tz = int(std::floor(f.kind == Kind::Plasma ? f.tz : f.z));
          const bool water = isWaterAt(tx + 0.5, tz + 0.5), inMap = tx >= 0 && tz >= 0 && tx < W_ && tz < H_;
          if (f.kind == Kind::Plasma) {
            if (inMap) { if (water) spawnSplash(tx + 0.5, tz + 0.5, 0); else spawnExplosion(tx + 0.5, tz + 0.5, 1, 2); report(FxImpact::UfoBomb, tx, tz); }
          } else if (inMap) {
            if (water) spawnSplash(tx + 0.5, tz + 0.5, f.variant == 0 ? 2 : 0);   // the falling-UFO piece makes the 10-frame splash
            else spawnExplosion(tx + 0.5, tz + 0.5, 3, 0);                         // set 3 = the space-junk impact explosion (variant 8)
            report(FxImpact::Debris, tx, tz);
          }
          done = true;
        }
        break;
      }
    }
    if (done) f.life = -1;
  }
  updating_ = false;
  fx_.erase(std::remove_if(fx_.begin(), fx_.end(), [](const Fx& f) { return f.life < 0; }), fx_.end());
  fx_.insert(fx_.end(), incoming_.begin(), incoming_.end());
  incoming_.clear();

  // Tornado funnels (spec 21 s12.5): straight line at speed tiles/min / 60, opening -> loop A/B cycles -> closing (the opening backwards).
  for (TornadoView& v : tornadoes_) {
    v.age += dt; v.stageMs += dt;
    if (v.stage < 2) {
      v.x += v.dx * v.speed / 60.0 * dts; v.z += v.dz * v.speed / 60.0 * dts;
      const int tx = int(std::floor(v.x)), tz = int(std::floor(v.z));
      if (tx < 0 || tz < 0 || tx >= W_ || tz >= H_) {
        FxImpact im; im.kind = FxImpact::TornadoLeft; im.id = v.uid; impacts_.push_back(im);
        v.stage = 2; v.stageMs = 0;
      } else if (tx != v.tx || tz != v.tz) {
        v.tx = tx; v.tz = tz;
        FxImpact im; im.kind = FxImpact::TornadoTile; im.id = v.uid; im.x = tx; im.z = tz; im.fx = float(v.x); im.fz = float(v.z);
        impacts_.push_back(im);
      }
    }
    if (v.stage == 0 && v.stageMs >= 8 * 200) { v.stage = 1; v.stageMs -= 8 * 200; }
    if (v.stage == 1 && v.stageMs >= 8 * 200) { v.stageMs -= 8 * 200; v.loopB = rndi(10) > 6; }   // each 8-frame cycle: B with 30 %
    if (v.stage == 2 && v.stageMs >= 8 * 200) v.gone = true;
  }
  tornadoes_.erase(std::remove_if(tornadoes_.begin(), tornadoes_.end(), [](const TornadoView& v) { return v.gone; }), tornadoes_.end());
  // Toxic puffs: opacity changes at 0.00015 per ms (about 6.7 s) in and out.
  for (PuffView& p : puffs_) { p.age += dt; if (p.closing >= 0) p.closing += dt; }
  puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(), [](const PuffView& p) { return p.closing * 0.00015 >= 1.0; }), puffs_.end());
  // Space-junk launcher.
  if (launcher_.on) {
    launcher_.nextMs -= dt;
    if (launcher_.nextMs <= 0) {
      spawnPiece();
      launcher_.nextMs = rndRange(768 * launcher_.scale, 1536 * launcher_.scale) / 1000;   // µs -> ms
    }
  } else launcher_.nextMs = 0;
  // Riot crowds: drive along the road lanes at about 0.95 tiles/s [INFERRED next-piece choice: a random adjacent road tile] and throw something
  // every 0.3-0.6 s (the 0x2484A289 / 0x2484A290 range), which ignites the building it lands on.
  for (CrowdView& cv : crowds_) {
    cv.walkMs += dt; cv.throwMs += dt; cv.ageMs += dt;
    bool lost = false;
    if (cv.walkMs >= 1050) {
      cv.walkMs = 0;
      bool moved = false;
      static const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      const int tx = int(cv.x), tz = int(cv.z), start = rndi(4);
      for (int k = 0; k < 4; k++) {
        const int nx = tx + d4[(start + k) & 3][0], nz = tz + d4[(start + k) & 3][1];
        if (nx < 0 || nz < 0 || nx >= W_ || nz >= H_ || (road_ && !road_(nx, nz))) continue;
        {   // heading in 32 steps, as the automata use (0 = +x, counter-clockwise on screen)
          double a = std::atan2(double(nz - tz), double(nx - tx)); if (a < 0) a += 2 * kPi;
          const int hv = int(std::lround(a * 32.0 / (2 * kPi))); cv.heading32 = hv >= 32 ? 0 : hv;
        }
        cv.x = nx + 0.5; cv.z = nz + 0.5;
        report(FxImpact::RioterWalk, nx, nz);
        moved = true;
        break;
      }
      if (!moved) { report(FxImpact::RioterLost, tx, tz); lost = true; }
    }
    if (cv.throwMs >= cv.nextThrow) {
      cv.throwMs = 0; cv.nextThrow = rndRange(300, 600);
      const double phi = rndf() * 2 * kPi, d = 0.5 + rndf() * 3.0;   // [INFERRED] landing distance of a projectile
      const int ix = clampi(int(std::floor(cv.x + std::cos(phi) * d)), 0, std::max(0, W_ - 1)), iz = clampi(int(std::floor(cv.z + std::sin(phi) * d)), 0, std::max(0, H_ - 1));
      report(FxImpact::Rioter, ix, iz);
    }
    if (lost) cv.walkMs = -1000000;   // destroyed: removed below; the riot relocates and a new crowd starts at its new site
  }
  crowds_.erase(std::remove_if(crowds_.begin(), crowds_.end(), [](const CrowdView& c) { return c.walkMs < -100000; }), crowds_.end());
  // Whirlpool vortex (spec 21 s12.6): speed 0.12 u/ms, accepted only over a 9 x 9 block of water, else the heading turns in steps of 2*pi/14.
  for (VortexView& v : vortices_) {
    v.age += dt;
    if (v.closing) { v.closeAge += dt; continue; }
    if (v.age >= v.life) { v.closing = true; v.closeAge = 0; continue; }
    auto allWater = [&](double px, double pz) {
      const int bx = int(std::floor(px - 4.5)), bz = int(std::floor(pz - 4.5));
      for (int z = bz; z < bz + 9; z++) for (int x = bx; x < bx + 9; x++) if (!water_ || x < 0 || z < 0 || x >= W_ || z >= H_ || !water_(x, z)) return false;
      return true;
    };
    const double sx = v.vx * dt / 256.0, sz = v.vz * dt / 256.0;
    if (allWater(v.x + sx, v.z + sz)) { v.x += sx; v.z += sz; }
    else {
      double th = std::atan2(v.vz, v.vx);
      for (int k = 0; k < 14; k++) {
        th += 2 * kPi / 14;
        const double nvx = 0.12 * std::cos(th), nvz = 0.12 * std::sin(th);
        if (allWater(v.x + nvx * dt / 256.0, v.z + nvz * dt / 256.0)) { v.vx = nvx; v.vz = nvz; break; }
      }
    }
    const int tx = int(std::floor(v.x)), tz = int(std::floor(v.z));
    if (tx != v.tx || tz != v.tz) { v.tx = tx; v.tz = tz; report(FxImpact::VortexAt, tx, tz); }
  }
  vortices_.erase(std::remove_if(vortices_.begin(), vortices_.end(), [](const VortexView& v) { return v.closing && v.closeAge >= 11 * 250; }), vortices_.end());
}

bool DisasterFx::push(std::vector<SceneSprite>& out, uint32_t colourId, int alphaDelta, int frames, int frame, double x, double z, double heightUnits,
                      int level, bool mag) const {
  if (!data_ || level <= 0) return false;
  const SpriteImage* base = data_->sprite(0, colourId);
  if (!base || base->img.w <= 0) return false;
  const int nf = frames > 0 ? frames : std::max(1, base->img.w / std::max(1, base->L + base->R));
  const int fw = base->img.w / nf;
  if (fw <= 0) return false;
  frame = ((frame % nf) + nf) % nf;
  SceneSprite s;
  s.category = fxFrameCat(fw, frame); s.spriteId = colourId;
  if (alphaDelta && data_->spritePlane(0, colourId + uint32_t(alphaDelta))) {
    s.hasAlpha = true; s.alphaCategory = s.category; s.alphaId = colourId + uint32_t(alphaDelta);
  }
  s.globalLevel = std::min(32, level); s.magnify2x = mag;
  s.x = int32_t(std::lround(x * 256.0)); s.z = int32_t(std::lround(z * 256.0));
  s.height = groundAt(x, z) + int32_t(std::lround(heightUnits * 256.0));
  s.moving = true;
  out.push_back(s);
  return true;
}

bool DisasterFx::pushTint(std::vector<SceneSprite>& out, uint32_t top, uint32_t planeId, int frames, int frame, double x, double z, double heightUnits) const {
  (void)top;
  if (!data_) return false;
  const Plane* pl = data_->spritePlane(0, planeId);
  const int nf = std::max(1, frames);
  if (!pl || pl->w <= 0) return false;
  const int fw = pl->w / nf;
  frame = ((frame % nf) + nf) % nf;
  SceneSprite s;
  s.category = fxTintBeamCat(fw, frame); s.spriteId = planeId;
  s.hasAlpha = true; s.alphaCategory = fxFrameCat(fw, frame); s.alphaId = planeId;
  s.globalLevel = 20;   // translucent beam [INFERRED strength]
  s.x = int32_t(std::lround(x * 256.0)); s.z = int32_t(std::lround(z * 256.0));
  s.height = groundAt(x, z) + int32_t(std::lround(heightUnits * 256.0));
  s.moving = true;
  out.push_back(s);
  return true;
}

void DisasterFx::emitFx(const Fx& f, std::vector<SceneSprite>& out) const {
  if (f.delay > 0) return;
  const int z = zoom_, r = rot_ & 3;
  const int level = f.fadeMs >= 0 ? std::max(0, 32 * f.fadeLeft / std::max(1, f.fadeMs)) : 32;
  switch (f.kind) {
    case Kind::Explosion: {
      const int n = explFrames(f.set, f.size);
      int frame = 0;
      while (frame < n - 1 && f.age >= explThresholdMs(frame)) frame++;
      if (f.set == 0) push(out, kExplFire + uint32_t(f.size * 5 + z), 0x200, n, frame, f.x, f.z, 0);
      else if (f.set == 1) push(out, kExplPlasma + uint32_t(f.size * 5 + z), 0x200, n, frame, f.x, f.z, 0);
      else if (f.set == 2) push(out, kExplDirt + uint32_t(z), 0x200, n, frame, f.x, f.z, 0);
      else push(out, kJunkBlast + uint32_t(z * 4 + r), 0x14, 13, frame, f.x, f.z, 0);
      break;
    }
    case Kind::Splash: {
      const int frame = f.age / 200;
      if (f.set == 0) push(out, kSplash + uint32_t(z), 0x100, 8, frame, f.x, f.z, 0);
      else if (f.set == 1) push(out, kSplashWater1 + uint32_t(z), 5, 9, frame, f.x, f.z, 0);
      else push(out, kSplashWater2 + uint32_t(z), 5, 10, frame, f.x, f.z, 0);
      break;
    }
    case Kind::Crater: push(out, kCraters[f.variant] + uint32_t(z), 5, 1, 0, f.x, f.z, 0); break;
    case Kind::Plasma: push(out, kPlasmaBall + uint32_t(z), 0x80, 8, f.age / 100, f.x, f.z, f.y); break;
    case Kind::Piece: push(out, kPieces[f.variant] + uint32_t(z * 4 + r), 0x14, 6, f.age / 300, f.x, f.z, f.y); break;
    case Kind::Saucer: {
      const int fr = (f.age / 100) % 5;
      if (f.beam) pushTint(out, kFxTintBeamTop, kBeamPlane + uint32_t(z), 5, f.age / 300, f.x, f.z, std::max(0.0, f.y - 5.0));
      push(out, kUfo + uint32_t(z), 0, 5, f.back ? 4 - fr : fr, f.x, f.z, f.y, level);
      break;
    }
    case Kind::Swarm: {
      const int fr = f.age / 100;
      const uint32_t base = f.eating || (f.cur < f.q.size() && f.y <= 6.5) ? kLocustEat : kLocustFly;
      push(out, base + uint32_t(z), 5, 0, f.back ? -fr : fr, f.x, f.z, f.y, level);
      break;
    }
  }
}

void DisasterFx::emit(int zoom, int rot, std::vector<SceneSprite>& out) const {
  zoom_ = clampi(zoom, 0, 4); rot_ = rot & 3;
  const int z = zoom_, k = z < 4 ? z + 1 : 4;   // shifted zoom: image k = z + 1 (4 at zoom 4, drawn doubled)
  const bool mag = z == 4;
  for (const Fx& f : fx_) emitFx(f, out);
  for (const FlameView& f : flames_)
    push(out, kFlames + uint32_t(f.size * 5 + z), 0x20, 15, int(flameClockMs_ / std::max(1, f.period)) + f.phase, f.x, f.z, 0);
  for (const TornadoView& v : tornadoes_) {
    const int frame = v.stage == 0 ? v.stageMs / 200 : v.stage == 1 ? (v.stageMs / 200) % 8 : 7 - std::min(7, v.stageMs / 200);
    const uint32_t body = v.stage == 1 ? (v.loopB ? kTornadoB : kTornadoA) : kTornadoEnds;
    if (v.stage == 1) {
      const bool wet = isWaterAt(v.x, v.z);
      push(out, (wet ? kWaterSpray : kDirtSpray) + uint32_t(k), 0x100, 11, (v.age / 200), v.x, v.z, 0, 32, mag);
    }
    push(out, body + uint32_t(k), 0x100, 8, frame, v.x, v.z, 0, 32, mag);
  }
  for (const PuffView& p : puffs_) {
    const double op = p.closing >= 0 ? 1.0 - p.closing * 0.00015 : std::min(1.0, p.age * 0.00015);
    push(out, kPuff + uint32_t(k), 0x100, 0, 0, p.x, p.z, p.h, int(std::lround(op * 32.0)), mag);
  }
  for (const VortexView& v : vortices_) {
    const int fp = 250;
    int frame; uint32_t base;
    if (v.closing) { base = kVortexOpen; frame = 10 - std::min(10, v.closeAge / fp); }
    else if (v.age < 11 * fp) { base = kVortexOpen; frame = v.age / fp; }
    else { base = kVortexLoop; frame = (v.age - 11 * fp) / fp; }
    push(out, base + uint32_t(k), 0x1005, 11, frame, v.x, v.z, 6.0, 32, mag);
  }
}

}  // namespace m3k
