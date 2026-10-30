// Scenario files (.SNR), header/tables, run-time state record, scenario runtime and .SSC templates.
// spec/23-scenarios.md.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/ixf.h"
#include "core/script.h"

namespace m3k {

// Resource keys (spec 23 section 2.1, 8).
constexpr ResKey kSnrHeaderKey{0x04845CBE, 0xC3DE4D66, 0};
constexpr uint32_t kSnrScriptType = 0x23DFAE5F, kSnrScriptGroup = 0x23DFAE8C;
constexpr ResKey kSnrStyleKey{0xA4846144, 0xA48461A3, 0};
constexpr ResKey kSnrPresenterKey{0x44EB6BC8, 0x44EB6BC9, 0};
constexpr ResKey kSnrCatalogueKey{0x26E732E0, 0x19A6CEA1, 0};
constexpr ResKey kSnrShortAuthorKey{0x2026960B, 0x19A6CEA1, 2};
constexpr ResKey kSnrPictureKey{0x62B9DA24, 0x19A6CEA1, 1};
constexpr uint32_t kSnrAuthorInstance = 0x01BBE888;
constexpr ResKey kSnrStateKey{0x206C6E7C, 0xC3DE4D66, 0};

// True for the key groups the game treats as scenario records (section 8).
bool isScenarioRecordKey(const ResKey& k);

// Standard advisor presenter IDs (section 4.6).
constexpr uint32_t kAdvisorUtilities = 0x022E288E, kAdvisorBudget = 0x628D0C45, kAdvisorCityPlanner = 0xC28D0B6E,
                   kAdvisorEnvironment = 0x828D04EB, kAdvisorPublicSafety = 0x828D0A2F,
                   kAdvisorTransportation = 0x028D0FC5, kAdvisorDemographics = 0xC28D0F40;
bool isKnownAdvisor(uint32_t id);
// Advisor index 1..7 (Utilities..Demographics), 0 if unknown.
int advisorIndex(uint32_t id);
// Portrait instance 62B9DA24:52DB60A0:I composed from advisor index, mood (0 -> 3), variant 0-2, important.
uint32_t advisorPortraitInstance(int advisorIdx, int mood, int variant, bool important);
// Lookup order when the portrait image is missing: requested, variant 0, then mood 3, then 0x10303.
std::vector<uint32_t> advisorPortraitCandidates(int advisorIdx, int mood, int variant, bool important);

struct ScenarioHeader {
  std::string signature = "SNR";
  std::string sourcePath;
  ScriptRef entry;            // line 3
  StringRef title;            // line 4 (string table, id)
  uint32_t presenter = kAdvisorUtilities;  // line 5 P
  uint32_t presenterKind = 1;              // line 5 K (1 advisor, 0 custom)
  uint32_t stringTable = 0;   // line 6
  static bool parse(const std::string& text, ScenarioHeader& out, std::string* err = nullptr);
  std::string serialize() const;
  // Effective default advisor: unknown advisor with kind 1 falls back to Utilities.
  uint32_t effectivePresenter() const;
};

struct TickerStyle {
  int cls = 1;  // stored as s-1 by the game; this holds the file value 1..7
  uint8_t r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0;
  bool underline = true;
  int repeats = 1;
};
struct StyleTable {
  std::map<uint32_t, TickerStyle> byString;  // key: string ID (group ignored)
  std::map<uint32_t, uint32_t> groups;       // string ID -> group as written (for round trip)
  static bool parse(const std::string& text, StyleTable& out, std::string* err = nullptr);
  std::string serialize() const;
  const TickerStyle* find(uint32_t stringId) const;
  // Style a cmdShowNewsticker item is posted with: the table entry, else the ticker defaults
  // (class 6, white, underline 0, repeats 1; spec 23 section 2.5).
  TickerStyle resolve(uint32_t stringId) const;
};

struct PresenterRow { int32_t id = 0, name = 0, title = 0; };
struct PresenterTable {
  std::vector<PresenterRow> rows;
  static bool parse(const std::string& text, PresenterTable& out);  // a failing row stops loading
  std::string serialize() const;
  const PresenterRow* find(int32_t id) const;
};
// Custom presenter picture key 62B9DA24:6504691D:P.
inline ResKey presenterPictureKey(uint32_t p) { return ResKey{0x62B9DA24, 0x6504691D, p}; }

struct Catalogue {  // 12-byte record
  uint32_t stringTable = 0, scenarioType = 0, difficulty = 0;
  static bool parse(const Bytes& b, Catalogue& out);
  Bytes serialize() const;
};

// Run-time state record 206C6E7C:C3DE4D66:0 (section 8).
Bytes serializeScenarioState(const ScenarioState& st);
bool parseScenarioState(const Bytes& b, ScenarioState& out);

// String record helpers (length from the payload prefix, never from the index).
Bytes makeStringRecord(const std::string& s);
bool readStringRecord(const IxfFile& f, const ResKey& k, std::string& out);

// Scenario token substitution (section 6.1): %SCNNAME%, %SCNV<M|N|Y> n%, plus %YEAR%/%POPULATION%/
// %MAYOR%/%CITYNAME%/%YOURNAME%/%YOURCITY% when supplied.
struct ScenarioTextContext {
  std::string scenarioName, mayor, city;
  std::string paradeName;        // %PARADENAME%: text for the player's country (empty -> token kept)
  std::string anyNeighbor;       // %ANYNEIGHBOR%: name of one neighbour city (empty -> token kept)
  int population = 0, year = 2000;
  const std::vector<int32_t>* vars = nullptr;
};
std::string expandScenarioTokens(const std::string& s, const ScenarioTextContext& c);

// Presenter shown with a pop-up or the start dialog (spec 23 sections 2.3, 2.6, 4.6).
struct ResolvedPresenter {
  bool advisor = true;           // kind 1
  uint32_t advisorId = 0;        // effective advisor ID (kind 1)
  int advisorIdx = 0;            // 1..7
  ResKey picture;                // custom presenter picture 62B9DA24:6504691D:P (kind 0)
  ResKey nameString, titleString;  // 2026960B:<table>:NAME / TITLE (kind 0, when the row exists)
  bool found = true;             // false: custom presenter with no table row
};

// What the scenario chooser shows (spec 23 section 2.7, 6).
struct ScenarioChooserInfo {
  uint32_t stringTable = 0;
  ResKey string0, string1;       // strings 0 and 1 of the scenario table (description / title)
  bool hasPicture = false;
  uint32_t scenarioType = 0, difficulty = 0;  // preserved, not read by the game
  std::string author;
};

// A loaded scenario: every scenario record of section 2.1.
struct ScenarioFile {
  ScenarioHeader header;
  std::map<uint32_t, Script> scripts;       // by instance
  StyleTable styles;
  PresenterTable presenters;
  Catalogue catalogue;
  bool hasCatalogue = false;
  std::string shortAuthor;                  // 2026960B:19A6CEA1:2
  std::string author;                       // 2026960B:<table>:01BBE888
  Bytes picture;                            // BMP, 62B9DA24:19A6CEA1:1
  std::map<uint32_t, std::string> strings;  // embedded strings of the scenario table (id -> text)
  bool hasState = false;
  ScenarioState state;                      // valid when hasState
  std::vector<std::string> warnings;

