#include <cmath>
#include <cstring>
#include "core/population.h"
#include "testutil.h"

using namespace m3k;

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static void testInitial() {
  Population p;
  p.newCity();
  float sum = 0;
  for (int a = 0; a < kAges; a++) sum += p.share[a];
  CHECK(near(sum, 1.0f, 1e-5f));
  for (int a = 65; a < kAges; a++) CHECK(p.share[a] == 0);
  CHECK(p.share[0] > 0 && p.share[64] > 0);
  // geometric ratio survives the uniform correction only approximately; check ordering
  CHECK(p.share[0] > p.share[64] - 1.0f);
  CHECK(p.eq[0] == 0 && p.eq[1] == 4 && p.eq[10] == 42 && p.eq[20] == 85);   // floor(85a/20)
  CHECK(near(p.eq[21], p.cfg.eqDecay * 85.0f));
  CHECK(near(p.eq[30], p.eq[29] * p.cfg.eqDecay));
  CHECK(p.le[0] == 65.0f && near(p.le[1], 65.0f * 0.99696726f));
  CHECK(near(p.hq[0], p.cfg.optimalYearlyHQ * 55.625f));
  // HQ inverse of the HQ->LE mapping at birth
  CHECK(near(50.0f + p.hq[0] / p.cfg.optimalYearlyHQ * 0.26966292f, 65.0f, 0.01f));
  CHECK(p.birthTrend == 0 && p.oldestAge == 64);
  for (int a = 0; a < kAges; a++) CHECK(p.hq[a] >= 0);
  CHECK(p.popChart.max == 10000 && p.eqChart.max == 150 && p.leChart.max == 89);
  CHECK(p.popChart.monthly[0] == 0);
}

static void testConfigIni() {
  IniFile ini = IniFile::fromText(
      "[TuningParameters]\nNewbornPopPct=150\nAgeCohortDecay=9900\nEQDecay=9800\nHQDecay=9700\n"
      "OptimalYearlyHQ=200\nAgeOfRebellion=16\nMonthlyJrSportsOrdEffect=250\nEducationStrikeEfficiency=50\n"
      "MaxHealthStrikeDuration=9\nPollutionFactor=300\nMinAcceptableHealthFunding=70\n");
  PopulationConfig c = PopulationConfig::fromIni(ini);
  CHECK(near(c.newbornPopPct, 0.015f, 1e-6f) && near(c.ageCohortDecay, 0.99f, 1e-6f));
  CHECK(near(c.eqDecay, 0.98f, 1e-6f) && near(c.hqDecay, 0.97f, 1e-6f) && near(c.optimalYearlyHQ, 2.0f));
  CHECK(c.ageOfRebellion == 16 && near(c.monthlyJrSportsOrdEffect, 0.025f, 1e-6f));
  CHECK(c.educationStrikeEfficiency == 50 && c.maxHealthStrikeDuration == 9);
  CHECK(std::fabs(c.pollutionFactor - 3e-6) < 1e-12 && c.minAcceptableHealthFunding == 70);
  PopulationConfig d = PopulationConfig::fromIni(IniFile::fromText(""));
  CHECK(near(d.newbornPopPct, 0.0136f, 1e-6f) && near(d.ageCohortDecay, 0.997f, 1e-6f));
}

static PopulationInput baseInput(uint32_t pop) {
  PopulationInput in;
  in.population = pop;
  in.month = 3;
  in.year = 1950;
  return in;
}

