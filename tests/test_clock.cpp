#include <string>
#include <thread>
#include <vector>
#include "core/clock.h"
#include "testutil.h"

using namespace m3k;

static void testCalendar() {
  CHECK(cal::toJdn(1, 1, 1900) == 2415021);
  CHECK(cal::toJdn(1, 1, 2000) == 2451545);
  auto d = cal::fromJdn(2451545);
  CHECK(d.month == 1 && d.day == 1 && d.year == 2000);
  // round trip over a long span incl. leap years/centuries
  bool ok = true;
  for (int32_t j = cal::toJdn(1, 1, 1800); j < cal::toJdn(1, 1, 2410); j += 7) {
    auto x = cal::fromJdn(j);
    if (cal::toJdn(x.month, x.day, x.year) != j) ok = false;
  }
  CHECK(ok);
  auto leap = cal::fromJdn(cal::toJdn(2, 28, 2000) + 1);
  CHECK(leap.month == 2 && leap.day == 29);
  auto nl = cal::fromJdn(cal::toJdn(2, 28, 1900) + 1);
  CHECK(nl.month == 3 && nl.day == 1);   // 1900 not leap
  CHECK(cal::daysInMonth(2, 2000) == 29 && cal::daysInMonth(2, 2100) == 28 && cal::daysInMonth(12, 2000) == 31);
  CHECK(cal::weekday(cal::toJdn(1, 1, 2000)) == 6);   // Saturday
  CHECK(cal::weekday(cal::toJdn(1, 3, 2000)) == 1);   // Monday
  CHECK(cal::weekday(cal::toJdn(1, 2, 2000)) == 7);   // Sunday
  CHECK(cal::dayOfYear(cal::toJdn(1, 1, 2000)) == 1);
  CHECK(cal::dayOfYear(cal::toJdn(12, 31, 2000)) == 366);
  CHECK(cal::dayOfYear(cal::toJdn(3, 1, 1901)) == 60);
  CHECK(cal::jdnFromDayOfYear(1, 2000) == cal::toJdn(1, 1, 2000));
  // year 0 special case: 2 January 1901
  CHECK(cal::jdnFromDayOfYear(1, 0) == cal::toJdn(1, 2, 1901));
  CHECK(cal::dateStamp(1, 31, 2000) == cal::dateStamp(1, 31, 2000));
  CHECK(cal::dateStamp(1, 31, 2000) + 0 == (1 + 24000) * 30 + 31);
  // day 31 of one month vs day 1 of next: the stamp is monotonic only with 30-day assumption
  CHECK(cal::dateStamp(1, 31, 2000) == cal::dateStamp(2, 1, 2000));
}

static void testSc2Import() {
  auto r = cal::importSc2(true, 1234, true, 1900);
  CHECK(r.date.day == 1234 % 25 + 1 && r.date.month == (1234 / 25) % 12 + 1 && r.date.year == 1234 / 300 + 1900);
  CHECK(r.foundingYear == 1900);
  auto m = cal::importSc2(false, 0, true, 1900);
  CHECK(m.date.year == 3000 && m.date.month == 1 && m.date.day == 1 && m.foundingYear == 3000);
  CHECK(SpeedControl::importSc2Speed(true, 0) == 1 && SpeedControl::importSc2Speed(true, 3) == 4);
  CHECK(SpeedControl::importSc2Speed(false, 0) == 1);
}

static void testRng() {
  SimRng r(1);
  const uint32_t want[10] = {0x41C6, 0x59D4967E, 0xBF022781, 0x959AC46B, 0x79D0F94B,
                             0x60B995FB, 0x0703D9E2, 0x584E9CFB, 0x81FCBF54, 0xBA030FF6};
  for (int i = 0; i < 10; i++) CHECK(r.nextRaw() == want[i]);
  SimRng r2(1);
  CHECK(r2.rand(100) == 38);
  CHECK(r2.rand(0) == 0);
  // float generator seeded with 1
  SimRng f(1);
  const uint32_t wf[5] = {0x278DDE6D, 0xFB533A69, 0x59EBECB5, 0xD369BF11, 0x8B14183D};
  for (int i = 0; i < 5; i++) { f.nextReal(); CHECK(f.realState() == wf[i]); }
  SimRng z(0);
  const uint32_t wz[3] = {0x0B5CE118, 0x03F6A738, 0x36DFC2D8};
  for (int i = 0; i < 3; i++) { z.nextReal(); CHECK(z.realState() == wz[i]); }
  // seed sets both states
  SimRng s(77);
  CHECK(s.intState() == 77 && s.realState() == 77);
  // large n: inclusive upper bound, rejection sampling
  SimRng big(5);
  bool inRange = true;
  for (int i = 0; i < 200; i++) if (big.rand(0x01000000u) > 0x01000000u) inRange = false;
  CHECK(inRange);
  CHECK(SimRng(3).randInt(10, 20) >= 10);
  // CRT rand: seed 1 first outputs 41, 18467, 6334
  CRand c(1);
  CHECK(c.rand() == 41 && c.rand() == 18467 && c.rand() == 6334);
}

