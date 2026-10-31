// Disaster subsystem implementation (spec/21-disasters.md).
#include "core/disasters.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace m3k {

// ---------------------------------------------------------------------------------------------
// IDs
// ---------------------------------------------------------------------------------------------
const std::vector<DisasterType>& allDisasterTypes() {
  static const std::vector<DisasterType> v = {
      DisasterType::Fire,       DisasterType::Tornado, DisasterType::Earthquake, DisasterType::Riot,
      DisasterType::UFO,        DisasterType::ToxicCloud, DisasterType::Whirlpool, DisasterType::Locusts,
      DisasterType::SpaceJunk,  DisasterType::Parade};
  return v;
}
uint32_t disasterClassId(DisasterType t) {
  switch (t) {
    case DisasterType::Fire: return 0x428FD431;
    case DisasterType::Tornado: return 0x62A34670;
    case DisasterType::Earthquake: return 0x22FE8D32;
    case DisasterType::Riot: return 0xE2FE8D38;
    case DisasterType::UFO: return 0x22FE8D3E;
    case DisasterType::ToxicCloud: return 0xC2FE8D43;
    case DisasterType::Whirlpool: return 0x52FE8D50;
    case DisasterType::Locusts: return 0x52FE8D51;
    case DisasterType::SpaceJunk: return 0x52FE8D52;
    case DisasterType::Parade: return 0x84C92CBE;
  }
  return 0;
}
const char* disasterName(DisasterType t) {
  switch (t) {
    case DisasterType::Fire: return "Fire";
    case DisasterType::Tornado: return "Tornado";
    case DisasterType::Earthquake: return "Earthquake";
    case DisasterType::Riot: return "Riot";
    case DisasterType::UFO: return "UFO";
    case DisasterType::ToxicCloud: return "ToxicCloud";
    case DisasterType::Whirlpool: return "Whirlpool";
    case DisasterType::Locusts: return "Locusts";
    case DisasterType::SpaceJunk: return "SpaceJunk";
    case DisasterType::Parade: return "Parade";
  }
  return "?";
}
uint32_t disasterOccupantKeyFire() { return 0x32CC; }
uint32_t disasterOccupantKeyPolice() { return 0x32CB; }

// ---------------------------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------------------------
static int parseIntLoose(const std::string& s, size_t* endPos = nullptr) {
  const char* p = s.c_str();
  char* e = nullptr;
  long v = std::strtol(p, &e, 0);
  if (endPos) *endPos = size_t(e - p);
  return int(v);
}

namespace {
struct IdSlot { uint32_t id; int* p; };
std::vector<IdSlot> idSlots(DisasterTunables& t) {
  std::vector<IdSlot> v = {
      {0x047DF002, &t.fireIntervalMin}, {0x647DF00F, &t.fireIntervalMax}, {0xC47DF01A, &t.stageMin},
      {0xA47DF024, &t.stageMax}, {0x647DF02B, &t.minFlam}, {0x647DF032, &t.spreadFlam},
      {0x247DF038, &t.spreadLo[0]}, {0xA47DF03E, &t.spreadHi[0]}, {0x047DF044, &t.spreadLo[1]},
      {0xE47DF04A, &t.spreadHi[1]}, {0x047DF051, &t.spreadLo[2]}, {0xA47DF05A, &t.spreadHi[2]},
      {0x047DF061, &t.groupBias},
      {0xE48381BC, &t.tornadoIntervalMin}, {0x048381C8, &t.tornadoIntervalMax},
      {0xA48381D2, &t.tornadoSpeedMin}, {0x84838207, &t.tornadoSpeedMax},
      {0x4483820F, &t.tornadoDestroyChance[0]}, {0x84838216, &t.tornadoDestroyChance[1]},
      {0x8483821D, &t.tornadoDestroyChance[2]}, {0x24838224, &t.tornadoLifeMin}, {0x0483822A, &t.tornadoLifeMax},
      {0x44838230, &t.tornadoWarn[0]}, {0xC4838239, &t.tornadoWarn[1]}, {0x0483823F, &t.tornadoWarn[2]},
      {0x6483824B, &t.tornadoWarn[3]}, {0xE4838253, &t.tornadoWarn[4]},
      {0xE423750F, &t.quakeMax}, {0xE4237518, &t.quakeMinGapMonths}, {0x0423752B, &t.quakeMinMag},
      {0x64237533, &t.quakeMaxMag}, {0x6423753A, &t.quakeChance},
      {0x64237541, &t.quakeDestroy[0]}, {0x44237548, &t.quakeDestroy[1]}, {0x4423754E, &t.quakeDestroy[2]},
      {0x44237555, &t.quakeIgnite[0]}, {0x6423755E, &t.quakeIgnite[1]}, {0xA4237564, &t.quakeIgnite[2]},
      {0x2423756A, &t.quakeHalfWidth[0]}, {0x24237579, &t.quakeHalfWidth[1]}, {0x6423757F, &t.quakeHalfWidth[2]},
      {0xA423758D, &t.quakeShake[0]}, {0x24237593, &t.quakeShake[1]}, {0x442375AA, &t.quakeShake[2]},
      {0x442375AF, &t.quakeShakeParam[0]}, {0x842375BE, &t.quakeShakeParam[1]}, {0xC42375C5, &t.quakeShakeParam[2]},
      {0xE42375CA, &t.quakeFore[0]}, {0x442375D3, &t.quakeFore[1]}, {0xC42375D9, &t.quakeFore[2]},
      {0x842375E4, &t.quakeForeParam[0]}, {0xE42375E9, &t.quakeForeParam[1]}, {0x842375EF, &t.quakeForeParam[2]},
      {0x442375F6, &t.quakeT[0]}, {0x842375FB, &t.quakeT[1]}, {0xC4237602, &t.quakeT[2]}, {0x04237608, &t.quakeT[3]},
      {0xC4237621, &t.quakeScarpP[0]}, {0x64237626, &t.quakeScarpP[1]}, {0x6423762B, &t.quakeScarpP[2]},
      {0x24295C2C, &t.quakeScarpQ[0]}, {0x44237637, &t.quakeScarpQ[1]}, {0x4423763D, &t.quakeScarpQ[2]},
      {0x6484A25D, &t.riotUnrestThreshold}, {0x2484A264, &t.riotIntervalMin}, {0x6484A26A, &t.riotIntervalMax},
      {0x4484A270, &t.riotSiteCoverageMax}, {0x4484A278, &t.riotSearchRadius}, {0xE484A27D, &t.riotDurMax},
      {0xA484A283, &t.riotDurMin}, {0x2484A289, &t.riotCrowdA}, {0x2484A290, &t.riotCrowdB},
      {0x2484A29E, &t.riotWarnDelay}, {0x2484A2A5, &t.riotBrewTime}, {0x4484A2AC, &t.riotGrowth},
      {0x24860901, &t.whirlMax}, {0x24860902, &t.whirlMinGapMonths}, {0x24860904, &t.whirlMagMin},
      {0x24860905, &t.whirlMagMax}, {0x24860906, &t.whirlChance}, {0x24860907, &t.whirlRadius},
      {0x2486090E, &t.whirlT[0]}, {0x2486090F, &t.whirlT[1]}, {0x24860910, &t.whirlT[2]}, {0x24860911, &t.whirlT[3]},
      {0x24860913, &t.whirlDuration},
      {0x44860901, &t.junkMax}, {0x44860902, &t.junkMinGapMonths}, {0x44860906, &t.junkChance},
      {0x4486090E, &t.junkT[0]}, {0x4486090F, &t.junkT[1]}, {0x44860910, &t.junkT[2]}, {0x44860911, &t.junkT[3]},
      {0x44860912, &t.junkT[4]}, {0x44860913, &t.junkDuration}, {0x44860914, &t.junkB}, {0x44860915, &t.junkA},
      {0x44860916, &t.junkCraterDecal}, {0x44860917, &t.junkMaxHeightDiff}, {0x44860918, &t.junkFirstYear},
  };
  return v;
}
struct NameSlot { const char* section; const char* key; int* p; };
std::vector<NameSlot> nameSlots(DisasterTunables& t) {
  return {
      {"MiscDisasterTunables", "MaxEmergencyDuration", &t.maxEmergencyDuration},
      {"MiscDisasterTunables", "MinDisasterCostToGetRelief", &t.minDisasterCostToGetRelief},
      {"MiscDisasterTunables", "ReliefAsPercentageOfCostWithoutEW", &t.reliefPctWithoutEW},
      {"MiscDisasterTunables", "ReliefAsPercentageOfCostWithEW", &t.reliefPctWithEW},
      {"ToxicCloudDisasterTunables", "MaxToxicCloudInstances", &t.toxMaxInstances},
      {"ToxicCloudDisasterTunables", "MinCloudsPerInstance", &t.toxMinClouds},
      {"ToxicCloudDisasterTunables", "MaxCloudsPerInstance", &t.toxMaxClouds},
      {"ToxicCloudDisasterTunables", "MinToxicCloudSpeed", &t.toxMinSpeed},
      {"ToxicCloudDisasterTunables", "MaxToxicCloudSpeed", &t.toxMaxSpeed},
      {"ToxicCloudDisasterTunables", "TimeBeforeFirstPremonition", &t.toxPrem1},
      {"ToxicCloudDisasterTunables", "TimeBeforeSecondPremonition", &t.toxPrem2},
      {"ToxicCloudDisasterTunables", "TimeBeforeThirdPremonition", &t.toxPrem3},
      {"ToxicCloudDisasterTunables", "TimeBeforeToxicCloudVisible", &t.toxVisible},
      {"ToxicCloudDisasterTunables", "MinHeightAboveTerrain", &t.toxMinHeight},
      {"ToxicCloudDisasterTunables", "MaxHeightAboveTerrain", &t.toxMaxHeight},
      {"ToxicCloudDisasterTunables", "MinCloudDuration", &t.toxMinDuration},
      {"ToxicCloudDisasterTunables", "MaxCloudDuration", &t.toxMaxDuration},
      {"ToxicCloudDisasterTunables", "AverageDurationBetweenOccurrences", &t.toxAvgMonthsBetween},
      {"ToxicCloudDisasterTunables", "WeightForPollution", &t.toxWeightPollution},
      {"ToxicCloudDisasterTunables", "WeightForCleanIndustryOrdinance", &t.toxWeightCleanInd},
      {"ToxicCloudDisasterTunables", "WeightForIndPollutantFeeOrdinance", &t.toxWeightIndFee},
      {"ToxicCloudDisasterTunables", "WeightForWasteDisposalOrdinance", &t.toxWeightWaste},
      {"ToxicCloudDisasterTunables", "WeightForEachToxicWastePlant", &t.toxWeightPlant},
      {"ToxicCloudDisasterTunables", "ThresholdScoreForToxicCloud", &t.toxThreshold},
      {"ToxicCloudDisasterTunables", "ScorePerLevelIncrement", &t.toxScorePerLevel},
      {"ToxicCloudDisasterTunables", "MinTimeForRainStart", &t.toxMinRainStart},
      {"ToxicCloudDisasterTunables", "MaxTimeForRainStart", &t.toxMaxRainStart},
      {"ToxicCloudDisasterTunables", "MinRainDuration", &t.toxMinRain},
      {"ToxicCloudDisasterTunables", "MaxRainDuration", &t.toxMaxRain},
      {"ToxicCloudDisasterTunables", "MinGapBetweenRains", &t.toxMinGap},
      {"ToxicCloudDisasterTunables", "AbandonRadiusIncrementPerLevel", &t.toxAbandonRadiusInc},
      {"ToxicCloudDisasterTunables", "DestructionRadiusIncrementPerLevel", &t.toxDestroyRadiusInc},
      {"ToxicCloudDisasterTunables", "DestructionLikelihood", &t.toxDestroyLikelihood},
      {"ToxicCloudDisasterTunables", "AbandonLikelihood", &t.toxAbandonLikelihood},
      {"UFODisasterTunables", "MaxUFOInstances", &t.ufoMaxInstances},
      {"UFODisasterTunables", "MinMonthsBetweenUFOAttacks", &t.ufoMinMonths},
      {"UFODisasterTunables", "MaxMonthsBetweenUFOAttacks", &t.ufoMaxMonths},
      {"UFODisasterTunables", "PercentageChanceOfUFOAttackEachMonthTimes100", &t.ufoChance100},
      {"UFODisasterTunables", "TimeBeforeFirstPremonitionMessage", &t.ufoPrem1},
      {"UFODisasterTunables", "TimeBeforeSecondPremonitionMessage", &t.ufoPrem2},
      {"UFODisasterTunables", "TimeBeforeThirdPremonitionMessage", &t.ufoPrem3},
      {"UFODisasterTunables", "TimeBeforeUFOAttack", &t.ufoAttack},
      {"UFODisasterTunables", "TimeBeforeUFOAttackEnd", &t.ufoAttackEnd},
      {"UFODisasterTunables", "MaxUFOsPerInstance", &t.ufoMaxPerInstance},
      {"UFODisasterTunables", "MaxUFOsPerInstanceCheatSwarm", &t.ufoMaxPerInstanceCheat},
      {"UFODisasterTunables", "PercentageChanceOfUFOExplosionOnExitTimes100", &t.ufoExplodeOnExit100},
      {"UFODisasterTunables", "MaxAbductionsMadePerAttack", &t.ufoMaxAbductions},
      {"UFODisasterTunables", "MaxCropCirclesMadePerAttack", &t.ufoMaxCropCircles},
      {"UFODisasterTunables", "MaxLandmarkBuildingsDestroyedPerAttack", &t.ufoMaxLandmark},
      {"UFODisasterTunables", "MaxSpaceportBuildingsDestroyedPerAttack", &t.ufoMaxSpaceport},
      {"UFODisasterTunables", "MaxLowTechBuildingsDestroyedPerAttack", &t.ufoMaxLowTech},
      {"UFODisasterTunables", "MaxHighTechBuildingsDestroyedPerAttack", &t.ufoMaxHighTech},
      {"UFODisasterTunables", "MaxPowerBuildingsDestroyedPerAttack", &t.ufoMaxPower},
      {"UFODisasterTunables", "MaxTotalBuildingsToDestroy", &t.ufoMaxTotal},
      {"UFODisasterTunables", "YearOfFirstPossibleVisitationAppearance", &t.ufoYearVisit},
      {"UFODisasterTunables", "YearOfFirstPossibleAbductionAppearance", &t.ufoYearAbduct},
      {"UFODisasterTunables", "YearOfFirstPossibleCropCircleAppearance", &t.ufoYearCrop},
      {"UFODisasterTunables", "YearOfFirstPossibleAttackAppearance", &t.ufoYearAttack},
      {"UFODisasterTunables", "PercentageOfBuildingsDestroyedByUFOBombs", &t.ufoPctDestroyed},
      {"UFODisasterTunables", "PercentageOfBuildingsCatchFireByUFOBombs", &t.ufoPctFire},
      {"LocustDisasterTunables", "MaxLocustInstances", &t.locMaxInstances},
      {"LocustDisasterTunables", "MinMonthsBetweenLocustAttacks", &t.locMinMonths},
      {"LocustDisasterTunables", "MaxMonthsBetweenLocustAttacks", &t.locMaxMonths},
      {"LocustDisasterTunables", "PercentageChanceOfLocustAttackEachYearTimes100", &t.locChance100},
      {"LocustDisasterTunables", "TimeBeforeFirstPremonitionMessage", &t.locPrem1},
      {"LocustDisasterTunables", "TimeBeforeSecondPremonitionMessage", &t.locPrem2},
      {"LocustDisasterTunables", "TimeBeforeLocustAttack", &t.locAttack},
      {"LocustDisasterTunables", "MinLocustSwarmsPerInstance", &t.locMinSwarms},
      {"LocustDisasterTunables", "MaxLocustSwarmsPerInstance", &t.locMaxSwarms},
      {"LocustDisasterTunables", "SwarmToCropRatio", &t.locSwarmToCrop},
      {"LocustDisasterTunables", "MaxNumCropsToAttack", &t.locMaxCrops},
      {"LocustDisasterTunables", "MaxLocustsPerInstanceCheatSwarm", &t.locMaxCheat},
      {"LocustDisasterTunables", "YearOfFirstPossibleVisitationAppearance", &t.locYearVisit},
  };
}
}  // namespace

