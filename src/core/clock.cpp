#include "core/clock.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace m3k {

namespace {
int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}
}  // namespace

// ---------------------------------------------------------------- calendar
namespace cal {

int32_t toJdn(int m, int d, int y) {
  int mp, yp;
  if (m < 3) { mp = m + 9; yp = y - 1; } else { mp = m - 3; yp = y; }
  int64_t c = floorDiv(yp, 100);
  int64_t ya = yp - 100 * c;
  int64_t j = floorDiv(146097 * c, 4) + floorDiv(1461 * ya, 4) + floorDiv(153 * mp + 2, 5) + d + 1721119;
  return int32_t(j);
}

Date fromJdn(int32_t jdn) {
  int64_t t = 4 * (int64_t(jdn) - 1721119) - 1;
  int64_t c = floorDiv(t, 146097);
  t = floorDiv(t - 146097 * c, 4) * 4;
  int64_t ya = floorDiv(t + 3, 1461);
  t = floorDiv(t + 3 - 1461 * ya + 4, 4) * 5;
  int64_t mp = floorDiv(t - 3, 153);
  int64_t d = floorDiv(t - 153 * mp + 2, 5);
  int64_t y = 100 * c + ya;
  int64_t m;
  if (mp < 10) m = mp + 3; else { m = mp - 9; y += 1; }
  return Date{int(m), int(d), int(y)};
}

int dayOfYear(int32_t jdn) {
  Date d = fromJdn(jdn);
  return int(jdn - toJdn(12, 31, d.year - 1));
}

int weekday(int32_t jdn) {
  int64_t a = ((int64_t(jdn) + 1) % 7 + 6) % 7 + 1;
  return int(a);
}

bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int m, int y) {
  static const int dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 30;
  return (m == 2 && isLeap(y)) ? 29 : dm[m - 1];
}

int32_t jdnFromDayOfYear(int doy, int year) {
  if (year != 0) return toJdn(12, 31, year - 1) + doy;
  return doy + 2415386;
}

int32_t dateStamp(int month, int day, int year) { return (month + 12 * year) * 30 + day; }

ImportedDate importSc2(bool haveCount, int32_t n, bool haveStart, int startYear) {
  ImportedDate r;
  if (!haveCount || !haveStart) {
    r.date = Date{1, 1, 3000};
    r.foundingYear = 3000;
    return r;
  }
  r.date.day = int(n % 25) + 1;
  r.date.month = int((n / 25) % 12) + 1;
  r.date.year = int(n / 300) + startYear;
  r.foundingYear = startYear;
  return r;
}

}  // namespace cal

// ---------------------------------------------------------------- RNG
uint32_t SimRng::nextRaw() {
  uint64_t v = uint64_t(s_) * 1103515245ull + 12345ull;
  s_ = uint32_t(v);
  return uint32_t(v >> 16);
}

uint32_t SimRng::rand(uint32_t n) {
  if (n == 0) return 0;
  if (n < 0xFFFFFFu) return nextRaw() % n;
  for (;;) {
    uint32_t r = nextRaw();
    if (r <= n) return r;
  }
}

double SimRng::nextReal() {
  if (t_ == 0) t_ = 0x12345678u;
  t_ = t_ * 0x278DDE6Du;
  return double(t_) / 4294967296.0;
}

double SimRng::nextNormal() {
  if (hasSpareNormal_) {
    hasSpareNormal_ = false;
    return spareNormal_;
  }
  // Box–Muller: two independent standard normals from two U(0,1) draws.
  double u1 = nextReal(), u2 = nextReal();
  if (u1 < 1e-12) u1 = 1e-12;
  double r = std::sqrt(-2.0 * std::log(u1));
  double th = 2.0 * 3.14159265358979323846 * u2;
  spareNormal_ = r * std::sin(th);
  hasSpareNormal_ = true;
  return r * std::cos(th);
}

// ---------------------------------------------------------------- Clock
Clock::Clock() { setCompression(1440); }

void Clock::setCompression(uint32_t f) {
  if (f == 0) f = 1;
  f_ = f;
  msPerDay_ = uint32_t(86400000ull / f);
  msPerHour_ = std::max<uint32_t>(msPerDay_ / 24, 50);
  msPerMinute_ = std::max<uint32_t>(msPerHour_ / 60, 50);
  // Spec 14 s4 table: speed 0 (F=120) shows msPerMinute 500 "no floor"; the formula gives
  // floor(floor(720000/24)/60) = 500, which is above the floor anyway.
}