static void testClockTiming() {
  Clock c;
  CHECK(c.compression() == 1440);
  CHECK(c.msPerDay() == 60000 && c.msPerHour() == 2500 && c.msPerMinute() == 50);
  c.setCompression(10080);
  CHECK(c.msPerDay() == 8571 && c.msPerMinute() == 50);
  c.setCompression(43200);
  CHECK(c.msPerDay() == 2000);
  c.setCompression(129600);
  CHECK(c.msPerDay() == 666);
  c.setCompression(525600);
  CHECK(c.msPerDay() == 164);
  c.setCompression(120);
  CHECK(c.msPerDay() == 720000 && c.msPerMinute() == 500);

  // day mode ticking
  c.setCompression(43200);
  c.setDate(1, 1, 2000);
  c.start(0);
  int days = 0;
  c.addListener(Clock::Day, [&](Clock&) { days++; });
  c.tick(1999);
  CHECK(days == 0);
  c.tick(2000);
  CHECK(days == 1 && c.date().day == 2);
  c.tick(10000);   // 4 more days in one callback
  CHECK(days == 5);
  CHECK(c.accumulator() == 0);

  // pause: elapsed time before pause is kept; time during pause discarded
  Clock p;
  p.setCompression(43200);
  p.start(0);
  int d = 0;
  p.addListener(Clock::Day, [&](Clock&) { d++; });
  p.pause(1500);
  p.pause(1600);                  // nested
  CHECK(p.pauseCount() == 2);
  p.tick(9000);
  CHECK(d == 0);
  p.resume(9500);
  CHECK(p.paused());
  p.resume(10000);
  CHECK(!p.paused() && d == 0);
  p.tick(10499);                  // 1500 kept + 499 = 1999
  CHECK(d == 0);
  p.tick(10500);
  CHECK(d == 1);
  p.resume(10600);                // extra resume is harmless
  CHECK(p.pauseCount() == 0);
}

static void testClockCascade() {
  Clock c;
  c.setCompression(43200);
  c.setDate(12, 29, 1999);   // Wednesday
  std::vector<std::string> log;
  c.addListener(Clock::Day, [&](Clock& k) { log.push_back("day" + std::to_string(k.date().day)); });
  c.addListener(Clock::Day, [&](Clock&) { log.push_back("day2"); });
  c.addListener(Clock::Week, [&](Clock&) { log.push_back("week"); });
  c.addListener(Clock::Month, [&](Clock&) { log.push_back("month"); });
  c.addListener(Clock::Year, [&](Clock&) { log.push_back("year"); });
  CHECK(!c.addAlarm(c.jdn(), [](Clock&) {}));      // not strictly later
  int fired = 0;
  CHECK(c.addAlarm(cal::toJdn(1, 1, 2000), [&](Clock&) { fired++; log.push_back("alarm"); }));
  c.dayTick();   // Dec 30 (Thu)
  c.dayTick();   // Dec 31 (Fri)
  CHECK(fired == 0);
  log.clear();
  c.dayTick();   // Jan 1 2000, Saturday: day, month, year, alarm
  const std::vector<std::string> want1 = {"day1", "day2", "month", "year", "alarm"};
  CHECK(log == want1);
  c.dayTick();
  CHECK(fired == 1);
  c.dayTick();   // Jan 3 = Monday
  CHECK(log.back() == "week");
  // 1 Jan on a Monday runs day, week, month, year in that order
  Clock m;
  m.setDate(12, 31, 2017);
  std::vector<std::string> l2;
  m.addListener(Clock::Year, [&](Clock&) { l2.push_back("year"); });
  m.addListener(Clock::Month, [&](Clock&) { l2.push_back("month"); });
  m.addListener(Clock::Week, [&](Clock&) { l2.push_back("week"); });
  m.addListener(Clock::Day, [&](Clock&) { l2.push_back("day"); });
  m.dayTick();
  const std::vector<std::string> want2 = {"day", "week", "month", "year"};
  CHECK(l2 == want2);
  // listener removal
  Clock r;
  int n = 0;
  auto id = r.addListener(Clock::Day, [&](Clock&) { n++; });
  r.dayTick();
  CHECK(r.removeListener(id) && !r.removeListener(id));
  r.dayTick();
  CHECK(n == 1);
}

