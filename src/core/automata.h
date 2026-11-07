// Street-sim automata (vehicles, pedestrians, boats, aircraft, trains). spec/26-automata.md.
// Platform independent: data parsing (VATTRIB/VCLPROD/PATTRIB/PPLPROD/ANSPRATT/PPATHATT/VPATHATT/
// TRBLKATT/SIMSCRPT/TRNPROD/AIRTRMGR), spawn logic, movement, and sprite selection output.
// Presentation only: nothing here writes back into the simulation.
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "core/bytes.h"
#include "core/ini.h"
#include "core/ixf.h"

namespace m3k {

// ---------------------------------------------------------------- resource type IDs (spec 2)
namespace automata_ids {
constexpr uint32_t kVAttribType = 0xC25C2BF7, kVAttribGroup = 0x025C2C45;
constexpr uint32_t kVclProdType = 0x625C6CC7, kVclProdGroup = 0xA25C6D40;
constexpr uint32_t kPAttribType = 0xC22B54FA, kPAttribGroup = 0xA22B551E;
constexpr uint32_t kPplProdType = 0x21FD6EEF, kPplProdGroup = 0x21FD8E4C;
constexpr uint32_t kAnSprAttType = 0x222B7EE4, kAnSprAttGroup = 0x822B7E6B;
constexpr uint32_t kPPathType = 0xA20BF670, kPPathGroup = 0xC20BF6C3;
constexpr uint32_t kVPathType = 0xC25C74F2, kVPathGroup = 0xA25C7520;
constexpr uint32_t kTrBlkType = 0x625C6226, kTrBlkGroup = 0x825C6289;
constexpr uint32_t kScriptType = 0x822B51AA, kScriptGroup = 0xE22B51C4;
constexpr uint32_t kTrnProdType = 0x22CEFD99, kTrnProdGroup = 0x62CEFDDB;
constexpr uint32_t kAirTrMgrType = 0x62DFCCEC, kAirTrMgrGroup = 0x62DFCD34;
constexpr uint32_t kSpriteInstType = 0x00006300, kSpriteInstGroup = 0x00006400;
constexpr double kSpeedFactor = 1.0 / 450000000.0;  // stored speed -> 1/256-tile per microsecond
constexpr uint32_t kDefaultTrBlk = 0x1D00;
}  // namespace automata_ids

// ---------------------------------------------------------------- parsed records
struct Pt { int a = 0, h = 0, b = 0; };  // 1/256 tile; h = height

struct VAttrib {
  bool emergency = false;
  int32_t motionSound = 0, paramB = 0, sirenSound = 0, length = 0;
  ResKey anim, script;
  uint32_t maxSpeedRaw = 0;
  double maxSpeed() const { return maxSpeedRaw * automata_ids::kSpeedFactor; }
};
struct VclProd { float rate = 0; std::vector<std::pair<uint32_t, ResKey>> entries; };  // weight, VATTRIB key
struct PAttrib { ResKey anim, script; };

struct PplMode {
  uint32_t dateCode = 0;
  bool dated = false;
  int startMonth = 0, startDay = 0, endMonth = 0, endDay = 0;  // dated modes
  int64_t cumThreshold = 0;                                    // undated modes (running -1 + code)
  float rate = 0;
  uint32_t radius = 8;
  std::vector<std::pair<uint32_t, ResKey>> entries;  // weight, PATTRIB key
  std::vector<int64_t> cum;                          // cumulative weights, running total starts at -1
  bool containsDate(int month, int day) const;       // inclusive; ranges may wrap the year end
};
struct PplCountry { std::string name; std::vector<PplMode> modes; };
struct PplProd { std::vector<PplCountry> countries; };

struct AnimSeq { uint32_t id = 0; std::vector<uint16_t> frames; };
struct AnSprAtt { ResKey sprite; std::vector<AnimSeq> seqs;
  const AnimSeq* find(uint32_t id) const; };

struct PPath { std::vector<Pt> pts; int entryHeading8 = 0, exitHeading8 = 0; std::vector<int> cum; int length() const { return cum.empty() ? 0 : cum.back(); } };  // headings 0..7
struct VPath {
  std::vector<Pt> pts;
  int entryHeading = 0, exitHeading = 0;  // 0/8/16/24
  int lane = 0;                           // stored + 1
  std::vector<int> cum;                   // cumulative arc length per point (1/256 tile)
  double grade = 0;
  int heading0 = 0;                       // initial 32-step heading
  int length() const { return cum.empty() ? 0 : cum.back(); }
};
struct Conn { int8_t next = -1; uint8_t heading = 0; uint32_t path = 0; uint32_t value = 0; };
struct PedLane { uint32_t id = 0; Pt pos; std::vector<Conn> conns; };
struct VehLane { uint32_t id = 0; int8_t type = 0; Pt pos; std::vector<Conn> conns; };
struct TrBlk {
  int box[4] = {0, 0, 0, 0};  // already divided by 256
  bool flag = false;
  std::vector<PedLane> ped;   // set A
  std::vector<VehLane> veh;   // set B
  const VehLane* vehLane(int id) const;
  const PedLane* pedLane(int id) const;
};

struct ScriptStep { uint16_t op = 0; std::vector<int32_t> args; };
struct Script { std::vector<ScriptStep> steps; };

struct SpriteFrame { uint16_t index = 0; uint32_t category = 0, spriteId = 0; int8_t flags = 0; int16_t ox = 0, oy = 0; };
struct SpriteInst {
  int zoomCount = 0, rotCount = 0, zoomSetCount = 0, animSetCount = 0, animFrames = 0;
  uint32_t layerFlags = 0;
  std::vector<SpriteFrame> frames;
};

struct Consist { int loco = 0; std::vector<int> list[4]; };
struct TrnProd { int64_t minUs = 0, maxUs = 0; std::vector<Consist> consists; };
struct AirTrMgr { int maxActive = 0; std::vector<int> models; };

struct StreetSimTunables {
  int maxMarinaToBoatDistance = 30;
  std::vector<std::pair<std::string, int>> keyVehiclesLow, keyVehiclesHigh;
  std::vector<std::pair<std::string, int>> emergencyLow, emergencyHigh;  // name -> VATTRIB instance
  static StreetSimTunables fromIni(const IniFile& ini);
};

// ---------------------------------------------------------------- record parsers
// Binary ("BIN\r" header) and text forms are both accepted. Each returns false on malformed data.
bool parseVAttrib(const Bytes& b, VAttrib& out);
bool parseVclProd(const Bytes& b, VclProd& out);
bool parsePAttrib(const Bytes& b, PAttrib& out);
bool parsePplProd(const Bytes& b, PplProd& out);  // "BIN!" / "TXT!"
bool parseAnSprAtt(const Bytes& b, AnSprAtt& out);
bool parsePPath(const Bytes& b, PPath& out);
bool parseVPath(const Bytes& b, VPath& out);
bool parseTrBlk(const Bytes& b, TrBlk& out);
bool parseSpriteInst(const Bytes& b, SpriteInst& out);  // binary only
bool parseScript(const std::string& text, Script& out);
bool parseTrnProd(const std::string& text, TrnProd& out);
bool parseAirTrMgr(const std::string& text, AirTrMgr& out);

// 32-step heading (0..31) of vector (da, db); see spec 3.8 (axis convention [OPEN]).
int heading32(double da, double db);
// Point along a vehicle/ped path by arc length (clamped). Also returns heading in 32 steps.
Pt vpathPoint(const VPath& p, double dist, int* heading = nullptr);
Pt polylinePoint(const std::vector<Pt>& pts, const std::vector<int>& cum, double dist, int* heading = nullptr);

// ---------------------------------------------------------------- data store
class AutomataData {
 public:
  // Add every automata record in an IXF (dispatch by record Type). Holiday files may be loaded the
  // same way (same types); PPLPROD blocks of the same instance are merged. Returns records read;
  // `loadErrors` counts records that failed to parse.
  int loadIxf(const IxfFile& f);
  // Direct adders (used by tests / custom loaders).
  void addRecord(const ResKey& k, const Bytes& payload);

