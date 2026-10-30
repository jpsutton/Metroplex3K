// Disaster subsystem (spec/21-disasters.md). Platform independent state machines. The caller supplies
// a DisasterWorld (map queries) and DisasterInputs (city statistics snapshot); everything the
// disasters want done to the city comes back as DisasterCommand / DisasterEvent lists.
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "core/bytes.h"
#include "core/gamerng.h"
#include "core/ini.h"

namespace m3k {

// ---- IDs (spec 21 section 2) ----------------------------------------------------------------
enum class DisasterType : uint32_t {
  Fire = 0x621CDA33, Tornado = 0x02963821, Earthquake = 0x42963812, Riot = 0x4296380E,
  UFO = 0xE2963828, ToxicCloud = 0x22963800, Whirlpool = 0x13873254, Locusts = 0x13873255,
  SpaceJunk = 0x13873256, Parade = 0xC4C90997,
};
const std::vector<DisasterType>& allDisasterTypes();
uint32_t disasterClassId(DisasterType t);            // class ID from [DisasterDescriptions]
const char* disasterName(DisasterType t);

// Damage types (section 9)
enum : int { kDmgAbandon = 0x02, kDmgRubble = 0x08, kDmgBurnt = 0x10, kDmgCrop = 0x20 };
// Occupant flag bits used by the rules
enum : uint32_t { kOccNonFlammable = 0x400, kOccIndestructible = 0x800, kOccCrowdSite = 0x8000,
                  kOccOverWater = 0x400000 };

// ---- Tunables (SimTune.INI [Tunables] + SC3DisasterLayer.ini) -----------------------------------
// Shipped values are the defaults. Values marked [OPEN default] are not given in the spec.
struct DisasterTunables {
  // [MiscDisasterTunables] [OPEN default]
  int maxEmergencyDuration = 12;        // months
  int minDisasterCostToGetRelief = 0;
  int reliefPctWithoutEW = 0;
  int reliefPctWithEW = 50;
  // [FireDisaster] etc: <max>,<sound>
  int maxFires = 20, fireSound = 62;
  int maxTornadoes = 5, tornadoSound = 60;
  int maxRiots = 3, riotSound = 63;
  int paradeMax = 1;
  // Fire
  int fireIntervalMin = 48, fireIntervalMax = 240, stageMin = 10, stageMax = 40;
  int minFlam = 1, spreadFlam = 100;
  int spreadLo[3] = {0, 5, 15}, spreadHi[3] = {20, 50, 95};
  int groupBias = 15;
  // Tornado
  int tornadoIntervalMin = 48, tornadoIntervalMax = 360;
  int tornadoSpeedMin = 60, tornadoSpeedMax = 120;     // stored as signed 8-bit
  int tornadoDestroyChance[3] = {50, 70, 95};
  int tornadoLifeMin = 30, tornadoLifeMax = 60;
  int tornadoWarn[5] = {0, 60, 60, 60, 60};
  double tornadoTilesPerSpeedUnitPerTick = 1.0 / 60.0;  // [OPEN] ground track per minute tick
  // Toxic cloud [OPEN defaults: values not shipped in spec]
  int toxMaxInstances = 1;
  int toxMinClouds = 3, toxMaxClouds = 6;
  int toxMinSpeed = 5, toxMaxSpeed = 15;
  int toxPrem1 = 0, toxPrem2 = 60, toxPrem3 = 120, toxVisible = 180;
  int toxMinHeight = 2, toxMaxHeight = 4;               // tiles
  int toxMinDuration = 120, toxMaxDuration = 300;
  int toxAvgMonthsBetween = 24;
  int toxWeightPollution = 100, toxWeightCleanInd = 20, toxWeightIndFee = 20, toxWeightWaste = 20;
  int toxWeightPlant = 30, toxThreshold = 60, toxScorePerLevel = 10;
  int toxMinRainStart = 10, toxMaxRainStart = 40, toxMinRain = 30, toxMaxRain = 90, toxMinGap = 20;
  int toxAbandonRadiusInc = 2, toxDestroyRadiusInc = 1;
  int toxDestroyLikelihood = 50, toxAbandonLikelihood = 200;   // per 1000 per tick
  // UFO [OPEN defaults except shipped timings/bombs]
  int ufoMaxInstances = 1, ufoMinMonths = 60, ufoMaxMonths = 240, ufoChance100 = 33;
  int ufoPrem1 = 0, ufoPrem2 = 60, ufoPrem3 = 120, ufoAttack = 180, ufoAttackEnd = 185;
  int ufoMaxPerInstance = 12, ufoMaxPerInstanceCheat = 40, ufoExplodeOnExit100 = 1000;
  int ufoMaxAbductions = 5, ufoMaxCropCircles = 5;
  int ufoMaxLandmark = 2, ufoMaxSpaceport = 1, ufoMaxLowTech = 10, ufoMaxHighTech = 3, ufoMaxPower = 2,
      ufoMaxTotal = 15;
  int ufoYearVisit = 0, ufoYearAbduct = 1950, ufoYearCrop = 1960, ufoYearAttack = 1970;
  int ufoPctDestroyed = 100, ufoPctFire = 100;
  // Locusts [OPEN defaults]
  int locMaxInstances = 1, locMinMonths = 24, locMaxMonths = 240, locChance100 = 33;
  int locPrem1 = 0, locPrem2 = 60, locAttack = 120;
  int locMinSwarms = 1, locMaxSwarms = 6, locSwarmToCrop = 8, locMaxCrops = 128, locMaxCheat = 20;
  int locYearVisit = 0;
  int locDuration = 120;                 // [OPEN] ticks the instance lives after the swarm is launched
  // Earthquake
  int quakeMax = 1, quakeMinGapMonths = 96, quakeMinMag = 25, quakeMaxMag = 85, quakeChance = 40;
  int quakeDestroy[3] = {5000, 6000, 7000}, quakeIgnite[3] = {3000, 3000, 3000};
  int quakeHalfWidth[3] = {2, 4, 6};
  int quakeShake[3] = {1000, 3000, 5000}, quakeShakeParam[3] = {1, 2, 4};
  int quakeFore[3] = {100, 500, 1000}, quakeForeParam[3] = {0, 1, 1};
  int quakeT[4] = {0, 60, 120, 180};
  int quakeScarpP[3] = {1, 1, 2}, quakeScarpQ[3] = {0, 0, 0};
  int terrainHeightStep = 1;                              // [OPEN] "h" of the scarp rule
  // Riot
  int riotUnrestThreshold = 8, riotIntervalMin = 48, riotIntervalMax = 300, riotSiteCoverageMax = 50;
  int riotSearchRadius = 40, riotDurMax = 240, riotDurMin = 20, riotCrowdA = 300000, riotCrowdB = 600000;
  int riotWarnDelay = 0, riotBrewTime = 80, riotGrowth = 5;
  // Whirlpool
  int whirlMax = 1, whirlMinGapMonths = 24, whirlMagMin = 25, whirlMagMax = 85, whirlChance = 83,
      whirlRadius = 7;
  int whirlT[4] = {0, 20, 40, 60};
  int whirlDuration = 30000;
  // Space junk
  int junkMax = 1, junkMinGapMonths = 120, junkChance = 20, junkT[5] = {0, 60, 120, 180, 300};
  int junkDuration = 3000000, junkB = 30, junkA = 50, junkCraterDecal = 300000, junkMaxHeightDiff = 10,
      junkFirstYear = 1960;
  // Services (SC3FireLayer.ini / SC3Police.ini)
  int fireDispatchRadius = 4, fireDispatchEffect = 225, policeDispatchRadius = 4, policeDispatchEffect = 200;
  int fireStrikeMinFunding = 50, fireStrikeMaxFlam = 100, fireStrikeEfficiency = 50, fireStrikeMaxDuration = 100;
  int policeStrikeMinFunding = 50, policeStrikeMaxCrime = 100, policeStrikeEfficiency = 50,
      policeStrikeMaxDuration = 100;

