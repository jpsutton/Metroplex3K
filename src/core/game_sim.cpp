// Game <-> simulation glue: sim stepping, disasters, cheats, scenarios, street sim hooks.
#include <algorithm>

#include "core/game.h"
#include "core/game_scenario.h"
#include "core/records.h"
#include "core/tiler.h"

namespace m3k {

namespace {

// Neighbour-deal hook of the utilities advisor (spec 22 s3.13.1): offer terms come from NeighbourDeals::makeOffer.
class UtilityDeals : public DealHandler {
 public:
  explicit UtilityDeals(Game& g) : g_(g) {}
  bool dealActive(int type) override { return g_.city().neighbours.dealOfType(type); }
  int dealAgeMonths(int type) override {
    for (const auto& d : g_.city().neighbours.active) if (d.dealType == type && !d.terminated()) return d.monthCounter;
    return 0;
  }
  bool eligibleNeighbour(int type) override {
    SimRng probe = rng_;   // the neighbour pick draws a random number; do not consume it for a mere eligibility test
    return g_.city().neighbours.chooseNeighbour(type, context(type), probe) >= 0;
  }
  bool offersDisabled() override { return !g_.city().neighbours.newDealsEnabled; }
  bool offerStillValid(int type) override {
    for (const auto& d : g_.city().neighbours.pending) if (d.dealType == type) return true;
    return false;
  }
  bool generateOffer(int type) override { return g_.city().neighbours.makeOffer(type, context(type), rng_).has_value(); }
  void clearOffer(int type) override {
    auto& p = g_.city().neighbours.pending;
    p.erase(std::remove_if(p.begin(), p.end(), [&](const Deal& d) { return d.dealType == type; }), p.end());
  }
  // Petition ids 0xA5-0xCA are the per-type offer variants of the table in spec 22 s3.13.1; -1 for anything else.
  static int typeOfPetition(uint32_t id) {
    static const struct { uint32_t id; int type; } t[] = {
        {0xB3, 1}, {0xB4, 1}, {0xB5, 1}, {0xAC, 0}, {0xAD, 0}, {0xCA, 0}, {0xB9, 3}, {0xBA, 3}, {0xBB, 3},
        {0xA5, 2}, {0xA6, 2}, {0xA7, 2}, {0xBF, 5}, {0xC0, 5}, {0xC1, 5}, {0xC4, 4}, {0xC5, 4}, {0xF7, 4}};
    for (const auto& e : t) if (e.id == id) return e.type;
    return -1;
  }
 private:
  OfferContext context(int type) {
    City& c = g_.city();
    OfferContext x;
    x.cityPopulation = uint32_t(std::max(0, 2 * c.population()));
    x.currentYear = c.clock.date().year;
    x.referenceRate = 0;   // [OPEN] the neighbour's reference rate is not modelled
    for (const auto& o : g_.world().occupants) if (o.agentType == 12330) { x.hasSeaport = true; break; }
    const int64_t pc = c.power.stats().produced, pu = c.power.stats().consumed;
    const int64_t wc = c.water.stats.supply, wu = c.water.stats.used;
    int64_t avail = 0;
    if (type == 1) avail = pc - pu; else if (type == 3) avail = wc - wu;
    x.availableToSell = uint32_t(std::max<int64_t>(0, avail));
    x.powered = [pc](int, int) { return pc > 0; };
    x.watered = [wc](int, int) { return wc > 0; };
    return x;
  }
  Game& g_;
  SimRng rng_{7};
};

// DisasterWorld adapter over World / City.
class CityDisasterWorld : public DisasterWorld {
 public:
  CityDisasterWorld(World& w, City& c) : w_(w), c_(c) {}
  int width() const override { return w_.W; }
  int height() const override { return w_.H; }
  OccInfo occupantAt(int x, int z) const override {
    OccInfo o;
    int i = c_.occupantAt(x, z);
    if (i < 0 || i >= int(w_.occupants.size())) {
      // Network pieces are occupants with a class flag word (spec 13 s3.1a) [STATIC]: 0x4 | 0x800 (network piece), 0x4000
      // for all but pipes and plain power lines, plus 0x8000 road. Road pieces are riot sites and pedestrian spawns.
      if (!w_.inTiles(x, z)) return o;
      const uint16_t tf = c_.traffic ? c_.traffic->flags(x, z) : 0;
      const bool road = (tf & TF_ROAD) != 0, hwy = (tf & TF_HIGHWAY) != 0, rail = (tf & TF_RAIL) != 0;
      const bool power = c_.isPowerLine(x, z);
      if (!road && !hwy && !rail && !power) return o;
      o.valid = true; o.ax = x; o.az = z; o.w = o.h = 1;
      o.flags = 0x4u | kOccIndestructible | ((road || hwy || rail) ? 0x4000u : 0u) | (road ? kOccCrowdSite : 0u) |
                (hwy ? 0x10000u : 0u) | (rail ? 0x20000u : 0u) | (power ? 0x400000u : 0u);
      return o;
    }
    const Occupant& oc = w_.occupants[i];
    o.valid = true; o.id = oc.uid; o.ax = oc.x; o.az = oc.z; o.w = oc.sizeX; o.h = oc.sizeZ;
    o.agentType = oc.agentType;
    o.value = c_.budget.placementCost(oc.agentType, c_.economyHierarchy(), false);   // the building's price, summed into the disaster's cost [INFERRED: the original reads its cost attribute]
    o.burning = oc.state == 16;
    // Spec 13 §3.1a run-time class flag word; generic buildings = 0x104 (Q-BC).
    o.flags = oc.classFlags ? oc.classFlags : runtimeOccupantFlags(oc.agentType);
    // Zoned building lots (agent types 3000-9999) are "lots" for the destroy and UFO rules; farm fields are the filler
    // members flagged 0x100|0x400 of the farm family (spec 15 s4.3.2).
    const uint32_t kind = oc.agentType / 1000;
    o.isLot = kind >= 3 && kind <= 9;
    if (o.isLot && oc.agentType == 9300 && c_.gameData())
      if (const OccAttr* a = c_.gameData()->occupant(oc.occId)) o.crop = (a->buildingFlags & 0x500) == 0x500;
    o.lotDamage = oc.state == 8 ? kDmgRubble : oc.state == 16 ? kDmgBurnt : oc.state == 2 ? kDmgAbandon : 0;
    return o;
  }
  std::vector<OccInfo> occupantsTouching(const OccInfo& o) const override {
    std::vector<OccInfo> out;
    auto add = [&](int x, int z) {
      OccInfo t = occupantAt(x, z);
      if (!t.valid || t.id == o.id) return;
      for (const auto& e : out) if (e.id == t.id) return;
      out.push_back(t);
    };
    for (int i = 0; i < o.w; i++) { add(o.ax + i, o.az - 1); add(o.ax + i, o.az + o.h); }
    for (int j = 0; j < o.h; j++) { add(o.ax - 1, o.az + j); add(o.ax + o.w, o.az + j); }
    return out;
  }
  bool inGroup(const OccInfo& o, int group) const override { return c_.agentHierarchy().isAncestorOrSelf(o.agentType, uint32_t(group)); }
  bool isA(uint32_t agentType, uint32_t ancestor) const override { return c_.agentHierarchy().isAncestorOrSelf(agentType, ancestor); }
  bool isWater(int x, int z) const override {
    if (!w_.inTiles(x, z)) return false;
    return w_.altAt(x, z) < w_.waterAt(x, z);
  }
  int terrainHeight(int x, int z) const override { return w_.inTiles(x, z) ? w_.altAt(x, z) : 0; }
  int zoneType(int x, int z) const override { return w_.inTiles(x, z) ? w_.zone[w_.tIdx(x, z)] : 0; }
  int flammability(int x, int z) const override {
    if (!w_.inTiles(x, z)) return 0;
    const auto& g = c_.flammability.grid;
    size_t i = w_.tIdx(x, z);
    if (i < g.size() && g[i]) return g[i];
    return c_.occupantAt(x, z) >= 0 ? 120 : 0;   // layer not run yet: wood-ish default on occupied tiles
  }
  int fireCoverage(int x, int z) const override { return w_.inTiles(x, z) ? c_.fire.coverageAt(x, z) : 0; }
  int policeCoverage(int x, int z) const override { return w_.inTiles(x, z) ? c_.police.coverageAt(x, z) : 0; }
 private:
  World& w_;
  City& c_;
};

DisasterInputs inputsFor(City& c, bool enabled, const CheatRegistry& cheats) {
  DisasterInputs in;
  in.jdn = c.clock.jdn();
  in.year = c.clock.date().year;
  in.disastersEnabled = enabled;
  // spec 29 section 2.6 interim sources: U = unemployment %, A = city aura.
  in.unrest = std::max(0, std::min(100, c.valves.unemploymentPct()));
  in.aura = int(c.aura.cityAura);
  // Toxic-cloud pollution term (spec 21 s6.2) [STATIC]: the pollution record's avgAir over SC3Pollution MaxAirPolluteForUI.
  in.pollutionTotal = int64_t(c.pollution.avgAir);
  in.pollutionNorm = c.pollution.params().maxAir;
  in.ordCleanIndustry = c.ordinances.enacted(ord::CleanIndustry);
  in.ordIndPollutantFee = c.ordinances.enacted(ord::IndPollutantFee);
  in.ordWasteDisposal = c.ordinances.enacted(ord::IndWasteDisposal);
  for (const auto& o : c.world()->occupants) if (o.agentType == 19001 && o.state == 1) in.toxicWastePlants++;
  in.cheatUfoSwarm = cheats.active("UFO Swarm"); in.cheatLocustSwarm = cheats.active("Locust Swarm");
  return in;
}

}  // namespace

void Game::setDisastersOn(bool on) {
  disastersOn_ = on;
  disasters_.setDisastersEnabled(on);
  if (simReady_) city().sched.disastersEnabled = on;   // monthly item 1 gate (Q-BD / Q-BE)
}

void Game::initManagers() {
  disasters_.seed(uint32_t(world_.W) * 2654435761u + 7u);
  managersReady_ = true;
  lastMonth_ = city().clock.date().month;
  lastWeekDay_ = -1;
  petWeekFlag_ = false;
  // Spec 14 §6.1 / Q-BE [STATIC]: monthly DM is a dedicated schedule entry before the shared pass.
  // City::wire leaves it unwired (Game owns DisasterManager); plug it in here so runMonth order is
  // DM → neighbours → … rather than neighbours/4–16 then a post-hoc monthlyTick.
  city().sched.disastersEnabled = disastersOn_;
  // The four Game-bound steps below touch Game-owned state (disasters, scenario, UI effects), so when the tick runs
  // on the worker they run on the owner thread while the worker waits (callOnOwner is inline when synchronous).
  city().sched.setHandler(Step::MonthlyDisasterManager, [this] {
    simWorker_.callOnOwner([this] {
      CityDisasterWorld dw(world_, city());
      DisasterOutput out;
      disasters_.monthlyTick(dw, inputsFor(city(), disastersOn_, cheats_), out);
      applyDisasterOutput(out);
    });
  });
  // Spec 14 §6.1 / Q-BI [STATIC]: items 1–5 in creation order inside the shared daily pass.
  city().sched.setHandler(Step::DailyDisasters, [this] {
    simWorker_.callOnOwner([this] {
      DisasterOutput out;
      disasters_.emergencyDayTick(inputsFor(city(), disastersOn_, cheats_), out);
      applyDisasterOutput(out);
    });
  });
  city().sched.setHandler(Step::DailyScenario, [this] {
    simWorker_.callOnOwner([this] { if (scen_) scen_->dayTick(); });
  });
  // Spec 14 §6.1 / Q-BJ [STATIC]: item 7 after daily 1–5, before self-scheduled (gated by disasterActive_).
  city().sched.setHandler(Step::DailyDisasterInstances, [this] {
    simWorker_.callOnOwner([this] {
      CityDisasterWorld dw(world_, city());
      DisasterOutput out;
      disasters_.instanceDayTick(dw, inputsFor(city(), disastersOn_, cheats_), out);
      applyDisasterOutput(out);
    });
  });
  // Ticker text carries %CITYNAME%/%YOURCITY%/%MAYOR%... tags; expand when the item is created.
  auto textFn = [this](uint32_t t, uint32_t i) { return expandTokens(str(t, i), env().tc); };
  tickerDb_ = TickerDb();
  if (haveData_) {
    IniFile nc;
    if (data_.ini().load("SC3Newscast.INI", nc)) {
      for (const auto& name : TickerDb::fileList(nc)) {
        IniFile f;
        if (data_.ini().load(name, f)) tickerDb_.addFile(f);
      }
    }
    AdvisorThresholds thr;
    for (uint32_t id : allAdvisorIds()) {
      IniFile f;
      if (data_.ini().load(advisorIniName(id), f)) { thr.loadAdvisorIni(id, f); if (id == kAdvPlanner) thr.loadBuildingLists(f); }
    }
    advisors_.thr = thr;
    IniFile pm;
    if (data_.ini().load("PetitionerManager.ini", pm)) {
      petitioners_.config.loadManagerIni(pm);
      for (const auto& name : PetitionerConfig::extraInfoFiles(pm)) {
        IniFile f;
        if (data_.ini().load(name, f)) petitioners_.config.addExtraInfo(f);
      }
    }
    IniFile moods, gold, gfx;
    bool haveGold = data_.ini().load("GoldAdvisorMoods.INI", gold);
    if (data_.ini().load("AdvisorMoods.INI", moods)) advisors_.moods.load(moods, haveGold ? &gold : nullptr);
    if (data_.ini().load("AdvisorTopicGfx.INI", gfx)) advisors_.gfx.load(gfx);
  }
  news_.setDb(&tickerDb_);
  news_.setTextFn(textFn);
  news_.clear();
  sink_ = std::make_unique<NewsTickerSink>(news_, [this] { return int32_t(city().clock.jdn()); });
  sink_->text = textFn;
  advisors_.setSink(sink_.get());
  advisors_.petitioners = &petitioners_;
  dealHandler_ = std::make_unique<UtilityDeals>(*this);
  advisors_.deals = dealHandler_.get();
  advisors_.clear();
  petitioners_.setSink(sink_.get());
  petitioners_.clear();
  petShown_.clear();
  petView_.reset();
  // The pending petitions travel with the city (spec 22 P.2). They are listed in the Meet window but not re-presented
  // as pop-ups on load.
  if (world_.raw)
    if (const InnerRecord* pr = world_.raw->findInner({kPetitionerRecType, kPetitionerRecGroup, kPetitionerRecInstance}))
      if (petitioners_.load(pr->data)) for (const auto& p : petitioners_.list()) petShown_.push_back(p.id);
  advisors_.initBuildingUnlocks(city().clock.date().year);
  advisors_.initTransportFlags(city().clock.date().year);   // spec 22 "Building availability": unlocked at load
}

void Game::fillAdvisorInputs(AdvisorInputs& in) {
  City& c = city();
  in.population = c.population();
  in.year = c.clock.date().year;
  in.mapWidth = world_.W; in.mapHeight = world_.H;
  in.budget.treasury = sim_.funds;
  for (int i = 0; i < 3; i++) in.budget.taxRate[i] = c.budget.tax[i].rate;
  in.aura = int(c.aura.cityAura);
  in.plan.demand[0] = sim_.valveR.value; in.plan.demand[1] = sim_.valveC.value; in.plan.demand[2] = sim_.valveI.value;
  in.plan.treasury = sim_.funds;
  in.plan.monthIndex = c.clock.date().month - 1;
  in.trans.nRoad = c.roadCount();
  int64_t totalB = 0;
  int unpowered = 0, needPower = 0;
  for (const auto& o : world_.occupants) {
    totalB++;
    if ((o.state & ~0x200u) != 1) continue;
    switch (o.agentType) {
      case 13130: in.safety.policeStations++; break;
      case 13002: in.safety.fireStations++; break;
      case 13881: in.safety.jails++; break;
      case 13310: in.demo.nHospital++; break;
      case 13010: in.demo.nSchool++; break;
      case 13873: in.demo.nCollege++; break;
      case 14600: in.demo.nStadium++; break;
      case 12310: in.trans.hasAirport = true; in.trans.airportBuildings++; break;
      case 12330: in.env.hasSeaport = true; break;
      case 12210: in.trans.nBus++; in.trans.hasBusStations = true; break;
      case 12230: in.trans.n12230++; in.trans.nStations++; break;
      case 12360: in.trans.n12360++; in.trans.hasSubway = true; break;
      case 12361: in.trans.n12361++; in.trans.hasSubway = true; break;
      default: break;
    }
    if (o.agentType >= 3000 && o.agentType < 10000) { needPower++; if (!c.powered(o.x, o.z)) unpowered++; }
    if (us::powerCategory(o.agentType) >= 0) in.util.nPlants++;
    // spec 22 s3.13 [STATIC]: nW = water facilities (12007 family: pump, treatment, desalination, tower); nI = incinerators
    // + waste-to-energy plants (WTE counts as both a plant and an incinerator).
    if (o.agentType == 12810 || o.agentType == 12840 || o.agentType == 12830 || o.agentType == 12820) in.util.nPumps++;
    if (o.agentType == 12852 || o.agentType == 12853) in.util.nIncin++;
  }
  in.plan.totalBuildings = totalB;
  in.util.unpoweredBuildings = unpowered; in.util.buildingsNeedingPower = needPower;
  in.util.powerUsed = c.power.stats().consumed; in.util.powerCap = c.power.stats().produced;
  in.util.treasury = sim_.funds;
  if (const Department* d = c.budget.dept(dept::Police)) in.safety.policeFunding = d->funding;
  if (const Department* d = c.budget.dept(dept::Fire)) in.safety.fireFunding = d->funding;
  if (const Department* d = c.budget.dept(dept::Roads)) in.trans.roadFunding = d->funding;
  if (const Department* d = c.budget.dept(dept::Transit)) in.trans.transitFunding = d->funding;
  if (const Department* d = c.budget.dept(dept::Health)) in.demo.healthFunding = d->funding;
  if (const Department* d = c.budget.dept(dept::Education)) in.demo.eduFunding = d->funding;
  in.neighbourConnections = 0;
  {  // neighbour connections and deals
    const NeighbourDeals& nd = c.neighbours;
    bool anyPower = false, anyPipe = false;
    for (const auto& nb : nd.neighbours) {
      if (nb.nRoad + nb.nRail + nb.nPower + nb.nPipe + nb.nHighway + nb.nSubway > 0) in.neighbourConnections++;
      anyPower = anyPower || nb.nPower > 0; anyPipe = anyPipe || nb.nPipe > 0;
    }
    in.util.neighbours = in.neighbourConnections;
    in.util.powerLineConnection = anyPower; in.util.pipeConnection = anyPipe;
    for (const auto& d : nd.active) {
      if (d.terminated()) continue;
      in.budget.anyNeighbourDealActive = true;
      switch (d.dealType) {
        case 0: in.util.dealBuyPower = true; break;
        case 1: in.util.dealSellPower = true; break;
        case 2: in.util.dealBuyWater = true; break;
        case 3: in.util.dealSellWater = true; break;
        case 4: in.env.trashExportDealActive = in.env.anyTrashDeal = true; break;
        case 5: in.budget.trashImportDealKind5Exists = in.env.trashImportDealExists = in.env.anyTrashDeal = true; break;
        default: break;
      }
    }
  }
  in.util.waterUsed = c.water.stats.used; in.util.waterCap = c.water.stats.supply;
  in.util.powerDemand = in.util.powerUsed;   // [INFERRED] demand = consumption actually served
  {
    // [INFERRED] garbage capacity GC = what the disposal stages took this month plus what they could still take
    const auto& ps = c.pollution;
    in.util.garbageProduced = ps.disp.month[0];
    in.util.garbageCap = ps.disp.month[4] + ps.disp.month[5] + ps.disp.month[6] + ps.disp.wteBudget + ps.disp.incineratorBudget + ps.disp.landfillRoom();
  }
  for (const auto& o : world_.occupants)
    if ((o.state & ~0x200u) == 1 && o.agentType >= 3000 && o.agentType < 10000 && !c.watered(o.x, o.z)) in.util.dryBuildings++;
  in.plan.landValue = c.landValue.cityAverage();
  in.safety.curfewEnacted = c.ordinances.enacted(ord::YouthCurfew);
  in.safety.youthSportsEnacted = c.ordinances.enacted(ord::YouthSports);
  in.demo.healthStrike = c.pop.healthStrike; in.demo.schoolStrike = c.pop.schoolStrike; in.demo.collegeStrike = c.pop.collegeStrike;
  // Indices on the graph scales (spec 22 s3.8/3.9/3.10.1, metric identities INFERRED there): city-wide air and water
  // pollution, crime, road traffic and transit usage as the subsystems' monthly averages.
  in.env.airPollution = int(c.pollution.avgAir); in.env.waterPollution = int(c.pollution.avgWater);
  in.plan.airPollution = in.env.airPollution; in.plan.waterPollution = in.env.waterPollution;
  in.safety.crime = int(c.crime.avgCrime);
  in.safety.policeStrike = c.police.strike.striking; in.safety.fireStrike = c.fire.strike.striking;
  // Strike warnings = the layers' strike chance per tick / 100 (spec 22 s3.9) [STATIC police; INFERRED fire].
  in.safety.policeWarn = int(c.police.lastStrikeChance / 100); in.safety.fireWarn = int(c.fire.lastStrikeChance / 100);
  for (const auto& j : c.police.jails()) { in.safety.jailInmates += j.inmates; in.safety.jailCapacity += j.capacity; }
  in.safety.policeCoverage = int(c.police.pctCovered);
  in.safety.fireCoverage = int(c.fire.pctCovered);
  in.safety.jailUnjailed = c.police.unjailed;
  // EQ and LE as the advisors read them (spec 22 s3.12, 20 s4.5) [STATIC]: unweighted means over ages 20-65, not Q(0,89).
  in.demo.lifeExpectancy = c.pop.qLE(20, 65); in.demo.eq = c.pop.qEQ(20, 65);
  in.plan.lifeExpectancy = c.pop.qLE(20, 65); in.plan.eq = c.pop.qEQ(20, 65);
  if (c.traffic) {
    const auto& tr = c.traffic->rec();
    in.trans.roadTraffic = int(tr.avgRoad); in.trans.railUsage = int(tr.avgTransit);
    in.trans.failedCommutes = double(tr.failureRatio);
    in.trans.transitStrike = tr.strikeState == STRIKE_STRIKING;
    in.trans.transitWarn = int(tr.strikeMonths);
    for (int x = 0; x < world_.W; x++)
      for (int z = 0; z < world_.H; z++) {
        uint16_t f = c.traffic->flags(x, z);
        if (f & TF_RAIL) in.trans.railTiles++;
        if (f & TF_SUBWAY) in.trans.subwayTiles++;
        if (f & TF_HIGHWAY) in.trans.hasHighway = true;
      }
    in.trans.nTransit = in.trans.railTiles + in.trans.subwayTiles;
  }
  in.trans.roadDamage = c.roadCondition();   // spec 22 s3.10.1 / 12 s10 (NaN with no road pieces, as in the original)
  in.trans.carpoolEnacted = c.ordinances.enacted(ord::Carpool);
  in.trans.crossingGuardsEnacted = c.ordinances.enacted(ord::CrossingGuards);
  for (auto z : world_.zone) if (z == 14) in.trans.airportZoneTiles++;
  for (const auto& o : world_.occupants) {
    if (o.agentType == 12810 || o.agentType == 12840 || o.agentType == 12830) in.env.hasWaterSystem = true;
  }
  {  // neighbour sides with any / road / rail connection
    for (const auto& nb : c.neighbours.neighbours) {
      if (nb.nRoad + nb.nRail + nb.nPower + nb.nPipe + nb.nHighway + nb.nSubway > 0) in.plan.K++;
      if (nb.nRoad + nb.nHighway > 0) in.plan.Kr++;
      if (nb.nRail > 0) in.plan.Kt++;
    }
  }
  in.util.desalinationAvailable = advisors_.buildingUnlocked(12830);
  {  // Planner metrics (spec 22 s3.11)
    auto& P = in.plan;
    for (auto z : world_.zone) { if (z == 15) P.zSea++; else if (z == 14) P.zAir++; else if (z == 17) P.zFill++; }
    // x(f, c): tiles of family f with last result c = the zone developer's tallies of the last completed pass, summed over
    // the family's zone types [INFERRED: per-pass tallies stand in for per-tile last codes]
    int seaBad = 0, fillNoAccess = 0;
    for (const auto& kv : c.zones.resultPrev) {
      const int code = kv.first.first, zt = kv.first.second;
      const int f = (zt >= 1 && zt <= 3) ? 0 : (zt >= 5 && zt <= 7) ? 1 : (zt >= 9 && zt <= 11) ? 2 : -1;
      if (f >= 0 && code < 20) P.x[f][code] += int(kv.second);
      if (zt == 15 && (code == 9 || code == 12 || (code >= 14 && code <= 17))) seaBad += int(kv.second);
      if (zt == 17 && code == 16) fillNoAccess += int(kv.second);
    }
    P.seaportBadTiles = seaBad; P.landfillNoAccessTiles = fillNoAccess;
    for (const auto& o : world_.occupants) {
      if ((o.state & ~0x200u) != 1) continue;
      switch (o.agentType) {
        case 12310: P.airportCount++; break;
        case 12330: P.seaportCount++; break;
        case 9300: P.farms++; break;
        case 8700: case 8900: case 9100: P.cleanIndustry++; break;
        case 5700: case 6100: P.medComm++; break;
        case 6300: case 6700: P.highComm++; break;
        case 14310: P.n14310++; break; case 14311: P.n14311++; break; case 14312: P.n14312++; break;
        case 14313: P.n14313++; break; case 14314: P.n14314++; break; case 14315: P.n14315++; break; case 14316: P.n14316++; break;
        default: break;
      }
      if (o.agentType >= 8000 && o.agentType < 10000) P.industrialBuildings++;
    }
    P.farmersMarketAvailable = c.ordinances.available(0x62F6E82B) && !c.ordinances.enacted(0x62F6E82B);
    P.proReadingEnacted = c.ordinances.enacted(0xE0D07233);
    P.tourismEnacted = c.ordinances.enacted(0x62F6E7CF);
    // landfill "full" and "no free capacity" [INFERRED from the store]: pending garbage exceeds spare room / no spare room
    // spec 22 s3.11 [STATIC]: 0xE1 = registered landfill tiles x 32767 > 0 and the store's Dlast == 0
    P.landfillFull = int64_t(c.pollution.landfillTiles) * 32767 > 0;
    P.landfillNoFreeCapacity = c.pollution.disp.store.Dlast == 0;
    // capC/capI = capPct of valves 5000/8000; flags A/B = the transportation advisor's airport / highway tech flags
    P.capC = c.valves.v5000.capPct; P.capI = c.valves.v8000.capPct;
    P.capFlagA = advisors_.transportTech(2); P.capFlagB = advisors_.transportTech(3);
    // B (petitions 0x49/0x4A) = all occupants + road, highway, rail and power-line piece counts (spec 12 s10 counters)
    {
      int64_t net = 0;
      for (const auto& p : world_.surface) {
        if (!world_.inTiles(p.x, p.z)) continue;
        if (c.isRoadTile(p.x, p.z) || c.isPowerLine(p.x, p.z)) net++;
      }
      P.totalBuildings = int64_t(world_.occupants.size()) + net;
    }
  }
  {  // Ageing counts (spec 16 s2.6, 22 s3.13) [STATIC]: plants from the power layer's monthly pass, water facilities that reached
     // the ageing step, incinerators (global [Incinerator] DeclineAge) and WTE plants (their own Ld) in lot state 1.
    in.util.nOldPlants = c.power.stats().oldPlants;
    in.util.nNuclearEnding = c.power.stats().nuclearEnding;
    in.util.nOldPumps = int(c.water.stats.ageingCount);
    us::IniReader tu{&c.config().ini.tune};
    int maxL = int(tu.num("Incinerator", "MaxLifespan", 0)); if (maxL <= 0) maxL = 1;
    int decl = int(tu.num("Incinerator", "DeclineAge", 0)); if (decl > maxL) decl = maxL / 2;
    const int now = 12 * c.clock.date().year + c.clock.date().month;
    for (const auto& o : world_.occupants) {
      if ((o.state & ~0x200u) != 1) continue;
      if (o.agentType == 12852 && o.hasDate && o.dateYear > 0) {
        int built = 12 * o.dateYear + std::max(0, std::min(11, (std::max(1, o.dateDay) - 1) * 12 / 366)) + 1;
        if (now - built >= decl) in.util.nOldIncin++;
      }
    }
    for (const auto& p : c.power.plants())
      if (p.agentType == 12853 && c.clock.date().year * 12 + c.clock.date().month - p.buildAbsMonth >= p.ld) in.util.nOldIncin++;
  }
  {  // Health & education inputs (spec 22 s3.12 metrics) [STATIC]: the residential layer's saved fields. fundClass* are the
     // 0-4 service ratings; demand = patients / school-age (7-18) / college-age (19-24) population; capacity = beds /
     // desks; warn = strike risk / 100.
    const Population& p = c.pop;
    in.demo.fundClassHealth = p.healthRating; in.demo.fundClassSchool = p.schoolRating; in.demo.fundClassCollege = p.collegeRating;
    in.demo.demandHealth = p.patients; in.demo.capHealth = p.hospitalBeds;
    in.demo.demandSchool = p.schoolAgePop; in.demo.capSchool = p.schoolDesks;
    in.demo.demandCollege = p.collegeAgePop; in.demo.capCollege = p.collegeDesks;
    in.demo.libraryCap = p.libraryCapacity; in.demo.museumCap = p.museumCapacity;
    in.demo.warnHealth = int(p.strikeRisk(0, in.demo.healthFunding, in.demo.nHospital > 0) / 100);
    in.demo.warnSchool = int(p.strikeRisk(1, in.demo.eduFunding, in.demo.nSchool > 0) / 100);
    in.demo.warnCollege = int(p.strikeRisk(2, in.demo.eduFunding, in.demo.nCollege > 0) / 100);
  }
  {  // Garbage metrics (spec 22) [STATIC]: previous month's counters (our history is newest first), landfill spare room
     // R = store Dlast, burn capacities per run; A = standing garbage on the map, latched when the last sweep finalised.
    const auto& P = c.pollution;
    const int64_t* G = P.disp.history[0];
    in.env.garbageDemand = G[0] + G[1];
    in.env.garbageCapacity = int64_t(P.disp.store.Dlast) / 12 + 30 * (int64_t(P.burnCapA) + int64_t(P.burnCapB)) + G[3];
    in.env.garbageAccumulating = P.standingAtSweepEnd;
    in.env.garbageNearCapacity = !in.env.garbageAccumulating && in.env.garbageDemand > in.env.garbageCapacity;
    in.env.garbageSurplusWithinLimit = !in.env.garbageAccumulating && in.env.garbageDemand <= in.env.garbageCapacity;
  }   // spec 22 "Building availability"
  {  // Dreary rating (spec 22 s3.12) [STATIC]: the cap rule (15 s3.2) on residential amenity valve 3001 at the last month end.
    const auto& v = c.valves.v3001;
    in.dreary = v.Dlast <= 0 ? 0 : v.Slast < v.Dlast ? 100 : int(std::min<int64_t>(100, int64_t(100) * v.Dlast / v.Slast));
  }
  in.safety.policeStrength = in.safety.policeFunding;   // spec 22 s3.9: the "> 105" test reads police funding
  for (const auto& o : world_.occupants) {
    if ((o.state & ~0x200u) != 1) continue;
    if (o.agentType == 14450) in.demo.nZoo++;
    else if (o.agentType == 14430) in.demo.nMarina++;
  }
  for (auto z : world_.zone) {
    if (z >= 1 && z <= 3) in.plan.zoned[0]++; else if (z >= 5 && z <= 7) in.plan.zoned[1]++; else if (z >= 9 && z <= 11) in.plan.zoned[2]++;
  }
}

// Spec 22 3.6 [STATIC cadence]: the advisors and the petitioner manager listen to the calendar's week and day notifications. The week
// notification (Monday) only raises a flag; every day notification, which the calendar sends before the week one on the same date, runs one
// evaluation when the flag is up and clears it. So the evaluation happens once per game week, on Tuesday, and nothing is pending after a load.
void Game::weekNotification() {
  advisors_.calendarWeek();
  petWeekFlag_ = true;
}

void Game::dayNotification() {
  if (!managersReady_) return;
  const bool petFlag = petWeekFlag_;
  petWeekFlag_ = false;
  if (advisors_.clock.needsUpdate || petFlag) {
    AdvisorInputs ai;
    fillAdvisorInputs(ai);
    AdvisorOutputs out;
    advisors_.calendarDay(ai, rng_, out);   // advisors first (registration order), then the petitioner manager
    if (petFlag) {
      PetitionerInputs pi;
      pi.population = ai.population; pi.year = ai.year;
      petitioners_.weeklyCycle(pi, rng_);
      {  // spec 19 section 11: low-treasury business-deal offers, raised as advisor petitions
        City& c = city();
        BusinessDeals::Inputs bi;
        bi.funds = sim_.funds; bi.population = uint32_t(std::max<int64_t>(0, ai.population)); bi.currentYear = ai.year;
        bi.cityHasBusiness = [this](uint32_t type) {
          for (const auto& o : world_.occupants) if (o.agentType == type) return true;
          return false;
        };
        if (cheats_.active("let's make a deal")) {   // spec 27 s5.3: every eligible deal, all pending together
          for (size_t i : c.business.eligibleDeals(bi)) petitioners_.request(c.business.defs[i].messageId, 0, 48);
        } else if (auto offer = c.evaluateBusinessDeals(bi)) petitioners_.request(c.business.defs[*offer].messageId, 0, 48);
      }
    }
  }
  presentPetitions();
}

void Game::soundLoop(int id, bool on, int pct) {
  if (!audio_ || id <= 0) return;
  if (on) {
    if (disasterLoops_.insert(id).second) audio_->request(id, SoundAction::Play);
    audio_->request(id, SoundAction::SetLevel, std::max(0, std::min(100, pct)));
  } else if (disasterLoops_.erase(id)) audio_->request(id, SoundAction::Stop);
}

void Game::applyDisasterOutput(DisasterOutput& out) {
  City& c = city();
  // The sprite automata that would carry out saucer bombs, crop circles, locust bites and falling debris are not
  // drawn yet; their gameplay effect is applied here at once, as if each had just landed. Results go through the
  // same command path (follow-up output is applied after this batch).
  DisasterOutput follow;
  {
    CityDisasterWorld dw(world_, c);
    for (const auto& cmd : out.commands) {
      switch (cmd.kind) {
        case DisasterCommand::UfoSwarm:
          for (const auto& p : cmd.pts) disasters_.ufoBombHit(dw, p.first, p.second, follow);
          for (const auto& p : cmd.pts2) disasters_.ufoCropCircle(dw, p.first, p.second, follow);
          break;
        case DisasterCommand::LocustSwarm:
          for (const auto& p : cmd.pts) disasters_.locustEaten(dw, p.first, p.second, follow);
          break;
        case DisasterCommand::DebrisSpawn: {   // [INFERRED] the class value (2000/5000/10000) is the intensity: a/250 impacts
          int n = std::max(1, cmd.a / 250);
          for (int i = 0; i < n && world_.W > 0; i++) disasters_.debrisImpact(dw, int(rng_.rand(uint32_t(world_.W))), int(rng_.rand(uint32_t(world_.H))), follow);
          break;
        }
        default: break;
      }
    }
  }
  for (const auto& cmd : out.commands) {
    switch (cmd.kind) {
      case DisasterCommand::DestroyOccupant: {
        int i = c.occupantIndexByUid(cmd.id);
        if (i >= 0 && i < int(world_.occupants.size()) && (cmd.b == 0 || cmd.b == 1)) { notifyBuildingDestroyed(i); c.damageOccupant(i, 8); }
        break;
      }
      case DisasterCommand::IgniteOccupant: {
        int i = c.occupantIndexByUid(cmd.id);
        if (i >= 0 && i < int(world_.occupants.size())) c.damageOccupant(i, 16);
        break;
      }
      case DisasterCommand::ClearBurning: {
        int i = c.occupantIndexByUid(cmd.id);
        if (i >= 0 && i < int(world_.occupants.size()) && world_.occupants[i].state == 16) { world_.occupants[i].state = 1; c.markOccupantsDirty(); }
        break;
      }
      case DisasterCommand::MarkAbandoned: {
        int i = c.occupantAt(cmd.x, cmd.z);
        if (i >= 0) { world_.occupants[i].state = 2; c.markOccupantsDirty(); }
        break;
      }
      case DisasterCommand::CreditTreasury: sim_.funds += cmd.value; break;
      case DisasterCommand::Explosion: case DisasterCommand::Sound: sound(cmd.a ? cmd.a : 0); break;
      case DisasterCommand::ScreenShake: shake_ = std::max(shake_, cmd.a); break;
      case DisasterCommand::TerrainDelta:
        if (cmd.x >= 0 && cmd.z >= 0 && cmd.x <= world_.W && cmd.z <= world_.H) {
          int v = world_.altAt(cmd.x, cmd.z) + cmd.a;
          world_.alt[world_.vIdx(cmd.x, cmd.z)] = uint8_t(std::max(0, std::min(255, v)));
          invalidateCityView();
        }
        break;
      case DisasterCommand::MarkRadiation: c.markRadiation(cmd.x, cmd.z); break;
      case DisasterCommand::SoundLoop: soundLoop(cmd.a, cmd.b != 0, int(cmd.value)); break;
      // Visual only (no state of the city changes; the destruction they accompany arrives as DestroyOccupant / MarkAbandoned): effect sprites
      // (splash, fire intensity, crop circle, tornado, toxic cloud and rain), the saucers' exit and the rioter crowd automaton [#35: nothing is drawn].
      case DisasterCommand::SetFireIntensity: case DisasterCommand::Splash: case DisasterCommand::CropCircle: case DisasterCommand::UfoLeave:
      case DisasterCommand::TornadoTouchdown: case DisasterCommand::ToxicPuffs: case DisasterCommand::ToxicRain: case DisasterCommand::SpawnRioters:
      // Dispatch bookkeeping of DispatchTracker, which the game does not use: the dispatch tools stamp the coverage layers directly (CoverageLayer::dispatch).
      case DisasterCommand::StampCoverage: case DisasterCommand::SpawnUnit: case DisasterCommand::RemoveUnit:
        break;
      case DisasterCommand::VortexSpawn: {   // the vortex object is not drawn; it still scours the coast once and lasts its duration
        CityDisasterWorld dw(world_, c);
        DisasterOutput extra;
        disasters_.vortexPosition(dw, cmd.x, cmd.z, extra);
        for (auto& e : extra.commands) out.commands.push_back(e);
        vortexMs_ = std::max(1, cmd.a);
        break;
      }
      case DisasterCommand::Crater:
        if (cmd.pts.size() >= 2) {
          std::set<int> hit;
          for (int x = cmd.pts[0].first; x <= cmd.pts[1].first; x++)
            for (int z = cmd.pts[0].second; z <= cmd.pts[1].second; z++) { int i = c.occupantAt(x, z); if (i >= 0) hit.insert(i); }
          for (int i : hit) c.damageOccupant(i, 8);
          sound(244);
        }
        break;
      default: break;
    }
  }
  for (const auto& ev : out.events) {
    if (ev.kind == DisasterEvent::Started) {
      disasterX_ = ev.x; disasterZ_ = ev.z;
      NewsKey k = disasterStartNews(ev.type, rng_, ev.a == 1);   // ticker item per disaster type (spec 22 4.7); none for parades
      if (k.valid()) postNews(k.table, k.id);
    }
    if (ev.kind == DisasterEvent::AllOver) { disasterX_ = disasterZ_ = -1; }
  }
  out.clear();
  if (!follow.commands.empty() || !follow.events.empty()) applyDisasterOutput(follow);
}

void Game::startDisaster(DisasterType t) {
  syncSim();   // mutates sim state; also callable from outside a frame
  if (!simReady_ || !disastersOn_) return;
  if (!managersReady_) initManagers();
  CityDisasterWorld dw(world_, city());
  DisasterOutput out;
  if (!disasters_.startRandom(t, dw, inputsFor(city(), disastersOn_, cheats_), out)) message("The disaster could not start.");
  applyDisasterOutput(out);
}

void Game::sirenAndGo() {
  sound(48);
  gotoDisaster();
}

void Game::gotoDisaster() {
  if (disasterX_ >= 0) { sound(55); centreOn(disasterX_, disasterZ_); }
}

void Game::dispatchUnit(int kind, int tx, int tz) {
  // Dispatch tools (spec 21 s10.3): 0 police, 1 fire, 2 plane. A police or fire unit adds a temporary coverage stamp around
  // the clicked tile (limited to one unit more than the station count) and a street vehicle drives there.
  sound(kind == 0 ? 34 : 35);
  if (simReady_ && world_.inTiles(tx, tz)) {
    City& c = city();
    if (kind == 0) c.police.dispatch(tx, tz);
    else if (kind == 1) c.fire.dispatch(tx, tz);
    if (street_) street_->dispatchEmergency(kind == 0 ? EmergencyType::PoliceCar : kind == 1 ? EmergencyType::FirePumper : EmergencyType::Ambulance, tx, tz);
  }
  centreOn(tx, tz);
}

void Game::typeCheat(const std::string& s) {
  CheatResult r = cheats_.submit(s);
  if (!r.match.info) return;
  applyCheat(r);
}

// Effects of spec 27 section 5.3. `traffic lights on|off` and `tuneup` have no visible effect in the original (the first sets a
// flag nothing reads; the second drives a developer object on a file that is not shipped), so they are accepted and ignored.
// The build-menu cheats (`power to the masses`, `water in the desert`, `garbage in, garbage out`) are read from the cheat
// registry by the pickers (game_windows.cpp).
void Game::applyCheat(const CheatResult& r) {
  const CheatInfo& c = *r.match.info;
  std::string phrase;
  for (char ch : std::string(c.phrase)) phrase += char(std::tolower(static_cast<unsigned char>(ch)));
  if (!r.tickerEcho.empty()) {
    cal::Date d = sim_.city().clock.date();
    std::string text = r.tickerEcho;
    news_.post(kTickerStringTable, r.tickerMsg, tickerStamp(d.month, d.day, d.year), false, 0, 0, &text);
  } else if (r.tickerMsg) {
    postNews(kTickerStringTable, r.tickerMsg);
  }
  if (!simReady_) return;
  City& cy = city();
  if (phrase == "i am weak") cy.budget.freePlacement = r.stateOn;
  else if (phrase == "so sue me") cy.budget.debtFloor = r.stateOn;
  else if (phrase == "i love red tape") {
    std::vector<uint32_t> announced;
    cy.ordinances.makeAllAvailable(&announced);
  } else if (phrase == "nerdz rool") cy.setCleanIndustryOnly(true);
  else if (phrase == "pay tribute to your king") cheatAllRewards_ = true;
  else if (phrase == "force petitioners to say") {   // spec 27 s5.3 / 22 s5.3: re-requested every petitioner cycle
    long id = forcedMessageId(r.match);
    if (id > 0 && std::find(petitioners_.forced.begin(), petitioners_.forced.end(), uint32_t(id)) == petitioners_.forced.end())
      petitioners_.forced.push_back(uint32_t(id));
  } else if (phrase.rfind("force ", 0) == 0 && phrase.size() > 6) {   // force <advisor> to say <n> (spec 27 s5.3, 22)
    static const std::pair<const char*, uint32_t> names[] = {{"force mortimer to say", kAdvBudget}, {"force moe to say", kAdvTransport},
        {"force constance to say", kAdvPlanner}, {"force maria to say", kAdvSafety}, {"force randall to say", kAdvDemographics},
        {"force gus to say", kAdvUtilities}, {"force karen to say", kAdvEnvironment}};
    for (const auto& nm : names)
      if (phrase == nm.first) {
        long id = forcedMessageId(r.match);
        if (id > 0) advisors_.list(nm.second).forced.push_back(uint32_t(id));   // duplicates allowed, not saved
      }
  } else if (phrase == "stop forcing advice") {
    petitioners_.forced.clear();
    for (uint32_t a : allAdvisorIds()) advisors_.list(a).forced.clear();
  }
  else if (phrase == "call cousin vinnie") { if (!vinnieDone_) { petitioners_.request(0xD7, 0, 6); presentPetitions(); } }
  else if (phrase == "zyxwvu") { if (vinnieRefused_) advisors_.rewards.markAvailable(19999); }
  else if (phrase == "salt off" || phrase == "salt on") { cy.applySaltCheat(phrase == "salt on"); dirty_ = true; }
  else if (int delta = terrainCheatDelta(&c)) {
    World& w = world_;
    for (auto& v : w.alt) v = uint8_t(shiftTerrainAltitude(v, delta));
    for (auto& v : w.water) v = uint8_t(shiftTerrainAltitude(v, delta));
    w.seaLevel = uint8_t(shiftTerrainAltitude(w.seaLevel, delta));
    w.invalidateDeepWater();
    terrainChanged();
    dirty_ = true;
  }
}

// Starts the holiday parade: the disaster layer's parade event plus its vehicles (see startParadeEvent).
bool Game::startParadeNow() {
  if (!managersReady_) initManagers();
  City& c = sim_.city();
  CityDisasterWorld dw(world_, c);
  DisasterOutput out;
  if (!disasters_.startRandom(DisasterType::Parade, dw, inputsFor(c, true, cheats_), out)) return false;
  applyDisasterOutput(out);
  if (startParadeEvent()) return true;
  DisasterOutput o2; disasters_.stop(DisasterType::Parade, o2);   // no route / no data: no parade
  return false;
}

bool Game::scenarioActive() const { return scen_ && scen_->active(); }

void Game::startScenarioHooks() {
  if (!scen_) return;
  ScenarioHooks h;
  h.popUp = [this](const std::string& text, int, uint32_t, int, bool) { message(text); };
  h.ticker = [this](const std::string& text, int, bool) { statusLine_ = text; };
  h.sound = [this](int id) { sound(id); };
  h.moveCamera = [this](int x, int z, int zoom) { setView(x, z, zoom); };
  h.setSpeed = [this](int sp) { speed_ = std::max(1, std::min(4, sp)); };
  h.startDisaster = [this](int kind, const std::vector<int32_t>&) {
    static const DisasterType map[] = {DisasterType::Fire, DisasterType::Tornado, DisasterType::Earthquake, DisasterType::Riot,
                                       DisasterType::UFO, DisasterType::ToxicCloud, DisasterType::Whirlpool, DisasterType::Locusts,
                                       DisasterType::SpaceJunk};
    if (kind >= 0 && kind < 9) startDisaster(map[kind]);
  };
  h.setDisastersEnabled = [this](bool on) { setDisastersOn(on); };
  h.text = [this](uint32_t g, uint32_t i) { return str(g, i); };
  scen_->start(city(), world_.info.mayorName, world_.info.cityName, h);
}

void Game::applyPetitionEffect(const PetitionEffect& e) {
  City& c = city();
  if (e.kind == PetitionEffect::NeighbourDeal) {   // answers to the utilities advisor's offer petitions (spec 22 s5.5)
    int type = UtilityDeals::typeOfPetition(e.petition);
    if (type < 0) return;
    auto& pend = c.neighbours.pending;
    for (size_t i = 0; i < pend.size(); i++)
      if (pend[i].dealType == type) {
        if (e.accepted) c.neighbours.accept(i); else { pend.erase(pend.begin() + long(i)); c.neighbours.removeType(type); }
        dirty_ = true;
        break;
      }
    return;
  }
  if (e.petition == 0xD7) vinnieDone_ = true;   // cousin Vinnie: hidden for the rest of the session once answered
  if (!e.accepted) {
    if (e.kind == PetitionEffect::BusinessDeal) c.business.pendingOffer.reset();   // declined: offers may be raised again
    if (e.kind == PetitionEffect::PlannerFlag && e.petition == 0xD7) vinnieRefused_ = true;
    return;
  }
  switch (e.kind) {
    case PetitionEffect::Treasury: sim_.funds += e.treasuryDelta; break;
    case PetitionEffect::SetOrdinance:
      if (e.value) c.ordinances.enact(e.ordinance); else c.ordinances.repeal(e.ordinance);
      break;
    case PetitionEffect::RewardAvailable:
      if (e.buildingType) advisors_.rewards.markAvailable(e.buildingType);
      break;
    case PetitionEffect::BusinessDeal: {
      BusinessDeals& b = c.business;
      for (const auto& d : b.defs)
        if (d.messageId == e.petition) { if (b.acceptByMessage(e.petition)) advisors_.rewards.markAvailable(int(d.buildingType)); break; }
      break;
    }
    case PetitionEffect::WorkerStrike: case PetitionEffect::PublicSafetyStrike: case PetitionEffect::TransitStrike: {
      // Strike petitions (spec 22 s5.5) [STATIC]: accept sets the department to 110 % when below; decline does nothing.
      // Transit accept also forces the strike state to striking (counter 0) when it is not normal (original quirk).
      uint32_t d = 0;
      switch (e.petition) {
        case 0x9B: case 0x9C: d = dept::Health; break;
        case 0x9D: case 0x9E: d = dept::Education; break;
        case 0x9F: case 0xA0: d = dept::Police; break;
        case 0xA2: case 0xCC: d = dept::Fire; break;
        case 0xA3: case 0xA4: d = dept::Transit; break;
        default: break;
      }
      if (const Department* dp = d ? c.budget.dept(d) : nullptr)
        if (dp->funding < 110) {
          c.budget.setFunding(d, 110);
          if (d == dept::Police) c.police.setFunding(110);
          else if (d == dept::Fire) c.fire.setFunding(110);
          else if (d == dept::Transit && c.traffic) c.traffic->setTransitFunding(110);
          dirty_ = true;
        }
      if (d == dept::Transit && c.traffic && c.traffic->rec().strikeState != STRIKE_NORMAL) {
        c.traffic->rec().strikeState = STRIKE_STRIKING; c.traffic->rec().strikeMonths = 0;
      }
      break;
    }
    case PetitionEffect::StartParade: {   // 0x12D accepted: the holiday parade (spec 22 s5.5, 26) runs regardless of the disaster setting
      startParadeNow();
      break;
    }
    default: break;
  }
  if (e.voiceSound) sound(e.voiceSound);
}

void Game::presentPetitions() {
  if (petView_ || !managersReady_) return;
  for (const auto& p : petitioners_.list()) {
    if (p.hidden() || !p.raised()) continue;
    if (std::find(petShown_.begin(), petShown_.end(), p.id) != petShown_.end()) continue;
    WinEnv E = env();
    petView_ = std::make_unique<PetitionerView>(makePetitionerView(E, petitioners_.config, p, &E.tc));
    petViewId_ = p.id; petDlg_.impact = false; petDlg_.inMeet = false;
    petShown_.push_back(p.id);
    break;
  }
}

void Game::dayPostStep() {
  int month = city().clock.date().month;
  if (month != lastMonth_) {
    // Monthly DM already ran inside CitySchedule::runMonth (dedicated item 1, Q-BE) during tickDay.
    lastMonth_ = month;
    advisors_.monthlyBuildingUnlock(city().clock.date().year, rng_);
    if (month == 1) {   // city anniversary ticker item (10, 25, 50, 100, ... years since founding)
      NewsKey k = anniversaryNews(city().clock.date().year - world_.info.foundingYear);
      if (k.valid()) postNews(k.table, k.id);
    }
  }
  int wd = cal::weekday(city().clock.jdn());
  dayNotification();   // day listeners are notified before the week ones on the same date
  if (wd == 1 && lastWeekDay_ != int(city().clock.jdn())) { lastWeekDay_ = int(city().clock.jdn()); weekNotification(); }
}

void Game::tickSim(uint32_t dtMs) {
  if (!simReady_ || paused_ || !world_.W) return;
  if (city().hold()) return;   // a pause or modal latch is raised: the schedule would wait for it (spec 14 6.3 rule 6)
  if (!managersReady_) initManagers();
  const uint32_t factor = SpeedControl::kFactor[std::max(1, std::min(4, speed_))];
  // One simulated day: sim step, disaster day tick, month/anniversary hooks, scenario and weekly hooks.
  auto dayStep = [&] {
    simDayThisFrame_ = true;
    daySimStep(disasters_.anyActiveNonParade());
    dayPostStep();
  };
  // Async: credit time here; asyncFrameEnd starts the worker job. Disasters, whirlpools and minute mode need the
  // owner thread, so those wait for a running job and use the synchronous path below.
  const bool asyncOk = asyncSim_ && vortexMs_ <= 0 && !minuteModeOn_ && !disasters_.anyActiveNonParade();
  if (asyncSim_ && !asyncOk) syncSim();
  if (asyncOk) {
    clockCbAccMs_ += dtMs;
    simAccum_ = std::min(4.0, simAccum_ + double(dtMs) / 1000.0 * double(factor) / 86400.0);
    if (shake_ > 0) shake_--;
    return;
  }
  simDayThisFrame_ = false;
  // Whirlpool lifetime is real wall-clock (not batched with the sim timer).
  if (vortexMs_ > 0) {
    vortexMs_ -= int(dtMs);
    if (vortexMs_ <= 0) {
      vortexMs_ = 0;
      DisasterOutput out;
      disasters_.vortexFinished(inputsFor(city(), disastersOn_, cheats_), out);
      applyDisasterOutput(out);
    }
  }
  // Spec 14 §2.2: the original credits time from a 1000 ms timer callback, which at high speed dumps
  // several days onto consecutive frames (a once-per-second hitch cluster). We keep the same long-term
  // rate (factor / 86400 days per real second) but credit proportionally every frame and drain at most
  // one day (or a few minutes) per frame so the work is spread evenly.
  clockCbAccMs_ += dtMs;   // retained for diagnostics / catch-up accounting
  const bool minuteMode = disasters_.anyActiveNonParade();
  if (minuteMode) {
    if (!minuteModeOn_) { minuteModeOn_ = true; minutesDone_ = int(simAccum_ * 1440.0); simAccum_ = 0; minuteAccMs_ = 0; }
    minuteAccMs_ += dtMs;
    const uint32_t msPerDay = uint32_t(86400000ull / std::max<uint32_t>(1, factor));
    const uint32_t msPerMinute = std::max<uint32_t>(std::max<uint32_t>(msPerDay / 24, 50) / 60, 50);
    int guard = 0;
    while (minuteAccMs_ >= msPerMinute && guard++ < 8) {
      minuteAccMs_ -= msPerMinute;
      {
        CityDisasterWorld dw(world_, city());
        DisasterOutput out;
        disasters_.minuteTick(dw, inputsFor(city(), disastersOn_, cheats_), out);
        applyDisasterOutput(out);
      }
      if (++minutesDone_ >= 1440) { minutesDone_ = 0; dayStep(); break; }
      if (!disasters_.anyActiveNonParade()) break;
    }
  } else {
    if (minuteModeOn_) { minuteModeOn_ = false; simAccum_ = minutesDone_ / 1440.0; minutesDone_ = 0; }
    simAccum_ += double(dtMs) / 1000.0 * double(factor) / 86400.0;
    if (simAccum_ >= 1.0) { simAccum_ -= 1.0; dayStep(); }
  }
  if (shake_ > 0) shake_--;
}

}  // namespace m3k