static void testMinuteMode() {
  Clock c;
  c.setCompression(43200);   // msPerMinute floored to 50
  c.start(0);
  int mins = 0, hours = 0, days = 0;
  auto id = c.addListener(Clock::Minute, [&](Clock&) { mins++; });
  c.addListener(Clock::Hour, [&](Clock&) { hours++; });
  c.addListener(Clock::Day, [&](Clock&) { days++; });
  CHECK(c.minuteMode());
  c.tick(50 * 60 * 24 - 1);
  CHECK(days == 0);
  c.tick(50 * 60 * 24);
  CHECK(mins == 1440 && hours == 24 && days == 1);
  CHECK(c.minuteCounter() == 0 && c.hourCounter() == 0);
  // day takes >= 72 s of real time (1440*50ms) whatever the speed
  c.setCompression(525600);
  uint32_t t0 = 50 * 60 * 24;
  c.tick(t0 + 71999);
  CHECK(days == 1);
  c.tick(t0 + 72000);
  CHECK(days == 2);
  // leaving minute mode returns to day ticks, counters are kept
  c.removeListener(id);
  CHECK(c.minuteMode());   // hour listener remains
}

static void testSpeed() {
  SpeedControl s;
  CHECK(s.current() == 1 && s.selected() == 1 && s.factor() == 10080);  // new city starts at Turtle [OBSERVED]
  s.setSpeed(2);
  int announced = -1;
  s.onSpeedChanged = [&](int sel) { announced = sel; };
  CHECK(s.setSpeed(2) && announced == -1);   // no change: nothing announced
  CHECK(s.setSpeed(4) && s.current() == 4 && s.selected() == 4 && announced == 4);
  CHECK(s.factor() == 525600);
  CHECK(!s.setSpeed(0) && !s.setSpeed(5) && s.current() == 4);
  CHECK(s.setSpeed(1) && s.factor() == 10080);
  // paused state
  int pauses = 0;
  s.onPaused = [&]() { pauses++; };
  CHECK(s.pause() && pauses == 1);
  CHECK(!s.pause() && pauses == 1);
  CHECK(s.current() == 0 && s.factor() == 120);
  CHECK(s.setSpeed(3) && s.current() == 0 && s.selected() == 3);
  CHECK(s.setSpeed(9) && s.selected() == 4);
  // Q-AY: while paused, negative s is accepted; only upper clamp to 4.
  announced = -99;
  CHECK(s.setSpeed(-2) && s.current() == 0 && s.selected() == -2 && announced == -2);
  CHECK(s.setSpeed(9) && s.selected() == 4);  // restore for resume
  CHECK(!s.resume() && s.resume());
  CHECK(s.current() == 4);
  // Not paused: s ≤ 0 rejected.
  CHECK(!s.setSpeed(-1) && !s.setSpeed(0) && s.current() == 4);
  Clock c;
  s.applyTo(c);
  CHECK(c.compression() == 525600);
  CHECK(std::string(SpeedControl::name(4)) == "African Swallow" && std::string(SpeedControl::name(0)) == "Pause");
}