  // Reads every tunable present. simTune: [Tunables] hex IDs; layer: SC3DisasterLayer.ini.
  void load(const IniFile& simTune, const IniFile& layerIni);
  // Same, from an already-parsed id->value map (useful for tests / non-INI sources).
  void applyTunableMap(const std::map<uint32_t, int>& m);
};

// ---- World interface ------------------------------------------------------------------------
struct OccInfo {
  bool valid = false;
  uint32_t id = 0;                 // unique occupant instance id
  int ax = 0, az = 0;              // anchor tile
  int w = 1, h = 1;                // footprint in tiles
  uint32_t agentType = 0;
  uint32_t flags = 0;              // occupant flag bits
  int64_t value = 0;               // cost / price attribute
  bool isLot = false, isFlora = false, crop = false, burning = false;
  int lotDamage = 0;               // 0 normal, else damaged state kDmg*
};

class DisasterWorld {
 public:
  virtual ~DisasterWorld() = default;
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual int flammability(int, int) const { return 0; }
  virtual int fireCoverage(int, int) const { return 0; }
  virtual int policeCoverage(int, int) const { return 0; }
  virtual OccInfo occupantAt(int, int) const { return OccInfo(); }          // occupant covering tile
  virtual OccInfo occupantAnchoredAt(int x, int z) const {
    OccInfo o = occupantAt(x, z);
    return (o.valid && o.ax == x && o.az == z) ? o : OccInfo();
  }
  virtual std::vector<OccInfo> occupantsTouching(const OccInfo&) const { return {}; }
  virtual bool inGroup(const OccInfo&, int /*group 100 or 300*/) const { return false; }
  virtual bool isA(uint32_t agentType, uint32_t ancestor) const { return agentType == ancestor; }
  virtual bool isWater(int, int) const { return false; }
  virtual int terrainHeight(int, int) const { return 0; }
  virtual int zoneType(int, int) const { return 0; }
  bool inMap(int x, int z) const { return x >= 0 && z >= 0 && x < width() && z < height(); }
};

struct DisasterInputs {
  int32_t jdn = 2415021;            // today, Julian day number
  int year = 1900;
  int unrest = 0;                   // U 0..100 [OPEN] default source: unemployment %
  int aura = 0;                     // A -128..127 [OPEN] default source: city aura
  int64_t pollutionTotal = 0, pollutionNorm = 1;     // pollution layer total / normaliser
  bool ordCleanIndustry = false, ordIndPollutantFee = false, ordWasteDisposal = false;
  int toxicWastePlants = 0;
  bool disastersEnabled = true;
  bool cheatUfoSwarm = false, cheatLocustSwarm = false;
};

struct DisasterCommand {
  enum Kind {
    DestroyOccupant,   // id,x,z; a=damage; b=0 building/lot,1 lot,2 flora; c=flora burnt variant (8/9) or -1; value=cost
    IgniteOccupant,    // id,x,z; a=intensity  (attach fire effect, flag burning)
    SetFireIntensity,  // id; a=intensity
    ClearBurning,      // id (fire went out, building survives)
    Explosion,         // x,z effect + explosion sound
    Splash,            // x,z
    Sound,             // a=AUDIO.INI item, x,z
    SoundLoop,         // a=item, b=1 start/update, 0 stop; value = intensity percent
    ScreenShake,       // a=strength, b=parameter
    TerrainDelta,      // x,z vertex; a=delta (caller clamps / skips locked vertices); b=1 refill water check
    MarkRadiation,     // x,z
    CreditTreasury,    // value
    MarkAbandoned,     // x,z
    CropCircle,        // x,z 4x4 block
    UfoSwarm,          // a=saucers, b=passive(1), c=abduction passes; pts=bomb targets; pts2=crop targets; pts3=waypoints (2 per saucer)
    UfoLeave,          // a=saucers, ints=1 per saucer that explodes
    LocustSwarm,       // a=swarms; pts=crop targets
    TornadoTouchdown,  // x,z; a=variant+1; f=dirX, f2=dirZ, f3=speed
    ToxicPuffs,        // pts=puff positions; a=level
    ToxicRain,         // a=1 start / 0 stop
    SpawnRioters,      // x,z; a,b=crowd parameters
    VortexSpawn,       // x,z world units in pts[0]? (x,z tile); f=magnitude
    DebrisSpawn,       // a=class parameter (2000/5000/10000), b=duration
    Crater,            // x0,z0..x1,z1 in pts (2 points); a=strength*10; b=decal(1/0)
    StampCoverage,     // a=service(0 fire,1 police), x,z; b=radius; value=effect; f=+1/-1
    SpawnUnit,         // a=service, x,z, id
    RemoveUnit,        // a=service, id
  } kind = Sound;
  int x = 0, z = 0;
  uint32_t id = 0;
  int a = 0, b = 0, c = 0;
  int64_t value = 0;
  float f = 0, f2 = 0, f3 = 0;
  std::vector<std::pair<int, int>> pts, pts2, pts3;
  std::vector<int> ints;
};

struct DisasterEvent {
  enum Kind {
    Started,           // type,x,z; a=1 if a building occupies the tile (fire)
    Premonition,       // type; a=index
    Ended,             // type
    AllOver,           // all disasters finished
    EmergencyDeclared, // a=disaster active, b=false alarm counter < 3
    EmergencyRefused,
    EmergencyEnded,
    FalseAlarmNote,    // advisor note: emergency requested with no disaster
    RiotBrewing,       // a=variant 0..5
    RiotBrokeOut,
    RiotOver,          // a=variant 0..2
    FireStrikeStarted, PoliceStrikeStarted,
    UfoKind,           // a=kind 2..5
  } kind = Started;
  DisasterType type = DisasterType::Fire;
  int x = 0, z = 0, a = 0, b = 0;
};

struct DisasterOutput {
  std::vector<DisasterCommand> commands;
  std::vector<DisasterEvent> events;
  void clear() { commands.clear(); events.clear(); }
  DisasterCommand& cmd(DisasterCommand::Kind k) { commands.emplace_back(); commands.back().kind = k; return commands.back(); }
  void event(DisasterEvent::Kind k, DisasterType t, int x = 0, int z = 0, int a = 0, int b = 0) {
    DisasterEvent e; e.kind = k; e.type = t; e.x = x; e.z = z; e.a = a; e.b = b; events.push_back(e);
  }
};

// ---- Persistence ----------------------------------------------------------------------------
struct DisasterRecord { uint32_t type = 0, group = 0, instance = 0; Bytes data; };
constexpr uint32_t kDisasterRecType = 0xE1F6ABE2u;
constexpr uint32_t kDisasterLayerType = 0x206C6E7Cu, kDisasterLayerGroup = 0x21F6ABCAu;
constexpr uint32_t kGroupEarthquake = 0xC336F77Cu, kGroupUfo = 0x0336F76Au, kGroupSpaceJunk = 0x24889F78u,
                   kGroupLocust = 0x45326359u, kGroupWhirlpool = 0xC446F87Cu;

// A history entry: array of u32, first word the date (Julian day number).
using HistoryEntry = std::vector<uint32_t>;
struct HistoryList {
  int entryWords = 6;
  std::vector<HistoryEntry> entries;
  uint16_t version = 1;
  int32_t extra = 0;     // locust v2 extra s32
};

struct BurnRecord { uint32_t occId = 0; int x = 0, z = 0, intensity = 0, timer = 0, steps = 0, duration = 0; };
struct TornadoInst {
  float px = 0, pz = 0, dx = 0, dz = 1, speed = 60;   // position tiles, unit direction, tiles/min speed
  int variant = 0, lifetime = 45, elapsed = 0, phase = 0, counter = 0, destroyed = 0; int64_t cost = 0;
  int warn[5] = {0, 60, 60, 60, 60};
  float unknown = 0;
};
struct RiotInst {
  int x = 0, z = 0, phase = 0, counter = 0, strength = 0, ticks = 0, burned = 0; bool noWarn = false;
};
struct CloudInst {
  int x = 0, z = 0, phase = 0, counter = 0, level = 0, lifetime = 0, rainStart = 0, rainLeft = 0;
  float dx = 0, dz = 0, speed = 0; bool raining = false; int heightMin = 0, heightMax = 0;
  std::vector<std::pair<float, float>> puffs;
  float cx = 0, cz = 0;
};
struct QuakeInst {
  float mag = 0; int cls = 1, phase = 1, counter = 0, shakeLeft = 0; bool warn = true;
  int T[4] = {0, 60, 120, 180};
  std::vector<std::pair<int, int>> fault;
  int epiX = 0, epiZ = 0, destroyed = 0; int64_t cost = 0;
};
struct UfoInst { int kind = 2, phase = 0, counter = 0, T[5] = {0, 60, 120, 180, 185}; int destroyed = 0, abductions = 0, crops = 0,
                 catLow = 0, catLandmark = 0, catSpace = 0, catHigh = 0; int64_t cost = 0; };
struct LocustInst { int phase = 0, counter = 0, T1 = 0, T2 = 60, TA = 120, eaten = 0; int64_t cost = 0; };
struct WhirlInst { float px = 0, pz = 0, mag = 0; int phase = 0, counter = 0, T[4] = {0, 20, 40, 60}, destroyed = 0; int64_t cost = 0; };
struct JunkInst { int mag = 5, phase = 0, counter = 0, T[5] = {0, 60, 120, 180, 300}, hits = 0; };

struct ParadeRecord {
  uint16_t version = 2;
  uint32_t a = 1; uint8_t b = 0; uint32_t c = 0, x = 0, z = 0; uint8_t d = 0; int32_t route[4] = {0, 0, 0, 0};
  uint32_t e = 0;
};

struct EmergencyMenuState { bool goTo = false, fireDispatch = false, policeDispatch = false, planeDispatch = false; };

// ---- Manager --------------------------------------------------------------------------------
class DisasterManager {
 public:
  DisasterManager();
  DisasterTunables tune;
  void seed(uint32_t s) { rng_.seed(s); }
  GameRng& rng() { return rng_; }

