#include "core/tiling_rules.h"
#include <algorithm>
#include <cctype>

namespace m3k {

static const char* kPrefix[] = {"ROAD", "HWAY", "RAIL", "SUBW", "PIPE", "POWR", "DIAG"};
const char* netPrefix(int t) { return (t >= 0 && t <= 6) ? kPrefix[t] : ""; }

int netKindToTable(int kind) {
  switch (kind) {
    case NK_ROAD: return NT_ROAD;
    case NK_RAIL: return NT_RAIL;
    case NK_POWER: return NT_POWR;
    case NK_HIGHWAY: return NT_HWAY;
    case NK_PIPE: return NT_PIPE;
    case NK_SUBWAY: return NT_SUBW;
  }
  return -1;
}
int tableToNetKind(int t) {
  switch (t) {
    case NT_ROAD: return NK_ROAD;
    case NT_RAIL: return NK_RAIL;
    case NT_POWR: return NK_POWER;
    case NT_HWAY: return NK_HIGHWAY;
    case NT_PIPE: return NK_PIPE;
    case NT_SUBW: return NK_SUBWAY;
  }
  return NK_NONE;
}

std::vector<std::string> tilingTokens(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : text) {
    if (c == ' ' || c == ',' || c == '\n' || c == '\t') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

int tilingAtoi(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || (s[i] >= 9 && s[i] <= 13))) i++;
  bool neg = false;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
  long long v = 0;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
    v = v * 10 + (s[i] - '0');
    if (v > 0x7FFFFFFFLL * 2) v = 0x7FFFFFFFLL * 2;
    i++;
  }
  if (neg) v = -v;
  return int(v);
}

std::vector<uint32_t> parseSetFile(const std::string& text) {
  std::vector<uint32_t> out;
  for (auto& t : tilingTokens(text)) {
    size_t i = 0;
    while (i < t.size() && !(t[i] >= '0' && t[i] <= '9')) i++;
    if (i >= t.size()) continue;
    uint64_t v = 0;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') { v = v * 10 + uint64_t(t[i] - '0'); i++; }
    out.push_back(uint32_t(v));
  }
  return out;
}

Piece ConvertTable::apply(const Piece& p) const {
  Piece r = p;
  for (auto& pr : pairs)
    if (pr.first == p) r = pr.second;
  return r;
}

ConvertTable parseConvertFile(const std::string& text) {
  ConvertTable t;
  auto toks = tilingTokens(text);
  size_t i = 0;
  // count = first non-zero token
  for (; i < toks.size(); i++) {
    int v = tilingAtoi(toks[i]);
    if (v != 0) { t.declaredCount = uint32_t(v); i++; break; }
  }
  Piece from, to;
  bool haveFrom = false;
  for (; i < toks.size(); i++) {
    Piece p = unpackPiece(uint32_t(tilingAtoi(toks[i])));
    if (!haveFrom) {
      if (p.id == 0) continue;  // slot "empty" while decoded id is 0
      from = p; haveFrom = true;
    } else {
      if (p.id == 0) continue;
      to = p;
      t.pairs.push_back({from, to});
      haveFrom = false;
    }
  }
  return t;
}

std::vector<int> parseFlatList(const std::string& text) {
  std::vector<int> out;
  auto toks = tilingTokens(text);
  size_t i = 0;
  for (; i < toks.size(); i++)
    if (tilingAtoi(toks[i]) != 0) { i++; break; }
  for (; i < toks.size(); i++) out.push_back(tilingAtoi(toks[i]));
  return out;
}

std::vector<std::vector<uint32_t>> parseCollapseFile(const std::string& text) {
  std::vector<std::vector<uint32_t>> groups;
  auto v = parseFlatList(text);
  size_t i = 0;
  while (i < v.size()) {
    int L = v[i];
    if (L <= 0) break;  // malformed / padding
    std::vector<uint32_t> g;
    for (int k = 1; k < L && i + size_t(k) < v.size(); k++) g.push_back(uint32_t(v[i + size_t(k)]));
    groups.push_back(std::move(g));
    i += size_t(L);
  }
  return groups;
}