void Clock::pause(uint32_t nowMs) {
  lastNow_ = nowMs;
  if (pauseCount_ == 0) acc_ += nowMs - last_;
  pauseCount_++;
}

void Clock::resume(uint32_t nowMs) {
  lastNow_ = nowMs;
  if (pauseCount_ > 0) {
    pauseCount_--;
    if (pauseCount_ == 0) last_ = nowMs;
  }
}

void Clock::tick(uint32_t nowMs) {
  lastNow_ = nowMs;
  if (pauseCount_ > 0) return;
  acc_ += nowMs - last_;
  last_ = nowMs;
  if (minuteMode()) {
    while (acc_ >= msPerMinute_ && pauseCount_ == 0) {
      acc_ -= msPerMinute_;
      minuteTick();
      if (!minuteMode()) break;  // mode changed by a listener: day mode continues below
    }
    if (!minuteMode()) {
      while (acc_ >= msPerDay_ && pauseCount_ == 0) { acc_ -= msPerDay_; dayTick(); }
    }
  } else {
    while (acc_ >= msPerDay_ && pauseCount_ == 0) {
      acc_ -= msPerDay_;
      dayTick();
      if (minuteMode()) break;
    }
  }
}

void Clock::minuteTick() {
  minute_++;
  notify(Minute);
  if (minute_ >= 60) {
    minute_ = 0;
    hourTick();
  }
}

void Clock::hourTick() {
  hour_++;
  notify(Hour);
  if (hour_ >= 24) {
    hour_ = 0;
    dayTick();
  }
}

void Clock::dayTick() {
  jdn_++;
  notify(Day);
  if (cal::weekday(jdn_) == 1) notify(Week);
  cal::Date d = cal::fromJdn(jdn_);
  if (d.day == 1) notify(Month);
  if (d.day == 1 && d.month == 1) notify(Year);  // day of year 1
  // One-shot alarms for the new date, fired then removed.
  std::vector<Alarm> due;
  for (auto it = alarms_.begin(); it != alarms_.end();) {
    if (it->jdn == jdn_) { due.push_back(std::move(*it)); it = alarms_.erase(it); } else ++it;
  }
  for (auto& a : due) a.fn(*this);
}

Clock::ListenerId Clock::addListener(Period p, Listener fn) {
  ListenerId id = nextId_++;
  listeners_.push_back(Entry{id, p, std::move(fn)});
  return id;
}

bool Clock::removeListener(ListenerId id) {
  for (auto it = listeners_.begin(); it != listeners_.end(); ++it)
    if (it->id == id) { listeners_.erase(it); return true; }
  return false;
}

size_t Clock::listenerCount(Period p) const {
  size_t n = 0;
  for (auto& e : listeners_) if (e.p == p) n++;
  return n;
}

bool Clock::addAlarm(int32_t j, Listener fn) {
  if (j <= jdn_) return false;
  alarms_.push_back(Alarm{j, std::move(fn)});
  return true;
}

void Clock::notify(Period p) {
  std::vector<ListenerId> ids;
  for (auto& e : listeners_) if (e.p == p) ids.push_back(e.id);
  for (ListenerId id : ids) {
    Listener fn;
    for (auto& e : listeners_) if (e.id == id) { fn = e.fn; break; }
    if (fn) fn(*this);
  }
}

// ---------------------------------------------------------------- speeds
constexpr uint32_t SpeedControl::kFactor[5];

const char* SpeedControl::name(int s) {
  switch (s) {
    case 0: return "Pause";
    case 1: return "Turtle";
    case 2: return "Llama";
    case 3: return "Cheetah";
    case 4: return "African Swallow";
  }
  return "";
}

bool SpeedControl::setSpeed(int s) {
  if (s == current_ && s == selected_) return true;
  if (simPaused()) {
    // Spec 14 §2.2 / Q-AY: while paused, negative s is kept (only upper clamp to 4).
    current_ = 0;
    if (s != 0) selected_ = std::min(s, 4);
  } else {
    if (s < 1 || s > 4) return false;  // s ≤ 0 rejected when not paused
    current_ = selected_ = s;
  }
  if (onSpeedChanged) onSpeedChanged(selected_);
  return true;
}

