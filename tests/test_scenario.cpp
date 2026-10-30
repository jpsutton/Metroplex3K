#include "core/scenario.h"
#include "testutil.h"
using namespace m3k;

struct Stats : ICityStats {
  GameDate dt{1, 1, 2000};
  int32_t pop = 0;
  int32_t population() const override { return pop; }
  int32_t landValue() const override { return 0; }
  int32_t crime() const override { return 0; }
  int32_t aura() const override { return 0; }
  int32_t pollution(int) const override { return 0; }
  int32_t funds() const override { return 0; }
  int32_t totalDebt() const override { return 0; }
  int32_t taxRate(int) const override { return 0; }
  int32_t trafficDensity() const override { return 0; }
  int32_t surplusPower() const override { return 0; }
  int32_t surplusWater() const override { return 0; }
  int32_t unpoweredBuildings() const override { return 0; }
  int32_t unwateredBuildings() const override { return 0; }
  GameDate date() const override { return dt; }
  void mapSize(int& w, int& h) const override { w = h = 64; }
  int32_t countStructures(uint32_t) const override { return 0; }
  int32_t countStructuresNear(uint32_t, int, int, int) const override { return 0; }
  int32_t countAppearance(uint32_t, uint32_t, uint32_t) const override { return 0; }
  int32_t countAppearanceNear(uint32_t, uint32_t, uint32_t, int, int, int) const override { return 0; }
  bool ordinanceEnacted(int) const override { return false; }
  bool neighborConnection(int, int) const override { return false; }
  bool neighborDealPresent(int, int) const override { return false; }
  bool businessDealActive(int) const override { return false; }
};
struct Host : IScenarioHost {
  int popups = 0, ends = -99, tickers = 0;
  void popUp(const PopUpRequest&) override { popups++; }
  void newsTicker(uint32_t, uint32_t) override { tickers++; }
  void endScenario(int r) override { ends = r; }
};

static ScriptCommand C(int cmd, std::initializer_list<ScriptArg> a, uint32_t delay = 0) {
  ScriptCommand c; c.cmd = uint16_t(cmd); c.delay = delay; c.args = a; return c;
}
static ScriptArg L(int32_t v) { return {0, v}; }
static ScriptArg V(int32_t v) { return {1, v}; }

static ScenarioFile makeScenario() {
  ScenarioFile f;
  f.header.sourcePath = "C:\\x\\y.sc3";
  f.header.entry.instance = 1;
  f.header.title = {0xFA47D792u, 1};
  f.header.presenter = 0x828D04EB;
  f.header.presenterKind = 1;
  f.header.stringTable = 0xFA47D792u;
  // entry: at t=0 register daily(0)->script 2, monthly(2)->3; at t=1440 set var 5=42; at t=2880 end
  Script e;
  e.cmds = {C(cmdRepeatRegular, {L(0), L(601861772), L(2)}), C(cmdRepeatRegular, {L(2), L(601861772), L(3)}),
            C(cmdAddRankInfo, {L(1), L(7), L(8)}),
            C(cmdSetVariable, {L(5), L(42)}, 1440), C(cmdEndScenario, {L(1)}, 2880)};
  f.scripts[1] = e;
  Script daily; daily.cmds = {C(cmdIncrement, {L(10)})};
  f.scripts[2] = daily;
  Script monthly; monthly.cmds = {C(cmdIncrement, {L(11)}), C(cmdSubRoutine, {L(601861772), L(4)})};
  f.scripts[3] = monthly;
  Script sub; sub.cmds = {C(cmdIncrement, {L(12)})};
  f.scripts[4] = sub;
  TickerStyle ts; ts.cls = 2; ts.r1 = 255; ts.g1 = 10; ts.repeats = 1;
  f.styles.byString[0x65] = ts; f.styles.groups[0x65] = 0xFA47D792u;
  f.presenters.rows = {{1, 0x32, 0x33}, {137, 0x80, 0x81}};
  f.catalogue = {0xFA47D792u, 7, 5};
  f.hasCatalogue = true;
  f.shortAuthor = "Maxis";
  f.author = "Jane";
  f.strings[0x65] = "Hello %SCNNAME%";
  return f;
}