void DisasterTunables::applyTunableMap(const std::map<uint32_t, int>& m) {
  for (auto& s : idSlots(*this)) {
    auto it = m.find(s.id);
    if (it != m.end()) *s.p = it->second;
  }
  minFlam = std::max(0, std::min(255, minFlam));
  spreadFlam = std::max(0, std::min(255, spreadFlam));
  tornadoSpeedMin = int(int8_t(tornadoSpeedMin));
  tornadoSpeedMax = int(int8_t(tornadoSpeedMax));
}

void DisasterTunables::load(const IniFile& simTune, const IniFile& layerIni) {
  std::map<uint32_t, int> m;
  for (auto& kv : simTune.section("Tunables")) m[parseIdNumber(kv.first)] = parseIntLoose(kv.second);
  applyTunableMap(m);
  for (auto& s : nameSlots(*this)) {
    std::string v;
    if (layerIni.get(s.section, s.key, v)) *s.p = parseIntLoose(v);
  }
  auto pair = [&](const char* sec, DisasterType t, int& count, int& sound) {
    for (auto& kv : layerIni.section(sec)) {
      if (parseIdNumber(kv.first) != uint32_t(t)) continue;
      size_t e = 0;
      int a = parseIntLoose(kv.second, &e);
      count = a;
      if (e < kv.second.size() && kv.second[e] == ',') sound = parseIntLoose(kv.second.substr(e + 1));
    }
  };
  int dummy = 0;
  pair("FireDisaster", DisasterType::Fire, maxFires, fireSound);
  pair("TornadoDisaster", DisasterType::Tornado, maxTornadoes, tornadoSound);
  pair("RiotDisaster", DisasterType::Riot, maxRiots, riotSound);
  pair("ParadeEvent", DisasterType::Parade, paradeMax, dummy);
}

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------
namespace {
int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
int drawRange(GameRng& r, int a, int b) { return b <= a ? a : r.randInt(a, b); }

// Sweep a rectangle starting at (cx,cz): rows start at cx and wrap, z advances with wrap. fn returns true to stop.
template <class F>
bool sweepRect(int x0, int z0, int x1, int z1, int cx, int cz, F fn) {  // inclusive bounds
  int nx = x1 - x0 + 1, nz = z1 - z0 + 1;
  if (nx <= 0 || nz <= 0) return false;
  cx = clampi(cx, x0, x1); cz = clampi(cz, z0, z1);
  for (int j = 0; j < nz; ++j) {
    int z = z0 + (cz - z0 + j) % nz;
    for (int i = 0; i < nx; ++i) {
      int x = x0 + (cx - x0 + i) % nx;
      if (fn(x, z)) return true;
    }
  }
  return false;
}
}  // namespace

DisasterManager::DisasterManager() {
  quakeHist_.entryWords = 9;
  ufoHist_.entryWords = 12;
  junkHist_.entryWords = 6;
  locustHist_.entryWords = 6; locustHist_.version = 2;
  whirlHist_.entryWords = 8;
}

void DisasterManager::resetToNewCity() {
  DisasterTunables t = tune;
  GameRng r = rng_;
  *this = DisasterManager();
  tune = t; rng_ = r;
}

// ---------------------------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------------------------
int DisasterManager::activeCount(DisasterType t) const {
  switch (t) {
    case DisasterType::Fire: return int(burning_.size());
    case DisasterType::Tornado: return int(tornadoes_.size());
    case DisasterType::Earthquake: return int(quakes_.size());
    case DisasterType::Riot: return int(riots_.size());
    case DisasterType::UFO: return int(ufos_.size());
    case DisasterType::ToxicCloud: return int(clouds_.size());
    case DisasterType::Whirlpool: return int(whirls_.size());
    case DisasterType::Locusts: return int(locusts_.size());
    case DisasterType::SpaceJunk: return int(junk_.size());
    case DisasterType::Parade: return paradeActive ? 1 : 0;
  }
  return 0;
}
bool DisasterManager::active(DisasterType t) const { return activeCount(t) > 0; }
bool DisasterManager::anyActiveNonParade() const {
  for (auto t : allDisasterTypes()) if (t != DisasterType::Parade && active(t)) return true;
  return false;
}
bool DisasterManager::anyActive() const { return anyActiveNonParade() || paradeActive; }
EmergencyMenuState DisasterManager::menuState() const {
  EmergencyMenuState s;
  s.goTo = anyActive();
  s.fireDispatch = s.policeDispatch = anyActiveNonParade();
  s.planeDispatch = active(DisasterType::Locusts);
  return s;
}

void DisasterManager::checkAllOver(DisasterOutput& out) {
  bool now = anyActiveNonParade();
  if (wasActive_ && !now) {
    out.event(DisasterEvent::AllOver, DisasterType::Fire);
    if (emergencyActive_) endEmergency(out);
    if (cost_ > tune.minDisasterCostToGetRelief) {
      uint32_t pct = uint32_t(emergencyEW_ ? tune.reliefPctWithEW : tune.reliefPctWithoutEW);
      uint32_t relief = uint32_t(pct * uint32_t(cost_)) / 100u;
      if (relief) out.cmd(DisasterCommand::CreditTreasury).value = relief;
    }
    cost_ = 0;
    emergencyEW_ = false;
  }
  wasActive_ = now;
}
void DisasterManager::endEmergency(DisasterOutput& out) {
  emergencyActive_ = false;
  emergencyMonths_ = 0;
  out.event(DisasterEvent::EmergencyEnded, DisasterType::Fire);
}
void DisasterManager::declareEmergency(DisasterOutput& out) {
  if (emergencyActive_) return;
  bool dis = anyActive();
  if (!dis) ++falseAlarms_;
  out.event(DisasterEvent::EmergencyDeclared, DisasterType::Fire, 0, 0, dis ? 1 : 0, falseAlarms_ < 3 ? 1 : 0);
  if (dis) falseAlarms_ = 0;
  if (falseAlarms_ < 3) {
    emergencyActive_ = true;
    emergencyMonths_ = 0;
    if (dis) emergencyEW_ = true;
  } else {
    out.event(DisasterEvent::EmergencyRefused, DisasterType::Fire);
  }
}

// ---------------------------------------------------------------------------------------------
// Destroy routine (section 9)
// ---------------------------------------------------------------------------------------------
bool DisasterManager::destroyOccupant(DisasterWorld& w, const OccInfo& o, int damage, DisasterOutput& out) {
  if (!o.valid) return false;
  if (o.agentType == 12850) return false;  // landfill
  if (o.isLot && o.lotDamage != 0) {
    if (o.lotDamage == damage || o.lotDamage == kDmgBurnt ||
        (o.lotDamage == kDmgRubble && (damage == kDmgRubble || damage == kDmgAbandon)))
      return false;
  }
  if (!(o.isLot && (o.lotDamage == kDmgBurnt || o.lotDamage == kDmgRubble))) cost_ += o.value;
  DisasterCommand& c = out.cmd(DisasterCommand::DestroyOccupant);
  c.id = o.id; c.x = o.ax; c.z = o.az; c.a = damage; c.value = o.value;
  c.b = o.isLot ? 1 : o.isFlora ? 2 : 0;
  c.c = (o.isFlora && (damage == kDmgBurnt || damage == kDmgCrop)) ? (damage == kDmgBurnt ? 8 : 9) : -1;
  // Nuclear fall-out (rule 7)
  if (o.agentType == 12115) {
    int cx = o.ax + o.w / 2, cz = o.az + o.h / 2;
    for (int z = cz - 20; z <= cz + 20; ++z)
      for (int x = cx - 20; x <= cx + 20; ++x) {
        if (!w.inMap(x, z)) continue;
        int d = int(std::lround(std::sqrt(double((x - cx) * (x - cx) + (z - cz) * (z - cz)))));
        if (d > 20) continue;
        if (d <= 10 || rng_.rand(10) != 0) {
          DisasterCommand& r = out.cmd(DisasterCommand::MarkRadiation);
          r.x = x; r.z = z;
        }
      }
  }
  return true;
}

// ---------------------------------------------------------------------------------------------
// Fire (section 5)
// ---------------------------------------------------------------------------------------------
void DisasterManager::fireSoundUpdate(DisasterOutput& out, bool) {
  DisasterCommand& c = out.cmd(DisasterCommand::SoundLoop);
  c.a = tune.fireSound;
  int n = int(burning_.size());
  c.b = n > 0 ? 1 : 0;
  c.value = tune.maxFires > 0 ? std::min(100, n * 100 / tune.maxFires) : 100;
}

bool DisasterManager::ignite(DisasterWorld& w, const OccInfo& o, int intensity, DisasterOutput& out) {
  if (!o.valid || o.burning) return false;
  if (w.flammability(o.ax, o.az) < tune.minFlam) return false;
  for (auto& b : burning_) if (b.occId == o.id) return false;
  BurnRecord r;
  r.occId = o.id; r.x = o.ax; r.z = o.az; r.intensity = clampi(intensity, 0, 2);
  r.duration = drawRange(rng_, tune.stageMin, tune.stageMax);
  burning_.push_back(r);
  DisasterCommand& c = out.cmd(DisasterCommand::IgniteOccupant);
  c.id = o.id; c.x = o.ax; c.z = o.az; c.a = r.intensity;
  fireSoundUpdate(out);
  wasActive_ = true;
  return true;
}

bool DisasterManager::sweepIgnite(DisasterWorld& w, int cx, int cz, int group, DisasterOutput& out) {
  bool ok = false;
  sweepRect(0, 0, w.width() - 1, w.height() - 1, cx, cz, [&](int x, int z) {
    OccInfo o = w.occupantAt(x, z);
    if (!o.valid) return false;
    if (group != 0 && !w.inGroup(o, group)) return false;
    if (ignite(w, o, 0, out)) {
      out.event(DisasterEvent::Started, DisasterType::Fire, o.ax, o.az, 1);
      ok = true;
      return true;
    }
    return false;
  });
  return ok;
}