  std::map<uint32_t, VAttrib> vattrib;
  std::map<uint32_t, VclProd> vclprod;
  std::map<uint32_t, PAttrib> pattrib;
  std::map<uint32_t, PplProd> pplprod;
  std::map<uint32_t, AnSprAtt> ansprat;
  std::map<uint32_t, PPath> ppath;
  std::map<uint32_t, VPath> vpath;
  std::map<uint32_t, TrBlk> trblk;
  std::map<uint32_t, Script> scripts;
  std::map<uint32_t, TrnProd> trnprod;
  std::map<uint32_t, AirTrMgr> airtrmgr;
  std::unordered_map<ResKey, SpriteInst, ResKeyHash> sprites;
  StreetSimTunables tunables;
  int loadErrors = 0;

  const TrBlk* trblkFor(uint32_t pieceId, int rotation) const;  // falls back to 0x1D00
  const PPath* ppathFor(uint32_t pieceId, int rotation) const;
  // Pedestrian production lookup.
  // Country block = name == country (case-insens.), else "def". With holidays, a "<name>:holidays"
  // block is consulted first.
  const PplMode* pickPplMode(const PplProd& t, const std::string& country, bool holidays, int month, int day,
                             uint32_t undatedDraw1000 = 0) const;
};

// Weighted draws (spec 3.3, 3.5). `draw` is a uniform value; helpers take a callable rng.
// VCLPROD: r in [0,sum(weights)); first entry whose cumulative weight exceeds r.
int vclProdPick(const VclProd& t, uint32_t r);
// PPLPROD mode: r in [0, total]; first entry with cum >= r. Returns index or -1.
int pplModePick(const PplMode& m, uint32_t r);

// ---------------------------------------------------------------- host interface
enum class AutoNet { Road, Rail, Walk };
struct PieceRef { uint32_t pieceId = 0; int rotation = 0; };

class AutomataHost {
 public:
  virtual ~AutomataHost() = default;
  virtual int mapWidth() const = 0;
  virtual int mapHeight() const = 0;
  virtual int trafficDensity(int /*x*/, int /*y*/) const { return 0; }                // 0..255
  virtual bool pieceAt(int x, int y, AutoNet k, PieceRef& out) const = 0;              // drivable/walkable piece
  virtual bool isWater(int /*x*/, int /*y*/) const { return false; }
  virtual bool pedEligible(int /*x*/, int /*y*/) const { return false; }              // occupant flag 0x8000
  // Base height (1/256 units, same scale as path h) that path and flight heights at tile (x, y) are
  // relative to: the network piece's altitude, else the ground or water surface.
  virtual int groundHeight(int /*x*/, int /*y*/) const { return 0; }
  virtual void gameDate(int& month, int& day) const { month = 1; day = 1; }
  // Waypoints (tile coords) a seaport's ships follow; empty = no route (spawner not ready).
  virtual std::vector<std::pair<double, double>> seaRoute(int /*portId*/) const { return {}; }
  // Same with the port's anchor tile (hosts that find the route from the map override this one).
  virtual std::vector<std::pair<double, double>> seaRouteFrom(int portId, int /*tileX*/, int /*tileY*/) const { return seaRoute(portId); }
  // City condition codes 4 and 5, and presence of occupant 0x2AFA (ship VCLPROD index bits).
  virtual bool cityCondition(int /*code*/) const { return false; }
  virtual bool hasOccupantType2AFA() const { return false; }
};

struct AutoView {
  int x0 = 0, y0 = 0, x1 = -1, y1 = -1;  // inclusive visible tile rectangle
  int zoom = 4;                          // 0 farthest .. 4 closest
  int rotation = 0;                      // 0..3
  int secondaryMode = 0;                 // non-zero (e.g. underground view) deactivates
};

// ---------------------------------------------------------------- outputs
enum class AutomatonKind { Vehicle, Pedestrian, Train, Boat, Aircraft };

struct AutomatonSprite {
  int id = 0;
  AutomatonKind kind = AutomatonKind::Vehicle;
  uint32_t category = 0, spriteId = 0;  // sprite DAT group + instance for the renderer
  int flags = 0;                        // 2 = draw magnified 2x
  double tileX = 0, tileY = 0;          // world position in tiles (fractional)
  double height = 0;                    // 1/256-tile height
  int heading32 = 0;                    // facing in 32 steps (0..31)
  int frameIndex = 0;                   // index into the sprite instance's frame list
};

struct SoundEvent {
  enum Type { Start, Move, Stop, Siren } type = Start;
  int soundId = 0, automatonId = 0, variant = 0;
  double tileX = 0, tileY = 0;
};

enum class EmergencyType { FirePumper, Ambulance, PoliceCar };

constexpr int kMaxRoadVehicles = 200;
constexpr int kMaxPedestrians = 400;
constexpr int64_t kAnimTickUs = 66667;  // animation tick (assumption: ~15 Hz; spec gives no rate)

// ---------------------------------------------------------------- system
class StreetSim {
 public:
  struct Config {
    int zoomThreshold = 2;       // Z = 5 - p
    bool holidays = false;
    std::string country = "def";
    bool highMemory = true;      // selects key/emergency vehicle lists
    uint32_t seed = 1;
  };
  StreetSim(const AutomataData& data, AutomataHost& host, const Config& cfg);
  ~StreetSim();