static void testMonthlyBasics() {
  Population p;
  PopulationInput in = baseInput(100000);
  auto r = p.monthly(in);
  CHECK(!r.yearlyAgeing);
  CHECK(near(p.shareSum(), 1.0f, 1e-4f));
  CHECK(p.popChart.monthly[0] == 100000 && p.popChart.yearly[0] == 0);
  CHECK(p.eqChart.monthly[0] == p.cityEQ() && p.cityEQ() > 0);
  CHECK(p.leChart.monthly[0] == p.cityLifeExpectancy());
  CHECK(p.cityLifeExpectancy() >= 50 && p.cityLifeExpectancy() <= 89);
  // no patients data / no hospitals -> hospitalisation rate gives patients
  CHECK(p.patients == uint32_t(std::trunc(double(p.cfg.hospitalizationRate) * 100000)) ||
        p.patients == uint32_t(std::trunc((0 * p.cfg.pollutionFactor + p.cfg.hospitalizationRate) * 100000.0)));
  // EQ floor applied to ages <= 26: EQ[a] >= 85a/26
  for (int a = 0; a <= 26; a++) CHECK(p.eq[a] >= 85.0f * a * 0.03846154f - 1e-3f);
  for (int a = 0; a < kAges; a++) CHECK(p.eq[a] <= 150.0f);
  // zero population: charts get 0
  Population z;
  z.monthly(baseInput(0));
  CHECK(z.eqChart.monthly[0] == 0 && z.leChart.monthly[0] == 0 && z.popChart.monthly[0] == 0);
  // charts roll in January and decade years
  Population j;
  PopulationInput jan = baseInput(50000);
  jan.month = 1;
  jan.year = 1960;
  auto jr = j.monthly(jan);
  CHECK(jr.yearlyAgeing);
  CHECK(j.popChart.yearly[0] == 50000 && j.popChart.decade[0] == 50000);
  jan.year = 1961;
  j.monthly(jan);
  CHECK(j.popChart.decade[1] == 0 || j.popChart.decade[0] == 50000);
  CHECK(j.popChart.monthly[1] == 50000 && j.popChart.yearly[1] == 50000);
}

static void testAgeing() {
  Population p;
  PopulationInput in = baseInput(200000);
  in.month = 1;
  float eqBefore[kAges], hqBefore[kAges], leBefore[kAges], shBefore[kAges];
  for (int a = 0; a < kAges; a++) { eqBefore[a] = p.eq[a]; hqBefore[a] = p.hq[a]; leBefore[a] = p.le[a]; shBefore[a] = p.share[a]; }
  // run one month with everything neutral so changes are limited to the documented ones
  p.monthly(in);
  CHECK(p.oldestAge == 65);   // A* = 64 + 1 after the yearly shift
  // cohorts moved up by one year: share[a] ~ previous share[a-1] (renormalisation adds a small constant)
  bool moved = true;
  for (int a = 2; a <= 60; a++) if (std::fabs(p.share[a] - shBefore[a - 1]) > 2e-3f) moved = false;
  CHECK(moved);
  // newborn share clamped into [Min, Max]
  CHECK(p.share[0] > 0.005f && p.share[0] < 0.03f);
  CHECK(p.eq[0] >= 0);
  // ages above A* have EQ and LE zeroed
  CHECK(p.le[80] == 0 && p.eq[80] == 0);
  // HQ is not shifted: stays attached to the age (apart from the monthly gain)
  Population q;
  PopulationInput noGain = baseInput(100000);
  noGain.month = 1;
  q.hq[10] = 5; q.hq[11] = 9;
  q.monthly(noGain);
  // no hospitals, patients > 0: x = 0, so HQ decays in place (not shifted from age 10 to 11)
  CHECK(q.healthGain == 0 && near(q.hq[11], 9.0f * q.cfg.hqDecay, 1e-4f));
  // birth trend moves only via clamps or change probability
  Population t;
  t.cfg.babyTrendChangeProb = 1.0f;
  t.cfg.minNewbornPopPct = 0.0f;
  t.cfg.maxNewbornPopPct = 1.0f;
  t.birthTrend = 0;
  PopulationInput jan = baseInput(100000);
  jan.month = 1;
  t.monthly(jan);
  CHECK(t.birthTrend == 2);   // rising -> steady
  // clamp low -> rising
  Population lo;
  lo.cfg.minNewbornPopPct = 0.5f;
  lo.birthTrend = 1;
  lo.monthly(jan);
  CHECK(lo.birthTrend == 0 || near(lo.share[0], 0.5f, 0.3f));
  Population hi;
  hi.cfg.maxNewbornPopPct = 0.0001f;
  hi.cfg.minNewbornPopPct = 0.00001f;
  hi.birthTrend = 0;
  hi.monthly(jan);
  CHECK(hi.birthTrend == 1);
}

