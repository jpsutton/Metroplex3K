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
  TEST_MAIN_END();
}
