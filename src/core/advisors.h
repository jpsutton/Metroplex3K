// Advisors (spec/22 section 3, 6 and persisted records P.1, P.3, P.4). The seven advisors evaluate once per
// game week from an explicit AdvisorInputs snapshot; topics live in per-advisor TopicLists (cool-down state
// machine of 3.4); petition requests are made on a PetitionerManager.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/font.h"
#include "core/gamerng.h"
#include "core/ini.h"
#include "core/news.h"
#include "core/petitioners.h"

namespace m3k {

// Advisor IDs (3.1)
constexpr uint32_t kAdvBudget = 0x628D0C45, kAdvPlanner = 0xC28D0B6E, kAdvDemographics = 0xC28D0F40,
                   kAdvEnvironment = 0x828D04EB, kAdvSafety = 0x828D0A2F, kAdvTransport = 0x028D0FC5,
                   kAdvUtilities = 0x022E288E;
const std::vector<uint32_t>& allAdvisorIds();   // evaluation order
const char* advisorIniName(uint32_t advisorId);  // "Advisor-Budget.ini" ...

constexpr uint32_t kTopicRecType = 0x013DEE82, kTopicRecInstance = 0x0000000B;     // group = advisor id
constexpr uint32_t kRewardRecType = 0x206C6E7C, kRewardRecGroup = 0xC28D0B6E;      // P.3 instance 0
constexpr uint32_t kAdvMgrRecType = 0x022E288E, kAdvMgrRecGroup = 0x422E28E8;      // P.4 instance 0

// ---- Moods / pictures ---------------------------------------------------------------------------
class MoodTable {
 public:
  // AdvisorMoods.ini [AdvisorMoods] key 0x%08X = 1..5; `gold` consulted when absent from base.
  void load(const IniFile& base, const IniFile* gold = nullptr);
  int mood(uint32_t topic) const;               // 0 if unknown
  void set(uint32_t topic, int m) { base_[topic] = m; }
 private:
  std::map<uint32_t, int> base_, gold_;
};
struct TopicGfxTable {
  void load(const IniFile& base, const IniFile* gold = nullptr);   // [AdvisorTopicGfxIDs] topic = 0xGROUP,0xINSTANCE
  bool get(uint32_t topic, uint32_t& group, uint32_t& instance) const;
  std::map<uint32_t, std::pair<uint32_t, uint32_t>> v;
};
// Token substitution of 2.1 plus per-instance argument tokens (args substituted by token name).
std::string topicText(const std::string& raw, const TextContext& ctx, const std::vector<TopicArg>& args);

// ---- Topic instances (3.4) ---------------------------------------------------------------------
struct TopicInstance {
  uint32_t topicId = 0, tickerItem = 0, groupKey = 0;
  uint8_t sticky = 0, reserved = 0, limit = 0, counter = 0, flags = 0;
  std::vector<TopicArg> args;
  bool hidden() const { return flags & kFlagHidden; }
  bool persistent() const { return flags & kFlagPersistent; }
  bool expiring() const { return flags & kFlagExpiring; }
  bool raised() const { return flags & kFlagRaised; }
};

class TopicList {
 public:
  explicit TopicList(uint32_t advisorId = 0) : advisorId_(advisorId) {}
  uint32_t advisorId() const { return advisorId_; }
  void setSink(TickerSink* s) { sink_ = s; }

  // raise(topic, K, L, P, E, S) with optional map location (makes the ticker line clickable).
  void raise(uint32_t topic, uint32_t key = 0, int limit = 0, bool persistent = false, bool expiring = false,
             bool sticky = false, bool hasLoc = false, int x = 0, int z = 0);
  void event(uint32_t topic, int limit, uint32_t key = 0) { raise(topic, key, limit, true, true); }
  void persistentTopic(uint32_t topic, int limit) { raise(topic, 0, limit, true); }
  void update();                                   // end-of-cycle update; clears R
  bool dismiss(uint32_t topic);                    // player dismissal (3.4)
  bool listed(uint32_t topic) const { return find(topic) != nullptr; }
  bool visible(uint32_t topic) const { auto* t = find(topic); return t && !t->hidden(); }
  const TopicInstance* find(uint32_t topic) const;
  void remove(uint32_t topic);
  void clear();
  const std::vector<TopicInstance>& topics() const { return v_; }
  std::vector<uint32_t> visibleTopics() const;
  // Integer mean of the moods of visible topics; 3 if none (3.3).
  int mood(const MoodTable& m) const;
  // Forced topics (cheats / events): raised as raise(topic) after the update.
  std::vector<uint32_t> forced;
  std::vector<uint32_t> queued;                    // one-shot forced topics (ordinance announcements, spec 22 3.6); raised at the next evaluation
  void raiseForced() { for (uint32_t t : forced) raise(t); for (uint32_t t : queued) raise(t); queued.clear(); }

