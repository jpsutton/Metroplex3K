// Zone manager and zone developers (spec/15-sim-growth.md sections 4, 5, 7; record spec/10 section 6.4).
// Standalone state classes driven through the abstract GrowthCity view, so they can be tested
// synthetically and integrated later. Everything here lives in namespace m3k::growth.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/ini.h"
#include "core/ixf.h"
#include "core/valves.h"

namespace m3k {
namespace growth {

// ---- random generators (spec 14 section 8) ----
class SimRand {  // simulation integer generator
 public:
  explicit SimRand(uint32_t seed = 1) : s_(seed) {}
  void seed(uint32_t x) { s_ = x; }
  uint32_t next() { uint64_t v = uint64_t(s_) * 1103515245ull + 12345ull; s_ = uint32_t(v); return uint32_t(v >> 16); }
  // Rand(n): n<=0 -> 0; n < 0xFFFFFF -> R mod n; else rejection sampling in [0, n].
  int rand(int64_t n);
 private:
  uint32_t s_;
};
class CrtRand {  // Microsoft C runtime rand(), initial seed 1
 public:
  explicit CrtRand(uint32_t seed = 1) : s_(seed) {}
  int next() { s_ = s_ * 214013u + 2531011u; return int((s_ >> 16) & 0x7FFF); }
 private:
  uint32_t s_;
};

constexpr uint32_t kDevResidential = 0x619BA64E, kDevCommercial = 0x41A3ADC1, kDevIndustrial = 0xE1A53B30,
                   kDevAirport = 0xC1F81E7E, kDevSeaport = 0x82D2D72B, kDevLandfill = 0x82348DE5;

// ---- configuration data ----
struct Category {
  uint32_t agentType = 0;
  char cls = 'N';  // 'N' (R/C), industrial: 'D' dirty, 'C' clean, 'A' agriculture
  int density = 1, maxLV = 255, minLV = 0;
};
struct ZoneDescription {
  int zone = 0;
  uint32_t developerClass = 0, baseAgentType = 0;
  std::vector<Category> cats;
  static ZoneDescription parse(int zone, const std::string& value);  // "0x619BA64E,3000:3100,1,200,40;..."
  bool hasClass(char c) const { for (auto& k : cats) if (k.cls == c) return true; return false; }
};

struct BuildingChoice {  // a member of a family list (weight = duplicates in the list)
  uint32_t occId = 0;
  int sizeX = 1, sizeZ = 1;
  uint32_t agentType = 0;
  uint32_t flagWord = 0;  // attribute 0xCA (0x100 filler, 0x400 farm field)
  bool isFiller() const { return (flagWord & 0x100) != 0; }
};
struct CategoryLists { std::vector<BuildingChoice> filler, small, large; };
struct DevRule {
  uint32_t id = 0;
  int a = 1, b = 10, c = 0, d = 50;
  bool hasParams = false;
  std::map<uint32_t, CategoryLists> lists;  // by category agent type
  // Add `weight` copies of a family member under a category (spec 15 section 5.2).
  void addMember(uint32_t categoryAgentType, const BuildingChoice& b, int weight);
};
struct RuleBinding { int zone = 0; uint32_t ruleId = 0; std::vector<int> families; };

struct DeveloperTuning {
  int noServicePercentage = 20, maxDistFromTransport = 3, constructionTimeFactor = 60;
  int higherLandValuePct = 0, lowerLandValuePct = 0, higherDensityPct = 0, anarchyFactor = 0;
};
struct IndustrialTuning : DeveloperTuning {
  int pctChanceAg = 0, minAgAirPollution = 0, minAgWaterPollution = 0, maxAgAirPollution = 32767, maxAgWaterPollution = 32767;
  int lawnChemBanOrdEffect = 0, farmersMarketOrdEffect = 0;
  int minMinCleanInd = 0, maxMinCleanInd = 0, minMaxCleanInd = 100, maxMaxCleanInd = 100, minCleanIndFlatYear = 2000, maxCleanIndFlatYear = 2000;
  int cleanAirOrd = 0, touristPromotionOrd = 0, conservationCorpsOrd = 0, indPolluteFeeOrd = 0, cleanIndAssocOrd = 0,
      electronicsIncentiveOrd = 0, aerospaceIncentiveOrd = 0, electronicsJobFairOrd = 0, biotechIncentiveOrd = 0, publicAccessCableOrd = 0;
};

struct GrowthConfig {
  std::map<int, ZoneDescription> zones;
  std::map<int, std::vector<DevRule>> rules;          // per zone
  DeveloperTuning res, com;
  IndustrialTuning ind;
  int landfillDecayRate = 490;
  const std::map<uint32_t, std::vector<ValveItem>>* valveEffects = nullptr;  // for committed demand
  int airportValve = kValveComConn, seaportValve = kValveIndConn;           // [OPEN] valves gating port developers
  bool cleanIndustryOnly = false;                                           // [OPEN] city-wide flag