bool SpeedControl::pause() {
  pauseCount_++;
  if (pauseCount_ == 1) {
    current_ = 0;  // [INFERRED] pausing shows speed 0; selected speed is kept
    if (onPaused) onPaused();
    return true;
  }
  return false;
}

bool SpeedControl::resume() {
  if (pauseCount_ == 0) return false;
  pauseCount_--;
  if (pauseCount_ == 0) {
    // Selected may be negative from a paused setSpeed (Q-AY); running current must be 1–4.
    current_ = (selected_ >= 1 && selected_ <= 4) ? selected_ : 1;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- scheduling
const char* stepName(Step s) {
  static const char* n[] = {
      "DailyDisasters", "DailyPolice", "DailyFire", "DailyLandValue", "DailyScenario", "DailyZoneDev",
      "DailyDisasterInstances", "DailyPollution", "DailyCrime", "DailyFlammability", "DailyAura",
      "WeeklyStreetSim",
      "MonthlyDisasterManager", "MonthlyNeighbours", "MonthlyWorldLayer", "MonthlyRci", "MonthlyOrdinances",
      "MonthlyResidential", "MonthlyCommercial", "MonthlyIndustrial", "MonthlyTraffic", "MonthlyBudget",
      "MonthlyPower", "MonthlyWater", "MonthlyWeather", "MonthlyPollution", "MonthlyCrime", "MonthlyAura",
      "MinuteDisasterInstances", "MinuteScenario"};
  int i = int(s);
  return (i >= 0 && i < int(Step::kCount)) ? n[i] : "?";
}

std::vector<Step> dailySharedSteps() {
  // Spec 14 §6.1 items 1–6 and 8–11 [STATIC Q-BG shared pass]. Aura before pollution/crime/flam (Q-AR).
  return {Step::DailyDisasters, Step::DailyPolice, Step::DailyFire, Step::DailyLandValue,
          Step::DailyScenario, Step::DailyZoneDev, Step::DailyAura, Step::DailyPollution,
          Step::DailyCrime, Step::DailyFlammability};
}
std::vector<Step> dailySteps() {
  // Spec 14 §6.1 / Q-BG [STATIC]: item 7 (active disaster instance day ticks) after the shared pass.
  std::vector<Step> v = dailySharedSteps();
  v.push_back(Step::DailyDisasterInstances);
  return v;
}
std::vector<Step> weeklySteps() { return {Step::WeeklyStreetSim}; }
std::vector<Step> monthlyDedicatedSteps() {
  // Spec 14 §6.1 items 1–3 [STATIC Q-BF]: dedicated monthly-tier entries, subsystem creation order.
  return {Step::MonthlyDisasterManager, Step::MonthlyNeighbours, Step::MonthlyWorldLayer};
}
std::vector<Step> monthlySharedSteps() {
  // Spec 14 §6.1 items 4–16 [STATIC]: shared monthly pass (AreaAgent cascade).
  return {Step::MonthlyRci, Step::MonthlyOrdinances, Step::MonthlyResidential, Step::MonthlyCommercial,
          Step::MonthlyIndustrial, Step::MonthlyTraffic, Step::MonthlyBudget, Step::MonthlyPower,
          Step::MonthlyWater, Step::MonthlyWeather, Step::MonthlyPollution, Step::MonthlyCrime,
          Step::MonthlyAura};
}
std::vector<Step> monthlySteps() {
  std::vector<Step> v = monthlyDedicatedSteps();
  auto shared = monthlySharedSteps();
  v.insert(v.end(), shared.begin(), shared.end());
  return v;
}
std::vector<Step> minuteSteps() { return {Step::MinuteDisasterInstances, Step::MinuteScenario}; }

uint32_t anniversaryNewsItem(int age) {
  switch (age) {
    case 10: return 0x1F4;
    case 25: return 0x1F5;
    case 50: return 0x1F6;
    case 100: return 0x1F7;
    case 250: return 0x1F8;
    case 500: return 0x1F9;
    case 1000: return 0x1FA;
    case 2000: return 0x1FB;
  }
  return 0;
}

int CitySchedule::addBuilding(Fn fn) {
  int id = nextBuilding_++;
  buildings_.push_back(Building{id, std::move(fn)});
  return id;
}
void CitySchedule::removeBuilding(int id) {
  for (auto it = buildings_.begin(); it != buildings_.end(); ++it)
    if (it->id == id) { buildings_.erase(it); return; }
}

void CitySchedule::setDisasterActive(bool active) {
  if (active == disasterActive_) return;
  disasterActive_ = active;
  if (!clock_) return;
  if (active) minuteListener_ = clock_->addListener(Clock::Minute, [this](Clock&) { runMinute(); });
  else { clock_->removeListener(minuteListener_); minuteListener_ = 0; }
}

void CitySchedule::attach(Clock& c) {
  clock_ = &c;
  c.addListener(Clock::Day, [this](Clock&) { runDay(); });
  c.addListener(Clock::Week, [this](Clock&) { runWeek(); });
  c.addListener(Clock::Month, [this](Clock&) { runMonth(); });
  c.addListener(Clock::Year, [this](Clock&) { runYear(); });
  if (disasterActive_) minuteListener_ = c.addListener(Clock::Minute, [this](Clock&) { runMinute(); });
}

void CitySchedule::run(Step s) {
  // Spec 14 §6.3 rule 6 [STATIC]: hold wait — pause∨pending/modal counts (§2.3); not busy.
  if (isHold) {
    int poll = holdPollMs > 0 ? holdPollMs : 1000;
    while (isHold() && (!stopRequested || !stopRequested())) {
      std::this_thread::sleep_for(std::chrono::milliseconds(poll));
    }
  }
  // Spec 14 §6.3 rule 6 [STATIC]: backlog pause — pending-work > 3 or message queue > 500
  // (item counts, not ms). Same busy/clock-pause path as monthly begin; busy ≠ hold.
  if (!cascadePaused_ && clock_ && !clock_->paused()) {
    int pw = pendingWorkCount ? pendingWorkCount() : 0;
    int mq = messageQueueCount ? messageQueueCount() : 0;
    if (pw > pendingWorkBacklogLimit || mq > messageQueueBacklogLimit) {
      clock_->pause();
      cascadePaused_ = true;
    }
  }
  if (logging) log_.push_back(s);
  if (handlers_[int(s)]) handlers_[int(s)]();
}

void CitySchedule::runDay() {
  cascadePaused_ = false;
  // Spec 14 §6.1 / Q-BG [STATIC]: shared daily pass (items 1–6, 8–11), then item 7.
  for (Step s : dailySharedSteps()) run(s);
  // Active disaster instance day ticks — separate daily-tier entry after the shared pass.
  if (disasterActive_) run(Step::DailyDisasterInstances);
  if (cascadePaused_ && clock_) {
    clock_->resume();
    cascadePaused_ = false;
  }
  elapsedDays++;
  if (onDay) onDay();
}

void CitySchedule::runWeek() {
  cascadePaused_ = false;
  // Enrolled buildings first (snapshot: buildings added during the step wait until it ends).
  size_t n = buildings_.size();
  std::vector<Fn> snapshot;
  snapshot.reserve(n);
  for (auto& b : buildings_) snapshot.push_back(b.fn);
  for (auto& f : snapshot) if (f) f();
  for (Step s : weeklySteps()) run(s);
  if (cascadePaused_ && clock_) {
    clock_->resume();
    cascadePaused_ = false;
  }
  if (onWeek) onWeek();
}

void CitySchedule::runMonth() {
  if (clock_) clock_->pause();
  // Spec 14 §6.1 / Q-BF [STATIC]: dedicated monthly-tier entries (items 1–3: disaster manager,
  // neighbours, world) before the shared monthly pass that drives items 4–16.
  for (Step s : monthlyDedicatedSteps()) {
    if (s == Step::MonthlyDisasterManager && !disastersEnabled) continue;
    run(s);
  }
  for (Step s : monthlySharedSteps()) run(s);
  if (onMonth) onMonth();
  if (clock_) clock_->resume();
}

void CitySchedule::runYear() {
  // No yearly cascade: only the year handler and anniversaries.
  if (onYear) onYear();
  if (clock_) {
    int age = clock_->date().year - clock_->foundingYear();
    uint32_t item = anniversaryNewsItem(age);
    if (item && onAnniversary) onAnniversary(item);
  }
}

void CitySchedule::runMinute() {
  cascadePaused_ = false;
  for (Step s : minuteSteps()) run(s);
  if (cascadePaused_ && clock_) {
    clock_->resume();
    cascadePaused_ = false;
  }
}

}  // namespace m3k
