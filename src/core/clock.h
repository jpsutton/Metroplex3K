// Simulation clock, calendar, speeds, RNG and update scheduling. See spec/14-sim-clock.md.
// Pure state classes: no wall-clock access; callers pass millisecond timestamps.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace m3k {

// ---------------------------------------------------------------- calendar (spec 14 s1)
namespace cal {

struct Date {
  int month = 1, day = 1, year = 1900;
  bool operator==(const Date& o) const { return month == o.month && day == o.day && year == o.year; }
};

int32_t toJdn(int month, int day, int year);
Date fromJdn(int32_t jdn);
int dayOfYear(int32_t jdn);        // 1 January = 1
int weekday(int32_t jdn);          // 1 = Monday .. 7 = Sunday
int daysInMonth(int month, int year);
bool isLeap(int year);
// Year 0 special case reproduces the original off-by-one (day 1 -> 2 Jan 1901).
int32_t jdnFromDayOfYear(int dayOfYear, int year);
// Coarse (month + 12*year)*30 + day stamp used by other subsystems (spec 14 s1.2).
int32_t dateStamp(int month, int day, int year);
// SimCity 2000 import (spec 14 s9). Returns date and founding year; n < 0 means "missing".
struct ImportedDate { Date date; int foundingYear; };
ImportedDate importSc2(bool haveCount, int32_t elapsedDays, bool haveStartYear, int startYear);

}  // namespace cal

// ---------------------------------------------------------------- RNG (spec 14 s8)
// Simulation integer + float generator pair.
class SimRng {
 public:
  explicit SimRng(uint32_t seed = 1) { seed_(seed); }
  void seed(uint32_t x) { seed_(x); }
  uint32_t nextRaw();                 // one integer-generator output R
  uint32_t rand(uint32_t n);          // [0,n-1]; n >= 0xFFFFFF: [0,n] inclusive (rejection)
  int32_t randInt(int32_t a, int32_t b) { return a + int32_t(rand(uint32_t(b - a))); }
  double nextReal();                  // u in [0,1) from the float generator
  double randRange(double a, double b) { return a + (b - a) * nextReal(); }
  uint32_t intState() const { return s_; }
  uint32_t realState() const { return t_; }
 private:
  void seed_(uint32_t x) { s_ = x; t_ = x; }
  uint32_t s_ = 0, t_ = 0;
};

// Microsoft C runtime rand() (shared process-wide in the original).
class CRand {
 public:
  explicit CRand(uint32_t seed = 1) : s_(seed) {}
  void seed(uint32_t x) { s_ = x; }
  int rand() { s_ = s_ * 214013u + 2531011u; return int((s_ >> 16) & 0x7FFF); }
 private:
  uint32_t s_;
};

// ---------------------------------------------------------------- real-time clock (spec 14 s2)
class Clock {
 public:
  enum Period { Minute, Hour, Day, Week, Month, Year, kPeriods };
  using Listener = std::function<void(Clock&)>;
  using ListenerId = int;

  Clock();

  // Compression factor F (sim seconds per real second); default 1440.
  void setCompression(uint32_t f);
  uint32_t compression() const { return f_; }
  uint32_t msPerDay() const { return msPerDay_; }
  uint32_t msPerHour() const { return msPerHour_; }
  uint32_t msPerMinute() const { return msPerMinute_; }

  // Date (JDN). Default 1 Jan 1900.
  int32_t jdn() const { return jdn_; }
  void setJdn(int32_t j) { jdn_ = j; }
  cal::Date date() const { return cal::fromJdn(jdn_); }
  void setDate(int month, int day, int year) { jdn_ = cal::toJdn(month, day, year); }
  int foundingYear() const { return foundingYear_; }
  void setFoundingYear(int y) { foundingYear_ = y; }
  int minuteCounter() const { return minute_; }
  int hourCounter() const { return hour_; }

  // Optional time source used by pause()/resume() without an explicit timestamp.
  void setTimeSource(std::function<uint32_t()> f) { timeSource_ = std::move(f); }
  uint32_t now() const { return timeSource_ ? timeSource_() : lastNow_; }

  void start(uint32_t nowMs) { last_ = lastNow_ = nowMs; acc_ = 0; }
  // Timer callback (spec 2.2). Call at least every 50 ms.
  void tick(uint32_t nowMs);

  // Counted pause (spec 2.3).
  void pause(uint32_t nowMs);
  void resume(uint32_t nowMs);
  void pause() { pause(now()); }
  void resume() { resume(now()); }
  int pauseCount() const { return pauseCount_; }
  bool paused() const { return pauseCount_ > 0; }
  uint32_t accumulator() const { return acc_; }

  ListenerId addListener(Period p, Listener fn);
  bool removeListener(ListenerId id);
  size_t listenerCount(Period p) const;
  bool minuteMode() const { return listenerCount(Minute) + listenerCount(Hour) > 0; }
  // One-shot alarm; accepted only if jdn is strictly later than the current date.
  bool addAlarm(int32_t jdn, Listener fn);

  // Direct tick entry points (used by tick() and by tests).
  void minuteTick();
  void hourTick();
  void dayTick();

