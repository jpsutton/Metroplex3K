// Tests for advisors, news ticker and petitioners (spec/22).
#include <algorithm>
#include "core/advisors.h"
#include "core/news.h"
#include "core/petitioners.h"
#include "testutil.h"

using namespace m3k;

namespace {
struct FakeSink : TickerSink {
  uint32_t next = 1;
  std::map<uint32_t, std::pair<uint32_t, uint32_t>> live;
  std::set<std::pair<uint32_t, uint32_t>> strings;
  bool all = true;
  uint32_t post(uint32_t t, uint32_t id, bool, int, int) override {
    if (!hasString(t, id)) return 0;
    live[next] = {t, id}; return next++;
  }
  void withdraw(uint32_t item) override { live.erase(item); }
  bool hasString(uint32_t t, uint32_t id) override { return all || strings.count({t, id}); }
};

struct FakeDeals : DealHandler {
  bool active[6] = {}, elig[6] = {}, disabled = false, valid = true;
  int gen = 0, cleared = 0;
  bool dealActive(int t) override { return active[t]; }
  int dealAgeMonths(int) override { return 30; }
  bool eligibleNeighbour(int t) override { return elig[t]; }
  bool offersDisabled() override { return disabled; }
  bool offerStillValid(int) override { return valid; }
  bool generateOffer(int) override { ++gen; return true; }
  void clearOffer(int) override { ++cleared; }
};
}  // namespace

static void testTopicList() {
  FakeSink sink;
  TopicList L(kAdvBudget);
  L.setSink(&sink);
  // plain condition topic: stays while raised each cycle, deleted when not
  L.raise(0x38, 0x82A493A5);
  CHECK(L.listed(0x38) && sink.live.size() == 1);
  L.update();
  CHECK(L.listed(0x38));
  L.update();                       // not raised this cycle -> deleted, ticker withdrawn
  CHECK(!L.listed(0x38) && sink.live.empty());
  // key group exclusivity
  L.raise(0x38, 0x82A493A5);
  L.raise(0x39, 0x82A493A5);
  CHECK(!L.listed(0x38) && L.listed(0x39));
  L.update();
  // re-raise sets R only
  L.raise(0x39, 0x82A493A5);
  CHECK(L.topics().size() == 1);
  L.update(); CHECK(L.listed(0x39));
  // event topic (P=E=1): hidden after L cycles but never removed by the update
  TopicList E(kAdvPlanner);
  E.event(0xD1, 3);
  for (int i = 0; i < 3; ++i) { E.update(); CHECK(!E.find(0xD1)->hidden()); }
  E.update();
  CHECK(E.listed(0xD1) && E.find(0xD1)->hidden());
  // raising an expiring instance again replaces it with a fresh one
  E.event(0xD1, 3);
  CHECK(E.find(0xD1)->counter == 0 && !E.find(0xD1)->hidden());
  // a pure expiring topic (E only) is deleted after L cycles
  TopicList X(kAdvPlanner);
  X.raise(0xD2, 0, 2, false, true);
  X.update(); X.update(); X.update();
  CHECK(!X.listed(0xD2));
  // persistent topic hides after L, stays listed
  TopicList P(kAdvPlanner);
  P.persistentTopic(0xC8, 2);
  P.update(); P.update(); P.update();
  CHECK(P.listed(0xC8) && P.find(0xC8)->hidden());
  CHECK(P.visibleTopics().empty());
  // dismissal: L = 0 condition topic cannot be dismissed
  TopicList D(kAdvBudget);
  D.raise(0x3C); CHECK(!D.dismiss(0x3C));
  D.raise(0x3D, 0, 3); CHECK(D.dismiss(0x3D)); CHECK(D.find(0x3D)->hidden());
  // dismissed condition topic re-appears after L cycles while raised
  for (int i = 0; i < 3; ++i) { D.update(); D.raise(0x3D, 0, 3); }
  D.update();
  CHECK(!D.find(0x3D)->hidden());
  // mood
  MoodTable mt; mt.set(1, 1); mt.set(2, 5);
  TopicList M(kAdvBudget);
  CHECK(M.mood(mt) == 3);
  M.raise(1); M.raise(2);
  CHECK(M.mood(mt) == 3);
  M.raise(0x99);   // unknown mood -> 0
  CHECK(M.mood(mt) == 2);
  // forced topics
  M.forced.push_back(0x77); M.update(); M.raiseForced();
  CHECK(M.listed(0x77));
  // ticker text missing -> no ticker item
  FakeSink none; none.all = false;
  TopicList N(kAdvBudget); N.setSink(&none); N.raise(5);
  CHECK(N.find(5)->tickerItem == 0);
}

