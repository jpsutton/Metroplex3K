#include "core/population.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/tune.h"

namespace m3k {

PopulationConfig PopulationConfig::fromIni(const IniFile& ini) {
  PopulationConfig c;
  using namespace tune;
  const char* S = "TuningParameters";
  auto gi = [&](const char* k, double d) { return getDouble(ini, S, k, d); };
  c.externalFunSupply = int(gi("ExternalFunSupply", c.externalFunSupply));
  c.newbornPopPct = float(gi("NewbornPopPct", c.newbornPopPct * 10000) * 0.0001);
  c.minNewbornPopPct = float(gi("MinNewbornPopPct", c.minNewbornPopPct * 10000) * 0.0001);
  c.maxNewbornPopPct = float(gi("MaxNewbornPopPct", c.maxNewbornPopPct * 10000) * 0.0001);
  c.ageCohortDecay = float(gi("AgeCohortDecay", c.ageCohortDecay * 10000) * 0.0001);
  c.babyTrendChangeProb = float(gi("BabyTrendChangeProb", c.babyTrendChangeProb * 10000) * 0.0001);
  c.eqDecay = float(gi("EQDecay", c.eqDecay * 10000) * 0.0001);
  c.hqDecay = float(gi("HQDecay", c.hqDecay * 10000) * 0.0001);
  c.optimalYearlyHQ = float(gi("OptimalYearlyHQ", c.optimalYearlyHQ * 100) * 0.01);
  c.optimalYearlySchoolEQ = float(gi("OptimalYearlySchoolEQ", c.optimalYearlySchoolEQ * 100) * 0.01);
  c.optimalYearlyCollegeEQ = float(gi("OptimalYearlyCollegeEQ", c.optimalYearlyCollegeEQ * 100) * 0.01);
  c.hospitalizationRate = float(gi("HospitalizationRate", c.hospitalizationRate * 10000) * 0.0001);
  c.pollutionFactor = gi("PollutionFactor", c.pollutionFactor * 1e8) * 1e-8;
  c.ageOfRebellion = uint8_t(gi("AgeOfRebellion", c.ageOfRebellion));
  c.monthlyYouthCurfewOrdEffect = float(gi("MonthlyYouthCurfewOrdEffect", c.monthlyYouthCurfewOrdEffect * 10000) * 0.0001);
  c.monthlyReadingOrdEffectAdult = float(gi("MonthlyReadingOrdEffectAdult", c.monthlyReadingOrdEffectAdult * 10000) * 0.0001);
  c.monthlyReadingOrdEffectChild = float(gi("MonthlyReadingOrdEffectChild", c.monthlyReadingOrdEffectChild * 10000) * 0.0001);
  c.monthlyJrSportsOrdEffect = float(gi("MonthlyJrSportsOrdEffect", c.monthlyJrSportsOrdEffect * 10000) * 0.0001);
  c.monthlyCrossingGuardsOrdEffect = float(gi("MonthlyCrossingGuardsOrdEffect", c.monthlyCrossingGuardsOrdEffect * 10000) * 0.0001);
  c.monthlyFreeClinicsOrdEffect = float(gi("MonthlyFreeClinicsOrdEffect", c.monthlyFreeClinicsOrdEffect * 10000) * 0.0001);
  c.monthlyCPRTrainingOrdEffect = float(gi("MonthlyCPRTrainingOrdEffect", c.monthlyCPRTrainingOrdEffect * 10000) * 0.0001);
  c.monthlySmokingBanOrdEffect = float(gi("MonthlySmokingBanOrdEffect", c.monthlySmokingBanOrdEffect * 10000) * 0.0001);
  c.youthCurfewMinAge = int(gi("YouthCurfewOrdMinAge", c.youthCurfewMinAge));
  c.youthCurfewMaxAge = int(gi("YouthCurfewOrdMaxAge", c.youthCurfewMaxAge));
  c.readingMinChildAge = int(gi("ReadingOrdMinChildAge", c.readingMinChildAge));
  c.readingMaxChildAge = int(gi("ReadingOrdMaxChildAge", c.readingMaxChildAge));
  c.jrSportsMinAge = int(gi("JrSportsOrdMinAge", c.jrSportsMinAge));
  c.jrSportsMaxAge = int(gi("JrSportsOrdMaxAge", c.jrSportsMaxAge));
  c.minAcceptableEducationFunding = uint8_t(gi("MinAcceptableEducationFunding", c.minAcceptableEducationFunding));
  c.minAcceptableHealthFunding = uint8_t(gi("MinAcceptableHealthFunding", c.minAcceptableHealthFunding));
  c.minAcceptableEducationRating = uint8_t(gi("MinAcceptableEducationRating", c.minAcceptableEducationRating));
  c.minAcceptableHealthRating = uint8_t(gi("MinAcceptableHealthRating", c.minAcceptableHealthRating));
  c.educationStrikeEfficiency = uint8_t(gi("EducationStrikeEfficiency", c.educationStrikeEfficiency));
  c.healthStrikeEfficiency = uint8_t(gi("HealthStrikeEfficiency", c.healthStrikeEfficiency));
  c.maxEducationStrikeDuration = uint32_t(gi("MaxEducationStrikeDuration", c.maxEducationStrikeDuration));
  c.maxHealthStrikeDuration = uint32_t(gi("MaxHealthStrikeDuration", c.maxHealthStrikeDuration));
  return c;
}

Population::Population(PopulationConfig c, uint32_t seed) : cfg(c), rng(seed) {
  configureCharts();
  newCity();
}

void Population::configureCharts() {
  popChart = Chart::make(0, 10000, true, 10000);
  eqChart = Chart::make(0, 150, false);
  leChart = Chart::make(0, 89, false);
}

void Population::newCity() {
  configureCharts();
  schoolStrike = healthStrike = collegeStrike = false;
  schoolStrikeMonths = healthStrikeMonths = collegeStrikeMonths = 0;
  schoolRating = healthRating = collegeRating = 0;
  patients = schoolAgePop = collegeAgePop = 0;
  schoolDesks = collegeDesks = hospitalBeds = museumCapacity = libraryCapacity = 0;
  teachers = professors = doctors = 0;
  unknown1BE = 0;
  healthGain = 0;
  const float N = cfg.newbornPopPct, D = cfg.ageCohortDecay;
  for (int a = 0; a < kAges; a++) share[a] = 0;
  share[0] = N;
  for (int a = 1; a <= 64; a++) share[a] = D * share[a - 1];
  float S = 0;
  for (int a = 0; a < kAges; a++) S += share[a];
  if (S != 1.0f)
    for (int a = 0; a <= 64; a++) share[a] += (1.0f - S) * 0.015384615f;
  for (int a = 0; a < kAges; a++) {
    if (a <= 20) eq[a] = float(85 * a / 20);
    else eq[a] = cfg.eqDecay * eq[a - 1];
  }
  le[0] = 65.0f;
  for (int a = 1; a < kAges; a++) le[a] = 0.99696726f * le[a - 1];
  hq[0] = cfg.optimalYearlyHQ * 55.625f;
  for (int a = 1; a < kAges; a++) hq[a] = std::max(0.0f, (le[a] - 50.0f) * cfg.optimalYearlyHQ * 3.7083333f);
  birthTrend = 0;
  oldestAge = 64;
}

uint8_t Population::qEQ(int lo, int hi) const {
  hi = std::min(hi, kAges - 1);
  double sum = 0;
  int n = 0;
  for (int a = std::max(lo, 0); a <= hi; a++)
    if (share[a] > 0) { sum += eq[a]; n++; }
  if (!n) return 0;
  return uint8_t(std::clamp(std::nearbyint(sum / n), 0.0, 255.0));
}

uint8_t Population::cityLifeExpectancy() const {
  double sum = 0;
  int n = 0;
  for (int a = 0; a < kAges; a++)
    if (share[a] > 0) { sum += le[a]; n++; }
  if (!n) return 0;
  return uint8_t(std::clamp(std::nearbyint(sum / n), 0.0, 255.0));
}

float Population::shareSum() const {
  float s = 0;
  for (int a = 0; a < kAges; a++) s += share[a];
  return s;
}

float Population::workforceFraction() const {
  float w = 0;
  for (int a = kWorkAgeMin; a <= kWorkAgeMax && a < kAges; a++) w += share[a];
  return w;
}

int32_t Population::workers(int32_t residents) const {
  return int32_t(std::nearbyint(double(residents) * workforceFraction()));
}

PopulationResult Population::monthly(const PopulationInput& in) {
  PopulationResult res;
  const float N = float(in.population);
  const bool january = in.month == 1;

  // 1. The cap of valve 3000 from valve 3001 (spec 20 s3.2 step 1) is set by the valve layer (growth::ValveLayer).

  // 2. facility totals (funding is already folded into the facilities' effects by the caller)
  schoolDesks = collegeDesks = museumCapacity = libraryCapacity = hospitalBeds = 0;
  teachers = professors = doctors = 0;
  bool haveSchool = false, haveCollege = false;
  for (auto& f : in.education) {
    switch (f.kind) {
      case EduKind::School: schoolDesks += uint32_t(f.capacity); teachers += f.staff; haveSchool = true; break;
      case EduKind::College: collegeDesks += uint32_t(f.capacity); professors += f.staff; haveCollege = true; break;
      case EduKind::Library: libraryCapacity += uint32_t(f.capacity); break;
      case EduKind::Museum: museumCapacity += uint32_t(f.capacity); break;
    }
  }
  for (auto& h : in.hospitals) { hospitalBeds += h.beds; doctors += h.doctors; }
  const bool haveHospital = !in.hospitals.empty();

  // 3. education distribution (uses shares from before this month's changes)
  float gain[4][kAges] = {}, seats[4][kAges] = {};
  for (auto& f : in.education) {
    int g = int(f.kind);
    int lo = f.minAge, hi = std::min(f.maxAge, kAges - 1);
    if (lo < 0) lo = 0;
    if (lo > hi) continue;
    float sp = 0;
    for (int a = lo; a <= hi; a++) sp += share[a];
    if (sp <= 0) continue;
    float k = 1.0f;
    if ((f.kind == EduKind::School && schoolStrike) || (f.kind == EduKind::College && collegeStrike))
      k = float(cfg.educationStrikeEfficiency);
    for (int a = lo; a <= hi; a++) {
      float s = std::nearbyint(f.capacity * share[a] / sp);
      seats[g][a] += s;
      gain[g][a] += f.effect * s * k;
    }
  }

  // 4. patients, health gain x, health rating
  double pbar = double((uint64_t(in.pollution1) + in.pollution2) / 2);
  double pat = std::trunc((pbar * cfg.pollutionFactor + cfg.hospitalizationRate) * double(in.population));
  patients = pat > 0 ? uint32_t(pat) : 0;
  double B = 0, W = 0;
  for (auto& h : in.hospitals) { B += h.beds; W += double(h.beds) * h.effect; }
  float x;
  if (pat <= 0) {
    x = 0.00001f;
  } else {
    double Dn = pat;
    if (pat <= B && B > 0) Dn = B;
    x = float(W / Dn);
  }
  {
    double r;
    if (pat != 0 || x <= 0) {
      double opt = cfg.optimalYearlyHQ / 12.0;
      r = opt > 0 ? std::nearbyint(4.0 * x / opt - 0.4) : 0;
      healthRating = uint8_t(std::clamp(r, 0.0, 4.0));
    } else {
      healthRating = 4;
    }
  }
  if (in.population != 0 && in.ordinanceEnacted) {
    if (in.ordinanceEnacted(0xC2BF1DEF)) x += cfg.monthlyCrossingGuardsOrdEffect;
    if (in.ordinanceEnacted(0x40D07236)) x += cfg.monthlyFreeClinicsOrdEffect;
    if (in.ordinanceEnacted(0x00D0723D)) x += cfg.monthlyCPRTrainingOrdEffect;
    if (in.ordinanceEnacted(0xE0D07239)) x += cfg.monthlySmokingBanOrdEffect;
  }
  if (healthStrike) x *= float(cfg.healthStrikeEfficiency);
  healthGain = x;

  // 5. per-age loop, a = 89..0
  const auto ens = [&](uint32_t id) { return in.ordinanceEnacted && in.ordinanceEnacted(id); };
  const bool curfew = ens(0xC2BF1DC5), reading = ens(0xE0D07233), sports = ens(0x815B4CEF);
  const float Epar = cfg.ageOfRebellion
                         ? std::min(float(qEQ(20, 40)), 120.0f) / float(cfg.ageOfRebellion * 12)
                         : 0.0f;
  float S = 0, Gschool = 0, Gcollege = 0;
  uint32_t schoolPop = 0, collegePop = 0;
  int astar = -1;
  for (int a = kAges - 1; a >= 0; a--) {
    if (share[a] <= 0) { share[a] = 0; continue; }
    if (astar < 0) astar = a;
    if (le[a] < float(a)) share[a] *= 0.9090909f;
    if (N * share[a] < 1.0f) share[a] = 0;
    S += share[a];
    // EQ
    const float people = N * share[a];
    float dfac = 0;
    for (int g = 0; g < 4; g++) {
      if (seats[g][a] >= people && seats[g][a] >= 1.0f) dfac += gain[g][a] / seats[g][a];
      else if (people > 0) dfac += gain[g][a] / people;
    }
    float dord = 0;
    if (curfew && a >= cfg.youthCurfewMinAge && a <= cfg.youthCurfewMaxAge) dord += cfg.monthlyYouthCurfewOrdEffect;
    if (reading) {
      if (a >= cfg.readingMinChildAge && a <= cfg.readingMaxChildAge) dord += cfg.monthlyReadingOrdEffectChild;
      else dord += cfg.monthlyReadingOrdEffectAdult;
    }
    if (sports && a >= cfg.jrSportsMinAge && a <= cfg.jrSportsMaxAge) dord += cfg.monthlyJrSportsOrdEffect;
    eq[a] += dord + dfac;
    if (a >= 7 && a <= 18) { Gschool += dfac; schoolPop += uint32_t(std::nearbyint(people)); }
    if (a >= 19 && a <= 24) { Gcollege += dfac; collegePop += uint32_t(std::nearbyint(people)); }
    if (a < cfg.ageOfRebellion) eq[a] += Epar;
    if (a <= 26) {
      float floorEq = 85.0f * float(a) * 0.03846154f;
      if (eq[a] < floorEq) eq[a] = floorEq;
    }
    if (eq[a] > 150.0f) eq[a] = 150.0f;
    // HQ and LE
    if (x > 0) hq[a] += x;
    else if (hq[a] > 0) hq[a] = cfg.hqDecay * hq[a];
    if (cfg.optimalYearlyHQ > 0)
      le[a] = std::min(89.0f, 50.0f + hq[a] / cfg.optimalYearlyHQ * 0.26966292f);
  }
  oldestAge = astar < 0 ? 0 : astar;
  schoolAgePop = schoolPop;
  collegeAgePop = collegePop;

  // 6. school and college ratings
  {
    const double c13 = double(1.0f / 13.0f), c6 = double(1.0f / 6.0f), c12 = double(1.0f / 12.0f), c04 = double(0.4f);
    double os = cfg.optimalYearlySchoolEQ, oc = cfg.optimalYearlyCollegeEQ;
    double rs = os > 0 ? std::nearbyint(4.0 * (double(Gschool) * c13) / (os * c12) - c04) : 0;
    double rc = oc > 0 ? std::nearbyint(4.0 * (double(Gcollege) * c6) / (oc * c12) - c04) : 0;
    schoolRating = uint8_t(std::clamp(rs, 0.0, 4.0));
    collegeRating = uint8_t(std::clamp(rc, 0.0, 4.0));
  }

  // 7. strikes: health, school, college
  struct M {
    bool* on; uint32_t* months; uint8_t rating; uint8_t minRating; int funding; uint8_t minFunding;
    bool exists; uint32_t duration;
  } machines[3] = {
      {&healthStrike, &healthStrikeMonths, healthRating, cfg.minAcceptableHealthRating, in.healthFunding,
       cfg.minAcceptableHealthFunding, haveHospital, cfg.maxHealthStrikeDuration},
      {&schoolStrike, &schoolStrikeMonths, schoolRating, cfg.minAcceptableEducationRating, in.educationFunding,
       cfg.minAcceptableEducationFunding, haveSchool, cfg.maxEducationStrikeDuration},
      {&collegeStrike, &collegeStrikeMonths, collegeRating, cfg.minAcceptableEducationRating, in.educationFunding,
       cfg.minAcceptableEducationFunding, haveCollege, cfg.maxEducationStrikeDuration}};
  for (int i = 0; i < 3; i++) {
    M& m = machines[i];
    if (!*m.on) {
      uint32_t r = 0;
      if (m.rating < m.minRating && m.funding < int(m.minFunding) && m.exists) {
        int d = int(m.minFunding) - m.funding;
        r = uint32_t(d * d);
      }
      if (r != 0 && rng.rand(10000) < r) {
        *m.on = true;
        *m.months = 0;
        res.strikeStarted[i] = true;
      }
    } else {
      (*m.months)++;
      if (*m.months > m.duration || m.funding > 99 || !m.exists) {
        *m.on = false;
        res.strikeEnded[i] = true;
      }
    }
  }

  // 8. yearly ageing (January)
  if (january) {
    res.yearlyAgeing = true;
    const int As = oldestAge;
    for (int a = kAges - 1; a >= 1; a--) {
      share[a] = share[a - 1];
      if (a > As) { eq[a] = 0; le[a] = 0; }
      else {
        eq[a] = a < 27 ? eq[a - 1] : cfg.eqDecay * eq[a - 1];
        le[a] = le[a - 1];
      }
    }   // HQ is not shifted
    oldestAge = As + 1;
    float beta = birthTrend == 0 ? 0.75f : birthTrend == 1 ? 0.25f : 0.5f;
    float u = float(rng.nextReal());
    share[0] = share[1] / cfg.ageCohortDecay + (beta - u) * 0.001f;
    if (share[0] < cfg.minNewbornPopPct) { share[0] = cfg.minNewbornPopPct; birthTrend = 0; }
    else if (share[0] > cfg.maxNewbornPopPct) { share[0] = cfg.maxNewbornPopPct; birthTrend = 1; }
    else {
      float u2 = float(rng.nextReal());
      if (u2 < cfg.babyTrendChangeProb) {
        if (birthTrend == 0 || birthTrend == 1) birthTrend = 2;
        else birthTrend = float(rng.nextReal()) < 0.5f ? 0 : 1;
      }
    }
    le[0] = le[1];
    eq[0] = 0;
    S += share[0];   // S still includes the share that dropped off at age 89 (original quirk)
  }

  // 9. renormalise
  if (S != 1.0f) {
    int top = std::min(oldestAge, kAges - 1);
    float add = (1.0f - S) / float(oldestAge + 1);
    for (int a = 0; a <= top; a++) share[a] += add;
  }

  // 10. chart samples
  uint32_t popS = in.population;
  uint32_t eqS = in.population ? cityEQ() : 0;
  uint32_t leS = in.population ? cityLifeExpectancy() : 0;
  popChart.pushMonthly(popS); eqChart.pushMonthly(eqS); leChart.pushMonthly(leS);
  if (january) {
    popChart.pushYearly(popS); eqChart.pushYearly(eqS); leChart.pushYearly(leS);
    if (in.year % 10 == 0) {
      popChart.pushDecade(popS); eqChart.pushDecade(eqS); leChart.pushDecade(leS);
    }
  }
  return res;
}

// ---------------------------------------------------------------- persistence
void Population::finishLoadDerived() {
  oldestAge = 0;
  for (int a = kAges - 1; a >= 0; a--)
    if (share[a] > 0) { oldestAge = a; break; }
}

void Population::parseState(const uint8_t* p, size_t n) {
  if (n < kStateBytes) throw FormatError("residential state: short block");
  Reader r(p, n);
  popChart = Chart::parse(r.ptr(), r.remaining()); r.skip(Chart::kBytes);
  eqChart = Chart::parse(r.ptr(), r.remaining()); r.skip(Chart::kBytes);
  leChart = Chart::parse(r.ptr(), r.remaining()); r.skip(Chart::kBytes);
  popChart.autoScale = true; popChart.floorMax = 10000;
  schoolStrike = r.u8() != 0;
  healthStrike = r.u8() != 0;
  schoolStrikeMonths = r.u32();
  healthStrikeMonths = r.u32();
  schoolRating = r.u8();
  healthRating = r.u8();
  patients = r.u32();
  collegeStrike = r.u8() != 0;
  collegeStrikeMonths = r.u32();
  collegeRating = r.u8();
  schoolAgePop = r.u32();
  collegeAgePop = r.u32();
  schoolDesks = r.u32();
  collegeDesks = r.u32();
  hospitalBeds = r.u32();
  museumCapacity = r.u32();
  libraryCapacity = r.u32();
  unknown1BE = r.u8();
  birthTrend = r.u8();
  for (auto& v : eq) v = r.f32();
  for (auto& v : hq) v = r.f32();
  for (auto& v : share) v = r.f32();
  for (auto& v : le) v = r.f32();
  teachers = r.u32();
  professors = r.u32();
  doctors = r.u32();
  finishLoadDerived();
}

static void putF32(Bytes& b, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  put32(b, v);
}

Bytes Population::serializeState() const {
  Bytes b;
  b.reserve(kStateBytes);
  popChart.serialize(b);
  eqChart.serialize(b);
  leChart.serialize(b);
  b.push_back(schoolStrike); b.push_back(healthStrike);
  put32(b, schoolStrikeMonths); put32(b, healthStrikeMonths);
  b.push_back(schoolRating); b.push_back(healthRating);
  put32(b, patients);
  b.push_back(collegeStrike);
  put32(b, collegeStrikeMonths);
  b.push_back(collegeRating);
  put32(b, schoolAgePop); put32(b, collegeAgePop);
  put32(b, schoolDesks); put32(b, collegeDesks); put32(b, hospitalBeds);
  put32(b, museumCapacity); put32(b, libraryCapacity);
  b.push_back(unknown1BE); b.push_back(birthTrend);
  for (float v : eq) putF32(b, v);
  for (float v : hq) putF32(b, v);
  for (float v : share) putF32(b, v);
  for (float v : le) putF32(b, v);
  put32(b, teachers); put32(b, professors); put32(b, doctors);
  return b;
}

void Population::parseRecord(const uint8_t* p, size_t n) {
  Reader r(p, n);
  uint16_t ver = r.u16();
  uint8_t flags = r.u8();
  if (flags & 2) r.u8();   // flags2, ignored
  uint32_t sentinel = r.u32();
  if (sentinel != 0xDEADBEEF) throw FormatError("residential record: legacy header not supported");
  if (flags & 1) { uint32_t len = r.u32(); r.skip(len); }   // start marker
  uint32_t cnt = r.u32();
  std::vector<Bytes> vb;
  for (uint32_t i = 0; i < cnt; i++) vb.push_back(r.bytes(41));
  if (r.remaining() < kStateBytes) throw FormatError("residential record: short state");
  parseState(r.ptr(), kStateBytes);
  recordVersion = ver;
  valveBlocks = vb;
}

Bytes Population::serializeRecord() const {
  Bytes b;
  put16(b, recordVersion);
  b.push_back(0x02);   // flags: no markers, second flag byte present
  b.push_back(0x00);
  put32(b, 0xDEADBEEF);
  std::vector<Bytes> vb = valveBlocks;
  if (vb.empty()) {
    for (uint32_t id : {3000u, 3001u}) {
      Bytes v(41, 0);
      wr32(v.data(), id);
      v[0x1C] = 100;   // cap percent
      vb.push_back(v);
    }
  }
  put32(b, uint32_t(vb.size()));
  for (auto& v : vb) b.insert(b.end(), v.begin(), v.end());
  Bytes s = serializeState();
  b.insert(b.end(), s.begin(), s.end());
  return b;
}

}  // namespace m3k
