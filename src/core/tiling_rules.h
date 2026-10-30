// TilingRules language parser (spec/12-networks.md section 3-4). Platform independent: files are
// supplied through a reader callback, not read from disk here.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace m3k {

// Table index order of spec 12 section 1 (ROAD, HWAY, RAIL, SUBW, PIPE, POWR, DIAG).
enum NetTable { NT_ROAD = 0, NT_HWAY = 1, NT_RAIL = 2, NT_SUBW = 3, NT_PIPE = 4, NT_POWR = 5, NT_DIAG = 6 };
constexpr int kNumTiledNets = 6;
const char* netPrefix(int table);  // "ROAD", "HWAY", ...

// Tool / tiler numbering (spec 12 section 1, second list): 1 road, 2 rail, 3 power, 4 highway,
// 5 pipe, 6 subway.
enum NetKind { NK_NONE = 0, NK_ROAD = 1, NK_RAIL = 2, NK_POWER = 3, NK_HIGHWAY = 4, NK_PIPE = 5, NK_SUBWAY = 6 };
int netKindToTable(int kind);  // -1 for invalid
int tableToNetKind(int table);

// A packed piece (section 2): occupant id = P>>8, rotation = P&0xFF.
struct Piece {
  uint32_t id = 0;
  int rot = 0;
  bool operator==(const Piece& o) const { return id == o.id && rot == o.rot; }
};
inline Piece unpackPiece(uint32_t p) { return Piece{p >> 8, int(p & 0xFF)}; }
inline uint32_t packPiece(const Piece& p) { return (p.id << 8) | uint32_t(p.rot & 0xFF); }

// Section 4.1: split at space, comma, LF, tab. CR stays glued to the token.
std::vector<std::string> tilingTokens(const std::string& text);
// C atoi semantics (leading whitespace, sign, digits).
int tilingAtoi(const std::string& tok);

// 4.2 Set file: every digit run found in a token (leading non-digits dropped) is a member id.
std::vector<uint32_t> parseSetFile(const std::string& text);

// 4.3 Convert / Complex_Convert.
struct ConvertTable {
  std::vector<std::pair<Piece, Piece>> pairs;
  uint32_t declaredCount = 0;
  // Last matching pair wins; returns p unchanged when none matches.
  Piece apply(const Piece& p) const;
};
ConvertTable parseConvertFile(const std::string& text);

// 4.4 Protected / flat list: count then items verbatim.
std::vector<int> parseFlatList(const std::string& text);
inline std::vector<int> parseProtectedFile(const std::string& text) { return parseFlatList(text); }

// Collapse.txt: groups of occupant ids (length token includes itself).
std::vector<std::vector<uint32_t>> parseCollapseFile(const std::string& text);

// Bridges: records `L axis n (k piece*k)x5`.
struct BridgeDesign {
  int axis = 0;
  int n = 0;
  std::vector<uint32_t> seg[5];  // packed pieces: start ramp, left filler, body, right filler, end ramp
};
std::vector<BridgeDesign> parseBridgesFile(const std::string& text);

// 4.5 Rule files.
struct RuleCond {
  int dir = 31;      // 255 stored as 31
  uint32_t id = 0;
  int rot = 0;
};
struct Rule {
  int key = 256;
  std::vector<RuleCond> conds;
  std::vector<RuleCond> results;
};
struct RuleTable {
  int declared = 0;  // tag 0 header value
  std::vector<Rule> rules;
};
RuleTable parseRulesFile(const std::string& text);

// (dx, dz) of a direction code 0..31 (section 4.5 table; 24..31 mean (0,0)).
void dirOffset(int dir, int& dx, int& dz);

struct NetworkRules {
  std::vector<uint32_t> set;
  ConvertTable convert, complexConvert;
  std::vector<int> protectedIds;
  std::vector<BridgeDesign> bridges;
  RuleTable simple, complex, final_;
  bool isProtected(uint32_t id) const;
  bool inSet(uint32_t id) const;
};

struct TilingRuleSet {
  NetworkRules nets[kNumTiledNets];
  std::vector<uint32_t> diagSet;
  std::vector<std::vector<uint32_t>> collapse;
  // File-name list loaded by the game, in order (section 3). Names are as written in the spec.
  static std::vector<std::string> fileNames();
  // `reader(name, out)` returns false when the file is missing (treated as empty). Names are
  // matched however the callback likes (the game's file system is case-insensitive).
  void load(const std::function<bool(const std::string&, std::string&)>& reader);
  // "is occupant id a member of network N's set" query (table index 0..5, 6 = DIAG).
  bool inSet(int table, uint32_t id) const;
  // Collapse group containing id (empty when none).
  const std::vector<uint32_t>* collapseGroupOf(uint32_t id) const;
};

}  // namespace m3k
