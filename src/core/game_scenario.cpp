#include "core/game_scenario.h"
#include <algorithm>
#include "core/scenario.h"
#include "core/windows_scenario.h"

namespace m3k {

namespace {
struct Stats : ICityStats {
  City* c = nullptr;
  const World* w() const { return c->world(); }
  int32_t population() const override { return c->population(); }
  int32_t landValue() const override { return c->landValue.cityAverage(); }
  int32_t crime() const override { return c->crime.avgCrime; }
  int32_t aura() const override {
    const auto& g = c->aura.grid; if (g.empty()) return 0;
    int64_t s = 0; for (int8_t v : g) s += v; return int32_t(s / int64_t(g.size()));
  }
  int32_t pollution(int kind) const override {
    return kind == 0 ? int32_t(c->pollution.avgAir) : kind == 1 ? int32_t(c->pollution.avgWater) : int32_t(c->pollution.totalStanding);
  }
  int32_t funds() const override { return int32_t(std::max<int64_t>(-2000000000, std::min<int64_t>(2000000000, c->budget.funds))); }
  int32_t totalDebt() const override { return int32_t(c->budget.totalPrincipal); }
  int32_t taxRate(int cat) const override { return c->budget.tax[std::max(0, std::min(2, cat))].rate; }
  int32_t trafficDensity() const override { return c->trafficDensityAverage(); }
  int32_t surplusPower() const override { auto s = c->power.stats(); return int32_t(s.produced - s.consumed); }
  int32_t surplusWater() const override { return 0; }
  int32_t unpoweredBuildings() const override { return c->power.stats().unpoweredConsumers; }
  int32_t unwateredBuildings() const override { return 0; }
  GameDate date() const override { auto d = c->clock.date(); GameDate g; g.day = d.day; g.month = d.month; g.year = d.year; return g; }
  void mapSize(int& W, int& H) const override { W = w() ? w()->W : 0; H = w() ? w()->H : 0; }
  int32_t countStructures(uint32_t id) const override {
    int n = 0; if (w()) for (const auto& o : w()->occupants) if (o.occId == id) n++; return n;
  }
  int32_t countStructuresNear(uint32_t id, int x, int z, int r) const override {
    int n = 0; if (w()) for (const auto& o : w()->occupants) if (o.occId == id && std::abs(o.x - x) <= r && std::abs(o.z - z) <= r) n++; return n;
  }
  int32_t countAppearance(uint32_t, uint32_t, uint32_t) const override { return 0; }
  int32_t countAppearanceNear(uint32_t, uint32_t, uint32_t, int, int, int) const override { return 0; }
  bool ordinanceEnacted(int id) const override { return c->ordinances.enacted(uint32_t(id)); }
  bool neighborConnection(int n, int kind) const override {
    if (n < 0 || n >= kNeighbours) return false;
    const Neighbour& nb = c->neighbours.neighbours[n];
    switch (kind) { case 0: return nb.nRail; case 1: return nb.nRoad; case 2: return nb.nPipe; case 3: return nb.nPower; case 4: return nb.nHighway; default: return nb.nSubway; }
  }
  bool neighborDealPresent(int n, int type) const override {
    for (const auto& d : c->neighbours.active) if (d.neighbourIndex == n && d.dealType == type && !d.terminated()) return true;
    return false;
  }
  bool businessDealActive(int id) const override {
    for (uint32_t b : c->business.inForce) if (int(b) == id) return true; return false;
  }
};

struct Host : IScenarioHost {
  ScenarioHooks* h = nullptr;
  std::function<std::string(uint32_t, uint32_t)> resolve;
  ScenarioFile* file = nullptr;
  void popUp(const PopUpRequest& r) override {
    if (!h || !h->popUp) return;
    std::string t = resolve ? resolve(r.group, r.id) : std::string();
    uint32_t adv = r.presenter ? uint32_t(r.presenter) : (file ? file->header.effectivePresenter() : kAdvisorUtilities);
    h->popUp(t, advisorIndex(adv), adv, r.mood ? r.mood : 3, r.important != 0);
  }
  void newsTicker(uint32_t g, uint32_t id) override {
    if (!h || !h->ticker) return;
    int cls = 6; bool ul = false;
    if (file) { TickerStyle s = file->styles.resolve(id); cls = s.cls; ul = s.underline; }
    h->ticker(resolve ? resolve(g, id) : std::string(), cls, ul);
  }
  void playSound(int32_t s) override { if (h && h->sound) h->sound(s); }
  bool moveCamera(int x, int z, int zoom) override { if (h && h->moveCamera) h->moveCamera(x, z, zoom); return true; }
  void setSpeed(int s) override { if (h && h->setSpeed) h->setSpeed(s); }
  void startDisaster(DisasterKind k, const std::vector<int32_t>& p) override { if (h && h->startDisaster) h->startDisaster(int(k), p); }
  void setDisastersEnabled(bool b) override { if (h && h->setDisastersEnabled) h->setDisastersEnabled(b); }
  void setNeighborDealsBlocked(int t, bool b) override { if (h && h->blockDeals) h->blockDeals(t, b); }
  void offerRewardBuilding(uint32_t b) override { if (h && h->offerReward) h->offerReward(b); }
  void offerBusinessDeal(int d) override { if (h && h->offerBusiness) h->offerBusiness(d); }
  void setBusinessDealEnabled(int d, bool e) override { if (h && h->enableBusiness) h->enableBusiness(d, e); }
  void endScenario(int rank) override { if (h && h->ended) h->ended(rank); }
};
}  // namespace

struct ScenarioController::Impl {
  ScenarioFile file;
  bool loaded = false;
  ScenarioHooks hooks;
  Stats stats;
  Host host;
  std::unique_ptr<ScenarioRuntime> rt;
  City* city = nullptr;
  std::string mayor, cityName;
  std::string res(uint32_t g, uint32_t id) const {
    std::string s = hooks.text ? hooks.text(g, id) : std::string();
    if (s.empty()) { auto it = file.strings.find(id); if (it != file.strings.end()) s = it->second; }
    return s;
  }
  ScenarioTextContext ctx() const {
    ScenarioTextContext c; c.mayor = mayor; c.city = cityName;
    c.scenarioName = res(file.header.title.group, file.header.title.id);
    if (city) { c.population = city->population(); c.year = city->clock.date().year; }
    if (rt) c.vars = &rt->state().vars;
    return c;
  }
};

ScenarioController::ScenarioController() : p_(new Impl) {}
ScenarioController::~ScenarioController() = default;

bool ScenarioController::load(const Bytes& file, std::string& err) {
  p_->loaded = ScenarioFile::load(file, p_->file, &err);
  return p_->loaded;
}

bool ScenarioController::peek(const Bytes& file, const std::function<std::string(uint32_t, uint32_t)>& text, ScenarioInfo& out) {
  ScenarioFile f; std::string err;
  if (!ScenarioFile::load(file, f, &err)) return false;
  ScenarioChooserInfo ci = f.chooserInfo();
  auto get = [&](ResKey k) {
    std::string s = text ? text(k.type, k.group) : std::string();
    return s;
  };
  (void)get;
  auto lookup = [&](uint32_t table, uint32_t id) {
    std::string s = text ? text(table, id) : std::string();
    if (s.empty()) { auto it = f.strings.find(id); if (it != f.strings.end()) s = it->second; }
    return s;
  };
  out.title = lookup(ci.stringTable, ci.string1.instance);
  out.description = lookup(ci.stringTable, ci.string0.instance);
  if (out.title.empty()) out.title = lookup(f.header.title.group, f.header.title.id);
  out.author = ci.author.empty() ? f.shortAuthor : ci.author;
  out.shortAuthor = f.shortAuthor.empty() ? out.author : f.shortAuthor;
  out.picture = f.picture;
  return true;
}

void ScenarioController::start(City& city, const std::string& mayor, const std::string& cityName, ScenarioHooks hooks) {
  if (!p_->loaded) return;
  p_->hooks = std::move(hooks);
  p_->city = &city; p_->mayor = mayor; p_->cityName = cityName;
  p_->stats.c = &city;
  p_->host.h = &p_->hooks; p_->host.file = &p_->file;
  p_->host.resolve = [this](uint32_t g, uint32_t id) { return p_->res(g, id) .empty() ? std::string() : expandScenarioTokens(p_->res(g, id), p_->ctx()); };
  p_->rt.reset(new ScenarioRuntime(p_->file, p_->stats, p_->host, 1));
  p_->rt->start();
}

void ScenarioController::dayTick() { if (p_->rt) p_->rt->dayTick(); }
void ScenarioController::minuteTick() { if (p_->rt) p_->rt->minuteTick(); }
bool ScenarioController::active() const { return p_->rt && p_->rt->active(); }
bool ScenarioController::started() const { return p_->rt != nullptr; }
void ScenarioController::setPopupOpen(bool o) { if (p_->rt) p_->rt->setPopupOpen(o); }

ScenarioGoalsView ScenarioController::goalsView() const {
  ScenarioState empty;
  const ScenarioState& st = p_->rt ? p_->rt->state() : empty;
  ScenarioGoalsView v = makeScenarioGoalsView(p_->hooks.text, st, &p_->file, p_->ctx());
  return v;
}

ScenarioController::Outcome ScenarioController::outcome() const {
  Outcome o;
  if (!p_->rt) return o;
  ScenarioResult r = p_->rt->result();
  o.ended = r.ended; o.rank = r.rank; o.goalsMet = r.goalsMet; o.goalsTotal = r.goalsTotal;
  o.win = r.ended && r.allGoalsMet;
  ScenarioTextContext c = p_->ctx();
  if (r.rankTitle.id) o.rankTitle = expandScenarioTokens(p_->res(r.rankTitle.group, r.rankTitle.id), c);
  if (r.resultsText.id) o.text = expandScenarioTokens(p_->res(r.resultsText.group, r.resultsText.id), c);
  return o;
}

std::string ScenarioController::title() const { return expandScenarioTokens(p_->res(p_->file.header.title.group, p_->file.header.title.id), p_->ctx()); }

std::string ScenarioController::briefing() const {
  if (!p_->rt) return "";
  const ScenarioState& st = p_->rt->state();
  if (!st.dialogText.id) return "";
  return expandScenarioTokens(p_->res(st.dialogText.group, st.dialogText.id), p_->ctx());
}

Bytes ScenarioController::mergeState(const Bytes& cityFile) const {
  if (!p_->loaded) return cityFile;
  ScenarioFile f = p_->file;
  if (p_->rt) { f.state = p_->rt->snapshot(); f.hasState = true; }
  return f.mergeInto(cityFile);
}

}  // namespace m3k
