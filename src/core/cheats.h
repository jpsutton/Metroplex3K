// Cheat phrase table, matching, active flags and the cheat log records. spec/27-platform.md
// section 5, spec/10-save-format.md section 3.6.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/ixf.h"

namespace m3k {

constexpr ResKey kCheatLogKey{0x630E2EE6, 0x730E2EE6, 0};    // IDs of cheats used
constexpr ResKey kCheatCountKey{0x430E2EE6, 0x530E2EE6, 0};  // "count" (meaning open; round-trip)
constexpr uint32_t kTickerStringTable = 0x42C1ED2D;           // string table of triggered ticker messages

enum class CheatKind {
  Ticker,   // news-ticker response only (never sets the active flag, never logged)
  Sets,     // switches state on for the session (active flag set -> logged)
  Toggle,   // flips state (flag set when turned on -> logged)
  Action,   // one-shot effect (no flag)
};

struct CheatInfo {
  const char* phrase;
  uint32_t id;            // 0 = ID not known (never logged)
  CheatKind kind;
  bool takesArg;          // text after the phrase is an argument
  uint16_t tickerMsg;     // Ticker kind: ticker string ID (capitals variant for `tickerMsgAlt` pairs)
  uint16_t tickerMsgAlt;  // second message when typed in lower case / otherwise; 0 = none
};

const std::vector<CheatInfo>& cheatTable();
const CheatInfo* findCheatByPhrase(const std::string& phrase);  // exact, case-insensitive
const CheatInfo* findCheatById(uint32_t id);

struct CheatMatch {
  const CheatInfo* info = nullptr;
  std::string arg;        // text after the phrase, leading spaces removed
  bool allCaps = false;   // typed text had no lower-case letters
  explicit operator bool() const { return info != nullptr; }
};
// Matches when the entered text begins with a registered phrase (case-insensitive: [OPEN] in the
// spec, chosen because shell cheats compare against upper-case forms). Longest phrase wins.
CheatMatch matchCheat(const std::string& entered);

// Ticker message ID for a Ticker-kind match (0 if none). `fund`/`help`/`sim`/`maxis` pick the first
// ID when typed in capitals, else the second [INFERRED order]. `simon says` echoes the argument.
uint16_t tickerMessageFor(const CheatMatch& m);

struct CheatResult {
  CheatMatch match;
  bool stateOn = false;         // Sets/Toggle: new state
  bool newlyLogged = false;     // ID was appended to the log by this entry
  uint16_t tickerMsg = 0;
  std::string tickerEcho;       // `simon says`: argument to echo on the ticker
};

// Per-session cheat state plus the per-city log.
class CheatRegistry {
 public:
  CheatResult submit(const std::string& entered);
  bool active(uint32_t id) const { return active_.count(id) != 0; }
  bool active(const std::string& phrase) const;
  void resetSession() { active_.clear(); phraseOn_.clear(); }          // on city load / new city
  const std::vector<uint32_t>& log() const { return log_; }
  void setLog(std::vector<uint32_t> ids) { log_ = std::move(ids); }
  void clearLog() { log_.clear(); }
  // Marks an active flag directly (e.g. `garbage in, garbage out` set by its owner). Logs once.
  bool setActive(uint32_t id, bool on);
 private:
  std::set<uint32_t> active_;
  std::set<std::string> phraseOn_;  // lower-case phrases of ID-less cheats
  std::vector<uint32_t> log_;
};

// ---- cheat effect helpers (spec 27 section 5.3) ----------------------------------------------
// Altitude delta of the four `Terrain one/ten up/down` phrases (+1, -1, +10, -10); 0 for others.
int terrainCheatDelta(const CheatInfo* c);
// New value for ground altitude, water altitude and sea level: clamp(v + delta, 2, 253).
int shiftTerrainAltitude(int value, int delta);
// `so sue me`: after the monthly settlement the treasury never stays below -99,999.
int32_t soSueMeClamp(int32_t funds);
// `traffic lights <arg>`: true when the argument is `on` or `ON`, else false.
bool trafficLightsArgOn(const CheatMatch& m);
// `force <advisor> to say <n>` / `force petitioners to say <n>`: message string ID (decimal, or 0x hex), -1 if absent.
long forcedMessageId(const CheatMatch& m);
// Phrases that set a build-menu flag etc. are queried by phrase: true while `phrase` is on this session.

// Log record payloads (versioned header: version 1, flags 0x02, flags2 0, 0xDEADBEEF).
Bytes serializeCheatLog(const std::vector<uint32_t>& ids);  // caller omits the record when empty
bool parseCheatLog(const Bytes& payload, std::vector<uint32_t>& ids);  // cap 1024
Bytes serializeCheatCount(uint32_t v);
bool parseCheatCount(const Bytes& payload, uint32_t& v);

// Read the log from / write it into a city file image (outer records). An empty log removes the record.
bool readCheatLog(const Bytes& cityFile, std::vector<uint32_t>& ids);
Bytes writeCheatLog(const Bytes& cityFile, const std::vector<uint32_t>& ids);

}  // namespace m3k
