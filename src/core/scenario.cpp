#include "core/scenario.h"
#include <algorithm>
#include <cstring>
#include "core/font.h"

namespace m3k {

bool isScenarioRecordKey(const ResKey& k) {
  struct TG { uint32_t t, g; };
  static const TG groups[] = {{0x04845CBE, 0xC3DE4D66}, {0xA4846144, 0xA48461A3}, {0x44EB6BC8, 0x44EB6BC9},
                              {0x62B9DA24, 0x6504691D}, {0x23DFAE5F, 0x23DFAE8C}, {0x26E732E0, 0x19A6CEA1},
                              {0x2026960B, 0x19A6CEA1}, {0x62B9DA24, 0x19A6CEA1}};
  for (const auto& g : groups)
    if (k.type == g.t && k.group == g.g) return true;
  return false;
}

static const uint32_t kAdvisors[7] = {kAdvisorUtilities, kAdvisorBudget, kAdvisorCityPlanner, kAdvisorEnvironment,
                                      kAdvisorPublicSafety, kAdvisorTransportation, kAdvisorDemographics};
// Advisor index per spec: Utilities 1, Budget 2, City Planner 3, Environment 4, Public Safety 5,
// Transportation 6, Demographics 7 (same order as kAdvisors).
bool isKnownAdvisor(uint32_t id) { return advisorIndex(id) != 0; }
int advisorIndex(uint32_t id) {
  for (int i = 0; i < 7; i++) if (kAdvisors[i] == id) return i + 1;
  return 0;
}
uint32_t advisorPortraitInstance(int idx, int mood, int variant, bool important) {
  if (mood == 0) mood = 3;
  return (important ? 0x01000000u : 0u) | (uint32_t(variant & 0xFF) << 16) | (uint32_t(idx & 0xFF) << 8) | uint32_t(mood & 0xFF);
}

std::vector<uint32_t> advisorPortraitCandidates(int idx, int mood, int variant, bool important) {
  std::vector<uint32_t> v;
  auto add = [&](uint32_t x) { if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x); };
  add(advisorPortraitInstance(idx, mood, variant, important));
  add(advisorPortraitInstance(idx, mood, 0, important));
  add(advisorPortraitInstance(idx, 3, 0, important));
  add(0x10303);
  return v;
}

namespace {
std::vector<std::string> splitCR(const std::string& t) {
  std::vector<std::string> lines;
  std::string cur;
  for (char c : t) {
    if (c == '\n') continue;
    if (c == '\r') { lines.push_back(cur); cur.clear(); } else cur.push_back(c);
  }
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}
std::string stripComment(const std::string& l) {
  size_t s = l.find(';');
  return s == std::string::npos ? l : l.substr(0, s);
}
// All integers on a line, with ',' and '=' treated as separators.
std::vector<int64_t> ints(const std::string& line) {
  std::string t = line;
  for (char& c : t) if (c == ',' || c == '=' || c == '\t') c = ' ';
  std::vector<int64_t> v;
  size_t p = 0;
  int64_t x;
  while (parseLeadingInt(t, p, x)) v.push_back(x);
  return v;
}
uint32_t u(int64_t v) { return uint32_t(uint64_t(v)); }
int32_t sgn(uint32_t v) { return int32_t(v); }
std::string text(const Bytes& b) { return std::string(b.begin(), b.end()); }
Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }
}  // namespace

// ---- header ---------------------------------------------------------------------------------
bool ScenarioHeader::parse(const std::string& t, ScenarioHeader& o, std::string* err) {
  auto L = splitCR(t);
  auto fail = [&](const char* m) { if (err) *err = m; return false; };
  if (L.size() < 6) return fail("scenario header: too few lines");
  o.signature = L[0];
  o.sourcePath = L[1];
  auto a = ints(stripComment(L[2]));
  if (a.size() < 3) return fail("scenario header: bad entry script line");
  o.entry = {u(a[0]), u(a[1]), u(a[2])};
  auto b = ints(stripComment(L[3]));
  if (b.size() < 2) return fail("scenario header: bad title line");
  o.title = {u(b[0]), u(b[1])};
  auto c = ints(stripComment(L[4]));
  if (c.size() < 2) return fail("scenario header: bad presenter line");
  o.presenter = u(c[0]);
  o.presenterKind = u(c[1]);
  auto d = ints(stripComment(L[5]));
  if (d.empty()) return fail("scenario header: bad string table line");
  o.stringTable = u(d[0]);
  return true;
}
std::string ScenarioHeader::serialize() const {
  std::string s = signature + "\r" + sourcePath + "\r";
  s += std::to_string(sgn(entry.type)) + " " + std::to_string(sgn(entry.group)) + " " + std::to_string(sgn(entry.instance)) + "\r";
  s += std::to_string(sgn(title.group)) + " " + std::to_string(sgn(title.id)) + "\r";
  s += std::to_string(sgn(presenter)) + " " + std::to_string(sgn(presenterKind)) + "\r";
  s += std::to_string(sgn(stringTable)) + "\r";
  return s;
}
uint32_t ScenarioHeader::effectivePresenter() const {
  if (presenterKind == 1 && !isKnownAdvisor(presenter)) return kAdvisorUtilities;
  return presenter;
}