  // P.1 record (Group = advisor id)
  Bytes save() const;
  bool load(const Bytes& b);

 private:
  uint32_t advisorId_;
  TickerSink* sink_ = nullptr;
  std::vector<TopicInstance> v_;
  uint32_t postTicker(const TopicInstance& t);
  void drop(TopicInstance& t);
};

// ---- Thresholds (Advisor-*.ini) -------------------------------------------------------------
class AdvisorThresholds {
 public:
  AdvisorThresholds();                              // [OPEN] defaults where the spec gives none
  int64_t get(const std::string& key) const;
  void set(const std::string& key, int64_t v) { v_[key] = v; }
  // Reads every key of the listed sections (integers; "60;" reads as 60).
  void loadSections(const IniFile& f, const std::vector<std::string>& sections);
  // Loads the advisor's INI with its default section list.
  void loadAdvisorIni(uint32_t advisorId, const IniFile& f);
  struct Announcement { int buildingId = 0, year = 0; uint32_t topic = 0; };
  // [POWER_BUILDINGS] / [WATER_BUILDINGS] / [GARBAGE_BUILDINGS] / [TRANSPORTATION_ADVANCEMENTS]: ID=Name;year;topic
  std::vector<Announcement> power, water, garbage;
  void loadBuildingLists(const IniFile& f);
 private:
  std::map<std::string, int64_t> v_;
};

// ---- Inputs ----------------------------------------------------------------------------------
struct BusinessDealOffer { uint32_t petition = 0; int limit = 6; };   // computed by the economy (19 section 11)

struct AdvisorInputs {
  int64_t population = 0;
  int year = 2000;
  int aura = 0;                  // signed city aura / mayor rating
  int dreary = 0;                // dreary rating
  int mapWidth = 0, mapHeight = 0;
  int neighbourConnections = 0;  // neighbour connection count (0..4)

  struct Budget {
    int taxRate[3] = {7, 7, 7};              // R, C, I percent
    double taxEffect[3] = {0, 0, 0};         // tax-rate growth factor x_z
    int64_t treasury = 0, estIncome = 0, estExpense = 0;
    bool anyNeighbourDealActive = false, trashImportDealKind5Exists = false, hasBuilding302A = false;
    std::vector<BusinessDealOffer> dealOffers;                  // low-treasury business-deal offers
    std::vector<uint32_t> businessFollowUps;                    // petitions 0x11/0x12/0xD5 whose business exists
  } budget;

  struct Environment {
    int waterPollution = 0, airPollution = 0;
    bool hasWaterSystem = false;
    bool garbageAccumulating = false, garbageNearCapacity = false, garbageSurplusWithinLimit = true;
    int64_t garbageDemand = 0, garbageCapacity = 0;   // GD, GC of spec 22 "Garbage metrics"
    bool trashExportDealActive = false, trashImportDealExists = false, anyTrashDeal = false;
    bool hasSeaport = false;
  } env;

  struct Safety {
    int policeFunding = 100, fireFunding = 100;     // percent of recommended
    int crime = 0;
    int policeStations = 0, fireStations = 0, jails = 0;
    bool policeStrike = false, fireStrike = false;
    int policeWarn = 0, fireWarn = 0;               // strike-warning counters
    int jailInmates = 0, jailCapacity = 0, jailCapacityTerm = 1;
    int policeCoverage = 100;                       // percent
    int fireCoverage = 100;                         // percent (fire layer getter +0x6C)
    int jailUnjailed = 0;                           // prisoners without a cell (police getter +0x78, "jail full") [INFERRED]
    int policeStrength = 0;                         // "police strength measure"
    bool curfewEnacted = false, youthSportsEnacted = false;
    bool toxicImminent = false, debrisActive = false;   // Ra / Rb flags (spec 22 3.9, SIMDSTR)
  } safety;