  // Read scenario records from an IXF file image (.SNR or a city saved mid-scenario). Returns false
  // if the header record is absent (the file is not a scenario) or malformed.
  static bool load(const Bytes& file, ScenarioFile& out, std::string* err = nullptr);
  // Add every scenario record (and state, if hasState) to a writer.
  void addRecords(IxfWriter& w) const;
  // Rebuild `cityFile` (an IXF) with scenario records replaced by this scenario's records.
  Bytes mergeInto(const Bytes& cityFile) const;
  // Records only (no city).
  Bytes buildStandalone() const;

  const Script* script(uint32_t instance) const;
  // Presenter for (id, kind) using the presenter table for custom presenters.
  ResolvedPresenter resolvePresenter(uint32_t id, uint32_t kind) const;
  ResolvedPresenter defaultPresenter() const { return resolvePresenter(header.presenter, header.presenterKind); }
  ScenarioChooserInfo chooserInfo() const;
  // Shipped-data shape checks: every script of the file is reachable by number, entry script exists.
  bool entryScriptPresent() const { return script(header.entry.instance) != nullptr; }
};

// Outcome of a scenario for the results dialog (spec 23 section 4.7).
enum class ResultsChoice { Reload = 0, OpenLoad = 1, LeaveAndKeepPlaying = 2 };
struct ScenarioResult {
  bool ended = false;            // cmdEndScenario ran
  int32_t rank = -1;             // result rank, -1 none
  StringRef rankTitle;           // from cmdAddRankInfo (zero when no rank)
  StringRef resultsText;         // cmdSetResultsDlgText slot 1
  std::vector<StringRef> resultsExtra;
  bool allGoalsMet = false;      // every defined goal is met (true when none)
  int goalsMet = 0, goalsTotal = 0;
};

// Runtime: owns state, interpreter and RNG. spec 23 section 4.2-4.4.
class ScenarioRuntime {
 public:
  ScenarioRuntime(const ScenarioFile& file, ICityStats& stats, IScenarioHost& host, uint32_t seed = 1,
                  const CommandMap* map = nullptr);
  // Fresh start (records start month/year, puts the entry script on the timeline) or resume from
  // file.state when present. Then runs the "simulator started" evaluation.
  void start();
  // Once per simulated minute, only while a disaster instance is active.
  void minuteTick();
  // Once per day tick.
  void dayTick();
  // Evaluation pass (section 4.3 steps 1-3); called by the tick functions.
  void evaluate();