// ---- style table ----------------------------------------------------------------------------
bool StyleTable::parse(const std::string& t, StyleTable& o, std::string* err) {
  o.byString.clear(); o.groups.clear();
  auto L = splitCR(t);
  if (L.empty()) { if (err) *err = "empty style table"; return false; }
  auto n = ints(L[0]);
  if (n.empty() || n[0] < 0) { if (err) *err = "bad style count"; return false; }
  for (int64_t i = 0; i < n[0] && size_t(i) + 1 < L.size(); i++) {
    auto v = ints(L[size_t(i) + 1]);
    if (v.size() < 11) { if (err) *err = "bad style row"; return false; }
    TickerStyle s;
    s.cls = int(v[2]);
    s.r1 = uint8_t(v[3]); s.g1 = uint8_t(v[4]); s.b1 = uint8_t(v[5]);
    s.r2 = uint8_t(v[6]); s.g2 = uint8_t(v[7]); s.b2 = uint8_t(v[8]);
    s.underline = v[9] != 0;
    s.repeats = int(v[10]);
    o.byString[u(v[1])] = s;
    o.groups[u(v[1])] = u(v[0]);
  }
  return true;
}
std::string StyleTable::serialize() const {
  std::string s = std::to_string(byString.size()) + "\r";
  for (const auto& kv : byString) {
    const TickerStyle& t = kv.second;
    auto g = groups.find(kv.first);
    uint32_t grp = g == groups.end() ? 0 : g->second;
    s += std::to_string(sgn(grp)) + " , " + std::to_string(sgn(kv.first)) + " = " + std::to_string(t.cls) + " , " +
         std::to_string(t.r1) + " , " + std::to_string(t.g1) + " , " + std::to_string(t.b1) + " , " +
         std::to_string(t.r2) + " , " + std::to_string(t.g2) + " , " + std::to_string(t.b2) + " , " +
         std::to_string(t.underline ? 1 : 0) + " , " + std::to_string(t.repeats) + "\r";
  }
  return s;
}
const TickerStyle* StyleTable::find(uint32_t id) const {
  auto it = byString.find(id);
  return it == byString.end() ? nullptr : &it->second;
}

TickerStyle StyleTable::resolve(uint32_t id) const {
  if (const TickerStyle* t = find(id)) return *t;
  TickerStyle d;
  d.cls = 6; d.r1 = d.g1 = d.b1 = 255; d.r2 = d.g2 = d.b2 = 0; d.underline = false; d.repeats = 1;
  return d;
}

// ---- presenters -----------------------------------------------------------------------------
bool PresenterTable::parse(const std::string& t, PresenterTable& o) {
  o.rows.clear();
  auto L = splitCR(t);
  if (L.empty()) return false;
  auto n = ints(L[0]);
  if (n.empty() || n[0] < 0) return false;
  for (int64_t i = 0; i < n[0] && size_t(i) + 1 < L.size(); i++) {
    auto v = ints(L[size_t(i) + 1]);
    if (v.size() != 3) return false;  // exactly three of up to four integers
    o.rows.push_back({int32_t(v[0]), int32_t(v[1]), int32_t(v[2])});
  }
  return true;
}
std::string PresenterTable::serialize() const {
  std::string s = std::to_string(rows.size()) + "\r";
  for (const auto& r : rows) s += std::to_string(r.id) + " " + std::to_string(r.name) + " " + std::to_string(r.title) + "\r";
  return s;
}
const PresenterRow* PresenterTable::find(int32_t id) const {
  for (const auto& r : rows) if (r.id == id) return &r;
  return nullptr;
}

// ---- catalogue ------------------------------------------------------------------------------
bool Catalogue::parse(const Bytes& b, Catalogue& o) {
  if (b.size() < 12) return false;
  o.stringTable = rd32(&b[0]); o.scenarioType = rd32(&b[4]); o.difficulty = rd32(&b[8]);
  return true;
}
Bytes Catalogue::serialize() const {
  Bytes b;
  put32(b, stringTable); put32(b, scenarioType); put32(b, difficulty);
  return b;
}

// ---- state record ---------------------------------------------------------------------------
namespace {
void putKey(Bytes& b, uint32_t t, uint32_t g, uint32_t i) { put32(b, t); put32(b, g); put32(b, i); }
}  // namespace