static void testTopicRecord() {
  TopicList L(kAdvPlanner);
  L.event(0xD1, 4);
  L.raise(0x2B, 0xE2A4935D, 0, false, false, false, true, 12, 34);
  L.persistentTopic(0xC8, 8);
  L.raise(0xBF);
  Bytes b = L.save();
  TopicList R(kAdvPlanner);
  CHECK(R.load(b));
  CHECK(R.save() == b);
  CHECK(R.topics().size() == 4);
  CHECK(R.find(0x2B)->args.size() == 2 && R.find(0x2B)->args[1].value == 34);
  // empty list = 24 bytes
  TopicList Z(kAdvBudget);
  CHECK(Z.save().size() == 24);
  Bytes bad = b; bad[4] = 0;
  CHECK(!R.load(bad));
  bad = b; bad.pop_back();
  CHECK(!R.load(bad));
  CHECK(R.find(0xD1)->flags & kFlagExpiring);
  CHECK(rd32(b.data() + 8) == 1 && rd32(b.data() + 16) == 1 && rd32(b.data() + 20) == 1);
}

static void testTicker() {
  TickerDb db;
  IniFile f = IniFile::fromText("[Sc3TickerData]\n0x42C1ED2D,0x64=1,255,0,0,255,0,255,1,2\n"
                                "0x42C1ED2D,0x64=3,0,0,0,255,0,255,0,1\n"
                                "0xC2498F0E,0x22=7,10,20,30,255,0,255,0,1\n0xC2498F0E,0x23=7,10,20,30,255,0,255,0,1\n");
  db.addFile(f);
  db.addFile(f);   // duplicates keep first definition but extend the pool
  CHECK(db.style(0x42C1ED2D, 0x64).cls == 1 && db.style(0x42C1ED2D, 0x64).repeats == 2);
  CHECK(db.style(0x42C1ED2D, 0x64).underline);
  CHECK(db.pool().size() == 4);
  CHECK(db.style(1, 2).cls == 6 && db.style(1, 2).r == 255);
  IniFile nc = IniFile::fromText("[TickerData Files]\nentry=SC3TickerData.INI\nentry=Gold.INI\n");
  CHECK(TickerDb::fileList(nc).size() == 2);

  NewsTicker t;
  t.setDb(&db);
  t.setTextFn([](uint32_t, uint32_t id) { return std::string("msg") + std::to_string(id); });
  GameRng rng(3);
  int32_t today = tickerStamp(5, 10, 2000);
  CHECK(tickerStamp(1, 1, 1900) == (1 + 12 * 1900) * 30 + 1);
  CHECK(tickerStamp(1, 31, 2000) == tickerStamp(1, 30, 2000) + 1);
  CHECK(tickerStamp(2, 1, 2000) == tickerStamp(1, 31, 2000));   // day 31 and day 1 share a stamp
  uint32_t n1 = t.post(0x42C1ED2D, 0x64, today);   // class 1, repeats 2
  CHECK(n1 != 0 && t.queueSize(1) == 1);
  NewsItem a = t.next(today, rng);
  CHECK(a.number == n1 && t.queueSize(1) == 1);     // repeats 2 -> copy re-queued
  NewsItem b = t.next(today, rng);
  CHECK(b.number == n1 && t.queueSize(1) == 0);     // class 1 head shown even if equal to last
  NewsItem c = t.next(today, rng);
  CHECK(c.filler);

  TickerDb d2;
  d2.addFile(IniFile::fromText("[Sc3TickerData]\n1,1=2,1,1,1,0,0,0,0,0\n1,2=3,1,1,1,0,0,0,0,1\n1,3=4,1,1,1,0,0,0,0,1\n"
                               "1,4=5,1,1,1,0,0,0,0,1\n1,5=6,1,1,1,0,0,0,0,1\n"));
  NewsTicker t2; t2.setDb(&d2); t2.setTextFn([](uint32_t, uint32_t) { return std::string("x"); });
  uint32_t q2 = t2.post(1, 1, today);   // class 2, repeats 0 -> repeats forever
  NewsItem s1 = t2.next(today, rng); CHECK(s1.number == q2);
  NewsItem s2 = t2.next(today, rng); CHECK(s2.filler);   // head == last, nothing else
  uint32_t q3 = t2.post(1, 2, today);
  CHECK(t2.queueSize(3) == 1);
  t2.next(today + 91, rng);
  CHECK(t2.queueSize(3) == 0);
  (void)q3;
  uint32_t q4 = t2.post(1, 3, today);
  NewsItem s4 = t2.next(today, rng);        // step 5a: young class-4 item shown
  CHECK(s4.number == q4);
  // priority: class 2 before class 3
  NewsTicker t3; t3.setDb(&d2); t3.setTextFn([](uint32_t, uint32_t) { return std::string("x"); });
  uint32_t i3 = t3.post(1, 2, today), i2 = t3.post(1, 1, today);
  NewsItem o1 = t3.next(today, rng);
  CHECK(o1.number == i2);
  NewsItem o2 = t3.next(today, rng);
  CHECK(o2.number == i3 || o2.filler);
  // withdraw and queue cap
  NewsTicker t4; t4.setDb(&d2); t4.setTextFn([](uint32_t, uint32_t) { return std::string("x"); });
  uint32_t w1 = t4.post(1, 5, today);
  CHECK(t4.withdraw(w1) && t4.queueSize(6) == 0 && !t4.withdraw(w1));
  for (int i = 0; i < 70; ++i) t4.post(1, 5, today);
  CHECK(t4.queueSize(6) == 64);
  t4.clear(); CHECK(t4.queueSize(6) == 0);
  NewsTicker t5; t5.setDb(&d2); t5.setTextFn([](uint32_t, uint32_t) { return std::string(); });
  CHECK(t5.post(1, 5, today) == 0);
  NewsTicker t6; t6.setDb(&d2); t6.setTextFn([](uint32_t, uint32_t) { return std::string("y"); });
  t6.post(1, 5, today, true, 7, 8);
  NewsItem loc = t6.next(today, rng);
  CHECK(loc.underline && loc.hasLoc && loc.locX == 7 && loc.locZ == 8);
  CHECK(TickerScroll::advance(51) == 2 && TickerScroll::advance(50) == 0);
  CHECK(NewsTicker::expiryDays(3) == 90 && NewsTicker::expiryDays(6) == 270 && NewsTicker::expiryDays(1) == 1080);
  // sink adapter
  NewsTicker t7; t7.setDb(&d2);
  NewsTickerSink adapter(t7, [&] { return today; });
  adapter.text = [](uint32_t, uint32_t) { return std::string("z"); };
  t7.setTextFn(adapter.text);
  uint32_t item = adapter.post(1, 5, false, 0, 0);
  CHECK(item != 0 && t7.queueSize(6) == 1);
  adapter.withdraw(item);
  CHECK(t7.queueSize(6) == 0);
}