  // Schedulers (spec 21 section 4.1)
  void monthlyTick(DisasterWorld& w, const DisasterInputs& in, DisasterOutput& out);  // random checks + emergency month
  void dayTick(DisasterWorld& w, const DisasterInputs& in, DisasterOutput& out);      // burning + layer bookkeeping
  void minuteTick(DisasterWorld& w, const DisasterInputs& in, DisasterOutput& out);   // every active instance

  // Starts. `warn` = warning flag. All return success.
  bool startFire(DisasterWorld& w, int x, int z, DisasterOutput& out, int intensity = 0);
  bool startRandomFire(DisasterWorld& w, DisasterOutput& out);
  bool startTornado(DisasterWorld& w, int x, int z, float dirX, float dirZ, int variant, int lifetime,
                    float speed, bool warn, DisasterOutput& out);   // speed in scenario units (tiles/min)
  bool startRandomTornado(DisasterWorld& w, DisasterOutput& out);
  bool startEarthquake(DisasterWorld& w, const DisasterInputs& in, float m, bool warn, DisasterOutput& out);
  bool startRiot(DisasterWorld& w, int x, int z, bool warn, DisasterOutput& out);
  bool startUfo(DisasterWorld& w, const DisasterInputs& in, bool warn, DisasterOutput& out);
  bool startToxicCloud(DisasterWorld& w, int x, int z, bool warn, DisasterOutput& out,
                       int level = 0, float dirX = 0, float dirZ = 0);
  bool startWhirlpool(DisasterWorld& w, const DisasterInputs& in, float m, bool warn, DisasterOutput& out);
  bool startLocusts(DisasterWorld& w, const DisasterInputs& in, bool warn, DisasterOutput& out);
  bool startSpaceJunk(DisasterWorld& w, const DisasterInputs& in, float m, bool warn, DisasterOutput& out);
  // Menu entry: random start of the type (bypasses timers and the enabled flag).
  bool startRandom(DisasterType t, DisasterWorld& w, const DisasterInputs& in, DisasterOutput& out);
  // Scenario commands (script args are integers, tile coordinates 1-based) (section 3)
  bool scriptEarthquake(DisasterWorld& w, const DisasterInputs& in, int a0, int a1, DisasterOutput& out);
  bool scriptFire(DisasterWorld& w, int a0, int a1, DisasterOutput& out);
  bool scriptLocusts(DisasterWorld& w, const DisasterInputs& in, int a0, DisasterOutput& out);
  bool scriptRiot(DisasterWorld& w, int a0, int a1, int a2, DisasterOutput& out);
  bool scriptSpaceJunk(DisasterWorld& w, const DisasterInputs& in, int a0, int a1, DisasterOutput& out);
  bool scriptTornado(DisasterWorld& w, const int args[8], DisasterOutput& out);
  bool scriptUfo(DisasterWorld& w, const DisasterInputs& in, int a0, DisasterOutput& out);
  bool scriptToxicCloud(DisasterWorld& w, int a0, int a1, int a2, DisasterOutput& out);
  bool scriptWhirlpool(DisasterWorld& w, const DisasterInputs& in, int a0, int a1, DisasterOutput& out);
  void setDisastersEnabled(bool b) { enabled_ = b; }
  bool disastersEnabled() const { return enabled_; }

