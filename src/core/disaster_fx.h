// Disaster visuals: animated effect sprites and the flying "automata" of the disasters (spec 21 section 12, spec 26 5.9, spec 04 4.4).
// Platform independent apart from the sprite archives: the owner feeds DisasterCommands (one-shot effects, flying objects) and a
// DisasterManager snapshot (state-driven effects: tornado funnels, toxic puffs, flames, debris launcher), advances it in real time, and
// collects SceneSprites to draw. Gameplay depends on this layer only through FxImpact: the reports the effects send back to their
// disaster (spec 21 section 12.12), which the owner turns into the matching DisasterManager calls.
//
// Sprite sets, frame periods, speeds and sequences follow spec 21 section 12 [STATIC/DATA]; what it leaves open is marked [INFERRED].
// Units: tiles for x / z, altitude units for heights (spec world units / 256), milliseconds for time.
#pragma once
#include <cstdint>
#include <functional>
#include <tuple>
#include <utility>
#include <vector>
#include "core/disasters.h"
#include "core/gamedata.h"
#include "core/render.h"

namespace m3k {

struct FxImpact {
  enum Kind {
    UfoBomb,       // plasma ball hit the ground at (x, z)
    UfoBeam,       // beam target reached (destroy)
    UfoCrop,       // crop circle target reached
    UfoAbduction,
    LocustEat,     // a swarm ate at (x, z)
    LocustFlora,   // a swarm passed over (x, z): flora there takes damage 0x20
    Debris,        // a space-junk piece hit (x, z)
    Rioter,        // the riot crowd threw something that landed on (x, z)
    RioterWalk,    // the riot crowd entered road tile (x, z)
    RioterLost,    // the crowd could not continue (no road ahead): destroyed
    RioterLeft,    // the crowd left road tile (x, z) (also sent when it is destroyed or the riot ends)
    TornadoTile,   // funnel `id` is over a new tile; fx, fz = exact position
    TornadoLeft,   // funnel `id` left the map
    VortexAt,      // whirlpool vortex moved to tile (x, z)
    VortexSucked,  // the vortex pulled the boat it had captured into its centre at (x, z)
    PuffGone,      // a toxic puff finished fading out
    VortexScan,    // bridge scan at (x, z) (the vortex centre), every 4.1 s from 2 s
  } kind = UfoBomb;
  int x = 0, z = 0;
  uint32_t id = 0;
  float fx = 0, fz = 0;
};

class DisasterFx {
 public:
  // `ground(x, z)` = ground height in 1/256 altitude units at tile (x, z) (land or water surface).
  using GroundFn = std::function<int32_t(int, int)>;
  void setData(GameData* d) { data_ = d; }
  void setGround(GroundFn g) { ground_ = std::move(g); }
  void setWater(std::function<bool(int, int)> w) { water_ = std::move(w); }
  void setRoad(std::function<bool(int, int)> r) { road_ = std::move(r); }   // tiles a rioter crowd can walk on
  void setMapSize(int w, int h) { W_ = w; H_ = h; }
  // A floating wreck (spec 21 s12.6): kind 0 car, 1 bus, 2 truck, 3 truck + dude, 4 floater dude, 5 debris field.
  void spawnWreck(double x, double z, int kind);
  // The street-sim boats the whirlpool can capture (spec 21 s12.6): list those in a square (id, x, y in tiles), take one over, place it, remove it.
  struct BoatPort {
    std::function<std::vector<std::tuple<int, double, double>>(double, double, double)> inSquare;
    std::function<void(int)> capture, remove;
    std::function<void(int, double, double)> move;
  };
  void setBoatPort(BoatPort p) { boats_ = std::move(p); }
  void seed(uint32_t s) { rng_ = s ? s : 1u; }
  void clear();

