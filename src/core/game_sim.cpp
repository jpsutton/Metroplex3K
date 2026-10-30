// Game <-> simulation glue: sim stepping, disasters, cheats, scenarios, street sim hooks.
#include <algorithm>

#include "core/game.h"
#include "core/game_scenario.h"

namespace m3k {

namespace {

// DisasterWorld adapter over World / City.
class CityDisasterWorld : public DisasterWorld {
 public:
  CityDisasterWorld(World& w, City& c) : w_(w), c_(c) {}
  int width() const override { return w_.W; }
  int height() const override { return w_.H; }
  OccInfo occupantAt(int x, int z) const override {
    OccInfo o;
    int i = c_.occupantAt(x, z);
    if (i < 0 || i >= int(w_.occupants.size())) return o;
    const Occupant& oc = w_.occupants[i];
    o.valid = true; o.id = uint32_t(i + 1); o.ax = oc.x; o.az = oc.z; o.w = oc.sizeX; o.h = oc.sizeZ;
    o.agentType = oc.agentType;
    o.burning = oc.state == 16;
    o.lotDamage = oc.state == 8 ? kDmgRubble : oc.state == 16 ? kDmgBurnt : oc.state == 2 ? kDmgAbandon : 0;
    return o;
  }
  bool isWater(int x, int z) const override {
    if (!w_.inTiles(x, z)) return false;
    return w_.altAt(x, z) < w_.waterAt(x, z);
  }
  int terrainHeight(int x, int z) const override { return w_.inTiles(x, z) ? w_.altAt(x, z) : 0; }
  int zoneType(int x, int z) const override { return w_.inTiles(x, z) ? w_.zone[w_.tIdx(x, z)] : 0; }
  int flammability(int x, int z) const override {
    // Without the flammability layer view: wood-ish default on occupied tiles.
    return c_.occupantAt(x, z) >= 0 ? 120 : 0;
  }
 private:
  World& w_;
  City& c_;
};

DisasterInputs inputsFor(City& c, bool enabled) {
  DisasterInputs in;
  in.jdn = c.clock.jdn();
  in.year = c.clock.date().year;
  in.disastersEnabled = enabled;
  return in;
}

}  // namespace

void Game::initManagers() {
  disasters_.seed(uint32_t(world_.W) * 2654435761u + 7u);
  managersReady_ = true;
  lastMonth_ = city().clock.date().month;
  lastWeekDay_ = -1;
  auto textFn = [this](uint32_t t, uint32_t i) { return str(t, i); };
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
  advisors_.clear();
  petitioners_.setSink(sink_.get());
  petitioners_.clear();
  petShown_.clear();
  petView_.reset();
}

void Game::fillAdvisorInputs(AdvisorInputs& in) {
  City& c = city();
  in.population = c.population();
  in.year = c.clock.date().year;
  in.mapWidth = world_.W; in.mapHeight = world_.H;
  in.budget.treasury = sim_.funds;
  for (int i = 0; i < 3; i++) in.budget.taxRate[i] = sim_.taxRate;
  in.plan.demand[0] = sim_.valveR.value; in.plan.demand[1] = sim_.valveC.value; in.plan.demand[2] = sim_.valveI.value;
  in.plan.treasury = sim_.funds;
  in.plan.monthIndex = c.clock.date().month - 1;
  in.trans.nRoad = c.roadCount();
  int64_t totalB = 0;
  for (const auto& o : world_.occupants) { (void)o; totalB++; }
  in.plan.totalBuildings = totalB;
  for (auto z : world_.zone) {
    if (z >= 1 && z <= 3) in.plan.zoned[0]++; else if (z >= 5 && z <= 7) in.plan.zoned[1]++; else if (z >= 9 && z <= 11) in.plan.zoned[2]++;
  }
}

void Game::weeklyHook() {
  if (!managersReady_) return;
  AdvisorInputs ai;
  fillAdvisorInputs(ai);
  advisors_.calendarWeek();
  AdvisorOutputs out;
  advisors_.calendarDay(ai, rng_, out);
  PetitionerInputs pi;
  pi.population = ai.population; pi.year = ai.year;
  petitioners_.weeklyCycle(pi, rng_);
  presentPetitions();
}

void Game::applyDisasterOutput(DisasterOutput& out) {
  City& c = city();
  for (const auto& cmd : out.commands) {
    switch (cmd.kind) {
      case DisasterCommand::DestroyOccupant: {
        int i = int(cmd.id) - 1;
        if (i >= 0 && i < int(world_.occupants.size())) { world_.occupants[i].state = cmd.b == 0 || cmd.b == 1 ? 8 : world_.occupants[i].state; c.markOccupantsDirty(); }
        break;
      }
      case DisasterCommand::IgniteOccupant: {
        int i = int(cmd.id) - 1;
        if (i >= 0 && i < int(world_.occupants.size())) { world_.occupants[i].state = 16; c.markOccupantsDirty(); }
        break;
      }
      case DisasterCommand::ClearBurning: {
        int i = int(cmd.id) - 1;
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
          renderer_.invalidateShade();
        }
        break;
      default: break;
    }
  }
  for (const auto& ev : out.events) {
    if (ev.kind == DisasterEvent::Started) { disasterX_ = ev.x; disasterZ_ = ev.z; }
    if (ev.kind == DisasterEvent::AllOver) { disasterX_ = disasterZ_ = -1; }
  }
  out.clear();
}

void Game::startDisaster(DisasterType t) {
  if (!simReady_ || !disastersOn_) return;
  if (!managersReady_) initManagers();
  CityDisasterWorld dw(world_, city());
  DisasterOutput out;
  if (!disasters_.startRandom(t, dw, inputsFor(city(), disastersOn_), out)) message("The disaster could not start.");
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
  (void)kind;
  sound(kind == 0 ? 34 : 35);
  centreOn(tx, tz);
}

void Game::typeCheat(const std::string& s) {
  CheatResult r = cheats_.submit(s);
  (void)r;
  if (r.match.info) message("Cheat accepted.");
}

bool Game::scenarioActive() const { return scen_ && scen_->active(); }

void Game::startScenarioHooks() {
  if (!scen_) return;
  ScenarioHooks h;
  h.popUp = [this](const std::string& text, int, uint32_t, int, bool) { message(text); };
  h.ticker = [this](const std::string& text, int, bool) { statusLine_ = text; };
  h.sound = [this](int id) { sound(id); };
  h.moveCamera = [this](int x, int z, int zoom) { setView(x, z, zoom); };
  h.setSpeed = [this](int sp) { speed_ = std::max(1, std::min(3, sp)); };
  h.startDisaster = [this](int kind, const std::vector<int32_t>&) {
    static const DisasterType map[] = {DisasterType::Fire, DisasterType::Tornado, DisasterType::Earthquake, DisasterType::Riot,
                                       DisasterType::UFO, DisasterType::ToxicCloud, DisasterType::Whirlpool, DisasterType::Locusts,
                                       DisasterType::SpaceJunk};
    if (kind >= 0 && kind < 9) startDisaster(map[kind]);
  };
  h.setDisastersEnabled = [this](bool on) { disastersOn_ = on; };
  h.text = [this](uint32_t g, uint32_t i) { return str(g, i); };
  scen_->start(city(), world_.info.mayorName, world_.info.cityName, h);
}

void Game::applyPetitionEffect(const PetitionEffect& e) {
  if (!e.accepted) return;
  City& c = city();
  switch (e.kind) {
    case PetitionEffect::Treasury: sim_.funds += e.treasuryDelta; break;
    case PetitionEffect::SetOrdinance:
      if (e.value) c.ordinances.enact(e.ordinance); else c.ordinances.repeal(e.ordinance);
      break;
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
    petViewId_ = p.id;
    petShown_.push_back(p.id);
    break;
  }
}

void Game::tickSim(uint32_t dtMs) {
  if (!simReady_ || paused_ || !world_.W) return;
  if (!managersReady_) initManagers();
  static const double rate[] = {0.0, 1.0, 3.0, 8.0};
  simAccum_ += dtMs / 1000.0 * rate[std::max(1, std::min(3, speed_))];
  int guard = 0;
  while (simAccum_ >= 0.25 && guard++ < 16) {
    simAccum_ -= 0.25;
    sim_.tickDay();
    CityDisasterWorld dw(world_, city());
    DisasterOutput out;
    DisasterInputs in = inputsFor(city(), disastersOn_);
    disasters_.dayTick(dw, in, out);
    int month = city().clock.date().month;
    if (month != lastMonth_) { lastMonth_ = month; disasters_.monthlyTick(dw, in, out); }
    applyDisasterOutput(out);
    if (scen_) scen_->dayTick();
    int wd = cal::weekday(city().clock.jdn());
    if (wd == 1 && lastWeekDay_ != int(city().clock.jdn())) { lastWeekDay_ = int(city().clock.jdn()); weeklyHook(); }
  }
  if (shake_ > 0) shake_--;
}

}  // namespace m3k