  // Stop everything of one type (ends instances without side effects).
  void stop(DisasterType t, DisasterOutput& out);

  // Events reported back by sprite automata / effects
  void ufoBombHit(DisasterWorld& w, int x, int z, DisasterOutput& out);
  void ufoBeamHit(DisasterWorld& w, int x, int z, DisasterOutput& out);
  void ufoAbduction() ;
  void ufoCropCircle(DisasterWorld& w, int x, int z, DisasterOutput& out);
  void locustEaten(DisasterWorld& w, int x, int z, DisasterOutput& out);
  void vortexPosition(DisasterWorld& w, int x0, int z0, DisasterOutput& out);
  void vortexObjectSucked(DisasterWorld& w, int x, int z, bool bridge, DisasterOutput& out);
  void vortexFinished(const DisasterInputs& in, DisasterOutput& out);
  void debrisImpact(DisasterWorld& w, int x, int z, DisasterOutput& out);
  void rioterAt(DisasterWorld& w, int x, int z, DisasterOutput& out);
  bool riotRelocate(DisasterWorld& w, DisasterOutput& out);

  // Generic destroy routine (section 9). Returns false if nothing happened.
  bool destroyOccupant(DisasterWorld& w, const OccInfo& o, int damage, DisasterOutput& out);

  // Emergency (section 10)
  void declareEmergency(DisasterOutput& out);
  bool emergencyActive() const { return emergencyActive_; }
  bool emergencyWithDisaster() const { return emergencyEW_; }
  int falseAlarms() const { return falseAlarms_; }
  int64_t disasterCost() const { return cost_; }
  EmergencyMenuState menuState() const;
  bool anyActive() const;
  bool anyActiveNonParade() const;
  bool active(DisasterType t) const;
  int activeCount(DisasterType t) const;