  struct Transport {
    int roadFunding = 100, transitFunding = 100;
    int nRoad = 0, nTransit = 0;                    // road+highway tiles, rail+subway tiles
    double roadDamage = 0, failedCommutes = 0;
    int roadTraffic = 0, railUsage = 0;
    int nBus = 0, nStations = 0, n12230 = 0, n12360 = 0, n12361 = 0;
    int railTiles = 0, subwayTiles = 0;
    int airportZoneTiles = 0, airportFailTiles = 0, airportBuildings = 0;
    bool transitStrike = false; int transitWarn = 0;
    bool hasSubway = false, hasBusStations = false, hasAirport = false, hasHighway = false;
    bool carpoolEnacted = false, crossingGuardsEnacted = false, altDayAvailable = false;
  } trans;

  struct Planner {
    int demand[3] = {0, 0, 0};                     // R, C, I valves (3000/5000/8000)
    int zoned[3] = {0, 0, 0};
    int zSea = 0, zAir = 0, zFill = 0;
    int x[3][20] = {};                             // developer result-code counts per family
    int airportCount = 0, seaportCount = 0, farms = 0, cleanIndustry = 0, medComm = 0, highComm = 0, industrialBuildings = 0;
    int landValue = 0, airPollution = 0, waterPollution = 0;
    int capC = 0, capI = 0;
    bool capFlagA = false, capFlagB = false;
    int K = 0, Kr = 0, Kt = 0;                     // neighbour sides: any / road / rail
    bool landfillFull = false, landfillNoFreeCapacity = false;
    int seaportBadTiles = 0, landfillNoAccessTiles = 0;
    int eq = 0, lifeExpectancy = 0;
    int n14311 = 0, n14312 = 0, n14313 = 0, n14314 = 0, n14315 = 0, n14316 = 0, n14310 = 0;
    int64_t totalBuildings = 0;                    // B
    bool farmersMarketAvailable = false, proReadingEnacted = false, tourismEnacted = false;
    int64_t treasury = 0;
    bool holidaysEnabled = false; int month = 1;   // calendar month 1..12 (rewards 20001 / 20002)
    // Weighted park measure of the rewards 14003 and 20002 and the planner's park ratio (14310 itself is not part of it) [STATIC, spec 22 6.2].
    int64_t parkMeasure() const { return int64_t(n14311) + 9 * n14312 + 16 * n14313 + 4 * n14314 + 4 * n14315 + n14316; }
  } plan;

  struct Demographics {
    double lifeExpectancy = 0; int eq = 0;
    int nHospital = 0, nSchool = 0, nCollege = 0, nStadium = 0, nZoo = 0, nMarina = 0;
    bool healthStrike = false, schoolStrike = false, collegeStrike = false;
    int warnHealth = 0, warnSchool = 0, warnCollege = 0;
    int fundClassHealth = 0, fundClassSchool = 0, fundClassCollege = 0;   // service ratings 0..4 (spec 22 s3.12; not funding)
    // warn* = strike risk / 100 (spec 20 s5.4), not a months counter
    int64_t demandHealth = 0, capHealth = 0, demandSchool = 0, capSchool = 0, demandCollege = 0, capCollege = 0;
    int healthFunding = 100, eduFunding = 100;
    int64_t libraryCap = 0, museumCap = 0;
  } demo;

