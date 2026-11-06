// Petitioner manager (spec/22 section 5 and persisted record P.2).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/gamerng.h"
#include "core/ini.h"
#include "core/news.h"

namespace m3k {

constexpr uint32_t kPetitionerRecType = 0x206C6E7C, kPetitionerRecGroup = 0x02619041, kPetitionerRecInstance = 0;

// Petition / topic list flag bits (P.1 / P.2)
enum : uint8_t { kFlagHidden = 1, kFlagPersistent = 2, kFlagExpiring = 4, kFlagRaised = 8 };

enum class PetitionText { Advice, Button1, Button2, Heading, Message, Name, Reaction1, Reaction2, Ticker, TickerX, TickerY };
constexpr int kPetitionTextCount = 11;

// Everything read from PetitionerManager.ini and PetitionerExtraInfo files.
// Picture instance (52DB60A0:FFxx) of a [PetitionerExtraInfo] portrait index (the game-side presenter pictures, spec 23 4.7.0); 0 = none.
uint32_t petitionerPortraitImage(uint32_t portraitIndex);

class PetitionerConfig {
 public:
  void loadManagerIni(const IniFile& f);
  static std::vector<std::string> extraInfoFiles(const IniFile& managerIni);   // [PetitionerExtraInfo Filenames]
  void addExtraInfo(const IniFile& f);                                       // first file with the key wins
  // Threshold from [PETITIONER_ORDINANCE_TRIGGER_MAPPINGS] (0 if missing, per the INI rules).
  int threshold(const std::string& key) const;
  void setThreshold(const std::string& key, int v) { thr_[key] = v; }
  // Advisor id delivering the advice for petition n (0 = none).
  uint32_t adviceAdvisor(uint32_t petition) const;
  // First table of the category that contains `id` according to `has`; 0 if none.
  uint32_t tableFor(PetitionText cat, uint32_t id, const std::function<bool(uint32_t, uint32_t)>& has) const;
  const std::vector<uint32_t>& tables(PetitionText cat) const { return tables_[int(cat)]; }
  void setTables(PetitionText cat, std::vector<uint32_t> t) { tables_[int(cat)] = std::move(t); }
  struct ExtraInfo { uint32_t portrait = 0, second = 0; bool found = false; };
  ExtraInfo extraInfo(uint32_t petition) const;
  int adviceSize = 0;
 private:
  std::map<std::string, int> thr_;
  std::map<uint32_t, uint32_t> advice_;
  std::vector<uint32_t> tables_[kPetitionTextCount];
  std::map<uint32_t, ExtraInfo> extra_;
};

struct PendingPetition {
  uint32_t id = 0, argument = 0, tickerItem = 0, reserved = 0;
  uint8_t reserved8 = 0, limit = 0, counter = 0, flags = 0;
  std::vector<TopicArg> args;
  bool hidden() const { return flags & kFlagHidden; }
  bool persistent() const { return flags & kFlagPersistent; }
  bool expiring() const { return flags & kFlagExpiring; }
  bool raised() const { return flags & kFlagRaised; }
};

// City statistics the petitioner manager reads each cycle.
struct PetitionerInputs {
  int64_t population = 0;
  int year = 2000;
  int aura = 0;                     // signed city aura
  int lifeExpectancy = 80;
  // Ordinance state keyed by ordinance id: bit0 available, bit1 enacted.
  std::map<uint32_t, uint8_t> ordinances;
  bool quakeHistoryCondition = false;   // [OPEN] disaster-history condition of petition 0xF4
  bool available(uint32_t o) const { auto i = ordinances.find(o); return i != ordinances.end() && (i->second & 1); }
  bool enacted(uint32_t o) const { auto i = ordinances.find(o); return i != ordinances.end() && (i->second & 2); }
};

// Effect of answering a petition (section 5.5); the caller applies it.
struct PetitionEffect {
  enum Kind { None, SetOrdinance, RewardAvailable, BusinessDeal, WorkerStrike, PublicSafetyStrike, TransitStrike,
              NeighbourDeal, Treasury, PlannerFlag, StartParade } kind = None;
  uint32_t petition = 0;
  bool accepted = false;
  uint32_t ordinance = 0; int value = 0;        // SetOrdinance
  int buildingType = 0;                         // RewardAvailable (accept only)
  int64_t treasuryDelta = 0;
  int voiceSound = 0;                           // AUDIO.INI item or 0 = by character type [OPEN table]
  NewsKey outcomeTicker;                        // TICKER_X / TICKER_Y item (table 0 if none)
  uint32_t outcomeTickerItem = 0;
};

class PetitionerManager {
 public:
  PetitionerConfig config;
  std::vector<uint32_t> forced;                  // cheat-forced petitions, re-requested every cycle

  void setSink(TickerSink* s) { sink_ = s; }
  // Request a petition (section 5.3). Ignored (marked re-raised) if already pending.
  void request(uint32_t id, uint32_t argument, int limit, bool persistent = false, bool expiring = false,
               bool hasLoc = false, int x = 0, int z = 0);
  // Remove a pending petition if present (ticker withdrawn).
  bool withdraw(uint32_t id);
  bool pending(uint32_t id) const { return find(id) != nullptr; }
  bool visible(uint32_t id) const { auto* p = find(id); return p && !p->hidden(); }
  const PendingPetition* find(uint32_t id) const;
  const std::vector<PendingPetition>& list() const { return list_; }
  std::vector<const PendingPetition*> presentable() const;  // pending and not hidden, list order

  // One weekly cycle: ordinance petitions (5.4) then update rules (5.3), R cleared, forced re-requested.
  void weeklyCycle(const PetitionerInputs& in, GameRng& rng);
  // Update rules only (used by tests and the on-demand pass).
  void update();
  void generateOrdinancePetitions(const PetitionerInputs& in, GameRng& rng);

  // Answer a petition: H := 1, outcome ticker, effect (5.5).
  PetitionEffect answer(uint32_t id, bool accepted, int32_t unusedToday = 0);
  void clear() { list_.clear(); }

  // Persistence P.2
  Bytes save() const;
  bool load(const Bytes& b);

 private:
  TickerSink* sink_ = nullptr;
  std::vector<PendingPetition> list_;
  PendingPetition* findMut(uint32_t id);
  void postTicker(PendingPetition& p, bool hasLoc, int x, int z);
  void dropTicker(PendingPetition& p);
  bool hasString(uint32_t t, uint32_t id) { return sink_ ? sink_->hasString(t, id) : false; }
};

// Shared serialisation of topic/petition argument entries.
void putArgs(Bytes& b, const std::vector<TopicArg>& args);
bool getArgs(Reader& r, std::vector<TopicArg>& args);

// Accept actions (tables in 5.4). Returns false if the petition is not an ordinance petition.
bool petitionOrdinanceAction(uint32_t petition, uint32_t& ordinance, int& value);
// Reward petition -> building type (6.2 mapping); 0 if none.
int petitionRewardBuilding(uint32_t petition);

}  // namespace m3k
