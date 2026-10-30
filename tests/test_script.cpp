#include "core/script.h"
#include "testutil.h"
using namespace m3k;

struct FakeStats : ICityStats {
  int32_t pop = 1234, lv = 50, cr = 300, au = 200, fnd = 10000, debt = 5;
  GameDate dt{1, 3, 2001};
  int32_t surplusP = -5, surplusW = -7;
  int32_t population() const override { return pop; }
  int32_t landValue() const override { return lv; }
  int32_t crime() const override { return cr; }
  int32_t aura() const override { return au; }
  int32_t pollution(int k) const override { return 10 + k; }
  int32_t funds() const override { return fnd; }
  int32_t totalDebt() const override { return debt; }
  int32_t taxRate(int c) const override { return 7 + c; }
  int32_t trafficDensity() const override { return 257; }
  int32_t surplusPower() const override { return surplusP; }
  int32_t surplusWater() const override { return surplusW; }
  int32_t unpoweredBuildings() const override { return 3; }
  int32_t unwateredBuildings() const override { return 4; }
  GameDate date() const override { return dt; }
  void mapSize(int& w, int& h) const override { w = h = 128; }
  int32_t countStructures(uint32_t id) const override { return id == 77 ? 5 : 0; }
  int32_t countStructuresNear(uint32_t id, int x, int z, int r) const override { return id == 77 && x == 10 && z == 20 && r == 3 ? 2 : 0; }
  int32_t countAppearance(uint32_t, uint32_t, uint32_t) const override { return 9; }
  int32_t countAppearanceNear(uint32_t, uint32_t, uint32_t, int, int, int) const override { return 8; }
  bool ordinanceEnacted(int id) const override { return id == 4; }
  bool neighborConnection(int a, int b) const override { return a == 1 && b == 2; }
  bool neighborDealPresent(int, int) const override { return false; }
  bool businessDealActive(int id) const override { return id == 3; }
};

struct Host : IScenarioHost {
  int popups = 0, ends = -99, tickers = 0, speed = -1;
  std::vector<DisasterKind> dis;
  void popUp(const PopUpRequest&) override { popups++; }
  void newsTicker(uint32_t, uint32_t) override { tickers++; }
  void endScenario(int r) override { ends = r; }
  void setSpeed(int s) override { speed = s; }
  void startDisaster(DisasterKind k, const std::vector<int32_t>&) override { dis.push_back(k); }
};

static Script mk(std::initializer_list<ScriptCommand> l) { Script s; s.cmds = l; return s; }
static ScriptCommand C(int cmd, std::initializer_list<ScriptArg> a, uint32_t delay = 0) {
  ScriptCommand c; c.cmd = uint16_t(cmd); c.delay = delay; c.args = a; return c;
}
static ScriptArg L(int32_t v) { return {0, v}; }
static ScriptArg V(int32_t v) { return {1, v}; }

static const Script* noLookup(void*, uint32_t) { return nullptr; }