  struct Utilities {
    int64_t powerUsed = 0, powerCap = 0, powerDemand = 0;
    int unpoweredBuildings = 0, buildingsNeedingPower = 0;
    int64_t waterUsed = 0, waterCap = 0;
    int dryBuildings = 0, buildingsNeedingWater = 0;
    int neighbours = 0;                            // Nb
    bool dealSellPower = false, dealBuyPower = false, dealSellWater = false, dealBuyWater = false;
    bool powerLineConnection = false, pipeConnection = false;
    int nPlants = 0, nOldPlants = 0, nNuclearEnding = 0, nPumps = 0, nOldPumps = 0, nIncin = 0, nOldIncin = 0;
    bool desalinationAvailable = false;
    int64_t garbageCap = 0, garbageProduced = 0, treasury = 0;
  } util;
};

// Neighbour deal manager hook for utilities offers (3.13.1). Types: 0 buy power, 1 sell power, 2 buy water,
// 3 sell water, 4 export garbage, 5 import garbage.
class DealHandler {
 public:
  virtual ~DealHandler() = default;
  virtual bool dealActive(int type) = 0;
  virtual int dealAgeMonths(int type) = 0;
  virtual bool eligibleNeighbour(int type) = 0;
  virtual bool offersDisabled() = 0;
  virtual bool offerStillValid(int type) = 0;
  virtual bool generateOffer(int type) = 0;       // true if a valid offer was produced
  virtual void clearOffer(int type) = 0;
};

// ---- Rewards (6) ------------------------------------------------------------------------------
struct RewardEntry {
  int buildingType = 0;
  std::string name;
  int minPopulation = 0, minYear = 0;
  int petition = 0;
  int special = 0;
  bool isDeal = false;
  bool available = false, owned = false;
};
class RewardTable {
 public:
  // [REWARD_BUILDINGS], [BUSINESS_DEAL_BUILDINGS] ("type=name;pop;year;petition;special") and [REWARD_BUILDINGS_2].
  // knownTypes: if non-empty, entries whose building type is not in it are dropped.
  void load(const IniFile& cityPlanner, const std::set<int>& knownTypes = {});
  std::vector<RewardEntry>& entries() { return e_; }
  const std::vector<RewardEntry>& entries() const { return e_; }
  RewardEntry* find(int buildingType);
  void markAvailable(int buildingType);
  void markOwned(int buildingType, bool v = true);
  int64_t param(const std::string& k) const { auto i = p_.find(k); return i == p_.end() ? 0 : i->second; }
  void setParam(const std::string& k, int64_t v) { p_[k] = v; }
  // Offer rule (6.2); runs after the planner update.
  void evaluate(const AdvisorInputs& in, PetitionerManager& pm);
  // Persisted P.3 (spec 22 P.3). `typeToInstance` maps a building (agent) type to the Instance of its occupant record, key
  // 207EDC0E:0000057E:<instance>: the lowest occupant ID with that agent type [DATA, shipped cities]. Entries of unknown types
  // are preserved verbatim in `rawEntries`.
  struct Raw { uint32_t type = 0, group = 0, instance = 0; uint8_t owned = 0, available = 0; };
  std::map<int, uint32_t> typeToInstance = {
      {1002, 0x1F51},  {10110, 0xF3},   {10120, 0xD0},   {11001, 0x1F57}, {11002, 0x1F8B}, {12350, 0x1F52}, {13005, 0x1F4D}, {13871, 0x1F98},
      {13872, 0x1F56}, {13874, 0x1F6A}, {14003, 0x1F49}, {14150, 0xDB},   {14170, 0x1F4B}, {14325, 0x1F5D}, {14370, 0x1F6E}, {14550, 0x1F53},
      {14600, 0x1F54}, {19001, 0x1F55}, {19002, 0x1F95}, {19003, 0x1F5B}, {19004, 0x1F48}, {19999, 0x4E20}, {20001, 0x61A8}, {20002, 0x2D18}};
  std::vector<Raw> rawEntries;
  Bytes save() const;
  bool load(const Bytes& b);
 private:
  std::vector<RewardEntry> e_;
  std::map<std::string, int64_t> p_;
};

// ---- Calendar cadence (3.6) ------------------------------------------------------------------
struct CycleClock {
  bool needsUpdate = false;
  void weekNotification() { needsUpdate = true; }
  bool dayNotification() { bool r = needsUpdate; needsUpdate = false; return r; }   // true -> evaluate now
};

struct AdvisorOutputs {
  bool gameLost = false;                          // budget advisor's debt rule
  std::vector<uint32_t> advisorsUpdated;          // "advisor updated" broadcasts
};

class AdvisorManager {
 public:
  AdvisorManager();
  AdvisorThresholds thr;
  MoodTable moods;
  TopicGfxTable gfx;
  RewardTable rewards;
  PetitionerManager* petitioners = nullptr;       // petition requests go here
  DealHandler* deals = nullptr;
  GameRng utilRng{1};                             // utilities advisor's private generator [OPEN seed]
  CycleClock clock;
  uint8_t mgrFlags[6] = {1, 1, 1, 1, 1, 1};       // P.4

  void setSink(TickerSink* s);
  TopicList& list(uint32_t advisorId);
  const TopicList& list(uint32_t advisorId) const;