bool DisasterManager::startRandomFire(DisasterWorld& w, DisasterOutput& out) {
  if (int(burning_.size()) > tune.maxFires) return false;
  int group = int(rng_.rand(100)) < tune.groupBias ? 300 : 100;
  int cx = int(rng_.rand(uint32_t(std::max(0, w.width() - 1))));
  int cz = int(rng_.rand(uint32_t(std::max(0, w.height() - 1))));
  if (sweepIgnite(w, cx, cz, group, out)) return true;
  return sweepIgnite(w, cx, cz, 0, out);
}

bool DisasterManager::startFire(DisasterWorld& w, int x, int z, DisasterOutput& out, int intensity) {
  if (!w.inMap(x, z)) return false;
  OccInfo o = w.occupantAt(x, z);
  if (!o.valid) return false;
  if (!ignite(w, o, intensity, out)) return false;
  out.event(DisasterEvent::Started, DisasterType::Fire, o.ax, o.az, 1);
  return true;
}

void DisasterManager::spread(DisasterWorld& w, const OccInfo& o, int p, DisasterOutput& out) {
  if (int(burning_.size()) > tune.maxFires || !o.valid) return;
  for (auto& t : w.occupantsTouching(o))
    if (int(rng_.rand(100)) < p) ignite(w, t, 0, out);
  int W = w.width(), H = w.height();
  int ox0 = o.ax, oz0 = o.az, ox1 = o.ax + o.w - 1, oz1 = o.az + o.h - 1;
  int ex0 = std::max(0, ox0 - 2), ez0 = std::max(0, oz0 - 2);
  int ex1 = std::min(W - 1, ox1 + 2), ez1 = std::min(H - 1, oz1 + 2);
  struct R { int x0, z0, x1, z1; };
  R strips[4] = {{ex0, ez0, ex1, oz0 - 1},                  // top
                 {ex0, oz1 + 1, ex1, ez1},                  // bottom
                 {ex0, std::max(ez0, oz0), ox0 - 1, std::min(ez1, oz1)},   // left
                 {ox1 + 1, std::max(ez0, oz0), ex1, std::min(ez1, oz1)}};  // right
  for (auto& s : strips) {
    if (s.x1 < s.x0 || s.z1 < s.z0) continue;
    std::vector<OccInfo> found;
    std::vector<uint32_t> seen;
    for (int z = s.z0; z <= s.z1 && found.size() < 6; ++z)
      for (int x = s.x0; x <= s.x1 && found.size() < 6; ++x) {
        OccInfo c = w.occupantAnchoredAt(x, z);
        if (!c.valid || w.flammability(c.ax, c.az) <= tune.spreadFlam) continue;
        if (std::find(seen.begin(), seen.end(), c.id) != seen.end()) continue;
        seen.push_back(c.id);
        found.push_back(c);
      }
    for (auto& c : found)
      if (int(rng_.rand(100)) < p) ignite(w, c, 0, out);
  }
}

void DisasterManager::burnTick(DisasterWorld& w, DisasterOutput& out) {
  if (burning_.empty()) return;
  const int n = int(burning_.size());
  std::vector<char> dead(size_t(n), 0);
  int sMaxStage = tune.stageMax;
  float s = (255 - tune.minFlam) > 0 ? float(tune.stageMax - tune.stageMin) / float(255 - tune.minFlam) : 0.f;
  if (255 - tune.minFlam <= 0) sMaxStage = 0;
  float k[3];
  for (int i = 0; i < 3; ++i) k[i] = float(tune.spreadHi[i] - tune.spreadLo[i]) / 255.f;
  bool changed = false;
  for (int i = 0; i < n; ++i) {
    BurnRecord r = burning_[size_t(i)];
    OccInfo o = w.occupantAt(r.x, r.z);
    if (r.occId == 0 && o.valid) r.occId = o.id;           // bind a record loaded from a save
    if (!o.valid || o.id != r.occId) { dead[size_t(i)] = 1; changed = true; continue; }
    if (++r.timer < r.duration) { burning_[size_t(i)] = r; continue; }
    r.steps += 1; r.timer = 0;
    int cov = w.fireCoverage(r.x, r.z), flam = w.flammability(r.x, r.z);
    bool grow = r.steps < 10 ? cov < int(rng_.rand(255)) : true;
    r.duration = grow ? int(float(sMaxStage) - float(flam) * s) : int(float(flam) * s + float(tune.stageMin));
    int p = int(float(tune.spreadHi[r.intensity]) - float(cov) * k[r.intensity]);
    int oldI = r.intensity;
    burning_[size_t(i)] = r;
    if (p > 0) spread(w, o, p, out);
    BurnRecord& rr = burning_[size_t(i)];
    if (oldI == 0) {
      if (grow) { rr.intensity = 1; DisasterCommand& c = out.cmd(DisasterCommand::SetFireIntensity); c.id = o.id; c.a = 1; }
      else { dead[size_t(i)] = 1; changed = true; out.cmd(DisasterCommand::ClearBurning).id = o.id; }
    } else if (oldI == 1) {
      rr.intensity = grow ? 2 : 0;
      DisasterCommand& c = out.cmd(DisasterCommand::SetFireIntensity); c.id = o.id; c.a = rr.intensity;
    } else {
      if (grow) {
        dead[size_t(i)] = 1; changed = true;
        DisasterCommand& e = out.cmd(DisasterCommand::Explosion); e.x = r.x; e.z = r.z;
        destroyOccupant(w, o, kDmgBurnt, out);
      } else {
        rr.intensity = 1;
        DisasterCommand& c = out.cmd(DisasterCommand::SetFireIntensity); c.id = o.id; c.a = 1;
      }
    }
  }
  if (changed) {
    std::vector<BurnRecord> keep;
    for (int i = 0; i < n; ++i) if (!dead[size_t(i)]) keep.push_back(burning_[size_t(i)]);
    for (size_t i = size_t(n); i < burning_.size(); ++i) keep.push_back(burning_[i]);  // newly ignited this tick
    burning_.swap(keep);
    fireSoundUpdate(out);
  } else if (int(burning_.size()) != n) {
    // new ignitions already updated the sound
  }
}

// ---------------------------------------------------------------------------------------------
// Tornado (section 6.1)
// ---------------------------------------------------------------------------------------------
bool DisasterManager::startTornado(DisasterWorld& w, int x, int z, float dirX, float dirZ, int variant,
                                   int lifetime, float speed, bool warn, DisasterOutput& out) {
  float len = std::sqrt(dirX * dirX + dirZ * dirZ);
  if (len <= 0.f || !w.inMap(x, z)) return false;
  TornadoInst t;
  t.px = float(x) + 0.5f; t.pz = float(z) + 0.5f;
  t.dx = dirX / len; t.dz = dirZ / len;
  t.variant = clampi(variant, 0, 2); t.lifetime = lifetime; t.speed = speed;
  for (int i = 0; i < 5; ++i) t.warn[i] = tune.tornadoWarn[i];
  if (!warn) { t.phase = 4; t.counter = t.warn[4]; }
  tornadoes_.push_back(t);
  out.event(DisasterEvent::Started, DisasterType::Tornado, x, z);
  wasActive_ = true;
  return true;
}
bool DisasterManager::startRandomTornado(DisasterWorld& w, DisasterOutput& out) {
  if (int(tornadoes_.size()) > tune.maxTornadoes) return false;
  int x = int(rng_.rand(uint32_t(w.width()))), z = int(rng_.rand(uint32_t(w.height())));
  float u = rng_.randRange(-1.f, 1.f);
  float dz = std::sqrt(std::max(0.f, 1.f - u * u));
  float speed = rng_.randRange(float(tune.tornadoSpeedMin), float(tune.tornadoSpeedMax));
  int variant = int(rng_.rand(3));
  int life = drawRange(rng_, tune.tornadoLifeMin, tune.tornadoLifeMax);
  return startTornado(w, x, z, u, dz, variant, life, speed, true, out);
}
bool DisasterManager::scriptTornado(DisasterWorld& w, const int a[8], DisasterOutput& out) {
  int W = w.width(), H = w.height();
  int sx = clampi(a[0] - 1, 0, W - 1), sz = clampi(a[1] - 1, 0, H - 1);
  int tx = clampi(a[2] - 1, 0, W - 1), tz = clampi(a[3] - 1, 0, H - 1);
  int variant = clampi(a[4], 0, 2);
  int life = clampi(a[5], 0, 400);
  int speed = a[6] < 0 ? 1 : clampi(a[6], 1, 500);
  return startTornado(w, sx, sz, float(tx - sx), float(tz - sz), variant, life, float(speed), a[7] != 0, out);
}

void DisasterManager::tornadoTick(DisasterWorld& w, DisasterOutput& out) {
  std::vector<TornadoInst> keep;
  for (size_t i = 0; i < tornadoes_.size(); ++i) {
    TornadoInst t = tornadoes_[i];
    bool end = false;
    if (t.phase < 5) {
      if (t.counter >= t.warn[t.phase]) {
        t.phase++; t.counter = 0;
        if (t.phase < 5) out.event(DisasterEvent::Premonition, DisasterType::Tornado, 0, 0, t.phase);
        else {
          DisasterCommand& c = out.cmd(DisasterCommand::TornadoTouchdown);
          c.x = int(t.px); c.z = int(t.pz); c.a = t.variant + 1; c.f = t.dx; c.f2 = t.dz; c.f3 = t.speed;
        }
      } else t.counter++;
    } else {
      float step = float(double(t.speed) * tune.tornadoTilesPerSpeedUnitPerTick);
      t.px += t.dx * step; t.pz += t.dz * step;
      t.elapsed++;
      int tx = int(std::floor(t.px)), tz = int(std::floor(t.pz));
      if (!w.inMap(tx, tz)) end = true;
      else {
        if (int(rng_.rand(100)) <= tune.tornadoDestroyChance[t.variant]) {
          OccInfo o = w.occupantAt(tx, tz);
          if (o.valid) {
            int64_t before = cost_;
            if (destroyOccupant(w, o, kDmgRubble, out)) {
              t.destroyed++; t.cost += cost_ - before;
              DisasterCommand& e = out.cmd(DisasterCommand::Explosion); e.x = tx; e.z = tz;
              if (o.flags & kOccOverWater) { DisasterCommand& s = out.cmd(DisasterCommand::Splash); s.x = tx; s.z = tz; }
            }
          }
        }
        if (t.elapsed >= t.lifetime) end = true;
      }
    }
    if (end) out.event(DisasterEvent::Ended, DisasterType::Tornado, int(t.px), int(t.pz));
    else keep.push_back(t);
  }
  tornadoes_.swap(keep);
}

// ---------------------------------------------------------------------------------------------
// History helpers
// ---------------------------------------------------------------------------------------------
void DisasterManager::seedHistory(HistoryList& h, int32_t today) {
  if (!h.entries.empty()) return;
  HistoryEntry e(size_t(h.entryWords), 0);
  e[0] = uint32_t(today);
  h.entries.push_back(e);
}
void DisasterManager::addHistory(HistoryList& h, int32_t date, uint32_t d1, int destroyed, int64_t cost) {
  HistoryEntry e(size_t(h.entryWords), 0);
  e[0] = uint32_t(date);
  if (h.entryWords > 3) e[1] = d1;
  e[size_t(h.entryWords) - 2] = uint32_t(destroyed);
  e[size_t(h.entryWords) - 1] = uint32_t(std::min<int64_t>(std::max<int64_t>(cost, 0), 0xFFFFFFFFll));
  h.entries.push_back(e);
  if (h.entries.size() > 50000) h.entries.erase(h.entries.begin(), h.entries.begin() + long(h.entries.size() - 50000));
}
namespace {
// Shared "gap since last" test of earthquakes / UFO / whirlpool / space junk.
bool gapElapsed(const HistoryList& h, int32_t today, int months) {
  int32_t last = today;
  if (!h.entries.empty()) last = std::min<int32_t>(today, int32_t(h.entries.back()[0]));
  return int64_t(today) - last > int64_t(months) * 30;
}
}  // namespace

// ---------------------------------------------------------------------------------------------
// Earthquake (section 7)
// ---------------------------------------------------------------------------------------------
bool DisasterManager::startEarthquake(DisasterWorld&, const DisasterInputs& in, float m, bool warn,
                                      DisasterOutput& out) {
  m = std::max(0.f, std::min(1.f, m));
  if (m <= 0.f) return true;  // no-op that reports success
  if (int(quakes_.size()) >= tune.quakeMax) return false;
  QuakeInst q;
  float M = (float(tune.quakeMinMag) + float(tune.quakeMaxMag - tune.quakeMinMag) * m) * 0.1f;
  q.mag = std::floor(M * 10.f + 0.5f) / 10.f;
  q.cls = q.mag <= 4.0f ? 1 : q.mag <= 6.0f ? 2 : 3;
  q.warn = warn;
  for (int i = 0; i < 4; ++i) q.T[i] = warn ? tune.quakeT[i] : 0;
  q.phase = warn ? 1 : 4;
  quakes_.push_back(q);
  today_ = in.jdn;
  out.event(DisasterEvent::Started, DisasterType::Earthquake, 0, 0, int(q.mag * 10));
  wasActive_ = true;
  return true;
}
bool DisasterManager::scriptEarthquake(DisasterWorld& w, const DisasterInputs& in, int a0, int a1, DisasterOutput& out) {
  return startEarthquake(w, in, std::max(0.f, std::min(1.f, float(a0) * 0.1f)), a1 != 0, out);
}

