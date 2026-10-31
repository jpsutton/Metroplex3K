#include "core/advisors.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace m3k {

namespace {
int64_t parseI64(const std::string& s) { return std::strtoll(s.c_str(), nullptr, 0); }
std::vector<std::string> splitSemi(const std::string& s) {
  std::vector<std::string> v;
  size_t p = 0;
  while (true) {
    size_t e = s.find(';', p);
    v.push_back(s.substr(p, e == std::string::npos ? std::string::npos : e - p));
    if (e == std::string::npos) break;
    p = e + 1;
  }
  return v;
}
}  // namespace

const std::vector<uint32_t>& allAdvisorIds() {
  static const std::vector<uint32_t> v = {kAdvBudget, kAdvPlanner, kAdvDemographics, kAdvEnvironment,
                                          kAdvSafety, kAdvTransport, kAdvUtilities};
  return v;
}
const char* advisorIniName(uint32_t id) {
  switch (id) {
    case kAdvBudget: return "Advisor-Budget.ini";
    case kAdvPlanner: return "Advisor-CityPlanner.ini";
    case kAdvDemographics: return "Advisor-Demographics.ini";
    case kAdvEnvironment: return "Advisor-Environment.ini";
    case kAdvSafety: return "Advisor-PublicSafety.ini";
    case kAdvTransport: return "Advisor-Transportation.ini";
    case kAdvUtilities: return "Advisor-Utilities.ini";
  }
  return "";
}

// ---------------------------------------------------------------------------------------------
// Moods, pictures, text
// ---------------------------------------------------------------------------------------------
void MoodTable::load(const IniFile& base, const IniFile* gold) {
  for (auto& kv : base.section("AdvisorMoods")) base_[parseIdNumber(kv.first)] = int(parseI64(kv.second));
  if (gold) for (auto& kv : gold->section("AdvisorMoods")) gold_[parseIdNumber(kv.first)] = int(parseI64(kv.second));
}
int MoodTable::mood(uint32_t t) const {
  auto i = base_.find(t);
  if (i != base_.end()) return i->second;
  auto j = gold_.find(t);
  return j != gold_.end() ? j->second : 0;
}
void TopicGfxTable::load(const IniFile& base, const IniFile* gold) {
  auto rd = [&](const IniFile& f, bool over) {
    for (auto& kv : f.section("AdvisorTopicGfxIDs")) {
      uint32_t t = parseIdNumber(kv.first);
      if (!over && v.count(t)) continue;
      size_t c = kv.second.find(',');
      if (c == std::string::npos) continue;
      v[t] = {parseIdNumber(kv.second.substr(0, c)), parseIdNumber(kv.second.substr(c + 1))};
    }
  };
  rd(base, true);
  if (gold) rd(*gold, false);
}
bool TopicGfxTable::get(uint32_t t, uint32_t& g, uint32_t& i) const {
  auto it = v.find(t);
  if (it == v.end()) return false;
  g = it->second.first; i = it->second.second;
  return true;
}

std::string topicText(const std::string& raw, const TextContext& ctx, const std::vector<TopicArg>& args) {
  std::string s = expandTokens(raw, ctx);
  for (auto& a : args) {
    if (a.name.empty()) continue;
    std::string rep = std::to_string(a.value);
    size_t p = 0;
    while ((p = s.find(a.name, p)) != std::string::npos) { s.replace(p, a.name.size(), rep); p += rep.size(); }
  }
  return s;
}

// ---------------------------------------------------------------------------------------------
// TopicList (3.4)
// ---------------------------------------------------------------------------------------------
const TopicInstance* TopicList::find(uint32_t topic) const {
  for (auto& t : v_) if (t.topicId == topic) return &t;
  return nullptr;
}
uint32_t TopicList::postTicker(const TopicInstance& t) {
  if (!sink_ || !sink_->hasString(kTblAdvisorTicker, t.topicId)) return 0;
  bool loc = t.args.size() >= 2;
  return sink_->post(kTblAdvisorTicker, t.topicId, loc, loc ? int(t.args[0].value) : 0, loc ? int(t.args[1].value) : 0);
}
void TopicList::drop(TopicInstance& t) {
  if (sink_ && t.tickerItem) sink_->withdraw(t.tickerItem);
  t.tickerItem = 0;
}

void TopicList::raise(uint32_t topic, uint32_t key, int limit, bool persistent, bool expiring, bool sticky,
                      bool hasLoc, int x, int z) {
  for (size_t i = 0; i < v_.size(); ++i) {
    if (v_[i].topicId != topic) continue;
    if (v_[i].expiring()) { drop(v_[i]); v_.erase(v_.begin() + long(i)); break; }
    v_[i].flags |= kFlagRaised;
    return;
  }
  TopicInstance n;
  n.topicId = topic; n.groupKey = key; n.limit = uint8_t(std::max(0, std::min(255, limit)));
  n.sticky = sticky ? 1 : 0;
  n.flags = kFlagRaised | (persistent ? kFlagPersistent : 0) | (expiring ? kFlagExpiring : 0);
  if (hasLoc) {
    TopicArg a; a.index = 0; a.name = "%X%"; a.value = uint32_t(x);
    TopicArg b; b.index = 1; b.name = "%Y%"; b.value = uint32_t(z);
    n.args = {a, b};
  }
  n.tickerItem = postTicker(n);
  if (key != 0) {
    for (size_t i = v_.size(); i-- > 0;)
      if (v_[i].groupKey == key) { drop(v_[i]); v_.erase(v_.begin() + long(i)); }
  }
  v_.push_back(n);
}

void TopicList::update() {
  std::vector<TopicInstance> keep;
  for (auto t : v_) {
    bool del = false;
    if (t.sticky || t.persistent()) {
      if (t.counter < t.limit) ++t.counter;
      else { t.flags |= kFlagHidden; drop(t); }
    } else if (t.expiring()) {
      if (t.counter < t.limit) ++t.counter; else del = true;
    } else if (t.raised()) {
      if (t.hidden()) {
        if (t.counter < 255) ++t.counter;
        if (t.counter >= t.limit) { t.counter = 0; t.flags &= uint8_t(~kFlagHidden); t.tickerItem = postTicker(t); }
      }
    } else del = true;
    if (del) drop(t); else keep.push_back(t);
  }
  for (auto& t : keep) t.flags &= uint8_t(~kFlagRaised);
  v_.swap(keep);
}

bool TopicList::dismiss(uint32_t topic) {
  for (auto& t : v_) {
    if (t.topicId != topic) continue;
    if (t.limit != 0 || t.persistent() || t.expiring()) { t.flags |= kFlagHidden; drop(t); return true; }
    return false;
  }
  return false;
}
void TopicList::remove(uint32_t topic) {
  for (size_t i = 0; i < v_.size(); ++i)
    if (v_[i].topicId == topic) { drop(v_[i]); v_.erase(v_.begin() + long(i)); return; }
}
void TopicList::clear() { for (auto& t : v_) drop(t); v_.clear(); }
std::vector<uint32_t> TopicList::visibleTopics() const {
  std::vector<uint32_t> r;
  for (auto& t : v_) if (!t.hidden()) r.push_back(t.topicId);
  return r;
}
int TopicList::mood(const MoodTable& m) const {
  int64_t sum = 0; int n = 0;
  for (auto& t : v_) if (!t.hidden()) { sum += m.mood(t.topicId); ++n; }
  return n ? int(sum / n) : 3;
}

Bytes TopicList::save() const {
  Bytes b;
  put16(b, 1); put16(b, 2); put32(b, 0xDEADBEEFu);
  put32(b, 1); put32(b, uint32_t(v_.size())); put32(b, 1); put32(b, 1);
  for (auto& t : v_) {
    put32(b, t.topicId); put32(b, t.tickerItem); put32(b, t.groupKey);
    b.push_back(t.sticky); b.push_back(t.reserved); b.push_back(t.limit); b.push_back(t.counter); b.push_back(t.flags);
    putArgs(b, t.args);
  }
  return b;
}
bool TopicList::load(const Bytes& d) {
  try {
    Reader r(d);
    if (r.u16() != 1) return false;
    r.u16();
    if (r.u32() != 0xDEADBEEFu) return false;
    if (r.u32() != 1) return false;
    uint32_t n = r.u32();
    r.u32(); r.u32();
    if (n > 100000) return false;
    std::vector<TopicInstance> v;
    for (uint32_t i = 0; i < n; ++i) {
      TopicInstance t;
      t.topicId = r.u32(); r.u32(); t.tickerItem = 0; t.groupKey = r.u32();
      t.sticky = r.u8(); t.reserved = r.u8(); t.limit = r.u8(); t.counter = r.u8(); t.flags = r.u8();
      if (!getArgs(r, t.args)) return false;
      v.push_back(t);
    }
    if (r.remaining() != 0) return false;
    v_ = v;
    return true;
  } catch (const FormatError&) { return false; }
}