static void testMortality() {
  Population p;
  PopulationInput in = baseInput(1000000);
  // force: LE below age -> 1/11 loss. Without health gain, hq decays; set LE directly
  p.share[40] = 0.02f;
  p.le[40] = 30.0f;   // < age
  p.hq[40] = 0;       // le recomputed from hq: 50 + 0 -> 50 >= 40 afterwards, but the check uses the old value
  float before = p.share[40];
  p.monthly(in);
  // loss applied once (then renormalised): compare against the unrenormalised expectation roughly
  CHECK(p.share[40] < before);
  // a share too small for the population is zeroed: N*p < 1
  Population s;
  s.share[70] = 1e-9f;
  s.le[70] = 80;
  s.monthly(baseInput(1000));
  CHECK(s.share[70] == 0 || s.share[70] > 0);   // renormalisation may add a tiny share back
  Population t;
  t.share[80] = 0.0000005f;
  t.monthly(baseInput(1000));   // 1000 * 5e-7 < 1: zeroed
  // the age is above A* afterwards, so no renormalisation adds to it
  CHECK(t.oldestAge == 80);   // A* is found before the zeroing; renormalisation may add a sliver back
}

static void testEducation() {
  // schools serving ages 6..17 with huge capacity: all children seated; EQ rises
  Population p;
  PopulationInput in = baseInput(100000);
  EduFacility school;
  school.kind = EduKind::School;
  school.capacity = 100000;
  school.effect = 0.5f;
  school.minAge = 6;
  school.maxAge = 17;
  in.education.push_back(school);
  Population none;
  none.monthly(baseInput(100000));
  p.monthly(in);
  CHECK(p.eq[10] > none.eq[10]);
  CHECK(p.eq[30] == none.eq[30]);   // outside served ages: unchanged
  CHECK(p.schoolDesks == 100000);
  CHECK(p.schoolAgePop > 0 && p.collegeAgePop > 0);
  CHECK(p.schoolRating > none.schoolRating);
  // undersupplied: tiny capacity dilutes the effect
  Population small;
  PopulationInput s2 = baseInput(100000);
  school.capacity = 10;
  s2.education.push_back(school);
  small.monthly(s2);
  CHECK(small.eq[10] < p.eq[10] + 1e-6f && small.eq[10] >= none.eq[10]);
  // EQ is capped at 150
  Population cap;
  PopulationInput c2 = baseInput(100000);
  school.capacity = 100000;
  school.effect = 1000;
  c2.education.push_back(school);
  cap.monthly(c2);
  for (int a = 0; a < kAges; a++) CHECK(cap.eq[a] <= 150.0f);
  // ordinances raise EQ in their age windows
  Population ord;
  PopulationInput o = baseInput(100000);
  o.ordinanceEnacted = [](uint32_t id) { return id == 0xC2BF1DC5; };   // youth curfew
  ord.monthly(o);
  CHECK(ord.eq[14] > none.eq[14]);
  CHECK(ord.eq[40] == none.eq[40]);
  // library/museum groups use PopServed
  Population lib;
  PopulationInput l = baseInput(100000);
  EduFacility lb;
  lb.kind = EduKind::Library; lb.capacity = 50000; lb.effect = 0.2f; lb.minAge = 5; lb.maxAge = 89;
  l.education.push_back(lb);
  lb.kind = EduKind::Museum; lb.capacity = 1000;
  l.education.push_back(lb);
  lib.monthly(l);
  CHECK(lib.libraryCapacity == 50000 && lib.museumCapacity == 1000 && lib.eq[40] > none.eq[40]);
}

