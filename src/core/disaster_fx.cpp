// Disaster visuals (see disaster_fx.h). Sprite ID layouts are [DATA] from the shipped SII files; every timing, path, height and set
// choice below is [INFERRED] unless a comment says otherwise (spec 21 / 26 leave them [OPEN], questions Q-AL..Q-AN).
#include "core/disaster_fx.h"

#include <algorithm>
#include <cmath>

namespace m3k {
namespace {
constexpr int kFrameMs = kAnimTickMs;       // effect sprites animate at the 15 Hz animation tick [INFERRED]
constexpr double kUnitsPerTile = 8.0;       // altitude units per tile of height used for flying objects [INFERRED]

// ---- sprite sets: colour ID of image k, alpha plane = colour + alphaDelta (EffectSprites.sii / disaster_*.sii) ----
// Explosion, fire type: 4 variants x 5 zooms (k = variant * 5 + zoom), 9 / 9 / 11 / 13 frames, alpha +0x200.
constexpr uint32_t kExplFire = 0xFFFD1000u, kExplPlasma = 0xFFFD1020u;
constexpr int kExplFireFrames[4] = {9, 9, 11, 13};
constexpr uint32_t kSplash = 0xFFFE0000u;                                   // 5 zooms, 8 frames, alpha +0x100
constexpr uint32_t kTornadoEnds = 0xFFFE5000u, kTornadoT1 = 0xFFFE5200u, kTornadoT2 = 0xFFFE5400u;   // 8 frames, alpha +0x100
constexpr uint32_t kDirtSpray = 0xFFFE5600u, kWaterSpray = 0xFFFE5800u;    // 11 frames, alpha +0x100
constexpr uint32_t kUfo = 0xFFFE1040u;                                      // 5 frames, no opacity plane
constexpr uint32_t kPlasmaBall = 0xFFFE1300u;                               // 8 frames, alpha +0x80
constexpr uint32_t kLocustFly = 0x3FAC0000u, kLocustEat = 0x3FAD0000u;      // 6 frames, alpha +5
constexpr uint32_t kJunkBodies[5] = {0x3FB20000u, 0x3FB30000u, 0x3FB40000u, 0x3FB50000u, 0x3FB60000u};   // k = zoom*4+rot, 6 frames, alpha +0x14
constexpr uint32_t kJunkBlast = 0x3FB70000u;                                // k = zoom*4+rot, 13 frames, alpha +0x14
constexpr uint32_t kCraters[4] = {0x3FC80000u, 0x3FC90000u, 0x3FCA0000u, 0x3FC50000u};   // 2x2, 3x3, 4x4, 5x5; one frame, alpha +5
constexpr uint32_t kToxOpen = 0x48580000u, kToxCycle = 0x4858200Au, kToxClose = 0x48584014u;   // 4 frames, alpha +0x1005
constexpr uint32_t kToxRain = 0x48590000u;                                  // 11 frames, alpha +0x1005
constexpr uint32_t kVortexOuter = 0x2D490000u, kVortexInner = 0x2D4A0000u;  // 9 frames, alpha +5

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
}  // namespace

void DisasterFx::clear() {
  fx_.clear(); incoming_.clear(); tornadoes_.clear(); puffs_.clear(); rain_.clear(); vortices_.clear(); impacts_.clear();
}

int32_t DisasterFx::groundAt(double x, double z) const {
  if (!ground_ || W_ <= 0 || H_ <= 0) return 0;
  return ground_(clampi(int(std::floor(x)), 0, W_ - 1), clampi(int(std::floor(z)), 0, H_ - 1));
}

bool DisasterFx::pendingImpacts() const {
  for (const Fx& f : fx_) {
    if (f.kind == Kind::Plasma || f.kind == Kind::Junk) return true;
    if ((f.kind == Kind::Saucer || f.kind == Kind::Locust)) {
      for (size_t i = f.leg; i < f.route.size(); i++) if (f.route[i].type == 1 || f.route[i].type == 3 || f.route[i].type == 4) return true;
    }
  }
  return false;
}

bool DisasterFx::focus(double& x, double& z) const {
  for (const TornadoView& v : tornadoes_) if (v.alive) { x = v.x; z = v.z; return true; }
  for (const VortexView& v : vortices_) { x = v.x; z = v.z; return true; }
  for (const Fx& f : fx_) if (f.delay <= 0 && (f.kind == Kind::Saucer || f.kind == Kind::Locust || f.kind == Kind::Plasma || f.kind == Kind::Junk)) { x = f.x; z = f.z; return true; }
  for (const Fx& f : fx_) if (f.delay <= 0) { x = f.x; z = f.z; return true; }
  if (!puffs_.empty()) { x = puffs_[0].x; z = puffs_[0].z; return true; }
  return false;
}

void DisasterFx::edgePoint(double& x, double& z) {
  const double w = std::max(1, W_), h = std::max(1, H_);
  switch (rndi(4)) {
    case 0: x = 0; z = rndf() * h; break;
    case 1: x = w; z = rndf() * h; break;
    case 2: x = rndf() * w; z = 0; break;
    default: x = rndf() * w; z = h; break;
  }
}

void DisasterFx::spawnExplosion(double x, double z, Kind k) {
  Fx f; f.kind = k; f.x = x; f.z = z;
  f.variant = rndi(4);
  const int frames = k == Kind::Explosion ? kExplFireFrames[f.variant] : k == Kind::PlasmaBlast ? 9 : 13;
  f.life = frames * kFrameMs;
  spawn(f);
}

void DisasterFx::onCommand(const DisasterCommand& c) {
  switch (c.kind) {
    case DisasterCommand::Explosion: spawnExplosion(c.x + 0.5, c.z + 0.5); break;
    case DisasterCommand::Splash: {
      Fx f; f.kind = Kind::Splash; f.x = c.x + 0.5; f.z = c.z + 0.5; f.life = 8 * kFrameMs; spawn(f); break;
    }
    case DisasterCommand::VortexSpawn: {
      VortexView v; v.x = c.x + 0.5; v.z = c.z + 0.5; v.life = std::max(1, c.a); vortices_.push_back(v); break;
    }
    case DisasterCommand::UfoSwarm: {
      const int n = std::max(1, c.a);
      for (int i = 0; i < n; i++) {
        Fx s; s.kind = Kind::Saucer; s.speed = 10.0; s.life = 1 << 30;
        edgePoint(s.x, s.z); s.h = 3.5 * kUnitsPerTile; s.variant = i;
        if (size_t(2 * i + 1) < c.pts3.size()) {
          s.route.push_back({c.pts3[size_t(2 * i)].first, c.pts3[size_t(2 * i)].second, 0});
          s.route.push_back({c.pts3[size_t(2 * i + 1)].first, c.pts3[size_t(2 * i + 1)].second, 0});
        }
        for (size_t j = size_t(i); j < c.pts.size(); j += size_t(n)) s.route.push_back({c.pts[j].first, c.pts[j].second, 1});
        for (size_t j = size_t(i); j < c.pts2.size(); j += size_t(n)) s.route.push_back({c.pts2[j].first, c.pts2[j].second, 3});
        if (s.route.empty()) s.route.push_back({int(rndf() * std::max(1, W_)), int(rndf() * std::max(1, H_)), 0});
        double ex, ez; edgePoint(ex, ez);
        s.route.push_back({int(ex), int(ez), 2});
        spawn(s);
      }
      break;
    }
    case DisasterCommand::UfoLeave: {
      size_t k = 0;
      for (size_t i = 0; i < fx_.size() && k < c.ints.size();) {
        if (fx_[i].kind != Kind::Saucer) { i++; continue; }
        if (c.ints[k++] != 0) {   // a saucer that explodes on exit [spec 21 s6.3]
          spawnExplosion(fx_[i].x, fx_[i].z, Kind::PlasmaBlast);
          fx_.erase(fx_.begin() + long(i));
        } else i++;
      }
      break;
    }
    case DisasterCommand::LocustSwarm: {
      const int n = std::max(1, c.a);
      for (int i = 0; i < n; i++) {
        Fx s; s.kind = Kind::Locust; s.speed = 10.0; s.life = 1 << 30; s.variant = i * 7 + 3;
        edgePoint(s.x, s.z); s.h = 10.0;
        for (size_t j = size_t(i); j < c.pts.size(); j += size_t(n)) s.route.push_back({c.pts[j].first, c.pts[j].second, 4});
        if (s.route.empty()) s.route.push_back({int(rndf() * std::max(1, W_)), int(rndf() * std::max(1, H_)), 0});
        double ex, ez; edgePoint(ex, ez);
        s.route.push_back({int(ex), int(ez), 2});
        spawn(s);
      }
      break;
    }
    case DisasterCommand::DebrisSpawn: {
      // [INFERRED] the class value (2000 / 5000 / 10000) / 250 pieces fall over the duration (b, in microseconds), uniformly over the map.
      const int n = std::max(1, c.a / 250), durMs = std::max(500, c.b / 1000);
      for (int i = 0; i < n && W_ > 0; i++) {
        Fx f; f.kind = Kind::Junk; f.life = 1 << 30; f.variant = rndi(5);
        f.targetX = rndi(W_); f.targetZ = rndi(H_);
        f.x = f.targetX + 0.5; f.z = f.targetZ + 0.5;
        f.h = 7.5 * kUnitsPerTile; f.delay = rndi(durMs);
        const double fall = 1.0 + rndf() * 0.6;
        f.vh = -f.h / fall;
        spawn(f);
      }
      break;
    }
    case DisasterCommand::Crater: {
      if (c.pts.size() < 2) break;
      const int w = c.pts[1].first - c.pts[0].first + 1;
      Fx f; f.kind = Kind::Crater; f.x = (c.pts[0].first + c.pts[1].first + 1) * 0.5; f.z = (c.pts[0].second + c.pts[1].second + 1) * 0.5;
      f.variant = w <= 2 ? 0 : w == 3 ? 1 : w == 4 ? 2 : 3;
      f.life = 90000;
      spawn(f);
      break;
    }
    default: break;
  }
}

void DisasterFx::syncState(const DisasterManager& m) {
  // Tornado funnels follow the instances (index order); a funnel without an instance fades out.
  const auto& ts = m.tornadoes();
  for (size_t i = 0; i < ts.size(); i++) {
    if (i >= tornadoes_.size()) tornadoes_.emplace_back();
    TornadoView& v = tornadoes_[i];
    const bool down = ts[i].phase >= 5;
    v.fade = -1;
    if (down) {
      if (!v.alive) { v.alive = true; v.age = 0; v.x = ts[i].px; v.z = ts[i].pz; }
      v.tx = ts[i].px; v.tz = ts[i].pz; v.variant = ts[i].variant;
    } else v.alive = false;
  }
  for (size_t i = ts.size(); i < tornadoes_.size(); i++) if (tornadoes_[i].alive && tornadoes_[i].fade < 0) tornadoes_[i].fade = 0;
  // Toxic clouds: one puff sprite per puff; puffs that disappear play the closing frames.
  std::vector<std::pair<double, double>> cur;
  rain_.clear();
  for (const CloudInst& c : m.clouds()) {
    for (const auto& p : c.puffs) cur.push_back(p);
    if (c.raining) { RainView r; r.x = c.cx; r.z = c.cz; rain_.push_back(r); }
  }
  size_t live = 0;
  for (const PuffView& p : puffs_) if (p.closing < 0) live++;
  size_t seen = 0;
  for (PuffView& p : puffs_) {
    if (p.closing >= 0) continue;
    if (seen < cur.size()) { p.x = cur[seen].first; p.z = cur[seen].second; }
    else p.closing = 0;
    seen++;
  }
  for (size_t i = live; i < cur.size(); i++) {
    PuffView p; p.x = cur[i].first; p.z = cur[i].second;
    p.height = int((2.0 + double(rnd() % 3)) * kUnitsPerTile);   // 2..4 tiles (MinHeight..MaxHeight defaults)
    puffs_.push_back(p);
  }
}

void DisasterFx::stepFlyer(Fx& f, int dtMs, bool& done) {
  const double dt = dtMs / 1000.0;
  if (f.dwell > 0) {
    f.dwell -= dtMs;
    if (f.dwell <= 0) {   // the action of the waypoint just finished
      f.acting = false;
      const Wp& wp = f.route[f.leg];
      if (wp.type == 3) { FxImpact i; i.kind = FxImpact::CropCircle; i.x = wp.x; i.z = wp.z; impacts_.push_back(i); }
      if (wp.type == 4) { FxImpact i; i.kind = FxImpact::LocustBite; i.x = wp.x; i.z = wp.z; impacts_.push_back(i); }
      f.leg++;
    }
    return;
  }
  if (f.leg >= f.route.size()) { done = true; return; }
  const Wp& wp = f.route[f.leg];
  const double tx = wp.x + 0.5, tz = wp.z + 0.5;
  const double dx = tx - f.x, dz = tz - f.z, d = std::sqrt(dx * dx + dz * dz);
  const double step = f.speed * dt;
  if (d > step && d > 1e-6) { f.x += dx / d * step; f.z += dz / d * step; return; }
  f.x = tx; f.z = tz;
  switch (wp.type) {
    case 1: {   // drop a plasma bomb
      Fx b; b.kind = Kind::Plasma; b.x = f.x; b.z = f.z; b.h = f.h; b.life = 1 << 30;
      b.targetX = wp.x; b.targetZ = wp.z; b.vh = -std::max(8.0, f.h) / 0.7;
      spawn(b);
      f.dwell = 300; f.acting = true; break;
    }
    case 3: f.dwell = 900; f.acting = true; break;       // hover over the crop field
    case 4: f.dwell = 450; f.acting = true; break;       // eat
    case 2: done = true; break;
    default: f.leg++; break;
  }
}

void DisasterFx::update(uint32_t dtMs) {
  const int dt = int(std::min<uint32_t>(dtMs, 250));   // a long frame must not teleport the flyers
  const size_t n0 = fx_.size();
  updating_ = true;
  for (size_t i = 0; i < n0; i++) {
    Fx& f = fx_[i];
    if (f.delay > 0) { f.delay -= dt; if (f.delay > 0) continue; f.delay = 0; }
    f.age += dt;
    bool done = false;
    switch (f.kind) {
      case Kind::Explosion: case Kind::PlasmaBlast: case Kind::JunkBlast: case Kind::Splash: case Kind::Crater:
        if (f.age >= f.life) done = true;
        break;
      case Kind::Saucer: case Kind::Locust: stepFlyer(f, dt, done); break;
      case Kind::Plasma: case Kind::Junk:
        f.h += f.vh * dt / 1000.0;
        if (f.h <= 0) {
          FxImpact im; im.kind = f.kind == Kind::Plasma ? FxImpact::UfoBomb : FxImpact::Debris; im.x = f.targetX; im.z = f.targetZ;
          impacts_.push_back(im);
          Kind bk = f.kind == Kind::Plasma ? Kind::PlasmaBlast : Kind::JunkBlast;
          spawnExplosion(f.targetX + 0.5, f.targetZ + 0.5, bk);
          done = true;
        }
        break;
    }
    if (done) f.life = -1;
  }
  updating_ = false;
  fx_.erase(std::remove_if(fx_.begin(), fx_.end(), [](const Fx& f) { return f.life < 0; }), fx_.end());
  fx_.insert(fx_.end(), incoming_.begin(), incoming_.end());
  incoming_.clear();
  // state views
  for (TornadoView& v : tornadoes_) {
    if (v.alive) {
      v.age += dt;
      const double k = std::min(1.0, dt / 250.0);
      v.x += (v.tx - v.x) * k; v.z += (v.tz - v.z) * k;
    }
    if (v.fade >= 0) { v.fade += dt; if (v.fade >= 600) { v.alive = false; v.fade = -1; } }
  }
  while (!tornadoes_.empty() && !tornadoes_.back().alive) tornadoes_.pop_back();
  for (PuffView& p : puffs_) { p.age += dt; if (p.closing >= 0) p.closing += dt; }
  puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(), [](const PuffView& p) { return p.closing >= 4 * 150; }), puffs_.end());
  rainAge_ += dt;
  for (VortexView& v : vortices_) v.age += dt;
  vortices_.erase(std::remove_if(vortices_.begin(), vortices_.end(), [](const VortexView& v) { return v.age >= v.life; }), vortices_.end());
}