void DisasterManager::quakeMainShock(DisasterWorld& w, QuakeInst& q, DisasterOutput& out) {
  const int W = w.width() * 256, H = w.height() * 256;
  int zA, zB, guard = 0;
  do {
    zA = rng_.randInt(0, H);
    zB = rng_.randInt(zA - 3 * H / 4, zA + 3 * H / 4);
  } while (std::abs(zB - zA) < 1024 && ++guard < 1000);
  int xA = 0, xB = W - 1;
  if (zB < 0 || zB > H - 1) {
    int zc = zB < 0 ? 0 : H - 1;
    if (zB != zA) xB = int(double(W - 1) * double(zc - zA) / double(zB - zA));
    zB = zc;
  }
  int ez = rng_.randInt(std::min(zA, zB), std::max(zA, zB) + 1);
  q.epiZ = ez / 256;
  q.epiX = int(double(xB - xA) * (zB == zA ? 0.0 : double(ez - zA) / double(zB - zA))) / 256;
  // rasterise A -> B in tiles
  double ax = xA / 256.0, az = zA / 256.0, bx = xB / 256.0, bz = zB / 256.0;
  int steps = int(std::max(std::fabs(bx - ax), std::fabs(bz - az))) + 1;
  steps = std::min(steps, 1281);
  std::vector<std::pair<int, int>> cells;
  for (int i = 0; i < steps; ++i) {
    double t = steps > 1 ? double(i) / double(steps - 1) : 0.0;
    int x = int(ax + (bx - ax) * t), z = int(az + (bz - az) * t);
    x += rng_.randInt(-1, 2);
    x = clampi(x, 0, w.width() - 1); z = clampi(z, 0, w.height() - 1);
    cells.emplace_back(x, z);
  }
  if (q.cls > 1) {
    bool inZ = std::fabs(bx - ax) > std::fabs(bz - az);
    std::vector<std::pair<int, int>> wide;
    for (auto& c : cells) {
      for (int d = -(q.cls - 1); d <= q.cls - 1; ++d) {
        int x = c.first + (inZ ? 0 : d), z = c.second + (inZ ? d : 0);
        if (w.inMap(x, z)) wide.emplace_back(x, z);
      }
    }
    cells.swap(wide);
  }
  q.fault = cells;
  DisasterCommand& s = out.cmd(DisasterCommand::ScreenShake);
  s.a = tune.quakeShake[q.cls - 1]; s.b = tune.quakeShakeParam[q.cls - 1];
  DisasterCommand& snd = out.cmd(DisasterCommand::Sound);
  snd.a = 0; snd.x = q.epiX; snd.z = q.epiZ;
  q.shakeLeft = std::max(1, tune.quakeShakeParam[q.cls - 1]);
  seedHistory(quakeHist_, today_);
}

void DisasterManager::quakeDamage(DisasterWorld& w, QuakeInst& q, DisasterOutput& out) {
  const int cls = q.cls;
  if (cls >= 2) {
    int P = tune.quakeScarpP[cls - 1], Q = tune.quakeScarpQ[cls - 1], h = tune.terrainHeightStep;
    int S = rng_.rangeInt(3, 7);
    for (size_t i = 0; i < q.fault.size(); ++i) {
      int p, s;
      if (int(i) == S) { p = 1; s = Q != 0 ? -(h / 2) : 0; }
      else if (int(i) < S) { p = rng_.rangeInt(P, P + 1); s = Q != 0 ? rng_.rangeInt(Q, Q + 1) : 0; }
      else { p = 0; s = Q != 0 ? -(h / 4) : 0; S += rng_.rangeInt(3, 7); }
      int x = q.fault[i].first, z = q.fault[i].second;
      for (int v = 0; v < 4; ++v) {
        int d = v == 0 ? p : s;
        int vz = z - 1 + v;
        if (d == 0 || x < 0 || vz < 0 || x > w.width() || vz > w.height()) continue;
        DisasterCommand& c = out.cmd(DisasterCommand::TerrainDelta);
        c.x = x; c.z = vz; c.a = d; c.b = (v != 0 && s != 0) ? 1 : 0; c.c = cls;
      }
    }
  }
  const int hw = tune.quakeHalfWidth[cls - 1];
  for (auto& cell : q.fault) {
    for (int dz = -hw; dz <= hw; ++dz) {
      int z = cell.second + dz, x = cell.first;
      if (!w.inMap(x, z)) continue;
      OccInfo o = w.occupantAnchoredAt(x, z);
      if (!o.valid) continue;
      if (int(rng_.rand(10000)) < tune.quakeDestroy[cls - 1]) {
        int64_t before = cost_;
        if (destroyOccupant(w, o, kDmgRubble, out)) {
          q.destroyed++; q.cost += cost_ - before;
          if (rng_.rand(50) == 0) {
            DisasterCommand& e = out.cmd(DisasterCommand::Explosion); e.x = x; e.z = z;
            if (o.flags & kOccOverWater) { DisasterCommand& s = out.cmd(DisasterCommand::Splash); s.x = x; s.z = z; }
          }
        }
      } else if (int(rng_.rand(10000)) < tune.quakeIgnite[cls - 1] && !(o.flags & kOccNonFlammable)) {
        ignite(w, o, 0, out);
      }
    }
  }
}

void DisasterManager::quakeTick(DisasterWorld& w, DisasterOutput& out) {
  std::vector<QuakeInst> keep;
  for (size_t i = 0; i < quakes_.size(); ++i) {
    QuakeInst q = quakes_[i];
    bool done = false;
    q.counter++;
    bool progress = true;
    while (progress && !done) {
      progress = false;
      switch (q.phase) {
        case 1: case 2: case 3: case 4:
          if (q.counter >= q.T[q.phase - 1]) {
            q.phase++; progress = true;
            if (q.phase <= 4) out.event(DisasterEvent::Premonition, DisasterType::Earthquake, 0, 0, q.phase - 1);
            if (q.phase == 3 && tune.quakeForeParam[q.cls - 1] != 0) {
              DisasterCommand& s = out.cmd(DisasterCommand::ScreenShake);
              s.a = tune.quakeFore[q.cls - 1]; s.b = tune.quakeForeParam[q.cls - 1];
            }
            if (q.phase == 5) quakeMainShock(w, q, out);
          }
          break;
        case 5:
          if (--q.shakeLeft <= 0) { q.phase = 6; progress = true; }
          break;
        case 6:
          quakeDamage(w, q, out);
          addHistory(quakeHist_, today_, uint32_t(q.mag * 10.f + 0.5f), q.destroyed, q.cost);
          done = true;
          break;
      }
      if (q.phase == 5 && progress) progress = false;  // shake runs at least until next tick
    }
    if (done) out.event(DisasterEvent::Ended, DisasterType::Earthquake);
    else keep.push_back(q);
  }
  quakes_.swap(keep);
}

// ---------------------------------------------------------------------------------------------
// Riot (section 8)
// ---------------------------------------------------------------------------------------------
bool DisasterManager::findRiotSite(DisasterWorld& w, int cx, int cz, int& ox, int& oz) {
  int r = tune.riotSearchRadius;
  return sweepRect(std::max(0, cx - r), std::max(0, cz - r), std::min(w.width() - 1, cx + r),
                   std::min(w.height() - 1, cz + r), cx, cz, [&](int x, int z) {
    if (w.policeCoverage(x, z) >= tune.riotSiteCoverageMax) return false;
    OccInfo o = w.occupantAt(x, z);
    if (!o.valid || !(o.flags & kOccCrowdSite)) return false;
    ox = x; oz = z;
    return true;
  });
}

bool DisasterManager::startRiot(DisasterWorld& w, int x, int z, bool warn, DisasterOutput& out) {
  if (!w.inMap(x, z) || int(riots_.size()) >= tune.maxRiots) return false;
  int sx, sz;
  if (!findRiotSite(w, x, z, sx, sz)) return false;
  RiotInst r;
  r.x = sx; r.z = sz; r.noWarn = !warn;
  if (!warn) { r.phase = 1; r.counter = tune.riotBrewTime; }
  riots_.push_back(r);
  out.event(DisasterEvent::Started, DisasterType::Riot, sx, sz);
  wasActive_ = true;
  return true;
}
bool DisasterManager::scriptRiot(DisasterWorld& w, int a0, int a1, int a2, DisasterOutput& out) {
  return startRiot(w, clampi(a0 - 1, 0, w.width() - 1), clampi(a1 - 1, 0, w.height() - 1), a2 != 0, out);
}

void DisasterManager::riotTick(DisasterWorld& w, const DisasterInputs& in, DisasterOutput& out) {
  std::vector<RiotInst> keep;
  float kDur = float(tune.riotDurMax - tune.riotDurMin) / 255.f;
  for (size_t i = 0; i < riots_.size(); ++i) {
    RiotInst r = riots_[i];
    bool end = false;
    if (r.phase == 0) {
      if (r.counter >= tune.riotWarnDelay) {
        r.phase = 1; r.counter = 0;
        out.event(DisasterEvent::RiotBrewing, DisasterType::Riot, r.x, r.z, int(rng_.rand(6)));
      } else r.counter++;
    } else if (r.phase == 1) {
      if (r.counter >= tune.riotBrewTime) {
        OccInfo o = w.occupantAt(r.x, r.z);
        bool siteOk = o.valid && (o.flags & kOccCrowdSite) && w.policeCoverage(r.x, r.z) < tune.riotSiteCoverageMax;
        if ((r.noWarn || in.unrest >= tune.riotUnrestThreshold) && siteOk) {
          r.phase = 2; r.counter = 0;
          DisasterCommand& c = out.cmd(DisasterCommand::SpawnRioters);
          c.x = r.x; c.z = r.z; c.a = tune.riotCrowdA; c.b = tune.riotCrowdB;
          DisasterCommand& s = out.cmd(DisasterCommand::SoundLoop);
          s.a = tune.riotSound; s.b = 1; s.value = 100;
          out.event(DisasterEvent::RiotBrokeOut, DisasterType::Riot, r.x, r.z);
        } else end = true;  // fizzles
      } else r.counter++;
    } else {
      r.strength += tune.riotGrowth;
      r.counter++; r.ticks++;
      if (r.ticks % 8 == 0) {
        int cov = w.policeCoverage(r.x, r.z);
        if (r.counter >= int(float(tune.riotDurMax) - float(cov) * kDur)) {
          end = true;
          DisasterCommand& s = out.cmd(DisasterCommand::SoundLoop);
          s.a = tune.riotSound; s.b = 0;
          out.event(DisasterEvent::RiotOver, DisasterType::Riot, r.x, r.z, int(rng_.rand(3)));
        }
      }
    }
    if (end) out.event(DisasterEvent::Ended, DisasterType::Riot, r.x, r.z);
    else keep.push_back(r);
  }
  riots_.swap(keep);
}

void DisasterManager::rioterAt(DisasterWorld& w, int x, int z, DisasterOutput& out) {
  if (riots_.empty()) return;
  OccInfo o = w.occupantAt(x, z);
  if (o.valid && ignite(w, o, 0, out)) riots_[0].burned++;
}

bool DisasterManager::riotRelocate(DisasterWorld& w, DisasterOutput& out) {
  if (riots_.empty()) return false;
  RiotInst& r = riots_[0];
  for (int attempt = 0; attempt < 3; ++attempt) {
    int cx = rng_.rangeInt(0, w.width() - 1), cz = rng_.rangeInt(0, w.height() - 1);
    int sx, sz;
    if (findRiotSite(w, cx, cz, sx, sz) && (sx != r.x || sz != r.z)) {
      r.x = sx; r.z = sz;
      DisasterCommand& c = out.cmd(DisasterCommand::SpawnRioters);
      c.x = sx; c.z = sz; c.a = tune.riotCrowdA; c.b = tune.riotCrowdB;
      return true;
    }
  }
  out.event(DisasterEvent::Ended, DisasterType::Riot, r.x, r.z);
  riots_.erase(riots_.begin());
  checkAllOver(out);
  return false;
}

// ---------------------------------------------------------------------------------------------
// Toxic cloud (section 6.2)
// ---------------------------------------------------------------------------------------------
bool DisasterManager::startToxicCloud(DisasterWorld& w, int x, int z, bool warn, DisasterOutput& out,
                                      int level, float dirX, float dirZ) {
  if (int(clouds_.size()) >= tune.toxMaxInstances) return false;
  CloudInst c;
  c.x = clampi(x, 0, w.width() - 1); c.z = clampi(z, 0, w.height() - 1);
  c.level = std::max(0, level);
  float len = std::sqrt(dirX * dirX + dirZ * dirZ);
  if (len > 0.f) { c.dx = dirX / len; c.dz = dirZ / len; }
  else {
    float u = rng_.randRange(-1.f, 1.f);
    c.dx = u; c.dz = std::sqrt(std::max(0.f, 1.f - u * u)) * (rng_.rand(2) ? 1.f : -1.f);
  }
  c.speed = rng_.randRange(float(tune.toxMinSpeed), float(tune.toxMaxSpeed));
  c.phase = warn ? 0 : 3;
  c.counter = warn ? 0 : tune.toxVisible;
  clouds_.push_back(c);
  haveCloud_ = true; lastCloudJdn_ = today_;
  out.event(DisasterEvent::Started, DisasterType::ToxicCloud, c.x, c.z);
  wasActive_ = true;
  return true;
}
bool DisasterManager::scriptToxicCloud(DisasterWorld& w, int a0, int a1, int a2, DisasterOutput& out) {
  return startToxicCloud(w, a0 - 1, a1 - 1, a2 != 0, out);
}