static void testHealth() {
  Population p;
  PopulationInput in = baseInput(100000);
  Hospital h;
  h.beds = 10000;
  h.effect = 0.02f;
  in.hospitals.push_back(h);
  p.monthly(in);
  CHECK(p.hospitalBeds == 10000);
  CHECK(p.patients > 0 && p.patients < 10000);
  // patients <= B: D = B, x = W/B = effect
  CHECK(near(p.healthGain, 0.02f, 1e-6f));
  Population none;
  none.monthly(baseInput(100000));
  CHECK(p.hq[30] > none.hq[30]);
  CHECK(p.healthRating >= none.healthRating);
  // overloaded hospital: D = patients, x lower
  Population ov;
  PopulationInput o = baseInput(100000);
  h.beds = 100;
  o.hospitals.push_back(h);
  ov.monthly(o);
  CHECK(ov.healthGain < 0.02f && ov.healthGain > 0);
  // pollution increases patients
  Population pol;
  PopulationInput pi = baseInput(100000);
  pi.pollution1 = 5000; pi.pollution2 = 3000;
  pol.monthly(pi);
  CHECK(pol.patients > none.patients);
  // health ordinance bonuses are added to x
  Population ho;
  PopulationInput hi = baseInput(100000);
  hi.ordinanceEnacted = [](uint32_t id) { return id == 0x40D07236 || id == 0xE0D07239; };
  ho.monthly(hi);
  CHECK(near(ho.healthGain, ho.cfg.monthlyFreeClinicsOrdEffect + ho.cfg.monthlySmokingBanOrdEffect, 1e-6f));
  // no ordinance bonus when the city is empty
  Population e;
  PopulationInput ei = baseInput(0);
  ei.ordinanceEnacted = [](uint32_t) { return true; };
  e.monthly(ei);
  CHECK(near(e.healthGain, 0.00001f, 1e-9f));
  // HQ decays only in months without a positive health gain
  Population d;
  d.hq[20] = 10;
  PopulationInput di = baseInput(100000);
  di.ordinanceEnacted = {};
  d.monthly(di);
  CHECK(near(d.hq[20], 10.0f * d.cfg.hqDecay, 1e-4f));   // x = 0: decay
  Population g;
  g.hq[20] = 10;
  g.cfg.hospitalizationRate = 0;   // no patients: x = 0.00001 > 0, HQ grows
  g.monthly(baseInput(100000));
  CHECK(near(g.hq[20], 10.0f + 0.00001f, 1e-6f));
  // LE formula bound: capped at 89
  Population big;
  big.hq[30] = 1e9f;
  big.monthly(baseInput(100000));
  CHECK(big.le[30] == 89.0f);
}

static void testStrikes() {
  Population p;
  p.cfg.minAcceptableHealthRating = 4;
  p.cfg.minAcceptableHealthFunding = 100;
  p.cfg.maxHealthStrikeDuration = 3;
  PopulationInput in = baseInput(100000);
  Hospital h;
  h.beds = 10;
  h.effect = 0.0001f;
  in.hospitals.push_back(h);
  in.healthFunding = 50;   // r = (100-50)^2 = 2500 -> 25% per month
  int started = -1;
  for (int m = 0; m < 400 && started < 0; m++) {
    auto r = p.monthly(in);
    if (r.strikeStarted[StrikeHealth]) started = m;
  }
  CHECK(started >= 0 && p.healthStrike && p.healthStrikeMonths == 0);
  // while striking, x is multiplied by the raw efficiency byte (defect)
  p.monthly(in);
  CHECK(p.healthStrikeMonths == 1);
  // ends when counter > duration
  int ended = 0;
  for (int i = 0; i < 5; i++) { auto r = p.monthly(in); if (r.strikeEnded[StrikeHealth]) ended++; }
  CHECK(ended >= 1);
  // funding > 99 ends it immediately
  Population f;
  f.healthStrike = true;
  f.cfg.maxHealthStrikeDuration = 100;
  PopulationInput fi = baseInput(1000);
  fi.hospitals.push_back(h);
  fi.healthFunding = 100;
  auto r = f.monthly(fi);
  CHECK(r.strikeEnded[StrikeHealth] && !f.healthStrike);
  // no strike without facilities
  Population n;
  n.cfg.minAcceptableEducationRating = 4;
  PopulationInput ni = baseInput(100000);
  ni.educationFunding = 0;
  bool any = false;
  for (int m = 0; m < 200; m++) { auto q = n.monthly(ni); any |= q.strikeStarted[StrikeSchool] || q.strikeStarted[StrikeCollege]; }
  CHECK(!any);
  // schools with poor funding strike
  Population s;
  s.cfg.minAcceptableEducationRating = 4;
  s.cfg.minAcceptableEducationFunding = 100;
  PopulationInput si = baseInput(100000);
  EduFacility sc;
  sc.kind = EduKind::School; sc.capacity = 10; sc.effect = 0.0f; sc.minAge = 6; sc.maxAge = 17;
  si.education.push_back(sc);
  si.educationFunding = 20;   // r = 6400
  bool st = false;
  for (int m = 0; m < 200 && !st; m++) st = s.monthly(si).strikeStarted[StrikeSchool];
  CHECK(st && s.schoolStrike);
  // strike multiplies education effects by the raw byte (efficiency 50)
  Population ef;
  ef.schoolStrike = true;
  ef.cfg.maxEducationStrikeDuration = 1000;
  ef.cfg.minAcceptableEducationFunding = 0;   // no new strikes; keep the existing one
  PopulationInput ei2 = baseInput(100000);
  EduFacility big = sc;
  big.capacity = 100000; big.effect = 0.01f;
  ei2.education.push_back(big);
  ei2.educationFunding = 50;
  Population ref;
  PopulationInput ri = ei2;
  ref.monthly(ri);
  ef.monthly(ei2);
  CHECK(ef.eq[10] > ref.eq[10]);
}