  // One-shot and flying effects from the simulation output.
  void onCommand(const DisasterCommand& c);
  // State-driven effects (tornado funnels, toxic clouds, flames, debris launcher, riot crowds) follow the manager's instances.
  void syncState(const DisasterManager& m);
  // Advances every effect by `dtMs` of real time (effects do not freeze with the game paused, spec 26 s7).
  void update(uint32_t dtMs);
  // Appends the sprites of every live effect for the view zoom (0..4) and rotation (0..3).
  void emit(int zoom, int rot, std::vector<SceneSprite>& out) const;
  // Reports the effects produced during update(); the caller applies them through the manager.
  std::vector<FxImpact> takeImpacts() { std::vector<FxImpact> r; r.swap(impacts_); return r; }
  // True while a flying object still has a report to deliver (the disaster instance stays alive until then).
  bool pendingImpacts() const;
  size_t count() const { return fx_.size(); }
  // Sounds (spec 06 s10.3, 21 s12): one-shot item IDs produced during update(), and which loops should be running now.
  std::vector<int> takeSounds() { std::vector<int> r; r.swap(sounds_); return r; }
  struct Loops { bool ufo = false, locust = false, locustEat = false, toxic = false, rain = false, vortex = false, junk = false; };
  Loops loops() const;
  // Giant locusts that cross the screen once per attack (spec 21 s12.9): screen-space overlay, drawn by the owner (image 3FB1 + variant, alpha +5).
  void setScreen(int w, int h) { screenW_ = w; screenH_ = h; }
  struct GiantPose { double sx = 0, sy = 0; int variant = 0; };
  std::vector<GiantPose> giantPoses() const { std::vector<GiantPose> v; for (const Giant& g : giants_) if (g.delay <= 0) v.push_back({g.sx, g.sy, g.variant}); return v; }
  bool giantSoundDue() { const bool b = giantSound_; giantSound_ = false; return b; }   // sound 0xFE when the fly-by starts
  // Riot crowds, for the owner to draw with the vehicle sprite of VATTRIB 0x54 (spec 21 s12.10): tile position, heading in 32 steps, age in ms.
  // Street-sim-style vehicles the effects own, for the owner to draw with the sprite of VATTRIB `vattrib`: the riot crowd (0x54) and crop-duster planes (110).
  struct VehiclePose { double x = 0, z = 0, heightUnits = 0; int heading32 = 0, ageMs = 0; uint32_t vattrib = 0; };
  std::vector<VehiclePose> vehiclePoses() const;
  // Removes the crop-duster planes and clears the spray marks (the locust attack ended).
  void endCropDusters() { planes_.clear(); sprayed_.clear(); dusters_.clear(); }
  size_t cropDusterCount() const { return planes_.size(); }
  // Crop-duster dispatch to tile (x, z) (spec 21 s6.4 / 12.9): at most 3 active; the plane sprays up to 5 times along its heading. A swarm that arrives at a
  // crop within 6 tiles (Chebyshev) of a spray point fades out over 9 s. The plane itself is not drawn (its speed and altitude are [OPEN]).
  void dispatchCropDuster(int x, int z);
  // Position (tile) of some live effect, for tools that centre the view on the action; false when nothing is alive.
  bool focus(double& x, double& z) const;

 private:
  enum class Kind { Explosion, Splash, Crater, Saucer, Plasma, Swarm, Piece, Spray, Chunk, Wreck };
  // Queued task of a saucer or swarm: type 0 fly to, 1 beam (hover, report), 2 crop circle, 3 abduction, 4 eat (swarm), 5 exit.
  struct Task { int type = 0; double x = 0, z = 0; int aux = 0; };
  struct Fx {
    Kind kind = Kind::Explosion;
    double x = 0, z = 0, y = 0;       // tile position; height above the ground in altitude units
    int age = 0;                      // ms since it appeared
    int delay = 0;                    // ms until it appears
    int life = 1000;                  // one-shot length (ms); < 0 = remove
    int variant = 0, size = 0, set = 0;
    bool back = false;                // animation plays backwards
    int fadeMs = -1, fadeLeft = 0;    // fade-out
    // flying objects
    std::vector<Task> q; size_t cur = 0;
    int hover = 0, bombMs = 0, idleHops = 0;
    bool hovering = false, beam = false, eating = false, reported = false;
    double tx = 0, tz = 0, ty = 0;    // plasma / piece destination
    double vx = 0, vz = 0, vy = 0;    // plasma / piece velocity per second
    int ox = -1, oz = -1;             // swarm: tile at the last update
    bool passive = false;
    bool debrisDone = false;           // explosion: its debris pieces were thrown
    uint32_t uid = 0;                  // wrecks: identity for the vortex
    bool held = false;                 // wreck captured by a vortex (does not expire)
    int period = 100;                  // debris / wreck: frame period (ms)
    bool stopped = false; int stopAge = 0;   // debris at rest
    uint32_t orbitUid = 0; double orbitR = 0, orbitA = 0;   // tornado piece orbiting funnel orbitUid
  };
  struct TornadoView {
    uint32_t uid = 0; double x = 0, z = 0, dx = 0, dz = 1, speed = 1; int variant = 0;
    int age = 0, stage = 0;           // stage 0 opening, 1 loop, 2 closing
    int stageMs = 0; bool loopB = false, gone = false; int tx = -1000, tz = -1000;
  };
  // Rain column of one puff (spec 21 s12.7): grown length E, retracted top S and texture scroll in world units; steam stage 0 none, 1 open, 2 cycle, 3 close.
  struct RainState { bool active = false, stopped = false; double lengthWu = 0, E = 0, S = 0, scroll = 0; int steam = 0, steamMs = 0; };
  struct PuffView { double x = 0, z = 0, h = 0; int age = 0; int closing = -1; RainState rain; };
  struct FlameView { double x = 0, z = 0; int size = 1; int period = 450; int phase = 0; };
  struct CrowdView { double x = 0, z = 0; int walkMs = 0, throwMs = 0, nextThrow = 450, heading32 = 0, ageMs = 0; };
  struct Giant { double sx = 0, sy = 0, vx = 0, vy = 0; int variant = 0, delay = 0; };
  struct Captured { int id = 0; uint32_t wreckUid = 0; double ang = 0, dWu = 0; int orbitMs = 0; };
  struct VortexView { double x = 0, z = 0, vx = 0.12 * 0.5, vz = 0.12 * 0.125; int age = 0, life = 0; bool closing = false; int closeAge = 0; int tx = -1, tz = -1; int captureMs = 0, scanMs = 0; std::vector<Captured> held; };
  struct Launcher { bool on = false; int nextMs = 0; int scale = 2929; };