  // Inspection
  const std::vector<BurnRecord>& burning() const { return burning_; }
  const std::vector<TornadoInst>& tornadoes() const { return tornadoes_; }
  const std::vector<RiotInst>& riots() const { return riots_; }
  const std::vector<CloudInst>& clouds() const { return clouds_; }
  const std::vector<QuakeInst>& quakes() const { return quakes_; }
  const std::vector<UfoInst>& ufos() const { return ufos_; }
  const std::vector<LocustInst>& locusts() const { return locusts_; }
  const std::vector<WhirlInst>& whirlpools() const { return whirls_; }
  const std::vector<JunkInst>& junk() const { return junk_; }
  HistoryList& quakeHistory() { return quakeHist_; }
  HistoryList& ufoHistory() { return ufoHist_; }
  HistoryList& junkHistory() { return junkHist_; }
  HistoryList& locustHistory() { return locustHist_; }
  HistoryList& whirlHistory() { return whirlHist_; }
  ParadeRecord& parade() { return parade_; }
  bool paradeActive = false;

  // Persistence: records `E1F6ABE2:<G>:0` and layer `206C6E7C:21F6ABCA:0` (spec 10 section 6.5).
  std::vector<DisasterRecord> saveRecords() const;
  // Returns false if the record is not ours or is malformed (state left unchanged then).
  bool loadRecord(const DisasterRecord& r);
  // Layer record fields (section "Layer record")
  uint32_t layerReserved0 = 0, layerFlagA = 0, layerFlagB = 0;
  void resetToNewCity();