// ---------------------------------------------------------------------------------------------
// Thresholds
// ---------------------------------------------------------------------------------------------
AdvisorThresholds::AdvisorThresholds() {
  // [OPEN] defaults: spec gives only the budget growth/decline values (0.05 / -0.1 scaled by 1e-4).
  const std::pair<const char*, int64_t> d[] = {
      {"TaxGrowthThreshold", 500}, {"TaxDeclineThreshold", -1000}, {"TREASURY_LOW", 20000}, {"LOSE_GAME_LEVEL", 100000},
      {"WATER_POLLUTION_HIGH", 150}, {"WATER_POLLUTION_MEDIUM", 90}, {"AIR_POLLUTION_HIGH", 150}, {"AIR_POLLUTION_MEDIUM", 90},
      {"POLICE_FUNDING_HIGH", 100}, {"POLICE_FUNDING_MEDIUM", 75}, {"FIRE_FUNDING_HIGH", 100}, {"FIRE_FUNDING_MEDIUM", 75},
      {"HIGH_CRIME_LEVEL", 150}, {"MED_CRIME_LEVEL", 80}, {"FIRE_STATION_MIN_POP", 500}, {"JAIL_MIN_POP", 1000},
      {"ROAD_FUNDING_HIGH", 100}, {"ROAD_FUNDING_MEDIUM", 75}, {"ROAD_FUNDING_LOW", 50}, {"RAIL_FUNDING_HIGH", 100},
      {"RAIL_FUNDING_MEDIUM", 75}, {"ROAD_DAMAGE", 5}, {"HIGH_ROAD_TRAFFIC", 150}, {"MED_ROAD_TRAFFIC", 90},
      {"MED_RAIL_USAGE", 50}, {"HIGH_RAIL_USAGE", 120},
      {"SUBWAY_AVAILABLE", 1910}, {"BUS_STATION_AVAILABLE", 1900}, {"AIRPORT_AVAILABLE", 1920}, {"HIGHWAY_AVAILABLE", 1940},
      {"CARPOOL_TRIGGER_LEVEL", 150}, {"CARPOOL_TRIGGER_YEAR", 1970}, {"CROSSING_GUARD_TRIGGER_LEVEL", 150},
      {"CROSSING_GUARD_TRIGGER_YEAR", 1930}, {"ALTERNATE_DAY_DRIVING_LEVEL", 200}, {"ALTERNATE_DAY_DRIVING_YEAR", 1970},
      {"BUS_STOP_TRIGGER_LEVEL", 100},
      {"SMALL_SIZE_CITY", 5000}, {"MEDIUM_SIZE_CITY", 50000}, {"NON_DEV_SENSITIVITY", 20}, {"LOW_LAND_VALUE", 50},
      {"LOW_AIR_POLLUTION_FARMS", 40}, {"LOW_WATER_POLLUTION_FARMS", 40}, {"COMMERCIAL_CAP_PROBLEM", 100},
      {"INDUSTRIAL_CAP_PROBLEM", 100}, {"RESPONSIVENESS_IN_MONTHS", 6}, {"RESIDENTIAL_DEMAND_POSITIVE", 1000},
      {"COMMERCIAL_DEMAND_POSITIVE", 1000}, {"MIN_HI_TECH_EQ", 80}, {"HI_TECH_JOB_RATIO", 25}, {"PARK_RATIO", 20},
      {"PARK_AURA_CUTOFF", 0},
      {"HOSPITAL_POPULATION_MINIMUM", 3000}, {"SCHOOL_POPULATION_MINIMUM", 1000}, {"LIBRARY_POPULATION_MINIMUM", 20000},
      {"MUSEUM_POPULATION_MINIMUM", 40000}, {"LE_AND_EQ_POPULATION_MINIMUM", 10000}, {"STADIUM_GROWTH_CUTOFF", 50000},
      {"RESIDENTIAL_GROWTH_CAP", 30000}, {"LOW_LIFE_EXPECTANCY", 60}, {"HIGH_LIFE_EXPECTANCY", 80}, {"LOW_EQ_CUTOFF", 60},
      {"AVERAGE_EQ_CUTOFF", 100}, {"HIGH_EQ_CUTOFF", 140}, {"HIGH_DREARY_RATING", 60}, {"HOSPITAL_RATIO", 20000},
      {"CITY_AURA_LOW", -20}, {"CITY_AURA_HIGH", 40}, {"LOW_DREARY_RATING", 20}, {"CITY_AURA_DREARY_POPULATION", 10000},
      {"ZOO_POPULATION_MINIMUM", 20000}, {"MARINA_POPULATION_MINIMUM", 15000},
      {"POWER_CONSUMPTION_CRITICAL", 99}, {"POWER_CONSUMPTION_HIGH", 90}, {"POWER_CONSUMPTION_MEDIUM", 75},
      {"WATER_CONSUMPTION_MEDIUM", 75}, {"MINIMUM_DEAL_POPULATION", 3000}, {"LOW_TREASURY_LEVEL", 20000},
      {"PETITION_0x37_CHANCE", 2},
  };
  for (auto& p : d) v_[p.first] = p.second;
}
int64_t AdvisorThresholds::get(const std::string& k) const {
  auto it = v_.find(k);
  return it == v_.end() ? 0 : it->second;
}
void AdvisorThresholds::loadSections(const IniFile& f, const std::vector<std::string>& sections) {
  for (auto& s : sections)
    for (auto& kv : f.section(s)) v_[kv.first] = parseI64(kv.second);
}
void AdvisorThresholds::loadBuildingLists(const IniFile& f) {
  auto rd = [&](const char* sec, std::vector<Announcement>& out) {
    if (!f.has(sec)) return;
    out.clear();
    for (auto& kv : f.section(sec)) {
      auto parts = splitSemi(kv.second);
      Announcement a;
      a.buildingId = int(parseI64(kv.first));
      if (parts.size() > 1) a.year = int(parseI64(parts[1]));
      if (parts.size() > 2) a.topic = uint32_t(parseI64(parts[2]));
      out.push_back(a);
    }
  };
  rd("POWER_BUILDINGS", power); rd("WATER_BUILDINGS", water); rd("GARBAGE_BUILDINGS", garbage);
}
void AdvisorThresholds::loadAdvisorIni(uint32_t id, const IniFile& f) {
  switch (id) {
    case kAdvBudget: loadSections(f, {"TAX_RATE_CUTOFFS", "TREASURY_NUMBERS"}); break;
    case kAdvPlanner:
      loadSections(f, {"ADVICE_TRIGGERS", "GROWTH_CAP_LIMITS", "DEMAND_RESPONSIVENESS", "DEMAND_CUTOFFS",
                       "REWARD_BUILDINGS_2", "PETITIONER_GENERATION"});
      break;
    case kAdvDemographics: loadSections(f, {"ADVICE_TRIGGERS", "PETITIONER_GENERATION", "FUNDING_RATE_CUTOFFS"}); break;
    case kAdvEnvironment: loadSections(f, {"POLLUTION_RATE_CUTOFFS"}); break;
    case kAdvSafety: loadSections(f, {"FUNDING_RATE_CUTOFFS", "CRIME_RATE_CUTOFFS", "ADVICE_TRIGGERS", "PETITIONER_GENERATION"}); break;
    case kAdvTransport:
      loadSections(f, {"FUNDING_RATE_CUTOFFS", "TRAFFIC_RATE_CUTOFFS", "ADVICE_TRIGGERS", "PETITIONER_GENERATION",
                       "TRANSPORTATION_ADVANCEMENTS"});
      break;
    case kAdvUtilities: loadSections(f, {"CONSUMPTION_RATE_CUTOFFS", "FUNDING_RATE_CUTOFFS", "NEIGHBOR_DEAL_CONSTANTS"}); break;
  }
  loadBuildingLists(f);
}

