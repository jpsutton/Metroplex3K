// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/news.h"
#include <algorithm>
#include <cstdlib>

namespace m3k {

// ---------------------------------------------------------------------------------------------
// TickerDb
// ---------------------------------------------------------------------------------------------
std::vector<std::string> TickerDb::fileList(const IniFile& newscast) {
  std::vector<std::string> v;
  for (auto& kv : newscast.section("TickerData Files"))
    if (iequals(kv.first, "entry")) v.push_back(kv.second);
  return v;
}

void TickerDb::addFile(const IniFile& f) {
  for (auto& kv : f.section("Sc3TickerData")) {
    size_t comma = kv.first.find(',');
    if (comma == std::string::npos) continue;
    uint32_t table = parseIdNumber(kv.first.substr(0, comma)), id = parseIdNumber(kv.first.substr(comma + 1));
    std::vector<uint32_t> n;
    std::string s = kv.second;
    size_t p = 0;
    while (p <= s.size() && n.size() < 9) {
      size_t e = s.find(',', p);
      std::string t = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
      n.push_back(parseIdNumber(t));
      if (e == std::string::npos) break;
      p = e + 1;
    }
    if (n.size() < 9) continue;
    TickerStyle st;
    st.cls = int(n[0]); st.r = uint8_t(n[1]); st.g = uint8_t(n[2]); st.b = uint8_t(n[3]);
    st.underline = n[7] != 0; st.repeats = int(n[8]);
    map_.emplace(std::make_pair(table, id), st);          // first definition wins
    if (st.cls == 7) pool_.push_back({table, id});         // duplicates raise the weight
  }
}

TickerStyle TickerDb::style(uint32_t table, uint32_t id) const {
  auto it = map_.find({table, id});
  return it == map_.end() ? TickerStyle() : it->second;
}

// ---------------------------------------------------------------------------------------------
// NewsTicker
// ---------------------------------------------------------------------------------------------
int NewsTicker::expiryDays(int cls) {
  switch (cls) { case 3: return 90; case 6: return 270; default: return 1080; }
}

void NewsTicker::clear() {
  for (auto& q : q_) q.clear();
  last_ = 0; run_ = 0;
}

uint32_t NewsTicker::post(uint32_t table, uint32_t id, int32_t today, bool hasLoc, int x, int z,
                          const std::string* textOverride) {
  NewsItem it;
  it.table = table; it.id = id;
  if (textOverride) it.text = *textOverride;
  else if (text_) { it.text = text_(table, id); if (it.text.empty()) return 0; }
  TickerStyle st = db_ ? db_->style(table, id) : TickerStyle();
  it.cls = st.cls < 1 ? 1 : st.cls > 6 ? 6 : st.cls;
  it.r = st.r; it.g = st.g; it.b = st.b; it.underline = st.underline || hasLoc; it.repeats = st.repeats;
  it.postDate = today;
  it.hasLoc = hasLoc; it.locX = x; it.locZ = z;
  it.number = nextNumber_++;
  auto& q = q_[it.cls - 1];
  if (q.size() >= kQueueCap) q.pop_front();
  q.push_back(it);
  return it.number;
}

bool NewsTicker::withdraw(uint32_t number) {
  bool any = false;
  for (auto& q : q_)
    for (size_t i = q.size(); i-- > 0;)
      if (q[i].number == number) { q.erase(q.begin() + long(i)); any = true; }
  return any;
}

void NewsTicker::setLink(uint32_t number, uint8_t link, uint32_t linkId) {
  for (auto& q : q_)
    for (auto& it : q)
      if (it.number == number) { it.link = link; it.linkId = linkId; it.underline = it.underline || link != LinkNone; }
}

void NewsTicker::purge(int cls, int32_t today) {
  auto& q = q_[cls - 1];
  int lim = expiryDays(cls);
  for (size_t i = q.size(); i-- > 0;)
    if (today - q[i].postDate > lim) q.erase(q.begin() + long(i));
}

NewsItem NewsTicker::show(int cls) {
  auto& q = q_[cls - 1];
  NewsItem it = q.front();
  q.pop_front();
  last_ = it.number;
  if (it.repeats == 0) q.push_back(it);
  else if (it.repeats - 1 > 0) { NewsItem c = it; c.repeats = it.repeats - 1; q.push_back(c); }
  ++run_;
  return it;
}

NewsItem NewsTicker::next(int32_t T, GameRng& rng) {
  auto headOk = [&](int cls) { return !q_[cls - 1].empty() && q_[cls - 1].front().number != last_; };
  purge(1, T);
  if (!q_[0].empty()) return show(1);
  purge(2, T);
  if (headOk(2)) return show(2);
  purge(4, T);
  if (headOk(4) && T - q_[3].front().postDate > 360) return show(4);
  purge(3, T);
  if (headOk(3)) return show(3);
  if (run_ < 5) {
    bool young = false;
    for (auto& i : q_[3]) if (T - i.postDate < 360) young = true;
    if (young && headOk(4)) return show(4);
    purge(5, T);
    if (headOk(5)) return show(5);
    purge(6, T);
    if (headOk(6)) return show(6);
  }
  // random filler
  NewsItem f;
  f.filler = true; f.cls = 7;
  if (db_ && !db_->pool().empty()) {
    NewsKey k = db_->pool()[rng.rand(uint32_t(db_->pool().size()))];
    f.table = k.table; f.id = k.id;
    TickerStyle st = db_->style(k.table, k.id);
    f.r = st.r; f.g = st.g; f.b = st.b;
    if (text_) f.text = text_(k.table, k.id);
  }
  if (f.text.empty()) { f.r = f.g = f.b = 255; }   // blank white placeholder
  f.repeats = 1;
  run_ = 0;
  return f;
}

// ---------------------------------------------------------------------------------------------
// Event news (section 4.7)
// ---------------------------------------------------------------------------------------------
NewsKey disasterStartNews(DisasterType t, GameRng& rng, bool buildingOnFireTile) {
  auto r = [&](uint32_t n) { return rng.rand(n); };
  switch (t) {
    case DisasterType::ToxicCloud: return {kTblNewsTriggeredMBE, 0x67 + r(3)};
    case DisasterType::Tornado: return {kTblNewsTriggered, 0x25D + r(3)};
    case DisasterType::Whirlpool: return {kTblNewsTriggeredMBE, 0x73 + r(3)};
    case DisasterType::Locusts: return {kTblNewsTriggeredMBE, 0x7E + r(3)};
    case DisasterType::SpaceJunk: return {kTblNewsTriggeredMBE, 0x8D + r(2)};
    case DisasterType::Riot: return {kTblNewsTriggered, 0x26C + r(3)};
    case DisasterType::Earthquake: return {kTblNewsTriggered, 0x24B + r(3)};
    case DisasterType::UFO: return {kTblNewsTriggered, 0x234 + r(3)};
    case DisasterType::Fire:
      return buildingOnFireTile ? NewsKey{kTblNewsTriggered, 0x27C + r(4)} : NewsKey{kTblNewsTriggered, 0x280 + r(2)};
    case DisasterType::Parade: break;
  }
  return NewsKey();
}

NewsKey buildingNews(uint32_t a, int count, bool landmark, GameRng& rng) {
  struct Row { uint32_t type; uint32_t id; int need; };  // need: 0 any, 1 first only
  static const Row rows[] = {
      {1001, 0x2A9, 0}, {13872, 0x2AF, 0}, {1002, 0x2B4, 0}, {10110, 0x2A4, 0}, {10120, 0x2A5, 0},
      {11001, 0x2AB, 0}, {11002, 0x2AA, 0}, {12350, 0x2B5, 0}, {12810, 0x2BB, 1}, {12820, 0x2BE, 0},
      {12830, 0x2BD, 0}, {12840, 0x2BC, 0}, {12851, 0x2C0, 1}, {12852, 0x2BF, 1}, {13002, 0x2B9, 1},
      {13010, 0x2B6, 1}, {13130, 0x2BA, 1}, {13310, 0x2B8, 1}, {13871, 0x2B2, 0}, {13873, 0x2B7, 1},
      {13874, 0x2B0, 0}, {14003, 0x2AC, 0}, {14170, 0x2B1, 0}, {14312, 0x29D, 0}, {14313, 0x29E, 0},
      {14314, 0x29F, 0}, {14325, 0x2B3, 0}, {14370, 0x2AE, 0}, {14430, 0x2A1, 0}, {14450, 0x2A0, 0},
      {14550, 0x2AD, 0}, {14600, 0x2A2, 0}, {19001, 0x2A6, 0}, {19002, 0x2A8, 0}, {19003, 0x2A3, 0},
      {19004, 0x2A7, 0},
  };
  if (a == 12210) {
    if (count == 1) return {kTblNewsTriggered, 0x20A};
    if (count == 10) return {kTblNewsTriggered, 0x20B};
    return NewsKey();
  }
  if (a == 20001) { static const uint32_t v[3] = {0x3E8, 0x3EB, 0x3EC}; return {kTblNewsTriggered, v[rng.rand(3)]}; }
  if (a == 20002) { static const uint32_t v[3] = {0x3EE, 0x3EF, 0x3F0}; return {kTblNewsTriggered, v[rng.rand(3)]}; }
  for (auto& r : rows)
    if (r.type == a) return (r.need == 1 && count != 1) ? NewsKey() : NewsKey{kTblNewsTriggered, r.id};
  if (landmark) return {kTblNewsTriggered, 0x1FC + rng.rand(12)};
  return NewsKey();
}

std::vector<NewsKey> cityStartNews(int mode, int advisorCount, GameRng& rng) {
  std::vector<NewsKey> v;
  auto add = [&](uint32_t id) { v.push_back({kTblNewsTriggered, id}); };
  // 0x2F9 is the "Simulation Paused" item (class 1, repeats=0). The shell keeps its item number and
  // withdraws it on Resume so it cannot monopolise the strip while the sim is running (spec 22 4.7.2).
  if (mode == 3) { add(0x2ED + rng.rand(6)); add(0x2F9); }
  else if (mode == 7) {
    for (uint32_t i = 0x64; i <= 0x68; ++i) add(i);
    uint32_t id = 0x69;
    for (int i = 0; i < advisorCount && id <= 0x72; ++i) add(id++);
    for (; id <= 0x72; ++id) add(id);
  } else add(0x2F9);
  return v;
}

NewsKey anniversaryNews(int age) {
  static const int ages[8] = {10, 25, 50, 100, 250, 500, 1000, 2000};
  for (int i = 0; i < 8; ++i) if (ages[i] == age) return {kTblNewsTriggered, 0x1F4u + uint32_t(i)};
  return NewsKey();
}
NewsKey cheatNews(int index) {
  if (index < 0 || index > 0x341 - 0x325) return NewsKey();
  return {kTblNewsTriggered, 0x325u + uint32_t(index)};
}

}  // namespace m3k