Bytes serializeScenarioState(const ScenarioState& s) {
  Bytes b;
  put16(b, 1); b.push_back(0x02); b.push_back(0); put32(b, 0xDEADBEEF);
  put32(b, s.clock); put32(b, s.startMonth); put32(b, s.startYear);
  for (int i = 0; i < kScenarioVars; i++) put32(b, uint32_t(i < int(s.vars.size()) ? s.vars[size_t(i)] : 0));
  for (int l = 0; l < 4; l++) {
    put32(b, uint32_t(s.repeat[l].size()));
    for (const auto& r : s.repeat[l]) putKey(b, r.type, r.group, r.instance);
  }
  put32(b, s.hasTimeline ? 1 : 0);
  if (s.hasTimeline) {
    put32(b, uint32_t(s.cursor));
    putKey(b, s.timelineScript.type, s.timelineScript.group, s.timelineScript.instance);
  }
  put32(b, s.presenter); put32(b, s.presenterMood); put32(b, s.presenterKind);
  put32(b, uint32_t(s.goals.size()));
  for (const auto& kv : s.goals) {
    put32(b, kv.first); b.push_back(kv.second.met ? 1 : 0); put32(b, kv.second.text.group); put32(b, kv.second.text.id);
  }
  put32(b, uint32_t(s.ranks.size()));
  put32(b, uint32_t(s.resultRank));
  for (const auto& kv : s.ranks) { put32(b, kv.first); put32(b, kv.second.group); put32(b, kv.second.id); }
  if (s.dialogText.group || s.dialogText.id) putKey(b, 0x2026960B, s.dialogText.group, s.dialogText.id); else putKey(b, 0, 0, 0);
  if (s.resultsText.group || s.resultsText.id) putKey(b, 0x2026960B, s.resultsText.group, s.resultsText.id); else putKey(b, 0, 0, 0);
  b.push_back(s.speedLocked ? 1 : 0);
  return b;
}

bool parseScenarioState(const Bytes& b, ScenarioState& o) {
  try {
    Reader r(b);
    r.u16();
    uint8_t flags = r.u8();
    if (flags & 2) r.u8();
    if (r.u32() != 0xDEADBEEF) return false;
    ScenarioState s;
    s.clock = r.u32(); s.startMonth = r.u32(); s.startYear = r.u32();
    for (int i = 0; i < kScenarioVars; i++) s.vars[size_t(i)] = r.s32();
    for (int l = 0; l < 4; l++) {
      uint32_t n = r.u32();
      if (n > 100000) return false;
      for (uint32_t i = 0; i < n; i++) { ScriptRef k; k.type = r.u32(); k.group = r.u32(); k.instance = r.u32(); s.repeat[l].push_back(k); }
    }
    s.hasTimeline = r.u32() != 0;
    if (s.hasTimeline) {
      s.cursor = r.s32();
      s.timelineScript.type = r.u32(); s.timelineScript.group = r.u32(); s.timelineScript.instance = r.u32();
    }
    s.presenter = r.u32(); s.presenterMood = r.u32(); s.presenterKind = r.u32();
    uint32_t gc = r.u32();
    if (gc > 100000) return false;
    for (uint32_t i = 0; i < gc; i++) {
      uint32_t id = r.u32(); Goal g; g.met = r.u8() != 0; g.text.group = r.u32(); g.text.id = r.u32();
      s.goals[id] = g;
    }
    uint32_t rc = r.u32();
    if (rc > 100000) return false;
    s.resultRank = r.s32();
    for (uint32_t i = 0; i < rc; i++) { uint32_t id = r.u32(); StringRef sr; sr.group = r.u32(); sr.id = r.u32(); s.ranks[id] = sr; }
    r.u32(); s.dialogText.group = r.u32(); s.dialogText.id = r.u32();
    r.u32(); s.resultsText.group = r.u32(); s.resultsText.id = r.u32();
    s.speedLocked = r.u8() != 0;
    s.active = true;
    o = std::move(s);
    return true;
  } catch (const FormatError&) { return false; }
}

// ---- strings & tokens -----------------------------------------------------------------------
Bytes makeStringRecord(const std::string& s) {
  Bytes b;
  put32(b, uint32_t(s.size()));
  b.insert(b.end(), s.begin(), s.end());
  return b;
}
bool readStringRecord(const IxfFile& f, const ResKey& k, std::string& out) {
  const uint8_t* p; size_t n;
  if (!f.get(k, p, n) || n < 4) return false;
  uint32_t len = rd32(p);
  if (len > n - 4) len = uint32_t(n - 4);
  out.assign(reinterpret_cast<const char*>(p + 4), len);
  return true;
}