// ---------------------------------------------------------------------------------------------
// Rewards
// ---------------------------------------------------------------------------------------------
void RewardTable::load(const IniFile& f, const std::set<int>& known) {
  e_.clear();
  auto rd = [&](const char* sec, bool deal) {
    for (auto& kv : f.section(sec)) {
      auto p = splitSemi(kv.second);
      RewardEntry e;
      e.buildingType = int(parseI64(kv.first));
      e.name = p.empty() ? "" : p[0];
      if (p.size() > 1) e.minPopulation = int(parseI64(p[1]));
      if (p.size() > 2) e.minYear = int(parseI64(p[2]));
      if (p.size() > 3) e.petition = int(parseI64(p[3]));
      if (p.size() > 4) e.special = int(parseI64(p[4]));
      e.isDeal = deal;
      if (!known.empty() && !known.count(e.buildingType)) continue;
      e_.push_back(e);
    }
  };
  rd("REWARD_BUILDINGS", false);
  rd("BUSINESS_DEAL_BUILDINGS", true);
  for (auto& kv : f.section("REWARD_BUILDINGS_2")) p_[kv.first] = parseI64(kv.second);
}
RewardEntry* RewardTable::find(int t) {
  for (auto& e : e_) if (e.buildingType == t) return &e;
  return nullptr;
}
void RewardTable::markAvailable(int t) { if (auto* e = find(t)) e->available = true; }
void RewardTable::markOwned(int t, bool v) { if (auto* e = find(t)) e->owned = v; }

void RewardTable::evaluate(const AdvisorInputs& in, PetitionerManager& pm) {
  const int64_t N = in.population;
  const int Y = in.year;
  const auto& P = in.plan;
  for (auto& e : e_) {
    if (e.isDeal || e.owned) continue;
    bool ok = N >= e.minPopulation && Y >= e.minYear;
    if (ok && e.special == 1) {
      switch (e.buildingType) {
        case 1002: ok = P.treasury > param("FUNDS_NEEDED_FOR_STOCK_EXCHANGE"); break;
        case 11001: { auto* m = find(11002); ok = m && m->owned; break; }
        case 12350: ok = P.airportCount > param("AIRPORT_NEEDED_FOR_SPACEPORT"); break;
        case 13871: ok = P.eq > param("EQ_NEEDED_FOR_UNIVERSITY") && P.proReadingEnacted; break;
        case 13872: ok = P.lifeExpectancy > param("LE_NEEDED_FOR_MEDICAL_CENTER"); break;
        case 13874: ok = P.eq > param("EQ_NEEDED_FOR_SCIENCE_CENTER"); break;
        case 14003: ok = P.n14310 > param("PARKS_NEEDED_FOR_NATIONAL_PARK"); break;   // [OPEN] alt. population measure
        case 14325: ok = P.airportCount > 5 && P.tourismEnacted; break;
        case 14550: break;                                                           // [OPEN] alt. population measure
        case 20001: ok = P.holidaysEnabled && P.monthIndex >= 7 && P.monthIndex <= 9; break;
        case 20002: ok = P.holidaysEnabled && P.monthIndex >= 9 && P.monthIndex <= 11 && P.n14310 > 29; break;
        default: break;
      }
    }
    if (ok) {
      switch (e.buildingType) {
        case 1002: case 10110: case 10120: case 13005: case 13871: case 14150: case 14170: e.available = true; break;
        default: break;
      }
      pm.request(uint32_t(e.petition), uint32_t(e.buildingType), 0, true);
    } else if (!e.available) {
      if (pm.visible(uint32_t(e.petition))) pm.withdraw(uint32_t(e.petition));
    }
  }
}

Bytes RewardTable::save() const {
  Bytes body;
  uint32_t count = 0;
  std::set<uint32_t> written;
  auto stateFor = [&](uint32_t inst, uint8_t& owned, uint8_t& avail) {
    for (auto& kv : typeToInstance) if (kv.second == inst)
      for (auto& e : e_) if (e.buildingType == kv.first) { owned = e.owned; avail = e.available; return true; }
    return false;
  };
  for (auto& r : rawEntries) {
    uint8_t o = r.owned, a = r.available;
    stateFor(r.instance, o, a);
    put32(body, r.type); put32(body, r.group); put32(body, r.instance); body.push_back(o); body.push_back(a);
    written.insert(r.instance); ++count;
  }
  for (auto& e : e_) {
    if (!(e.available || e.owned)) continue;
    auto it = typeToInstance.find(e.buildingType);
    if (it == typeToInstance.end() || written.count(it->second)) continue;
    put32(body, 0x207EDC0Eu); put32(body, 0x0000057Eu); put32(body, it->second);
    body.push_back(e.owned); body.push_back(e.available);
    ++count;
  }
  Bytes b;
  put16(b, 1); put16(b, 2); put32(b, 0xDEADBEEFu); put32(b, count);
  b.insert(b.end(), body.begin(), body.end());
  return b;
}
bool RewardTable::load(const Bytes& d) {
  try {
    Reader r(d);
    if (r.u16() != 1) return false;
    r.u16();
    if (r.u32() != 0xDEADBEEFu) return false;
    uint32_t n = r.u32();
    if (r.remaining() != size_t(n) * 14) return false;
    std::vector<Raw> v;
    for (uint32_t i = 0; i < n; ++i) {
      Raw x; x.type = r.u32(); x.group = r.u32(); x.instance = r.u32(); x.owned = r.u8(); x.available = r.u8();
      v.push_back(x);
    }
    rawEntries = v;
    for (auto& e : e_) { e.owned = false; e.available = false; }
    for (auto& x : v)
      for (auto& kv : typeToInstance)
        if (kv.second == x.instance)
          if (auto* e = find(kv.first)) { e->owned = x.owned != 0; e->available = x.available != 0; }
    return true;
  } catch (const FormatError&) { return false; }
}

// ---------------------------------------------------------------------------------------------
// Manager
// ---------------------------------------------------------------------------------------------
AdvisorManager::AdvisorManager() {
  for (uint32_t id : allAdvisorIds()) lists_.emplace_back(id);
}
void AdvisorManager::setSink(TickerSink* s) { for (auto& l : lists_) l.setSink(s); }
TopicList& AdvisorManager::list(uint32_t id) {
  for (auto& l : lists_) if (l.advisorId() == id) return l;
  return lists_.front();
}
const TopicList& AdvisorManager::list(uint32_t id) const {
  for (auto& l : lists_) if (l.advisorId() == id) return l;
  return lists_.front();
}
void AdvisorManager::clear() {
  for (auto& l : lists_) l.clear();
  for (auto& e : rewards.entries()) { e.available = false; e.owned = false; }
  rewards.rawEntries.clear();
  announced_.clear();
  unlocked_.clear(); unlockTopics_.clear(); unlockTopicsEnv_.clear();
  for (auto& a : tAnnounced_) a = false;
  garbageStreak_ = 0;
  for (auto& r : ring_) r.clear();
  clock = CycleClock();
}

bool AdvisorManager::calendarDay(const AdvisorInputs& in, GameRng& rng, AdvisorOutputs& out) {
  if (!clock.dayNotification()) return false;
  evaluateAll(in, rng, out);
  return true;
}
void AdvisorManager::evaluateAll(const AdvisorInputs& in, GameRng& rng, AdvisorOutputs& out) {
  for (uint32_t id : allAdvisorIds()) evaluate(id, in, rng, out);
}

void AdvisorManager::rulesOnly(uint32_t id, const AdvisorInputs& in, GameRng& rng) {
  TopicList& L = list(id);
  AdvisorOutputs dummy;
  switch (id) {
    case kAdvBudget: budgetRules(L, in, dummy); break;
    case kAdvEnvironment: envRules(L, in); break;
    case kAdvSafety: safetyRules(L, in); break;
    case kAdvTransport: transRules(L, in, rng); break;
    case kAdvPlanner: planRules(L, in); break;
    case kAdvDemographics: demoRules(L, in, rng); break;
    case kAdvUtilities: utilRules(L, in); break;
  }
}

void AdvisorManager::evaluate(uint32_t id, const AdvisorInputs& in, GameRng& rng, AdvisorOutputs& out) {
  TopicList& L = list(id);
  switch (id) {
    case kAdvBudget: budgetRules(L, in, out); break;
    case kAdvEnvironment: envRules(L, in); break;
    case kAdvSafety: safetyRules(L, in); break;
    case kAdvTransport: transRules(L, in, rng); break;
    case kAdvPlanner: planRules(L, in); break;
    case kAdvDemographics: demoRules(L, in, rng); break;
    case kAdvUtilities: utilRules(L, in); break;
  }
  L.update();
  postUpdate(id, L, in, rng);
  L.raiseForced();
  out.advisorsUpdated.push_back(id);
}