  uint32_t rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }
  double rndf() { return double(rnd() & 0xFFFFFFu) / double(0x1000000); }
  int rndi(int n) { return n > 0 ? int(rnd() % uint32_t(n)) : 0; }
  int rndRange(int a, int b) { return b > a ? a + rndi(b - a) : a; }
  int32_t groundAt(double x, double z) const;
  double groundUnits(double x, double z) const { return double(groundAt(x, z)) / 256.0; }
  bool isWaterAt(double x, double z) const;
  void spawn(const Fx& f) { if (fx_.size() + incoming_.size() < 800) (updating_ ? incoming_ : fx_).push_back(f); }
  void spawnExplosion(double x, double z, int set, int size);
  void spawnSplash(double x, double z, int set);
  void spawnSaucers(const DisasterCommand& c);
  void spawnPiece();
  void updatePlanes(int dt);
  void spawnChunk(double x, double z, double heightUnits, int kind, double vx, double vz, double vy);
  void throwExplosionDebris(const Fx& e);
  void edgePoint(double& x, double& z, int edge);
  void updateSaucer(Fx& f, int dt, bool& done);
  void updateSwarm(Fx& f, int dt, bool& done);
  void report(FxImpact::Kind k, int x, int z) { FxImpact i; i.kind = k; i.x = x; i.z = z; impacts_.push_back(i); }
  void emitFx(const Fx& f, std::vector<SceneSprite>& out) const;
  // `colourId` is the full image ID (set base + zoom index); `mag` draws the image doubled (shifted zoom at zoom 4).
  bool push(std::vector<SceneSprite>& out, uint32_t colourId, int alphaDelta, int frames, int frame, double x, double z, double heightUnits,
            int level = 32, bool mag = false) const;
  // Opacity-plane-only sprite (UFO beam): a flat tint is drawn through the plane.
  void emitRain(const PuffView& p, int slot, std::vector<SceneSprite>& out) const;
  bool pushTint(std::vector<SceneSprite>& out, uint32_t top, uint32_t planeId, int frames, int frame, double x, double z, double heightUnits, int level = 20) const;

  GameData* data_ = nullptr;
  GroundFn ground_;
  std::function<bool(int, int)> water_, road_;
  int W_ = 0, H_ = 0;
  uint32_t rng_ = 0x9E3779B9u;
  std::vector<Fx> fx_, incoming_;
  bool updating_ = false;
  std::vector<TornadoView> tornadoes_;
  std::vector<PuffView> puffs_;
  std::vector<FlameView> flames_;
  std::vector<CrowdView> crowds_;
  std::vector<VortexView> vortices_;
  Launcher launcher_;
  std::vector<Giant> giants_;
  int screenW_ = 1280, screenH_ = 800;
  bool giantSound_ = false;
  BoatPort boats_;
  std::vector<std::pair<int, int>> sprayed_;   // spray points of the current locust attack
  struct Duster { int x = 0, z = 0; };
  struct DusterPlane { double x = 0, z = 0, hx = 0, hz = 0, dirX = 1, dirZ = 0; std::vector<std::pair<int, int>> targets; size_t cur = 0; int sprays = 0; bool spraying = false; int ageMs = 0; double nextX = 0, nextZ = 0; };
  std::vector<DusterPlane> planes_;
  std::vector<Duster> dusters_;
  bool raining_ = false;
  mutable int transientGen_ = 0;
  std::vector<FxImpact> impacts_;
  std::vector<int> sounds_;
  int locustEdge_ = 0;
  uint32_t nextWreckUid_ = 1;
  int64_t flameClockMs_ = 0;
  mutable int zoom_ = 2, rot_ = 0;
};

}  // namespace m3k