std::string expandScenarioTokens(const std::string& s, const ScenarioTextContext& c) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] != '%') { out.push_back(s[i++]); continue; }
    size_t e = s.find('%', i + 1);
    if (e == std::string::npos) { out.append(s, i, std::string::npos); break; }
    std::string tok = s.substr(i + 1, e - i - 1);
    std::string up = toUpper(tok);
    std::string rep;
    bool ok = true;
    if (up == "MAYOR" || up == "YOURNAME") rep = c.mayor;
    else if (up == "CITYNAME" || up == "YOURCITY") rep = c.city;
    else if (up == "POPULATION") rep = groupDigits(c.population);
    else if (up == "YEAR") rep = std::to_string(c.year);
    else if (up == "PARADENAME" && !c.paradeName.empty()) rep = c.paradeName;
    else if (up == "ANYNEIGHBOR" && !c.anyNeighbor.empty()) rep = c.anyNeighbor;
    else if (up == "SCNNAME" || up == "SCN_NAME") rep = c.scenarioName;
    else if (up.size() > 5 && up.compare(0, 4, "SCNV") == 0 && up[5] == ' ') {
      size_t p = 6;
      int64_t n;
      char f = up[4];
      if (!parseLeadingInt(up, p, n) || (f != 'M' && f != 'N' && f != 'Y')) ok = false;
      else {
        int32_t val = (c.vars && n >= 0 && size_t(n) < c.vars->size()) ? (*c.vars)[size_t(n)] : 0;
        rep = f == 'M' ? "$" + groupDigits(val) : std::to_string(val);
      }
    } else ok = false;
    if (ok) { out += rep; i = e + 1; }
    else { out.push_back('%'); i++; }  // unknown token: keep text, rescan after the '%'
  }
  return out;
}

// ---- ScenarioFile ---------------------------------------------------------------------------
const Script* ScenarioFile::script(uint32_t inst) const {
  auto it = scripts.find(inst);
  return it == scripts.end() ? nullptr : &it->second;
}

static bool readText(const IxfFile& f, const ResKey& k, std::string& out) {
  Bytes b;
  if (!f.getBytes(k, b)) return false;
  out = text(b);
  return true;
}

bool ScenarioFile::load(const Bytes& file, ScenarioFile& o, std::string* err) {
  o = ScenarioFile();
  IxfFile f;
  if (!f.openMemory(file)) { if (err) *err = "not an IXF file"; return false; }
  std::string t;
  if (!readText(f, kSnrHeaderKey, t)) { if (err) *err = "no scenario header"; return false; }
  if (!ScenarioHeader::parse(t, o.header, err)) return false;
  for (const auto& e : f.entries()) {
    if (e.key.type == kSnrScriptType && e.key.group == kSnrScriptGroup) {
      std::string st;
      Script sc;
      std::string perr;
      if (readText(f, e.key, st) && Script::parse(st, sc, &perr)) o.scripts[e.key.instance] = std::move(sc);
      else o.warnings.push_back("script " + std::to_string(e.key.instance) + ": " + perr);
    } else if (e.key.type == 0x2026960B && e.key.group == o.header.stringTable && e.key.instance != kSnrAuthorInstance) {
      std::string s;
      if (readStringRecord(f, e.key, s)) o.strings[e.key.instance] = s;
    }
  }
  if (readText(f, kSnrStyleKey, t) && !StyleTable::parse(t, o.styles)) o.warnings.push_back("style table unreadable");
  if (readText(f, kSnrPresenterKey, t) && !PresenterTable::parse(t, o.presenters)) o.warnings.push_back("presenter table truncated");
  Bytes b;
  if (f.getBytes(kSnrCatalogueKey, b)) o.hasCatalogue = Catalogue::parse(b, o.catalogue);
  readStringRecord(f, kSnrShortAuthorKey, o.shortAuthor);
  readStringRecord(f, {0x2026960B, o.header.stringTable, kSnrAuthorInstance}, o.author);
  f.getBytes(kSnrPictureKey, o.picture);
  if (f.getBytes(kSnrStateKey, b)) {
    if (parseScenarioState(b, o.state)) o.hasState = true;
    else { if (err) *err = "scenario state record unreadable"; return false; }  // loading fails (spec 8)
  }
  return true;
}

ResolvedPresenter ScenarioFile::resolvePresenter(uint32_t id, uint32_t kind) const {
  ResolvedPresenter r;
  if (kind == 1) {
    r.advisor = true;
    r.advisorId = isKnownAdvisor(id) ? id : kAdvisorUtilities;
    r.advisorIdx = advisorIndex(r.advisorId);
    return r;
  }
  r.advisor = false;
  r.picture = presenterPictureKey(id);
  if (const PresenterRow* row = presenters.find(int32_t(id))) {
    r.nameString = {0x2026960B, header.stringTable, uint32_t(row->name)};
    r.titleString = {0x2026960B, header.stringTable, uint32_t(row->title)};
  } else r.found = false;
  return r;
}

ScenarioChooserInfo ScenarioFile::chooserInfo() const {
  ScenarioChooserInfo c;
  c.stringTable = hasCatalogue ? catalogue.stringTable : header.stringTable;
  c.string0 = {0x2026960B, c.stringTable, 0};
  c.string1 = {0x2026960B, c.stringTable, 1};
  c.hasPicture = !picture.empty();
  if (hasCatalogue) { c.scenarioType = catalogue.scenarioType; c.difficulty = catalogue.difficulty; }
  c.author = !author.empty() ? author : shortAuthor;
  return c;
}