void DisasterManager::cloudTick(DisasterWorld& w, DisasterOutput& out) {
  std::vector<CloudInst> keep;
  for (size_t i = 0; i < clouds_.size(); ++i) {
    CloudInst c = clouds_[i];
    bool end = false;
    if (c.phase < 4) {
      c.counter++;
      if (c.phase == 0 && c.counter >= tune.toxPrem1) { c.phase = 1; out.event(DisasterEvent::Premonition, DisasterType::ToxicCloud, 0, 0, 1); }
      else if (c.phase == 1 && c.counter >= tune.toxPrem2) { c.phase = 2; out.event(DisasterEvent::Premonition, DisasterType::ToxicCloud, 0, 0, 2); }
      else if (c.phase == 2 && c.counter >= tune.toxPrem3) { c.phase = 3; out.event(DisasterEvent::Premonition, DisasterType::ToxicCloud, 0, 0, 3); }
      else if (c.phase == 3 && c.counter > tune.toxVisible) {
        // Formation
        c.phase = 4; c.counter = 0;
        int rmax = std::max(tune.toxMaxRainStart, tune.toxMinRainStart + 1);
        c.rainStart = rng_.rangeInt(tune.toxMinRainStart, rmax);
        int dmin = tune.toxMinDuration == 0 ? 10 : tune.toxMinDuration;
        c.lifetime = rng_.rangeInt(dmin, std::max(tune.toxMaxDuration, dmin + 1));
        int want = rng_.rangeInt(tune.toxMinClouds, std::max(tune.toxMaxClouds, tune.toxMinClouds + 1));
        if (want < 1) want = 1;
        int gap = rng_.rangeInt(8, 16), scanned = 0;
        int passes = 0;
        while (int(c.puffs.size()) < want && passes < 3) {
          sweepRect(0, 0, w.width() - 1, w.height() - 1, c.x, c.z, [&](int x, int z) {
            if (++scanned >= gap) {
              scanned = 0; gap = rng_.rangeInt(8, 16);
              float px = float(x) + rng_.randRange(-6.f, 6.f), pz = float(z) + rng_.randRange(-6.f, 6.f);
              (void)rng_.randRange(float(tune.toxMinHeight), float(tune.toxMaxHeight));  // altitude (presentation)
              if (w.inMap(int(std::floor(px)), int(std::floor(pz)))) c.puffs.emplace_back(px, pz);
            }
            return int(c.puffs.size()) >= want;
          });
          ++passes;
        }
        DisasterCommand& cmd = out.cmd(DisasterCommand::ToxicPuffs);
        cmd.a = c.level;
        float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
        for (auto& p : c.puffs) {
          cmd.pts.emplace_back(int(p.first), int(p.second));
          x0 = std::min(x0, p.first); x1 = std::max(x1, p.first); z0 = std::min(z0, p.second); z1 = std::max(z1, p.second);
        }
        if (!c.puffs.empty()) { c.cx = (x0 + x1) / 2; c.cz = (z0 + z1) / 2; }
      }
    } else {
      c.counter++;
      float step = float(double(c.speed) * tune.tornadoTilesPerSpeedUnitPerTick);
      std::vector<std::pair<float, float>> alive;
      for (auto p : c.puffs) {
        p.first += c.dx * step; p.second += c.dz * step;
        if (w.inMap(int(std::floor(p.first)), int(std::floor(p.second)))) alive.push_back(p);
      }
      c.puffs.swap(alive);
      c.cx += c.dx * step; c.cz += c.dz * step;
      if (!c.raining && c.counter > c.rainStart) {
        c.raining = true;
        int rmax = std::max(tune.toxMaxRain, tune.toxMinRain + 1);
        c.rainLeft = rng_.rangeInt(tune.toxMinRain, rmax);
        out.cmd(DisasterCommand::ToxicRain).a = 1;
      }
      if (c.raining) {
        int cx = int(std::floor(c.cx)), cz = int(std::floor(c.cz));
        int dh = (c.level + 1) * tune.toxDestroyRadiusInc, ah = (c.level + 1) * tune.toxAbandonRadiusInc;
        if (int(rng_.rand(1000)) < tune.toxDestroyLikelihood) {
          int x = cx + rng_.randInt(-dh, dh + 1), z = cz + rng_.randInt(-dh, dh + 1);
          if (w.inMap(x, z)) destroyOccupant(w, w.occupantAt(x, z), kDmgBurnt, out);
        }
        if (int(rng_.rand(1000)) < tune.toxAbandonLikelihood) {
          int x = cx + rng_.randInt(-ah, ah + 1), z = cz + rng_.randInt(-ah, ah + 1);
          if (w.inMap(x, z)) {
            OccInfo o = w.occupantAt(x, z);
            if (o.valid && o.isLot && w.zoneType(x, z) != 22 &&
                (w.isA(o.agentType, 3000) || w.isA(o.agentType, 5000) || w.isA(o.agentType, 8000)) &&
                destroyOccupant(w, o, kDmgAbandon, out)) {
              DisasterCommand& m = out.cmd(DisasterCommand::MarkAbandoned); m.x = o.ax; m.z = o.az;
            }
          }
        }
        if (--c.rainLeft <= 0) {
          c.raining = false;
          out.cmd(DisasterCommand::ToxicRain).a = 0;
          int rmax = std::max(tune.toxMaxRainStart, tune.toxMinRainStart + 1);
          c.rainStart = c.counter + tune.toxMinGap + rng_.rangeInt(tune.toxMinRainStart, rmax);
        }
      }
      if (c.puffs.empty() || c.counter > c.lifetime) {
        end = true;
        if (c.raining) out.cmd(DisasterCommand::ToxicRain).a = 0;
      }
    }
    if (end) out.event(DisasterEvent::Ended, DisasterType::ToxicCloud, c.x, c.z);
    else keep.push_back(c);
  }
  clouds_.swap(keep);
}

// ---------------------------------------------------------------------------------------------
// UFO (section 6.3)
// ---------------------------------------------------------------------------------------------
namespace {
// UFO category of a lot occupant: 4 power, 3 high-tech, 2 spaceport, 1 landmark, 0 low-tech, -1 excluded.
int ufoCategory(DisasterWorld& w, const OccInfo& o) {
  if (w.isA(o.agentType, 12101)) return 4;
  static const uint32_t hi[] = {13870, 1001, 9100, 12310, 12330, 19001};
  for (uint32_t t : hi) if (w.isA(o.agentType, t)) return 3;
  if (w.isA(o.agentType, 12350)) return 2;
  if (w.isA(o.agentType, 14160) || w.isA(o.agentType, 18000)) return 1;
  if (o.flags & (kOccNonFlammable | kOccIndestructible)) return -1;
  return 0;
}
}  // namespace

bool DisasterManager::cropTile(DisasterWorld& w, int x, int z, OccInfo* out) const {
  OccInfo o = w.occupantAt(x, z);
  bool ok = o.valid && o.w == 1 && o.h == 1 && o.isLot && o.crop && w.isA(o.agentType, 9300);
  if (ok && out) *out = o;
  return ok;
}

bool DisasterManager::startUfo(DisasterWorld&, const DisasterInputs& in, bool warn, DisasterOutput& out) {
  int Y = in.year;
  if (int(ufos_.size()) >= tune.ufoMaxInstances || Y < tune.ufoYearVisit) return false;
  int kind = 2;
  if (Y >= tune.ufoYearAttack && rng_.rand(100) >= 10) kind = 5;
  else if (Y >= tune.ufoYearCrop && rng_.rand(100) >= 30) kind = 4;
  else if (Y >= tune.ufoYearAbduct && rng_.rand(100) >= 5) kind = 3;
  UfoInst u;
  u.kind = kind;
  if (warn) {
    u.T[0] = tune.ufoPrem1; u.T[1] = tune.ufoPrem2; u.T[2] = tune.ufoPrem3; u.T[3] = tune.ufoAttack;
    u.T[4] = tune.ufoAttackEnd; u.phase = 0;
  } else {
    u.T[0] = u.T[1] = u.T[2] = u.T[3] = 0; u.T[4] = tune.ufoAttackEnd - tune.ufoAttack; u.phase = 3;
  }
  ufos_.push_back(u);
  today_ = in.jdn; lastIn_ = in;
  out.event(DisasterEvent::UfoKind, DisasterType::UFO, 0, 0, kind);
  out.event(DisasterEvent::Started, DisasterType::UFO, 0, 0);
  wasActive_ = true;
  return true;
}
bool DisasterManager::scriptUfo(DisasterWorld& w, const DisasterInputs& in, int a0, DisasterOutput& out) {
  return startUfo(w, in, a0 != 0, out);
}

void DisasterManager::launchUfo(DisasterWorld& w, const DisasterInputs& in, UfoInst& u, DisasterOutput& out) {
  const int W = w.width(), H = w.height();
  std::vector<std::pair<int, int>> bombs, crops;
  if (u.kind == 5) {
    std::vector<int> order(size_t(W) * size_t(H));
    for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[rng_.rand(uint32_t(i))]);
    std::vector<std::pair<int, int>> lists[5];
    const int caps[5] = {tune.ufoMaxLowTech, tune.ufoMaxLandmark, tune.ufoMaxSpaceport, tune.ufoMaxHighTech, tune.ufoMaxPower};
    for (int idx : order) {
      int x = idx % W, z = idx / W;
      OccInfo o = w.occupantAnchoredAt(x, z);
      if (!o.valid || !o.isLot) continue;
      int cat = ufoCategory(w, o);
      if (cat < 0 || int(lists[cat].size()) >= caps[cat]) continue;
      lists[cat].emplace_back(o.ax + o.w / 2, o.az + o.h / 2);
    }
    const int emit[5] = {0, 1, 2, 3, 4};
    for (int c : emit)
      for (auto& p : lists[c]) if (int(bombs.size()) < tune.ufoMaxTotal) bombs.push_back(p);
  }
  if (u.kind == 4 || u.kind == 5) {
    for (int x = 0; x < W && int(crops.size()) < tune.ufoMaxCropCircles; ++x)
      for (int z = 0; z < H && int(crops.size()) < tune.ufoMaxCropCircles; ++z)
        if (cropTile(w, x, z)) crops.emplace_back(x, z);
  }
  int n = 1;
  if (u.kind == 5) n = std::max(6, W / 16 + 1);
  else if (u.kind == 4) n = std::max(5, W / 16 + 1);
  n = std::min(n, tune.ufoMaxPerInstance);
  if (in.cheatUfoSwarm) n = tune.ufoMaxPerInstanceCheat;
  DisasterCommand& c = out.cmd(DisasterCommand::UfoSwarm);
  c.a = n; c.b = u.kind != 5 ? 1 : 0; c.c = u.kind != 2 ? 5 : 0;
  c.pts = bombs; c.pts2 = crops;
  if (in.cheatUfoSwarm || u.kind == 2 || bombs.size() < 3 || crops.size() < 3)
    for (int i = 0; i < n * 2; ++i)
      c.pts3.emplace_back(int(rng_.rand(uint32_t(W))), int(rng_.rand(uint32_t(H))));
}

void DisasterManager::ufoTick(DisasterWorld& w, DisasterOutput& out) {
  std::vector<UfoInst> keep;
  for (size_t i = 0; i < ufos_.size(); ++i) {
    UfoInst u = ufos_[i];
    u.counter++;
    bool end = false;
    if (u.phase < 3) {
      if (u.counter >= u.T[u.phase]) { u.phase++; out.event(DisasterEvent::Premonition, DisasterType::UFO, 0, 0, u.phase); }
    } else if (u.phase == 3) {
      if (u.counter >= u.T[3]) { u.phase = 4; launchUfo(w, lastIn_, u, out); }
    }
    if (u.phase == 4 && u.counter >= u.T[4]) {
      u.phase = 5;
      DisasterCommand& c = out.cmd(DisasterCommand::UfoLeave);
      int saucers = 1;
      for (auto& cc : out.commands) if (cc.kind == DisasterCommand::UfoSwarm) saucers = cc.a;
      c.a = saucers;
      for (int s = 0; s < saucers; ++s) c.ints.push_back(int(rng_.rand(10000)) < tune.ufoExplodeOnExit100 ? 1 : 0);
      end = true;
    }
    if (end) {
      addHistory(ufoHist_, today_, uint32_t(u.kind), u.destroyed, u.cost);
      out.event(DisasterEvent::Ended, DisasterType::UFO);
    } else keep.push_back(u);
  }
  ufos_.swap(keep);
}

static UfoInst* activeAttackUfo(std::vector<UfoInst>& v) {
  for (auto& u : v) if (u.phase >= 4) return &u;
  return nullptr;
}
static int* ufoCounter(UfoInst& u, int cat) {
  return cat == 1 ? &u.catLandmark : cat == 2 ? &u.catSpace : (cat == 3 || cat == 4) ? &u.catHigh : &u.catLow;
}

