// News ticker (spec/22-advisors-news-petitioners.md section 4). Platform independent; text comes from a
// caller-supplied resolver (GameData::string), presentation is left to the shell.
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "core/bytes.h"
#include "core/disasters.h"
#include "core/gamerng.h"
#include "core/ini.h"

namespace m3k {

// String-table groups (section 2)
constexpr uint32_t kTblAdvisorHeading = 0x825ADC5A, kTblAdvisorMessage = 0x225ADC6B, kTblAdvisorTicker = 0xC25B11C3;
constexpr uint32_t kTblNewsTriggered = 0x42C1ED2D, kTblNewsTriggeredMBE = 0x547BA060, kTblNewsRandom = 0xC2498F0E;

struct NewsKey { uint32_t table = 0, id = 0; bool valid() const { return table != 0; } };

// Argument entry used in saved topic / petition lists (spec P.1).
struct TopicArg { uint32_t index = 0; std::string name; uint32_t a = 0, b = 0, value = 0; };

struct TickerStyle {
  int cls = 6;                       // queue 1..7
  uint8_t r = 255, g = 255, b = 255; // text colour
  bool underline = false;
  int repeats = 1;                   // 0 = until expired
};

// SC3TickerData.INI family: `0xTABLE,0xID=class,R,G,B,r2,g2,b2,underline,repeats`.
class TickerDb {
 public:
  // File names from SC3Newscast.INI [TickerData Files] (entry=<name>).
  static std::vector<std::string> fileList(const IniFile& newscast);
  // Add one data file (call in listed order). First definition of a key wins; class-7 lines always join the pool.
  void addFile(const IniFile& f);
  TickerStyle style(uint32_t table, uint32_t id) const;   // defaults: class 6, white, no underline, 1 repeat
  bool has(uint32_t table, uint32_t id) const { return map_.count({table, id}) != 0; }
  const std::vector<NewsKey>& pool() const { return pool_; }
  size_t size() const { return map_.size(); }
 private:
  std::map<std::pair<uint32_t, uint32_t>, TickerStyle> map_;
  std::vector<NewsKey> pool_;
};

struct NewsItem {
  uint32_t number = 0, table = 0, id = 0;
  std::string text;
  uint8_t r = 255, g = 255, b = 255;
  bool underline = false;
  int repeats = 1;
  int32_t postDate = 0;
  int cls = 6;
  bool hasLoc = false; int locX = 0, locZ = 0;
  bool filler = false;
};

// Date stamp (month + 12*year)*30 + day (spec 14 section 1.2): month 1..12, day 1..31.
inline int32_t tickerStamp(int month, int day, int year) { return (month + 12 * year) * 30 + day; }

class NewsTicker {
 public:
  using TextFn = std::function<std::string(uint32_t table, uint32_t id)>;
  static constexpr size_t kQueueCap = 64;
  static int expiryDays(int cls);   // 1080,1080,90,1080,1080,270

  void setDb(const TickerDb* db) { db_ = db; }
  void setTextFn(TextFn f) { text_ = std::move(f); }
  void clear();                     // new/loaded city
  // Posts an item. Returns its item number, or 0 if the string cannot be found (and no override given).
  uint32_t post(uint32_t table, uint32_t id, int32_t today, bool hasLoc = false, int x = 0, int z = 0,
                const std::string* textOverride = nullptr);
  uint32_t post(NewsKey k, int32_t today, bool hasLoc = false, int x = 0, int z = 0) { return post(k.table, k.id, today, hasLoc, x, z); }
  bool withdraw(uint32_t number);   // searches all six queues
  // Selection of the next message (section 4.3). Always yields an item (random filler as last resort).
  NewsItem next(int32_t today, GameRng& rng);
  size_t queueSize(int cls) const { return cls >= 1 && cls <= 6 ? q_[cls - 1].size() : 0; }
  const std::deque<NewsItem>& queue(int cls) const { return q_[cls - 1]; }
  uint32_t lastShown() const { return last_; }
  int run() const { return run_; }

 private:
  const TickerDb* db_ = nullptr;
  TextFn text_;
  std::deque<NewsItem> q_[6];
  uint32_t nextNumber_ = 1, last_ = 0;
  int run_ = 0;
  void purge(int cls, int32_t today);
  NewsItem show(int cls);
};

// Scrolling constants (section 4.5)
struct TickerScroll {
  static constexpr int kStepPixels = 2, kStepMs = 50, kGapPixels = 20;
  // Pixels to advance after `elapsedMs` since the last step (0 if <= 50 ms).
  static int advance(int elapsedMs) { return elapsedMs > kStepMs ? kStepPixels : 0; }
};

// ---- Event news sources (section 4.7) ------------------------------------------------------
// Ticker item for a disaster start. `buildingOnFireTile` only matters for fire. Returns invalid for the parade.
NewsKey disasterStartNews(DisasterType t, GameRng& rng, bool buildingOnFireTile);
// Building completed. countAfter = city count of that agent type after the addition. isLandmarkFamily = descends from 18000.
NewsKey buildingNews(uint32_t agentType, int countAfter, bool isLandmarkFamily, GameRng& rng);
// City start/load items. mode per 01-architecture; advisorCount = advisors present.
std::vector<NewsKey> cityStartNews(int gameMode, int advisorCount, GameRng& rng);
NewsKey anniversaryNews(int cityAgeYears);            // invalid unless 10,25,50,100,250,500,1000,2000
NewsKey cheatNews(int index);                         // index 0..28 -> 0x325 + index

// ---- Sinks used by advisors / petitioners ----------------------------------------------------
class TickerSink {
 public:
  virtual ~TickerSink() = default;
  // Post a ticker line for (table,id); returns item number or 0 if no string. hasLoc attaches a map location.
  virtual uint32_t post(uint32_t table, uint32_t id, bool hasLoc, int x, int z) = 0;
  virtual void withdraw(uint32_t item) = 0;
  virtual bool hasString(uint32_t table, uint32_t id) = 0;
};
class NewsTickerSink : public TickerSink {
 public:
  NewsTickerSink(NewsTicker& t, std::function<int32_t()> today) : t_(t), today_(std::move(today)) {}
  uint32_t post(uint32_t table, uint32_t id, bool hasLoc, int x, int z) override {
    return t_.post(table, id, today_(), hasLoc, x, z);
  }
  void withdraw(uint32_t item) override { if (item) t_.withdraw(item); }
  bool hasString(uint32_t table, uint32_t id) override { return !textOf(table, id).empty(); }
  NewsTicker::TextFn text;
 private:
  std::string textOf(uint32_t table, uint32_t id) { return text ? text(table, id) : std::string("x"); }
  NewsTicker& t_;
  std::function<int32_t()> today_;
};

}  // namespace m3k