static void testSchedule() {
  CHECK(Clock::kTimerIntervalMs == 1000);   // spec 14 §2.2 Q-AS
  CHECK(dailySteps().size() == 11);
  // Spec 14 §6.1 / Q-BI [STATIC]: items 1–5 exact order.
  CHECK(dailyDriverSteps().size() == 5);
  CHECK(dailyDriverSteps()[0] == Step::DailyDisasters);
  CHECK(dailyDriverSteps()[1] == Step::DailyPolice);
  CHECK(dailyDriverSteps()[2] == Step::DailyFire);
  CHECK(dailyDriverSteps()[3] == Step::DailyLandValue);
  CHECK(dailyDriverSteps()[4] == Step::DailyScenario);
  // Spec 14 §6.1 / Q-BJ [STATIC]: item 7 after 1–5, before self-scheduled 8→11→6→9→10.
  CHECK(dailySelfScheduledSteps().size() == 5);
  CHECK(dailySelfScheduledSteps()[0] == Step::DailyAura);
  CHECK(dailySelfScheduledSteps()[1] == Step::DailyFlammability);
  CHECK(dailySelfScheduledSteps()[2] == Step::DailyZoneDev);
  CHECK(dailySelfScheduledSteps()[3] == Step::DailyPollution);
  CHECK(dailySelfScheduledSteps()[4] == Step::DailyCrime);
  CHECK(dailySharedSteps().size() == 10);
  CHECK(dailySteps()[5] == Step::DailyDisasterInstances);
  CHECK(dailySteps()[6] == Step::DailyAura);
  CHECK(dailySteps().back() == Step::DailyCrime);
  CHECK(monthlySteps().size() == 16);
  CHECK(monthlySteps()[9] == Step::MonthlyBudget && monthlySteps()[5] == Step::MonthlyResidential);
  CHECK(monthlySteps().back() == Step::MonthlyAura);
  // Spec 14 §6.1 / Q-BF [STATIC]: dedicated items 1–3 (DM, neighbours, world) before shared 4–16.
  CHECK(monthlyDedicatedSteps().size() == 3);
  CHECK(monthlyDedicatedSteps()[0] == Step::MonthlyDisasterManager);
  CHECK(monthlyDedicatedSteps()[1] == Step::MonthlyNeighbours);
  CHECK(monthlyDedicatedSteps()[2] == Step::MonthlyWorldLayer);
  CHECK(monthlySharedSteps()[0] == Step::MonthlyRci);
  CHECK(monthlySharedSteps().back() == Step::MonthlyAura);
  CHECK(monthlySteps()[0] == Step::MonthlyDisasterManager && monthlySteps()[1] == Step::MonthlyNeighbours);
  CHECK(monthlySteps()[2] == Step::MonthlyWorldLayer && monthlySteps()[3] == Step::MonthlyRci);
  CHECK(anniversaryNewsItem(10) == 0x1F4 && anniversaryNewsItem(2000) == 0x1FB && anniversaryNewsItem(11) == 0);

  Clock clk;
  clk.setCompression(43200);
  clk.setDate(12, 31, 2009);   // next day: Fri 1 Jan 2010
  clk.setFoundingYear(2000);
  CitySchedule sc;
  sc.logging = true;
  sc.attach(clk);
  std::vector<std::string> ev;
  sc.onDay = [&]() { ev.push_back("onDay"); };
  sc.onWeek = [&]() { ev.push_back("onWeek"); };
  sc.onMonth = [&]() { ev.push_back(std::string("onMonth paused=") + (clk.paused() ? "1" : "0")); };
  sc.onYear = [&]() { ev.push_back("onYear"); };
  uint32_t news = 0;
  sc.onAnniversary = [&](uint32_t n) { news = n; };
  sc.setHandler(Step::MonthlyBudget, [&]() { ev.push_back("budget"); });
  sc.setHandler(Step::DailyPolice, [&]() { ev.push_back("police"); });
  clk.dayTick();   // 1 Jan 2010 (Friday)
  const std::vector<std::string> want = {"police", "onDay", "budget", "onMonth paused=1", "onYear"};
  CHECK(ev == want);
  CHECK(!clk.paused());
  CHECK(news == 0x1F4);   // age 10
  CHECK(sc.elapsedDays == 1);
  // log order: driver 1–5, self-scheduled (no item 7), then monthly
  CHECK(sc.cascadeLog(0) == int(Step::DailyDisasters));
  CHECK(sc.cascadeLog(1) == int(Step::DailyPolice));
  CHECK(sc.cascadeLog(2) == int(Step::DailyFire));
  CHECK(sc.cascadeLog(3) == int(Step::DailyLandValue));
  CHECK(sc.cascadeLog(4) == int(Step::DailyScenario));
  CHECK(sc.cascadeLog(5) == int(Step::DailyAura));
  CHECK(sc.cascadeLog(6) == int(Step::DailyFlammability));
  CHECK(sc.cascadeLog(7) == int(Step::DailyZoneDev));
  CHECK(sc.cascadeLog(8) == int(Step::DailyPollution));
  CHECK(sc.cascadeLog(9) == int(Step::DailyCrime));
  size_t daily = dailySharedSteps().size();   // item 7 skipped when inactive
  CHECK(sc.cascadeLog(daily) == int(Step::MonthlyDisasterManager));
  CHECK(sc.cascadeLog(daily + 1) == int(Step::MonthlyNeighbours));   // Q-BF: 1→2→3
  CHECK(sc.cascadeLog(daily + 2) == int(Step::MonthlyWorldLayer));
  CHECK(sc.cascadeLog(daily + 3) == int(Step::MonthlyRci));          // shared pass after dedicated
  CHECK(sc.cascadeLog(daily + 16 - 1) == int(Step::MonthlyAura));
  CHECK(sc.cascadeLog(daily + 16) == -1);

  // disasters disabled skip the disaster manager monthly step; 2→3 still before shared
  sc.clearLog();
  sc.disastersEnabled = false;
  sc.runMonth();
  CHECK(sc.cascadeLog(0) == int(Step::MonthlyNeighbours));
  CHECK(sc.cascadeLog(1) == int(Step::MonthlyWorldLayer));
  CHECK(sc.cascadeLog(2) == int(Step::MonthlyRci));

  // Q-BJ: with disasters active, item 7 after 1–5, before aura/self-scheduled
  sc.clearLog();
  sc.setDisasterActive(true);
  sc.runDay();
  CHECK(sc.cascadeLog(4) == int(Step::DailyScenario));
  CHECK(sc.cascadeLog(5) == int(Step::DailyDisasterInstances));
  CHECK(sc.cascadeLog(6) == int(Step::DailyAura));
  sc.setDisasterActive(false);

  // weekly: enrolled buildings first, in order, additions during the step wait
  std::vector<std::string> wk;
  int b1 = sc.addBuilding([&]() {
    wk.push_back("b1");
    sc.addBuilding([&]() { wk.push_back("late"); });
  });
  sc.addBuilding([&]() { wk.push_back("b2"); });
  sc.setHandler(Step::WeeklyStreetSim, [&]() { wk.push_back("street"); });
  sc.runWeek();
  const std::vector<std::string> wantW = {"b1", "b2", "street"};
  CHECK(wk == wantW);
  wk.clear();
  sc.removeBuilding(b1);
  sc.runWeek();
  const std::vector<std::string> wantW2 = {"b2", "late", "street"};
  CHECK(wk == wantW2);

  // minute work only while a disaster is active
  int minutes = 0;
  sc.setHandler(Step::MinuteDisasterInstances, [&]() { minutes++; });
  CHECK(!clk.minuteMode());
  sc.setDisasterActive(true);
  CHECK(clk.minuteMode());
  clk.start(0);
  clk.tick(50 * 3);
  CHECK(minutes == 3);
  sc.setDisasterActive(false);
  CHECK(!clk.minuteMode());

  // Spec 14 §6.3 rule 6: backlog pause when pending-work queue > 3 (item count, not ms)
  Clock cPause;
  cPause.setDate(1, 1, 2000);
  CitySchedule scPause;
  scPause.attach(cPause);
  int pending = 0;
  scPause.pendingWorkCount = [&]() { return pending; };
  bool sawPauseDuringStep = false;
  scPause.setHandler(Step::DailyDisasters, [&]() { pending = 4; });
  scPause.setHandler(Step::DailyPolice, [&]() {
    if (cPause.paused()) sawPauseDuringStep = true;
  });
  scPause.runDay();
  CHECK(sawPauseDuringStep);
  CHECK(!cPause.paused());

  // Hold condition test (pause∨pending counts; poll fast for the unit test)
  int holdTicks = 0;
  scPause.holdPollMs = 1;
  scPause.pendingWorkCount = nullptr;
  scPause.isHold = [&]() {
    holdTicks++;
    return holdTicks < 3;
  };
  scPause.runDay();
  CHECK(holdTicks >= 3);
}

int main() {
  testCalendar();
  testSc2Import();
  testRng();
  testClockTiming();
  testClockCascade();
  testMinuteMode();
  testSpeed();
  testSchedule();
  TEST_MAIN_END();
}
