// Land-value layer (spec/15-sim-growth.md section 6; record spec/10 section 6.12).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "core/bytes.h"
#include "core/ini.h"
#include "core/valves.h"

namespace m3k {
namespace growth {

// Zone classes for land value (spec 15 section 1).
enum LvClass { kLvNone = 0, kLvRes = 1, kLvCom = 2, kLvInd = 3 };
int lvZoneClass(int zone);

struct LvFactor { int t = 0; float b = 0, c = 0; };  // threshold, above slope (b/1000), below slope (c/1000)

struct LvCentre {  // "E+Rp x0:y0 x1:y1 ..." (section 6.2)
  int edge = 0, radiusPct = 100;
  std::vector<CurvePoint> pts;
  static LvCentre parse(const std::string& text);
};

struct LvAgentMod { int8_t all = 0, r = 0, c = 0, i = 0; };

struct LandValueTuning {
  int updatePeriod = 180;
  float radB = 0;
  LvFactor water, air, garbage, crime;
  int waterEffect = 0, waterRadiusBlocks = 0, hillEffect = 0, homelessShelterPct = 0;
  LvCentre res, com, ind;
  std::map<uint32_t, LvAgentMod> agentMods;          // [AgentIDModifiers]
  std::map<uint32_t, uint32_t> hierarchy;            // child agent type -> parent (CitySim.ini [AgentTypeHierarchy])

  void parse(const IniFile& landValueIni);           // SC3LandValue.ini
  void parseHierarchy(const IniFile& citySimIni);
  static LvFactor parseTriple(const std::string& s, bool low8bit);
  static LvAgentMod parseMod(const std::string& s);
  const LvAgentMod* modFor(uint32_t agentType) const;
};

struct LvOccupant { int x = 0, z = 0, sizeX = 1, sizeZ = 1; uint32_t agentType = 0; bool occupied = true; };

// Abstract city view. Coordinates are tiles (x, z).
class LandValueCity {
 public:
  virtual ~LandValueCity() = default;
  virtual int mapW() const = 0;
  virtual int mapH() const = 0;
  virtual int zoneAt(int x, int z) const = 0;
  virtual bool radioactive(int, int) const { return false; }
  virtual int airPollution(int, int) const { return 0; }     // 0..32767
  virtual int waterPollution(int, int) const { return 0; }   // 0..32767
  virtual int garbage(int, int) const { return 0; }          // 0..32767
  virtual int crime(int, int) const { return 0; }            // 0..255
  virtual int altitude(int x, int z) const = 0;              // terrain altitude of vertex (x, z), 0..W x 0..H
  virtual int waterSurface(int x, int z) const { return seaLevel(); }   // water-surface altitude of vertex (x, z)
  virtual int seaLevel() const = 0;                          // base (water) level
  virtual bool hasWater(int x, int z) const = 0;             // water tile (spec 15 s6.3 step 1)
  virtual uint32_t blockSignature(int, int) const { return 0; }   // zone/occupant signature of a block's 4 tiles
  virtual int64_t population() const { return 0; }
  virtual bool ordinanceEnacted(uint32_t) const { return false; }
  virtual void occupants(std::vector<LvOccupant>&) const {}
  // City centre (spec 15 s6.3 step 6b) [STATIC]: area-weighted centroid of the lot-bearing occupants (see
  // buildingCentroid); the map centre when there are none.
  virtual void cityCentre(int& cx, int& cz) const { cx = mapW() / 2; cz = mapH() / 2; }
  // Reference height Href; default (-1) = computed (spec 15 s6.3 cycle start step 4).
  virtual int hillReference() const { return -1; }
};

// Area-weighted centroid of building footprints with the original's rounding quirk (spec 15 s6.3 step 6b) [STATIC].
// Footprint [x0..x0+sizeX-1] x [z0..z0+sizeZ-1]. Order-independent.
struct BuildingCentroid {
  int64_t sumX = 0, sumZ = 0, n = 0;
  void add(int x0, int z0, int sizeX, int sizeZ, int sign = 1);
  void get(int mapW, int mapH, int& cx, int& cz) const;
};

// Href (spec 15 s6.3 cycle start step 4) [STATIC]: over all (W+1) x (H+1) vertices whose altitude is above the
// vertex's water surface, mean of (altitude - sea level), integer division, low 8 bits; 0 when no vertex is dry.
int hillReferenceOf(const LandValueCity& c);

class LandValueLayer {
 public:
  void init(int W, int H, const LandValueTuning& t);   // new-city state
  void setTuning(const LandValueTuning& t) { tune_ = t; derivedValid_ = false; }
  const LandValueTuning& tuning() const { return tune_; }

  // One update call: processes (W*H)/(UpdatePeriod*4) blocks (section 6.1).
  void update(const LandValueCity& city);
  // Loaded city: blocks whose four tiles are zone 0 but hold a saved value above what the rule can give keep that byte
  // until a tile's zone or occupant changes (spec 15 s6.3 step 6g note, interim) [OPEN]. Not saved.
  void pinUnzonedBlocks(const LandValueCity& city);
  // Compute every block now (initial fill / tests). Does not touch scan position or statistics.
  void recomputeAll(const LandValueCity& city);
  // Run the cycle-start recomputation (curves, agent maps, Href, statistics roll-over).
  void cycleStart(const LandValueCity& city, bool rollStats);

  int landValueAtTile(int x, int z) const { return lv[size_t(x / 2) * (H / 2) + (z / 2)]; }
  int cityAverage() const { return avgLV; }

  int W = 0, H = 0;
  std::vector<uint8_t> lv;  // (W/2)*(H/2), index bx*(H/2)+bz
  uint8_t phase = 0, avgLV = 0;
  uint32_t scanX = 4, scanY = 4, count = 0, tail = 260;
  int32_t prevSum = 0, sum = 0;
  std::array<uint32_t, 32> history{};

  // Record 206C6E7C:C0AB8A88 (version 1, no markers).
  bool parse(const uint8_t* p, size_t n);
  Bytes write() const;

  static const int kPhaseOffsets[16][2];
  // An unclassed block cannot exceed about 6 by the rule (spec 15 s6.3 step 6g note); saved bytes above this are pinned.
  static constexpr int kUnzonedMax = 6;
  int hillRef() const { return href_; }
  // Diagnostic: the inputs of one block's computation (tools).
  std::string explainBlock(const LandValueCity& c, int x, int y) const;
  int centreValue(int cls) const { return cls == kLvRes ? cr_ : cls == kLvCom ? cc_ : ci_; }

 private:
  void computeBlock(const LandValueCity& c, int x, int y, int cx, int cy, std::string* dbg = nullptr);
  void buildWaterDist(const LandValueCity& c);
  void buildModMaps(const LandValueCity& c);
  LandValueTuning tune_;
  bool derivedValid_ = false;
  int cr_ = 0, cc_ = 0, ci_ = 0, href_ = 0;
  std::unordered_map<uint32_t, uint32_t> pinned_;   // block index -> signature at load
  std::vector<uint8_t> waterDist_;     // per block, Chebyshev distance capped at 255
  std::vector<int16_t> modAll_, modR_, modC_, modI_;  // (W/4)*(H/4)
};

}  // namespace growth
}  // namespace m3k