static void testEventNews() {
  GameRng r(1);
  for (auto t : allDisasterTypes()) {
    NewsKey k = disasterStartNews(t, r, true);
    if (t == DisasterType::Parade) CHECK(!k.valid()); else CHECK(k.valid());
  }
  NewsKey tor = disasterStartNews(DisasterType::Tornado, r, false);
  CHECK(tor.table == kTblNewsTriggered && tor.id >= 0x25D && tor.id <= 0x25F);
  NewsKey tox = disasterStartNews(DisasterType::ToxicCloud, r, false);
  CHECK(tox.table == kTblNewsTriggeredMBE && tox.id >= 0x67 && tox.id <= 0x69);
  NewsKey fire1 = disasterStartNews(DisasterType::Fire, r, true), fire2 = disasterStartNews(DisasterType::Fire, r, false);
  CHECK(fire1.id >= 0x27C && fire1.id <= 0x27F && fire2.id >= 0x280 && fire2.id <= 0x281);
  CHECK(buildingNews(12210, 1, false, r).id == 0x20A && buildingNews(12210, 10, false, r).id == 0x20B);
  CHECK(!buildingNews(12210, 5, false, r).valid());
  CHECK(buildingNews(13010, 1, false, r).id == 0x2B6 && !buildingNews(13010, 2, false, r).valid());
  CHECK(buildingNews(10110, 7, false, r).id == 0x2A4);
  CHECK(buildingNews(99999, 1, true, r).id >= 0x1FC && buildingNews(99999, 1, true, r).id <= 0x207);
  CHECK(!buildingNews(99999, 1, false, r).valid());
  auto n7 = cityStartNews(7, 7, r);
  CHECK(n7.size() == 15);
  CHECK(n7.front().id == 0x64 && n7.back().id == 0x72);
  auto n3 = cityStartNews(3, 0, r);
  CHECK(n3.size() == 2 && n3[0].id >= 0x2ED && n3[0].id <= 0x2F2 && n3[1].id == 0x2F9);
  CHECK(cityStartNews(1, 0, r).size() == 1);
  CHECK(anniversaryNews(10).id == 0x1F4 && anniversaryNews(2000).id == 0x1FB && !anniversaryNews(11).valid());
  CHECK(cheatNews(0).id == 0x325 && cheatNews(28).id == 0x341 && !cheatNews(29).valid());
}

