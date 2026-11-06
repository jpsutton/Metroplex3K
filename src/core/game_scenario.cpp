#include "core/game_scenario.h"
#include "core/script_coro.h"
#include <algorithm>
#include "core/scenario.h"
#include "core/windows_scenario.h"

namespace m3k {

namespace {
struct Stats : ICityStats {
  City* c = nullptr;
  std::function<uint32_t(uint32_t)> pieceAgent;
  const World* w() const { return c->world(); }
  int32_t population() const override { return c->population(); }
  int32_t landValue() const override { return c->landValue.cityAverage(); }
  int32_t crime() const override { return c->crime.avgCrime; }
  int32_t aura() const override {
    const auto& g = c->aura.grid; if (g.empty()) return 0;
    int64_t s = 0; for (int8_t v : g) s += v; return int32_t(s / int64_t(g.size()));
  }
  int32_t pollution(int kind) const override {
    // Kind 2: floor(sum of standing garbage / N) as u16, with the divisor of kinds 0 and 1 (spec 23, held between sweeps).
    if (kind == 2) return c->pollution.pollutedCount ? int32_t(uint16_t(c->pollution.totalStanding / c->pollution.pollutedCount)) : 0;
    return kind == 0 ? int32_t(c->pollution.avgAir) : int32_t(c->pollution.avgWater);
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
  // The argument of cmdCountStructures / cmdCountStructuresNearLocation is an AGENT TYPE; a parent type matches its subtypes (spec 23).
  // Sources are tried while the count is 0: occupants, then network pieces (a third source, probably flora and props, is not modelled).
  int countType(uint32_t type, int x0, int z0, int x1, int z1) const {
    if (!w()) return 0;
    const auto& hier = c->agentHierarchy();
    auto in = [&](int x, int z) { return x >= x0 && x <= x1 && z >= z0 && z <= z1; };
    int n = 0;
    for (const auto& o : w()->occupants)
      if (in(o.x, o.z) && hier.isAncestorOrSelf(o.agentType, type)) n++;
    if (n) return n;
    // Surface network pieces by their own agent type (road, highway, rail, power, bridge parts, tunnels, ramps ...).
    for (const auto& p : w()->surface) {
      if (!in(p.x, p.z)) continue;
      if (pieceAgent) { const uint32_t a = pieceAgent(p.pieceId); if (a && hier.isAncestorOrSelf(a, type)) n++; }
      else if ((type == 16000 && c->pieceInKind(p.pieceId, NetworkKind::Road)) || (type == 17000 && c->pieceInKind(p.pieceId, NetworkKind::Rail))) n++;
    }
    if (n) return n;
    // Third source [INFERRED]: the underground layers (pipes 17300, subway 17400).
    for (const auto& p : w()->pipes) if (in(p.x, p.z) && hier.isAncestorOrSelf(17300, type)) n++;
    for (const auto& p : w()->subway) if (in(p.x, p.z) && hier.isAncestorOrSelf(17400, type)) n++;
    return n;
  }
  // Structures whose occupant key is exactly (T, G, I); the whole map has centre (0,0) and radius max(W,H)+1 (spec 23).
  int countKey(uint32_t t, uint32_t g, uint32_t i, int x0, int z0, int x1, int z1) const {
    if (!w() || g != 0x57E || (t != 0x207EDC0Eu && t != 0x220055E1u)) return 0;
    int n = 0;
    for (const auto& o : w()->occupants)
      if (o.occId == i && o.keyType == t && o.x >= x0 && o.x <= x1 && o.z >= z0 && o.z <= z1) n++;
    return n;
  }
  int32_t countStructures(uint32_t type) const override { return w() ? countType(type, 0, 0, w()->W, w()->H) : 0; }
  int32_t countStructuresNear(uint32_t type, int x, int z, int r) const override {
    if (!w()) return 0;
    // Script coordinates are 1-based; one above the map size becomes 0. The rectangle is [centre - r, centre + r], clamped to the map.
    int cx = x - 1, cz = z - 1;
    if (cx > w()->W) cx = 0;
    if (cz > w()->H) cz = 0;
    return countType(type, std::max(0, cx - r), std::max(0, cz - r), std::min(w()->W, cx + r), std::min(w()->H, cz + r));
  }
  int32_t countAppearance(uint32_t t, uint32_t g, uint32_t i) const override { return w() ? countKey(t, g, i, 0, 0, w()->W, w()->H) : 0; }
  int32_t countAppearanceNear(uint32_t t, uint32_t g, uint32_t i, int x, int z, int r) const override {
    if (!w()) return 0;
    int cx = x - 1, cz = z - 1;
    if (cx > w()->W) cx = 0;
    if (cz > w()->H) cz = 0;
    return countKey(t, g, i, std::max(0, cx - r), std::max(0, cz - r), std::min(w()->W, cx + r), std::min(w()->H, cz + r));
  }
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
  ScriptCoroutine* co = nullptr;   // the script suspends inside cmdPopUpMsg until the player closes the pop-up
  std::function<std::string(uint32_t, uint32_t)> resolve;
  ScenarioFile* file = nullptr;
  void popUp(const PopUpRequest& r) override {
    if (!h || !h->popUp) return;
    std::string t = resolve ? resolve(r.group, r.id) : std::string();
    // Presenter: id 0 = the header's default; a4 = kind (0 game table, 1 advisor, 2 scenario table; spec 23 4.7.0).
    uint32_t pid = uint32_t(r.presenter), kind = uint32_t(r.a4);
    if (!pid && file) { pid = file->header.presenter; kind = file->header.presenterKind; }
    uint32_t adv = kAdvisorUtilities;
    ScenarioPresenterPic pic;
    if (file) {
      PresenterChoice pc = resolvePresenterChoice(h->text, h->neighbourMayor, *file, pid, kind);
      adv = pc.advisor; pic = pc.pic;
      if (kind >= 2) pic.group = pic.inst = 0;   // original defect: kind >= 2 pop-ups never store the "portrait found" flag (0x10001A1B): name only
    } else if (pid) adv = pid;
    h->popUp(t, advisorIndex(adv), adv, r.mood ? r.mood : 3, r.important != 0, pic);
    if (co) co->yield();   // resumes after the OK (ScenarioController::popupClosed)
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
  ScriptCoroutine co;   // declared after rt: destroyed first, so a suspended script unwinds before the runtime goes
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
  p_->stats.pieceAgent = p_->hooks.pieceAgentType;
  p_->host.h = &p_->hooks; p_->host.file = &p_->file; p_->host.co = &p_->co;
  p_->host.resolve = [this](uint32_t g, uint32_t id) { return p_->res(g, id) .empty() ? std::string() : expandScenarioTokens(p_->res(g, id), p_->ctx()); };
  p_->rt.reset(new ScenarioRuntime(p_->file, p_->stats, p_->host, 1));
  p_->co.run([this] { p_->rt->start(); });
}

// A script blocked in a pop-up ignores ticks until the player closes it; every pass runs on the coroutine so that the pop-up command can suspend it.
void ScenarioController::dayTick() { if (p_->rt && !p_->co.suspended()) p_->co.run([this] { p_->rt->dayTick(); }); }
void ScenarioController::minuteTick() { if (p_->rt && !p_->co.suspended()) p_->co.run([this] { p_->rt->minuteTick(); }); }
bool ScenarioController::scriptWaiting() const { return p_->co.suspended(); }
void ScenarioController::popupClosed() { p_->co.resume(); }
void ScenarioController::setMayor(const std::string& m) { p_->mayor = m; }
bool ScenarioController::active() const { return p_->rt && p_->rt->active(); }
bool ScenarioController::started() const { return p_->rt != nullptr; }
bool ScenarioController::resumed() const { return p_->loaded && p_->file.hasState; }
void ScenarioController::setPopupOpen(bool o) { if (p_->rt) p_->rt->setPopupOpen(o); }

ScenarioGoalsView ScenarioController::goalsView() const {
  ScenarioState empty;
  const ScenarioState& st = p_->rt ? p_->rt->state() : empty;
  ScenarioGoalsView v = makeScenarioGoalsView(p_->hooks.text, st, &p_->file, p_->ctx(), p_->hooks.neighbourMayor);
  v.startMode = false;
  v.title = title();   // the title bar shows the scenario's title (spec 23 4.7.1)
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
  for (const auto& kv : p_->rt->state().ranks) {
    if (int(kv.first) == r.rank) o.achieved = int(o.ranks.size());
    o.ranks.push_back(expandScenarioTokens(p_->res(kv.second.group, kv.second.id), c));
  }
  { PresenterChoice pc = resolvePresenterChoice(p_->hooks.text, p_->hooks.neighbourMayor, p_->file, p_->file.header.presenter, p_->file.header.presenterKind);
    o.presenterAdvisor = pc.advisor; o.presenterPic = pc.pic; }
  o.mood = p_->rt->state().presenterMood ? int(p_->rt->state().presenterMood) : 3;   // the mood of cmdSetResultsDlgText
  o.title = title(); o.mayor = c.mayor;
  o.resultsFor = expandScenarioTokens(p_->res(0x041F2625, 0x25), c);   // "Results for Mayor %YOURNAME%"
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