  // Win/lose view for the results dialog; the scripts decide, there is no implicit test.
  ScenarioResult result() const;
  // State to persist with a save made while the scenario is active (record 206C6E7C).
  ScenarioState snapshot() const;
  ScenarioState& state() { return st_; }
  const ScenarioState& state() const { return st_; }
  bool active() const { return st_.active; }
  int32_t run(const Script& s) { return interp_->run(s); }

 private:
  static const Script* lookup(void* user, uint32_t inst);
  const ScenarioFile& file_;
  ICityStats& stats_;
  IScenarioHost& host_;
  GameRng rng_;
  ScenarioState st_;
  std::unique_ptr<ScriptInterpreter> interp_;
  GameDate seenRepeat_{0, 0, 0};
  int seenClockDay_ = 0;
  bool started_ = false;
};

// ---- .SSC scenario-creator template (section 7) ---------------------------------------------
struct SscMessage { uint32_t textId = 0; std::u16string notes, text, label; };
struct SscTicker { uint32_t textId = 0; std::u16string label, notes, text, a, b; uint32_t c = 0; };
struct SscVariable { uint32_t no = 0; std::u16string name, description; };
struct SscGoal { uint32_t textId = 0; std::u16string text; uint32_t goalNo = 0; std::u16string notes, label; int32_t x = -1; };
struct SscRank { uint32_t textId = 0; std::u16string text; uint32_t rankNo = 0; std::u16string label, notes; };
struct SscScriptInfo { uint32_t id = 0; std::u16string name, author, description; };
struct SscCommandArg { std::u16string kind, value; };
struct SscCommand {
  uint32_t position = 0, scriptId = 0, delay = 0, cmdId = 0;
  std::u16string comment;
  std::vector<SscCommandArg> args;
};

struct SscProject {
  uint32_t version = 3;
  std::u16string name, description, pictureName;
  Bytes picture;
  std::u16string cityName;
  Bytes city;
  uint32_t nextId = 0;
  std::u16string settings[8];  // name; description; presenter; entry script name; source city path; a; b; picture path
  std::vector<SscMessage> messages;
  std::vector<SscTicker> tickers;
  std::vector<SscVariable> variables;
  std::vector<SscGoal> goals;
  std::vector<SscRank> ranks;
  uint32_t presenterCount = 0;
  std::vector<SscScriptInfo> scripts;
  std::vector<SscCommand> commands;
  bool sectionLengthOk = false;
  size_t trailingBytes = 0;  // stale bytes after the command table

  static bool parse(const Bytes& data, SscProject& out, std::string* err = nullptr);
  Bytes serialize() const;
  // Commands of one script ordered by position, converted to the compiled form (Constant->0, Variable->1).
  Script compileScript(uint32_t scriptId) const;
  // Compile the whole project into scenario records (spec 23 section 7.1). Scripts are renumbered to
  // instances 1..n in project order; script-ID arguments expand to `601861772 n`, text-ID arguments to
  // `stringTable textID` [INFERRED]. The entry script is the one named settings[3] (else the first).
  // Returns false if the project has no script.
  bool toScenario(uint32_t stringTable, ScenarioFile& out, std::string* err = nullptr) const;
};

std::string utf16ToUtf8(const std::u16string& s);
std::u16string utf8ToUtf16(const std::string& s);

}  // namespace m3k