void DisasterManager::ufoBombHit(DisasterWorld& w, int x, int z, DisasterOutput& out) {
  UfoInst* u = activeAttackUfo(ufos_);
  if (!u || !w.inMap(x, z)) return;
  OccInfo o = w.occupantAt(x, z);
  if (!o.valid) return;
  int r1 = int(rng_.rand(100));
  int cat = ufoCategory(w, o);
  if (r1 <= tune.ufoPctFire) {
    if (cat == 0 && u->catLow < tune.ufoMaxLowTech && ignite(w, o, 0, out)) u->catLow++;
  } else if (int(rng_.rand(100)) <= tune.ufoPctDestroyed) {
    DisasterCommand& e = out.cmd(DisasterCommand::Explosion); e.x = x; e.z = z;
    if (o.flags & kOccOverWater) { DisasterCommand& s = out.cmd(DisasterCommand::Splash); s.x = x; s.z = z; }
    int64_t before = cost_;
    if (destroyOccupant(w, o, kDmgRubble, out)) {
      u->destroyed++; u->cost += cost_ - before; ++*ufoCounter(*u, cat);
    }
  }
}
void DisasterManager::ufoBeamHit(DisasterWorld& w, int x, int z, DisasterOutput& out) {
  UfoInst* u = activeAttackUfo(ufos_);
  if (!u || !w.inMap(x, z)) return;
  OccInfo o = w.occupantAt(x, z);
  if (!o.valid) return;
  int cat = ufoCategory(w, o);
  int64_t before = cost_;
  if (destroyOccupant(w, o, kDmgRubble, out)) { u->destroyed++; u->cost += cost_ - before; ++*ufoCounter(*u, cat); }
}
void DisasterManager::ufoAbduction() {
  if (UfoInst* u = activeAttackUfo(ufos_)) u->abductions++;
}
void DisasterManager::ufoCropCircle(DisasterWorld& w, int x, int z, DisasterOutput& out) {
  UfoInst* u = activeAttackUfo(ufos_);
  if (!u) return;
  for (int dz = 0; dz < 4; ++dz)
    for (int dx = 0; dx < 4; ++dx)
      if (!w.inMap(x + dx, z + dz) || !cropTile(w, x + dx, z + dz)) return;
  DisasterCommand& c = out.cmd(DisasterCommand::CropCircle); c.x = x; c.z = z;
  u->crops++;
}

// ---------------------------------------------------------------------------------------------
// Locusts (section 6.4)
// ---------------------------------------------------------------------------------------------
bool DisasterManager::startLocusts(DisasterWorld&, const DisasterInputs& in, bool warn, DisasterOutput& out) {
  if (int(locusts_.size()) >= tune.locMaxInstances || in.year < tune.locYearVisit) return false;
  LocustInst l;
  if (warn) { l.T1 = tune.locPrem1; l.T2 = tune.locPrem2; l.TA = tune.locAttack; l.phase = 0; }
  else { l.T1 = l.T2 = l.TA = 0; l.phase = 2; }
  locusts_.push_back(l);
  locustMonths_ = 0;
  today_ = in.jdn; lastIn_ = in;
  out.event(DisasterEvent::Started, DisasterType::Locusts);
  wasActive_ = true;
  return true;
}
bool DisasterManager::scriptLocusts(DisasterWorld& w, const DisasterInputs& in, int a0, DisasterOutput& out) {
  return startLocusts(w, in, a0 != 0, out);
}
void DisasterManager::locustTick(DisasterWorld& w, DisasterOutput& out) {
  std::vector<LocustInst> keep;
  for (auto l : locusts_) {
    l.counter++;
    bool end = false;
    if (l.phase == 0 && l.counter >= l.T1) { l.phase = 1; out.event(DisasterEvent::Premonition, DisasterType::Locusts, 0, 0, 1); }
    else if (l.phase == 1 && l.counter >= l.T2) { l.phase = 2; out.event(DisasterEvent::Premonition, DisasterType::Locusts, 0, 0, 2); }
    else if (l.phase == 2 && l.counter >= l.TA) {
      l.phase = 3;
      std::vector<std::pair<int, int>> crops;
      for (int x = 0; x < w.width() && int(crops.size()) < tune.locMaxCrops; ++x)
        for (int z = 0; z < w.height() && int(crops.size()) < tune.locMaxCrops; ++z)
          if (cropTile(w, x, z)) crops.emplace_back(x, z);
      int N = int(crops.size()), swarms;
      if (N == 0) swarms = 1 + int(rng_.rand(2));
      else {
        swarms = (N - 1) / std::max(1, tune.locSwarmToCrop) + 1;
        swarms = clampi(swarms, tune.locMinSwarms, std::max(tune.locMinSwarms, tune.locMaxSwarms));
      }
      if (lastIn_.cheatLocustSwarm) swarms = tune.locMaxCheat;
      DisasterCommand& c = out.cmd(DisasterCommand::LocustSwarm);
      c.a = swarms; c.pts = crops;
      l.counter = 0;
    } else if (l.phase == 3 && l.counter >= tune.locDuration) end = true;
    if (end) {
      addHistory(locustHist_, today_, uint32_t(l.eaten), l.eaten, l.cost);
      out.event(DisasterEvent::Ended, DisasterType::Locusts);
    } else keep.push_back(l);
  }
  locusts_.swap(keep);
}
void DisasterManager::locustEaten(DisasterWorld& w, int x, int z, DisasterOutput& out) {
  if (locusts_.empty() || !w.inMap(x, z)) return;
  OccInfo o = w.occupantAt(x, z);
  if (!o.valid) return;
  int64_t before = cost_;
  if (destroyOccupant(w, o, kDmgCrop, out)) { locusts_[0].eaten++; locusts_[0].cost += cost_ - before; }
}

// ---------------------------------------------------------------------------------------------
// Whirlpool (section 6.5)
// ---------------------------------------------------------------------------------------------
bool DisasterManager::startWhirlpool(DisasterWorld& w, const DisasterInputs& in, float m, bool warn, DisasterOutput& out) {
  m = std::max(0.f, std::min(1.f, m));
  if (m <= 0.f) return true;
  if (int(whirls_.size()) >= tune.whirlMax) return false;
  const int W = w.width(), H = w.height();
  // 8x8 all-water rectangles via prefix sums
  std::vector<int> ps(size_t(W + 1) * size_t(H + 1), 0);
  auto P = [&](int x, int z) -> int& { return ps[size_t(z) * size_t(W + 1) + size_t(x)]; };
  for (int z = 0; z < H; ++z)
    for (int x = 0; x < W; ++x)
      P(x + 1, z + 1) = P(x, z + 1) + P(x + 1, z) - P(x, z) + (w.isWater(x, z) ? 1 : 0);
  std::vector<std::pair<int, int>> rects, favoured;
  for (int z = 0; z + 8 <= H; ++z)
    for (int x = 0; x + 8 <= W; ++x)
      if (P(x + 8, z + 8) - P(x, z + 8) - P(x + 8, z) + P(x, z) == 64) rects.emplace_back(x, z);
  if (rects.empty()) return false;
  for (auto& r : rects) {
    bool hit = false;
    for (int dz = 0; dz < 8 && !hit; ++dz)
      for (int dx = 0; dx < 8 && !hit; ++dx) {
        OccInfo o = w.occupantAt(r.first + dx, r.second + dz);
        if (o.valid && (w.isA(o.agentType, 12330) || w.isA(o.agentType, 14430))) hit = true;
      }
    if (hit) favoured.push_back(r);
  }
  auto& pool = favoured.empty() ? rects : favoured;
  WhirlInst wi;
  bool placed = false;
  for (int attempt = 0; attempt < 16 && !placed; ++attempt) {
    auto r = pool[rng_.rand(uint32_t(pool.size()))];
    float px = float(r.first) + rng_.randRange(0.f, 2.f), pz = float(r.second) + rng_.randRange(0.f, 2.f);
    int tx = int(std::floor(px)), tz = int(std::floor(pz));
    if (w.inMap(tx, tz) && w.isWater(tx, tz)) { wi.px = px; wi.pz = pz; placed = true; }
  }
  if (!placed) return false;
  wi.mag = (float(tune.whirlMagMin) + float(tune.whirlMagMax - tune.whirlMagMin) * m) / 10.f;
  for (int i = 0; i < 4; ++i) wi.T[i] = warn ? tune.whirlT[i] : 0;
  whirls_.push_back(wi);
  today_ = in.jdn;
  out.event(DisasterEvent::Started, DisasterType::Whirlpool, int(wi.px), int(wi.pz));
  wasActive_ = true;
  return true;
}
bool DisasterManager::scriptWhirlpool(DisasterWorld& w, const DisasterInputs& in, int a0, int a1, DisasterOutput& out) {
  return startWhirlpool(w, in, std::max(0.f, std::min(1.f, float(a0) * 0.1f)), a1 != 0, out);
}
void DisasterManager::whirlTick(DisasterWorld&, DisasterOutput& out) {
  for (auto& wi : whirls_) {
    if (wi.phase >= 4) continue;
    wi.counter++;
    while (wi.phase < 4 && wi.counter >= wi.T[wi.phase]) {
      wi.phase++;
      if (wi.phase < 4) out.event(DisasterEvent::Premonition, DisasterType::Whirlpool, 0, 0, wi.phase);
      else {
        DisasterCommand& c = out.cmd(DisasterCommand::VortexSpawn);
        c.x = int(wi.px); c.z = int(wi.pz); c.f = wi.mag; c.a = tune.whirlDuration;
      }
    }
  }
}
void DisasterManager::vortexPosition(DisasterWorld& w, int x0, int z0, DisasterOutput& out) {
  if (whirls_.empty()) return;
  int R = tune.whirlRadius;
  for (int z = z0 - R; z < z0 + R; ++z)
    for (int x = x0 - R; x < x0 + R; ++x) {
      if (!w.inMap(x, z)) continue;
      OccInfo o = w.occupantAnchoredAt(x, z);
      if (!o.valid || !(w.isA(o.agentType, 12330) || w.isA(o.agentType, 14430))) continue;
      int64_t before = cost_;
      if (destroyOccupant(w, o, kDmgRubble, out)) {
        whirls_[0].destroyed++; whirls_[0].cost += cost_ - before;
        DisasterCommand& s = out.cmd(DisasterCommand::Sound); s.a = 240; s.x = x; s.z = z;
        DisasterCommand& sp = out.cmd(DisasterCommand::Splash); sp.x = x; sp.z = z;
        DisasterCommand& e = out.cmd(DisasterCommand::Explosion); e.x = x; e.z = z;
      }
    }
}
void DisasterManager::vortexObjectSucked(DisasterWorld& w, int x, int z, bool bridge, DisasterOutput& out) {
  if (whirls_.empty() || !w.inMap(x, z)) return;
  OccInfo o = w.occupantAt(x, z);
  if (o.valid && !bridge) {
    int64_t before = cost_;
    if (destroyOccupant(w, o, kDmgRubble, out)) { whirls_[0].destroyed++; whirls_[0].cost += cost_ - before; }
  } else if (bridge) {
    whirls_[0].destroyed++;  // network pieces removed by the caller's layers
  }
}
void DisasterManager::vortexFinished(const DisasterInputs& in, DisasterOutput& out) {
  if (whirls_.empty()) return;
  WhirlInst wi = whirls_.front();
  whirls_.erase(whirls_.begin());
  addHistory(whirlHist_, in.jdn, uint32_t(wi.mag * 10.f), wi.destroyed, wi.cost);
  out.event(DisasterEvent::Ended, DisasterType::Whirlpool);
  checkAllOver(out);
}