  // Calendar listeners: Monday sets the flag, the next day notification runs one evaluation of everything.
  void calendarWeek() { clock.weekNotification(); }
  bool calendarDay(const AdvisorInputs& in, GameRng& rng, AdvisorOutputs& out);
  // One full evaluation of every advisor (rules, update, post-update rules, forced topics, broadcast).
  void evaluateAll(const AdvisorInputs& in, GameRng& rng, AdvisorOutputs& out);
  void evaluate(uint32_t advisorId, const AdvisorInputs& in, GameRng& rng, AdvisorOutputs& out);
  // On-demand pass: gather + rules only (no update step).
  void rulesOnly(uint32_t advisorId, const AdvisorInputs& in, GameRng& rng);
  // Ordinance announcement (spec 22 3.6 [STATIC]): sent when an ordinance BECOMES AVAILABLE (not on enact / repeal); the owning advisor queues a forced
  // topic (a = b = 0) that is raised at its next evaluation.
  void ordinanceAvailable(uint32_t ordinanceId);
  int mood(uint32_t advisorId) const { return list(advisorId).mood(moods); }

  // Persistence: P.1 per advisor, P.3, P.4
  struct Record { uint32_t type, group, instance; Bytes data; };
  std::vector<Record> saveRecords() const;
  bool loadRecord(uint32_t type, uint32_t group, uint32_t instance, const Bytes& data);
  void clear();   // new city: empty topics, rewards neither owned nor available

  // Building availability (spec 22 "Building availability") [STATIC]: the [POWER_BUILDINGS] / [WATER_BUILDINGS] /
  // [GARBAGE_BUILDINGS] (Advisor-Environment.ini) entries
  // each carry an unlock flag (not saved). At load every entry with year <= the current year is unlocked; each month,
  // with rand(100) < 10, the first locked entry with year <= the current year is unlocked (power list first) and its
  // topic is announced by the utilities advisor. Types in no list are not gated.
  void initBuildingUnlocks(int year);
  void monthlyBuildingUnlock(int year, GameRng& rng);
  bool buildingUnlocked(int agentType) const;
  // The environment advisor's own availability query (spec 22 "Garbage metrics" C): a type absent from
  // [GARBAGE_BUILDINGS] counts as not available.
  bool garbageBuildingAvailable(int agentType) const;
  // Transportation tech flags (spec 22 s3.10, Q-AK) [STATIC]: 0 subway, 1 bus, 2 airport, 3 highway. At city setup and on
  // "city opened" flag = (year <= current year), no announcement; in play a flag is set only by the 10 % announcement.
  void initTransportFlags(int year);
  bool transportTech(int i) const { return i >= 0 && i < 4 && tAnnounced_[i]; }

 private:
  std::vector<TopicList> lists_;
  std::set<uint32_t> announced_;                  // announcement topics already raised (not saved)
  std::set<int> unlocked_;                        // unlocked power/water building types (not saved)
  std::vector<uint32_t> unlockTopics_;            // power/water unlocks waiting for the utilities advisor
  std::vector<uint32_t> unlockTopicsEnv_;         // garbage unlocks waiting for the environment advisor [INFERRED owner]
  // Transport announcement flags (not saved [OPEN])
  bool tAnnounced_[4] = {false, false, false, false};
  int garbageStreak_ = 0;
  std::vector<int> ring_[3];                      // planner demand-sample ring (12 entries)
  void budgetRules(TopicList&, const AdvisorInputs&, AdvisorOutputs&);
  void envRules(TopicList&, const AdvisorInputs&);
  void safetyRules(TopicList&, const AdvisorInputs&);
  void transRules(TopicList&, const AdvisorInputs&, GameRng&);
  void planRules(TopicList&, const AdvisorInputs&);
  void demoRules(TopicList&, const AdvisorInputs&, GameRng&);
  void utilRules(TopicList&, const AdvisorInputs&);
  void postUpdate(uint32_t advisorId, TopicList&, const AdvisorInputs&, GameRng&);
  void safetyPetitions(const TopicList&, const AdvisorInputs&, GameRng&);
  void transPetitions(const AdvisorInputs&, GameRng&);
  void planPetitions(const AdvisorInputs&, GameRng&);
  void demoPetitions(const AdvisorInputs&, GameRng&);
  void utilDeals(TopicList&, const AdvisorInputs&);
  void budgetPetitions(const AdvisorInputs&, GameRng&);
  int64_t accumulated(const AdvisorInputs&, int fam) const;
};

}  // namespace m3k