void AdvisorManager::ordinanceChanged(uint32_t ord, bool enacted) {
  if (!enacted) return;
  switch (ord) {
    case 0x00D0723D: list(kAdvDemographics).event(0x2D, 6); break;   // CPR
    case 0x40D07236: list(kAdvDemographics).event(0x28, 6); break;   // clinics
    case 0x815B4CEF: list(kAdvSafety).event(0x29, 6); break;         // youth sports [INFERRED advisor]
    default: break;                                                   // full table [OPEN]
  }
}

// ---- Budget (3.7) ------------------------------------------------------------------------------
void AdvisorManager::budgetRules(TopicList& L, const AdvisorInputs& in, AdvisorOutputs& out) {
  const auto& B = in.budget;
  const double G = double(thr.get("TaxGrowthThreshold")) * 0.0001, D = double(thr.get("TaxDeclineThreshold")) * 0.0001;
  const int64_t TL = thr.get("TREASURY_LOW"), LG = thr.get("LOSE_GAME_LEVEL");
  static const uint32_t topics[3][3] = {{0x3A, 0x39, 0x38}, {0x3E, 0x3D, 0x3C}, {0x42, 0x41, 0x40}};
  static const uint32_t keys[3] = {0x82A493A5, 0x42A493C2, 0x02A493DD};
  for (int z = 0; z < 3; ++z) {
    double x = B.taxEffect[z]; int r = B.taxRate[z];
    int row = (x < D && r >= 2) ? 0 : (x < G && r != 0) ? 1 : 2;
    L.raise(topics[z][row], keys[z]);
  }
  const int64_t T = B.treasury, I = B.estIncome, E = B.estExpense;
  const int64_t hi = E * 110 / 100, lo = E * 98 / 100;
  if (I <= hi) {
    if (I >= lo) { if (TL < T && T < 3 * TL) L.raise(0x10D, 0xC2EBE5E9); }
    else if (T <= TL + 2 * (E - I)) L.raise(0xD0, 0xC2EBE5E9);
    else L.raise(0x45, 0xC2EBE5E9);
  } else if (T > 4 * TL) L.raise(0x44, 0xC2EBE5E9);
  // reserve
  if (T > 2 * TL + 5 * E && in.population > 10000) L.raise(0x47, 0xC2EBE7F2);
  else if (T >= TL) {}
  else if (T > 0) L.raise(0x49, 0xC2EBE7F2);
  else if (T == 0) {}
  else if (T <= -LG) { L.raise(0xCE, 0xC2EBE7F2); out.gameLost = true; }
  else if (T >= 8 * (LG / -10)) L.raise(in.population <= 50000 ? 0x125 : 0x4A, 0xC2EBE7F2);
  else L.raise(0xCD, 0xC2EBE7F2);
  if (T < TL && !B.anyNeighbourDealActive && !B.trashImportDealKind5Exists && !B.hasBuilding302A)
    L.raise(in.neighbourConnections < 4 ? 0x04 : 0x03, 0x031C8D5D);
}

// ---- Environment (3.8) ------------------------------------------------------------------------
void AdvisorManager::envRules(TopicList& L, const AdvisorInputs& in) {
  for (uint32_t t : unlockTopicsEnv_) L.raise(t, 0, 120, true, true, true);   // garbage-building unlock announcements
  unlockTopicsEnv_.clear();
  if (in.population < 100) return;
  const auto& E = in.env;
  const int64_t WH = thr.get("WATER_POLLUTION_HIGH"), WM = thr.get("WATER_POLLUTION_MEDIUM");
  const int64_t AH = thr.get("AIR_POLLUTION_HIGH"), AM = thr.get("AIR_POLLUTION_MEDIUM");
  if (E.hasWaterSystem) {
    if (E.waterPollution > WH) L.raise(0x7C, 0x82A497F0);
    else if (E.waterPollution > WM) L.raise(0x7B, 0x82A497F0);
    else if (L.listed(0x7B) || L.listed(0x7C)) L.raise(0xF4, 0x82A497F0, 6, true, true);
  }
  if (E.airPollution > AH) L.raise(0x7E, 0x22A497FB);
  else if (E.airPollution > AM) L.raise(0x7F, 0x22A497FB);
  else if (L.listed(0x7E) || L.listed(0x7F)) L.raise(0x80, 0x22A497FB, 6, true, true);
  if (!E.trashExportDealActive) {
    if (!E.garbageAccumulating) {
      if (E.garbageNearCapacity) L.raise(garbageBuildingAvailable(12853) ? 0x77 : 0xF8, 0xC2F8060C);   // WTE unlocked
      else if (E.garbageSurplusWithinLimit) L.raise(0x78, 0xC2F8060C);
    } else if (in.year < 1920) L.raise(0x76, 0xC2F8060C);
    else if (in.year < 1970) L.raise(0xEF, 0xC2F8060C);
    else if (in.year <= 1982) L.raise(0xF0, 0xC2F8060C);
    else L.raise(garbageBuildingAvailable(12851) ? 0xF2 : 0xF1, 0xC2F8060C);   // recycling unlocked
  }
  // Accumulation counter (spec 22 "Garbage metrics") [STATIC]: +1 with A, -1 without (not below 0); "problem solved"
  // when the counter is > 6 and A is clear, and without an export deal also 10 GD < 7 GC.
  if (E.garbageAccumulating) ++garbageStreak_;
  else {
    if (garbageStreak_ > 6 && (E.trashExportDealActive || 10 * E.garbageDemand < 7 * E.garbageCapacity)) L.raise(0xF3, 0, 60, true, true);
    if (garbageStreak_ > 0) --garbageStreak_;
  }
  if (E.garbageAccumulating && garbageStreak_ >= 7 && !E.trashExportDealActive) {
    if (in.neighbourConnections < 4 && !E.hasSeaport) L.raise(0x70, 0x03387EA5);
    else if (in.neighbourConnections >= 1 && !E.anyTrashDeal) L.raise(0x6F, 0x03387EA5);
  }
}

// ---- Public safety (3.9) ----------------------------------------------------------------------
void AdvisorManager::safetyRules(TopicList& L, const AdvisorInputs& in) {
  const auto& S = in.safety;
  const int64_t N = in.population;
  const int64_t PH = thr.get("POLICE_FUNDING_HIGH"), PM = thr.get("POLICE_FUNDING_MEDIUM");
  const int64_t FH = thr.get("FIRE_FUNDING_HIGH"), FM = thr.get("FIRE_FUNDING_MEDIUM");
  const int64_t CH = thr.get("HIGH_CRIME_LEVEL"), CM = thr.get("MED_CRIME_LEVEL");
  const int K = S.crime;
  if (S.policeStrike) L.raise(0x9F, 0x42A498C9);
  else if (S.policeStations == 0) L.raise(0x11F, 0x42A498C9);
  else if (S.policeWarn >= 16) L.raise(0xA0, 0x42A498C9);
  else if (S.policeFunding > PH && K > CM) L.raise(0x132, 0x42A498C9);
  else if (S.policeFunding > PH) L.raise(0xFB, 0x42A498C9);
  else if (S.policeFunding > PM) L.raise(0xA2, 0x42A498C9);
  if (S.fireStrike) L.raise(0xA7, 0x22A498D1);
  else if (S.fireStations == 0) L.raise(N > thr.get("FIRE_STATION_MIN_POP") ? 0xFC : 0x120, 0x22A498D1);
  else if (S.fireWarn >= 16) L.raise(0xA8, 0x22A498D1);
  else if (S.fireFunding > FH) L.raise(0x130, 0x22A498D1);
  else if (S.fireFunding > FM) L.raise(0xAA, 0x22A498D1);
  else L.raise(0xA9, 0x22A498D1);
  if (K > CH) L.raise(0xBC);
  else if (K > CM) L.raise(0xC5);
  if (S.jails == 0) { if (N > 1000 && K > CM) L.raise(0xBA, 0xC358210E); }
  else if (S.jailInmates - S.jailCapacity > 8 * S.jailCapacityTerm) L.raise(0xBB, 0xC358210E);
  else if (S.jailInmates >= S.jailCapacity) L.raise(0xB9, 0xC358210E);
  if (N > 1000) {
    if (S.policeCoverage < 50) L.raise(0x9B, 0xC3582DBE);
    else if (S.policeCoverage < 75) L.raise(0x9C, 0xC3582DBE);
    else if (S.policeCoverage < 99) L.raise(0x9D, 0xC3582DBE);
  }
}