bool DisasterFx::push(std::vector<SceneSprite>& out, uint32_t colourId, int alphaDelta, int frames, int frame, double x, double z,
                      double heightUnits, int level) const {
  if (!data_) return false;
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
  s.globalLevel = level;
  s.x = int32_t(std::lround(x * 256.0)); s.z = int32_t(std::lround(z * 256.0));
  s.height = groundAt(x, z) + int32_t(std::lround(heightUnits * 256.0));
  s.moving = true;
  out.push_back(s);
  return true;
}

void DisasterFx::emitFx(const Fx& f, std::vector<SceneSprite>& out) const {
  if (f.delay > 0) return;
  const int z = zoom_, r = rot_ & 3, frame = f.age / kFrameMs;
  switch (f.kind) {
    case Kind::Explosion: push(out, kExplFire + uint32_t(f.variant * 5 + z), 0x200, kExplFireFrames[f.variant], frame, f.x, f.z, 0); break;
    case Kind::PlasmaBlast: push(out, kExplPlasma + uint32_t(f.variant * 5 + z), 0x200, 9, frame, f.x, f.z, 0); break;
    case Kind::JunkBlast: push(out, kJunkBlast + uint32_t(z * 4 + r), 0x14, 13, frame, f.x, f.z, 0); break;
    case Kind::Splash: push(out, kSplash + uint32_t(z), 0x100, 8, frame, f.x, f.z, 0); break;
    case Kind::Crater: push(out, kCraters[f.variant] + uint32_t(z), 5, 1, 0, f.x, f.z, 0); break;
    case Kind::Plasma: push(out, kPlasmaBall + uint32_t(z), 0x80, 8, frame, f.x, f.z, f.h); break;
    case Kind::Junk: push(out, kJunkBodies[f.variant] + uint32_t(z * 4 + r), 0x14, 6, frame, f.x, f.z, f.h); break;
    case Kind::Saucer: push(out, kUfo + uint32_t(z), 0, 5, frame, f.x, f.z, f.h + 1.0 * std::sin(f.age / 350.0)); break;
    case Kind::Locust: {
      const uint32_t base = f.acting ? kLocustEat : kLocustFly;
      for (int k = 0; k < 6; k++) {   // a swarm is a handful of locust sprites scattered around its centre
        const double ox = (double((f.variant * 37 + k * 53) % 100) - 50.0) / 50.0 * 0.9, oz = (double((f.variant * 61 + k * 29) % 100) - 50.0) / 50.0 * 0.9;
        push(out, base + uint32_t(z), 5, 6, frame + k, f.x + ox, f.z + oz, f.h + double(k % 3) * 2.0);
      }
      break;
    }
  }
}