  void setView(const AutoView& v);          // activation, view-change handling (spec 5.6)
  void update(int64_t dtMicros);             // real-time elapsed time (spec 7)
  bool active() const;

  // Sources.
  int addBuilding(int tileX, int tileY, const ResKey& pedProd, const ResKey& vehProd);  // attr 0x6B / 0x6C
  void removeBuilding(int sourceId);
  // Same end state as calling removeBuilding for each id, in O(buildings) instead of O(n^2).
  void removeBuildings(const std::vector<int>& sourceIds);
  int addSeaport(int tileX, int tileY);
  int addAirport(int tileX, int tileY);
  int addMarina(int tileX, int tileY);
  void removeSpawner(int spawnerId);   // seaport / airport / marina / station timer (its live vehicles stay until they finish)
  // A parade (spec 26 5.7): the event layer picks the route and the lineup; the street sim only moves the vehicles. Vehicle i
  // starts the sum of the preceding spacings (tiles) behind the route start, facing along the route, and drives to the end
  // at its VATTRIB speed [INFERRED movement: opcode 200 is untraced]. Returns the number of vehicles created.
  struct ParadeVehicle { uint32_t vattrib = 0; double spacingTiles = 0; };
  int startParade(double x0, double y0, double x1, double y1, const std::vector<ParadeVehicle>& lineup);
  int paradeVehicleCount() const;
  int addStation(int tileX, int tileY, bool subway);   // TRNPROD instance 1 (rail) / 2 (subway)
  void addHelipad(int tileX, int tileY);
  void pointSpawnPedestrians(int tileX, int tileY, int n);  // honoured only on eligible tiles
  // Whirlpool capture (spec 21 s12.6): boats currently simulated inside a square, then taking one over (it stops moving by itself), placing it, removing it.
  struct BoatRef { int id = 0; double x = 0, y = 0; };
  std::vector<BoatRef> boatsInSquare(double centreX, double centreY, double halfSize) const;
  void captureAutomaton(int id);
  void moveAutomaton(int id, double x, double y);
  void removeAutomaton(int id);
  bool dispatchEmergency(EmergencyType t, int tileX, int tileY);
  void notifyEvent();                         // event mode 0 -> 1 (-> 2 after 15,000,000 us)
  int eventMode() const;

  // Output.
  std::vector<AutomatonSprite> sprites() const;   // visible automata with sprite selection
  std::vector<SoundEvent> takeSoundEvents();
  int trafficLevelPercent() const;                // last value of broadcast 0x402 argument
  // Stats / diagnostics.
  int roadVehicleCount() const;
  int pedestrianCount() const;
  int automatonCount() const;
  int vehicleLimit() const;
  int averageDensity() const;
  int countKind(AutomatonKind k) const;

  // Mix table access (per 32x32 cell): list of (VATTRIB instance, weight).
  std::vector<std::pair<uint32_t, double>> mixTable(int tileX, int tileY) const;

 public:
  struct Impl;  // implementation detail
 private:
  std::unique_ptr<Impl> p_;
};

}  // namespace m3k
