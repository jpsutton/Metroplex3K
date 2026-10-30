// Game shell core: menu tree, tool menu, tool dispatch, HUD, map interaction, frame loop.
// See game.h for the split over game_front.cpp / game_windows.cpp / game_sim.cpp.
#include "core/game.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "core/game_scenario.h"
#include "core/gamedir.h"
#include "core/terrain_gen.h"

namespace m3k {

static std::vector<std::string> splitFields(const std::string& s) {
  std::vector<std::string> v; std::string cur;
  for (char c : s) { if (c == ',') { v.push_back(trimSpaces(cur)); cur.clear(); } else cur.push_back(c); }
  v.push_back(trimSpaces(cur));
  return v;
}

bool MenuTree::parse(const IniFile& ini) {
  for (const auto& kv : ini.section("SC3MenuItemInfo")) {
    auto f = splitFields(kv.second);
    if (f.size() < 10) continue;
    MenuItemInfo m;
    m.enabled = parseIdNumber(f[0]) != 0;
    m.nameTable = parseIdNumber(f[1]); m.nameId = parseIdNumber(f[2]);
    m.kind = f[5]; m.target = parseIdNumber(f[6]); m.arg1 = parseIdNumber(f[7]); m.arg2 = parseIdNumber(f[8]);
    items[parseIdNumber(kv.first)] = m;
  }
  for (const auto& kv : ini.section("SC3MenuBtnDefs")) {
    auto f = splitFields(kv.second);
    if (f.size() < 2) continue;
    MenuButton b; b.id = parseIdNumber(kv.first); b.item = parseIdNumber(f[0]); b.sub = parseIdNumber(f[1]);
    buttons[b.id] = b;
  }
  for (const auto& kv : ini.section("SC3MenuDescs")) {
    auto f = splitFields(kv.second);
    if (f.size() < 5) continue;
    MenuDesc d; d.id = parseIdNumber(kv.first);
    d.titleTable = parseIdNumber(f[0]); d.titleId = parseIdNumber(f[1]); d.colour = parseIdNumber(f[2]);
    int n = int(parseIdNumber(f[4]));
    for (int i = 0; i < n && 5 + i < int(f.size()); i++) { if (f[5 + i] == "END") break; d.buttons.push_back(parseIdNumber(f[5 + i])); }
    menus[d.id] = d;
  }
  return !menus.empty();
}

void MenuTree::buildFallback() {
  items.clear(); buttons.clear(); menus.clear();
  uint32_t nextItem = 0x10000001, nextBtn = 1;
  auto item = [&](const char* name, uint32_t target, uint32_t arg1 = 0, uint32_t arg2 = 0) {
    MenuItemInfo m; m.kind = "COMM"; m.target = target; m.arg1 = arg1; m.arg2 = arg2; m.fallbackName = name;
    items[nextItem] = m; return nextItem++;
  };
  auto btn = [&](uint32_t it, uint32_t sub = 0) { MenuButton b; b.id = nextBtn++; b.item = it; b.sub = sub; buttons[b.id] = b; return b.id; };
  auto menu = [&](uint32_t id, const char* title, std::vector<uint32_t> bs, uint32_t colour = 0) {
    MenuDesc d; d.id = id; d.buttons = std::move(bs); d.fallbackTitle = title; d.colour = colour; menus[id] = d;
  };
  auto leaf = [&](const char* n, uint32_t t, uint32_t a1 = 0) { return btn(item(n, t, a1)); };
  // Fly-out groups.
  menu(0xD1003100, "Residential", {leaf("Light residential", 0x42E88586), leaf("Medium residential", 0x02E88915), leaf("Dense residential", 0xA2E8893B)}, 0x00C000);
  menu(0xD1003200, "Commercial", {leaf("Light commercial", 0x02E88949), leaf("Medium commercial", 0xA2E8895A), leaf("Dense commercial", 0x02E88968)}, 0x0000C0);
  menu(0xD1003300, "Industrial", {leaf("Light industrial", 0xC2E8898F), leaf("Medium industrial", 0xA2E8899D), leaf("Dense industrial", 0x02E889AA)}, 0xC0C000);
  menu(0xD1004100, "Highway", {leaf("Highway", 0x42B5DE86)}, 0x808080);
  menu(0xD1004200, "Rail", {leaf("Lay rail", 0x42B5DE98), leaf("Train station", 0x22FF058F)}, 0x808080);
  menu(0xD1004300, "Subway", {leaf("Lay subway", 0xC2B5DEA6), leaf("Subway station", 0x62FF059B)}, 0x808080);
  menu(0xD1006100, "Police", {leaf("Police station", 0x42FF052F), leaf("Jail", 0x22FF053F)}, 0x4040C0);
  menu(0xD1006200, "Health & Education", {leaf("Hospital", 0xE2FF0555), leaf("School", 0xC2FF0563), leaf("College", 0xC2FF0571), leaf("Library", 0xC2FF057A), leaf("Museum", 0x62FF0585)}, 0xC04040);
  menu(0xD1006300, "Parks", {leaf("Small park", 0x02FF05AA), leaf("Large park", 0x82FF05B7), leaf("Fountain", 0x22FF05F3), leaf("Pond", 0x22FF05EA),
                             leaf("Playground", 0xC2FF05E0), leaf("Zoo", 0x22FF05CA), leaf("Ballpark", 0x62FF05D1)}, 0x40C040);
  menu(0xD100A100, "Start disaster", {leaf("Fire", 0xC26CE54D), leaf("Tornado", 0x626CE901), leaf("Earthquake", 0x22FE8786), leaf("Riot", 0x02FE87A7),
                                       leaf("UFO attack", 0x42FE87AE), leaf("Toxic cloud", 0x447F1E40), leaf("Whirlpool", 0x447F1E41),
                                       leaf("Locusts", 0x447F1E42), leaf("Space debris", 0x447F1E43)}, 0xC00000);
  menu(0xD1008100, "Take snapshot", {btn(item("Small", 0x2435FF70, 1)), btn(item("Medium", 0x2435FF70, 2)), btn(item("Large", 0x2435FF70, 3))}, 0x808080);
  menu(0xD1009100, "Load", {leaf("Saved city", 0x610438A4, 0x83172AD7), leaf("Starter town", 0x610438A4, 0x63172B74),
                            leaf("Real terrain", 0x610438A4, 0x23172AF2), leaf("Scenario", 0x610438A4, 0xA42C7725)}, 0x808080);
  auto dem = [&] { return leaf("Demolish", 0xE3166801); };
  menu(0xD1002000, "Landscape", {leaf("Trees", 0x4253EC3F), leaf("Water", 0x02E763C1), leaf("Lower", 0xC25AD94F), leaf("Raise", 0x825AD7F6), leaf("Level", 0x825AD954), dem()});
  menu(0xD1003000, "Zone", {btn(0, 0xD1003100), btn(0, 0xD1003200), btn(0, 0xD1003300), leaf("Landfill", 0xC2E889B6), leaf("Seaport", 0x02E889C4),
                            leaf("Airport", 0xE2E889D4), leaf("De-zone", 0x82E88C9C), dem()});
  menu(0xD1004000, "Transportation", {leaf("Road", 0xC2B5DE77), btn(0, 0xD1004100), leaf("Bus stop", 0x02FF04FE), btn(0, 0xD1004200), btn(0, 0xD1004300),
                                      leaf("Subway to rail", 0x22FF0521), dem()});
  menu(0xD1005000, "Utilities", {leaf("Power lines", 0x42C2F8BF), leaf("Power plants", 0xE2ED3560, 0x62E925A2), leaf("Pipes", 0x22C2F8C5),
                                 leaf("Water structures", 0xE2ED3560, 0xE2E92527), leaf("Garbage", 0xE2ED3560, 0xC2F291A9), dem()});
  menu(0xD1006000, "Civic", {btn(0, 0xD1006100), leaf("Fire station", 0x22FF054C), btn(0, 0xD1006200), btn(0, 0xD1006300),
                             leaf("Landmarks", 0xE2ED3560, 0x22E92581), leaf("Rewards", 0xE2ED3560, 0x62E92553), dem()});
  menu(0xD1007000, "Meet", {leaf("Meet advisors", 0x724A82D0)});
  menu(0xD1008000, "Review", {leaf("View snapshots", 0x2435FF70, 0), btn(0, 0xD1008100), leaf("Budget", 0x025DDC4A), leaf("Neighbors", 0x630288E9),
                              leaf("Ordinances", 0x724A82D7), leaf("Scenario goals", 0x844587AE), leaf("Map view", 0x411164F1)});
  menu(0xD1009000, "Options", {leaf("Preferences", 0xC3001401), leaf("City properties", 0x855A2DC4), leaf("Save", 0x410438DD),
                               leaf("Save as", 0x410438DD, 0xC3172AEA), btn(0, 0xD1009100), leaf("New city", 0xE3270FE9), leaf("Exit", 0xA10438E9)});
  menu(0xD100A000, "Emergency", {leaf("Go to disaster", 0xC30177CA), leaf("Warning siren", 0x02B5AECF), leaf("Dispatch police", 0x029A52DD),
                                 leaf("Dispatch fire", 0x229A528C), leaf("Dispatch plane", 0xA5354652), btn(0, 0xD100A100)});
  menu(0xD1001000, "", {btn(0, 0xD1002000), btn(0, 0xD1003000), btn(0, 0xD1004000), btn(0, 0xD1005000), btn(0, 0xD1006000), btn(0, 0xD1007000),
                        btn(0, 0xD1008000), btn(0, 0xD1009000), btn(0, 0xD100A000)});
}

std::string Game::str(uint32_t table, uint32_t id) { return haveData_ ? data_.string(table, id) : std::string(); }

const Image* Game::icon(uint32_t group, uint32_t inst) {
  if (!haveData_) return nullptr;
  std::string k = std::to_string(group) + ":" + std::to_string(inst);
  auto it = iconCache_.find(k);
  if (it == iconCache_.end()) {
    Image im;
    ResourceRef r = data_.db().find({T_UI_IMAGE, group, inst});
    if (r) { try { im = decodeUiImage(r.data, r.available, true); } catch (const FormatError&) {} }
    it = iconCache_.emplace(k, std::move(im)).first;
  }
  return it->second.empty() ? nullptr : &it->second;
}

void Game::ensureFonts() {
  if (!haveData_) return;
  fs::path dir = resolveCI(data_.appsDir(), "Res/Text/" + data_.language);
  if (!dir.empty()) fonts_.load(dir);
}

Game::Game() { menu_.buildFallback(); menuLoaded_ = true; loadHotkeys(); }
Game::~Game() = default;

void Game::loadHotkeys() {
  hotkeys_.clear();
  const struct { char k; uint32_t t; } hk[] = {
      {'q', 0x42E88586}, {'w', 0x02E88915}, {'e', 0xA2E8893B}, {'a', 0x02E88949}, {'s', 0xA2E8895A}, {'d', 0x02E88968},
      {'z', 0xC2E8898F}, {'x', 0xA2E8899D}, {'c', 0x02E889AA}, {'v', 0x82E88C9C}, {'l', 0x42C2F8BF}, {'r', 0xC2B5DE77},
      {'t', 0x42B5DE98}, {'b', 0xE3166801}};
  for (const auto& h : hk) hotkeys_[h.k] = h.t;
}

bool Game::init(const fs::path& gameRoot, std::string& err) {
  ui_.fonts = &fonts_;
  loadSettings();
  if (gameRoot.empty()) { scr_ = Screen::NeedData; return true; }
  GameData g;
  if (!g.open(gameRoot, err)) { scr_ = Screen::NeedData; return false; }
  data_ = std::move(g);
  haveData_ = true; root_ = data_.root();
  saveGameRoot(root_);
  ensureFonts();
  cityCfg_ = CityConfig();
  cityCfg_.load(data_);
  initAudio();
  loadSchemes();
  IniFile mi;
  if (data_.ini().load("MenuItem.INI", mi)) {
    MenuTree t;
    if (t.parse(mi) && t.menus.count(0xD1001000)) { menu_ = std::move(t); menuLoaded_ = true; }
  }
  scr_ = introEnabled_ ? Screen::Intro : Screen::MainMenu;
  introTried_ = false;
  return true;
}

void Game::startDemo(int size) {
  haveData_ = false;
  cityCfg_ = CityConfig();
  cityCfg_.applyDemoData();
  ui_.fonts = &fonts_;
  TerrainGenOptions o; o.seed = 4242;
  World w; generateTerrain(w, size, size, o);
  w.info.cityName = "Demo"; w.info.mayorName = "Mayor"; w.info.year = 1950; w.info.foundingYear = 1950;
  w.info.fileKind = 3; w.view = {size / 2.f, size / 2.f, 2, 0, true};
  adopt(std::move(w));
}

void Game::dropPath(const std::string& path) {
  fs::path p = path;
  std::error_code ec;
  if (fs::is_regular_file(p, ec) && looksLikeGogInstaller(p)) {
    std::string msg;
    fs::path r = extractGogInstaller(p, configDir() / "game", msg);
    if (!r.empty()) { std::string err; if (init(r, err)) { message("Game data ready."); return; } msg = err; }
    message(msg);
    return;
  }
  if (fs::is_regular_file(p, ec) && haveData_) { loadFile(p); return; }
  fs::path r = findGameRoot(p, 3);
  if (r.empty()) { message("No SimCity 3000 Unlimited data found in that location."); return; }
  std::string err;
  if (!init(r, err)) message(err);
}

void Game::requestFile(const std::string& path) { if (haveData_) { introTried_ = true; loadFile(path); } }

std::string Game::windowTitle() const {
  std::string t = "Metroplex3K";
  if (scr_ == Screen::InCity && world_.W) t += " - " + world_.info.cityName;
  return t;
}

void Game::message(const std::string& s) { msgText_ = s; lastMsg_ = s; }

void Game::centreOn(int x, int z) { if (world_.W) renderer_.centreOnWorld(cam_, float(x), float(z)); }

void Game::setView(int tx, int tz, int zoom) {
  cam_.zoom = std::max(0, std::min(4, zoom));
  centreOn(tx, tz);
}

bool Game::tileScreenPos(int tx, int tz, int& sx, int& sy) {
  if (!world_.W || !world_.inTiles(tx, tz) || renderer_.width() <= 0) return false;
  int a = (world_.altAt(tx, tz) + world_.altAt(tx + 1, tz) + world_.altAt(tx, tz + 1) + world_.altAt(tx + 1, tz + 1)) / 4;
  int px, py;
  renderer_.projectSub(cam_, tx * 256 + 128, tz * 256 + 128, a * 256, px, py);
  for (int r = 0; r <= 12; r++)
    for (int dy = -r; dy <= r; dy++)
      for (int dx = -r; dx <= r; dx++) {
        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
        int x = px + dx, y = py + dy, gx, gz;
        if (x < 0 || y < 0 || x >= renderer_.width() || y >= renderer_.height()) continue;
        if (renderer_.pickGround(cam_, x, y, gx, gz) && gx == tx && gz == tz) { sx = x; sy = y; return true; }
      }
  return false;
}

bool Game::windowOpen(const std::string& n) const {
  if (n == "budget") return showBudget_;
  if (n == "ordinances") return showOrd_;
  if (n == "neighbours") return showNeigh_;
  if (n == "deals") return showDeals_;
  if (n == "advisors") return showAdv_;
  if (n == "prefs") return showPrefs_;
  if (n == "props") return showProps_;
  if (n == "goals") return showGoals_;
  if (n == "map") return showMap_;
  if (n == "graphs") return showGraphs_;
  if (n == "charts") return showCharts_;
  if (n == "picker") return showPicker_;
  if (n == "query") return showQuery_;
  return false;
}

void Game::initAudio() {
  audio_.reset();
  if (!haveData_) return;
  fs::path dir = resolveCI(data_.appsDir(), "Res/Sound");
  if (dir.empty()) return;
  fs::path ini = resolveCI(dir, "AUDIO.INI");
  Bytes b;
  if (ini.empty() || !readFile(ini, b)) return;
  auto a = std::make_unique<AudioSystem>();
  a->init(AudioIni::parse(std::string(b.begin(), b.end())), makeDirLoader(dir));
  audio_ = std::move(a);
  applySettingsToAudio();
}

// ---------------------------------------------------------------- tools & menu

void Game::setTool(ToolKind k, int a, uint32_t occ) {
  tool_ = k; toolArg_ = a; toolOcc_ = occ; dragging_ = false;
  hoverValid_ = false; scene_.previews.clear(); scene_.cost.show = false; costShow_ = false;
  if (k == ToolKind::None) toolName_.clear();
}

void Game::runTarget(uint32_t target, uint32_t arg1, const std::string& name) {
  MenuItemInfo mi; mi.kind = "COMM"; mi.target = target; mi.arg1 = arg1; mi.fallbackName = name;
  dispatch(mi);
}

void Game::execute(uint32_t menuItemId) {
  auto it = menu_.items.find(menuItemId);
  if (it != menu_.items.end() && it->second.enabled) dispatch(it->second);
}

void Game::dispatch(const MenuItemInfo& mi) {
  struct Z { uint32_t cls; int zone; };
  static const Z zones[] = {{0x42E88586, 1}, {0x02E88915, 2}, {0xA2E8893B, 3}, {0x02E88949, 5}, {0xA2E8895A, 6}, {0x02E88968, 7},
                            {0xC2E8898F, 9}, {0xA2E8899D, 10}, {0x02E889AA, 11}, {0xE2E889D4, 14}, {0x02E889C4, 15}, {0xC2E889B6, 17}, {0x82E88C9C, 0}};
  struct P { uint32_t cls; uint32_t occ; };
  static const P plops[] = {{0x42FF052F, 0xD2}, {0x22FF053F, 0x1F41}, {0x22FF054C, 0xD3}, {0xE2FF0555, 0xD1}, {0xC2FF0563, 0x1F94}, {0xC2FF0571, 0xD6},
                            {0xC2FF057A, 0xF5}, {0x62FF0585, 0xD4}, {0x02FF05AA, 0x0D}, {0x82FF05B7, 0xD5}, {0x22FF05CA, 0xDA}, {0x62FF05D1, 0xD7},
                            {0xC2FF05E0, 0x234B}, {0x22FF05EA, 0x234C}, {0x22FF05F3, 0x234E}, {0x02FF04FE, 0x1F96}, {0x22FF058F, 0xED},
                            {0x62FF059B, 0xE9}, {0x22FF0521, 0x3E8A}};
  struct D { uint32_t cls; DisasterType t; };
  static const D disas[] = {{0xC26CE54D, DisasterType::Fire}, {0x626CE901, DisasterType::Tornado}, {0x22FE8786, DisasterType::Earthquake},
                            {0x02FE87A7, DisasterType::Riot}, {0x42FE87AE, DisasterType::UFO}, {0x447F1E40, DisasterType::ToxicCloud},
                            {0x447F1E41, DisasterType::Whirlpool}, {0x447F1E42, DisasterType::Locusts}, {0x447F1E43, DisasterType::SpaceJunk}};
  std::string name = str(mi.nameTable, mi.nameId);
  if (name.empty()) name = mi.fallbackName;
  uint32_t t = mi.target;
  for (const auto& z : zones) if (t == z.cls) { setTool(ToolKind::Zone, z.zone); toolName_ = name; return; }
  for (const auto& p : plops) if (t == p.cls) { setTool(ToolKind::Plop, 0, p.occ); toolName_ = name; return; }
  for (const auto& d : disas) if (t == d.cls) { startDisaster(d.t); return; }
  PickerCategory pc;
  switch (t) {
    case 0xC2B5DE77: setTool(ToolKind::Road, int(NetworkKind::Road)); toolName_ = name; return;
    case 0x42B5DE86: setTool(ToolKind::Road, int(NetworkKind::Highway)); toolName_ = name; return;
    case 0x42B5DE98: setTool(ToolKind::Road, int(NetworkKind::Rail)); toolName_ = name; return;
    case 0xC2B5DEA6: setTool(ToolKind::Road, int(NetworkKind::Subway)); toolName_ = name; return;
    case 0x42C2F8BF: setTool(ToolKind::Road, int(NetworkKind::Power)); toolName_ = name; return;
    case 0x22C2F8C5: setTool(ToolKind::Road, int(NetworkKind::Pipe)); toolName_ = name; return;
    case 0xE2ED3560:
      if (pickerCategoryFromParam(mi.arg1, pc)) { openPicker(mi.arg1); return; }
      break;
    case 0x62E925A2: case 0xE2E92527: case 0xC2F291A9: case 0x22E92581: case 0x62E92553: openPicker(t); return;
    case 0x825AD7F6: setTool(ToolKind::Terrain, 1); toolName_ = name; return;
    case 0xC25AD94F: setTool(ToolKind::Terrain, 2); toolName_ = name; return;
    case 0x825AD954: setTool(ToolKind::Terrain, 4); toolName_ = name; return;
    case 0x02E763C1: setTool(ToolKind::Terrain, 32); toolName_ = name; return;
    case 0x4253EC3F: setTool(ToolKind::Terrain, 100); toolName_ = name; return;
    case 0xE3166801: setTool(ToolKind::Demolish); toolName_ = name; return;
    case 0x029A52DD: setTool(ToolKind::Dispatch, 0); toolName_ = name; return;
    case 0x229A528C: setTool(ToolKind::Dispatch, 1); toolName_ = name; return;
    case 0xA5354652: setTool(ToolKind::Dispatch, 2); toolName_ = name; return;
    case 0x02B5AECF: sirenAndGo(); return;
    case 0xC30177CA: gotoDisaster(); return;
    case 0x724A82D0: openWindow("advisors"); return;
    case 0x724A82D7: openWindow("ordinances"); return;
    case 0x025DDC4A: openWindow("budget"); return;
    case 0x630288E9: if (showNeigh_) showNeigh_ = false; else openWindow("neighbours"); return;
    case 0x411164F1: if (showMap_) showMap_ = false; else openWindow("map"); return;
    case 0x844587AE:
      if (scenarioActive()) openWindow("goals"); else message("There is no scenario goal in this city.");
      return;
    case 0xC3001401: openWindow("prefs"); return;
    case 0x855A2DC4: openWindow("props"); return;
    case 0x2435FF70:
      if (mi.arg1 >= 1 && mi.arg1 <= 3) { snapSizeIdx_ = int(mi.arg1); takeSnapshot(int(mi.arg1)); }
      else message(snapshotNames_.empty() ? "No snapshots taken yet." : "Snapshots: " + std::to_string(snapshotNames_.size()) + " taken this session.");
      return;
    case 0x410438DD:
      if (mi.arg1 == 0xC3172AEA || currentFile_.empty()) { saveMode_ = true; refreshFiles(0); saveName_ = world_.info.cityName; scr_ = Screen::LoadList; }
      else saveCurrent(currentFile_.stem().string());
      return;
    case 0x610438A4:
      saveMode_ = false;
      refreshFiles(mi.arg1 == 0x63172B74 ? 1 : mi.arg1 == 0x23172AF2 ? 2 : mi.arg1 == 0xA42C7725 ? 3 : 0);
      scr_ = Screen::LoadList; return;
    case 0xE3270FE9: scr_ = Screen::NewCity; return;
    case 0xA10438E9: scr_ = Screen::MainMenu; return;
    default: break;
  }
  setTool(ToolKind::Unavailable); toolName_ = name; message("Not available: " + name);
}

// ---------------------------------------------------------------- frame

static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void Game::frame(const Input& in, int w, int h, uint32_t dtMs) {
  W_ = w; H_ = h; frameDt_ = dtMs;
  if (audio_) audio_->update(dtMs);
  if (screen_.size() != size_t(w) * h) screen_.assign(size_t(w) * h, 0xFF302820u);
  bool world3d = (scr_ == Screen::InCity || scr_ == Screen::TerrainEdit) && world_.W;
  if (world3d) {
    if (renderer_.width() != w || renderer_.height() != h) renderer_.resize(w, h);
    if (scr_ == Screen::InCity) {
      tickSim(dtMs);
      streetSimFrame(dtMs);
      ropt_.overlay = (showMap_ && mapWin_.showInCityView) ? mapWin_.activeOverlay() : nullptr;
    } else ropt_.overlay = nullptr;
    costShow_ = false;
    renderer_.setCostTextHook([this](int sx, int sy, const std::string& text, bool ok) { costText_ = text; costX_ = sx; costY_ = sy; costOk_ = ok; costShow_ = true; });
    ropt_.animTick = int(sim_.day);
    renderer_.render(cam_, ropt_, &scene_);
    screen_ = renderer_.pixels();
  } else if (scr_ != Screen::Intro) {
    std::fill(screen_.begin(), screen_.end(), 0xFF3A2C20u);
  }
  ui_.begin(&screen_, w, h, in);
  uiHit_ = false;
  switch (scr_) {
    case Screen::NeedData: drawNeedData(); break;
    case Screen::Intro: drawIntro(dtMs); break;
    case Screen::MainMenu: drawMainMenu(); break;
    case Screen::NewCity: drawNewCity(); break;
    case Screen::TerrainEdit: drawTerrainEdit(); break;
    case Screen::LoadList: drawLoadList(); break;
    case Screen::Credits: drawCredits(dtMs); break;
    case Screen::InCity: drawInCity(dtMs); break;
    case Screen::ScenarioEnd: drawScenarioEnd(); break;
  }
  drawMessage();
  if (!ui_.tooltip.empty()) {
    int tw = ui_.textWidth(ui_.tooltip);
    Rect r{std::min(in.mx + 12, w - tw - 8), in.my + 18, tw + 8, 18};
    ui_.fill(r, rgb(255, 255, 200)); ui_.frame(r, true);
    ui_.text(r.x + 4, r.y + 2, ui_.tooltip);
  }
}

void Game::drawMessage() {
  if (msgText_.empty()) return;
  int tw = std::min(W_ - 40, std::max(300, ui_.textWidth(msgText_) + 40));
  Rect r{(W_ - tw) / 2, H_ / 2 - 50, tw, 100};
  ui_.panel(r, "Metroplex3K");
  ui_.textCentered({r.x, r.y + 30, r.w, 24}, msgText_);
  if (ui_.button({r.x + r.w / 2 - 40, r.y + 66, 80, 24}, "OK") || ui_.in.key[K_ENTER] || ui_.in.key[K_ESC]) msgText_.clear();
  uiHit_ = true;
  ui_.in.pressed = ui_.in.released = false;
}

// ---------------------------------------------------------------- tool menu & HUD

void Game::drawToolMenu() {
  if (!menuLoaded_) return;
  auto rootIt = menu_.menus.find(0xD1001000);
  if (rootIt == menu_.menus.end()) return;
  auto titleOf = [&](const MenuDesc& d) { std::string s = str(d.titleTable, d.titleId); return s.empty() ? d.fallbackTitle : s; };
  auto itemName = [&](const MenuItemInfo& m) { std::string s = str(m.nameTable, m.nameId); return s.empty() ? m.fallbackName : s; };
  int bh = 28, x = 4, y = 4;
  Rect panelR{0, 0, 118, int(rootIt->second.buttons.size()) * (bh + 2) + 8};
  ui_.panel(panelR);
  if (ui_.hover(panelR)) uiHit_ = true;
  for (uint32_t bid : rootIt->second.buttons) {
    const MenuButton& b = menu_.buttons[bid];
    auto sub = menu_.menus.find(b.sub);
    std::string label = sub != menu_.menus.end() ? titleOf(sub->second) : "?";
    if (label.empty()) label = "Menu";
    if (ui_.button({x, y, 110, bh}, label, true, openRoot_ == b.sub)) { openRoot_ = (openRoot_ == b.sub) ? 0 : b.sub; openFly_ = 0; }
    y += bh + 2;
  }
  if (!openRoot_) return;
  auto sm = menu_.menus.find(openRoot_);
  if (sm == menu_.menus.end()) return;
  int sx = panelR.w + 2, rows = int(sm->second.buttons.size());
  Rect sr{sx, 0, 190, rows * 28 + 8};
  ui_.panel(sr);
  if (ui_.hover(sr)) uiHit_ = true;
  int yy = 4;
  uint32_t flyOpen = 0; int flyY = 0;
  for (uint32_t bid : sm->second.buttons) {
    const MenuButton& b = menu_.buttons[bid];
    auto ii = menu_.items.find(b.item);
    std::string label = ii != menu_.items.end() ? itemName(ii->second) : std::string();
    if (label.empty() && b.sub) { auto fm = menu_.menus.find(b.sub); if (fm != menu_.menus.end()) label = titleOf(fm->second); }
    if (label.empty()) label = "(item)";
    if (b.sub) label += "  >";
    Rect br{sx + 4, yy, 182, 26};
    bool enabled = ii == menu_.items.end() || ii->second.enabled;
    if (ui_.button(br, label, enabled, openFly_ == b.sub && b.sub)) {
      if (b.sub) openFly_ = (openFly_ == b.sub) ? 0 : b.sub;
      else { execute(b.item); if (tool_ != ToolKind::Unavailable) openRoot_ = 0; }
    }
    if (b.sub && openFly_ == b.sub) { flyOpen = b.sub; flyY = yy; }
    yy += 28;
  }
  if (flyOpen) {
    auto fm = menu_.menus.find(flyOpen);
    if (fm != menu_.menus.end()) {
      Rect fr{sr.x + sr.w + 2, flyY, 190, int(fm->second.buttons.size()) * 28 + 8};
      ui_.panel(fr);
      if (ui_.hover(fr)) uiHit_ = true;
      if (fm->second.colour) ui_.fill({fr.x + 2, fr.y + 2, 4, fr.h - 4}, 0xFF000000u | ((fm->second.colour & 255) << 16) | (fm->second.colour & 0xFF00) | ((fm->second.colour >> 16) & 255));
      int fy = fr.y + 4;
      for (uint32_t bid : fm->second.buttons) {
        const MenuButton& b = menu_.buttons[bid];
        auto ii = menu_.items.find(b.item);
        std::string label = ii != menu_.items.end() ? itemName(ii->second) : "(item)";
        bool enabled = ii == menu_.items.end() || ii->second.enabled;
        // Start-disaster items are disabled when disasters are off for the city.
        if (flyOpen == 0xD100A100 && !disastersOn_) enabled = false;
        if (ui_.button({fr.x + 10, fy, 176, 26}, label, enabled)) { execute(b.item); if (tool_ != ToolKind::Unavailable) { openFly_ = 0; openRoot_ = 0; } }
        fy += 28;
      }
    }
  }
}

void Game::drawHud() {
  int bh = 30;
  Rect bar{0, H_ - bh, W_, bh};
  ui_.panel(bar);
  if (ui_.hover(bar)) uiHit_ = true;
  int month = std::min(11, (world_.info.dayOfYear - 1) * 12 / 365);
  std::string date = std::string(kMonths[month]) + " " + std::to_string(world_.info.year);
  ui_.text(8, H_ - bh + 8, world_.info.cityName);
  ui_.text(180, H_ - bh + 8, date);
  ui_.text(280, H_ - bh + 8, "$" + groupDigits(sim_.funds), "main10", sim_.funds < 0 ? rgb(255, 0, 0) : 0);
  ui_.text(400, H_ - bh + 8, "Pop " + groupDigits(sim_.population()));
  int bx = 520;
  if (ui_.button({bx, H_ - bh + 3, 44, 24}, paused_ ? "Run" : "Pause")) { paused_ = !paused_; sound(paused_ ? 24 : 23); }
  for (int i = 1; i <= 3; i++)
    if (ui_.button({bx + 48 + (i - 1) * 26, H_ - bh + 3, 24, 24}, std::to_string(i), true, !paused_ && speed_ == i)) { speed_ = i; paused_ = false; sound(24 + i); }
  int vx = bx + 140;
  if (ui_.button({vx, H_ - bh + 3, 26, 24}, "+")) { if (cam_.zoom < 4) sound(1); cam_.zoom = std::min(4, cam_.zoom + 1); }
  if (ui_.button({vx + 28, H_ - bh + 3, 26, 24}, "-")) { if (cam_.zoom > 0) sound(2); cam_.zoom = std::max(0, cam_.zoom - 1); }
  auto rot = [&](int d) {
    float Wf = float(world_.W), Hf = float(world_.H), x, z;
    switch (cam_.rot & 3) { case 0: x = cam_.u; z = cam_.v; break; case 1: x = cam_.v; z = Wf - cam_.u; break; case 2: x = Wf - cam_.u; z = Hf - cam_.v; break; default: x = Hf - cam_.v; z = cam_.u; }
    cam_.rot = (cam_.rot + d + 4) & 3; renderer_.centreOnWorld(cam_, x, z); sound(4);
  };
  if (ui_.button({vx + 56, H_ - bh + 3, 26, 24}, "<")) rot(-1);
  if (ui_.button({vx + 84, H_ - bh + 3, 26, 24}, ">")) rot(1);
  if (ui_.button({vx + 112, H_ - bh + 3, 40, 24}, "Grid", true, ropt_.grid)) ropt_.grid = !ropt_.grid;
  if (ui_.button({vx + 156, H_ - bh + 3, 50, 24}, "Query", true, tool_ == ToolKind::Query)) setTool(ToolKind::Query);
  if (ui_.button({vx + 210, H_ - bh + 3, 40, 24}, "Map")) openWindow("map");
  // RCI bars.
  int rx = W_ - 110;
  const Valve* v[3] = {&sim_.valveR, &sim_.valveC, &sim_.valveI};
  uint32_t cols[3] = {rgb(0, 192, 0), rgb(0, 0, 192), rgb(192, 192, 0)};
  const char* nm[3] = {"R", "C", "I"};
  for (int i = 0; i < 3; i++) {
    int cx = rx + i * 34;
    int hgt = std::max(-12, std::min(12, v[i]->value * 12 / 2000));
    ui_.fill({cx, H_ - bh + 15, 14, 1}, ui_.panelDark);
    if (hgt >= 0) ui_.fill({cx, H_ - bh + 15 - hgt, 14, hgt}, cols[i]); else ui_.fill({cx, H_ - bh + 16, 14, -hgt}, cols[i]);
    ui_.text(cx + 16, H_ - bh + 8, nm[i]);
  }
  // Ticker strip above the bar.
  drawTicker({122, H_ - bh - 24, W_ - 126, 22});
  if (tool_ != ToolKind::None)
    ui_.text(126, H_ - bh - 42, "Tool: " + toolName_ + "   (right click or Esc to cancel)", "main10", rgb(255, 255, 255));
  if (!statusLine_.empty()) ui_.text(126, H_ - bh - 58, statusLine_, "main10", rgb(255, 255, 200));
  if (costShow_ && !costText_.empty()) {
    int tw = ui_.textWidth(costText_);
    Rect cr{std::min(costX_, W_ - tw - 10), std::max(0, costY_ - 16), tw + 8, 18};
    ui_.fillAlpha(cr, rgb(0, 0, 0), 0.7f);
    ui_.text(cr.x + 4, cr.y + 2, costText_, "main10", costOk_ ? rgb(255, 255, 255) : rgb(255, 90, 90));
  }
}

// ---------------------------------------------------------------- map interaction

std::vector<std::pair<int, int>> Game::routePath(int x0, int z0, int x1, int z1) {
  // Straight / one bend: the longer leg first.
  std::vector<std::pair<int, int>> path;
  int x = x0, z = z0;
  bool xFirst = std::abs(x1 - x0) >= std::abs(z1 - z0);
  auto legX = [&] { while (x != x1) { path.push_back({x, z}); x += x1 > x ? 1 : -1; } };
  auto legZ = [&] { while (z != z1) { path.push_back({x, z}); z += z1 > z ? 1 : -1; } };
  if (xFirst) { legX(); legZ(); } else { legZ(); legX(); }
  path.push_back({x1, z1});
  return path;
}

City::Quote Game::quoteTool(int x0, int z0, int x1, int z1) {
  City& c = sim_.city();
  switch (tool_) {
    case ToolKind::Zone: return c.quoteZone(x0, z0, x1, z1, toolArg_);
    case ToolKind::Demolish: return c.quoteBulldoze(x0, z0, x1, z1);
    case ToolKind::Road: return c.quoteNetwork(NetworkKind(toolArg_ ? toolArg_ : 1), routePath(x0, z0, x1, z1));
    case ToolKind::Plop: return c.quotePlop(toolOcc_, x1, z1);
    case ToolKind::Terrain: {
      City::Quote q; q.valid = true;
      if (!terra_) return q;
      TerraPlan p = toolArg_ == 1 ? terra_->planRaise(x1, z1) : toolArg_ == 2 ? terra_->planLower(x1, z1) : toolArg_ == 32 ? terra_->planWater(x1, z1)
                    : toolArg_ == 4 ? terra_->planLevel(x0, z0, x1, z1) : TerraPlan();
      if (toolArg_ == 100) return q;
      q.valid = p.legal; q.cost = terra_->totalCost(p);
      return q;
    }
    default: return City::Quote();
  }
}

void Game::buildScene() {
  scene_.previews.clear();
  scene_.cost.show = false;
  if (!hoverValid_ || uiHit_) return;
  if (tool_ == ToolKind::None || tool_ == ToolKind::Unavailable) return;
  int x0 = dragging_ ? ax_ : hoverX_, z0 = dragging_ ? az_ : hoverZ_, x1 = hoverX_, z1 = hoverZ_;
  TilePreview pv;
  City::Quote q;
  bool showCost = true;
  switch (tool_) {
    case ToolKind::Zone: case ToolKind::Demolish:
      pv.x0 = x0; pv.z0 = z0; pv.x1 = x1; pv.z1 = z1;
      if (!dragging_) { pv.x0 = pv.x1 = x1; pv.z0 = pv.z1 = z1; }
      q = quoteTool(pv.x0, pv.z0, pv.x1, pv.z1);
      scene_.previews.push_back(pv);
      break;
    case ToolKind::Plop: {
      int sx = 1, sz = 1; sim_.city().footprint(toolOcc_, sx, sz);
      pv.x0 = x1; pv.z0 = z1; pv.x1 = x1 + sx - 1; pv.z1 = z1 + sz - 1;
      q = quoteTool(x1, z1, x1, z1);
      scene_.previews.push_back(pv);
      break;
    }
    case ToolKind::Road: {
      if (dragging_) {
        for (const auto& p : routePath(x0, z0, x1, z1)) { TilePreview t; t.x0 = t.x1 = p.first; t.z0 = t.z1 = p.second; scene_.previews.push_back(t); }
        q = quoteTool(x0, z0, x1, z1);
      } else { pv.x0 = pv.x1 = x1; pv.z0 = pv.z1 = z1; scene_.previews.push_back(pv); q.valid = true; showCost = false; }
      break;
    }
    case ToolKind::Terrain:
      pv.x0 = x0; pv.z0 = z0; pv.x1 = x1; pv.z1 = z1;
      if (toolArg_ != 4 || !dragging_) { pv.x0 = pv.x1 = x1; pv.z0 = pv.z1 = z1; }
      q = quoteTool(x0, z0, x1, z1);
      showCost = scr_ == Screen::InCity && toolArg_ != 100;
      scene_.previews.push_back(pv);
      break;
    case ToolKind::Dispatch: case ToolKind::Query:
      pv.x0 = pv.x1 = x1; pv.z0 = pv.z1 = z1; pv.customColour = true; pv.r = 240; pv.g = 240; pv.b = 80; pv.alpha = 90;
      scene_.previews.push_back(pv); showCost = false; q.valid = true;
      break;
    default: return;
  }
  if (!q.valid) for (auto& p : scene_.previews) { p.valid = false; p.customColour = false; }
  bool afford = !(q.cost != 0 && q.cost > fundsRef());
  if (!afford) for (auto& p : scene_.previews) { p.valid = false; p.customColour = false; }
  if (showCost && scr_ == Screen::InCity) {
    scene_.cost.show = true; scene_.cost.tileX = x1; scene_.cost.tileZ = z1; scene_.cost.cost = long(q.cost); scene_.cost.affordable = afford;
  }
}

void Game::executeTool(int x0, int z0, int x1, int z1) {
  switch (tool_) {
    case ToolKind::Terrain:
      if (toolArg_ == 4 && terra_) {
        TerraPlan p = terra_->planLevel(x0, z0, x1, z1);
        auto r = terra_->apply(p, fundsRef()); renderer_.invalidateShade();
        sound(r.status == ApplyStatus::Applied ? 15 : (r.status == ApplyStatus::NoMoney ? 32 : 30));
      }
      break;
    case ToolKind::Zone: sim_.paintZoneType(x0, z0, x1, z1, toolArg_); dirty_ = true; break;
    case ToolKind::Road: {
      City::Quote q = quoteTool(x0, z0, x1, z1);
      if (!q.valid) { sound(30); break; }
      if (q.cost != 0 && q.cost > sim_.funds) { sound(32); break; }
      sim_.buildNetwork(NetworkKind(toolArg_ ? toolArg_ : 1), routePath(x0, z0, x1, z1)); dirty_ = true;
      break;
    }
    case ToolKind::Demolish: sim_.bulldoze(x0, z0, x1, z1); dirty_ = true; break;
    case ToolKind::Plop: {
      City::Quote q = quoteTool(x1, z1, x1, z1);
      if (!q.valid) { sound(30); message("Cannot build here."); break; }
      if (q.cost != 0 && q.cost > sim_.funds) { sound(32); message("Insufficient funds."); break; }
      if (sim_.plop(toolOcc_, x1, z1, int(q.cost))) { dirty_ = true; }
      else message(sim_.city().lastFailNoMoney ? "Insufficient funds." : "Cannot build here.");
      break;
    }
    case ToolKind::Dispatch: dispatchUnit(toolArg_, x1, z1); break;
    case ToolKind::Query: {
      buildQueryInfo(x1, z1, queryInfo_);
      Rect r{std::min(ui_.in.mx + 10, std::max(0, W_ - 330)), std::min(ui_.in.my + 10, std::max(0, H_ - 280)), 0, 0};
      queryWin_.open(r);
      showQuery_ = true;
      break;
    }
    default: break;
  }
}

void Game::updateHover(int tx, int tz, bool onTile) {
  hoverValid_ = onTile; hoverX_ = tx; hoverZ_ = tz;
  if (onTile && tool_ != ToolKind::None && tool_ != ToolKind::Unavailable) {
    char b[96];
    if (dragging_ && (tool_ == ToolKind::Zone || tool_ == ToolKind::Demolish))
      std::snprintf(b, sizeof b, "%d x %d tiles", std::abs(tx - ax_) + 1, std::abs(tz - az_) + 1);
    else std::snprintf(b, sizeof b, "(%d, %d)", tx, tz);
    statusLine_ = b;
  } else statusLine_.clear();
}

void Game::handleMap() {
  const Input& in = ui_.in;
  if (!world_.W) return;
  if (in.wheel && !uiHit_) cam_.zoom = std::max(0, std::min(4, cam_.zoom + (in.wheel > 0 ? 1 : -1)));
  int tx = 0, tz = 0;
  bool onTile = !uiHit_ && renderer_.pickGround(cam_, in.mx, in.my, tx, tz);
  // Right button: cancel tool, else pan by dragging.
  if (in.rightPressed && !uiHit_) {
    if (dragging_) dragging_ = false;
    else if (tool_ != ToolKind::None) setTool(ToolKind::None);
  }
  updateHover(tx, tz, onTile);
  if (onTile && in.pressed) {
    if (tool_ == ToolKind::None) setTool(ToolKind::Query);
    if (tool_ != ToolKind::Unavailable) { dragging_ = true; ax_ = tx; az_ = tz; }
  }
  // Terrain tools: press-and-hold repeats raise/lower/water/trees; level applies on release.
  if (onTile && dragging_ && in.down && tool_ == ToolKind::Terrain && toolArg_ != 4 && terra_) {
    static uint32_t tick = 0;
    if ((++tick & 3) == 0 || in.pressed) {
      if (toolArg_ == 100) {
        FloraRng rng = floraRngFromLcg(floraLcg_);
        uint8_t cell = randomTreeCell(3, rng);
        TreeTool tt; TreePlan p = planTree(world_, tx, tz, cell, tt);
        if (p.legal && fundsRef() >= p.cost) { commitTree(world_, p); fundsRef() -= p.cost; }
      } else {
        TerraPlan p = toolArg_ == 1 ? terra_->planRaise(tx, tz) : toolArg_ == 2 ? terra_->planLower(tx, tz) : terra_->planWater(tx, tz);
        auto r = terra_->apply(p, fundsRef());
        if (r.status != ApplyStatus::Applied && in.pressed) sound(r.status == ApplyStatus::NoMoney ? 32 : 30);
        renderer_.invalidateShade();
        if (scr_ == Screen::InCity) { sim_.city().markOccupantsDirty(); dirty_ = true; }
      }
    }
  }
  if (dragging_ && in.released) {
    dragging_ = false;
    if (onTile) executeTool(ax_, az_, tx, tz);
    if (tool_ == ToolKind::Query && toolName_.empty()) tool_ = ToolKind::None;
  }
  buildScene();
}

// ---------------------------------------------------------------- in-city

void Game::drawInCity(uint32_t dtMs) {
  const Input& in = ui_.in;
  bool typing = ui_.focusId != 0 || cheatOpen_;
  if (!typing) {
    float s = 32.f;
    float dx = (in.held[1] ? s : 0) - (in.held[0] ? s : 0), dy = (in.held[3] ? s : 0) - (in.held[2] ? s : 0);
    if (dx || dy) {
      float Wt = 8.f * float(1 << cam_.zoom), Ht = Wt / 2;
      float du = (dy / (Ht / 2) - dx / (Wt / 2)) / 2, dv = (dy / (Ht / 2) + dx / (Wt / 2)) / 2;
      cam_.u = std::max(0.f, std::min(float(world_.W), cam_.u + du)); cam_.v = std::max(0.f, std::min(float(world_.H), cam_.v + dv));
    }
    for (char c : in.text) {
      if (c == ' ') { paused_ = !paused_; sound(paused_ ? 24 : 23); }
      else if (c == 'g') ropt_.grid = !ropt_.grid;
      else if (c == '+' || c == '=') cam_.zoom = std::min(4, cam_.zoom + 1);
      else if (c == '-') cam_.zoom = std::max(0, cam_.zoom - 1);
      else if (c >= 'A' && c <= 'Z') {
        // Ctrl+key equivalents are host dependent; capital letters open windows.
        if (c == 'B') runTarget(0x025DDC4A);
        else if (c == 'O') runTarget(0x724A82D7);
        else if (c == 'M') runTarget(0x411164F1);
        else if (c == 'N') runTarget(0x630288E9);
        else if (c == 'P') runTarget(0x724A82D0);
      } else { auto it = hotkeys_.find(c); if (it != hotkeys_.end()) runTarget(it->second); }
    }
    if (in.key[K_TAB]) cheatOpen_ = true;
    if (in.key[K_ESC]) {
      if (showQuery_) showQuery_ = false;
      else if (tool_ != ToolKind::None) setTool(ToolKind::None);
      else if (openRoot_) openRoot_ = 0;
      else if (!anyBlockingWindow()) { if (dirty_ && false) {} scr_ = Screen::MainMenu; }
    }
  }
  drawToolMenu();
  drawHud();
  drawWindows(dtMs);
  drawCheatBox();
  if (!anyBlockingWindow() && msgText_.empty()) handleMap();
  else { hoverValid_ = false; buildScene(); }
}

void Game::drawCheatBox() {
  if (!cheatOpen_) return;
  Rect r{W_ / 2 - 200, H_ / 2 - 40, 400, 70};
  ui_.panel(r, "Cheat");
  ui_.textBox({r.x + 10, r.y + 34, r.w - 20, 24}, cheatBuf_, 60);
  if (ui_.focusId == 0) ui_.focusId = 1;
  if (ui_.in.key[K_ENTER]) { typeCheat(cheatBuf_); cheatBuf_.clear(); cheatOpen_ = false; ui_.focusId = 0; }
  else if (ui_.in.key[K_ESC]) { cheatBuf_.clear(); cheatOpen_ = false; ui_.focusId = 0; }
  uiHit_ = true;
  ui_.in.pressed = ui_.in.released = false;
}

}  // namespace m3k