void ScenarioFile::addRecords(IxfWriter& w) const {
  w.add(kSnrHeaderKey, bytesOf(header.serialize()));
  for (const auto& kv : scripts) w.add({kSnrScriptType, kSnrScriptGroup, kv.first}, bytesOf(kv.second.serialize()));
  w.add(kSnrStyleKey, bytesOf(styles.serialize()));
  w.add(kSnrPresenterKey, bytesOf(presenters.serialize()));
  if (hasCatalogue) w.add(kSnrCatalogueKey, catalogue.serialize());
  w.add(kSnrShortAuthorKey, makeStringRecord(shortAuthor));
  if (!picture.empty()) w.add(kSnrPictureKey, picture);
  w.add({0x2026960B, header.stringTable, kSnrAuthorInstance}, makeStringRecord(author));
  for (const auto& kv : strings) w.add({0x2026960B, header.stringTable, kv.first}, makeStringRecord(kv.second));
  if (hasState) w.add(kSnrStateKey, serializeScenarioState(state));
}

Bytes ScenarioFile::mergeInto(const Bytes& city) const {
  IxfFile f;
  IxfWriter w;
  if (f.openMemory(city)) {
    for (const auto& e : f.entries()) {
      if (isScenarioRecordKey(e.key) || e.key == kSnrStateKey) continue;
      if (e.key.type == 0x2026960B && e.key.group == header.stringTable) continue;
      Bytes b;
      if (e.key.type == 0x2026960B) {
        const uint8_t* p; size_t n;
        if (!f.get(e.key, p, n) || n < 4) continue;
        uint32_t len = std::min<uint32_t>(rd32(p), uint32_t(n - 4));
        b.assign(p, p + 4 + len);
      } else if (!f.getBytes(e.key, b)) continue;
      w.add(e.key, std::move(b));
    }
  }
  addRecords(w);
  return w.build();
}

Bytes ScenarioFile::buildStandalone() const {
  IxfWriter w;
  addRecords(w);
  return w.build();
}

// ---- runtime --------------------------------------------------------------------------------
ScenarioRuntime::ScenarioRuntime(const ScenarioFile& f, ICityStats& stats, IScenarioHost& host, uint32_t seed,
                                 const CommandMap* map)
    : file_(f), stats_(stats), host_(host), rng_(seed) {
  interp_ = std::make_unique<ScriptInterpreter>(st_, stats_, host_, rng_, &ScenarioRuntime::lookup, this, map);
}

const Script* ScenarioRuntime::lookup(void* user, uint32_t inst) {
  return static_cast<ScenarioRuntime*>(user)->file_.script(inst);
}

void ScenarioRuntime::start() {
  if (file_.hasState) {
    st_ = file_.state;
    st_.active = true;
    seenRepeat_ = stats_.date();  // a restored scenario must not replay the repeat lists at once
    seenClockDay_ = seenRepeat_.day;
  } else {
    st_ = ScenarioState();
    GameDate d = stats_.date();
    st_.startMonth = uint32_t(d.month);
    st_.startYear = uint32_t(d.year);
    st_.timelineScript = file_.header.entry;
    const Script* e = file_.script(file_.header.entry.instance);
    st_.hasTimeline = e && !e->cmds.empty();
    st_.cursor = -1;
    st_.presenter = file_.header.presenter;
    st_.presenterKind = file_.header.presenterKind;
    st_.active = true;
  }
  started_ = true;
  evaluate();
  host_.scenarioStarted(file_.hasState);
}

ScenarioResult ScenarioRuntime::result() const {
  ScenarioResult r;
  r.ended = started_ && !st_.active;
  r.rank = st_.resultRank;
  auto it = st_.ranks.find(uint32_t(st_.resultRank));
  if (st_.resultRank >= 0 && it != st_.ranks.end()) r.rankTitle = it->second;
  r.resultsText = st_.resultsText;
  r.resultsExtra = st_.resultsExtra;
  r.goalsTotal = int(st_.goals.size());
  for (const auto& kv : st_.goals) if (kv.second.met) r.goalsMet++;
  r.allGoalsMet = r.goalsMet == r.goalsTotal;
  return r;
}

ScenarioState ScenarioRuntime::snapshot() const {
  ScenarioState s = st_;
  if (s.hasTimeline) {
    const Script* tl = file_.script(s.timelineScript.instance);
    if (!tl || size_t(s.cursor + 1) >= tl->cmds.size()) s.hasTimeline = false;
  }
  return s;
}

void ScenarioRuntime::minuteTick() {
  st_.clock++;
  evaluate();
}