// ---------------------------------------------------------------------------------------------
// Space junk (section 6.6)
// ---------------------------------------------------------------------------------------------
bool DisasterManager::startSpaceJunk(DisasterWorld&, const DisasterInputs& in, float m, bool warn, DisasterOutput& out) {
  m = std::max(0.f, std::min(1.f, m));
  if (m <= 0.f) return true;
  if (int(junk_.size()) >= tune.junkMax) return false;
  JunkInst j;
  // [INFERRED] spec: stored magnitude is in tenths of m*10 yet is compared with 40/60/80; use hundredths of m.
  j.mag = clampi(int(m * 100.f), 1, 99);
  for (int i = 0; i < 5; ++i) j.T[i] = warn ? tune.junkT[i] : 0;
  j.phase = warn ? 0 : 4;
  if (!warn) { j.counter = tune.junkT[3]; j.T[4] = tune.junkT[4]; }
  junk_.push_back(j);
  today_ = in.jdn;
  out.event(DisasterEvent::Started, DisasterType::SpaceJunk);
  if (!warn) {
    DisasterCommand& c = out.cmd(DisasterCommand::DebrisSpawn);
    c.a = j.mag <= 40 ? 2000 : j.mag <= 60 ? 5000 : 10000; c.b = tune.junkDuration;
  }
  wasActive_ = true;
  return true;
}
bool DisasterManager::scriptSpaceJunk(DisasterWorld& w, const DisasterInputs& in, int a0, int a1, DisasterOutput& out) {
  return startSpaceJunk(w, in, std::max(0.f, std::min(1.f, float(a0) * 0.1f)), a1 == 1, out);
}
void DisasterManager::junkTick(DisasterWorld&, DisasterOutput& out) {
  std::vector<JunkInst> keep;
  for (auto j : junk_) {
    j.counter++;
    bool end = false;
    if (j.phase < 4 && j.counter >= j.T[j.phase]) {
      j.phase++;
      if (j.phase < 4) out.event(DisasterEvent::Premonition, DisasterType::SpaceJunk, 0, 0, j.phase);
      else {
        DisasterCommand& c = out.cmd(DisasterCommand::DebrisSpawn);
        c.a = j.mag <= 40 ? 2000 : j.mag <= 60 ? 5000 : 10000; c.b = tune.junkDuration;
      }
    }
    if (j.phase >= 4 && j.counter >= j.T[4] + 10) end = true;
    if (end) { addHistory(junkHist_, today_, uint32_t(j.mag), j.hits, 0); out.event(DisasterEvent::Ended, DisasterType::SpaceJunk); }
    else keep.push_back(j);
  }
  junk_.swap(keep);
}
void DisasterManager::debrisImpact(DisasterWorld& w, int x, int z, DisasterOutput& out) {
  if (!w.inMap(x, z)) return;
  int mag = junk_.empty() ? 50 : junk_[0].mag;
  if (!junk_.empty()) junk_[0].hits++;
  if (w.isWater(x, z)) { DisasterCommand& s = out.cmd(DisasterCommand::Sound); s.a = 250; s.x = x; s.z = z; return; }
  int adj = mag < 40 ? 10 : mag < 60 ? 0 : mag < 80 ? -10 : -20;
  int A = tune.junkA + adj, B = tune.junkB + adj;
  int r = int(rng_.rand(100));
  OccInfo o = w.occupantAt(x, z);
  { DisasterCommand& s = out.cmd(DisasterCommand::Sound); s.a = 243; s.x = x; s.z = z; }
  { DisasterCommand& s = out.cmd(DisasterCommand::Sound); s.a = 200; s.x = x; s.z = z; }
  if (o.valid && (o.flags & kOccOverWater)) { DisasterCommand& sp = out.cmd(DisasterCommand::Splash); sp.x = x; sp.z = z; }
  if (r <= B) return;
  if (r < A) { if (o.valid) ignite(w, o, 0, out); return; }
  double c = A >= 100 ? 0.0 : double(r - A) / (double(100 - A) * 0.1);
  if (c >= 9.0) {
    DisasterCommand& sh = out.cmd(DisasterCommand::ScreenShake); sh.a = 1000; sh.b = 0;
    DisasterCommand& s = out.cmd(DisasterCommand::Sound); s.a = 244; s.x = x; s.z = z;
  }
  int x0, x1, z0, z1;
  if (c <= 3.3) { x0 = x - 1; x1 = x + 2; z0 = z - 1; z1 = z + 2; }
  else if (c <= 6.7) { x0 = x - 2; x1 = x + 2; z0 = z - 2; z1 = z + 2; }
  else { x0 = x - 2; x1 = x + 3; z0 = z - 2; z1 = z + 3; }
  x0 = std::max(0, x0); z0 = std::max(0, z0); x1 = std::min(w.width() - 1, x1); z1 = std::min(w.height() - 1, z1);
  for (int zz = z0; zz <= z1; ++zz) for (int xx = x0; xx <= x1; ++xx) if (w.isWater(xx, zz)) return;
  int hs[4] = {w.terrainHeight(x0, z0), w.terrainHeight(x1 + 1, z0), w.terrainHeight(x0, z1 + 1), w.terrainHeight(x1 + 1, z1 + 1)};
  int hmin = *std::min_element(hs, hs + 4), hmax = *std::max_element(hs, hs + 4);
  DisasterCommand& cr = out.cmd(DisasterCommand::Crater);
  cr.pts.emplace_back(x0, z0); cr.pts.emplace_back(x1, z1);
  cr.a = int(c * 10.0); cr.b = (hmax - hmin <= tune.junkMaxHeightDiff) ? 1 : 0; cr.value = tune.junkCraterDecal;
}

// ---------------------------------------------------------------------------------------------
// Scripts, random starts, schedulers
// ---------------------------------------------------------------------------------------------
bool DisasterManager::scriptFire(DisasterWorld& w, int a0, int a1, DisasterOutput& out) {
  return startFire(w, clampi(a0 - 1, 0, w.width() - 1), clampi(a1 - 1, 0, w.height() - 1), out);
}

bool DisasterManager::startRandom(DisasterType t, DisasterWorld& w, const DisasterInputs& in, DisasterOutput& out) {
  today_ = in.jdn; lastIn_ = in;
  bool ok = false;
  switch (t) {
    case DisasterType::Fire: ok = startRandomFire(w, out); break;
    case DisasterType::Tornado: ok = startRandomTornado(w, out); break;
    case DisasterType::Earthquake: ok = startEarthquake(w, in, rng_.nextFloat() > 0 ? float(rng_.nextFloat()) : 0.5f, true, out); break;
    case DisasterType::Riot: {
      int x = int(rng_.rand(uint32_t(w.width()))), z = int(rng_.rand(uint32_t(w.height())));
      ok = startRiot(w, x, z, true, out); break;
    }
    case DisasterType::UFO: ok = startUfo(w, in, true, out); break;
    case DisasterType::ToxicCloud: {
      int x = int(rng_.rand(uint32_t(w.width()))), z = int(rng_.rand(uint32_t(w.height())));
      ok = startToxicCloud(w, x, z, true, out); break;
    }
    case DisasterType::Whirlpool: ok = startWhirlpool(w, in, float(rng_.nextFloat()), true, out); break;
    case DisasterType::Locusts: ok = startLocusts(w, in, true, out); break;
    case DisasterType::SpaceJunk: ok = startSpaceJunk(w, in, float(rng_.nextFloat()), true, out); break;
    case DisasterType::Parade: paradeActive = true; ok = true; break;
  }
  return ok;
}

void DisasterManager::stop(DisasterType t, DisasterOutput& out) {
  switch (t) {
    case DisasterType::Fire: burning_.clear(); fireSoundUpdate(out); break;
    case DisasterType::Tornado: tornadoes_.clear(); break;
    case DisasterType::Earthquake: quakes_.clear(); break;
    case DisasterType::Riot: riots_.clear(); break;
    case DisasterType::UFO: ufos_.clear(); break;
    case DisasterType::ToxicCloud: clouds_.clear(); break;
    case DisasterType::Whirlpool: whirls_.clear(); break;
    case DisasterType::Locusts: locusts_.clear(); break;
    case DisasterType::SpaceJunk: junk_.clear(); break;
    case DisasterType::Parade: paradeActive = false; break;
  }
  checkAllOver(out);
}

void DisasterManager::monthlyTick(DisasterWorld& w, const DisasterInputs& in, DisasterOutput& out) {
  today_ = in.jdn; year_ = in.year; lastIn_ = in;
  if (emergencyActive_ && ++emergencyMonths_ > tune.maxEmergencyDuration) endEmergency(out);
  if (!enabled_ || !in.disastersEnabled) { checkAllOver(out); return; }
  // Fire
  if (fireInterval_ == 0) fireInterval_ = drawRange(rng_, tune.fireIntervalMin, tune.fireIntervalMax);
  if (++fireCounter_ >= fireInterval_) {
    fireCounter_ = 0;
    fireInterval_ = drawRange(rng_, tune.fireIntervalMin, tune.fireIntervalMax);
    startRandomFire(w, out);
  }
  // Tornado
  if (tornadoInterval_ == 0) tornadoInterval_ = drawRange(rng_, tune.tornadoIntervalMin, tune.tornadoIntervalMax);
  if (++tornadoCounter_ >= tornadoInterval_) {
    tornadoCounter_ = 0;
    tornadoInterval_ = drawRange(rng_, tune.tornadoIntervalMin, tune.tornadoIntervalMax);
    startRandomTornado(w, out);
  }
  // Earthquake
  if (int(quakes_.size()) < tune.quakeMax) {
    seedHistory(quakeHist_, in.jdn);
    if (gapElapsed(quakeHist_, in.jdn, tune.quakeMinGapMonths) && int(rng_.rand(10000)) < tune.quakeChance)
      startEarthquake(w, in, float(rng_.nextFloat()), true, out);
  }
  // Riot
  ++riotCounter_;
  if (int(riots_.size()) < tune.maxRiots && in.unrest >= tune.riotUnrestThreshold) {
    float slope = (100 - tune.riotUnrestThreshold) > 0
                      ? float(tune.riotIntervalMax - tune.riotIntervalMin) / float(100 - tune.riotUnrestThreshold) : 0.f;
    int I = tune.riotIntervalMin + int(float(100 - in.unrest) * slope);
    if (I < tune.riotIntervalMin) I = tune.riotIntervalMin;
    int lo = int(float(I) * 0.8f), hi = int(float(I) * 1.2f);
    int eff = lo < hi ? rng_.rangeInt(lo, hi) : I;
    if (riotCounter_ >= eff) {
      riotCounter_ = 0;
      int active = int(riots_.size());
      int n = int((1.0f - float(in.aura + 128) / 256.0f) * float(tune.maxRiots - active));
      n = clampi(n, 1, std::max(1, tune.maxRiots));
      for (int i = 0; i < n; ++i) {
        int x = int(rng_.rand(uint32_t(w.width()))), z = int(rng_.rand(uint32_t(w.height())));
        startRiot(w, x, z, true, out);
      }
    }
  }
  // UFO
  if (int(ufos_.size()) < tune.ufoMaxInstances) {
    seedHistory(ufoHist_, in.jdn);
    if (gapElapsed(ufoHist_, in.jdn, tune.ufoMinMonths) && int(rng_.rand(10000)) < tune.ufoChance100)
      startUfo(w, in, true, out);
  }
  // Locusts
  if (locusts_.empty()) {
    ++locustMonths_;
    if (locustMonths_ > tune.locMinMonths) {
      int r = int(rng_.rand(10000));
      if (r < tune.locChance100 || locustMonths_ > tune.locMaxMonths) {
        bool crop = false;
        for (int x = 0; x < w.width() && !crop; ++x)
          for (int z = 0; z < w.height() && !crop; ++z) crop = cropTile(w, x, z);
        if (crop) startLocusts(w, in, true, out);
      }
    }
  }
  // Toxic cloud
  if (int(clouds_.size()) < tune.toxMaxInstances &&
      (!haveCloud_ || int64_t(in.jdn) - lastCloudJdn_ >= int64_t(tune.toxAvgMonthsBetween) * 30)) {
    // spec 21 s6.2 [STATIC]: floor(WeightForPollution x avgAir / MaxAirPolluteForUI), product first, signed 32-bit division
    int64_t score = in.pollutionNorm != 0 ? int64_t(int32_t(int32_t(tune.toxWeightPollution) * int32_t(in.pollutionTotal)) / int32_t(in.pollutionNorm)) : 0;
    if (in.ordCleanIndustry) score -= tune.toxWeightCleanInd;
    if (in.ordIndPollutantFee) score -= tune.toxWeightIndFee;
    if (in.ordWasteDisposal) score -= tune.toxWeightWaste;
    score += int64_t(tune.toxWeightPlant) * in.toxicWastePlants;
    if (score > tune.toxThreshold) {
      int per = tune.toxScorePerLevel == 0 ? 10 : tune.toxScorePerLevel;
      int level = int((score - tune.toxThreshold) / per);
      int x = int(rng_.rand(uint32_t(w.width()))), z = int(rng_.rand(uint32_t(w.height())));
      startToxicCloud(w, x, z, true, out, level);
    }
  }
  // Whirlpool
  if (int(whirls_.size()) < tune.whirlMax) {
    seedHistory(whirlHist_, in.jdn);
    if (gapElapsed(whirlHist_, in.jdn, tune.whirlMinGapMonths) && int(rng_.rand(10000)) < tune.whirlChance)
      startWhirlpool(w, in, float(rng_.nextFloat()), true, out);
  }
  // Space junk
  if (int(junk_.size()) < tune.junkMax && in.year >= tune.junkFirstYear) {
    seedHistory(junkHist_, in.jdn);
    if (gapElapsed(junkHist_, in.jdn, tune.junkMinGapMonths) && int(rng_.rand(10000)) < tune.junkChance)
      startSpaceJunk(w, in, float(rng_.nextFloat()), true, out);
  }
  checkAllOver(out);
}

void DisasterManager::dayTick(DisasterWorld& w, const DisasterInputs& in, DisasterOutput& out) {
  today_ = in.jdn; year_ = in.year; lastIn_ = in;
  burnTick(w, out);
  checkAllOver(out);
}

void DisasterManager::minuteTick(DisasterWorld& w, const DisasterInputs& in, DisasterOutput& out) {
  today_ = in.jdn; year_ = in.year; lastIn_ = in;
  burnTick(w, out);
  tornadoTick(w, out);
  quakeTick(w, out);
  riotTick(w, in, out);
  cloudTick(w, out);
  ufoTick(w, out);
  locustTick(w, out);
  whirlTick(w, out);
  junkTick(w, out);
  checkAllOver(out);
}