void AdvisorManager::safetyPetitions(const AdvisorInputs& in, GameRng& rng) {
  if (!petitioners) return;
  const auto& S = in.safety;
  const int64_t N = in.population;
  const int64_t CH = thr.get("HIGH_CRIME_LEVEL"), CM = thr.get("MED_CRIME_LEVEL");
  const int K = S.crime;
  auto draw = [&](int pct) { return int(rng.rand(100)) < pct; };
  auto& P = *petitioners;
  if (K > CH && N > 50000 && draw(4)) P.request(0x22, 0, 48);
  if (K > CH && N > 10000 && !S.curfewEnacted && draw(2)) P.request(0x66, 0, 84);
  if (!S.youthSportsEnacted) {
    if (K > CH && N > 10000) { if (draw(3)) P.request(0x6F, 0, 84); }
    else if (K > CM && K <= CH && N > 1000) { if (draw(2)) P.request(0x6F, 0, 84); }
  }
  if (K < CM && N > 100 && S.curfewEnacted && draw(3)) P.request(0x67, 0, 84);
  if (K < CM && N > 100 && S.youthSportsEnacted && draw(3)) P.request(0x70, 0, 84);
  if (K > CM && draw(int(thr.get("PETITION_0x37_CHANCE")))) P.request(0x37, 0, 84);
  if (K < CM && N > 10000 && S.policeCoverage > 98 && draw(5)) P.request(0x23, 0, 60);
  if (K < CM && N > 10000 && S.policeStrength > 105 && draw(5)) P.request(0xD3, 0, 36);
  if (S.fireStations == 0 && draw(3)) P.request(0x32, 0, 24);
  if (S.policeStrike) P.request(0x9F, 0, 6);
  else if (S.policeWarn >= 16) P.request(0xA0, 0, 6);
  if (S.fireStrike) P.request(0xCC, 0, 6);
  else if (S.fireWarn >= 16) P.request(0xA2, 0, 6);
}

// ---- Transportation (3.10) --------------------------------------------------------------------
void AdvisorManager::transRules(TopicList& L, const AdvisorInputs& in, GameRng& rng) {
  const auto& T = in.trans;
  const int64_t RFH = thr.get("ROAD_FUNDING_HIGH"), RFM = thr.get("ROAD_FUNDING_MEDIUM"), RFL = thr.get("ROAD_FUNDING_LOW");
  const int64_t TFH = thr.get("RAIL_FUNDING_HIGH"), TFM = thr.get("RAIL_FUNDING_MEDIUM");
  // RD = f32(ROAD_DAMAGE x f32 0.01) (spec 22 s3.10.1) [STATIC]. A NaN damage (no road pieces: 0/0 in the original)
  // makes every damage test true.
  const float RD = float(double(thr.get("ROAD_DAMAGE")) * double(0.01f));
  const float dmg = float(T.roadDamage);
  const bool nan = std::isnan(dmg);
  const int64_t THR = thr.get("HIGH_ROAD_TRAFFIC"), TMR = thr.get("MED_ROAD_TRAFFIC");
  const int64_t TMU = thr.get("MED_RAIL_USAGE");
  const int Tr = T.roadTraffic, Ru = T.railUsage;
  if (T.nRoad + T.nTransit == 0) L.raise(0x53);
  if (T.nRoad == 0) L.raise(0x123, 0xC2A499BA);
  else if (T.roadFunding > RFH && (nan || dmg < 0.01f)) L.raise(0x59, 0xC2A499BA);
  else if (T.roadFunding > RFM && (nan || dmg < 0.01f)) L.raise(0x5A, 0xC2A499BA);
  else if (T.roadFunding > RFL && (nan || dmg <= RD)) L.raise(0x5B, 0xC2A499BA);
  else L.raise(0x5C, 0xC2A499BA);
  if (T.nTransit == 0) L.raise(0x124, 0xE2A499BE);
  else if (T.transitStrike) L.raise(0x61, 0xE2A499BE);
  else if (T.transitWarn > 10) L.raise(0x60, 0xE2A499BE);
  else if (T.transitFunding < TFM) L.raise(0x60, 0xE2A499BE);
  else if (T.transitFunding <= TFH) L.raise(0x5F, 0xE2A499BE);
  else L.raise(0x5E, 0xE2A499BE);
  if (T.nTransit > 2) {
    if (Ru > Tr && Ru > TMU) L.raise(0x11B, 0xA3372CAE);
    else if (3 * Ru < Tr) L.raise(0x11C, 0xA3372CAE);
  }
  if (T.nRoad > 0) {
    if (Tr > THR) L.raise(0x4F, 0x43372E49);
    else if (Tr > TMR) L.raise((T.nTransit == 0 && T.nStations == 0 && T.nBus <= 0) ? 0x50 : 0x51, 0x43372E49);
    else if (T.failedCommutes <= double(0.2f)) L.raise(0x52, 0x43372E49);
  }
  if (T.failedCommutes > double(0.2f)) L.raise(0x56);
  if (T.airportZoneTiles > 0 && T.airportFailTiles > 0 && T.airportBuildings == 0) L.raise(0x62);
  // advancement announcements
  static const char* names[4] = {"SUBWAY_AVAILABLE", "BUS_STATION_AVAILABLE", "AIRPORT_AVAILABLE", "HIGHWAY_AVAILABLE"};
  static const uint32_t tops[4] = {0x63, 0x64, 0x87, 0x110};
  const bool built[4] = {T.hasSubway, T.hasBusStations, T.hasAirport, T.hasHighway};
  int r = int(rng.rand(100));
  if (r < 10) {
    for (int i = 0; i < 4; ++i)
      if (!tAnnounced_[i] && thr.get(names[i]) <= in.year) {
        L.raise(tops[i], 0, 120, true, true, true);
        tAnnounced_[i] = true;
        break;
      }
  } else {
    for (int i = 0; i < 4; ++i) if (built[i] && L.listed(tops[i])) L.remove(tops[i]);
  }
}

void AdvisorManager::transPetitions(const AdvisorInputs& in, GameRng& rng) {
  if (!petitioners || in.population < 100) return;
  const auto& T = in.trans;
  auto& P = *petitioners;
  const int Y = in.year, Tr = T.roadTraffic, Ru = T.railUsage;
  const int64_t THR = thr.get("HIGH_ROAD_TRAFFIC"), THU = thr.get("HIGH_RAIL_USAGE");
  if (T.transitStrike) P.request(0xA4, 0xA3829374, 6);
  else if (T.transitWarn > 15) P.request(0xA3, 0xA3829374, 6);
  if (Tr > thr.get("CARPOOL_TRIGGER_LEVEL") && Y >= thr.get("CARPOOL_TRIGGER_YEAR") && !T.carpoolEnacted &&
      !P.pending(0x72) && rng.rand(100) == 0) P.request(0x72, 0, 120);
  if (Tr > thr.get("CROSSING_GUARD_TRIGGER_LEVEL") && Y > thr.get("CROSSING_GUARD_TRIGGER_YEAR") &&
      !T.crossingGuardsEnacted && !P.pending(0x74) && rng.rand(100) == 0) P.request(0x74, 0, 120);
  if (Tr > thr.get("ALTERNATE_DAY_DRIVING_LEVEL") && Y > thr.get("ALTERNATE_DAY_DRIVING_YEAR") && T.altDayAvailable &&
      !P.pending(0xD8) && rng.rand(100) == 0) P.request(0xD8, 0, 120);
  if (Tr > thr.get("BUS_STOP_TRIGGER_LEVEL") && T.nBus < 2) P.request(0x0F, 0, 24);
  if (Tr > THR && T.nTransit < 2) P.request(T.nBus < 2 ? 0x15 : 0x1C, 0, 24);
  if (Ru > THU && T.railTiles > 2 && T.n12230 + T.n12361 > 2) P.request(0x0D, 0, 24);
  if (Ru > THU && T.subwayTiles > 2 && T.n12360 + T.n12361 > 2 && !P.pending(0x16) && !P.pending(0x1A)) {
    if (rng.rand(2) == 0) { P.request(0x16, 0, 24); P.request(0x1A, 0, 24); }
    else P.request(0x1A, 0, 24);
  }
}

// ---- City planner (3.11) ----------------------------------------------------------------------
int64_t AdvisorManager::accumulated(const AdvisorInputs& in, int fam) const {
  int64_t s = in.plan.demand[fam];
  int rw = int(thr.get("RESPONSIVENESS_IN_MONTHS"));
  const auto& r = ring_[fam];
  for (int i = 0; i < rw && i < int(r.size()); ++i) s += r[r.size() - 1 - size_t(i)];
  return s;
}