std::vector<BridgeDesign> parseBridgesFile(const std::string& text) {
  std::vector<BridgeDesign> out;
  auto v = parseFlatList(text);
  size_t i = 0;
  while (i < v.size()) {
    int L = v[i];
    if (L < 3) break;
    size_t end = std::min(v.size(), i + size_t(L));
    BridgeDesign d;
    d.axis = v[i + 1];
    d.n = v[i + 2];
    size_t p = i + 3;
    bool ok = true;
    for (int s = 0; s < 5; s++) {
      if (p >= end) { ok = false; break; }
      int k = v[p++];
      if (k < 0 || p + size_t(k) > end) { ok = false; break; }
      for (int q = 0; q < k; q++) d.seg[s].push_back(uint32_t(v[p++]));
    }
    if (ok) out.push_back(std::move(d));
    i = end;
  }
  return out;
}

static const int8_t kDir[24][2] = {{-1, 0}, {0, -1}, {1, 0},  {0, 1},  {-1, -1}, {1, -1}, {1, 1},  {-1, 1},
                                   {-2, 0}, {-2, -1}, {-2, -2}, {-1, -2}, {0, -2}, {1, -2}, {2, -2}, {2, -1},
                                   {2, 0},  {2, 1},  {2, 2},  {1, 2},  {0, 2},  {-1, 2}, {-2, 2}, {-2, 1}};

void dirOffset(int dir, int& dx, int& dz) {
  if (dir < 0 || dir >= 24) { dx = dz = 0; return; }
  dx = kDir[dir][0];
  dz = kDir[dir][1];
}

RuleTable parseRulesFile(const std::string& text) {
  RuleTable t;
  auto toks = tilingTokens(text);
  int curKey = 256;
  Rule cur;
  bool inRule = false;     // between tag 1 and completion
  bool resultsOpen = false;
  int wantResults = 0;
  size_t i = 0;
  auto isDigitTok = [&](size_t k) { return k < toks.size() && !toks[k].empty() && std::isdigit((unsigned char)toks[k][0]); };
  auto finish = [&]() {
    t.rules.push_back(std::move(cur));
    cur = Rule();
    inRule = false;
    resultsOpen = false;
    cur.key = curKey;
  };
  cur.key = curKey;
  while (i < toks.size()) {
    if (!isDigitTok(i)) { i++; continue; }
    int tag = tilingAtoi(toks[i++]);
    auto nextVal = [&](int& v) {
      while (i < toks.size() && !isDigitTok(i)) i++;
      if (i >= toks.size()) return false;
      v = tilingAtoi(toks[i++]);
      return true;
    };
    int a = 0, b = 0;
    switch (tag) {
      case 0: if (!nextVal(a)) break; t.declared = a; break;
      case 1:
        if (!nextVal(a)) break;
        curKey = a;
        cur = Rule();
        cur.key = a;
        inRule = true;
        resultsOpen = false;
        break;
      case 2: nextVal(a); break;
      case 3: {
        if (!nextVal(a) || !nextVal(b)) break;
        RuleCond c;
        c.dir = a == 255 ? 31 : a;
        Piece p = unpackPiece(uint32_t(b));
        c.id = p.id; c.rot = p.rot;
        cur.conds.push_back(c);
        break;
      }
      case 4:
        if (!nextVal(a)) break;
        wantResults = a;
        resultsOpen = true;
        if (wantResults <= 0) finish();
        break;
      case 5: {
        if (!nextVal(a) || !nextVal(b)) break;
        RuleCond c;
        c.dir = a == 255 ? 31 : a;
        Piece p = unpackPiece(uint32_t(b));
        c.id = p.id; c.rot = p.rot;
        cur.results.push_back(c);
        if (resultsOpen && int(cur.results.size()) >= wantResults) finish();
        break;
      }
      default: break;
    }
  }
  (void)inRule;
  return t;
}