// ---------------------------------------------------------------------------------------------
// Strikes (section 11)
// ---------------------------------------------------------------------------------------------
int ServiceStrike::chancePer10000(int fundingPct, int stations, int stat) const {
  if (onStrike) return 10000;
  if (stations <= 0 || fundingPct >= minFunding || stat <= maxStat) return 0;
  int d = minFunding - fundingPct;
  return d * d;
}
bool ServiceStrike::update(int fundingPct, int stations, int stat, GameRng& rng) {
  if (onStrike) {
    ++counter;
    if (counter > maxDuration || fundingPct >= 100 || stations <= 0) { onStrike = false; counter = 0; }
    return false;
  }
  int chance = chancePer10000(fundingPct, stations, stat);
  if (chance > 0 && int(rng.rand(10000)) < chance) { onStrike = true; counter = 0; return true; }
  return false;
}

// ---------------------------------------------------------------------------------------------
// Dispatch (section 10.3)
// ---------------------------------------------------------------------------------------------
std::vector<CoverageCellDelta> dispatchStamp(int mapW, int mapH, int x, int z, int radius, int effect, int sign) {
  std::map<std::pair<int, int>, int> cells;
  for (int tz = z - radius; tz <= z + radius; ++tz)
    for (int tx = x - radius; tx <= x + radius; ++tx) {
      if (tx < 0 || tz < 0 || tx >= mapW || tz >= mapH) continue;
      int d = int(std::floor(std::sqrt(double((tx - x) * (tx - x) + (tz - z) * (tz - z)))));
      if (d > radius) continue;
      cells[{tx / 4, tz / 4}] += effect * sign;
    }
  std::vector<CoverageCellDelta> v;
  for (auto& kv : cells) v.push_back({kv.first.first, kv.first.second, kv.second});
  return v;
}

void DispatchTracker::dispatch(int x, int z, int stations, int mapW, int radius, int effect, uint32_t& nextId,
                               DisasterOutput& out) {
  if (int(units_.size()) >= stations + 1 && !units_.empty()) {
    Unit u = units_.front();
    units_.pop_front();
    DisasterCommand& rm = out.cmd(DisasterCommand::RemoveUnit); rm.a = service_; rm.id = u.id;
    DisasterCommand& st = out.cmd(DisasterCommand::StampCoverage);
    st.a = service_; st.x = u.x; st.z = u.z; st.b = u.radius; st.value = u.effect; st.f = -1.f;
  }
  Unit u{nextId++, x, z, radius, effect};
  units_.push_back(u);
  DisasterCommand& sp = out.cmd(DisasterCommand::SpawnUnit);
  sp.a = service_; sp.x = std::min(x + 1, mapW - 1); sp.z = z; sp.id = u.id;
  DisasterCommand& st = out.cmd(DisasterCommand::StampCoverage);
  st.a = service_; st.x = x; st.z = z; st.b = radius; st.value = effect; st.f = 1.f;
}

// ---------------------------------------------------------------------------------------------
// Persistence (spec 10 section 6.5)
// ---------------------------------------------------------------------------------------------
namespace {
void putHdr(Bytes& b, uint16_t ver) { put16(b, ver); put16(b, 2); put32(b, 0xDEADBEEFu); }
void putF(Bytes& b, float f) { uint32_t v; std::memcpy(&v, &f, 4); put32(b, v); }
bool getHdr(Reader& r, uint16_t maxVer, uint16_t& ver) {
  if (r.remaining() < 8) return false;
  ver = r.u16(); uint16_t fl = r.u16(); uint32_t s = r.u32();
  return s == 0xDEADBEEFu && fl == 2 && ver >= 1 && ver <= maxVer;
}
DisasterRecord mk(uint32_t group, Bytes b) {
  DisasterRecord r; r.type = kDisasterRecType; r.group = group; r.instance = 0; r.data = std::move(b); return r;
}
Bytes histBytes(const HistoryList& h, bool locust) {
  Bytes b; putHdr(b, h.version);
  if (locust && h.version >= 2) put32(b, uint32_t(h.extra));
  put32(b, uint32_t(h.entries.size()));
  for (auto& e : h.entries) for (int i = 0; i < h.entryWords; ++i) put32(b, size_t(i) < e.size() ? e[size_t(i)] : 0);
  return b;
}
bool histLoad(const Bytes& d, HistoryList& h, bool locust) {
  try {
    Reader r(d);
    uint16_t ver;
    if (!getHdr(r, locust ? 2 : 1, ver)) return false;
    HistoryList n = h;
    n.version = ver; n.extra = 0; n.entries.clear();
    if (locust && ver >= 2) n.extra = r.s32();
    uint32_t cnt = r.u32();
    if (cnt > 50000 || r.remaining() != size_t(cnt) * size_t(h.entryWords) * 4) return false;
    for (uint32_t i = 0; i < cnt; ++i) {
      HistoryEntry e(size_t(h.entryWords));
      for (auto& w : e) w = r.u32();
      n.entries.push_back(e);
    }
    h = n;
    return true;
  } catch (const FormatError&) { return false; }
}
}  // namespace

std::vector<DisasterRecord> DisasterManager::saveRecords() const {
  std::vector<DisasterRecord> v;
  {  // layer
    Bytes b; put16(b, 0x01F9); put16(b, 2); put32(b, 0xDEADBEEFu);
    put32(b, layerReserved0); put32(b, emergencyActive_ ? 1 : 0); put32(b, layerFlagA);
    put32(b, uint32_t(cost_)); put32(b, layerFlagB);
    DisasterRecord r; r.type = kDisasterLayerType; r.group = kDisasterLayerGroup; r.instance = 0; r.data = b;
    v.push_back(r);
  }
  {  // fire
    Bytes b; putHdr(b, 1); put32(b, uint32_t(burning_.size()));
    for (auto& f : burning_) { put32(b, uint32_t(f.x)); put32(b, uint32_t(f.z)); put32(b, uint32_t(f.intensity)); }
    v.push_back(mk(uint32_t(DisasterType::Fire), b));
  }
  {  // tornado
    Bytes b; putHdr(b, 1); put32(b, uint32_t(tornadoes_.size()));
    for (auto& t : tornadoes_) {
      put32(b, uint32_t(int(t.px))); put32(b, uint32_t(int(t.pz)));
      putF(b, t.dx); putF(b, t.dz); putF(b, t.speed);
      b.push_back(uint8_t(int8_t(t.variant)));
      putF(b, float(t.phase)); put32(b, uint32_t(t.elapsed)); put32(b, uint32_t(t.lifetime));
    }
    v.push_back(mk(uint32_t(DisasterType::Tornado), b));
  }
  {  // riot
    Bytes b; putHdr(b, 1); put32(b, uint32_t(riots_.size()));
    for (auto& r : riots_) {
      put32(b, uint32_t(r.x)); put32(b, uint32_t(r.z)); put32(b, uint32_t(r.phase));
      put32(b, uint32_t(r.counter)); put32(b, uint32_t(r.strength));
    }
    v.push_back(mk(uint32_t(DisasterType::Riot), b));
  }
  {  // toxic cloud
    Bytes b; putHdr(b, 1); put32(b, uint32_t(clouds_.size()));
    for (auto& c : clouds_) {
      put32(b, uint32_t(c.x)); put32(b, uint32_t(c.z)); put32(b, uint32_t(c.phase));
      putF(b, c.dx); putF(b, c.dz); putF(b, c.speed); put32(b, uint32_t(c.counter)); put32(b, uint32_t(c.lifetime));
    }
    v.push_back(mk(uint32_t(DisasterType::ToxicCloud), b));
  }
  v.push_back(mk(kGroupEarthquake, histBytes(quakeHist_, false)));
  v.push_back(mk(kGroupUfo, histBytes(ufoHist_, false)));
  v.push_back(mk(kGroupSpaceJunk, histBytes(junkHist_, false)));
  v.push_back(mk(kGroupLocust, histBytes(locustHist_, true)));
  v.push_back(mk(kGroupWhirlpool, histBytes(whirlHist_, false)));
  {  // parade
    Bytes b; putHdr(b, parade_.version);
    put32(b, parade_.a); b.push_back(parade_.b); put32(b, parade_.c); put32(b, parade_.x); put32(b, parade_.z);
    b.push_back(parade_.d);
    for (int i = 0; i < 4; ++i) put32(b, uint32_t(parade_.route[i]));
    if (parade_.version >= 2) put32(b, parade_.e);
    v.push_back(mk(uint32_t(DisasterType::Parade), b));
  }
  return v;
}

bool DisasterManager::loadRecord(const DisasterRecord& rec) {
  try {
    const Bytes& d = rec.data;
    Reader r(d);
    uint16_t ver;
    if (rec.type == kDisasterLayerType && rec.group == kDisasterLayerGroup) {
      if (d.size() != 28) return false;
      ver = r.u16(); uint16_t fl = r.u16(); uint32_t s = r.u32();
      if (s != 0xDEADBEEFu || fl != 2 || ver > 0x01F9) return false;
      layerReserved0 = r.u32(); emergencyActive_ = r.u32() != 0; layerFlagA = r.u32();
      cost_ = r.u32(); layerFlagB = r.u32();
      return true;
    }
    if (rec.type != kDisasterRecType) return false;
    switch (rec.group) {
      case uint32_t(DisasterType::Fire): {
        if (!getHdr(r, 1, ver)) return false;
        int32_t n = r.s32();
        if (n < 0 || r.remaining() != size_t(n) * 12) return false;
        std::vector<BurnRecord> v;
        for (int i = 0; i < n; ++i) {
          BurnRecord b; b.x = int(r.u32()); b.z = int(r.u32()); b.intensity = clampi(r.s32(), 0, 2);
          b.duration = drawRange(rng_, tune.stageMin, tune.stageMax);
          v.push_back(b);
        }
        burning_ = v; return true;
      }
      case uint32_t(DisasterType::Tornado): {
        if (!getHdr(r, 1, ver)) return false;
        int32_t n = r.s32();
        if (n < 0 || r.remaining() != size_t(n) * 33) return false;
        std::vector<TornadoInst> v;
        for (int i = 0; i < n; ++i) {
          TornadoInst t;
          t.px = float(r.u32()) + 0.5f; t.pz = float(r.u32()) + 0.5f;
          t.dx = r.f32(); t.dz = r.f32(); t.speed = r.f32(); t.variant = clampi(int(int8_t(r.u8())), 0, 2);
          t.unknown = r.f32(); t.phase = clampi(int(t.unknown), 0, 5);
          t.elapsed = int(r.u32()); t.lifetime = int(r.u32());
          for (int k = 0; k < 5; ++k) t.warn[k] = tune.tornadoWarn[k];
          t.counter = 0;
          v.push_back(t);
        }
        tornadoes_ = v; return true;
      }
      case uint32_t(DisasterType::Riot): {
        if (!getHdr(r, 1, ver)) return false;
        int32_t n = r.s32();
        if (n < 0 || r.remaining() != size_t(n) * 20) return false;
        std::vector<RiotInst> v;
        for (int i = 0; i < n; ++i) {
          RiotInst x; x.x = int(r.u32()); x.z = int(r.u32()); x.phase = clampi(r.s32(), 0, 2);
          x.counter = int(r.u32()); x.strength = int(r.u32());
          v.push_back(x);
        }
        riots_ = v; return true;
      }
      case uint32_t(DisasterType::ToxicCloud): {
        if (!getHdr(r, 1, ver)) return false;
        int32_t n = r.s32();
        if (n < 0 || r.remaining() != size_t(n) * 32) return false;
        std::vector<CloudInst> v;
        for (int i = 0; i < n; ++i) {
          CloudInst c; c.x = int(r.u32()); c.z = int(r.u32()); c.phase = r.s32();
          c.dx = r.f32(); c.dz = r.f32(); c.speed = r.f32(); c.counter = int(r.u32()); c.lifetime = int(r.u32());
          if (c.phase >= 4) { c.phase = 3; c.counter = tune.toxVisible; }  // puffs are not saved: re-form on load
          v.push_back(c);
        }
        clouds_ = v; return true;
      }
      case kGroupEarthquake: return histLoad(d, quakeHist_, false);
      case kGroupUfo: return histLoad(d, ufoHist_, false);
      case kGroupSpaceJunk: return histLoad(d, junkHist_, false);
      case kGroupLocust: return histLoad(d, locustHist_, true);
      case kGroupWhirlpool: return histLoad(d, whirlHist_, false);
      case uint32_t(DisasterType::Parade): {
        if (!getHdr(r, 2, ver)) return false;
        size_t need = ver >= 2 ? 38 : 34;
        if (r.remaining() != need) return false;
        ParadeRecord p; p.version = ver;
        p.a = r.u32(); p.b = r.u8(); p.c = r.u32(); p.x = r.u32(); p.z = r.u32(); p.d = r.u8();
        for (int i = 0; i < 4; ++i) {
          p.route[i] = r.s32();
          if (uint32_t(p.route[i]) == 0xCDCDCDCDu) p.route[i] = 0;  // uninitialised filler: write zeros
        }
        if (ver >= 2) p.e = r.u32();
        parade_ = p; return true;
      }
    }
    return false;
  } catch (const FormatError&) { return false; }
}

}  // namespace m3k