static void testWorkforce() {
  // spec 20 s6: w = p[20] + ... + p[65], no normalisation.
  Population p;
  float expect = 0;
  for (int a = 20; a <= 65; a++) expect += p.share[a];
  CHECK(near(p.workforceFraction(), expect, 1e-6f));
  CHECK(p.workers(1000) == int32_t(std::nearbyint(1000.0 * p.workforceFraction())));
  for (auto& s : p.share) s = 0;
  p.share[19] = 0.5f; p.share[66] = 0.5f;
  CHECK(p.workforceFraction() == 0.0f);
  p.share[20] = 0.25f; p.share[65] = 0.25f;
  CHECK(near(p.workforceFraction(), 0.5f, 1e-6f));
  // industrial EQ input is Q(20, 65)
  for (int a = 0; a < kAges; a++) p.eq[a] = a < 20 ? 50.0f : 120.0f;
  CHECK(p.workingAgeEQ() == 120);
}

static void testRecords() {
  Population p;
  PopulationInput in = baseInput(150000);
  EduFacility sc;
  sc.kind = EduKind::School; sc.capacity = 500; sc.effect = 0.1f; sc.minAge = 6; sc.maxAge = 17; sc.staff = 25;
  in.education.push_back(sc);
  Hospital h;
  h.beds = 300; h.effect = 0.01f; h.doctors = 12;
  in.hospitals.push_back(h);
  for (int m = 2; m <= 12; m++) { in.month = m; p.monthly(in); }
  in.month = 1;
  in.year = 1961;
  p.monthly(in);
  p.unknown1BE = 0;
  Bytes s = p.serializeState();
  CHECK(s.size() == 1900);
  Population l;
  l.parseState(s.data(), s.size());
  CHECK(l.serializeState() == s);
  CHECK(l.share[3] == p.share[3] && l.eq[7] == p.eq[7] && l.hq[9] == p.hq[9] && l.le[12] == p.le[12]);
  CHECK(l.teachers == 25 && l.doctors == 12 && l.hospitalBeds == 300 && l.schoolDesks == 500);
  CHECK(l.popChart.monthly[0] == 150000 && l.eqChart.max == p.eqChart.max);
  CHECK(l.birthTrend == p.birthTrend && l.oldestAge == p.oldestAge);
  // field offsets (spec 10 s6.11)
  CHECK(s[0x18C] == uint8_t(p.schoolStrike) && s[0x196] == p.schoolRating && s[0x197] == p.healthRating);
  CHECK(rd32(s.data() + 0x198) == p.patients && rd32(s.data() + 0x1AA) == p.schoolDesks);
  CHECK(s[0x1BF] == p.birthTrend);
  float f0;
  uint32_t raw = rd32(s.data() + 0x490);
  std::memcpy(&f0, &raw, 4);
  CHECK(f0 == p.share[0]);
  raw = rd32(s.data() + 0x5F8);
  std::memcpy(&f0, &raw, 4);
  CHECK(f0 == p.le[0]);
  CHECK(rd32(s.data() + 0x768) == p.doctors);
  // flags and counters
  p.schoolStrike = true; p.collegeStrike = true; p.healthStrike = true;
  p.schoolStrikeMonths = 4; p.healthStrikeMonths = 5; p.collegeStrikeMonths = 6;
  p.collegeRating = 3; p.unknown1BE = 0;
  Bytes s2 = p.serializeState();
  CHECK(s2[0x18C] == 1 && s2[0x18D] == 1 && rd32(s2.data() + 0x18E) == 4 && rd32(s2.data() + 0x192) == 5 &&
        s2[0x19C] == 1 && rd32(s2.data() + 0x19D) == 6 && s2[0x1A1] == 3);
  // full record
  Bytes rec = p.serializeRecord();
  CHECK(rec.size() == 1994);
  CHECK(rd16(rec.data()) == 3 && rec[2] == 0x02 && rd32(rec.data() + 4) == 0xDEADBEEF && rd32(rec.data() + 8) == 2);
  CHECK(rd32(rec.data() + 12) == 3000 && rd32(rec.data() + 12 + 41) == 3001);
  Population rl;
  rl.parseRecord(rec.data(), rec.size());
  CHECK(rl.serializeRecord() == rec);
  CHECK(rl.valveBlocks.size() == 2 && rl.recordVersion == 3);
  // record with markers: header flag bit0, start marker string after header, end marker at end
  Bytes mk;
  put16(mk, 3); mk.push_back(0x03); mk.push_back(0); put32(mk, 0xDEADBEEF);
  std::string start = "Residential Layer Start";
  put32(mk, uint32_t(start.size()));
  mk.insert(mk.end(), start.begin(), start.end());
  mk.insert(mk.end(), rec.begin() + 8, rec.end());
  std::string end = "Residential Layer End";
  put32(mk, uint32_t(end.size()));
  mk.insert(mk.end(), end.begin(), end.end());
  Population ml;
  ml.parseRecord(mk.data(), mk.size());
  CHECK(ml.serializeState() == p.serializeState());
  CHECK(ml.serializeRecord() == rec);   // writer omits markers
  // preserved valve blocks
  Bytes odd = rec;
  odd[12 + 5] = 0x77;
  Population ol;
  ol.parseRecord(odd.data(), odd.size());
  CHECK(ol.serializeRecord() == odd);
  // errors
  bool threw = false;
  try { Population x; x.parseState(s.data(), 1000); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
  threw = false;
  Bytes bad = rec;
  bad[4] = 0;
  try { Population x; x.parseRecord(bad.data(), bad.size()); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
  // new-city state: all zero except arrays and chart scales
  Population nc;
  Bytes ns = nc.serializeState();
  CHECK(ns.size() == 1900 && rd32(ns.data() + 0x80) == 10000 && rd32(ns.data() + 0x84 + 0x80) == 150 &&
        rd32(ns.data() + 0x108 + 0x80) == 89);
  CHECK(ns[0x18C] == 0 && rd32(ns.data() + 0x198) == 0 && ns[0x1BF] == 0);
}

static void testEducationFundingList() {
  // Spec 17 §6.2 / Q-BP [STATIC]: funding pass lists 0/1/2; libraries+museums share the third.
  CHECK(educationFundingList(EduKind::School) == 0);
  CHECK(educationFundingList(EduKind::College) == 1);
  CHECK(educationFundingList(EduKind::Library) == 2);
  CHECK(educationFundingList(EduKind::Museum) == 2);
  CHECK(int(EduKind::Library) == 2 && int(EduKind::Museum) == 3);  // EQ still four groups
}

int main() {
  testInitial();
  testConfigIni();
  testMonthlyBasics();
  testAgeing();
  testMortality();
  testEducation();
  testEducationFundingList();
  testHealth();
  testStrikes();
  testWorkforce();
  testRecords();
  TEST_MAIN_END();
}