void ScenarioRuntime::dayTick() {
  int day = stats_.date().day;
  if (day != seenClockDay_) {
    st_.clock = st_.clock / 1440u * 1440u + 1440u;
    seenClockDay_ = day;
  }
  evaluate();
}

void ScenarioRuntime::setPopupOpen(bool open) {
  popupOpen_ = open;
  if (!open && pending_ && !evaluating_) evaluate();
}

void ScenarioRuntime::evaluate() {
  if (!started_ || !st_.active) return;
  if (popupOpen_ || evaluating_) { pending_ = true; return; }  // suspended / re-entered from a modal pop-up
  pending_ = false;
  evaluating_ = true;
  struct Done { bool& f; ~Done() { f = false; } } done{evaluating_};
  GameDate d = stats_.date();
  auto runList = [&](int l) {
    std::vector<ScriptRef> list = st_.repeat[l];  // copy: scripts may register more
    for (const auto& r : list) {
      if (!st_.active) break;
      if (const Script* s = file_.script(r.instance)) interp_->run(*s);
    }
  };
  bool dayChanged = d.day != seenRepeat_.day, monthChanged = d.month != seenRepeat_.month,
       yearChanged = d.year != seenRepeat_.year;
  seenRepeat_ = d;
  if (dayChanged) {
    runList(0);
    if (d.day % 7 == 0) runList(1);
  }
  if (monthChanged) runList(2);
  if (yearChanged) runList(3);
  // Timeline.
  const Script* tl = file_.script(st_.timelineScript.instance);
  while (st_.active && st_.hasTimeline && tl) {
    size_t pos = size_t(st_.cursor + 1);
    if (pos >= tl->cmds.size()) { st_.hasTimeline = false; break; }
    if (tl->cmds[pos].delay > st_.clock) break;
    interp_->evalNext(*tl, pos, true);
    st_.cursor = int32_t(pos) - 1;
    if (pos >= tl->cmds.size()) st_.hasTimeline = false;
    if (popupOpen_) { pending_ = true; break; }  // a pop-up opened mid-pass: resume when it closes
  }
  evaluating_ = false;
  if (pending_ && !popupOpen_ && st_.active) evaluate();  // ticks that arrived during the pass
}

// ---- .SSC -----------------------------------------------------------------------------------
std::string utf16ToUtf8(const std::u16string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    uint32_t c = s[i];
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
      c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); i++;
    }
    if (c < 0x80) o.push_back(char(c));
    else if (c < 0x800) { o.push_back(char(0xC0 | (c >> 6))); o.push_back(char(0x80 | (c & 63))); }
    else if (c < 0x10000) { o.push_back(char(0xE0 | (c >> 12))); o.push_back(char(0x80 | ((c >> 6) & 63))); o.push_back(char(0x80 | (c & 63))); }
    else { o.push_back(char(0xF0 | (c >> 18))); o.push_back(char(0x80 | ((c >> 12) & 63))); o.push_back(char(0x80 | ((c >> 6) & 63))); o.push_back(char(0x80 | (c & 63))); }
  }
  return o;
}
std::u16string utf8ToUtf16(const std::string& s) {
  std::u16string o;
  for (size_t i = 0; i < s.size();) {
    uint8_t c = uint8_t(s[i]);
    uint32_t cp; int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c >> 5) == 6) { cp = c & 31; n = 2; }
    else if ((c >> 4) == 14) { cp = c & 15; n = 3; }
    else { cp = c & 7; n = 4; }
    for (int k = 1; k < n && i + size_t(k) < s.size(); k++) cp = (cp << 6) | (uint8_t(s[i + size_t(k)]) & 63);
    i += size_t(n);
    if (cp >= 0x10000) { cp -= 0x10000; o.push_back(char16_t(0xD800 + (cp >> 10))); o.push_back(char16_t(0xDC00 + (cp & 0x3FF))); }
    else o.push_back(char16_t(cp));
  }
  return o;
}

namespace {
std::u16string rdW(Reader& r) {
  uint32_t n = r.u32();
  if (n & 1) throw FormatError("odd WSTR length");
  if (n > r.remaining()) throw FormatError("WSTR overrun");
  std::u16string s;
  for (uint32_t i = 0; i < n / 2; i++) s.push_back(char16_t(r.u16()));
  return s;
}
Bytes rdBlob(Reader& r) {
  uint32_t n = r.u32();
  return r.bytes(n);
}
void wW(Bytes& b, const std::u16string& s) {
  put32(b, uint32_t(s.size() * 2));
  for (char16_t c : s) put16(b, uint16_t(c));
}
void wBlob(Bytes& b, const Bytes& d) { put32(b, uint32_t(d.size())); b.insert(b.end(), d.begin(), d.end()); }
constexpr uint32_t kSscMagic = 0x75CBCE6B, kSscSection = 0x012D8EDD;
}  // namespace