int main() {
  // header
  {
    ScenarioHeader h;
    CHECK(ScenarioHeader::parse("SNR\rC:\\a.sc3\r601861727 601861772 3\r-95955054 1\r-2104687381 1 ;c\r-95955054\r", h));
    CHECK(h.entry.type == 0x23DFAE5F && h.entry.group == 0x23DFAE8C && h.entry.instance == 3);
    CHECK(h.title.group == 0xFA47D792u && h.title.id == 1 && h.stringTable == 0xFA47D792u);
    CHECK(h.presenter == 0x828D04EBu && h.presenterKind == 1);
    ScenarioHeader h2;
    CHECK(ScenarioHeader::parse(h.serialize(), h2));
    CHECK(h2.entry == h.entry && h2.presenter == h.presenter && h2.sourcePath == h.sourcePath);
    h.presenter = 5;
    CHECK(h.effectivePresenter() == kAdvisorUtilities);
    CHECK(!ScenarioHeader::parse("SNR\r", h2));
  }
  // style, presenters, catalogue
  {
    StyleTable st;
    CHECK(StyleTable::parse("1\r-95955054 , 101 = 2 , 255 , 255 , 255 , 0 , 0 , 0 , 1 , 1\r", st));
    const TickerStyle* t = st.find(101);
    CHECK(t && t->cls == 2 && t->r1 == 255 && t->underline && t->repeats == 1);
    CHECK(!st.find(5));
    StyleTable s2;
    CHECK(StyleTable::parse(st.serialize(), s2) && s2.find(101));
    PresenterTable pt;
    CHECK(PresenterTable::parse("2\r1 50 51\r2 53 54\r", pt) && pt.rows.size() == 2 && pt.find(2)->name == 53);
    PresenterTable bad;
    CHECK(!PresenterTable::parse("2\r1 50 51\r2 53 54 55\r", bad) && bad.rows.size() == 1);
    Catalogue c{1, 7, 5}, d;
    CHECK(Catalogue::parse(c.serialize(), d) && d.scenarioType == 7 && d.difficulty == 5);
  }
  CHECK(advisorPortraitInstance(advisorIndex(kAdvisorBudget), 0, 1, true) == 0x01010203);
  // state record
  {
    ScenarioState s;
    s.clock = 1440; s.startMonth = 3; s.startYear = 2001; s.vars[7] = -5; s.vars[511] = 9;
    ScriptRef r; r.instance = 3;
    s.repeat[1].push_back(r);
    s.hasTimeline = true; s.cursor = 2; s.timelineScript.instance = 1;
    s.presenter = 0x828D04EB; s.presenterKind = 1;
    s.goals[4] = Goal{{5, 6}, true};
    s.ranks[2] = {9, 10};
    s.resultRank = -1;
    s.dialogText = {11, 12};
    s.speedLocked = true;
    Bytes b = serializeScenarioState(s);
    CHECK(rd32(&b[4]) == 0xDEADBEEF);
    // example from spec: repeat-list entry bytes
    ScenarioState o;
    CHECK(parseScenarioState(b, o));
    CHECK(o.clock == 1440 && o.startYear == 2001 && o.vars[7] == -5 && o.vars[511] == 9);
    CHECK(o.repeat[1].size() == 1 && o.repeat[1][0].instance == 3 && o.hasTimeline && o.cursor == 2);
    CHECK(o.goals.at(4).met && o.goals.at(4).text.id == 6 && o.ranks.at(2).id == 10 && o.dialogText.id == 12 && o.speedLocked);
    Bytes bad = b; bad[4] = 0;
    CHECK(!parseScenarioState(bad, o));
    Bytes trunc(b.begin(), b.begin() + 100);
    CHECK(!parseScenarioState(trunc, o));
    s.hasTimeline = false;
    CHECK(parseScenarioState(serializeScenarioState(s), o) && !o.hasTimeline);
  }
  // tokens
  {
    std::vector<int32_t> vars(512, 0); vars[3] = 1234;
    ScenarioTextContext c; c.scenarioName = "Test"; c.vars = &vars; c.mayor = "Bob"; c.year = 1999;
    CHECK(expandScenarioTokens("%SCNNAME% by %MAYOR% %SCNVN 3% %YEAR% 100%", c) == "Test by Bob 1234 1999 100%");
    CHECK(expandScenarioTokens("%FOO% x", c) == "%FOO% x");
  }
  // file round trip
  ScenarioFile f = makeScenario();
  Bytes snr = f.buildStandalone();
  ScenarioFile g;
  std::string err;
  CHECK(ScenarioFile::load(snr, g, &err));
  CHECK(g.header.entry.instance == 1 && g.scripts.size() == 4 && g.scripts[1] == f.scripts[1]);
  CHECK(g.styles.find(0x65) && g.presenters.rows.size() == 2 && g.hasCatalogue && g.catalogue.difficulty == 5);
  CHECK(g.shortAuthor == "Maxis" && g.author == "Jane" && g.strings.at(0x65) == "Hello %SCNNAME%" && !g.hasState);
  {
    ScenarioFile none;
    IxfWriter w; w.add({1, 2, 3}, Bytes{1, 2, 3});
    CHECK(!ScenarioFile::load(w.build(), none));
    // merge into a "city"
    Bytes merged = f.mergeInto(w.build());
    ScenarioFile m;
    CHECK(ScenarioFile::load(merged, m));
    IxfFile ix; CHECK(ix.openMemory(merged) && ix.has({1, 2, 3}));
    CHECK(isScenarioRecordKey({0x23DFAE5F, 0x23DFAE8C, 9}) && !isScenarioRecordKey({1, 2, 3}));
  }
  // runtime
  {
    Stats st; Host host;
    ScenarioRuntime rt(g, st, host, 1);
    rt.start();
    CHECK(rt.active() && rt.state().repeat[0].size() == 1 && rt.state().repeat[2].size() == 1);
    CHECK(rt.state().ranks.count(1) && rt.state().startYear == 2000);
    CHECK(rt.state().vars[5] == 0);
    // day 1: first dayTick advances clock to 1440, day changes -> daily runs; var 5 set by timeline.
    st.dt = {2, 1, 2000};
    rt.dayTick();
    CHECK(rt.state().clock == 1440 && rt.state().vars[5] == 42 && rt.state().vars[10] == 1);
    // same day again: no clock change, no daily run
    rt.dayTick();
    CHECK(rt.state().clock == 1440 && rt.state().vars[10] == 1);
    // new month: monthly script + subroutine
    st.dt = {1, 2, 2000};
    rt.dayTick();
    CHECK(rt.state().clock == 2880 && rt.state().vars[11] == 1 && rt.state().vars[12] == 1);
    CHECK(host.ends == 1 && !rt.active());
    int before = rt.state().vars[10];
    st.dt = {2, 2, 2000};
    rt.dayTick();
    CHECK(rt.state().vars[10] == before);
    // resume from state
    ScenarioFile h = g;
    h.hasState = true;
    h.state = rt.state();
    h.state.hasTimeline = true; h.state.cursor = 3; h.state.timelineScript.instance = 1; h.state.clock = 100;
    Bytes saved = h.buildStandalone();
    ScenarioFile r;
    CHECK(ScenarioFile::load(saved, r) && r.hasState && r.state.cursor == 3);
    Stats st2; Host host2;
    ScenarioRuntime rt2(r, st2, host2, 1);
    rt2.start();
    CHECK(rt2.state().ranks.count(1) && rt2.state().cursor == 3);
  }
  // minute ticks / weekly list
  {
    ScenarioFile w = makeScenario();
    w.scripts[1].cmds = {C(cmdRepeatRegular, {L(1), L(601861772), L(2)})};
    Stats st; Host host;
    ScenarioRuntime rt(w, st, host, 1);
    rt.start();
    st.dt = {7, 1, 2000};
    rt.minuteTick();
    CHECK(rt.state().clock == 1 && rt.state().vars[10] == 1);
    st.dt = {8, 1, 2000};
    rt.minuteTick();
    CHECK(rt.state().vars[10] == 1);
    st.dt = {14, 1, 2000};
    rt.dayTick();
    CHECK(rt.state().vars[10] == 2);
    CHECK(rt.state().clock == 1440);
  }
  // SSC
  {
    SscProject p;
    p.name = u"Name"; p.description = u"Desc \u00e9"; p.pictureName = u"p.bmp";
    p.picture = {'B', 'M', 1, 2};
    p.cityName = u"c.sc3"; p.city = {9, 9, 9};
    p.nextId = 138;
    p.settings[3] = u"Main";
    p.messages.push_back({5, u"n", u"text", u"lbl"});
    p.tickers.push_back({6, u"l", u"n", u"t", u"a", u"b", 3});
    p.variables.push_back({1, u"v", u"d"});
    p.goals.push_back({7, u"goal", 1, u"", u"", -1});
    p.ranks.push_back({8, u"rank", 1, u"", u""});
    p.scripts.push_back({100, u"Main", u"me", u""});
    SscCommand c1; c1.position = 2; c1.scriptId = 100; c1.delay = 0; c1.cmdId = cmdSetVariable;
    c1.args = {{u"Constant", u"3"}, {u"Variable", u"4"}};
    SscCommand c0; c0.position = 1; c0.scriptId = 100; c0.cmdId = cmdIncrement; c0.args = {{u"Constant", u"3"}};
    p.commands = {c1, c0};
    Bytes b = p.serialize();
    Bytes stale = b; stale.insert(stale.end(), 50, 0xAA);
    SscProject q;
    CHECK(SscProject::parse(stale, q, &err));
    CHECK(q.sectionLengthOk && q.trailingBytes == 50 && q.name == u"Name" && q.description == u"Desc \u00e9");
    CHECK(q.picture == p.picture && q.city == p.city && q.nextId == 138 && q.settings[3] == u"Main");
    CHECK(q.goals.size() == 1 && q.goals[0].x == -1 && q.tickers[0].c == 3 && q.commands.size() == 2);
    Script s = q.compileScript(100);
    CHECK(s.cmds.size() == 2 && s.cmds[0].cmd == cmdIncrement && s.cmds[1].args[1].kind == 1 && s.cmds[1].args[1].value == 4);
    CHECK(utf16ToUtf8(q.description) == "Desc \xC3\xA9" && utf8ToUtf16("Desc \xC3\xA9") == q.description);
    Bytes bad = b; bad[0] = 0;
    CHECK(!SscProject::parse(bad, q));
    Bytes cut(b.begin(), b.begin() + b.size() / 2);
    CHECK(!SscProject::parse(cut, q));
  }
  TEST_MAIN_END();
}
