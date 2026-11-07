// Disaster visuals: animated effect sprites that accompany the disaster simulation (spec 21 section 12, spec 26 5.9, spec 04 4.4).
// Platform independent apart from the sprite archives: the owner feeds DisasterCommands (one-shot effects, flying objects) and a
// DisasterManager snapshot (state-driven effects: tornado funnels, toxic cloud puffs, whirlpool), advances it in real time, and
// collects SceneSprites to draw. Gameplay depends on this layer only through FxImpact: the moment a flying object reaches its target,
// which the owner turns into the matching DisasterManager "automaton reports back" call (spec 21 section 12).
//
// Everything about timing, paths and set choice that the spec leaves open is an [INFERRED] default (see disaster_fx.cpp). The sprite
// sets and their ID layouts come from the shipped data (EffectSprites.sii, disaster_*.sii, probe_fx_sprites).
#pragma once
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>
#include "core/disasters.h"
#include "core/gamedata.h"
#include "core/render.h"

namespace m3k {

struct FxImpact {
  enum Kind { UfoBomb, LocustBite, Debris, CropCircle } kind = UfoBomb;
  int x = 0, z = 0;
};

class DisasterFx {
 public:
  // `ground(x, z)` = ground height in 1/256 altitude units at tile (x, z) (land or water surface).
  using GroundFn = std::function<int32_t(int, int)>;
  void setData(GameData* d) { data_ = d; }
  void setGround(GroundFn g) { ground_ = std::move(g); }
  void setWater(std::function<bool(int, int)> w) { water_ = std::move(w); }
  void setMapSize(int w, int h) { W_ = w; H_ = h; }
  void seed(uint32_t s) { rng_ = s ? s : 1u; }
  void clear();
  bool empty() const { return fx_.empty() && tornadoes_.empty() && puffs_.empty() && vortices_.empty() && rain_.empty(); }

  // One-shot and flying effects from the simulation output.
  void onCommand(const DisasterCommand& c);
  // State-driven effects (tornado funnels, toxic clouds, rain) follow the manager's instances.
  void syncState(const DisasterManager& m);
  // Advances every effect by `dtMs` of real time (effects do not freeze with the game paused, spec 26 s7).
  void update(uint32_t dtMs);
  // Appends the sprites of every live effect for the view zoom (0..4) and rotation (0..3).
  void emit(int zoom, int rot, std::vector<SceneSprite>& out) const;
  // Impacts that happened during update(); the caller applies their gameplay effect.
  std::vector<FxImpact> takeImpacts() { std::vector<FxImpact> r; r.swap(impacts_); return r; }
  // True while a flying object still has an impact to deliver (the disaster instance stays alive until then).
  bool pendingImpacts() const;
  size_t count() const { return fx_.size(); }
  // Position (tile) of some live effect, for tools that centre the view on the action; false when nothing is alive.
  bool focus(double& x, double& z) const;

 private:
  enum class Kind { Explosion, Splash, PlasmaBlast, JunkBlast, Locust, Saucer, Plasma, Junk, Crater };
  // Waypoint types: 0 pass, 1 bomb, 2 exit (despawn on arrival), 3 crop circle, 4 eat.
  struct Wp { int x = 0, z = 0; int type = 0; };
  struct Fx {
    Kind kind = Kind::Explosion;
    double x = 0, z = 0, h = 0;      // tile position and height above the ground (altitude units)
    double vh = 0;                    // vertical speed, altitude units per second
    int age = 0, life = 1000;         // ms
    int variant = 0;
    int delay = 0;                    // ms until the object appears
    int dwell = 0;                    // ms left of the current action
    bool acting = false;
    bool explodeOnExit = false;
    int targetX = 0, targetZ = 0;     // plasma / debris: where it lands
    double speed = 4;                 // tiles per second
    std::vector<Wp> route; size_t leg = 0;
  };
  struct TornadoView { double x = 0, z = 0, tx = 0, tz = 0; int variant = 0; int age = 0; bool alive = false; int fade = -1; };
  struct PuffView { double x = 0, z = 0; int age = 0; int closing = -1; int height = 0; };
  struct RainView { double x = 0, z = 0; };
  int rainAge_ = 0;
  struct VortexView { double x = 0, z = 0; int age = 0, life = 0; };

  uint32_t rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }
  double rndf() { return double(rnd() & 0xFFFFFFu) / double(0x1000000); }
  int rndi(int n) { return n > 0 ? int(rnd() % uint32_t(n)) : 0; }
  int32_t groundAt(double x, double z) const;
  // Spawns during update() go to a side list so the references the update loop holds stay valid.
  void spawn(const Fx& f) { if (fx_.size() + incoming_.size() < 800) (updating_ ? incoming_ : fx_).push_back(f); }
  void spawnExplosion(double x, double z, Kind k = Kind::Explosion);
  void edgePoint(double& x, double& z);
  void stepFlyer(Fx& f, int dtMs, bool& done);
  void emitFx(const Fx& f, std::vector<SceneSprite>& out) const;
  bool push(std::vector<SceneSprite>& out, uint32_t colourId, int alphaDelta, int frames, int frame, double x, double z,
            double heightUnits, int level = 32) const;

  GameData* data_ = nullptr;
  GroundFn ground_;
  std::function<bool(int, int)> water_;
  int W_ = 0, H_ = 0;
  uint32_t rng_ = 0x9E3779B9u;
  std::vector<Fx> fx_, incoming_;
  bool updating_ = false;
  std::vector<TornadoView> tornadoes_;
  std::vector<PuffView> puffs_;
  std::vector<RainView> rain_;
  std::vector<VortexView> vortices_;
  std::vector<FxImpact> impacts_;
  mutable int zoom_ = 2, rot_ = 0;
};

}  // namespace m3k