bool SscProject::parse(const Bytes& data, SscProject& o, std::string* err) {
  o = SscProject();
  try {
    Reader r(data);
    if (r.u32() != kSscMagic) throw FormatError("bad SSC magic");
    r.u32();
    o.version = r.u32();
    o.name = rdW(r); o.description = rdW(r); o.pictureName = rdW(r);
    o.picture = rdBlob(r);
    r.u32();
    o.cityName = rdW(r);
    o.city = rdBlob(r);
    uint32_t secLen = r.u32();
    size_t secStart = r.pos();
    if (r.u32() != kSscSection) throw FormatError("bad SSC section magic");
    o.nextId = r.u32();
    r.u32();
    for (auto& s : o.settings) s = rdW(r);
    auto count = [&]() { uint32_t n = r.u32(); if (n > 10000000) throw FormatError("absurd table size"); return n; };
    for (uint32_t i = 0, n = count(); i < n; i++) { SscMessage m; m.textId = r.u32(); m.notes = rdW(r); m.text = rdW(r); m.label = rdW(r); o.messages.push_back(m); }
    for (uint32_t i = 0, n = count(); i < n; i++) { SscTicker t; t.textId = r.u32(); t.label = rdW(r); t.notes = rdW(r); t.text = rdW(r); t.a = rdW(r); t.b = rdW(r); t.c = r.u32(); o.tickers.push_back(t); }
    for (uint32_t i = 0, n = count(); i < n; i++) { SscVariable v; v.no = r.u32(); v.name = rdW(r); v.description = rdW(r); o.variables.push_back(v); }
    for (uint32_t i = 0, n = count(); i < n; i++) { SscGoal g; g.textId = r.u32(); g.text = rdW(r); g.goalNo = r.u32(); g.notes = rdW(r); g.label = rdW(r); g.x = r.s32(); o.goals.push_back(g); }
    for (uint32_t i = 0, n = count(); i < n; i++) { SscRank k; k.textId = r.u32(); k.text = rdW(r); k.rankNo = r.u32(); k.label = rdW(r); k.notes = rdW(r); o.ranks.push_back(k); }
    o.presenterCount = count();
    if (o.presenterCount) throw FormatError("custom presenter rows: layout unknown");
    for (uint32_t i = 0, n = count(); i < n; i++) { SscScriptInfo s; s.id = r.u32(); s.name = rdW(r); s.author = rdW(r); s.description = rdW(r); o.scripts.push_back(s); }
    for (uint32_t i = 0, n = count(); i < n; i++) {
      SscCommand c;
      c.position = r.u32(); c.scriptId = r.u32(); c.delay = r.u32(); c.cmdId = r.u32();
      c.comment = rdW(r);
      uint32_t argc = r.u32();
      if (argc > 255) throw FormatError("absurd argc");
      for (uint32_t a = 0; a < argc; a++) { SscCommandArg g; g.kind = rdW(r); g.value = rdW(r); c.args.push_back(g); }
      o.commands.push_back(std::move(c));
    }
    o.sectionLengthOk = r.pos() - secStart == secLen;
    o.trailingBytes = r.remaining();
    return true;
  } catch (const FormatError& e) {
    if (err) *err = e.what();
    return false;
  }
}

Bytes SscProject::serialize() const {
  Bytes b;
  put32(b, kSscMagic); put32(b, 0); put32(b, version);
  wW(b, name); wW(b, description); wW(b, pictureName);
  wBlob(b, picture);
  put32(b, 0);
  wW(b, cityName);
  wBlob(b, city);
  Bytes s;
  put32(s, kSscSection); put32(s, nextId); put32(s, 0);
  for (const auto& x : settings) wW(s, x);
  put32(s, uint32_t(messages.size()));
  for (const auto& m : messages) { put32(s, m.textId); wW(s, m.notes); wW(s, m.text); wW(s, m.label); }
  put32(s, uint32_t(tickers.size()));
  for (const auto& t : tickers) { put32(s, t.textId); wW(s, t.label); wW(s, t.notes); wW(s, t.text); wW(s, t.a); wW(s, t.b); put32(s, t.c); }
  put32(s, uint32_t(variables.size()));
  for (const auto& v : variables) { put32(s, v.no); wW(s, v.name); wW(s, v.description); }
  put32(s, uint32_t(goals.size()));
  for (const auto& g : goals) { put32(s, g.textId); wW(s, g.text); put32(s, g.goalNo); wW(s, g.notes); wW(s, g.label); put32(s, uint32_t(g.x)); }
  put32(s, uint32_t(ranks.size()));
  for (const auto& k : ranks) { put32(s, k.textId); wW(s, k.text); put32(s, k.rankNo); wW(s, k.label); wW(s, k.notes); }
  put32(s, 0);
  put32(s, uint32_t(scripts.size()));
  for (const auto& x : scripts) { put32(s, x.id); wW(s, x.name); wW(s, x.author); wW(s, x.description); }
  put32(s, uint32_t(commands.size()));
  for (const auto& c : commands) {
    put32(s, c.position); put32(s, c.scriptId); put32(s, c.delay); put32(s, c.cmdId);
    wW(s, c.comment);
    put32(s, uint32_t(c.args.size()));
    for (const auto& a : c.args) { wW(s, a.kind); wW(s, a.value); }
  }
  put32(b, uint32_t(s.size()));
  b.insert(b.end(), s.begin(), s.end());
  return b;
}