int main() {
  {
    Script s = mk({C(cmdAdd, {L(1), L(2), V(3)}, 1440), C(cmdAllGoalsMet, {})});
    std::string t = s.serialize();
    Script b;
    CHECK(Script::parse(t, b));
    CHECK(b == s);
    Script q;
    CHECK(Script::parse("; Script Code\r1\r0 55 2 ; c\r0 1\r0 2.5\r\r", q));
    CHECK(q.cmds.size() == 1 && q.cmds[0].args[0].kind == 0 && q.cmds[0].args[0].value == 1);
    CHECK(q.cmds[0].args[1].value == 2);
    Script q2;
    CHECK(Script::parse("; Script Code\n\r1\n\r0 55 1\n\r0 -95955054\n\r\n\r", q2));
    CHECK(q2.cmds[0].args[0].value == -95955054);
    Script bad;
    CHECK(!Script::parse("; Script Code\r2\r0 55 0\r\r", bad));
  }
  CHECK(std::string(commandName(cmdIsNeighborDealPresent)) == "cmdIsNeighborDealPresent");
  CHECK(commandIdByName("cmdGetAura") == 48);
  {
    IniFile ini = IniFile::fromText("[CommandNameToIdMapping]\ncmdAdd=1\ncmdSubtract=0\n");
    CommandMap m(ini);
    CHECK(m.toCmd(1) == cmdAdd && m.toCmd(0) == cmdSubtract && m.toCmd(9) == -1);
  }
  {
    GameRng r(1);
    CHECK(r.next() == 0x41C6);
    CHECK(r.next() == 0x59D4967E);
    GameRng r2(1);
    CHECK(r2.rand(100) == 38);
  }

  FakeStats st;
  Host host;
  GameRng rng(1);
  ScenarioState ss;
  ss.active = true;
  ss.startMonth = 1; ss.startYear = 2000;
  ScriptInterpreter in(ss, st, host, rng, noLookup, nullptr);

  {
    Script s = mk({C(cmdSetVariable, {L(1), L(10)}), C(cmdAdd, {L(2), V(1), L(5)}), C(cmdMultiply, {L(3), V(2), L(-2)}),
                   C(cmdDivide, {L(4), V(3), L(0)}), C(cmdDivide, {L(5), L(-7), L(2)}),
                   C(cmdComputePercentage, {L(6), L(1), L(4)}), C(cmdComputePercentage, {L(7), L(1), L(0)}),
                   C(cmdIncrement, {L(8)}), C(cmdDecrement, {L(9)}), C(cmdSubtract, {L(10), L(3), L(5)})});
    in.run(s);
    CHECK(ss.vars[2] == 15 && ss.vars[3] == -30 && ss.vars[4] == -1 && ss.vars[5] == -3);
    CHECK(ss.vars[6] == 25 && ss.vars[7] == -1 && ss.vars[8] == 1 && ss.vars[9] == -1 && ss.vars[10] == -2);
    Script bad = mk({C(cmdAdd, {L(600), L(1), L(1)})});
    CHECK(in.run(bad) == 0);
  }
  {
    Script s = mk({C(cmdGetPopulation, {L(20)}), C(cmdGetCrimeValue, {L(21)}), C(cmdGetAura, {L(22)}),
                   C(cmdGetTrafficDensity, {L(23)}), C(cmdGetSurplusPower, {L(24)}), C(cmdGetSurplusWater, {L(25)}),
                   C(cmdGetPollutionValue, {L(26), L(2)}), C(cmdGetPollutionValue, {L(27), L(3)}),
                   C(cmdGetCurrentDate, {L(30), L(31), L(32)}), C(cmdGetMonthsElapsed, {L(33)}),
                   C(cmdGetRCItaxes, {L(34), L(35), L(36)}), C(cmdCountStructures, {L(37), L(77)}),
                   C(cmdCountStructuresNearLocation, {L(38), L(77), L(10), L(20), L(3)}),
                   C(cmdGetFundsAvailable, {L(39)}), C(cmdCountUnpoweredBuildings, {L(40)})});
    in.run(s);
    CHECK(ss.vars[20] == 1234 && ss.vars[21] == 300 - 256 && ss.vars[22] == 200 - 256);
    CHECK(ss.vars[23] == 1 && ss.vars[24] == 0 && ss.vars[25] == -7 && ss.vars[26] == 12 && ss.vars[27] == 0);
    CHECK(ss.vars[30] == 2001 && ss.vars[31] == 3 && ss.vars[32] == 1);
    CHECK(ss.vars[33] == 14);
    CHECK(ss.vars[34] == 7 && ss.vars[36] == 9 && ss.vars[37] == 5 && ss.vars[38] == 2 && ss.vars[39] == 10000 && ss.vars[40] == 3);
  }
  {
    ss.vars[50] = 0;
    Script s = mk({C(cmdIf, {}), C(cmdGreaterThan, {L(3), L(5)}), C(cmdSetVariable, {L(50), L(1)}),
                   C(cmdIfElse, {}), C(cmdLessThan, {L(3), L(5)}), C(cmdSetVariable, {L(51), L(7)}), C(cmdSetVariable, {L(52), L(8)}),
                   C(cmdAssign, {L(53)}), C(cmdAndCondition, {}), C(cmdEqual, {L(1), L(1)}), C(cmdEqual, {L(1), L(2)}),
                   C(cmdSetVariable, {L(54), L(9)}), C(cmdEndAnd, {}),
                   C(cmdAssign, {L(55)}), C(cmdOrCondition, {}), C(cmdEqual, {L(1), L(2)}), C(cmdEqual, {L(2), L(2)}), C(cmdEndOr, {}),
                   C(cmdBeginBlock, {}), C(cmdSetVariable, {L(56), L(1)}), C(cmdSetVariable, {L(57), L(2)}), C(cmdEndBlock, {}),
                   C(cmdSetVariable, {L(58), L(1)})});
    in.run(s);
    CHECK(ss.vars[50] == 0);
    CHECK(ss.vars[51] == 7 && ss.vars[52] == 0);
    CHECK(ss.vars[53] == 0);
    CHECK(ss.vars[54] == 0);
    CHECK(ss.vars[55] == 1);
    CHECK(ss.vars[56] == 1 && ss.vars[57] == 2 && ss.vars[58] == 1);
  }
  {
    Script s = mk({C(cmdGenerateRandomNum, {L(60), L(10), L(5)}), C(cmdGenerateRandomNum, {L(61), L(4), L(4)}),
                   C(cmdAddGoal, {L(1), L(100), L(1)}), C(cmdAddGoal, {L(2), L(100), L(2)}),
                   C(cmdMarkGoalStatus, {L(1), L(1)}), C(cmdAllGoalsMet, {})});
    CHECK(in.run(s) == 0);
    CHECK(ss.vars[60] >= 5 && ss.vars[60] <= 10 && ss.vars[61] == 4);
    Script t = mk({C(cmdMarkGoalStatus, {L(2), L(1)}), C(cmdAllGoalsMet, {}), C(cmdGoalStatusIs, {L(9), L(1)})});
    CHECK(in.run(t) == 0);
    Script e = mk({C(cmdAddRankInfo, {L(3), L(100), L(9)}), C(cmdEndScenario, {L(3)}), C(cmdSetVariable, {L(70), L(1)}),
                   C(cmdPopUpMsg, {L(1), L(2), L(0), L(3), L(0), L(3)})});
    in.run(e);
    CHECK(host.ends == 3 && ss.resultRank == 3 && !ss.active && ss.vars[70] == 0 && host.popups == 0);
  }

  // ---- every command of spec 23 section 4.6 with a recording host ----------------------------
  {
    struct RHost : IScenarioHost {
      std::vector<std::string> log;
      std::vector<std::vector<int32_t>> params;
      int camZoom = -1; bool camOk = true;
      void popUp(const PopUpRequest& p) override { log.push_back("popup"); params.push_back({int32_t(p.group), int32_t(p.id), p.important, p.presenter, p.a4, p.mood}); }
      void newsTicker(uint32_t g, uint32_t i) override { log.push_back("ticker"); params.push_back({int32_t(g), int32_t(i)}); }
      void playSound(int32_t id) override { log.push_back("sound"); params.push_back({id}); }
      bool moveCamera(int x, int z, int zoom) override { log.push_back("camera"); params.push_back({x, z, zoom}); return camOk; }
      void setSpeed(int sp) override { log.push_back("speed"); params.push_back({sp}); }
      void startDisaster(DisasterKind k, const std::vector<int32_t>& p) override { log.push_back("disaster" + std::to_string(int(k))); params.push_back(p); }
      void setDisastersEnabled(bool b) override { log.push_back(b ? "disOn" : "disOff"); }
      void setNeighborDealsBlocked(int t, bool b) override { log.push_back(b ? "block" : "allow"); params.push_back({t}); }
      void offerRewardBuilding(uint32_t id) override { log.push_back("reward"); params.push_back({int32_t(id)}); }
      void offerBusinessDeal(int id) override { log.push_back("offerDeal"); params.push_back({id}); }
      void setBusinessDealEnabled(int id, bool b) override { log.push_back(b ? "dealOn" : "dealOff"); params.push_back({id}); }
    };
    FakeStats st2;
    RHost h2;
    GameRng rg(7);
    ScenarioState s2; s2.active = true;
    Script sub = mk({C(cmdSetVariable, {L(100), L(77)})});
    struct Ctx { const Script* sub; } ctx{&sub};
    auto look = [](void* u, uint32_t inst) -> const Script* { return inst == 9 ? static_cast<Ctx*>(u)->sub : nullptr; };
    ScriptInterpreter in2(s2, st2, h2, rg, look, &ctx);
    // scripts and subroutines
    CHECK(in2.run(mk({C(cmdSubRoutine, {L(601861772), L(9)})})) == 1 && s2.vars[100] == 77);
    CHECK(in2.run(mk({C(cmdSubRoutine, {L(601861772), L(8)})})) == 0);
    CHECK(in2.run(mk({C(cmdRepeatRegular, {L(1), L(601861772), L(5)}), C(cmdRepeatRegular, {L(3), L(601861772), L(6)})})) == 1);
    CHECK(s2.repeat[1].size() == 1 && s2.repeat[1][0].instance == 5 && s2.repeat[3].size() == 1);
    CHECK(in2.run(mk({C(cmdRepeatRegular, {L(4), L(601861772), L(5)})})) == 0);
    // comparisons / variable arguments (kind 1)
    s2.vars[1] = 5; s2.vars[2] = 5;
    CHECK(in2.run(mk({C(cmdEqual, {V(1), V(2)})})) == 1 && in2.run(mk({C(cmdNotEqual, {V(1), L(5)})})) == 0);
    CHECK(in2.run(mk({C(cmdGreaterThan, {L(-1), L(-2)})})) == 1 && in2.run(mk({C(cmdLessThan, {L(-1), L(-2)})})) == 0);
    CHECK(in2.run(mk({C(cmdEqual, {L(0)})})) == 1);  // missing argument reads as 0
    // statistics: invalid destination returns 0 and writes nothing, cmdGetSurplus* return 1
    CHECK(in2.run(mk({C(cmdGetPopulation, {L(512)})})) == 0);
    CHECK(in2.run(mk({C(cmdGetSurplusPower, {L(512)})})) == 0 && in2.run(mk({C(cmdGetSurplusPower, {L(90)})})) == 1 && s2.vars[90] == 0);
    CHECK(in2.run(mk({C(cmdGetLandValue, {L(91)}), C(cmdGetTotalDebt, {L(92)}), C(cmdCountUnwateredBuildings, {L(93)})})) == 4);
    CHECK(s2.vars[91] == 50 && s2.vars[92] == 5 && s2.vars[93] == 4);
    CHECK(in2.run(mk({C(cmdGetCurrentDate, {L(0), L(1), L(600)})})) == 0);
    CHECK(in2.run(mk({C(cmdGetRCItaxes, {L(94), L(95), L(96)})})) == 1 && s2.vars[94] == 7 && s2.vars[95] == 8 && s2.vars[96] == 9);
    CHECK(in2.run(mk({C(cmdCountStructuresWithAppearanceInCity, {L(97), L(545184782), L(1406), L(209)})})) == 9 && s2.vars[97] == 9);
    CHECK(in2.run(mk({C(cmdCountStructuresWithAppearanceNearLocation, {L(98), L(1), L(2), L(3), L(4), L(5), L(6)})})) == 8);
    // booleans of the query commands: want = 1 returns t, otherwise NOT t
    CHECK(in2.run(mk({C(cmdIsOrdinanceEnacted, {L(4), L(1)})})) == 1 && in2.run(mk({C(cmdIsOrdinanceEnacted, {L(4), L(0)})})) == 0);
    CHECK(in2.run(mk({C(cmdIsOrdinanceEnacted, {L(5), L(0)})})) == 1);
    CHECK(in2.run(mk({C(cmdIsNeighborConnection, {L(1 + 256), L(2), L(1)})})) == 1 && in2.run(mk({C(cmdIsNeighborConnection, {L(1), L(2), L(0)})})) == 0);
    CHECK(in2.run(mk({C(cmdIsNeighborDealPresent, {L(1), L(2), L(0)})})) == 1);
    CHECK(in2.run(mk({C(cmdIsBusinessDealActive, {L(3), L(1)})})) == 1 && in2.run(mk({C(cmdIsBusinessDealActive, {L(3), L(2)})})) == 0);
    // goals / ranks / end
    in2.run(mk({C(cmdAddGoal, {L(1), L(100), L(2)}), C(cmdAddGoal, {L(1), L(100), L(3)})}));
    CHECK(s2.goals.size() == 1 && s2.goals.at(1).text.id == 3 && !s2.goals.at(1).met);
    CHECK(in2.run(mk({C(cmdMarkGoalStatus, {L(9), L(1)})})) == 0);
    CHECK(in2.run(mk({C(cmdGoalStatusIs, {L(1), L(0)})})) == 1 && in2.run(mk({C(cmdGoalStatusIs, {L(1), L(1)})})) == 0);
    CHECK(in2.run(mk({C(cmdAddRankInfo, {L(1), L(100), L(4)}), C(cmdAddRankInfo, {L(1), L(100), L(5)})})) == 1 && s2.ranks.at(1).id == 5);
    // dialog text: slot 1 main, slot 2 appended paragraphs
    in2.run(mk({C(cmdMakeDialogText, {L(100), L(7), L(1)}), C(cmdMakeDialogText, {L(100), L(8), L(2)}),
                C(cmdSetResultsDlgText, {L(100), L(9), L(1)}), C(cmdSetResultsDlgText, {L(100), L(10), L(2)})}));
    CHECK(s2.dialogText.id == 7 && s2.dialogExtra.size() == 1 && s2.resultsText.id == 9 && s2.resultsExtra.size() == 1);
    // side effects reach the host with their arguments
    h2.log.clear(); h2.params.clear();
    in2.run(mk({C(cmdPopUpMsg, {L(1), L(2), L(1), L(0x628D0C45), L(0), L(4)}), C(cmdShowNewsticker, {L(5), L(6)}), C(cmdPlaySound, {L(33)}),
                C(cmdMoveCamera, {L(10), L(20), L(3)}), C(cmdMoveCamera, {L(10), L(20), L(9)}), C(cmdSetScenarioSpeed, {L(2), L(1)})}));
    CHECK(h2.log.size() == 6 && h2.params[0][3] == 0x628D0C45 && h2.params[0][5] == 4 && h2.params[1][1] == 6 && h2.params[2][0] == 33);
    CHECK(h2.params[3][2] == 3 && h2.params[4][2] == 2 && h2.params[5][0] == 2 && s2.speedLocked);
    CHECK(in2.run(mk({C(cmdMoveCamera, {L(128), L(0), L(1)})})) == 0 && in2.run(mk({C(cmdMoveCamera, {L(-1), L(0), L(1)})})) == 0);
    h2.camOk = false;
    CHECK(in2.run(mk({C(cmdMoveCamera, {L(1), L(1), L(1)})})) == 0);
    h2.log.clear(); h2.params.clear();
    in2.run(mk({C(cmdStartFire, {L(1), L(2)}), C(cmdStartRiot, {L(1), L(2), L(3)}), C(cmdStartEarthquake, {L(25), L(1)}), C(cmdStartLocusts, {L(1)}),
                C(cmdStartUFOAttack, {L(0)}), C(cmdStartWhirlpool, {L(5), L(0)}), C(cmdStartSpaceJunk, {L(6), L(1)}), C(cmdStartToxicCloud, {L(7), L(8), L(1)}),
                C(cmdStartTornado, {L(1), L(2), L(3), L(4), L(5), L(6), L(7), L(8)})}));
    CHECK(h2.log.size() == 9 && h2.log[0] == "disaster0" && h2.log[8] == "disaster8" && h2.params[2][0] == 25 && h2.params[8].size() == 8 && h2.params[8][7] == 8);
    h2.log.clear(); h2.params.clear();
    in2.run(mk({C(cmdEnableDisableDisasters, {L(0)}), C(cmdEnableDisableDisasters, {L(5)}), C(cmdDisableNewNeighborDeals, {L(-1)}), C(cmdEnableNewNeighborDeals, {L(5)}),
                C(cmdDisableNewNeighborDeals, {L(6)}), C(cmdOfferRewardBuilding, {L(19999)}), C(cmdOfferBusinessDeal, {L(0x4A39)}),
                C(cmdEnableDisableBusinessDeal, {L(0x4A3A), L(1)}), C(cmdEnableDisableBusinessDeal, {L(0x4A3A), L(2)})}));
    CHECK(h2.log.size() == 8 && h2.log[0] == "disOff" && h2.log[1] == "disOn" && h2.log[2] == "block" && h2.params[0][0] == -1 && h2.log[3] == "allow");
    CHECK(h2.log[4] == "reward" && h2.log[5] == "offerDeal" && h2.log[6] == "dealOn" && h2.log[7] == "dealOff");
    // end scenario with an undefined rank: -1; later commands are skipped
    ScenarioState s3; s3.active = true;
    ScriptInterpreter in3(s3, st2, h2, rg, noLookup, nullptr);
    in3.run(mk({C(cmdEndScenario, {L(42)}), C(cmdSetVariable, {L(1), L(1)})}));
    CHECK(s3.resultRank == -1 && !s3.active && s3.vars[1] == 0);
    // skipped branches return 1 and have no effect; control flow owns terminators
    ScenarioState s4; s4.active = true;
    ScriptInterpreter in4(s4, st2, h2, rg, noLookup, nullptr);
    Script nest = mk({C(cmdIfElse, {}), C(cmdAndCondition, {}), C(cmdEqual, {L(1), L(1)}), C(cmdBeginBlock, {}), C(cmdSetVariable, {L(1), L(1)}),
                      C(cmdEndBlock, {}), C(cmdEndAnd, {}),
                      C(cmdSetVariable, {L(2), L(2)}), C(cmdSetVariable, {L(3), L(3)}), C(cmdSetVariable, {L(4), L(4)})});
    in4.run(nest);
    CHECK(s4.vars[1] == 1 && s4.vars[2] == 2 && s4.vars[3] == 0 && s4.vars[4] == 4);
    // And short-circuit: later conditions are skipped (no side effects)
    Script sc = mk({C(cmdAndCondition, {}), C(cmdEqual, {L(1), L(2)}), C(cmdSetVariable, {L(5), L(5)}), C(cmdEndAnd, {})});
    CHECK(in4.run(sc) == 0 && s4.vars[5] == 0);
    Script orr = mk({C(cmdOrCondition, {}), C(cmdEqual, {L(1), L(1)}), C(cmdSetVariable, {L(6), L(6)}), C(cmdEndOr, {})});
    CHECK(in4.run(orr) == 1 && s4.vars[6] == 0);
    // arithmetic wraps at 32 bits; percentage multiplies first
    in4.run(mk({C(cmdMultiply, {L(7), L(65536), L(65536)}), C(cmdComputePercentage, {L(8), L(30000000), L(2)})}));
    CHECK(s4.vars[7] == 0 && s4.vars[8] == int32_t(uint32_t(3000000000u)) / 2);
    // months elapsed uses the recorded start date
    s4.startMonth = 11; s4.startYear = 2000;
    in4.run(mk({C(cmdGetMonthsElapsed, {L(9)})}));
    CHECK(s4.vars[9] == (2001 - 2000) * 12 + (3 - 11));
    // random numbers: inclusive range, own generator
    bool lo = false, hi = false;
    for (int i = 0; i < 200; i++) { in4.run(mk({C(cmdGenerateRandomNum, {L(10), L(3), L(5)})})); lo |= s4.vars[10] == 3; hi |= s4.vars[10] == 5; CHECK(s4.vars[10] >= 3 && s4.vars[10] <= 5); }
    CHECK(lo && hi);
    // argc table and timeline order
    CHECK(commandArgc(cmdPopUpMsg) == 6 && commandArgc(cmdCountStructuresWithAppearanceNearLocation) == 7 && commandArgc(kCmdCount) == -1);
    for (int id = 0; id < kCmdCount; id++) CHECK(commandArgc(id) >= 0);
    Script tl = mk({C(cmdIncrement, {L(1)}, 5), C(cmdIncrement, {L(1)}, 9)});
    CHECK(tl.delaysNonDecreasing() && tl.argcConforms());
    tl.cmds[1].delay = 1;
    CHECK(!tl.delaysNonDecreasing());
    tl.cmds[0].args.clear();
    CHECK(!tl.argcConforms());
    // an unknown command evaluates to 0
    CHECK(in4.run(mk({C(200, {})})) == 0);
  }
  TEST_MAIN_END();
}