static void testPetitioners() {
  FakeSink sink;
  PetitionerManager pm;
  pm.setSink(&sink);
  IniFile ini = IniFile::fromText(
      "[PETITIONER_ORDINANCE_TRIGGER_MAPPINGS]\nSHUTTLE_SERVICE_YEAR=1970\nPOPULATION_FOR_GAMBLING=5000;\n"
      "[PETITIONER_ADVICE_MAPPING]\n16=0x828D04EB;\n24=0x022E288E;\n[PETITIONER_STRINGS_TICKER]\nBASE=0x026DD322\nHOL=0x649B17DB\n"
      "[PETITIONER_STRINGS_TICKER_X]\nBASE=0xE27EBF4D\n");
  pm.config.loadManagerIni(ini);
  CHECK(pm.config.threshold("SHUTTLE_SERVICE_YEAR") == 1970 && pm.config.threshold("POPULATION_FOR_GAMBLING") == 5000);
  CHECK(pm.config.adviceAdvisor(16) == 0x828D04EB && pm.config.adviceAdvisor(99) == 0);
  CHECK(pm.config.tables(PetitionText::Ticker).size() == 2);
  pm.config.addExtraInfo(IniFile::fromText("[PetitionerExtraInfo]\n0x00000005=12,7\n"));
  CHECK(pm.config.extraInfo(5).portrait == 12 && pm.config.extraInfo(5).second == 7 && !pm.config.extraInfo(6).found);

  pm.request(0x14, 0, 48);
  CHECK(pm.pending(0x14) && sink.live.size() == 1);
  pm.update();
  CHECK(pm.pending(0x14));           // raised once -> survives this update
  pm.update();
  CHECK(!pm.pending(0x14) && sink.live.empty());   // not re-requested -> deleted, ticker withdrawn
  // answering hides it; plain request keeps it as cool-down marker for L cycles
  pm.request(0x14, 0, 3); pm.update();
  PetitionEffect e = pm.answer(0x14, true);
  CHECK(pm.find(0x14)->hidden() && e.kind == PetitionEffect::None);
  for (int i = 0; i < 3; ++i) { pm.request(0x14, 0, 3); pm.update(); }
  CHECK(pm.pending(0x14) && pm.find(0x14)->hidden());
  pm.request(0x14, 0, 3); pm.update();
  CHECK(!pm.pending(0x14));          // cool-down over (C >= L) -> deleted
  // persistent with L = 0 never hides
  pm.request(0x84, 13874, 0, true);
  for (int i = 0; i < 10; ++i) { pm.update(); pm.request(0x84, 13874, 0, true); }
  CHECK(pm.visible(0x84));
  // expiring
  pm.request(0x61, 0, 2, false, true);
  pm.update(); pm.update(); pm.update();
  CHECK(!pm.pending(0x61));
  // answer effects
  PetitionerManager q;
  q.request(0x69, 0, 120, false, true);
  e = q.answer(0x69, true);
  CHECK(e.kind == PetitionEffect::SetOrdinance && e.ordinance == 0xA0D07129 && e.value == 1);
  e = q.answer(0x68, true);
  CHECK(e.kind == PetitionEffect::SetOrdinance && e.value == 0);
  e = q.answer(0x69, false); CHECK(e.kind == PetitionEffect::None);
  e = q.answer(0x84, true); CHECK(e.kind == PetitionEffect::RewardAvailable && e.buildingType == 13874);
  e = q.answer(0x84, false); CHECK(e.kind == PetitionEffect::RewardAvailable && e.buildingType == 0);
  e = q.answer(0xD7, true); CHECK(e.kind == PetitionEffect::Treasury && e.treasuryDelta == 100000);
  e = q.answer(0xD7, false); CHECK(e.kind == PetitionEffect::PlannerFlag);
  e = q.answer(0x12D, true); CHECK(e.kind == PetitionEffect::StartParade);
  e = q.answer(0xE9, true); CHECK(e.kind == PetitionEffect::NeighbourDeal && e.voiceSound == 0x9B);
  e = q.answer(0xEE, false); CHECK(e.voiceSound == 0xA4);
  e = q.answer(0x95, true); CHECK(e.kind == PetitionEffect::BusinessDeal);
  e = q.answer(0xA7, true); CHECK(e.kind == PetitionEffect::NeighbourDeal);
  e = q.answer(0x9B, true); CHECK(e.kind == PetitionEffect::WorkerStrike);
  e = q.answer(0xCC, true); CHECK(e.kind == PetitionEffect::PublicSafetyStrike);
  e = q.answer(0xA3, true); CHECK(e.kind == PetitionEffect::TransitStrike);
  e = q.answer(0x01, true); CHECK(e.kind == PetitionEffect::RewardAvailable);
  e = q.answer(0x2222, true); CHECK(e.kind == PetitionEffect::None);
  uint32_t ord; int val;
  CHECK(petitionOrdinanceAction(0xF4, ord, val) && ord == 0xC2BF1DD1);
  CHECK(!petitionOrdinanceAction(0x84, ord, val));
  // ordinance petition generation: only with pop >= 100, 1/100 chance
  PetitionerManager g;
  g.config.setThreshold("SHUTTLE_SERVICE_YEAR", 1970);
  PetitionerInputs in; in.population = 50; in.year = 2000;
  in.ordinances[0xA2BF1DE5] = 1;
  GameRng rng(1);
  for (int i = 0; i < 2000; ++i) g.generateOrdinancePetitions(in, rng);
  CHECK(!g.pending(0x73));
  in.population = 500;
  int got = 0;
  for (int i = 0; i < 3000 && !g.pending(0x73); ++i) { g.generateOrdinancePetitions(in, rng); ++got; }
  CHECK(g.pending(0x73) && got > 5);
  CHECK(g.find(0x73)->limit == 120 && g.find(0x73)->expiring());
  PetitionerManager g2; g2.config.setThreshold("SHUTTLE_SERVICE_YEAR", 2100);
  for (int i = 0; i < 3000; ++i) g2.generateOrdinancePetitions(in, rng);
  CHECK(!g2.pending(0x73));
  // record round trip
  PetitionerManager r;
  r.request(0x10, 7, 12, false, false, true, 3, 4);
  r.request(0x84, 13874, 0, true);
  r.request(0x61, 0, 120, false, true);
  r.answer(0x61, false);
  Bytes b = r.save();
  PetitionerManager r2;
  CHECK(r2.load(b));
  CHECK(r2.save() == b);
  CHECK(r2.list().size() == 3 && r2.find(0x10)->argument == 7 && r2.find(0x10)->args.size() == 2);
  PetitionerManager empty;
  CHECK(empty.save().size() == 24);
  Bytes bad = b; bad.pop_back();
  CHECK(!r2.load(bad));
  CHECK(r2.presentable().size() == 2);
}