void AdvisorManager::planRules(TopicList& L, const AdvisorInputs& in) {
  const auto& P = in.plan;
  const int64_t N = in.population;
  const int64_t SS = thr.get("SMALL_SIZE_CITY"), MS = thr.get("MEDIUM_SIZE_CITY"), NS = thr.get("NON_DEV_SENSITIVITY");
  const int64_t CC = thr.get("COMMERCIAL_CAP_PROBLEM"), IC = thr.get("INDUSTRIAL_CAP_PROBLEM");
  const int64_t dR = P.demand[0], dC = P.demand[1], dI = P.demand[2];
  const int64_t aR = accumulated(in, 0), aC = accumulated(in, 1), aI = accumulated(in, 2);
  const int64_t zR = P.zoned[0], zC = P.zoned[1], zI = P.zoned[2];
  auto x = [&](int f, int c) { return int64_t(P.x[f][c]); };
  // Cap rows
  if (P.capC > CC) {
    if (P.airportCount == 0) { if (P.capFlagA) L.raise(0x83, 0xE2A4935D); }
    else if (P.Kr == P.K && P.capFlagB) L.raise(0x2B, 0xE2A4935D);
    else if (P.capFlagB) L.raise(0x84, 0xE2A4935D);
  }
  if (P.capI > IC) {
    if (P.K >= 1 && P.K <= 3 && P.Kt == 0 && P.seaportCount == 0) L.raise(0x82, 0xA2A4933D);
    else if (P.Kt < P.K) L.raise(0xE5, 0xA2A4933D);
    else if (P.K != 4 && P.seaportCount >= 1) L.raise(0x86, 0xA2A4933D);
  }
  if (N < SS) {
    if (zR > NS && dR < 0 && aR < 0 && (zC < NS || zI < NS)) L.raise(0xBF);
    if (((zC > NS && aC < 0) || (zI > NS && aI < 0)) && dR > 0 && zR < NS) L.raise(0xD7);
    if (zR < NS && dR > 0 && (x(1, 3) > NS || x(2, 3) > NS)) L.raise(0xC9);
    if (x(0, 3) > NS && dR < 0 && (zC < NS || zI < NS)) L.raise(0xE4);
    if (P.airPollution < thr.get("LOW_AIR_POLLUTION_FARMS") && P.waterPollution < thr.get("LOW_WATER_POLLUTION_FARMS") &&
        P.farms < NS) L.raise(0xDE);
  }
  if (N >= SS && N < MS) {
    const int64_t z[3] = {zR, zC, zI}, d[3] = {dR, dC, dI};
    bool any = false;
    for (int f = 0; f < 3; ++f)
      if (z[f] > NS && d[f] > 0 && x(f, 14) + x(f, 15) + x(f, 16) > NS) any = true;
    if (any) L.raise(0xD8);
  }
  bool a4 = false, a5 = false, a6 = false;
  for (int f = 0; f < 3; ++f) { a4 |= x(f, 4) > NS; a5 |= x(f, 5) > NS; a6 |= x(f, 6) > NS; }
  if (a4) L.event(0xD1, 4);
  if (a5) L.event(0xD2, 4);
  if (a6) L.event(0xD3, 4);
  if (x(0, 1) + x(1, 1) + x(2, 1) > 0) L.event(0xD9, 6);
  if (x(0, 12) + x(2, 12) > 0) L.event(0xDA, 6);
  if (P.cleanIndustry > 0) L.persistentTopic(0xC8, 8);
  if (P.medComm > 0) L.persistentTopic(0xDC, 8);
  if (P.highComm > 0) L.persistentTopic(0xDD, 8);
  if (P.landValue < thr.get("LOW_LAND_VALUE")) L.raise(0xDB);
  if (P.zSea > 5 && P.seaportBadTiles > 5) L.raise(0xE2);
  if (P.zFill != 0 && P.landfillNoAccessTiles != 0) L.raise(0xE0);
  else if (P.landfillFull && P.landfillNoFreeCapacity) L.raise(0xE1);
}

void AdvisorManager::planPetitions(const AdvisorInputs& in, GameRng& rng) {
  if (!petitioners) return;
  auto& Pm = *petitioners;
  const auto& P = in.plan;
  const int64_t N = in.population;
  const int64_t NS = thr.get("NON_DEV_SENSITIVITY"), CC = thr.get("COMMERCIAL_CAP_PROBLEM");
  const int64_t CP = thr.get("COMMERCIAL_DEMAND_POSITIVE"), RP = thr.get("RESIDENTIAL_DEMAND_POSITIVE");
  const TopicList& L = list(kAdvPlanner);
  const int64_t aR = accumulated(in, 0), aC = accumulated(in, 1);
  auto draw = [&](int n, int lim) { return int(rng.rand(uint32_t(n))) < lim; };
  if ((L.listed(0x2B) || L.listed(0x84)) && P.capC < 40) Pm.request(0x14, 0, 48);
  if (P.airportCount == 0 && P.capC > CC) Pm.request(0x1B, 0, 48);
  if (P.farms > 0 && P.farmersMarketAvailable && !Pm.pending(0xDD) && draw(100, 2)) Pm.request(0xDD, 0, 120);
  if (P.zoned[1] > NS && P.demand[1] >= CP && aC >= CP && N > 500 && !Pm.pending(0x29) && !Pm.pending(0x2A)) {
    if (rng.rand(2) == 0) { Pm.request(0x29, 0, 12); Pm.request(0x2A, 0, 12); }
    else Pm.request(0x2A, 0, 12);
  }
  if (P.zoned[0] > NS && P.demand[0] >= RP && aR >= RP && !Pm.pending(0x2F)) Pm.request(0x2F, 0, 12);
  if (P.eq > thr.get("MIN_HI_TECH_EQ") &&
      thr.get("HI_TECH_JOB_RATIO") * P.industrialBuildings < 100 * int64_t(P.cleanIndustry) && !Pm.pending(0x27) &&
      !Pm.pending(0x28) && draw(100, 3)) {
    if (rng.rand(2) == 0) { Pm.request(0x27, 0, 30); Pm.request(0x28, 0, 30); }
    else Pm.request(0x28, 0, 30);
  }
  int64_t Pk = int64_t(P.n14311) + 9 * P.n14312 + 16 * P.n14313 + 4 * P.n14314 + 4 * P.n14315 + P.n14316;
  int64_t B = P.totalBuildings, F = P.farms;
  if (thr.get("PARK_RATIO") * Pk < B - F - Pk && in.aura < thr.get("PARK_AURA_CUTOFF") && !Pm.pending(0x49) && draw(100, 2))
    Pm.request(0x49, 0, 120);
  uint32_t half = uint32_t(int64_t(in.mapWidth) * in.mapHeight / 2);
  if (half < uint32_t(B - 2 * Pk - F) && !Pm.pending(0x4A) && draw(100, 2)) Pm.request(0x4A, 0, 120);
}

