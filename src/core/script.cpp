// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/script.h"
#include <algorithm>
#include <cctype>
#include <climits>

namespace m3k {

static const char* const kNames[kCmdCount] = {
  "cmdAdd", "cmdSubtract", "cmdAndCondition", "cmdAssign", "cmdGetCrimeValue", "cmdGetLandValue",
  "cmdGetPollutionValue", "cmdGetPopulation", "cmdComputePercentage", "cmdDecrement", "cmdDivide",
  "cmdEndAnd", "cmdEndOr", "cmdEqual", "cmdGreaterThan", "cmdIf", "cmdIfElse", "cmdIncrement",
  "cmdMultiply", "cmdOrCondition", "cmdRepeatRegular", "cmdShowNewsticker", "cmdStartFire",
  "cmdStartRiot", "cmdStartTornado", "cmdSubRoutine", "cmdCountUnpoweredBuildings",
  "cmdCountUnwateredBuildings", "cmdGetTrafficDensity", "cmdGetFundsAvailable", "cmdGetMonthsElapsed",
  "cmdEndScenario", "cmdNotEqual", "cmdCountStructures", "cmdLessThan", "cmdAddGoal",
  "cmdMakeDialogText", "cmdMarkGoalStatus", "cmdAllGoalsMet", "cmdGoalStatusIs", "cmdPopUpMsg",
  "cmdAddRankInfo", "cmdSetResultsDlgText", "cmdBeginBlock", "cmdEndBlock", "cmdGenerateRandomNum",
  "cmdStartEarthquake", "cmdStartUFOAttack", "cmdGetAura", "cmdDisableNewNeighborDeals",
  "cmdEnableNewNeighborDeals", "cmdGetSurplusPower", "cmdGetSurplusWater", "cmdOfferRewardBuilding",
  "cmdGetCurrentDate", "cmdSetVariable", "cmdCountStructuresNearLocation", "cmdGetTotalDebt",
  "cmdCountStructuresWithAppearanceNearLocation", "cmdCountStructuresWithAppearanceInCity",
  "cmdMoveCamera", "cmdEnableDisableDisasters", "cmdPlaySound", "cmdSetScenarioSpeed",
  "cmdStartToxicCloud", "cmdStartWhirlpool", "cmdStartSpaceJunk", "cmdStartLocusts",
  "cmdIsOrdinanceEnacted", "cmdOfferBusinessDeal", "cmdIsBusinessDealActive",
  "cmdEnableDisableBusinessDeal", "cmdGetRCItaxes", "cmdIsNeighborConnection",
  "cmdIsNeighborDealPresent"};
static_assert(kCmdCount == 75, "command table size");
static_assert(cmdIsNeighborDealPresent == 74 && cmdGetAura == 48 && cmdMoveCamera == 60, "command ids");

const char* commandName(int id) { return id >= 0 && id < kCmdCount ? kNames[id] : nullptr; }
int commandIdByName(const std::string& n) {
  for (int i = 0; i < kCmdCount; i++)
    if (n == kNames[i]) return i;
  for (int i = 0; i < kCmdCount; i++)  // case-insensitive second chance
    if (toLower(n) == toLower(kNames[i])) return i;
  return -1;
}

int commandArgc(int id) {
  switch (id) {
    case cmdAllGoalsMet: case cmdIf: case cmdIfElse: case cmdAndCondition: case cmdOrCondition:
    case cmdBeginBlock: case cmdEndAnd: case cmdEndOr: case cmdEndBlock: return 0;
    case cmdIncrement: case cmdDecrement: case cmdAssign: case cmdGetCrimeValue: case cmdGetLandValue:
    case cmdGetPopulation: case cmdGetAura: case cmdGetFundsAvailable: case cmdGetTotalDebt:
    case cmdGetTrafficDensity: case cmdGetSurplusPower: case cmdGetSurplusWater: case cmdCountUnpoweredBuildings:
    case cmdCountUnwateredBuildings: case cmdGetMonthsElapsed: case cmdEndScenario: case cmdPlaySound:
    case cmdStartLocusts: case cmdStartUFOAttack: case cmdEnableDisableDisasters:
    case cmdDisableNewNeighborDeals: case cmdEnableNewNeighborDeals: case cmdOfferRewardBuilding:
    case cmdOfferBusinessDeal: return 1;
    case cmdSetVariable: case cmdEqual: case cmdNotEqual: case cmdGreaterThan: case cmdLessThan:
    case cmdGetPollutionValue: case cmdCountStructures: case cmdIsOrdinanceEnacted: case cmdIsBusinessDealActive:
    case cmdEnableDisableBusinessDeal: case cmdMarkGoalStatus: case cmdGoalStatusIs: case cmdShowNewsticker:
    case cmdSetScenarioSpeed: case cmdSubRoutine: case cmdStartFire: case cmdStartEarthquake:
    case cmdStartWhirlpool: case cmdStartSpaceJunk: return 2;
    case cmdAdd: case cmdSubtract: case cmdMultiply: case cmdDivide: case cmdComputePercentage:
    case cmdGenerateRandomNum: case cmdGetCurrentDate: case cmdGetRCItaxes: case cmdIsNeighborConnection:
    case cmdIsNeighborDealPresent: case cmdAddGoal: case cmdAddRankInfo: case cmdMakeDialogText:
    case cmdSetResultsDlgText: case cmdMoveCamera: case cmdRepeatRegular: case cmdStartRiot:
    case cmdStartToxicCloud: return 3;
    case cmdCountStructuresWithAppearanceInCity: return 4;
    case cmdCountStructuresNearLocation: return 5;
    case cmdPopUpMsg: return 6;
    case cmdCountStructuresWithAppearanceNearLocation: return 7;
    case cmdStartTornado: return 8;
    default: return -1;
  }
}

bool Script::delaysNonDecreasing() const {
  for (size_t i = 1; i < cmds.size(); i++) if (cmds[i].delay < cmds[i - 1].delay) return false;
  return true;
}
bool Script::argcConforms() const {
  for (const auto& c : cmds) if (commandArgc(c.cmd) != int(c.args.size())) return false;
  return true;
}

CommandMap::CommandMap() { for (int i = 0; i < kCmdCount; i++) idToCmd_[i] = i; }
CommandMap::CommandMap(const IniFile& ini) {
  for (const auto& kv : ini.section("CommandNameToIdMapping")) {
    int c = commandIdByName(kv.first);
    if (c < 0) continue;
    idToCmd_[int(parseIdNumber(kv.second))] = c;
  }
  if (idToCmd_.empty()) for (int i = 0; i < kCmdCount; i++) idToCmd_[i] = i;
}
int CommandMap::toCmd(int id) const {
  auto it = idToCmd_.find(id);
  return it == idToCmd_.end() ? -1 : it->second;
}

// ---- text form ------------------------------------------------------------------------------
bool parseLeadingInt(const std::string& s, size_t& pos, int64_t& out) {
  size_t p = pos;
  while (p < s.size() && s[p] == ' ') p++;
  bool neg = false;
  if (p < s.size() && s[p] == '-') { neg = true; p++; }
  size_t d0 = p;
  int64_t v = 0;
  while (p < s.size() && s[p] >= '0' && s[p] <= '9') { v = v * 10 + (s[p] - '0'); if (v > (int64_t(1) << 40)) v = int64_t(1) << 40; p++; }
  if (p == d0) return false;
  out = neg ? -v : v;
  pos = p;
  return true;
}

namespace {
// Split on CR; LF ignored everywhere.
std::vector<std::string> splitLines(const std::string& t) {
  std::vector<std::string> lines;
  std::string cur;
  for (char c : t) {
    if (c == '\n') continue;
    if (c == '\r') { lines.push_back(cur); cur.clear(); } else cur.push_back(c);
  }
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}
}  // namespace

bool Script::parse(const std::string& text, Script& out, std::string* err) {
  out.cmds.clear();
  auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
  auto lines = splitLines(text);
  if (lines.size() < 2) return fail("script too short");
  size_t p = 0;
  int64_t count;
  if (!parseLeadingInt(lines[1], p, count) || count < 0) return fail("bad command count");
  count &= 0xFFFF;
  size_t li = 2;
  for (int64_t i = 0; i < count; i++) {
    if (li >= lines.size()) return fail("truncated command list");
    const std::string& h = lines[li++];
    size_t q = 0;
    int64_t delay, id, argc;
    if (!parseLeadingInt(h, q, delay) || !parseLeadingInt(h, q, id) || !parseLeadingInt(h, q, argc))
      return fail("bad command header");
    ScriptCommand c;
    c.delay = uint32_t(delay);
    c.cmd = uint16_t(id);
    argc &= 0xFF;
    for (int64_t a = 0; a < argc; a++) {
      if (li >= lines.size()) return fail("truncated arguments");
      size_t r = 0;
      int64_t kind, val;
      if (!parseLeadingInt(lines[li], r, kind) || !parseLeadingInt(lines[li], r, val)) return fail("bad argument line");
      li++;
      ScriptArg arg;
      arg.kind = uint8_t(kind);
      arg.value = int32_t(uint32_t(uint64_t(val)));
      c.args.push_back(arg);
    }
    li++;  // single ignored separator line (may be absent at EOF)
    out.cmds.push_back(std::move(c));
  }
  return true;
}

std::string Script::serialize() const {
  std::string s = "; Script Code\r" + std::to_string(cmds.size()) + "\r";
  for (const auto& c : cmds) {
    s += std::to_string(c.delay) + " " + std::to_string(c.cmd) + " " + std::to_string(c.args.size()) + "\r";
    for (const auto& a : c.args) s += std::to_string(int(a.kind)) + " " + std::to_string(a.value) + "\r";
    s += "\r";
  }
  return s;
}

bool Script::operator==(const Script& o) const {
  if (cmds.size() != o.cmds.size()) return false;
  for (size_t i = 0; i < cmds.size(); i++) {
    const auto &a = cmds[i], &b = o.cmds[i];
    if (a.delay != b.delay || a.cmd != b.cmd || a.args.size() != b.args.size()) return false;
    for (size_t k = 0; k < a.args.size(); k++)
      if (a.args[k].kind != b.args[k].kind || a.args[k].value != b.args[k].value) return false;
  }
  return true;
}

// ---- RNG ------------------------------------------------------------------------------------
uint32_t ScenarioRng::rand(uint32_t n) {
  if (n == 0) return 0;
  if (n < 0xFFFFFF) return next() % n;
  uint32_t r;
  do { r = next(); } while (r > n);
  return r;
}

// ---- interpreter ----------------------------------------------------------------------------
ScriptInterpreter::ScriptInterpreter(ScenarioState& st, ICityStats& stats, IScenarioHost& host, ScenarioRng& rng,
                                     Lookup lookup, void* user, const CommandMap* map)
    : st_(st), stats_(stats), host_(host), rng_(rng), lookup_(lookup), user_(user), map_(map) {}

int32_t ScriptInterpreter::raw(const ScriptCommand& c, size_t i) const { return i < c.args.size() ? c.args[i].value : 0; }
int32_t ScriptInterpreter::argv(const ScriptCommand& c, size_t i) const {
  if (i >= c.args.size()) return 0;
  const auto& a = c.args[i];
  if (a.kind == 1) return (a.value >= 0 && a.value < kScenarioVars) ? st_.vars[size_t(a.value)] : 0;
  return a.value;
}
bool ScriptInterpreter::dest(const ScriptCommand& c, int& v) const {
  v = raw(c, 0);
  return v >= 0 && v < kScenarioVars;
}

int32_t ScriptInterpreter::run(const Script& s, int depth) {
  size_t pos = 0;
  int32_t last = 0;
  while (pos < s.cmds.size()) last = evalNext(s, pos, true, depth);
  return last;
}

static int32_t wrapAdd(int32_t a, int32_t b) { return int32_t(uint32_t(a) + uint32_t(b)); }
static int32_t divTrunc(int32_t a, int32_t b) { return b == 0 ? -1 : (b == -1 ? int32_t(0u - uint32_t(a)) : a / b); }

int32_t ScriptInterpreter::evalNext(const Script& s, size_t& pos, bool exec, int depth) {
  if (pos >= s.cmds.size()) return 0;
  if (!st_.active) exec = false;
  const ScriptCommand& c = s.cmds[pos++];
  int id = map_ ? map_->toCmd(c.cmd) : int(c.cmd);
  switch (id) {
    case cmdIf: {
      int32_t cond = evalNext(s, pos, exec, depth);
      if (cond != 0) return evalNext(s, pos, exec, depth);
      evalNext(s, pos, false, depth);
      return 0;
    }
    case cmdIfElse: {
      int32_t cond = evalNext(s, pos, exec, depth);
      if (cond != 0) { int32_t v = evalNext(s, pos, exec, depth); evalNext(s, pos, false, depth); return v; }
      evalNext(s, pos, false, depth);
      return evalNext(s, pos, exec, depth);
    }
    case cmdAndCondition:
    case cmdOrCondition: {
      bool isAnd = id == cmdAndCondition;
      int term = isAnd ? cmdEndAnd : cmdEndOr;
      bool result = isAnd;
      bool decided = false;
      while (pos < s.cmds.size()) {
        int nid = map_ ? map_->toCmd(s.cmds[pos].cmd) : int(s.cmds[pos].cmd);
        if (nid == term) { pos++; break; }
        int32_t v = evalNext(s, pos, exec && !decided, depth);
        if (!decided) {
          if (isAnd && v == 0) { result = false; decided = true; }
          if (!isAnd && v != 0) { result = true; decided = true; }
        }
      }
      return exec ? (result ? 1 : 0) : 1;
    }
    case cmdBeginBlock: {
      int32_t last = 1;
      while (pos < s.cmds.size()) {
        int nid = map_ ? map_->toCmd(s.cmds[pos].cmd) : int(s.cmds[pos].cmd);
        if (nid == cmdEndBlock) { pos++; break; }
        last = evalNext(s, pos, exec, depth);
      }
      return last;
    }
    case cmdAssign: {
      int32_t v = evalNext(s, pos, exec, depth);
      int d;
      if (exec && dest(c, d)) setVar(d, v);
      return v;
    }
    case cmdEndAnd: case cmdEndOr: case cmdEndBlock:
      return 1;
    default:
      break;
  }
  if (!exec) return 1;
  return execLeaf(c, id, depth);
}

int32_t ScriptInterpreter::execLeaf(const ScriptCommand& c, int id, int depth) {
  int v = 0;
  auto stat = [&](int32_t x) -> int32_t {
    if (!dest(c, v)) return 0;
    setVar(v, x);
    return x;
  };
  switch (id) {
    case cmdAdd: if (!dest(c, v)) return 0; setVar(v, wrapAdd(argv(c, 1), argv(c, 2))); return st_.vars[size_t(v)];
    case cmdSubtract: if (!dest(c, v)) return 0; setVar(v, int32_t(uint32_t(argv(c, 1)) - uint32_t(argv(c, 2)))); return st_.vars[size_t(v)];
    case cmdMultiply: if (!dest(c, v)) return 0; setVar(v, int32_t(uint32_t(argv(c, 1)) * uint32_t(argv(c, 2)))); return st_.vars[size_t(v)];
    case cmdDivide: if (!dest(c, v)) return 0; setVar(v, divTrunc(argv(c, 1), argv(c, 2))); return st_.vars[size_t(v)];
    case cmdComputePercentage: {
      if (!dest(c, v)) return 0;
      int32_t a2 = argv(c, 2);
      setVar(v, divTrunc(int32_t(uint32_t(argv(c, 1)) * 100u), a2));
      return st_.vars[size_t(v)];
    }
    case cmdIncrement: if (!dest(c, v)) return 0; setVar(v, wrapAdd(st_.vars[size_t(v)], 1)); return st_.vars[size_t(v)];
    case cmdDecrement: if (!dest(c, v)) return 0; setVar(v, wrapAdd(st_.vars[size_t(v)], -1)); return st_.vars[size_t(v)];
    case cmdSetVariable: if (!dest(c, v)) return 0; setVar(v, argv(c, 1)); return 1;
    case cmdGenerateRandomNum: {
      if (!dest(c, v)) return 0;
      int32_t a = argv(c, 1), b = argv(c, 2);
      int32_t lo = std::min(a, b), hi = std::max(a, b);
      int64_t range = int64_t(hi) - lo + 1;  // up to 2^32 (full s32 span): any 32-bit draw is in range
      int32_t r = lo == hi ? a : int32_t(int64_t(lo) + (range > 0xFFFFFFFFll ? int64_t(rng_.next()) : int64_t(rng_.rand(uint32_t(range)))));
      setVar(v, r);
      return r;
    }
    case cmdEqual: return argv(c, 0) == argv(c, 1);
    case cmdNotEqual: return argv(c, 0) != argv(c, 1);
    case cmdGreaterThan: return argv(c, 0) > argv(c, 1);
    case cmdLessThan: return argv(c, 0) < argv(c, 1);
    case cmdGetPopulation: return stat(stats_.population());
    case cmdGetLandValue: return stat(stats_.landValue());
    case cmdGetCrimeValue: return stat(int32_t(uint8_t(stats_.crime())));
    case cmdGetAura: return stat(int32_t(int8_t(stats_.aura())));
    case cmdGetFundsAvailable: return stat(stats_.funds());
    case cmdGetTotalDebt: return stat(stats_.totalDebt());
    case cmdGetTrafficDensity: return stat(int32_t(uint8_t(stats_.trafficDensity())));
    case cmdGetSurplusPower: if (!dest(c, v)) return 0; setVar(v, std::max(0, stats_.surplusPower())); return 1;
    case cmdGetSurplusWater: if (!dest(c, v)) return 0; setVar(v, stats_.surplusWater()); return 1;
    case cmdCountUnpoweredBuildings: return stat(stats_.unpoweredBuildings());
    case cmdCountUnwateredBuildings: return stat(stats_.unwateredBuildings());
    case cmdGetPollutionValue: {
      int k = argv(c, 1);
      if (k < 0 || k > 2 || !dest(c, v)) return 0;
      setVar(v, stats_.pollution(k));
      return st_.vars[size_t(v)];
    }
    case cmdGetMonthsElapsed: {
      GameDate d = stats_.date();
      return stat(int32_t((int64_t(d.year) - int64_t(st_.startYear)) * 12 + (int64_t(d.month) - int64_t(st_.startMonth))));
    }
    case cmdGetCurrentDate: {
      int vy = raw(c, 0), vm = raw(c, 1), vd = raw(c, 2);
      auto ok = [](int x) { return x >= 0 && x < kScenarioVars; };
      if (!ok(vy) || !ok(vm) || !ok(vd)) return 0;
      GameDate d = stats_.date();
      setVar(vy, d.year); setVar(vm, d.month); setVar(vd, d.day);
      return 1;
    }
    case cmdGetRCItaxes: {
      int a = raw(c, 0), b = raw(c, 1), d = raw(c, 2);
      auto ok = [](int x) { return x >= 0 && x < kScenarioVars; };
      if (!ok(a) || !ok(b) || !ok(d)) return 0;
      setVar(a, stats_.taxRate(0) & 0xFF); setVar(b, stats_.taxRate(1) & 0xFF); setVar(d, stats_.taxRate(2) & 0xFF);
      return 1;
    }
    case cmdCountStructures: return stat(stats_.countStructures(uint32_t(argv(c, 1))));
    case cmdCountStructuresNearLocation:
      return stat(stats_.countStructuresNear(uint32_t(argv(c, 1)), argv(c, 2), argv(c, 3), argv(c, 4)));
    case cmdCountStructuresWithAppearanceInCity:
      return stat(stats_.countAppearance(uint32_t(argv(c, 1)), uint32_t(argv(c, 2)), uint32_t(argv(c, 3))));
    case cmdCountStructuresWithAppearanceNearLocation:
      return stat(stats_.countAppearanceNear(uint32_t(argv(c, 1)), uint32_t(argv(c, 2)), uint32_t(argv(c, 3)),
                                             argv(c, 4), argv(c, 5), argv(c, 6)));
    case cmdIsOrdinanceEnacted: return stats_.ordinanceEnacted(argv(c, 0)) == (argv(c, 1) != 0);
    case cmdIsNeighborConnection: {
      bool t = stats_.neighborConnection(argv(c, 0) & 0xFF, argv(c, 1) & 0xFF);
      return argv(c, 2) == 1 ? t : !t;
    }
    case cmdIsNeighborDealPresent: {
      bool t = stats_.neighborDealPresent(argv(c, 0) & 0xFF, argv(c, 1) & 0xFF);
      return argv(c, 2) == 1 ? t : !t;
    }
    case cmdIsBusinessDealActive: {
      bool t = stats_.businessDealActive(argv(c, 0));
      return argv(c, 1) == 1 ? t : !t;
    }
    case cmdAddGoal: {
      Goal g; g.text = {uint32_t(argv(c, 1)), uint32_t(argv(c, 2))};
      st_.goals[uint32_t(argv(c, 0))] = g;
      return 1;
    }
    case cmdMarkGoalStatus: {
      auto it = st_.goals.find(uint32_t(argv(c, 0)));
      if (it == st_.goals.end()) return 0;
      it->second.met = argv(c, 1) != 0;
      return 1;
    }
    case cmdGoalStatusIs: {
      auto it = st_.goals.find(uint32_t(argv(c, 0)));
      if (it == st_.goals.end()) return 0;
      return it->second.met == (argv(c, 1) != 0);
    }
    case cmdAllGoalsMet:
      for (const auto& kv : st_.goals) if (!kv.second.met) return 0;
      return 1;
    case cmdAddRankInfo: st_.ranks[uint32_t(argv(c, 0))] = {uint32_t(argv(c, 1)), uint32_t(argv(c, 2))}; return 1;
    case cmdEndScenario: {
      uint32_t rk = uint32_t(argv(c, 0));
      st_.resultRank = st_.ranks.count(rk) ? int32_t(rk) : -1;
      st_.active = false;
      host_.endScenario(st_.resultRank);
      return 1;
    }
    case cmdMakeDialogText: case cmdSetResultsDlgText: {
      // Each call replaces the dialog's single text; the third argument is the presenter mood (shared by both commands, last write wins,
      // used only when the presenter is an advisor), not a slot (spec 23).
      StringRef r{uint32_t(argv(c, 0)), uint32_t(argv(c, 1))};
      const bool dlg = id == cmdMakeDialogText;
      (dlg ? st_.dialogText : st_.resultsText) = r;
      (dlg ? st_.dialogExtra : st_.resultsExtra).clear();
      st_.presenterMood = uint32_t(argv(c, 2));
      return 1;
    }
    case cmdPopUpMsg: {
      PopUpRequest p;
      p.group = uint32_t(argv(c, 0)); p.id = uint32_t(argv(c, 1)); p.important = argv(c, 2);
      p.presenter = argv(c, 3); p.a4 = argv(c, 4); p.mood = argv(c, 5);
      host_.popUp(p);
      return 1;
    }
    case cmdShowNewsticker: host_.newsTicker(uint32_t(argv(c, 0)), uint32_t(argv(c, 1))); return 1;
    case cmdPlaySound: host_.playSound(argv(c, 0)); return 1;
    case cmdMoveCamera: {
      int x = argv(c, 0), z = argv(c, 1), zoom = argv(c, 2);
      int w = 0, h = 0;
      stats_.mapSize(w, h);
      if (x < 0 || z < 0 || x >= w || z >= h) return 0;
      if (zoom > 4) zoom = 2;
      return host_.moveCamera(x, z, zoom) ? 1 : 0;
    }
    case cmdSetScenarioSpeed: host_.setSpeed(argv(c, 0)); st_.speedLocked = argv(c, 1) != 0; return 1;
    case cmdRepeatRegular: {
      int f = argv(c, 0);
      if (f < 0 || f > 3) return 0;
      ScriptRef r; r.instance = uint32_t(argv(c, 2));
      st_.repeat[f].push_back(r);
      return 1;
    }
    case cmdSubRoutine: {
      if (depth >= 32 || !lookup_) return 0;
      const Script* sub = lookup_(user_, uint32_t(argv(c, 1)));
      if (!sub) return 0;
      return run(*sub, depth + 1);
    }
    case cmdStartFire: host_.startDisaster(DisasterKind::Fire, {argv(c, 0), argv(c, 1)}); return 1;
    case cmdStartRiot: host_.startDisaster(DisasterKind::Riot, {argv(c, 0), argv(c, 1), argv(c, 2)}); return 1;
    case cmdStartEarthquake: host_.startDisaster(DisasterKind::Earthquake, {argv(c, 0), argv(c, 1)}); return 1;
    case cmdStartLocusts: host_.startDisaster(DisasterKind::Locusts, {argv(c, 0)}); return 1;
    case cmdStartUFOAttack: host_.startDisaster(DisasterKind::UFO, {argv(c, 0)}); return 1;
    case cmdStartWhirlpool: host_.startDisaster(DisasterKind::Whirlpool, {argv(c, 0), argv(c, 1)}); return 1;
    case cmdStartSpaceJunk: host_.startDisaster(DisasterKind::SpaceJunk, {argv(c, 0), argv(c, 1)}); return 1;
    case cmdStartToxicCloud: host_.startDisaster(DisasterKind::ToxicCloud, {argv(c, 0), argv(c, 1), argv(c, 2)}); return 1;
    case cmdStartTornado: {
      std::vector<int32_t> p;
      for (size_t i = 0; i < 8; i++) p.push_back(argv(c, i));
      host_.startDisaster(DisasterKind::Tornado, p);
      return 1;
    }
    case cmdEnableDisableDisasters: host_.setDisastersEnabled(argv(c, 0) != 0); return 1;
    case cmdDisableNewNeighborDeals: case cmdEnableNewNeighborDeals: {
      int t = argv(c, 0);
      if (t >= -1 && t <= 5) host_.setNeighborDealsBlocked(t, id == cmdDisableNewNeighborDeals);
      return 1;
    }
    case cmdOfferRewardBuilding: host_.offerRewardBuilding(uint32_t(argv(c, 0))); return 1;
    case cmdOfferBusinessDeal: host_.offerBusinessDeal(argv(c, 0)); return 1;
    case cmdEnableDisableBusinessDeal: host_.setBusinessDealEnabled(argv(c, 0), argv(c, 1) == 1); return 1;
    default: return 0;
  }
}

}  // namespace m3k