 private:
  friend struct DisasterTestAccess;
  GameRng rng_;
  bool enabled_ = true;
  bool wasActive_ = false;
  // fire
  std::vector<BurnRecord> burning_;
  int fireCounter_ = 0, fireInterval_ = 0;
  bool ignite(DisasterWorld& w, const OccInfo& o, int intensity, DisasterOutput& out);
  void fireSoundUpdate(DisasterOutput& out, bool force = false);
  void burnTick(DisasterWorld& w, DisasterOutput& out);
  void spread(DisasterWorld& w, const OccInfo& o, int p, DisasterOutput& out);
  bool sweepIgnite(DisasterWorld& w, int cx, int cz, int group, DisasterOutput& out);
  // others
  std::vector<TornadoInst> tornadoes_; int tornadoCounter_ = 0, tornadoInterval_ = 0;
  std::vector<RiotInst> riots_; int riotCounter_ = 0;
  std::vector<CloudInst> clouds_; int32_t lastCloudJdn_ = 0; bool haveCloud_ = false;
  std::vector<QuakeInst> quakes_;
  std::vector<UfoInst> ufos_;
  std::vector<LocustInst> locusts_; int locustMonths_ = 0;
  std::vector<WhirlInst> whirls_;
  std::vector<JunkInst> junk_;
  HistoryList quakeHist_, ufoHist_, junkHist_, locustHist_, whirlHist_;
  ParadeRecord parade_;
  int32_t today_ = 2415021; int year_ = 1900;
  // emergency / layer
  bool emergencyActive_ = false, emergencyEW_ = false; int emergencyMonths_ = 0, falseAlarms_ = 0;
  int64_t cost_ = 0;
  void checkAllOver(DisasterOutput& out);
  void endEmergency(DisasterOutput& out);
  void addHistory(HistoryList& h, int32_t date, uint32_t d1, int destroyed, int64_t cost);
  void seedHistory(HistoryList& h, int32_t today);
  void tornadoTick(DisasterWorld& w, DisasterOutput& out);
  void riotTick(DisasterWorld& w, const DisasterInputs& in, DisasterOutput& out);
  void cloudTick(DisasterWorld& w, DisasterOutput& out);
  void quakeTick(DisasterWorld& w, DisasterOutput& out);
  void ufoTick(DisasterWorld& w, DisasterOutput& out);
  void locustTick(DisasterWorld& w, DisasterOutput& out);
  void whirlTick(DisasterWorld& w, DisasterOutput& out);
  void junkTick(DisasterWorld& w, DisasterOutput& out);
  void launchUfo(DisasterWorld& w, const DisasterInputs& in, UfoInst& u, DisasterOutput& out);
  void quakeMainShock(DisasterWorld& w, QuakeInst& q, DisasterOutput& out);
  void quakeDamage(DisasterWorld& w, QuakeInst& q, DisasterOutput& out);
  bool findRiotSite(DisasterWorld& w, int cx, int cz, int& ox, int& oz);
  bool cropTile(DisasterWorld& w, int x, int z, OccInfo* o = nullptr) const;
  DisasterInputs lastIn_;
};

// ---- Strikes (section 11) -------------------------------------------------------------------
struct ServiceStrike {
  int minFunding = 50;          // MinAcceptableFunding
  int maxStat = 100;            // MaxAcceptableFlam / MaxAcceptableCrime
  int efficiency = 50;          // StrikeEfficiency percent
  int maxDuration = 100;        // MaxStrikeDuration
  bool onStrike = false; int counter = 0;
  // One layer update. Returns true if a strike starts this update. stat = city flammability/crime statistic.
  bool update(int fundingPct, int stations, int stat, GameRng& rng);
  // Station coverage multiplier: coverage * efficiency / 100 during a strike (integer arithmetic).
  int apply(int coverage) const { return onStrike ? coverage * efficiency / 100 : coverage; }
  int chancePer10000(int fundingPct, int stations, int stat) const;
};

// ---- Dispatch (section 10.3) ---------------------------------------------------------------
struct CoverageCellDelta { int cx, cz, delta; };
// Coverage stamp of one dispatched unit: per tile with floor(dist) <= radius, the 4x4 cell gains `effect`.
// sign = -1 reverses a stamp.
std::vector<CoverageCellDelta> dispatchStamp(int mapW, int mapH, int x, int z, int radius, int effect, int sign = 1);
inline int coverageByteFromRaw(int raw) { int v = raw / 16; return v < 0 ? 0 : v > 255 ? 255 : v; }

class DispatchTracker {
 public:
  explicit DispatchTracker(int service = 0) : service_(service) {}
  // stations = number of stations of the service. Emits recall of oldest unit (if U >= S+1), unit placement and stamp.
  void dispatch(int x, int z, int stations, int mapW, int radius, int effect, uint32_t& nextId, DisasterOutput& out);
  size_t deployed() const { return units_.size(); }
  struct Unit { uint32_t id; int x, z, radius, effect; };
  const std::deque<Unit>& units() const { return units_; }
 private:
  int service_;
  std::deque<Unit> units_;
};

// Parse helpers (also used by tests)
uint32_t disasterOccupantKeyFire();    // 0x207EDC0E:0x0000057E:0x000032CC -> returns instance 0x32CC
uint32_t disasterOccupantKeyPolice();  // instance 0x32CB

}  // namespace m3k