 private:
  struct Entry { ListenerId id; Period p; Listener fn; };
  void notify(Period p);
  uint32_t f_ = 1440, msPerDay_ = 60000, msPerHour_ = 2500, msPerMinute_ = 50;
  int32_t jdn_ = 2415021;
  int foundingYear_ = 1900;
  int minute_ = 0, hour_ = 0;
  uint32_t last_ = 0, lastNow_ = 0, acc_ = 0;
  int pauseCount_ = 0;
  ListenerId nextId_ = 1;
  std::vector<Entry> listeners_;
  struct Alarm { int32_t jdn; Listener fn; };
  std::vector<Alarm> alarms_;
  std::function<uint32_t()> timeSource_;
};

// ---------------------------------------------------------------- speeds (spec 14 s4)
class SpeedControl {
 public:
  static constexpr uint32_t kFactor[5] = {120, 10080, 43200, 129600, 525600};
  static const char* name(int speed);
  int current() const { return current_; }
  int selected() const { return selected_; }
  bool simPaused() const { return pauseCount_ > 0; }
  int pauseCount() const { return pauseCount_; }
  uint32_t factor() const { return kFactor[current_]; }

  // Set-speed operation. Returns false when rejected. `changed` receives the announced speed.
  bool setSpeed(int s);
  // City-level counted pause. First pause announces "simulation paused" (returns true on 0->1).
  bool pause();
  bool resume();   // returns true on 1->0
  void applyTo(Clock& c) const { c.setCompression(factor()); }
  // SC2 import: speed 0..3 -> 1..4, default 1 when absent.
  static int importSc2Speed(bool have, int sc2Speed) { return have ? sc2Speed + 1 : 1; }

  std::function<void(int selected)> onSpeedChanged;
  std::function<void()> onPaused;

 private:
  int current_ = 2, selected_ = 2, pauseCount_ = 0;
};

// ---------------------------------------------------------------- scheduling (spec 14 s3, s5, s6)
// Subsystem numbers of spec 14 section 5 (row numbers).
enum class Subsystem : int {
  Terrain = 1, OccupantManager, NetworkManager, FloraLayer, ZoneManager, Disasters, Neighbours,
  RciLayer, Ordinances, Aura, Pollution, Crime, Police, Fire, ResidentialLayer, CommercialLayer,
  IndustrialLayer, LandValue, StreetSim, Traffic, Budget, WorldLayer, Power, Water, Flammability,
  SpacingGrid, Unknown27, Geometry28, Scenario, AdvisorManager, PetitionerManager = 37
};

enum class Step : int {
  // daily (in order)
  DailyDisasters, DailyPolice, DailyFire, DailyLandValue, DailyScenario, DailyZoneDev,
  DailyDisasterInstances, DailyPollution, DailyCrime, DailyFlammability, DailyAura,
  // weekly
  WeeklyStreetSim,
  // monthly (in order)
  MonthlyDisasterManager, MonthlyNeighbours, MonthlyWorldLayer, MonthlyRci, MonthlyOrdinances,
  MonthlyResidential, MonthlyCommercial, MonthlyIndustrial, MonthlyTraffic, MonthlyBudget,
  MonthlyPower, MonthlyWater, MonthlyUnknown27, MonthlyPollution, MonthlyCrime,
  // per minute (only while a disaster instance is active)
  MinuteDisasterInstances, MinuteScenario,
  kCount
};

const char* stepName(Step s);
// Run order of each cascade. Daily omits DailyAura unless auraDaily (recommended default, spec 29 2.11).
std::vector<Step> dailySteps(bool auraDaily = true);
std::vector<Step> weeklySteps();
std::vector<Step> monthlySteps();
std::vector<Step> minuteSteps();

// Anniversary news item for a city age in years; 0 if none (spec 14 s3.2).
uint32_t anniversaryNewsItem(int ageYears);

class CitySchedule {
 public:
  using Fn = std::function<void()>;
  void setHandler(Step s, Fn fn) { handlers_[int(s)] = std::move(fn); }
  // City handlers run after each period's cascade.
  Fn onDay, onWeek, onMonth, onYear;
  // Called with the news item id on anniversaries.
  std::function<void(uint32_t newsItem)> onAnniversary;

  bool auraDaily = true;
  bool disastersEnabled = true;   // monthly disaster-manager step only then (spec 6.1 monthly 1)

  // Per-building weekly work: runs before the weekly layer steps, in registration order;
  // buildings added during the step wait until it ends. Returns an id.
  int addBuilding(Fn fn);
  void removeBuilding(int id);

  // Disaster activity switches minute mode on/off on the clock.
  void setDisasterActive(bool active);
  bool disasterActive() const { return disasterActive_; }

  // Register day/week/month/year (and minute) listeners on the clock. Do once.
  void attach(Clock& clock);

  // Cascades, callable directly. The month cascade pauses the clock if attached.
  void runDay();
  void runWeek();
  void runMonth();
  void runYear();
  void runMinute();

  uint32_t elapsedDays = 0;   // city day counter (spec 3.2)
  int cascadeLog(size_t i) const { return i < log_.size() ? int(log_[i]) : -1; }
  void clearLog() { log_.clear(); }
  bool logging = false;       // when true, every executed step is appended to the log (for tests)

 private:
  void run(Step s);
  Fn handlers_[int(Step::kCount)];
  struct Building { int id; Fn fn; };
  std::vector<Building> buildings_;
  int nextBuilding_ = 1;
  Clock* clock_ = nullptr;
  bool disasterActive_ = false;
  Clock::ListenerId minuteListener_ = 0;
  std::vector<Step> log_;
};

}  // namespace m3k