bool NetworkRules::isProtected(uint32_t id) const {
  return std::find(protectedIds.begin(), protectedIds.end(), int(id)) != protectedIds.end();
}
bool NetworkRules::inSet(uint32_t id) const { return std::find(set.begin(), set.end(), id) != set.end(); }

static std::string netFile(int t, const char* suffix) { return std::string(kPrefix[t]) + "_GRND_" + suffix; }

std::vector<std::string> TilingRuleSet::fileNames() {
  std::vector<std::string> n;
  for (int t = 0; t < kNumTiledNets; t++) n.push_back(netFile(t, "Set.txt"));
  n.push_back("DIAG_Set.txt");
  n.push_back("Collapse.txt");
  const int order[] = {NT_ROAD, NT_RAIL, NT_HWAY, NT_SUBW, NT_PIPE, NT_POWR};
  for (int t : order) {
    n.push_back(netFile(t, "Convert.txt"));
    n.push_back(netFile(t, "Complex_Convert.txt"));
    n.push_back(t == NT_ROAD ? std::string("Road_GRND_Protected.txt") : netFile(t, "Protected.txt"));
    if (t != NT_SUBW && t != NT_PIPE) n.push_back(netFile(t, "Bridges.txt"));
  }
  n.push_back("ROAD_GRND_SimpleRules.txt");
  n.push_back("ROAD_GRND_ComplexRules.txt");
  n.push_back("ROAD_GRND_final.txt");
  const int o2[] = {NT_RAIL, NT_HWAY, NT_SUBW, NT_PIPE, NT_POWR};
  for (int t : o2) {
    n.push_back(netFile(t, "SIMPLERULES.txt"));
    n.push_back(netFile(t, "COMPLEXRULES.txt"));
    n.push_back(netFile(t, "final.txt"));
  }
  return n;
}

void TilingRuleSet::load(const std::function<bool(const std::string&, std::string&)>& rd) {
  auto get = [&](const std::string& name) {
    std::string s;
    if (!rd(name, s)) s.clear();
    if (s.size() > 131071) s.resize(131071);
    return s;
  };
  for (int t = 0; t < kNumTiledNets; t++) nets[t].set = parseSetFile(get(netFile(t, "Set.txt")));
  diagSet = parseSetFile(get("DIAG_Set.txt"));
  collapse = parseCollapseFile(get("Collapse.txt"));
  for (int t = 0; t < kNumTiledNets; t++) {
    auto& n = nets[t];
    n.convert = parseConvertFile(get(netFile(t, "Convert.txt")));
    n.complexConvert = parseConvertFile(get(netFile(t, "Complex_Convert.txt")));
    n.protectedIds = parseFlatList(get(t == NT_ROAD ? std::string("Road_GRND_Protected.txt") : netFile(t, "Protected.txt")));
    if (t != NT_SUBW && t != NT_PIPE) n.bridges = parseBridgesFile(get(netFile(t, "Bridges.txt")));
    bool road = t == NT_ROAD;
    n.simple = parseRulesFile(get(netFile(t, road ? "SimpleRules.txt" : "SIMPLERULES.txt")));
    n.complex = parseRulesFile(get(netFile(t, road ? "ComplexRules.txt" : "COMPLEXRULES.txt")));
    n.final_ = parseRulesFile(get(netFile(t, "final.txt")));
  }
}

bool TilingRuleSet::inSet(int table, uint32_t id) const {
  if (table == NT_DIAG) return std::find(diagSet.begin(), diagSet.end(), id) != diagSet.end();
  if (table < 0 || table >= kNumTiledNets) return false;
  return nets[table].inSet(id);
}

const std::vector<uint32_t>* TilingRuleSet::collapseGroupOf(uint32_t id) const {
  for (auto& g : collapse)
    if (std::find(g.begin(), g.end(), id) != g.end()) return &g;
  return nullptr;
}

}  // namespace m3k