void DisasterFx::emit(int zoom, int rot, std::vector<SceneSprite>& out) const {
  zoom_ = clampi(zoom, 0, 4); rot_ = rot & 3;
  const int z = zoom_;
  for (const Fx& f : fx_) emitFx(f, out);
  for (const TornadoView& v : tornadoes_) {
    if (!v.alive) continue;
    const int frame = v.age / kFrameMs;
    const bool ramp = v.age < 800 || v.fade >= 0;   // start / end set while the funnel forms or fades [INFERRED]
    const uint32_t body = ramp ? kTornadoEnds : (v.variant == 1 ? kTornadoT2 : kTornadoT1);
    const bool wet = water_ && water_(clampi(int(v.x), 0, std::max(0, W_ - 1)), clampi(int(v.z), 0, std::max(0, H_ - 1)));
    push(out, (wet ? kWaterSpray : kDirtSpray) + uint32_t(z), 0x100, 11, frame, v.x, v.z, 0);
    push(out, body + uint32_t(z), 0x100, 8, frame, v.x, v.z, 0);
  }
  for (const PuffView& p : puffs_) {
    if (p.closing >= 0) push(out, kToxClose + uint32_t(z), 0x1005, 4, p.closing / 150, p.x, p.z, p.height);
    else if (p.age < 4 * 150) push(out, kToxOpen + uint32_t(z), 0x1005, 4, p.age / 150, p.x, p.z, p.height);
    else push(out, kToxCycle + uint32_t(z), 0x1005, 4, (p.age - 600) / 250, p.x, p.z, p.height);
  }
  for (const RainView& r : rain_) push(out, kToxRain + uint32_t(z), 0x1005, 11, rainAge_ / kFrameMs, r.x, r.z, 0);
  for (const VortexView& v : vortices_) {
    push(out, kVortexOuter + uint32_t(z), 5, 9, v.age / 100, v.x, v.z, 0);
    push(out, kVortexInner + uint32_t(z), 5, 9, v.age / 100, v.x, v.z, 0);
  }
}

}  // namespace m3k