  void parseZoneDescriptions(const IniFile& zoneLayerIni);  // SC3ZoneLayer.ini [ZoneDeveloperDescriptions]
  std::vector<RuleBinding> parseRuleBindings(const IniFile& zoneLayerIni);  // [ZoneDeveloperRules]
  void parseTune(const IniFile& tuneIni);                   // SC3Tune.ini: developer sections + [ZoneDeveloperRules] params
  DevRule& ruleFor(int zone, uint32_t ruleId);              // find or create
  void applyRuleParams(const IniFile& tuneIni);
};

// ---- city view ----
struct LotInfo {
  bool present = false;
  int ax = 0, az = 0;           // anchor tile
  uint32_t state = 0;           // lot state word (1,2,4,8,16 | 0x200)
  uint32_t agentType = 0;
  int sizeX = 1, sizeZ = 1;
  uint32_t flagWord = 0;        // attribute 0xCA
  bool removable = true;        // developer-replaceable
  bool kind5 = false;           // "special kind 5" building (spec 15 section 5.3 e)
  uint32_t baseState() const { return state & ~0x200u; }
  bool flagged() const { return (state & 0x200) != 0; }
  bool filler() const { return (flagWord & 0x100) != 0; }
  int tiles() const { return sizeX * sizeZ; }
};
struct ValveReading { int V = 0, Max = 2000, Min = -2000; };
struct Rect { int x0 = 0, z0 = 0, x1 = -1, z1 = -1; int w() const { return x1 - x0 + 1; } int h() const { return z1 - z0 + 1; } };

class GrowthCity {
 public:
  virtual ~GrowthCity() = default;
  virtual int mapW() const = 0;
  virtual int mapH() const = 0;
  virtual int zoneAt(int x, int z) const = 0;
  virtual bool powered(int x, int z) const = 0;
  virtual bool watered(int x, int z) const = 0;
  virtual int landValue(int x, int z) const = 0;                 // 1..255
  virtual bool hasAccess(int x, int z, int maxDist) const = 0;   // transport tile within maxDist
  virtual bool isTransport(int x, int z) const = 0;              // tile carries a transport network piece
  virtual LotInfo lotAt(int x, int z) const = 0;                 // lot covering the tile (present=false when empty)
  virtual ValveReading valve(int id) const = 0;
  virtual bool radioactive(int, int) const { return false; }
  virtual bool tripFailed(int, int) const { return false; }
  virtual int airPollution(int, int) const { return 0; }
  virtual int waterPollution(int, int) const { return 0; }
  virtual bool ordinanceEnacted(uint32_t) const { return false; }
  virtual int year() const { return 1900; }
  virtual int month() const { return 1; }
  virtual int eq() const { return 100; }
  virtual bool tileAvailable(int, int) const { return true; }    // not used by anything that blocks development
  virtual bool canPlace(const BuildingChoice&, int, int) const { return true; }
  // Actions
  virtual void placeBuilding(const BuildingChoice& b, int x, int z) = 0;  // new lot in state 4; manager already removed overlaps
  virtual void removeBuilding(const LotInfo& lot) = 0;
  virtual void setLotState(const LotInfo& lot, uint32_t newState) = 0;
  virtual int layRunway(const Rect&, int /*side*/) { return 0; }  // piece sequence [OPEN]
  // Landfill support (spec 15 section 7)
  virtual int landfillGarbage(int, int) const { return 0; }
  virtual void setLandfillGarbage(int, int, int) {}
  virtual void placeLandfillPiece(int, int) {}
  virtual void clearLandfillTile(int, int) {}
  virtual void creditGarbageCapacity(int) {}
  virtual int takePendingLandfillGarbage() { return 0; }
  virtual void returnLandfillGarbage(int) {}
  virtual void landfillTilesChanged(const std::vector<std::pair<int, int>>&) {}
};

// Developer result codes (spec 15 section 4.3).
enum ResultCode {
  kNoResult = 0, kAbRadioactive = 1, kAbLowLV = 2, kAbNegDemand = 3, kAbNoPower = 4, kAbNoWater = 5, kAbNoAccess = 6, kAbFarmPollution = 7,
  kNdRadioactive = 10, kNdLVBelow = 11, kNdLVAbove = 12, kNdNegDemand = 13, kNdNoPower = 14, kNdNoCategory = 15, kNdNoAccess = 16, kNdNoCategoryWatered = 18
};

// Category selection (section 4.4). Return index into zd.cats or -1.
int pickCategoryE(const ZoneDescription& zd, int lv, bool watered, char setCls = 0);
int pickCategoryH(const ZoneDescription& zd, int cur, int lv, bool watered);
int pickCategoryD(const ZoneDescription& zd, int cur, int lv);
int findCategory(const ZoneDescription& zd, uint32_t agentType);

// Lot finding (section 5.3 step 1). `replacedAgent` = agent type of the building being replaced (0 for none).
Rect findLot(const GrowthCity& c, int x, int z, int minLV, uint32_t replacedAgent, int maxDist, bool skipPower);

// Landfill developer state (section 7).
class LandfillDeveloper {
 public:
  int decayRate = 490;
  std::vector<std::pair<int, int>> active;  // tiles holding garbage, in fill order
  void visit(GrowthCity& c, int x, int z);
};

// Zone manager: scan order, pass statistics, committed demand, developers. Record 206C6E7C:409FF3BA:*.
class ZoneManager {
 public:
  struct ScanState {
    int8_t state = 1; uint32_t mode = 0; uint8_t listValid = 1;
    int32_t x0 = 0, x1 = -1, z0 = 0, z1 = -1, count = 0;
    std::vector<uint16_t> list;  // logical order (the file stores it reversed)
    int32_t index = 0, stridePos = 0, strideStep = 1;
  };
  using ResultMap = std::map<std::pair<uint8_t, uint8_t>, uint32_t>;

