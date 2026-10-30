// Scenario goal scripts: compiled text form, command table and interpreter. spec/23-scenarios.md
// sections 3-4. City statistics and side effects go through abstract interfaces so the interpreter
// can run against fake data.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "core/ini.h"

namespace m3k {

// ---- command table (spec 23 section 3.3) --------------------------------------------------
enum Cmd : int {
  cmdAdd = 0, cmdSubtract, cmdAndCondition, cmdAssign, cmdGetCrimeValue, cmdGetLandValue,
  cmdGetPollutionValue, cmdGetPopulation, cmdComputePercentage, cmdDecrement, cmdDivide, cmdEndAnd,
  cmdEndOr, cmdEqual, cmdGreaterThan, cmdIf, cmdIfElse, cmdIncrement, cmdMultiply, cmdOrCondition,
  cmdRepeatRegular, cmdShowNewsticker, cmdStartFire, cmdStartRiot, cmdStartTornado, cmdSubRoutine,
  cmdCountUnpoweredBuildings, cmdCountUnwateredBuildings, cmdGetTrafficDensity, cmdGetFundsAvailable,
  cmdGetMonthsElapsed, cmdEndScenario, cmdNotEqual, cmdCountStructures, cmdLessThan, cmdAddGoal,
  cmdMakeDialogText, cmdMarkGoalStatus, cmdAllGoalsMet, cmdGoalStatusIs, cmdPopUpMsg, cmdAddRankInfo,
  cmdSetResultsDlgText, cmdBeginBlock, cmdEndBlock, cmdGenerateRandomNum, cmdStartEarthquake,
  cmdStartUFOAttack, cmdGetAura, cmdDisableNewNeighborDeals, cmdEnableNewNeighborDeals,
  cmdGetSurplusPower, cmdGetSurplusWater, cmdOfferRewardBuilding, cmdGetCurrentDate, cmdSetVariable,
  cmdCountStructuresNearLocation, cmdGetTotalDebt, cmdCountStructuresWithAppearanceNearLocation,
  cmdCountStructuresWithAppearanceInCity, cmdMoveCamera, cmdEnableDisableDisasters, cmdPlaySound,
  cmdSetScenarioSpeed, cmdStartToxicCloud, cmdStartWhirlpool, cmdStartSpaceJunk, cmdStartLocusts,
  cmdIsOrdinanceEnacted, cmdOfferBusinessDeal, cmdIsBusinessDealActive, cmdEnableDisableBusinessDeal,
  cmdGetRCItaxes, cmdIsNeighborConnection, cmdIsNeighborDealPresent,
  kCmdCount
};

const char* commandName(int id);           // shipped mapping; nullptr if unknown
int commandIdByName(const std::string& n);  // -1 if unknown
// Number of arguments the shipped scripts give each command (spec 23 section 4.6); -1 if unknown id.
int commandArgc(int id);

// Mapping read from SC3ScenarioLayer.ini [CommandNameToIdMapping] (name=id). Falls back to the
// shipped mapping when the section is absent.
class CommandMap {
 public:
  CommandMap();                                 // shipped mapping
  explicit CommandMap(const IniFile& ini);      // user data mapping (name -> game id)
  // Translates a file command ID to the engine's Cmd enum (identity for the shipped mapping), -1 if unknown.
  int toCmd(int fileId) const;
 private:
  std::map<int, int> idToCmd_;
};

// ---- script text form (spec 23 section 3.1) -----------------------------------------------
struct ScriptArg { uint8_t kind = 0; int32_t value = 0; };  // kind 0 literal, 1 variable
struct ScriptCommand {
  uint32_t delay = 0;
  uint16_t cmd = 0;
  std::vector<ScriptArg> args;
};

struct Script {
  std::vector<ScriptCommand> cmds;
  // Parse the record text ("; Script Code" line, count, command blocks). Returns false on malformed
  // input (cmds then hold the commands parsed so far).
  static bool parse(const std::string& text, Script& out, std::string* err = nullptr);
  // Emit exactly the layout the game reads (CR line ends).
  std::string serialize() const;
  bool operator==(const Script& o) const;
  // Timeline scripts need non-decreasing delays (spec 23 section 4.3 step 3).
  bool delaysNonDecreasing() const;
  // True when every command has the argc of commandArgc() (identity command map assumed).
  bool argcConforms() const;
};

// "Leading decimal integer" parse of the original: skips spaces, optional '-', digits; stops at first
// non-digit ("2.5" -> 2). `pos` advances past the number; false if no digits.
bool parseLeadingInt(const std::string& s, size_t& pos, int64_t& out);

// ---- external interfaces ------------------------------------------------------------------
struct GameDate { int day = 1, month = 1, year = 2000; };

// Live city statistics (spec 23 section 4.6.1). Values are raw; the interpreter applies width and
// sign conversions (crime u8, aura s8, traffic u8, RCI tax u8).
class ICityStats {
 public:
  virtual ~ICityStats() = default;
  virtual int32_t population() const = 0;
  virtual int32_t landValue() const = 0;
  virtual int32_t crime() const = 0;
  virtual int32_t aura() const = 0;
  virtual int32_t pollution(int kind) const = 0;  // 0 air, 1 water, 2 garbage
  virtual int32_t funds() const = 0;
  virtual int32_t totalDebt() const = 0;
  virtual int32_t taxRate(int category) const = 0;  // 0 R, 1 C, 2 I
  virtual int32_t trafficDensity() const = 0;
  virtual int32_t surplusPower() const = 0;  // raw (interpreter clamps at 0)
  virtual int32_t surplusWater() const = 0;
  virtual int32_t unpoweredBuildings() const = 0;
  virtual int32_t unwateredBuildings() const = 0;
  virtual GameDate date() const = 0;
  virtual void mapSize(int& w, int& h) const = 0;
  virtual int32_t countStructures(uint32_t occupantId) const = 0;
  virtual int32_t countStructuresNear(uint32_t occupantId, int x, int z, int radius) const = 0;
  virtual int32_t countAppearance(uint32_t a1, uint32_t a2, uint32_t a3) const = 0;
  virtual int32_t countAppearanceNear(uint32_t a1, uint32_t a2, uint32_t a3, int x, int z, int radius) const = 0;
  virtual bool ordinanceEnacted(int id) const = 0;
  virtual bool neighborConnection(int neighbor, int kind) const = 0;
  virtual bool neighborDealPresent(int neighbor, int dealKind) const = 0;
  virtual bool businessDealActive(int dealId) const = 0;
};

struct PopUpRequest {
  uint32_t group = 0, id = 0;
  int32_t important = 0, presenter = 0, a4 = 0, mood = 0;
};

enum class DisasterKind { Fire, Riot, Earthquake, Locusts, UFO, Whirlpool, SpaceJunk, ToxicCloud, Tornado };

// Side effects. Defaults do nothing and report success.
class IScenarioHost {
 public:
  virtual ~IScenarioHost() = default;
  virtual void popUp(const PopUpRequest&) {}
  virtual void newsTicker(uint32_t /*group*/, uint32_t /*id*/) {}
  virtual void playSound(int32_t) {}
  virtual bool moveCamera(int /*x*/, int /*z*/, int /*zoom*/) { return true; }
  virtual void setSpeed(int /*speed*/) {}
  virtual void startDisaster(DisasterKind, const std::vector<int32_t>& /*params*/) {}
  virtual void setDisastersEnabled(bool) {}
  virtual void setNeighborDealsBlocked(int /*dealType*/, bool /*blocked*/) {}  // -1 = all
  virtual void offerRewardBuilding(uint32_t) {}
  virtual void offerBusinessDeal(int) {}
  virtual void setBusinessDealEnabled(int, bool) {}
  virtual void endScenario(int /*rank*/) {}
  // Scenario became active (spec 23 4.2 step 3): tell the simulation, show the start dialog.
  // Called after the first evaluation pass; `resumed` = restored from a saved state record.
  virtual void scenarioStarted(bool /*resumed*/) {}
};

// ---- run-time state (spec 23 section 4.1, 8) ----------------------------------------------
struct StringRef { uint32_t group = 0, id = 0; bool operator==(const StringRef& o) const { return group == o.group && id == o.id; } };
struct ScriptRef { uint32_t type = 0x23DFAE5F, group = 0x23DFAE8C, instance = 0; bool operator==(const ScriptRef& o) const { return type == o.type && group == o.group && instance == o.instance; } };
struct Goal { StringRef text; bool met = false; };

constexpr int kScenarioVars = 512;

struct ScenarioState {
  std::vector<int32_t> vars = std::vector<int32_t>(kScenarioVars, 0);
  uint32_t clock = 0;  // scenario minutes
  uint32_t startMonth = 1, startYear = 2000;
  std::vector<ScriptRef> repeat[4];
  bool hasTimeline = false;
  int32_t cursor = -1;  // index of last executed timeline command
  ScriptRef timelineScript;
  uint32_t presenter = 0, presenterMood = 0, presenterKind = 0;
  std::map<uint32_t, Goal> goals;
  std::map<uint32_t, StringRef> ranks;
  int32_t resultRank = -1;
  StringRef dialogText, resultsText;
  std::vector<StringRef> dialogExtra, resultsExtra;  // slot-2 paragraphs (not persisted)
  bool speedLocked = false;
  bool active = false;
};

// Scenario RNG: the game's integer generator (spec 14 section 8).
struct ScenarioRng {
  uint32_t s = 1;
  explicit ScenarioRng(uint32_t seed = 1) : s(seed) {}
  uint32_t next() { uint64_t v = uint64_t(s) * 1103515245u + 12345u; s = uint32_t(v); return uint32_t(v >> 16); }
  uint32_t rand(uint32_t n);  // [0,n-1] (n==0 -> 0; n >= 0xFFFFFF: rejection, inclusive upper bound)
};

// Interpreter. Scripts are looked up through `lookup` (script instance -> Script*).
class ScriptInterpreter {
 public:
  using Lookup = const Script* (*)(void* user, uint32_t instance);
  ScriptInterpreter(ScenarioState& st, ICityStats& stats, IScenarioHost& host, ScenarioRng& rng,
                    Lookup lookup, void* user, const CommandMap* map = nullptr);

  // Run a whole script to completion; returns the value of the last evaluated command.
  int32_t run(const Script& s, int depth = 0);
  // Evaluate the command at `pos` with its owned sub-commands; advances pos. `exec` = side effects.
  int32_t evalNext(const Script& s, size_t& pos, bool exec, int depth = 0);

 private:
  int32_t argv(const ScriptCommand& c, size_t i) const;       // resolved
  int32_t raw(const ScriptCommand& c, size_t i) const;        // literal value as written
  bool dest(const ScriptCommand& c, int& v) const;            // destination variable (arg 0)
  void setVar(int v, int32_t x) { st_.vars[size_t(v)] = x; }
  int32_t execLeaf(const ScriptCommand& c, int cmd, int depth);
  ScenarioState& st_;
  ICityStats& stats_;
  IScenarioHost& host_;
  ScenarioRng& rng_;
  Lookup lookup_;
  void* user_;
  const CommandMap* map_;
};

}  // namespace m3k