static void testAdvisors() {
  FakeSink sink;
  AdvisorManager am;
  PetitionerManager pm;
  FakeDeals deals;
  am.petitioners = &pm;
  am.deals = &deals;
  am.setSink(&sink);
  GameRng rng(5);
  AdvisorInputs in;
  in.population = 20000; in.year = 1990; in.mapWidth = 128; in.mapHeight = 128;
  in.budget.treasury = 100000; in.budget.estIncome = 100; in.budget.estExpense = 100;
  in.budget.taxEffect[0] = -0.2; in.budget.taxEffect[1] = 0.1; in.budget.taxEffect[2] = 0.2;
  AdvisorOutputs out;
  am.evaluate(kAdvBudget, in, rng, out);
  const TopicList& B = am.list(kAdvBudget);
  CHECK(B.listed(0x3A));    // R: x < D and r >= 2
  CHECK(B.listed(0x3C));    // C: x >= G -> on target
  CHECK(B.listed(0x40));    // I
  CHECK(!B.listed(0x10D));  // T = 100000 is not < 3*TL = 60000
  CHECK(!out.gameLost && out.advisorsUpdated.size() == 1);
  in.budget.treasury = -200000;
  AdvisorOutputs out2;
  am.evaluate(kAdvBudget, in, rng, out2);
  CHECK(out2.gameLost && B.listed(0xCE));
  in.budget.treasury = -50000;   // above -0.8 LG: 0x125 for N <= 50000
  AdvisorOutputs out3;
  am.evaluate(kAdvBudget, in, rng, out3);
  CHECK(!out3.gameLost && B.listed(0x125) && !B.listed(0xCE));
  in.budget.treasury = -90000;
  am.evaluate(kAdvBudget, in, rng, out3);
  CHECK(B.listed(0xCD));
  in.budget.treasury = 1000; in.neighbourConnections = 2;
  am.evaluate(kAdvBudget, in, rng, out3);
  CHECK(B.listed(0x04) && !B.listed(0x03));

  // Environment
  in.env.hasWaterSystem = true; in.env.waterPollution = 200; in.env.airPollution = 100;
  in.env.garbageAccumulating = true;
  am.evaluate(kAdvEnvironment, in, rng, out);
  const TopicList& E = am.list(kAdvEnvironment);
  CHECK(E.listed(0x7C) && E.listed(0x7F) && E.listed(0xF1));   // recycling absent from [GARBAGE_BUILDINGS] counts as locked
  in.env.waterPollution = 10;
  am.evaluate(kAdvEnvironment, in, rng, out);
  CHECK(E.listed(0xF4) && !E.listed(0x7C));
  AdvisorInputs tiny = in; tiny.population = 50;
  AdvisorManager am2;
  am2.evaluate(kAdvEnvironment, tiny, rng, out);
  CHECK(am2.list(kAdvEnvironment).topics().empty());

  // Public safety
  in.safety.policeStations = 0; in.safety.fireStations = 0; in.safety.crime = 200;
  in.safety.policeCoverage = 40;
  am.evaluate(kAdvSafety, in, rng, out);
  const TopicList& S = am.list(kAdvSafety);
  CHECK(S.listed(0x11F) && S.listed(0xFC) && S.listed(0xBC) && S.listed(0x9B));

  // Transportation
  in.trans.nRoad = 100; in.trans.nTransit = 0; in.trans.roadFunding = 100; in.trans.roadDamage = 0.0;
  am.evaluate(kAdvTransport, in, rng, out);
  const TopicList& T = am.list(kAdvTransport);
  CHECK(T.listed(0x124) && (T.listed(0x5A) || T.listed(0x59) || T.listed(0x5B)));

  // Planner petitions and reward
  IniFile cp = IniFile::fromText(
      "[REWARD_BUILDINGS]\n10110=Mayors house;0;1900;0x05;0\n14003=National Park;5000;1950;136;1\n"
      "13874=Science;100;1950;132;1\n[BUSINESS_DEAL_BUILDINGS]\n20000=Casino;0;0;149;0\n"
      "[REWARD_BUILDINGS_2]\nPARKS_NEEDED_FOR_NATIONAL_PARK=3\nEQ_NEEDED_FOR_SCIENCE_CENTER=90\n");
  am.rewards.load(cp);
  CHECK(am.rewards.entries().size() == 4 && am.rewards.entries()[3].isDeal);
  CHECK(am.rewards.param("PARKS_NEEDED_FOR_NATIONAL_PARK") == 3);
  in.plan.n14310 = 2; in.plan.eq = 50;
  am.evaluate(kAdvPlanner, in, rng, out);
  CHECK(pm.pending(5));                    // mayor's house petition, persistent
  CHECK(am.rewards.find(10110)->available);
  CHECK(!pm.pending(136) && !pm.pending(132));
  in.plan.n14310 = 4; in.plan.eq = 95;
  am.evaluate(kAdvPlanner, in, rng, out);
  CHECK(pm.pending(136) && pm.pending(132));
  CHECK(!pm.pending(149));                 // deals are skipped by the reward logic
  am.rewards.markAvailable(14003); am.rewards.markOwned(14003);
  CHECK(am.rewards.find(14003)->owned);

  // Demographics
  in.demo.nHospital = 0; in.demo.nSchool = 0;
  am.evaluate(kAdvDemographics, in, rng, out);
  const TopicList& Dm = am.list(kAdvDemographics);
  CHECK(Dm.listed(0xBD) && Dm.listed(0x6E));

  // Utilities + deals
  in.util.powerCap = 10000; in.util.powerUsed = 100; in.util.powerDemand = 100; in.util.nPlants = 1;
  in.util.neighbours = 2; in.util.waterCap = 5000; in.util.waterUsed = 100; in.util.treasury = 100000;
  deals.elig[1] = true;
  am.evaluate(kAdvUtilities, in, rng, out);
  const TopicList& U = am.list(kAdvUtilities);
  CHECK(U.listed(0x05) && U.listed(0x07) && U.listed(0x12));
  CHECK(deals.gen == 1);
  CHECK(pm.pending(0xB3) || pm.pending(0xB4) || pm.pending(0xB5));
  am.evaluate(kAdvUtilities, in, rng, out);
  CHECK(deals.gen == 1);          // pending offer kept, no new generation
  deals.active[1] = true;
  am.evaluate(kAdvUtilities, in, rng, out);
  CHECK(!pm.pending(0xB3) && !pm.pending(0xB4) && !pm.pending(0xB5));

  // Calendar cadence: Monday sets the flag, next day runs everything once
  AdvisorManager cm;
  AdvisorOutputs co;
  CHECK(!cm.calendarDay(in, rng, co));
  cm.calendarWeek();
  CHECK(cm.calendarDay(in, rng, co) && co.advisorsUpdated.size() == 7);
  CHECK(!cm.calendarDay(in, rng, co));
  cm.ordinanceChanged(0x00D0723D, true);
  CHECK(cm.list(kAdvDemographics).listed(0x2D));
  // transport announcements: drawn only when r < 10
  AdvisorManager tm; AdvisorInputs ti = in; ti.year = 2000;
  tm.thr.set("SUBWAY_AVAILABLE", 1910);
  bool announced = false;
  for (int i = 0; i < 300 && !announced; ++i) { tm.evaluate(kAdvTransport, ti, rng, co); announced = tm.list(kAdvTransport).listed(0x63); }
  CHECK(announced);
}