  void init(int W, int H, CrtRand& crt);          // new-city state: shuffled list, index 1
  void setConfig(const GrowthConfig* cfg) { cfg_ = cfg; landfill.decayRate = cfg ? cfg->landfillDecayRate : 490; }

  // One daily tick: K = W*H/30 visits (section 4 "Rate").
  void tick(GrowthCity& c, SimRand& rng, CrtRand& crt);
  // One visit of the cursor tile; returns the result code (0 = none). Advances the scan.
  int step(GrowthCity& c, SimRand& rng, CrtRand& crt);
  int visitsPerTick() const { return int((int64_t(W) * H) / 30); }

  // Committed demand P (section 4). Call when a building enters/leaves the city outside the manager.
  void buildingAdded(uint32_t agentType, int sizeX, int sizeZ, uint32_t state) { adjustCommit(agentType, sizeX, sizeZ, state, +1); }
  void buildingRemoved(uint32_t agentType, int sizeX, int sizeZ, uint32_t state) { adjustCommit(agentType, sizeX, sizeZ, state, -1); }
  int32_t committed(int valve) const { auto it = commit.find(uint32_t(valve)); return it == commit.end() ? 0 : it->second; }

  // ---- state (public: persisted) ----
  int W = 0, H = 0;
  std::map<uint32_t, int32_t> commit;      // valveId -> amount
  std::array<uint32_t, 23> failCur{}, failPrev{}, zoneTileCount{};
  ResultMap resultPrev, resultCur;
  ScanState scan;
  uint32_t cursorX = 0, cursorZ = 0, okCur = 0, okPrev = 0, failCurTotal = 0, failPrevTotal = 0;
  Bytes trailing;                           // unparsed bytes after the record (kept verbatim)
  LandfillDeveloper landfill;
  // Industrial year curve (bytes, percent)
  int minClean = 0, maxClean = 100, curveYear = -1;

  void recountZones(const std::vector<uint8_t>& zoneGridTileMajor);
  // Instance 0 record. `zoneGrid` is tile-major (index x*H+z).
  bool parse(const uint8_t* p, size_t n, int W, int H, std::vector<uint8_t>* zoneGrid);
  Bytes write(const std::vector<uint8_t>& zoneGrid) const;
  // Instances z+1: developer class per zone type (4 bytes each). Returns (key, payload) pairs.
  std::vector<std::pair<ResKey, Bytes>> developerClassRecords() const;
  // Apply a stored class record: returns true when it matches the configured developer.
  bool acceptDeveloperClass(int zone, const uint8_t* p, size_t n) const;

  // Exposed for tests
  void shuffle(CrtRand& crt);
  int visitTile(GrowthCity& c, SimRand& rng, int x, int z);

 private:
  void takeNext();
  void rollover(CrtRand& crt);
  void record(int code, int zone);
  void adjustCommit(uint32_t agentType, int sx, int sz, uint32_t state, int sign);

  int visitRCI(GrowthCity& c, SimRand& rng, int x, int z, const ZoneDescription& zd, char fam);
  int visitPort(GrowthCity& c, SimRand& rng, int x, int z, const ZoneDescription& zd);
  struct IndPick { int idx = -1; bool agAllowed = false; };
  IndPick pickIndustrial(GrowthCity& c, SimRand& rng, int x, int z, const ZoneDescription& zd, int lv, bool watered, bool allowAg);
  void refreshCleanCurve(int year);
  bool startDevelopment(GrowthCity& c, SimRand& rng, int x, int z, const ZoneDescription& zd, int cat, uint32_t replacedAgent, int V, int P, int Max, bool skipPower);
  int runRule(GrowthCity& c, SimRand& rng, const DevRule& rule, const Rect& lot, const Category& cat, int V, int P, int Max, int maxDist);
  int place(GrowthCity& c, const BuildingChoice& b, int x, int z);
  void removeLot(GrowthCity& c, const LotInfo& lot);

  const GrowthConfig* cfg_ = nullptr;
};

}  // namespace growth
}  // namespace m3k