bool SscProject::toScenario(uint32_t G, ScenarioFile& out, std::string* err) const {
  out = ScenarioFile();
  if (scripts.empty()) { if (err) *err = "project has no script"; return false; }
  std::map<uint32_t, uint32_t> inst;
  for (size_t i = 0; i < scripts.size(); i++) inst[scripts[i].id] = uint32_t(i + 1);
  uint32_t entry = inst[scripts[0].id];
  for (const auto& sc : scripts) if (sc.name == settings[3]) { entry = inst[sc.id]; break; }
  auto num = [](const std::u16string& t) {
    std::string a = utf16ToUtf8(t);
    size_t p = 0; int64_t n = 0;
    parseLeadingInt(a, p, n);
    return n;
  };
  out.header.sourcePath = utf16ToUtf8(settings[4]);
  out.header.entry = {kSnrScriptType, kSnrScriptGroup, entry};
  out.header.title = {G, 1};
  out.header.presenter = uint32_t(uint64_t(num(settings[2])));
  out.header.presenterKind = isKnownAdvisor(out.header.presenter) ? 1 : 0;
  out.header.stringTable = G;
  for (const auto& sc : scripts) {
    Script c = compileScript(sc.id);
    for (auto& cmd : c.cmds) {
      int id = cmd.cmd;
      auto scriptArg = [&](size_t i) { return cmd.args[i].kind == 0 && inst.count(uint32_t(cmd.args[i].value)); };
      if (id == cmdSubRoutine && cmd.args.size() == 1 && scriptArg(0)) {
        uint32_t n = inst[uint32_t(cmd.args[0].value)];
        cmd.args = {{0, int32_t(kSnrScriptGroup)}, {0, int32_t(n)}};
      } else if (id == cmdRepeatRegular && cmd.args.size() == 2 && scriptArg(1)) {
        uint32_t n = inst[uint32_t(cmd.args[1].value)];
        cmd.args = {cmd.args[0], {0, int32_t(kSnrScriptGroup)}, {0, int32_t(n)}};
      } else {
        int pos = -1;
        switch (id) {
          case cmdShowNewsticker: case cmdMakeDialogText: case cmdSetResultsDlgText: case cmdPopUpMsg: pos = 0; break;
          case cmdAddGoal: case cmdAddRankInfo: pos = 1; break;
          default: break;
        }
        if (pos >= 0 && int(cmd.args.size()) + 1 == commandArgc(id))
          cmd.args.insert(cmd.args.begin() + pos, ScriptArg{0, int32_t(G)});
      }
    }
    out.scripts[inst[sc.id]] = std::move(c);
  }
  out.strings[1] = utf16ToUtf8(name);
  out.strings[0] = utf16ToUtf8(description);
  for (const auto& m : messages) out.strings[m.textId] = utf16ToUtf8(m.text);
  for (const auto& t : tickers) out.strings[t.textId] = utf16ToUtf8(t.text);
  for (const auto& g : goals) out.strings[g.textId] = utf16ToUtf8(g.text);
  for (const auto& r : ranks) out.strings[r.textId] = utf16ToUtf8(r.text);
  out.catalogue = {G, uint32_t(num(settings[5])), uint32_t(num(settings[6]))};
  out.hasCatalogue = true;
  out.picture = picture;
  out.shortAuthor = out.author = utf16ToUtf8(scripts[0].author);
  return true;
}

Script SscProject::compileScript(uint32_t id) const {
  std::vector<const SscCommand*> v;
  for (const auto& c : commands) if (c.scriptId == id) v.push_back(&c);
  std::stable_sort(v.begin(), v.end(), [](const SscCommand* a, const SscCommand* b) { return a->position < b->position; });
  Script s;
  for (const SscCommand* c : v) {
    ScriptCommand sc;
    sc.delay = c->delay;
    sc.cmd = uint16_t(c->cmdId);
    for (const auto& a : c->args) {
      ScriptArg g;
      g.kind = utf16ToUtf8(a.kind) == "Variable" ? 1 : 0;
      std::string val = utf16ToUtf8(a.value);
      size_t p = 0; int64_t n = 0;
      parseLeadingInt(val, p, n);
      g.value = int32_t(uint32_t(uint64_t(n)));
      sc.args.push_back(g);
    }
    s.cmds.push_back(std::move(sc));
  }
  return s;
}

}  // namespace m3k