static void testAdvisorRecords() {
  AdvisorManager a;
  AdvisorInputs in; in.population = 5000; in.year = 1990;
  GameRng rng(1);
  AdvisorOutputs out;
  a.evaluateAll(in, rng, out);
  a.list(kAdvPlanner).raise(0xD1, 0, 4, true, true, false, true, 5, 6);
  IniFile cp = IniFile::fromText("[REWARD_BUILDINGS]\n10110=Mayors house;0;1900;5;0\n14150=Statue;0;0;6;0\n");
  a.rewards.load(cp);
  a.rewards.typeToInstance[10110] = 0x1111;
  a.rewards.typeToInstance[14150] = 0x2222;
  a.rewards.markAvailable(10110); a.rewards.markOwned(10110);
  a.mgrFlags[2] = 0;
  auto recs = a.saveRecords();
  CHECK(recs.size() == 9);
  size_t mgr = 0;
  for (auto& r : recs) if (r.group == kAdvMgrRecGroup) mgr = r.data.size();
  CHECK(mgr == 14);
  AdvisorManager b;
  b.rewards.load(cp);
  b.rewards.typeToInstance = a.rewards.typeToInstance;
  for (auto& r : recs) CHECK(b.loadRecord(r.type, r.group, r.instance, r.data));
  auto recs2 = b.saveRecords();
  CHECK(recs.size() == recs2.size());
  for (size_t i = 0; i < recs.size(); ++i) CHECK(recs[i].data == recs2[i].data);
  CHECK(b.rewards.find(10110)->owned && b.rewards.find(10110)->available && !b.rewards.find(14150)->available);
  CHECK(b.mgrFlags[2] == 0);
  // reward record: entries are 14 bytes each; unmapped entries preserved verbatim
  Bytes raw; put16(raw, 1); put16(raw, 2); put32(raw, 0xDEADBEEFu); put32(raw, 2);
  put32(raw, 0x207EDC0E); put32(raw, 0x57E); put32(raw, 0x9999); raw.push_back(1); raw.push_back(1);
  put32(raw, 0x207EDC0E); put32(raw, 0x57E); put32(raw, 0x2222); raw.push_back(0); raw.push_back(1);
  AdvisorManager c; c.rewards.load(cp); c.rewards.typeToInstance = a.rewards.typeToInstance;
  CHECK(c.rewards.load(raw));
  CHECK(c.rewards.find(14150)->available && !c.rewards.find(14150)->owned);
  CHECK(c.rewards.save() == raw);
  Bytes shortRaw = raw; shortRaw.pop_back();
  CHECK(!c.rewards.load(shortRaw));
  // mood tables
  IniFile gold = IniFile::fromText("[AdvisorMoods]\n0x00000191=4\n0x00000001=5\n");
  MoodTable mg; mg.load(IniFile::fromText("[AdvisorMoods]\n0x00000001=2\n"), &gold);
  CHECK(mg.mood(1) == 2 && mg.mood(0x191) == 4 && mg.mood(7) == 0);
  TopicGfxTable g; g.load(IniFile::fromText("[AdvisorTopicGfxIDs]\n0x00000001=0x831D112B,0x00000010\n"));
  uint32_t gr, gi;
  CHECK(g.get(1, gr, gi) && gr == 0x831D112B && gi == 0x10 && !g.get(2, gr, gi));
  // thresholds
  AdvisorThresholds th;
  th.loadAdvisorIni(kAdvEnvironment, IniFile::fromText("[POLLUTION_RATE_CUTOFFS]\nWATER_POLLUTION_HIGH=77;\n"
                                                         "[GARBAGE_BUILDINGS]\n1=Recycling;1970;115\n"));
  CHECK(th.get("WATER_POLLUTION_HIGH") == 77);
  CHECK(th.garbage.size() == 1 && th.garbage[0].year == 1970 && th.garbage[0].topic == 115);
  TextContext tc; tc.city = "Springfield";
  std::vector<TopicArg> args(1); args[0].name = "%X%"; args[0].value = 12;
  CHECK(topicText("In %CITYNAME% at %X%", tc, args) == "In Springfield at 12");
  // building unlocks (spec 22 "Building availability"): unlocked silently at load when the year has passed; a later
  // monthly unlock is announced as a sticky one-shot event by the environment advisor
  AdvisorManager s;
  s.thr.garbage.push_back({1, 1950, 0x72});
  s.thr.garbage.push_back({2, 1955, 0x73});
  s.initBuildingUnlocks(1952);
  CHECK(s.buildingUnlocked(1) && !s.buildingUnlocked(2) && s.buildingUnlocked(99));
  for (int m = 0; m < 400 && !s.buildingUnlocked(2); m++) s.monthlyBuildingUnlock(1960, rng);
  CHECK(s.buildingUnlocked(2));
  AdvisorInputs ei; ei.population = 500; ei.year = 1960;
  s.evaluate(kAdvEnvironment, ei, rng, out);
  CHECK(!s.list(kAdvEnvironment).listed(0x72));
  CHECK(s.list(kAdvEnvironment).listed(0x73));
  CHECK(s.list(kAdvEnvironment).find(0x73)->sticky == 1);
  {  // garbage metrics (spec 22): near capacity -> 0x77 with WTE unlocked, else 0xF8
    AdvisorManager g;
    g.thr.garbage.push_back({12853, 2000, 0});
    g.initBuildingUnlocks(1990);
    AdvisorInputs gi; gi.population = 500; gi.year = 1990;
    gi.env.garbageAccumulating = false; gi.env.garbageDemand = 200; gi.env.garbageCapacity = 100;
    gi.env.garbageNearCapacity = true; gi.env.garbageSurplusWithinLimit = false;
    g.evaluate(kAdvEnvironment, gi, rng, out);
    CHECK(g.list(kAdvEnvironment).listed(0xF8) && !g.list(kAdvEnvironment).listed(0x77));
  }
}

int main() {
  testTopicList();
  testTopicRecord();
  testTicker();
  testEventNews();
  testPetitioners();
  testAdvisors();
  testAdvisorRecords();
  TEST_MAIN_END();
}
