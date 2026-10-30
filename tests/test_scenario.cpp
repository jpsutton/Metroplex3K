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

  // ---- additions: ticker defaults, portraits, presenters, chooser, results, tokens, templates --
  {
    StyleTable st;
    CHECK(StyleTable::parse("1\r-95955054 , 101 = 2 , 255 , 0 , 0 , 0 , 0 , 0 , 1 , 1\r", st));
    TickerStyle a = st.resolve(101), d = st.resolve(999);
    CHECK(a.cls == 2 && a.r1 == 255 && a.underline && a.repeats == 1);
    CHECK(d.cls == 6 && d.r1 == 255 && d.g1 == 255 && d.b1 == 255 && !d.underline && d.repeats == 1);
    auto c = advisorPortraitCandidates(2, 5, 1, false);
    CHECK(c.size() == 4);
    CHECK(c[0] == 0x10205 && c[1] == 0x0205 && c[2] == 0x0203 && c[3] == 0x10303);
    auto c2 = advisorPortraitCandidates(3, 3, 0, false);
    CHECK(c2.size() == 2 && c2[0] == 0x0303 && c2[1] == 0x10303);
    CHECK(advisorIndex(kAdvisorUtilities) == 1 && advisorIndex(kAdvisorDemographics) == 7 && advisorIndex(1) == 0);
    CHECK(!isScenarioRecordKey({0x2026960B, 0x1234, 1}) && isScenarioRecordKey({0x62B9DA24, 0x6504691D, 150}));
    std::vector<int32_t> v(512, 0); v[4] = 2005;
    ScenarioTextContext tc; tc.vars = &v; tc.paradeName = "Parade"; tc.anyNeighbor = "Elsewhere"; tc.population = 12345;
    CHECK(expandScenarioTokens("%PARADENAME% %ANYNEIGHBOR% %POPULATION% %SCNVY 4% %SCNVM 4%", tc) == "Parade Elsewhere 12,345 2005 $2,005");
    ScenarioTextContext none;
    CHECK(expandScenarioTokens("%PARADENAME%", none) == "%PARADENAME%");
  }
  {
    ScenarioFile f = makeScenario();
    ResolvedPresenter a = f.resolvePresenter(0x828D04EB, 1), b = f.resolvePresenter(5, 1), cu = f.resolvePresenter(137, 0), nf = f.resolvePresenter(999, 0);
    CHECK(a.advisor && a.advisorIdx == 4 && b.advisorId == kAdvisorUtilities && b.advisorIdx == 1);
    CHECK(!cu.advisor && cu.picture == presenterPictureKey(137) && cu.nameString.instance == 0x80 && cu.titleString.instance == 0x81 && cu.nameString.group == f.header.stringTable);
    CHECK(!nf.found && f.defaultPresenter().advisorIdx == 4);
    ScenarioChooserInfo ci = f.chooserInfo();
    CHECK(ci.stringTable == 0xFA47D792u && ci.string1.instance == 1 && ci.string0.instance == 0 && ci.scenarioType == 7 && ci.difficulty == 5 && ci.author == "Jane");
    CHECK(f.entryScriptPresent());
  }
  // win / lose hooks, snapshot and resume at the timeline cursor
  {
    ScenarioFile f = makeScenario();
    f.scripts[1].cmds.push_back(C(cmdMakeDialogText, {L(1), L(2), L(1)}, 2880));
    f.scripts[1].cmds.push_back(C(cmdSetVariable, {L(6), L(66)}, 5000));
    f.scripts[3].cmds = {C(cmdAddGoal, {L(1), L(7), L(8)}), C(cmdMarkGoalStatus, {L(1), L(1)})};
    Stats st; Host host;
    ScenarioRuntime rt(f, st, host, 1);
    rt.start();
    ScenarioResult r0 = rt.result();
    CHECK(!r0.ended && r0.rank == -1 && r0.allGoalsMet && r0.goalsTotal == 0);
    st.dt = {2, 1, 2000};
    rt.dayTick();  // clock 1440: var 5 set
    ScenarioState snap = rt.snapshot();
    CHECK(snap.hasTimeline && snap.cursor == 3 && snap.vars[5] == 42);
    Bytes rec = serializeScenarioState(snap);
    ScenarioState back;
    CHECK(parseScenarioState(rec, back) && back.cursor == 3 && back.timelineScript.instance == 1);
    ScenarioFile saved = f; saved.hasState = true; saved.state = back;
    Stats st2; st2.dt = {2, 1, 2000}; Host host2;
    ScenarioRuntime rt2(saved, st2, host2, 1);
    rt2.start();
    CHECK(rt2.state().vars[5] == 42 && rt2.state().cursor == 3 && rt2.active());
    st2.dt = {1, 2, 2000};  // month change: monthly script marks goal; clock 2880: end fires
    rt2.dayTick();
    ScenarioResult r = rt2.result();
    CHECK(r.ended && r.rank == 1 && r.rankTitle.id == 8 && host2.ends == 1);
    CHECK(r.goalsTotal == 1 && r.goalsMet == 1 && r.allGoalsMet);
    CHECK(rt2.state().vars[6] == 0);  // later timeline command no longer runs after the end
    CHECK(int(ResultsChoice::Reload) == 0 && int(ResultsChoice::OpenLoad) == 1 && int(ResultsChoice::LeaveAndKeepPlaying) == 2);
    ScenarioState fin = rt2.snapshot();
    ScenarioState fo;
    CHECK(parseScenarioState(serializeScenarioState(fin), fo) && fo.resultRank == 1 && fo.goals.at(1).met);
  }
  // corrupt state record fails the whole scenario load (spec 8)
  {
    ScenarioFile f = makeScenario();
    f.hasState = true;
    Bytes img = f.buildStandalone();
    IxfFile ix; CHECK(ix.openMemory(img));
    IxfWriter w;
    for (const auto& e : ix.entries()) {
      Bytes b; ix.getBytes(e.key, b);
      if (e.key == kSnrStateKey) b[4] ^= 0xFF;
      w.add(e.key, b);
    }
    ScenarioFile g; std::string e2;
    CHECK(!ScenarioFile::load(w.build(), g, &e2) && !e2.empty());
  }
  // text record conventions: LF ignored, text after ';' ignored, signed IDs
  {
    ScenarioHeader h;
    CHECK(ScenarioHeader::parse("SNR\n\rC:\\a.sc3\n\r601861727   601861772   7 ; entry\n\r-95955054 1\n\r-2104687381 1\n\r-95955054\n\r", h));
    CHECK(h.entry.instance == 7 && h.presenter == 0x828D04EBu && h.stringTable == 0xFA47D792u);
    PresenterTable pt;
    CHECK(!PresenterTable::parse("1\r1 50 51 52\r", pt));
  }
  // template -> scenario
  {
    SscProject p;
    p.name = u"Tmpl"; p.description = u"Desc"; p.picture = {'B', 'M'};
    p.settings[2] = u"-2104687381"; p.settings[3] = u"Main"; p.settings[4] = u"C:\\x.sc3"; p.settings[5] = u"7"; p.settings[6] = u"5";
    p.scripts = {{100, u"Other", u"Zed", u""}, {200, u"Main", u"Zed", u""}};
    p.goals.push_back({50, u"goal text", 1, u"", u"", -1});
    p.ranks.push_back({51, u"rank text", 1, u"", u""});
    SscCommand a; a.position = 1; a.scriptId = 200; a.cmdId = cmdRepeatRegular; a.args = {{u"Constant", u"0"}, {u"Constant", u"100"}};
    SscCommand b; b.position = 2; b.scriptId = 200; b.cmdId = cmdAddGoal; b.args = {{u"Constant", u"1"}, {u"Constant", u"50"}};
    SscCommand c; c.position = 1; c.scriptId = 100; c.cmdId = cmdSubRoutine; c.args = {{u"Constant", u"200"}};
    p.commands = {b, a, c};
    ScenarioFile out; std::string err;
    CHECK(p.toScenario(0xFA47D792u, out, &err));
    CHECK(out.header.entry.instance == 2 && out.header.presenterKind == 1 && out.header.presenter == 0x828D04EBu && out.header.stringTable == 0xFA47D792u);
    const Script* e = out.script(2);
    CHECK(e && e->cmds.size() == 2 && e->cmds[0].args.size() == 3 && e->cmds[0].args[1].value == int32_t(601861772) && e->cmds[0].args[2].value == 1);
    CHECK(e->cmds[1].args.size() == 3 && e->cmds[1].args[1].value == int32_t(0xFA47D792u) && e->cmds[1].args[2].value == 50 && e->argcConforms());
    const Script* o = out.script(1);
    CHECK(o && o->cmds[0].args.size() == 2 && o->cmds[0].args[1].value == 2);
    CHECK(out.strings.at(50) == "goal text" && out.strings.at(1) == "Tmpl" && out.catalogue.scenarioType == 7 && out.catalogue.difficulty == 5 && out.picture.size() == 2);
    ScenarioFile again;
    CHECK(ScenarioFile::load(out.buildStandalone(), again) && again.scripts.size() == 2 && again.catalogue.difficulty == 5);
    SscProject empty;
    CHECK(!empty.toScenario(1, out, &err));
  }
  TEST_MAIN_END();
}
