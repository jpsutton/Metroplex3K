#include "core/petitioners.h"
#include <algorithm>
#include <cstdlib>

namespace m3k {

namespace {
int parseI(const std::string& s) { return int(std::strtol(s.c_str(), nullptr, 0)); }
}

// ---------------------------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------------------------
void PetitionerConfig::loadManagerIni(const IniFile& f) {
  for (auto& kv : f.section("PETITIONER_ORDINANCE_TRIGGER_MAPPINGS")) thr_[kv.first] = parseI(kv.second);
  std::string v;
  if (f.get("PETITIONER_ADVICE_SIZE", "NumberOfPetitionerMessages", v)) adviceSize = parseI(v);
  for (auto& kv : f.section("PETITIONER_ADVICE_MAPPING")) {
    uint32_t n = uint32_t(parseI(kv.first));
    advice_[n] = parseIdNumber(kv.second);            // "0x628D0C45;" -> stops at ';'
  }
  static const char* cats[kPetitionTextCount] = {"ADVICE", "BUTTON1", "BUTTON2", "HEADING", "MESSAGE", "NAME",
                                                 "REACTION1", "REACTION2", "TICKER", "TICKER_X", "TICKER_Y"};
  for (int i = 0; i < kPetitionTextCount; ++i) {
    std::vector<uint32_t> t;
    for (auto& kv : f.section(std::string("PETITIONER_STRINGS_") + cats[i])) t.push_back(parseIdNumber(kv.second));
    if (!t.empty()) tables_[i] = t;
  }
}
std::vector<std::string> PetitionerConfig::extraInfoFiles(const IniFile& f) {
  std::vector<std::string> v;
  for (auto& kv : f.section("PetitionerExtraInfo Filenames"))
    if (iequals(kv.first, "entry")) v.push_back(kv.second);
  return v;
}
void PetitionerConfig::addExtraInfo(const IniFile& f) {
  for (auto& kv : f.section("PetitionerExtraInfo")) {
    uint32_t id = parseIdNumber(kv.first);
    if (extra_.count(id)) continue;
    ExtraInfo e; e.found = true;
    size_t c = kv.second.find(',');
    e.portrait = uint32_t(parseI(kv.second.substr(0, c)));
    if (c != std::string::npos) e.second = uint32_t(parseI(kv.second.substr(c + 1)));
    extra_[id] = e;
  }
}
int PetitionerConfig::threshold(const std::string& key) const {
  auto it = thr_.find(key);
  return it == thr_.end() ? 0 : it->second;
}
uint32_t PetitionerConfig::adviceAdvisor(uint32_t p) const {
  auto it = advice_.find(p);
  return it == advice_.end() ? 0 : it->second;
}
uint32_t PetitionerConfig::tableFor(PetitionText cat, uint32_t id, const std::function<bool(uint32_t, uint32_t)>& has) const {
  for (uint32_t t : tables_[int(cat)]) if (has(t, id)) return t;
  return 0;
}
PetitionerConfig::ExtraInfo PetitionerConfig::extraInfo(uint32_t p) const {
  auto it = extra_.find(p);
  return it == extra_.end() ? ExtraInfo() : it->second;
}

// ---------------------------------------------------------------------------------------------
// Accept action tables
// ---------------------------------------------------------------------------------------------
bool petitionOrdinanceAction(uint32_t p, uint32_t& ord, int& val) {
  struct Row { uint32_t pet, ord; int val; };
  static const Row rows[] = {
      {0x69, 0xA0D07129, 1}, {0x68, 0xA0D07129, 0}, {0x6A, 0xE0D07233, 1}, {0x63, 0x62BF1DAA, 1},
      {0x64, 0x62BF1DB9, 1}, {0x61, 0x00D0723D, 1}, {0x62, 0x00D0723D, 0}, {0x6B, 0xE0D07239, 1},
      {0x6D, 0xE0D07239, 0}, {0x6E, 0x40D07236, 1}, {0x71, 0x20D07175, 1}, {0x73, 0xA2BF1DE5, 1},
      {0x76, 0x82B9999B, 1}, {0x75, 0x82B9999B, 0}, {0x7F, 0xE0D0722E, 1}, {0x80, 0x015B4D1E, 1},
      {0x81, 0x015B4D1E, 0}, {0xD6, 0xA2F6E7F9, 1}, {0xDC, 0xC2F90EB1, 1}, {0xDE, 0xC2F6E81F, 1},
      {0xDF, 0x62F6E7CF, 1}, {0xE0, 0xA2F6E7DA, 1}, {0xE1, 0x22F6E81B, 1}, {0xE2, 0xC2F6E804, 1},
      {0xE4, 0x22F6E814, 1}, {0xE5, 0xA2F917CF, 1}, {0xF1, 0x22F6E7F4, 1}, {0xF3, 0xE2F6E7EA, 1},
      {0xF4, 0xC2BF1DD1, 1}, {0xF5, 0xE2F6E7FE, 1}, {0xF6, 0x82E42E30, 1},
      // advisor-requested
      {0x66, 0xC2BF1DC5, 1}, {0x67, 0xC2BF1DC5, 0}, {0x6F, 0x815B4CEF, 1}, {0x70, 0x815B4CEF, 0},
      {0x72, 0xA2BF1DDC, 1}, {0x74, 0xC2BF1DEF, 1}, {0x77, 0x02BF1DFA, 1}, {0x78, 0x02BF1DFA, 0},
      {0x7B, 0xA2BF1E43, 1}, {0x7C, 0xA2BF1E43, 0}, {0x85, 0x42BF1E18, 1}, {0x86, 0xC2BF1E04, 1},
      {0xD4, 0x62F6E808, 1}, {0xD8, 0xE2BF1E2A, 1}, {0xDD, 0x62F6E82B, 1}, {0xF2, 0x22F6E80C, 1},
  };
  for (auto& r : rows) if (r.pet == p) { ord = r.ord; val = r.val; return true; }
  return false;
}

int petitionRewardBuilding(uint32_t p) {
  switch (p) {
    case 0x84: return 13874; case 0x88: return 14003; case 0x8B: return 14550; case 0x8C: return 14370;
    case 0x8D: return 14325; case 0x8E: return 13872; case 0x8F: return 11001; case 0x94: return 14600;
    case 0x96: return 12350; case 0xCD: return 11002; case 0x12C: return 20001; case 0x12F: return 20002;
  }
  return 0;
}

// ---------------------------------------------------------------------------------------------
// Manager
// ---------------------------------------------------------------------------------------------
const PendingPetition* PetitionerManager::find(uint32_t id) const {
  for (auto& p : list_) if (p.id == id) return &p;
  return nullptr;
}
PendingPetition* PetitionerManager::findMut(uint32_t id) {
  for (auto& p : list_) if (p.id == id) return &p;
  return nullptr;
}
std::vector<const PendingPetition*> PetitionerManager::presentable() const {
  std::vector<const PendingPetition*> v;
  for (auto& p : list_) if (!p.hidden()) v.push_back(&p);
  return v;
}

void PetitionerManager::postTicker(PendingPetition& p, bool hasLoc, int x, int z) {
  if (!sink_) return;
  uint32_t t = config.tableFor(PetitionText::Ticker, p.id, [&](uint32_t tb, uint32_t i) { return sink_->hasString(tb, i); });
  if (t) p.tickerItem = sink_->post(t, p.id, hasLoc, x, z);
}
void PetitionerManager::dropTicker(PendingPetition& p) {
  if (sink_ && p.tickerItem) sink_->withdraw(p.tickerItem);
  p.tickerItem = 0;
}

void PetitionerManager::request(uint32_t id, uint32_t argument, int limit, bool persistent, bool expiring,
                                bool hasLoc, int x, int z) {
  if (PendingPetition* e = findMut(id)) { e->flags |= kFlagRaised; return; }
  PendingPetition p;
  p.id = id; p.argument = argument; p.limit = uint8_t(std::max(0, std::min(255, limit)));
  p.flags = kFlagRaised | (persistent ? kFlagPersistent : 0) | (expiring ? kFlagExpiring : 0);
  if (hasLoc) {
    TopicArg a; a.index = 0; a.name = "%X%"; a.value = uint32_t(x);
    TopicArg b; b.index = 1; b.name = "%Y%"; b.value = uint32_t(z);
    p.args.push_back(a); p.args.push_back(b);
  }
  postTicker(p, hasLoc, x, z);
  list_.push_back(p);
}

bool PetitionerManager::withdraw(uint32_t id) {
  for (size_t i = 0; i < list_.size(); ++i)
    if (list_[i].id == id) { dropTicker(list_[i]); list_.erase(list_.begin() + long(i)); return true; }
  return false;
}

void PetitionerManager::update() {
  std::vector<PendingPetition> keep;
  for (auto p : list_) {
    bool del = false;
    auto inc = [&] { if (p.counter < 255) ++p.counter; };
    if (p.persistent()) {
      if (p.limit == 0 || p.counter < p.limit) inc();
      else if (!p.hidden()) { p.flags |= kFlagHidden; dropTicker(p); }
    } else if (p.expiring()) {
      if (p.counter < p.limit) inc(); else del = true;
    } else if (p.raised()) {
      if (p.hidden()) {
        if (p.limit == 0 || p.counter < p.limit) inc(); else del = true;
      }
    } else del = true;
    if (del) dropTicker(p);
    else keep.push_back(p);
  }
  for (auto& p : keep) p.flags &= uint8_t(~kFlagRaised);
  list_.swap(keep);
}

void PetitionerManager::generateOrdinancePetitions(const PetitionerInputs& in, GameRng& rng) {
  if (in.population < 100) return;
  auto T = [&](const char* k) { return config.threshold(k); };
  auto row = [&](uint32_t petition, bool cond) {
    if (cond && !pending(petition) && rng.rand(100) == 0) request(petition, 0, 120, false, true);
  };
  const int64_t N = in.population;
  const int Y = in.year;
  auto av = [&](uint32_t o) { return in.available(o); };
  auto on = [&](uint32_t o) { return in.enacted(o); };
  auto simple = [&](uint32_t pet, uint32_t o, const char* yearKey) {
    row(pet, Y > T(yearKey) && av(o) && !on(o));
  };
  row(0x69, N > T("POPULATION_FOR_GAMBLING") && av(0xA0D07129) && !on(0xA0D07129) && in.aura < T("MAX_GAMBLING_AURA"));
  row(0x68, N > T("POPULATION_FOR_GAMBLING") && av(0xA0D07129) && on(0xA0D07129));
  row(0x6A, in.aura < T("PRO_READING_AURA") && N > T("PRO_READING_POP") && av(0xE0D07233) && !on(0xE0D07233));
  row(0x63, av(0x62BF1DAA) && !on(0x62BF1DAA));
  row(0x64, av(0x62BF1DB9) && !on(0x62BF1DB9));
  row(0x61, av(0x00D0723D) && !on(0x00D0723D));
  row(0x62, av(0x00D0723D) && on(0x00D0723D));
  row(0x6B, av(0xE0D07239) && !on(0xE0D07239));
  row(0x6D, av(0xE0D07239) && on(0xE0D07239));
  row(0x6E, in.lifeExpectancy < T("LE_LEVEL_FOR_CLINIC") && av(0x40D07236) && !on(0x40D07236));
  row(0x71, av(0x20D07175) && !on(0x20D07175) && N > T("HOMELESS_SHELTER_POPULATION"));
  simple(0x73, 0xA2BF1DE5, "SHUTTLE_SERVICE_YEAR");
  row(0x76, Y > T("POWER_CONSERVATION_YEAR") && av(0x82B9999B) && !on(0x82B9999B));
  row(0x75, Y > T("POWER_CONSERVATION_YEAR") && av(0x82B9999B) && on(0x82B9999B));
  simple(0x7F, 0xE0D0722E, "NUCLEAR_FREE_YEAR");
  row(0x80, Y > T("PARKING_FINES_YEAR") && av(0x015B4D1E) && !on(0x015B4D1E));
  row(0x81, Y > T("PARKING_FINES_YEAR") && av(0x015B4D1E) && on(0x015B4D1E));
  simple(0xD6, 0xA2F6E7F9, "ELECTRONICS_JOB_FAIR_YEAR");
  simple(0xDC, 0xC2F90EB1, "WATER_METERS_YEAR");
  simple(0xDE, 0xC2F6E81F, "TIRE_RECYCLING_YEAR");
  simple(0xDF, 0x62F6E7CF, "TOURIST_PROMOTION_YEAR");
  simple(0xE0, 0xA2F6E7DA, "CONSERVATION_CORPS_YEAR");
  simple(0xE1, 0x22F6E81B, "PAPERWORK_REDUCTION_YEAR");
  simple(0xE2, 0xC2F6E804, "PUBLIC_ACCESS_TV_YEAR");
  simple(0xE4, 0x22F6E814, "BACKYARD_COMPOST_YEAR");
  simple(0xE5, 0xA2F917CF, "IND_WASTE_DISPOSAL_YEAR");
  simple(0xF1, 0x22F6E7F4, "AEROSPACE_TAX_YEAR");
  simple(0xF3, 0xE2F6E7EA, "CLEAN_INDUSTRY_ASSOC_YEAR");
  row(0xF4, av(0xC2BF1DD1) && !on(0xC2BF1DD1) && in.quakeHistoryCondition);
  simple(0xF5, 0xE2F6E7FE, "BIOTECH_TAX_INCENTIVE_YEAR");
  simple(0xF6, 0x82E42E30, "STAIRWELL_LIGHTING_YEAR");
}

void PetitionerManager::weeklyCycle(const PetitionerInputs& in, GameRng& rng) {
  generateOrdinancePetitions(in, rng);
  update();
  for (uint32_t id : forced) request(id, 0, 0);
}

PetitionEffect PetitionerManager::answer(uint32_t id, bool accepted, int32_t) {
  PetitionEffect e;
  e.petition = id; e.accepted = accepted;
  {  // voice reaction by character type t = the PetitionerExtraInfo `a` value (spec 22 s5.5) [STATIC]
    static const int acc[19] = {0, 0, 0xA4, 0xA4, 0xA2, 0xA0, 0xA3, 0xA4, 0x9F, 0xA6, 0xA6, 0xA5, 0xA2, 0xA6, 0xA4, 0xA1, 0xA6, 0xA4, 0xA4};
    static const int dec[19] = {0, 0, 0x9C, 0x9C, 0x9E, 0x9E, 0x9D, 0x9E, 0x9D, 0x9D, 0x9D, 0x9C, 0x9E, 0x9D, 0x9C, 0x9D, 0x9D, 0x9C, 0x9B};
    PetitionerConfig::ExtraInfo xi = config.extraInfo(id);
    const uint32_t t = xi.found ? xi.portrait : 0;
    if (t >= 2 && t <= 18) e.voiceSound = accepted ? acc[t] : dec[t];
  }
  if (PendingPetition* p = findMut(id)) { p->flags |= kFlagHidden; dropTicker(*p); }
  if (sink_) {
    PetitionText cat = accepted ? PetitionText::TickerX : PetitionText::TickerY;
    uint32_t t = config.tableFor(cat, id, [&](uint32_t tb, uint32_t i) { return sink_->hasString(tb, i); });
    if (t) { e.outcomeTicker = {t, id}; e.outcomeTickerItem = sink_->post(t, id, false, 0, 0); }
  }
  uint32_t ord; int val;
  if (petitionOrdinanceAction(id, ord, val)) {
    if (accepted) { e.kind = PetitionEffect::SetOrdinance; e.ordinance = ord; e.value = val; }
    return e;
  }
  switch (id) {
    case 0x01: case 0x04: case 0x07: case 0x0A: case 0x84: case 0x88: case 0x8A: case 0x8B: case 0x8C: case 0x8D:
    case 0x8E: case 0x8F: case 0x90: case 0x94: case 0x96: case 0x97: case 0xCD: case 0x12C: case 0x12F:
      e.kind = PetitionEffect::RewardAvailable;
      if (accepted) e.buildingType = petitionRewardBuilding(id);
      return e;
    case 0x10: case 0x11: case 0x12: case 0x95: case 0xCE: case 0xD2: e.kind = PetitionEffect::BusinessDeal; return e;
    case 0x9B: case 0x9C: case 0x9D: case 0x9E: e.kind = PetitionEffect::WorkerStrike; return e;
    case 0x9F: case 0xA0: case 0xA2: case 0xCC: e.kind = PetitionEffect::PublicSafetyStrike; return e;
    case 0xA3: case 0xA4: e.kind = PetitionEffect::TransitStrike; return e;
    case 0xD7:
      if (accepted) { e.kind = PetitionEffect::Treasury; e.treasuryDelta = 100000; }
      else e.kind = PetitionEffect::PlannerFlag;
      return e;
    case 0x12D: if (accepted) e.kind = PetitionEffect::StartParade; return e;
    default: break;
  }
  if ((id >= 0xA5 && id <= 0xCA) || (id >= 0xE9 && id <= 0xEE) || id == 0xF7 || id == 0xF8) {
    e.kind = PetitionEffect::NeighbourDeal;
    if (id >= 0xE9 && id <= 0xEE) e.voiceSound = accepted ? 0x9B : 0xA4;
  }
  return e;
}

// ---------------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------------
void putArgs(Bytes& b, const std::vector<TopicArg>& args) {
  put32(b, uint32_t(args.size()));
  for (auto& a : args) {
    put32(b, a.index); put32(b, uint32_t(a.name.size()));
    b.insert(b.end(), a.name.begin(), a.name.end());
    put32(b, a.a); put32(b, a.b); put32(b, a.value);
  }
}
bool getArgs(Reader& r, std::vector<TopicArg>& args) {
  uint32_t n = r.u32();
  if (n > 100000) return false;
  args.clear();
  for (uint32_t i = 0; i < n; ++i) {
    TopicArg a;
    a.index = r.u32();
    uint32_t len = r.u32();
    if (len > r.remaining()) return false;
    a.name = r.str(len);
    a.a = r.u32(); a.b = r.u32(); a.value = r.u32();
    args.push_back(a);
  }
  return true;
}

Bytes PetitionerManager::save() const {
  Bytes b;
  put16(b, 1); put16(b, 2); put32(b, 0xDEADBEEFu);
  put32(b, 1); put32(b, uint32_t(list_.size())); put32(b, 1); put32(b, 1);
  for (auto& p : list_) {
    put32(b, p.id); put32(b, p.argument); put32(b, p.tickerItem); put32(b, p.reserved);
    b.push_back(p.reserved8); b.push_back(p.limit); b.push_back(p.counter); b.push_back(p.flags);
    putArgs(b, p.args);
  }
  return b;
}

bool PetitionerManager::load(const Bytes& d) {
  try {
    Reader r(d);
    if (r.u16() != 1) return false;
    r.u16();
    if (r.u32() != 0xDEADBEEFu) return false;
    if (r.u32() != 1) return false;
    uint32_t n = r.u32();
    r.u32(); r.u32();
    if (n > 100000) return false;
    std::vector<PendingPetition> v;
    for (uint32_t i = 0; i < n; ++i) {
      PendingPetition p;
      p.id = r.u32(); p.argument = r.u32(); p.tickerItem = 0; r.u32(); p.reserved = r.u32();
      p.reserved8 = r.u8(); p.limit = r.u8(); p.counter = r.u8(); p.flags = r.u8();
      if (!getArgs(r, p.args)) return false;
      v.push_back(p);
    }
    if (r.remaining() != 0) return false;
    list_ = v;
    return true;
  } catch (const FormatError&) { return false; }
}

}  // namespace m3k