// ---- Health & education (3.12) ----------------------------------------------------------------
void AdvisorManager::demoRules(TopicList& L, const AdvisorInputs& in, GameRng& rng) {
  const auto& D = in.demo;
  const int64_t N = in.population;
  const int64_t HP = thr.get("HOSPITAL_POPULATION_MINIMUM"), SP = thr.get("SCHOOL_POPULATION_MINIMUM");
  const int64_t LP = thr.get("LIBRARY_POPULATION_MINIMUM"), MP = thr.get("MUSEUM_POPULATION_MINIMUM");
  const int64_t QP = thr.get("LE_AND_EQ_POPULATION_MINIMUM"), STP = thr.get("STADIUM_GROWTH_CUTOFF");
  const int64_t RGC = thr.get("RESIDENTIAL_GROWTH_CAP");
  const int64_t LL = thr.get("LOW_LIFE_EXPECTANCY"), LH = thr.get("HIGH_LIFE_EXPECTANCY");
  const int64_t EL = thr.get("LOW_EQ_CUTOFF"), EA = thr.get("AVERAGE_EQ_CUTOFF"), EH = thr.get("HIGH_EQ_CUTOFF");
  const int64_t DH = thr.get("HIGH_DREARY_RATING");
  const int dr = in.dreary;
  const bool nH = D.nHospital > 0, nS = D.nSchool > 0, nC = D.nCollege > 0;
  // Health
  const uint32_t kH = 0xE2A49484;
  if (D.healthStrike) L.raise(0x65, kH);
  else if (nH && D.warnHealth > 15) L.raise(0x66, kH);
  else if (nH && D.demandHealth > 3 * D.capHealth / 2 && D.healthFunding > 99) L.raise(0x131, kH);
  else if (nH) L.raise(D.fundClassHealth == 4 ? 0x69 : D.fundClassHealth == 3 ? 0x68 : 0x67, kH);
  else L.raise(N > HP ? 0xBD : 0x121, kH);
  // Education
  const uint32_t kE = 0xA2A4947C;
  if (D.schoolStrike) L.raise(0x6A, kE);
  else if (D.collegeStrike) L.raise(0x104, kE);
  else if (nS && D.warnSchool > 15) L.raise(0x6B, kE);
  else if (nC && D.warnCollege > 15) L.raise(0x13A, kE);
  else if (nS && D.demandSchool > 3 * D.capSchool / 2 && D.eduFunding > 95) L.raise(0x135, kE);
  else if (nS && D.capSchool > D.demandSchool && D.eduFunding < 85) L.raise(0x134, kE);
  else if (nC && D.demandCollege > 3 * D.capCollege / 2 && D.eduFunding > 95) L.raise(0x139, kE);
  else if (nC && D.capCollege > D.demandCollege && D.eduFunding < 85) L.raise(0x138, kE);
  else if (nS && D.fundClassSchool == 3) L.raise(0x6C, kE);
  else if (nC && D.fundClassCollege == 3) L.raise(0x137, kE);
  else if (nS && D.fundClassSchool == 4) L.raise(0x6D, kE);
  else if (nC && D.fundClassCollege == 4) L.raise(0x136, kE);
  else if (nS) L.raise(0x6B, kE);
  else if (N <= SP && !nC) L.raise(0x126, kE);
  else L.raise(0x6E, kE);
  // Facilities
  if (N > SP && D.demandSchool > D.capSchool && D.fundClassSchool != 4) L.raise(0xAD);
  if (N > LP && N > D.libraryCap) L.raise(0xAB);
  if (N > MP && N > D.museumCap) L.raise(0xAC);
  if (N > HP && D.demandHealth > D.capHealth && nH) L.raise(0xB8);
  // LE / EQ
  if (N > QP) {
    const uint32_t kLE = 0x6307D0FB, kEQ = 0x2307D105;
    if (D.lifeExpectancy < LL && D.fundClassHealth != 3 && D.fundClassHealth != 4) L.raise(0xAE, kLE);
    else if (D.lifeExpectancy > LH) L.raise(0xB1, kLE);
    if (D.eq < EL && D.fundClassSchool != 3 && D.fundClassSchool != 4) {
      if (!(L.listed(0xB3) || L.listed(0xB4))) L.raise(rng.rand(2) == 0 ? 0xB3 : 0xB4, kEQ);
    } else if (D.eq < EA) L.raise(0xB5, kEQ);
    else if (D.eq < EH) L.raise(0xB6, kEQ);
    else L.raise(0xB7, kEQ);
  }
  uint32_t rk = N > QP ? 0x2307D105 : 0;
  if (N > STP && D.nStadium == 0) L.raise(0x27, rk);
  else if (N > RGC && dr > DH) L.raise(0x26, rk);
}

void AdvisorManager::demoPetitions(const AdvisorInputs& in, GameRng& rng) {
  if (!petitioners) return;
  auto& P = *petitioners;
  const auto& D = in.demo;
  const int64_t N = in.population;
  auto draw = [&](int lim) { return int(rng.rand(100)) < lim; };
  const bool nH = D.nHospital > 0, nS = D.nSchool > 0;
  if (D.healthStrike) P.request(0x9C, 0xA38293DE, 6); else if (D.warnHealth > 15) P.request(0x9B, 0xA38293DE, 6);
  if (D.schoolStrike) P.request(0x9E, 0xA38293EF, 6); else if (D.warnSchool > 15) P.request(0x9D, 0xA38293EF, 6);
  if (N > 200) {
    bool skipAll = false, skip02 = false;
    if (D.nHospital >= 1 && D.fundClassHealth == 4 && !P.pending(0x05)) {
      if (draw(2)) P.request(0x05, 0xC3829422, 40); else skipAll = true;
    }
    if (!skipAll && !nH && N > thr.get("HOSPITAL_RATIO") && !P.pending(0x0C)) {
      if (draw(10)) P.request(0x0C, 0, 12); else skip02 = true;
    }
    if (!skipAll && !skip02 && D.nHospital > 1 && D.capHealth < D.demandHealth && D.fundClassHealth < 3 &&
        !P.pending(0x02) && draw(5)) P.request(0x02, 0, 12);
  }
  if (N > thr.get("SCHOOL_POPULATION_MINIMUM")) {
    const bool noHi = D.fundClassSchool != 3 && D.fundClassSchool != 4;
    if (!nS && !P.pending(0x1D) && draw(10)) P.request(0x1D, 0, 12);
    if (D.capSchool < D.demandSchool && noHi && !P.pending(0x19) && draw(10)) P.request(0x19, 0, 12);
    if (D.demandSchool < D.capSchool && 7 * D.capSchool < 10 * D.demandSchool && !noHi && !P.pending(0x06) && draw(10))
      P.request(0x06, 0, 120);
  }
  if (N > 500) {
    const int64_t CAL = thr.get("CITY_AURA_LOW"), HDR = thr.get("HIGH_DREARY_RATING");
    const int a = in.aura, dr = in.dreary;
    if (a < CAL && dr > HDR) P.request(0x3C, 0, 18);
    else if ((a < CAL && dr <= HDR) || (a >= CAL && dr >= 71 && N > thr.get("CITY_AURA_DREARY_POPULATION")))
      P.request(0x39, 0, 24);
    else if (a > thr.get("CITY_AURA_HIGH") && dr < thr.get("LOW_DREARY_RATING") && !P.pending(0x3B) && draw(4))
      P.request(0x3B, 0, 24, true);
  }
  if (D.nZoo == 0 && N > thr.get("ZOO_POPULATION_MINIMUM") && !P.pending(0x9A) && draw(2)) P.request(0x9A, 0, 60);
  if (D.nMarina == 0 && N > thr.get("MARINA_POPULATION_MINIMUM") && !P.pending(0x99) && draw(2)) P.request(0x99, 0, 60);
}

// ---- Utilities (3.13) -------------------------------------------------------------------------
void AdvisorManager::utilRules(TopicList& L, const AdvisorInputs& in) {
  const auto& U = in.util;
  const int64_t PCC = thr.get("POWER_CONSUMPTION_CRITICAL"), PCH = thr.get("POWER_CONSUMPTION_HIGH"),
                PCM = thr.get("POWER_CONSUMPTION_MEDIUM"), WCM = thr.get("WATER_CONSUMPTION_MEDIUM");
  const int64_t PU = U.powerUsed, PC = U.powerCap, PD = U.powerDemand, WU = U.waterUsed, WC = U.waterCap;
  if (!U.dealSellPower && PC > 2 * PU && U.neighbours > 0 && !U.powerLineConnection) L.raise(0x05);
  if (!U.dealBuyPower && PU > 95 * PC / 100 && U.neighbours > 0 && !U.powerLineConnection) L.raise(0x06);
  if (!U.dealSellWater && WC > 2 * WU && U.neighbours > 0 && !U.pipeConnection) L.raise(0x07);
  if (!U.dealBuyWater && WU > 95 * WC / 100 && U.neighbours > 0 && !U.pipeConnection) L.raise(0x08);
  const uint32_t kP = 0x62A49A83;
  if (!U.dealBuyPower && U.nPlants == 0) L.raise(0x10E, kP);
  else if (PD <= 0) {}
  else if (U.dealBuyPower) L.raise(0x11, kP);
  else if (PU > PCC * PC / 100) { if (!(L.listed(0x0F) || L.listed(0x09))) L.raise(utilRng.rand(2) == 0 ? 0x0F : 0x09, kP); }
  else if (PU > PCH * PC / 100) { if (!(L.listed(0x10) || L.listed(0x0A))) L.raise(utilRng.rand(2) == 0 ? 0x10 : 0x0A, kP); }
  else if (PU > PCM * PC / 100) L.raise(0x11, kP);
  else if (!U.dealSellPower) L.raise(0x12, kP);
  if (U.nPlants > 0 && !U.dealBuyPower && PD > PC) L.raise(0x0E, 0x038A5570);
  if (PC > PD || U.dealBuyPower) {
    if (U.unpoweredBuildings > U.buildingsNeedingPower / 2) L.raise(0x13, 0x23440D97);
    else if (U.unpoweredBuildings > 10) L.raise(0x14, 0x23440D97);
    else if (L.listed(0x13) || L.listed(0x14)) L.raise(0x15, 0x23440D97, 6, true, true);
  }
  const uint32_t kW = 0x22A49AFA;
  if (WC == 0) { if (U.buildingsNeedingWater > 30) L.raise(0x16, kW); }
  else if (WU == WC && !U.dealBuyWater) L.raise((U.neighbours < 4 && U.desalinationAvailable) ? 0x18 : 0x17, kW);
  else if (WU > WC * WCM / 100) L.raise(0x19, kW);
  else L.raise(0x1A, kW);
  if (U.dryBuildings > 20 && (WC > WU || U.dealBuyWater))
    L.raise((2 * U.dryBuildings > U.buildingsNeedingWater && U.buildingsNeedingWater > 50) ? 0x1B : 0x1C, kW);
  else if (L.listed(0x1B) || L.listed(0x1C)) L.raise(0x1D, kW, 6, true, true);
  if (U.nOldPlants > U.nPlants / 4) L.raise(0x100);
  if (U.nNuclearEnding > 0) L.raise(0xFF);
  if (U.nOldPumps > U.nPumps / 4) L.raise(0x101);
  if (U.nOldIncin > U.nIncin / 2) L.raise(0x102);
  for (uint32_t t : unlockTopics_) L.raise(t, 0, 120, true, true, true);
  unlockTopics_.clear();
}

