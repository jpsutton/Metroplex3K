// Population, demographics, EQ, health, life expectancy, strikes, workforce.
// See spec/20-sim-population.md and spec/10-save-format.md section 6.11.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "core/bytes.h"
#include "core/chart.h"
#include "core/clock.h"
#include "core/ini.h"

namespace m3k {

constexpr int kAges = 90;

// SC3ResLayer.ini [TuningParameters], already scaled to their working units.
// Defaults are best guesses where the spec does not give a shipped value (marked "guess").
struct PopulationConfig {
  int externalFunSupply = 0;
  float newbornPopPct = 0.0136f;          // shipped 136
  float minNewbornPopPct = 0.0100f;       // guess
  float maxNewbornPopPct = 0.0180f;       // guess
  float ageCohortDecay = 0.9970f;         // shipped 9970
  float babyTrendChangeProb = 0.05f;      // guess
  float eqDecay = 0.9900f;                // guess
  float hqDecay = 0.9900f;                // guess
  float optimalYearlyHQ = 1.0f;           // guess (x0.01)
  float optimalYearlySchoolEQ = 4.0f;     // guess (x0.01)
  float optimalYearlyCollegeEQ = 4.0f;    // guess (x0.01)
  float hospitalizationRate = 0.005f;     // guess
  double pollutionFactor = 1e-6;          // guess (x1e-8)
  uint8_t ageOfRebellion = 18;            // guess
  float monthlyYouthCurfewOrdEffect = 0.01f;
  float monthlyReadingOrdEffectAdult = 0.002f, monthlyReadingOrdEffectChild = 0.02f;
  float monthlyJrSportsOrdEffect = 0.01f;
  float monthlyCrossingGuardsOrdEffect = 0.001f, monthlyFreeClinicsOrdEffect = 0.002f,
        monthlyCPRTrainingOrdEffect = 0.002f, monthlySmokingBanOrdEffect = 0.002f;
  int youthCurfewMinAge = 12, youthCurfewMaxAge = 17;
  int readingMinChildAge = 5, readingMaxChildAge = 12;
  int jrSportsMinAge = 6, jrSportsMaxAge = 17;
  uint8_t minAcceptableEducationFunding = 80, minAcceptableHealthFunding = 80;
  uint8_t minAcceptableEducationRating = 2, minAcceptableHealthRating = 2;
  uint8_t educationStrikeEfficiency = 50, healthStrikeEfficiency = 50;   // raw byte, used UNSCALED (defect)
  uint32_t maxEducationStrikeDuration = 6, maxHealthStrikeDuration = 6;

  static PopulationConfig fromIni(const IniFile& resLayer);
};

enum class EduKind { School = 0, College = 1, Library = 2, Museum = 3 };

struct EduFacility {
  EduKind kind = EduKind::School;
  float capacity = 0;     // OptimalDeskCount (school/college) or PopServed (library/museum)
  float effect = 0;       // EQ points per person per month (funded)
  int minAge = 0, maxAge = 89;
  uint32_t staff = 0;     // funded teachers/professors (recorded only)
};

struct Hospital {
  uint32_t beds = 0;      // OptimalBedCount
  float effect = 0;       // funded HQ effect
  uint32_t doctors = 0;
};

struct PopulationInput {
  uint32_t population = 0;          // N (recommended: N_R of the economy)
  int month = 1, year = 1900;       // date of this monthly run
  uint32_t pollution1 = 0, pollution2 = 0;   // city-wide totals (air, water)
  int educationFunding = 100, healthFunding = 100;   // percent
  std::function<bool(uint32_t ordinanceId)> ordinanceEnacted;
  std::vector<EduFacility> education;
  std::vector<Hospital> hospitals;
  uint32_t museumCapacity = 0, libraryCapacity = 0;  // recorded sums (optional; derived if 0)
};

enum StrikeKind { StrikeHealth = 0, StrikeSchool = 1, StrikeCollege = 2 };

struct PopulationResult {
  bool strikeStarted[3] = {};
  bool strikeEnded[3] = {};
  bool yearlyAgeing = false;
};

class Population {
 public:
  explicit Population(PopulationConfig c = PopulationConfig(), uint32_t seed = 1);

  PopulationConfig cfg;
  SimRng rng;

  // Persisted state (residential-layer state block, 1900 bytes).
  Chart popChart, eqChart, leChart;
  bool schoolStrike = false, healthStrike = false, collegeStrike = false;
  uint32_t schoolStrikeMonths = 0, healthStrikeMonths = 0, collegeStrikeMonths = 0;
  uint8_t schoolRating = 0, healthRating = 0, collegeRating = 0;
  uint32_t patients = 0, schoolAgePop = 0, collegeAgePop = 0;
  uint32_t schoolDesks = 0, collegeDesks = 0, hospitalBeds = 0, museumCapacity = 0, libraryCapacity = 0;
  uint8_t unknown1BE = 0;
  uint8_t birthTrend = 0;   // 0 rising, 1 falling, 2 steady
  float eq[kAges] = {}, hq[kAges] = {}, share[kAges] = {}, le[kAges] = {};
  uint32_t teachers = 0, professors = 0, doctors = 0;
  // Not persisted.
  int oldestAge = 64;       // A*
  float healthGain = 0;     // x of the last run

  // New-city initial distribution (spec 20 s3.1) and empty charts.
  void newCity();

  // Monthly step (spec 20 s3.2).
  PopulationResult monthly(const PopulationInput& in);

  // Statistics.
  uint8_t qEQ(int lo, int hi) const;          // spec 4.5: unweighted mean over ages with share > 0
  uint8_t cityEQ() const { return qEQ(0, 89); }
  uint8_t cityLifeExpectancy() const;
  // Workforce fraction w = p[20] + ... + p[65] (spec 20 s6) [STATIC]: no normalisation or weighting.
  float workforceFraction() const;
  int32_t workers(int32_t residents) const;   // round(n * w)
  static constexpr int kWorkAgeMin = 20, kWorkAgeMax = 65;
  // Industrial developer EQ input: working-age EQ Q(20, 65) (spec 20 s4.5, spec 15 s4.5) [STATIC].
  uint8_t workingAgeEQ() const { return qEQ(kWorkAgeMin, kWorkAgeMax); }
  float shareSum() const;
  // Number of people of an age for a given city population.
  double peopleAt(int age, uint32_t population) const { return double(population) * share[age]; }

  // Records. State block only (1900 bytes).
  static constexpr size_t kStateBytes = 0x76C;
  void parseState(const uint8_t* p, size_t n);
  Bytes serializeState() const;
  // Full record 206C6E7C:C106C4F5:0 = header + n + valve blocks + state (+ markers when present).
  static constexpr uint32_t kType = 0x206C6E7C, kGroup = 0xC106C4F5, kInstance = 0;
  uint16_t recordVersion = 3;
  std::vector<Bytes> valveBlocks;   // raw 41-byte blocks (3000, 3001); kept verbatim
  void parseRecord(const uint8_t* p, size_t n);
  Bytes serializeRecord() const;

 private:
  void configureCharts();
  void finishLoadDerived();
  float yearlyEQShiftFactor() const { return cfg.eqDecay; }
};

}  // namespace m3k