void AdvisorManager::initBuildingUnlocks(int year) {
  unlocked_.clear(); unlockTopics_.clear(); unlockTopicsEnv_.clear();
  for (auto* lst : {&thr.power, &thr.water, &thr.garbage})
    for (auto& a : *lst) if (a.year <= year) unlocked_.insert(a.buildingId);
}
void AdvisorManager::monthlyBuildingUnlock(int year, GameRng& rng) {
  if (rng.rand(100) >= 10) return;
  for (auto* lst : {&thr.power, &thr.water, &thr.garbage})
    for (auto& a : *lst)
      if (a.year <= year && !unlocked_.count(a.buildingId)) {
        unlocked_.insert(a.buildingId);
        if (a.topic) (lst == &thr.garbage ? unlockTopicsEnv_ : unlockTopics_).push_back(a.topic);
        return;
      }
}
bool AdvisorManager::garbageBuildingAvailable(int agentType) const {
  for (auto& a : thr.garbage) if (a.buildingId == agentType) return unlocked_.count(agentType) != 0;
  return false;
}
bool AdvisorManager::buildingUnlocked(int agentType) const {
  for (auto* lst : {&thr.power, &thr.water, &thr.garbage})
    for (auto& a : *lst) if (a.buildingId == agentType) return unlocked_.count(agentType) != 0;
  return true;
}

void AdvisorManager::utilDeals(TopicList&, const AdvisorInputs& in) {
  if (!petitioners || !deals) return;
  auto& P = *petitioners;
  const auto& U = in.util;
  const int64_t N = in.population, MDP = thr.get("MINIMUM_DEAL_POPULATION"), LTL = thr.get("LOW_TREASURY_LEVEL");
  const int64_t PU = U.powerUsed, PC = U.powerCap, PD = U.powerDemand, WU = U.waterUsed, WC = U.waterCap;
  const int64_t GC = U.garbageCap, GP = U.garbageProduced;
  struct Cat { int type; uint32_t key; uint32_t pet[3]; };
  static const Cat cats[6] = {
      {1, 0x63248EA7, {0xB3, 0xB4, 0xB5}}, {0, 0x83248FD7, {0xAC, 0xAD, 0xCA}}, {3, 0xC3249313, {0xB9, 0xBA, 0xBB}},
      {2, 0x232493D6, {0xA5, 0xA6, 0xA7}}, {5, 0x232494CF, {0xBF, 0xC0, 0xC1}}, {4, 0x63249696, {0xC4, 0xC5, 0xF7}}};
  for (auto& c : cats) {
    bool cond = deals->eligibleNeighbour(c.type);
    switch (c.type) {
      case 1: cond = cond && PD < 4 * PC / 10 && PC - PD > 1000 && N > MDP; break;
      case 0: cond = cond && (PU >= 8 * PC / 10 || U.unpoweredBuildings >= 31); break;
      case 3: cond = cond && WU < 4 * WC / 10 && WC - WU > 1000 && N > MDP; break;
      case 2: cond = cond && (WU >= 8 * WC / 10 || U.dryBuildings > 30); break;
      case 5: cond = cond && GP < 6 * GC / 10 && GC - GP > 500 && N > MDP; break;
      case 4: cond = cond && GP >= 9 * GC / 10; break;
    }
    auto withdrawAll = [&] { for (uint32_t p : c.pet) P.withdraw(p); };
    if (deals->dealActive(c.type)) {
      withdrawAll();
      int age = deals->dealAgeMonths(c.type);
      if (c.type == 0 && age > 24) P.request(0xB1, c.key, 1);
      else if (c.type == 2 && age > 24) P.request(0xAA, c.key, 1);
      else if (c.type == 4 && age > 24) P.request(0xC3, c.key, 1);
    } else if (!cond || deals->offersDisabled()) {
      deals->clearOffer(c.type);
      withdrawAll();
    } else {
      uint32_t pendingId = 0;
      for (uint32_t p : c.pet) if (P.pending(p)) pendingId = p;
      if (pendingId) {
        if (deals->offerStillValid(c.type)) P.request(pendingId, c.key, 1);
        else { deals->clearOffer(c.type); withdrawAll(); }
      } else {
        uint32_t v = utilRng.rand(3);
        if (deals->generateOffer(c.type)) P.request(c.pet[v], c.key, 1);
      }
    }
    // Sell power only: low treasury and the visible offer is not 0xB5 -> budget advisor topic 0x25
    if (c.type == 1 && U.treasury < LTL && (P.visible(0xB3) || P.visible(0xB4))) list(kAdvBudget).raise(0x25);
  }
}

// ---- Budget petitions --------------------------------------------------------------------------
void AdvisorManager::budgetPetitions(const AdvisorInputs& in, GameRng& rng) {
  if (!petitioners) return;
  for (auto& o : in.budget.dealOffers) petitioners->request(o.petition, 0, o.limit);
  for (uint32_t id : in.budget.businessFollowUps)
    if (rng.rand(100) < 2) petitioners->request(id, 0, 12);
}

void AdvisorManager::postUpdate(uint32_t id, TopicList& L, const AdvisorInputs& in, GameRng& rng) {
  switch (id) {
    case kAdvBudget: budgetPetitions(in, rng); break;
    case kAdvSafety: safetyPetitions(in, rng); break;
    case kAdvTransport: transPetitions(in, rng); break;
    case kAdvPlanner:
      planPetitions(in, rng);
      if (petitioners) rewards.evaluate(in, *petitioners);
      for (int f = 0; f < 3; ++f) {
        ring_[f].push_back(in.plan.demand[f]);
        if (ring_[f].size() > 12) ring_[f].erase(ring_[f].begin());
      }
      break;
    case kAdvDemographics: demoPetitions(in, rng); break;
    case kAdvUtilities: utilDeals(L, in); break;
    default: break;
  }
}

// ---------------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------------
std::vector<AdvisorManager::Record> AdvisorManager::saveRecords() const {
  std::vector<Record> v;
  for (auto& l : lists_) v.push_back({kTopicRecType, l.advisorId(), kTopicRecInstance, l.save()});
  v.push_back({kRewardRecType, kRewardRecGroup, 0, rewards.save()});
  Bytes b; put16(b, 1); put16(b, 2); put32(b, 0xDEADBEEFu);
  for (int i = 0; i < 6; ++i) b.push_back(mgrFlags[i]);
  v.push_back({kAdvMgrRecType, kAdvMgrRecGroup, 0, b});
  return v;
}
bool AdvisorManager::loadRecord(uint32_t type, uint32_t group, uint32_t instance, const Bytes& data) {
  if (type == kTopicRecType && instance == kTopicRecInstance) {
    for (auto& l : lists_) if (l.advisorId() == group) return l.load(data);
    return false;
  }
  if (type == kRewardRecType && group == kRewardRecGroup && instance == 0) return rewards.load(data);
  if (type == kAdvMgrRecType && group == kAdvMgrRecGroup && instance == 0) {
    if (data.size() != 14 || rd16(data.data()) != 1 || rd32(data.data() + 4) != 0xDEADBEEFu) return false;
    for (int i = 0; i < 6; ++i) mgrFlags[i] = data[size_t(8 + i)];
    return true;
  }
  return false;
}

}  // namespace m3k
