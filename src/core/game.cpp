// Game shell core: menu tree, tool menu, tool dispatch, HUD, map interaction, frame loop.
// See game.h for the split over game_front.cpp / game_windows.cpp / game_sim.cpp.
#include "core/game.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/game_scenario.h"
#include "core/gamedir.h"
#include "core/terrain_gen.h"

namespace m3k {

// Sentinel toolArg_ for the highway on-ramp point tool (menu 02B5DEBB); not a NetworkKind.
constexpr int kToolOnRamp = 7;

void Game::setPaused(bool p) {
  if (p == paused_) return;
  paused_ = p;
  // Spec 14 §2.3 / Q-AZ: city pause count = ordinary player pause (bottom-bar / hotkey).
  // Resume and selecting a non-zero speed clear it. Modals use raisePending instead.
  if (!simReady_) return;
  if (p) {
    // Ticker (0x2F9) is the pause announcement — no message-box dialog (observed: original has none).
    sim_.city().raisePause(false);
    // Spec 22 4.7.2: 0x2F9 is the pause ticker item (class 1, repeats until withdrawn).
    if (haveData_ && !pauseNewsItem_) pauseNewsItem_ = postNews(kTblNewsTriggered, 0x2F9);
  } else {
    sim_.city().resumePause();
    // Stop feeding new pause copies; items already on the strip finish scrolling off.
    if (pauseNewsItem_) { news_.withdraw(pauseNewsItem_); pauseNewsItem_ = 0; }
  }
}

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
Game::~Game() {
  if (asyncSim_ && simJobInFlight_) simWorker_.wait();   // the job uses members that die with us
}

void Game::loadHotkeys() {
  hotkeyTable_ = HotkeyTable();
  if (haveData_) {
    fs::path dir = resolveCI(data_.appsDir(), "Res/Hotkeys");
    fs::path f = dir.empty() ? fs::path() : resolveCI(dir, data_.language + ".IXF");
    IxfFile ix;
    Bytes b;
    if (!f.empty() && ix.open(f) && ix.getBytes({0xA2E3D533, 0xA2E3D56D, 1}, b) && !b.empty())
      hotkeyTable_.parse(std::string(b.begin(), b.end()));
  }
  if (hotkeyTable_.size() == 0) hotkeyTable_.loadDefaults();
}

void Game::hotkeyCommand(int cmd, int, int arg2) {
  (void)arg2;
  uint32_t param = 0;
  if (uint32_t cls = hotkeyToolClass(cmd, &param)) {
    // hotkeys follow the menu's transport tech flags (spec 22 s3.10, Q-AK)
    for (const auto& kv : menu_.items)
      if (kv.second.target == cls && kv.second.arg1 == param && !transportToolEnabled(kv.first)) return;
    runTarget(cls, param);
    return;
  }
  auto scroll = [&](float dx, float dy) {
    float Wt = 8.f * float(1 << cam_.zoom), Ht = Wt / 2;
    float du = (dy / (Ht / 2) - dx / (Wt / 2)) / 2, dv = (dy / (Ht / 2) + dx / (Wt / 2)) / 2;
    cam_.u = std::max(0.f, std::min(float(world_.W), cam_.u + du)); cam_.v = std::max(0.f, std::min(float(world_.H), cam_.v + dv));
  };
  auto rotate = [&](int d) {
    float Wf = float(world_.W), Hf = float(world_.H), x, z;
    switch (cam_.rot & 3) { case 0: x = cam_.u; z = cam_.v; break; case 1: x = cam_.v; z = Wf - cam_.u; break; case 2: x = Wf - cam_.u; z = Hf - cam_.v; break; default: x = Hf - cam_.v; z = cam_.u; }
    cam_.rot = (cam_.rot + d + 4) & 3; renderer_.centreOnWorld(cam_, x, z);
  };
  switch (cmd) {
    case 19: openWindow("advisors"); break;
    case 21: openWindow("ordinances"); break;
    case 33: case 91: case 102: requestQuit(); break;   // F10 / Ctrl+Q / Alt+F4: same prompts as the Exit menu item
    case 35: setPaused(!paused_); sound(paused_ ? 24 : 23); break;
    case 36: speed_ = std::max(1, speed_ - 1); break;
    case 37: speed_ = std::min(4, speed_ + 1); break;
    case 40: cheatOpen_ = true; break;
    case 41: cam_.zoom = std::min(4, cam_.zoom + 1); sound(1); break;
    case 42: cam_.zoom = std::max(0, cam_.zoom - 1); sound(2); break;
    // PageDown turns the map clockwise as seen on screen: the bottom corner goes to the left corner (spec 08 5.4 [OBSERVED]).
    // With this renderer's view transform that is the lower rotation index.
    case 43: rotate(-1); sound(4); break;
    case 44: rotate(1); sound(4); break;
    case 45: scroll(-32, 0); break;
    case 46: scroll(0, -32); break;
    case 47: scroll(32, 0); break;
    case 48: scroll(0, 32); break;
    case 49: panelsHidden_ = !panelsHidden_; break;   // F12: hide / show the toolbars
    case 50: if (hoverValid_) centreOn(hoverX_, hoverZ_); break;
    case 63: ropt_.grid = !ropt_.grid; break;
    case 75: case 76: cam_.zoom = 0; break;
    case 77: case 78: cam_.zoom = 1; break;
    case 79: case 80: cam_.zoom = 2; break;
    case 81: case 82: cam_.zoom = 3; break;
    case 83: case 84: cam_.zoom = 4; break;
    case 85: setTool(ToolKind::None); break;
    case 94: if (showLayers_) showLayers_ = false; else openWindow("layers"); sound(12); break;   // Ctrl+G: the navigator's layers button
    case 113: takeSnapshot(snapSizeIdx_); break;
    case 114: takeSnapshot(std::max(1, std::min(3, arg2 ? arg2 : snapSizeIdx_))); break;
    default: break;   // 74, 96-100, 104, 108, 112, 115 and unknown commands: no effect
  }
}

void Game::handleKeys(const Input& in) {
  // Restricted table (instance 2): terrain editor / pre-city stage allows only demolish, view keys, landscaping and Esc.
  auto restrictedOk = [](int c) {
    return c == 18 || (c >= 41 && c <= 48) || c == 50 || c == 67 || c == 70 || c == 71 || (c >= 75 && c <= 85);
  };
  for (const KeyEvent& k : in.keyDowns) {
    const HotkeyBinding* b = hotkeyTable_.find(k.vk, k.mods);
    if (!b) continue;
    if (scr_ == Screen::TerrainEdit && !restrictedOk(b->cmd)) continue;
    hotkeyCommand(b->cmd, b->arg1, b->arg2);
  }
}

bool Game::init(const fs::path& gameRoot, std::string& err) {
  ui_.fonts = &fonts_;
  loadSettings();
  if (gameRoot.empty()) { scr_ = Screen::NeedData; return true; }
  GameData g;
  g.languageRequest = !languageRequest_.empty() ? languageRequest_ : (std::getenv("M3K_LANGUAGE") ? std::getenv("M3K_LANGUAGE") : "");
  if (!g.open(gameRoot, err)) { scr_ = Screen::NeedData; return false; }
  data_ = std::move(g);
  haveData_ = true; root_ = data_.root();
  if (persist_) saveGameRoot(root_);
  ensureFonts();
  cityCfg_ = CityConfig();
  cityCfg_.load(data_);
  initAudio();
  loadSchemes();
  loadHotkeys();
  IniFile mi;
  if (data_.ini().load("MenuItem.INI", mi)) {
    MenuTree t;
    if (t.parse(mi) && t.menus.count(0xD1001000)) { menu_ = std::move(t); menuLoaded_ = true; }
  }
  {
    char b[512];
    size_t fontsOk = 0;
    for (const char* n : {"system9", "main9", "main10", "title9", "title12", "serif17"}) if (fonts_.has(n)) fontsOk++;
    Skin probe; bool skinOk = probe.load(data_, 800, 1280);
    std::snprintf(b, sizeof b, "data: %zu segments, %zu strings, %zu images, %zu/6 fonts, menu %s, skin %s (%zu art)",
                  data_.db().segmentCount(), data_.db().keysOfType(T_STRING).size(), data_.db().keysOfType(T_UI_IMAGE).size(), fontsOk,
                  menuLoaded_ && menu_.items.size() > 20 ? "ok" : "fallback", skinOk ? "ok" : "none", probe.imageCount());
    dataSummary_ = b;
    std::fprintf(stderr, "metroplex3k: %s (root %s)\n", b, root_.string().c_str());
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
  if (n == "engine") return showEngine_;
  if (n == "props") return showProps_;
  if (n == "goals") return showGoals_;
  if (n == "map") return showMap_;
  if (n == "layers") return showLayers_;
  if (n == "graphs") return showGraphs_;
  if (n == "charts") return showCharts_;
  if (n == "picker") return showPicker_;
  if (n == "query") return showQuery_;
  return false;
}

void Game::initAudio() {
  // The host callback reads audio_; swap it only under the host lock (the mixer guards everything else).
  auto swapAudio = [this](std::unique_ptr<AudioSystem> a) {
    if (audioLock) audioLock();
    audio_.swap(a);
    if (audioUnlock) audioUnlock();
  };   // the old system (now in `a`) is destroyed here, after the unlock
  swapAudio(nullptr); ambCfg_ = false; ambSel_.reset(-1);
  if (!haveData_) return;
  fs::path dir = resolveCI(data_.appsDir(), "Res/Sound");
  if (dir.empty()) return;
  fs::path ini = resolveCI(dir, "AUDIO.INI");
  Bytes b;
  if (ini.empty() || !readFile(ini, b)) return;
  auto a = std::make_unique<AudioSystem>();
  a->init(AudioIni::parse(std::string(b.begin(), b.end())), makeDirLoader(dir));
  a->setAsyncMusic(true);
  swapAudio(std::move(a));
  applySettingsToAudio();
}

// ---------------------------------------------------------------- tools & menu

void Game::setTool(ToolKind k, int a, uint32_t occ) {
  tool_ = k; toolArg_ = a; toolOcc_ = occ; dragging_ = false;
  hoverValid_ = false; scene_.previews.clear(); scene_.tints.clear(); scene_.cost.show = false; costShow_ = false;
  if (k == ToolKind::None) toolName_.clear();
  syncViewForTool(k, a, occ);
}

void Game::syncViewForTool(ToolKind k, int a, uint32_t occ) {
  // Spec 09 §7.2 / 25 §9.3 [INFERRED]: pipes and subway pieces only draw in the underground view
  // (render.cpp). Selecting those tools must enter that view; surface build tools leave it.
  auto goUnder = [&](int net) {   // 0 = subways, 1 = water pipes (CityLayers::underground)
    layers_.mode = CityLayers::Underground;
    layers_.underground = net;
    layersDlg_.edit = layers_;
    applyLayers();
  };
  auto leaveUnder = [&] {
    if (layers_.mode != CityLayers::Underground) return;
    layers_.mode = CityLayers::AboveGround;
    layersDlg_.edit = layers_;
    applyLayers();
  };
  if (k == ToolKind::Road) {
    if (a == int(NetworkKind::Pipe)) goUnder(1);
    else if (a == int(NetworkKind::Subway)) goUnder(0);
    else leaveUnder();   // road, rail, power, highway, on-ramp
    return;
  }
  if (k == ToolKind::Plop) {
    // Subway station (0xE9) and subway-to-rail (0x3E8A) are placed on the subway layer.
    if (occ == 0xE9 || occ == 0x3E8A) goUnder(0);
    else leaveUnder();
    return;
  }
  if (k == ToolKind::Zone || k == ToolKind::Terrain) leaveUnder();
}

void Game::runTarget(uint32_t target, uint32_t arg1, const std::string& name) {
  MenuItemInfo mi; mi.kind = "COMM"; mi.target = target; mi.arg1 = arg1; mi.fallbackName = name;
  dispatch(mi);
}

bool Game::transportToolEnabled(uint32_t id) const {
  int flag = -1;
  switch (id) {
    case 0x10004201: case 0x10004202: case 0x10004009: flag = 0; break;   // subway, subway station, subway-rail station
    case 0x10004004: flag = 1; break;                                     // bus stop
    case 0x10003006: flag = 2; break;                                     // airport zone
    case 0x10004301: case 0x10004302: flag = 3; break;                    // highway, on-ramp
    default: return true;
  }
  return !managersReady_ || advisors_.transportTech(flag);
}

void Game::execute(uint32_t menuItemId) {
  auto it = menu_.items.find(menuItemId);
  if (it != menu_.items.end() && it->second.enabled && transportToolEnabled(menuItemId)) dispatch(it->second);
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
    case 0x02B5DEBB: setTool(ToolKind::Road, kToolOnRamp); toolName_ = name; return;   // on-ramp (spec 12 §7)
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
      if (mi.arg1 == 0xC3172AEA || currentFile_.empty()) openSaveAs();
      else saveCurrent(currentFile_.stem().string(), currentFile_.parent_path());
      return;
    case 0x610438A4:
      requestLeave(kLeaveLoad + (mi.arg1 == 0x63172B74 ? 1 : mi.arg1 == 0x23172AF2 ? 2 : mi.arg1 == 0xA42C7725 ? 3 : 0));
      return;
    case 0xE3270FE9: requestLeave(kLeaveNew); return;
    case 0xA10438E9: requestLeave(kLeaveQuit); return;   // the original's Exit quits the program after its prompts
    default: break;
  }
  setTool(ToolKind::Unavailable); toolName_ = name; message("Not available: " + name);
}

// ---------------------------------------------------------------- frame

static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

uint64_t Game::cityViewSignature() const {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
  auto mixf = [&](float f) { uint32_t u = 0; std::memcpy(&u, &f, sizeof u); mix(u); };
  mixf(cam_.u); mixf(cam_.v);
  mix(uint64_t(cam_.zoom)); mix(uint64_t(cam_.rot));
  mix(uint64_t(W_)); mix(uint64_t(H_));
  mix(uint64_t(animTick_)); mix(uint64_t(ropt_.dateMD)); mix(cityVisualSerial_); mix(uint64_t(shake_));
  mix(ropt_.grid); mix(ropt_.objects); mix(ropt_.terrain); mix(ropt_.underground);
  mix(ropt_.zonePaint); mix(ropt_.background); mix(ropt_.edgeFaces); mix(ropt_.constructionSites);
  mix(ropt_.flora); mix(ropt_.pipes); mix(ropt_.subways); mix(ropt_.automata);
  mix(uint64_t(ropt_.ownImageFlags));
  mix(reinterpret_cast<uintptr_t>(ropt_.overlay));
  mix(uint64_t(layers_.mode)); mix(layers_.transport); mix(layers_.powerLines); mix(layers_.flora);
  mix(layers_.zonedBuildings); mix(layers_.otherBuildings); mix(layers_.zones);
  mix(uint64_t(layers_.underground)); mix(uint64_t(layers_.layer));
  const World& vw = viewWorld();   // async: the renderer's snapshot, not the world the worker is changing
  mix(uint64_t(vw.occupants.size())); mix(uint64_t(vw.surface.size()));
  mix(uint64_t(vw.pipes.size())); mix(uint64_t(vw.subway.size()));
  mix(uint64_t(scene_.sprites.size()));
  for (const auto& sp : scene_.sprites) {
    mix(sp.category); mix(sp.spriteId); mix(uint32_t(sp.x)); mix(uint32_t(sp.z));
    mix(uint32_t(sp.height)); mix(sp.magnify2x); mix(sp.moving);
  }
  mix(uint64_t(scene_.tints.size()));
  for (const auto& t : scene_.tints) {
    mix(uint64_t(t.x0)); mix(uint64_t(t.z0)); mix(uint64_t(t.x1)); mix(uint64_t(t.z1)); mix(t.r); mix(t.g); mix(t.b); mix(t.ground);
  }
  mix(uint64_t(scene_.previews.size()));
  for (const auto& p : scene_.previews) {
    mix(uint64_t(p.x0)); mix(uint64_t(p.z0)); mix(uint64_t(p.x1)); mix(uint64_t(p.z1));
    mix(p.valid); mix(p.customColour); mix(p.r); mix(p.g); mix(p.b); mix(p.alpha);
  }
  mix(scene_.cost.show); mix(uint64_t(scene_.cost.tileX)); mix(uint64_t(scene_.cost.tileZ));
  mix(uint64_t(scene_.cost.cost)); mix(scene_.cost.affordable);
  return h;
}

void Game::blitCityToDest() {
  if (!drawPx_ || !cityFbValid_ || cityFb_.size() != size_t(W_) * H_) return;
  const int rowBytes = W_ * int(sizeof(uint32_t));
  if (drawPitch_ == W_) {
    std::memcpy(drawPx_, cityFb_.data(), size_t(rowBytes) * size_t(H_));
  } else {
    for (int y = 0; y < H_; y++)
      std::memcpy(drawPx_ + size_t(y) * drawPitch_, cityFb_.data() + size_t(y) * W_, size_t(rowBytes));
  }
}

void Game::applyCostLabelFromScene() {
  costShow_ = false;
  if (!scene_.cost.show || !world_.inTiles(scene_.cost.tileX, scene_.cost.tileZ)) return;
  int alt = world_.altAt(scene_.cost.tileX, scene_.cost.tileZ);
  int sx = 0, sy = 0;
  renderer_.projectSub(cam_, scene_.cost.tileX * 256 + 128, scene_.cost.tileZ * 256 + 128, alt * 256, sx, sy);
  costX_ = sx - 2; costY_ = sy + 40;
  costText_ = "$" + groupDigits(scene_.cost.cost);
  costOk_ = scene_.cost.affordable;
  costShow_ = true;
}

void Game::frame(const Input& in, int w, int h, uint32_t dtMs, uint32_t* outPixels, int outPitchBytes) {
  using PhaseClock = std::chrono::steady_clock;
  auto phaseMs = [](PhaseClock::time_point a, PhaseClock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  phases_ = FramePhases();
  ++frameNo_;
  phases_.zoom = cam_.zoom; phases_.speed = speed_; phases_.paused = paused_;
  const auto pa0 = PhaseClock::now();
  asyncFrameBegin(in);
  phases_.simWaitMs += phaseMs(pa0, PhaseClock::now());
  W_ = w; H_ = h; frameDt_ = dtMs;
  // The original's view update is unthrottled; 60 Hz is the recommended fixed rate (spec 25 10, [OBSERVED] + [INFERRED]).
  constexpr double kViewUpdateHz = 60.0;
  viewAcc_ += double(dtMs) * kViewUpdateHz / 1000.0;
  viewUpdates_ = std::min(8, int(viewAcc_));
  viewAcc_ -= std::floor(viewAcc_);
  syncScreenPending();
  if (audio_) { audio_->setAudioZoom(cam_.zoom + 1); audio_->update(dtMs); }
  const bool ext = outPixels != nullptr;
  if (!ext) {
    if (screen_.size() != size_t(w) * h) screen_.assign(size_t(w) * h, 0xFF302820u);
    drawPx_ = screen_.data();
    drawPitch_ = w;
  } else {
    drawPx_ = outPixels;
    drawPitch_ = outPitchBytes > 0 ? outPitchBytes / int(sizeof(uint32_t)) : w;
  }
  // Dialogs opened from a city (Load / Save As) keep the city view and HUD behind them, frozen.
  const bool cityBackdrop = (scr_ == Screen::LoadList || scr_ == Screen::NewCity) && world_.W && simReady_;
  bool world3d = (scr_ == Screen::InCity || scr_ == Screen::TerrainEdit || cityBackdrop) && world_.W;
  const bool gpuTry = world3d && citySink_ && ext && scr_ == Screen::InCity;
  bool cityOnGpu = false;
  if (world3d) {
    if (renderer_.width() != w || renderer_.height() != h) { renderer_.resize(w, h); cityFbValid_ = false; cityGpuValid_ = false; }
    if (scr_ == Screen::InCity) {
      const auto pt0 = PhaseClock::now();
      tickSim(dtMs);
      phases_.simDay = simDayThisFrame_;
      if (simDayThisFrame_) bumpCityVisual();
      const auto pt1 = PhaseClock::now();
      streetSimFrame(dtMs);
      phases_.simMs = phaseMs(pt0, pt1);
      phases_.streetMs = phaseMs(pt1, PhaseClock::now());
      updateAmbience(dtMs);
      ropt_.overlay = (showMap_ && mapWin_.showInCityView) ? mapWin_.activeOverlay() : nullptr;
      if (!ropt_.overlay && layers_.mode == CityLayers::LayerView) {   // City Layer Views: kept current with the sim
        if (++layerOverlayAge_ >= 30 && !simBusy()) refreshLayerOverlay();
        ropt_.overlay = &layerOverlay_;
      }
      // Zones layer (spec 25 9.2): with the building layers hidden every lot is a flat palBasic colour; otherwise zoned terrain
      // carries the 50/50 palBasic tint of spec 25 6.4.
      if (!simBusy()) {
        const bool zonesOn = layers_.mode == CityLayers::AboveGround && layers_.zones && !ropt_.overlay;
        if (zonesOn && !layers_.zonedBuildings && !layers_.otherBuildings) { buildZoneOverlay(); ropt_.overlay = &zoneOverlay_; }
        updateZoneTint(zonesOn && !ropt_.overlay);
        if (cityVisualSerial_ != pollutedWaterSerial_) { pollutedWaterSerial_ = cityVisualSerial_; updatePollutedWater(); }
      }
    } else { ropt_.overlay = nullptr; stopAmbience(); }
    // Building animations: wall-clock tick (~15 Hz, same period as automata), not sim day.
    // Driving them from sim_.day froze them between day ticks and raced at high speed.
    if (scr_ == Screen::InCity) {   // building animations keep running while the game is paused (spec 25 7.1 [OBSERVED])
      animAccMs_ += dtMs;
      while (animAccMs_ >= kAnimPeriodMs) { animAccMs_ -= kAnimPeriodMs; ++animTick_; }
    }
    ropt_.animTick = animTick_;
    { const HudSnap& hs = hudNow(); ropt_.dateMD = hs.month * 100 + hs.day; }   // date windows of sprite scripts (holidays)
    const uint64_t sig = cityViewSignature();
    const bool haveCity = gpuTry ? cityGpuValid_ : cityFbValid_;
    const bool needRaster = !haveCity || (!gpuTry && cityFb_.size() != size_t(w) * h) || sig != lastCitySig_;
    if (needRaster) {
      costShow_ = false;
      renderer_.setCostTextHook([this](int sx, int sy, const std::string& text, bool ok) {
        costText_ = text; costX_ = sx; costY_ = sy; costOk_ = ok; costShow_ = true;
      });
      const auto pr0 = PhaseClock::now();
      bool onGpu = false;
      if (gpuTry) {
        GpuCityFrame gf;
        onGpu = renderer_.renderForGpu(cam_, ropt_, &scene_, gf) && citySink_->submitCity(gf);
      }
      if (onGpu) {
        cityGpuValid_ = true; cityFbValid_ = false;
      } else {
        renderer_.render(cam_, ropt_, &scene_);
        // Keep a city-only copy so UI-only frames can skip the rasteriser.
        if (cityFb_.size() != size_t(w) * h) cityFb_.assign(size_t(w) * h, 0);
        renderer_.swapPixels(cityFb_);
        cityFbValid_ = true; cityGpuValid_ = false;
      }
      phases_.rasterMs = phaseMs(pr0, PhaseClock::now());
      phases_.rastered = true;
      lastCitySig_ = sig;
      ++cityRasterFrames_;
    } else {
      ++cityCacheHits_;
      applyCostLabelFromScene();
    }
    cityOnGpu = gpuTry && cityGpuValid_;
    if (cityOnGpu) {
      // The city is on the GPU: the UI is drawn over transparent pixels and composited there.
      for (int y = 0; y < h; y++) std::fill(drawPx_ + size_t(y) * drawPitch_, drawPx_ + size_t(y) * drawPitch_ + w, 0u);
      citySink_->setCityVisible(true);
    } else {
      if (citySink_ && ext) citySink_->setCityVisible(false);
      blitCityToDest();
    }
    if (!ext) drawPx_ = screen_.data();
  } else if (scr_ != Screen::Intro) {
    stopAmbience();
    cityFbValid_ = false; cityGpuValid_ = false;
    if (citySink_ && ext) citySink_->setCityVisible(false);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) drawPx_[size_t(y) * drawPitch_ + x] = 0xFF3A2C20u;
  }
  ui_.begin(drawPx_, w, h, in, drawPitch_);
  ui_.overlay = cityOnGpu;
  if (haveData_ && (skinH_ != h || skinW_ != w)) { skin_ = Skin(); skin_.load(data_, h, w); skinH_ = h; skinW_ = w; }
  ui_.skin = (haveData_ && skin_.loaded()) ? &skin_ : nullptr;
  uiHit_ = false;
  const bool ubOpen = showBrm_ || showBle_;
  const Input ubLive = ui_.in;
  if (ubOpen) { ui_.in = Input(); ui_.in.mx = ui_.in.my = -1; }   // the dialogs behind the BRM take no input
  switch (scr_) {
    case Screen::NeedData: drawNeedData(); break;
    case Screen::Intro: drawIntro(dtMs); break;
    case Screen::MainMenu: drawMainMenu(); break;
    case Screen::TerrainEdit: drawTerrainEdit(); break;
    case Screen::LoadList:
      if (cityBackdrop && !panelsHidden_) {
        Input live = ui_.in;
        ui_.in = Input(); ui_.in.mx = ui_.in.my = -1;   // the HUD behind the dialog takes no input
        drawToolMenu(); drawHud();
        ui_.in = live; ui_.tooltip.clear();
      }
      drawLoadList();
      break;
    case Screen::NewCity:
      if (cityBackdrop && !panelsHidden_) {
        Input live = ui_.in;
        ui_.in = Input(); ui_.in.mx = ui_.in.my = -1;
        drawToolMenu(); drawHud();
        ui_.in = live; ui_.tooltip.clear();
      }
      drawNewCity();
      break;
    case Screen::Credits: drawCredits(dtMs); break;
    case Screen::InCity: drawInCity(dtMs); break;
    case Screen::ScenarioEnd: drawScenarioEnd(); break;
  }
  if (ubOpen) { ui_.in = ubLive; drawUserBuildings(); }
  drawMessage();
  // Right-button drag scroll (original behaviour): a fixed anchor-point marker where the drag began, and the PAN arrow
  // for the scroll direction at the pointer. Both are drawn here so they show whatever the OS cursor does.
  panCursor_.clear();
  if (panning_ && scr_ == Screen::InCity) {
    panCursor_ = cursorName_;
    drawSoftCursor("anchor point cursor", panAnchorX_, panAnchorY_);
    drawSoftCursor(panCursor_, in.mx, in.my);
    cursorName_ = "BLANK";
  }
  // Over any HUD element, window or dialog the cursor is the normal desktop arrow; tool cursors only apply on the map.
  if ((uiHit_ && !panning_) || scr_ != Screen::InCity) cursorName_.clear();
  if (!ui_.tooltip.empty()) {
    int tw = ui_.textWidth(ui_.tooltip);
    Rect r{std::min(in.mx + 12, w - tw - 8), in.my + 18, tw + 8, 18};
    ui_.fill(r, rgb(255, 255, 200)); ui_.frame(r, true);
    ui_.text(r.x + 4, r.y + 2, ui_.tooltip);
  }
  drawFpsCounter(dtMs);
  const auto pe0 = PhaseClock::now();
  asyncFrameEnd();
  phases_.simWaitMs += 0;
  phases_.asyncEndMs = phaseMs(pe0, PhaseClock::now());
}

void Game::drawFpsCounter(uint32_t dtMs) {
  fpsAccMs_ += dtMs; fpsFrames_++;
  if (fpsAccMs_ >= 500) { fps_ = int((uint32_t(fpsFrames_) * 1000 + fpsAccMs_ / 2) / fpsAccMs_); fpsAccMs_ = 0; fpsFrames_ = 0; }
  // Always sample so the sparkline is populated the moment the overlay is turned on.
  fpsHist_[fpsHistPos_] = uint16_t(std::min<uint32_t>(dtMs, 255));
  fpsHistPos_ = (fpsHistPos_ + 1) % kFpsHist;
  if (fpsHistCount_ < kFpsHist) fpsHistCount_++;
  if (!engineOpts_.showFps) return;
  const int s = std::max(1, H_ / 300);   // 6x9 cell per scale step: 9 px at 480, 18 px at 600-899, 27 px at 900-1199, ...
  if (s != fpsScale_) { fpsFont_.makeBuiltin(s); fpsScale_ = s; }
  const std::string t = std::to_string(fps_) + " FPS";
  const int pad = 3 * s, gap = 2 * s;
  const int textW = fpsFont_.measure(t), textH = fpsFont_.height();
  // 1 px columns with a 1 px gap (2 px stride) — stays crisp at every scale.
  const int stride = 2, graphH = 24 * s, graphW = kFpsHist * stride - 1;
  const int innerW = std::max(textW, graphW), innerH = textH + gap + graphH;
  const int boxX = 2 * s, boxY = 2 * s;
  const Rect box{boxX, boxY, innerW + 2 * pad, innerH + 2 * pad};
  const uint32_t green = rgb(0, 255, 0), dim = rgb(0, 96, 0), bg = rgb(24, 24, 24);
  ui_.fillAlpha(box, bg, 0.72f);
  ui_.fill({box.x, box.y, box.w, 1}, green);
  ui_.fill({box.x, box.y + box.h - 1, box.w, 1}, green);
  ui_.fill({box.x, box.y, 1, box.h}, green);
  ui_.fill({box.x + box.w - 1, box.y, 1, box.h}, green);
  const int tx = box.x + pad, ty = box.y + pad;
  fpsFont_.draw(drawPx_, W_, H_, tx, ty, t, green, drawPitch_, ui_.overlay);
  // Frame-time sparkline: one pixel column per recent frame (oldest left → newest right).
  const int gx = box.x + pad + (innerW - graphW) / 2, gy = ty + textH + gap;
  int peak = 33;   // at least a 30 FPS scale so short frames still read
  for (int i = 0; i < fpsHistCount_; i++) peak = std::max(peak, int(fpsHist_[i]));
  // 16.7 ms guide (60 FPS target), snapped to a pixel row.
  const int guideY = gy + graphH - 1 - std::min(graphH - 1, (17 * graphH) / peak);
  for (int x = gx; x < gx + graphW; x += stride)
    if (x >= 0 && x < W_ && guideY >= 0 && guideY < H_) drawPx_[size_t(guideY) * drawPitch_ + x] = dim;
  for (int i = 0; i < fpsHistCount_; i++) {
    const int idx = (fpsHistPos_ - fpsHistCount_ + i + kFpsHist * 2) % kFpsHist;
    const int ms = fpsHist_[idx];
    const int h = std::max(1, std::min(graphH, (ms * graphH + peak / 2) / peak));
    const int x = gx + i * stride;
    if (x < 0 || x >= W_) continue;
    const uint32_t col = ms >= 33 ? rgb(255, 64, 0) : ms >= 20 ? rgb(255, 200, 0) : green;
    for (int dy = 0; dy < h; dy++) {
      const int y = gy + graphH - 1 - dy;
      if (y >= 0 && y < H_) drawPx_[size_t(y) * drawPitch_ + x] = col;
    }
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

bool Game::drawSkinnedToolMenu() {
  if (!haveData_ || !menuLoaded_) return false;
  if (!skin_.loaded() || !skin_.get("rootpanel")) return false;
  auto rootIt = menu_.menus.find(0xD1001000);
  if (rootIt == menu_.menus.end()) return false;
  Rect col = skin_.columnRect(W_, H_);
  int ox = col.x, oy = col.y;
  int hover = 0, selected = 0;
  const auto& rb = rootIt->second.buttons;
  for (int n = 1; n <= int(rb.size()) && n <= 9; n++) {
    const MenuButton& b = menu_.buttons[rb[n - 1]];
    if (openRoot_ == b.sub && b.sub) selected = n;
    if (ui_.hover(skin_.rootButtonRect(n, ox, oy))) hover = n;
  }
  skin_.drawRootPanel(ui_, ox, oy, hover, selected);
  if (ui_.hover(col)) uiHit_ = true;
  for (int n = 1; n <= int(rb.size()) && n <= 9; n++) {
    const MenuButton& b = menu_.buttons[rb[n - 1]];
    if (ui_.clicked(skin_.rootButtonRect(n, ox, oy))) {
      // Root buttons without a submenu (Meet, item 0x10001006) run their own item directly.
      if (!b.sub) { openRoot_ = 0; openFly_ = 0; if (b.item) execute(b.item); continue; }
      auto sub = menu_.menus.find(b.sub);
      if (sub != menu_.menus.end() && sub->second.buttons.size() == 1 && skin_.subButtonRect(n, 0, ox, oy).w == 0) {
        const MenuButton& only = menu_.buttons[sub->second.buttons[0]];
        if (!only.sub) { openRoot_ = 0; openFly_ = 0; execute(only.item); continue; }
      }
      openRoot_ = (openRoot_ == b.sub) ? 0 : b.sub; openFly_ = 0;
    }
  }
  if (!openRoot_) return true;
  int sel = 0;
  for (int n = 1; n <= int(rb.size()) && n <= 9; n++) if (menu_.buttons[rb[n - 1]].sub == openRoot_) sel = n;
  auto sm = menu_.menus.find(openRoot_);
  if (!sel || sm == menu_.menus.end()) return true;
  skin_.drawSubPanel(ui_, ox, oy, sel);
  Rect sp = skin_.subPanelRect(sel, ox, oy);
  if (sp.w > 0 && ui_.hover(sp)) uiHit_ = true;
  uint32_t flyOpen = 0; Rect flyAnchor;
  for (int k = 0; k < int(sm->second.buttons.size()); k++) {
    const MenuButton& b = menu_.buttons[sm->second.buttons[k]];
    Rect r = skin_.subButtonRect(sel, k, ox, oy);
    auto ii = menu_.items.find(b.item);
    std::string tip = ii != menu_.items.end() ? str(ii->second.nameTable, ii->second.nameId) : std::string();
    const Image* strip = skin_.toolStrip(b.id);
    bool clicked = false;
    if (strip && skin_.imageButtonCells(ui_, strip, 4, r, tip, openFly_ == b.sub && b.sub, true, clicked)) {
      if (clicked) {
        if (b.sub) openFly_ = (openFly_ == b.sub) ? 0 : b.sub;
        else { execute(b.item); if (tool_ != ToolKind::Unavailable) openRoot_ = 0; }
      }
    } else if (ui_.button(r, tip.empty() ? "?" : tip.substr(0, 6))) {
      if (b.sub) openFly_ = (openFly_ == b.sub) ? 0 : b.sub;
      else { execute(b.item); if (tool_ != ToolKind::Unavailable) openRoot_ = 0; }
    }
    if (b.sub && openFly_ == b.sub) { flyOpen = b.sub; flyAnchor = r; }
  }
  if (flyOpen) {
    auto fm = menu_.menus.find(flyOpen);
    if (fm != menu_.menus.end()) {
      Rect fr{flyAnchor.x - 194, flyAnchor.y, 190, int(fm->second.buttons.size()) * 28 + 8};
      ui_.panel(fr);
      if (ui_.hover(fr)) uiHit_ = true;
      if (fm->second.colour) ui_.fill({fr.x + 2, fr.y + 2, 4, fr.h - 4}, 0xFF000000u | ((fm->second.colour & 255) << 16) | (fm->second.colour & 0xFF00) | ((fm->second.colour >> 16) & 255));
      int fy = fr.y + 4;
      for (uint32_t bid : fm->second.buttons) {
        const MenuButton& b = menu_.buttons[bid];
        auto ii = menu_.items.find(b.item);
        std::string label = ii != menu_.items.end() ? str(ii->second.nameTable, ii->second.nameId) : "(item)";
        bool enabled = ii == menu_.items.end() || ii->second.enabled;
        if (flyOpen == 0xD100A100 && !disastersOn_) enabled = false;
        if (!transportToolEnabled(b.item)) enabled = false;
        if (ui_.button({fr.x + 10, fy, 176, 26}, label, enabled)) { execute(b.item); if (tool_ != ToolKind::Unavailable) { openFly_ = 0; openRoot_ = 0; } }
        fy += 28;
      }
    }
  }
  return true;
}

void Game::drawToolMenu() {
  if (!menuLoaded_) return;
  if (drawSkinnedToolMenu()) return;
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
        if (!transportToolEnabled(b.item)) enabled = false;
        if (ui_.button({fr.x + 10, fy, 176, 26}, label, enabled)) { execute(b.item); if (tool_ != ToolKind::Unavailable) { openFly_ = 0; openRoot_ = 0; } }
        fy += 28;
      }
    }
  }
}

void Game::drawHud() {
  BottomBarPlace place;
  bool skinBar = haveData_ && skin_.loaded() && skin_.drawBottomBar(ui_, &place);
  int bh = skinBar ? std::max(30, place.bar.h) : 30;
  Rect bar{0, H_ - bh, W_, bh};
  if (!skinBar) ui_.panel(bar);
  if (ui_.hover(bar)) uiHit_ = true;
  const HudSnap& hud = hudNow();
  int month = std::min(11, (hud.dayOfYear - 1) * 12 / 365);
  std::string date = std::string(kMonths[month]) + " " + std::to_string(hud.year);
  std::string funds = "$" + groupDigits(hud.funds), popText = "Pop " + groupDigits(hud.pop);
  uint32_t fundsCol = hud.funds < 0 ? rgb(255, 0, 0) : 0;
  // Navigator panel (mini-map, zoom, rotate, layers) is in the bottom-right corner; the RCI indicator is just left of it
  // (spec 08 4.2 observed). Both are anchored to the window, not to the bar art.
  const int navX = navigatorRect(bh).x;
  auto centredField = [&](const Rect& r, const std::string& t, uint32_t col) {
    ui_.textCentered(r, t, "main10", col);   // spec 08 4.2: text centred in its field rect
  };
  if (skinBar) {
    centredField(place.fieldA, world_.info.cityName, 0);
    centredField(place.fieldB, groupDigits(hud.pop), 0);
    centredField(place.fieldC, funds, fundsCol);
    centredField(place.fieldD, date, 0);
  } else {
    ui_.text(8, H_ - bh + 8, world_.info.cityName);
    ui_.text(180, H_ - bh + 8, date);
    ui_.text(280, H_ - bh + 8, funds, "main10", fundsCol);
    ui_.text(400, H_ - bh + 8, popText);
  }

  // Pause / run / speed (spec 09 4.2). With the skin the art is anchored at P2 (pause), P1 (run) and P4 (speed lights).
  auto tip = [&](uint32_t id, const char* fb) { std::string t = str(0x225872FE, id); return t.empty() ? std::string(fb) : t; };
  const int fbx = 520;  // fallback (no art) button strip
  auto setSpeedBtn = [&](int sp) { speed_ = sp; setPaused(false); sound(24 + sp); };
  const Image* imPause = skinBar ? skin_.get("pause") : nullptr;
  if (skinBar && imPause && skin_.get("run") && skin_.get("speed.0")) {
    Rect pr{place.p2x, place.p2y, 22, 22}, rr{place.p1x, place.p1y, 26, 22};
    // Lit (red / green) for the current state, grey otherwise; hover shows the yellow variant.
    const bool pauseLit = paused_ && pauseFlashMs_ < 750;
    const Image* pa = ui_.hover(pr) ? skin_.get("pause.hover") : pauseLit ? imPause : skin_.get("pause.off");
    const Image* ra = ui_.hover(rr) ? skin_.get("run.hover") : !paused_ ? skin_.get("run") : skin_.get("run.off");
    if (pa) ui_.blit(*pa, pr.x, pr.y);
    if (ra) ui_.blit(*ra, rr.x, rr.y);
    if (ui_.hover(pr)) ui_.tooltip = tip(0x64, "Pause");
    if (ui_.hover(rr)) ui_.tooltip = tip(0x65, "Run");
    if (ui_.clicked(pr) && !paused_) { setPaused(true); sound(24); }
    if (ui_.clicked(rr) && paused_) { setPaused(false); sound(23); }
    int sp = std::max(1, std::min(4, speed_));
    ui_.blit(*skin_.get("speed." + std::to_string(sp - 1)), place.p4x, place.p4y);
    static const char* spTip[4] = {"Turtle", "Llama", "Cheetah", "African Swallow"};
    for (int i = 0; i < 4; i++) {
      Rect cr{place.p4x + 3 + i * 11, place.p4y + 4, 10, 12};
      if (ui_.hover(cr)) ui_.tooltip = tip(0x66 + i, spTip[i]);
      if (ui_.clicked(cr)) setSpeedBtn(i + 1);
    }
    // Help ("?") button, 4-cell strip at P3.
    // The green "?" arms the query tool (spec 09 7a.4 [OBSERVED]).
    bool hc = false;
    skin_.imageButtonCells(ui_, skin_.get("help"), 4, {place.p3x, place.p3y, 40, 39}, tip(0x3C, "Query"), tool_ == ToolKind::Query, true, hc);
    if (hc) setTool(ToolKind::Query);
    // Engine options (Metroplex3K only): same chrome as "?", with a cog. Prefer just right of help; when the RCI
    // indicator leaves no room (typical at 1024-wide), tuck it above and a few pixels right of help.
    Rect eb{place.p3x + 38, place.p3y, 40, 39};
    if (eb.x + eb.w > navX - RciGeometry::imgW - 2) eb = {place.p3x + 18, place.p3y - 36, 40, 39};
    bool ec = false;
    if (skin_.imageButtonCells(ui_, skin_.get("engine"), 4, eb, "M3K Options", false, true, ec)) {
      if (ec) openWindow("engine");
    } else {
      if (ui_.hover(eb)) { uiHit_ = true; ui_.tooltip = "M3K Options"; }
      if (ui_.button({eb.x + 2, eb.y + 8, 36, 22}, "M3K", true, false)) openWindow("engine");
    }
  } else {
    if (ui_.button({fbx, H_ - bh + 3, 44, 24}, paused_ ? "Run" : "Pause")) { setPaused(!paused_); sound(paused_ ? 24 : 23); }
    for (int i = 1; i <= 4; i++)
      if (ui_.button({fbx + 48 + (i - 1) * 26, H_ - bh + 3, 24, 24}, std::to_string(i), true, !paused_ && speed_ == i)) setSpeedBtn(i);
    if (ui_.button({fbx + 156, H_ - bh + 3, 44, 24}, "M3K", true, false)) openWindow("engine");
  }

  drawNavigator(bh);

  // RCI bars: bottom-anchored, just left of the navigator.
  int rx = navX - RciGeometry::imgW, ry = H_ - RciGeometry::imgH;
  const Valve* v[3] = {&hud.valveR, &hud.valveC, &hud.valveI};
  uint32_t cols[3] = {rgb(0, 192, 0), rgb(0, 0, 192), rgb(192, 192, 0)};
  const char* nm[3] = {"R", "C", "I"};
  // Spec 09 §4.5 / 15 §3.2 [STATIC Q-BQ]: demand unit u = N/1000 (= V/Max for ±2000 R/C/I valves).
  bool skinRci = haveData_ && skin_.loaded() &&
                 skin_.drawRciIndicator(ui_, rx, ry, v[0]->value / 2000.0, v[1]->value / 2000.0, v[2]->value / 2000.0);
  if (skinRci && ui_.hover({rx, ry, RciGeometry::imgW, RciGeometry::imgH})) uiHit_ = true;
  if (skinRci) {   // R / C / I labels in the gap between the two bar slots (225872FE:41..43)
    // Hand-drawn 5x7 pixel glyphs: scaling the 9 px font down turns the R into an A. Other languages' letters fall
    // back to the (scaled) text font.
    static const char* fb[3] = {"R", "C", "I"};
    static const char* glyph[3][7] = {
        {"XXXX.", "X...X", "X...X", "XXXX.", "X.X..", "X..X.", "X...X"},
        {".XXX.", "X...X", "X....", "X....", "X....", "X...X", ".XXX."},
        {"XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "XXXXX"}};
    const uint32_t ink = rgb(24, 40, 140);
    for (int i = 0; i < 3; i++) {
      std::string t = str(0x225872FE, 0x41 + i);
      if (t.empty() || t == fb[i]) {
        const int gx = rx + RciGeometry::barX[i], gy = ry + RciGeometry::labelTop + (RciGeometry::labelH - 7) / 2;   // 5 px = bar width
        for (int row = 0; row < 7; row++)
          for (int col = 0; col < 5; col++)
            if (glyph[i][row][col] == 'X') ui_.fill({gx + col, gy + row, 1, 1}, ink);
      } else {
        Rect lr{rx + RciGeometry::barX[i] - 4, ry + RciGeometry::labelTop, RciGeometry::barW + 8, RciGeometry::labelH};
        ui_.textCentered(lr, t, "system9", ink);
      }
    }
  }
  for (int i = 0; i < 3 && !skinRci; i++) {
    int cx = rx + i * 14;
    int hgt = std::max(-12, std::min(12, v[i]->value * 12 / 2000));
    ui_.fill({cx, H_ - bh + 15, 10, 1}, ui_.panelDark);
    if (hgt >= 0) ui_.fill({cx, H_ - bh + 15 - hgt, 10, hgt}, cols[i]); else ui_.fill({cx, H_ - bh + 16, 10, -hgt}, cols[i]);
    ui_.text(cx, H_ - bh + 8, nm[i]);
  }
  // News ticker: the bar's text line when the skin gives one, else a strip above the bar.
  if (skinBar) drawTicker(place.textLine);
  else drawTicker({122, H_ - bh - 24, W_ - 126, 22});
  const int tipY = H_ - bh - 18;
  (void)tipY;   // the original draws no tool-name or coordinate overlay above the bar [OBSERVED]
  if (costShow_ && !costText_.empty()) {
    // Observed (spec 25 9.4): cream box, 1 px black border, black text (red when unaffordable), 16 px tall.
    int tw = ui_.textWidth(costText_);
    Rect cr{std::min(costX_, W_ - tw - 6), std::max(0, costY_), tw + 4, 16};
    ui_.fill(cr, rgb(0, 0, 0));
    ui_.fill({cr.x + 1, cr.y + 1, cr.w - 2, cr.h - 2}, rgb(255, 255, 231));
    ui_.text(cr.x + 2, cr.y + 1, costText_, "main10", costOk_ ? rgb(0, 0, 0) : rgb(222, 44, 41));
  }
}

// ---------------------------------------------------------------- map interaction

std::vector<std::pair<int, int>> Game::routePath(int x0, int z0, int x1, int z1) {
  // Prefer the network-tools router (spec 12 §6.2) so highway gets both carriageways and each
  // tool applies its own slope / water / staircase rules. Fall back to a plain L when tiling is off.
  if (toolArg_ != kToolOnRamp) {
    auto path = sim_.city().routeNetworkPath(NetworkKind(toolArg_ ? toolArg_ : 1), x0, z0, x1, z1);
    if (!path.empty()) return path;
  }
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
    case ToolKind::Road:
      if (toolArg_ == kToolOnRamp) return c.quoteOnRamp(x1, z1);
      return c.quoteNetwork(NetworkKind(toolArg_ ? toolArg_ : 1), routePath(x0, z0, x1, z1));
    case ToolKind::Plop: return c.quotePlop(toolOcc_, x1, z1);
    case ToolKind::Terrain: {
      City::Quote q; q.valid = true;
      if (!terra_) return q;
      TerraPlan p = toolArg_ == 1 ? terra_->planRaise(x1, z1) : toolArg_ == 2 ? terra_->planLower(x1, z1) : toolArg_ == 32 ? terra_->planWater(x1, z1)
                    : toolArg_ == 4 ? terra_->planLevel(x0, z0, x1, z1) : TerraPlan();
      if (toolArg_ == 100) {   // tree tool: cyan unless a network, building or park piece is there; water shows cyan but plants nothing
        q.valid = !treeTileBlocked(x1, z1); q.cost = treeTool_.cost;
        return q;
      }
      q.valid = p.legal; bool clearOk = true; q.cost = terra_->totalCost(p, &clearOk); if (!clearOk) q.valid = false;
      return q;
    }
    default: return City::Quote();
  }
}

void Game::buildScene() {
  scene_.previews.clear(); scene_.tints.clear();
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
      if (toolArg_ == kToolOnRamp) {
        auto tiles = sim_.city().onRampPreview(x1, z1);
        for (const auto& p : tiles) { TilePreview t; t.x0 = t.x1 = p.first; t.z0 = t.z1 = p.second; scene_.previews.push_back(t); }
        q = quoteTool(x1, z1, x1, z1);
        if (tiles.empty()) { pv.x0 = pv.x1 = x1; pv.z0 = pv.z1 = z1; scene_.previews.push_back(pv); }
        break;
      }
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
      showCost = scr_ == Screen::InCity;
      scene_.previews.push_back(pv);
      break;
    case ToolKind::Query: {
      // Spec 25 9.4 [OBSERVED]: the object under the pointer is tinted per pixel (50% towards RGB 0,130,176). A building
      // tints its whole sprite; anything else tints one tile (flat diamond plus the sprites on it).
      SpriteTint t; t.r = 0; t.g = 130; t.b = 176;
      t.x0 = t.x1 = x1; t.z0 = t.z1 = z1; t.ground = true;
      int oi = sim_.occupantAt(x1, z1);
      if (oi >= 0 && size_t(oi) < world_.occupants.size()) {
        const Occupant& o = world_.occupants[size_t(oi)];
        t.x0 = o.x; t.z0 = o.z; t.x1 = o.x + o.sizeX - 1; t.z1 = o.z + o.sizeZ - 1; t.ground = false;
      }
      scene_.tints.push_back(t); showCost = false; q.valid = true;
      break;
    }
    case ToolKind::Dispatch:
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
        auto r = terra_->apply(p, fundsRef()); invalidateCityView();
        sound(r.status == ApplyStatus::Applied ? 15 : (r.status == ApplyStatus::NoMoney ? 32 : 30));
      }
      break;
    case ToolKind::Zone: sim_.paintZoneType(x0, z0, x1, z1, toolArg_); dirty_ = true; break;
    case ToolKind::Road: {
      if (toolArg_ == kToolOnRamp) {
        City::Quote q = quoteTool(x1, z1, x1, z1);
        if (!q.valid) { sound(30); break; }
        if (q.cost != 0 && q.cost > sim_.funds) { sound(32); break; }
        if (sim_.city().buildOnRamp(x1, z1)) dirty_ = true;
        else sound(sim_.city().lastFailNoMoney ? 32 : 30);
        break;
      }
      City::Quote q = quoteTool(x0, z0, x1, z1);
      if (!q.valid) { sound(30); break; }
      if (q.cost != 0 && q.cost > sim_.funds) { sound(32); break; }
      NetworkKind nk = NetworkKind(toolArg_ ? toolArg_ : 1);
      auto path = routePath(x0, z0, x1, z1);
      // Spec 12 §6.4 / §8: bridge, tunnel, then neighbour-connection yes/no dialogs before commit.
      if (!pendingNetAsk_) {
        City::NetworkConfirm conf = sim_.city().peekNetworkConfirm(nk, path);
        if (conf.kind != City::NetworkConfirmKind::None) {
          pendingNetAsk_ = true;
          pendingNetAskKind_ = conf.kind;
          pendingNetKind_ = nk;
          pendingNetPath_ = path;
          WinEnv E = env();
          std::string title = E.win(0x252, "Confirm");
          std::string body;
          if (conf.kind == City::NetworkConfirmKind::Bridge) {
            body = subst(E.win(conf.powerBridge ? 0x2D2u : 0x28Bu,
                               conf.powerBridge ? "Build a power line bridge for %s?" : "Build a bridge for %s?"),
                         {E.money(conf.price)});
            msgBoxPurpose_ = 4;
          } else if (conf.kind == City::NetworkConfirmKind::Tunnel) {
            body = subst(E.win(0x253, "Build a tunnel for %s?"), {E.money(conf.price)});
            msgBoxPurpose_ = 5;
          } else {
            title = E.win(0x252, "Neighbor Connection");
            body = subst(E.win(0x251, "Connect to the neighboring city for %s?"), {E.money(conf.price)});
            msgBoxPurpose_ = 3;
          }
          msgBox_.open(title, body, 3, MessageBox::IconQuestion);
          break;
        }
      }
      sim_.buildNetwork(nk, path);
      dirty_ = true;
      break;
    }
    case ToolKind::Demolish: sim_.bulldoze(x0, z0, x1, z1); dirty_ = true; break;
    case ToolKind::Plop: {
      City::Quote q = quoteTool(x1, z1, x1, z1);
      if (!q.valid) { sound(30); message("Cannot build here."); break; }
      if (q.cost != 0 && q.cost > sim_.funds) { sound(32); message("Insufficient funds."); break; }
      if (sim_.plop(toolOcc_, x1, z1, int(q.cost))) { dirty_ = true; if (audio_) audio_->notifyPlaced(tileSoundContext(x1, z1)); }
      else message(sim_.city().lastFailNoMoney ? "Insufficient funds." : "Cannot build here.");
      break;
    }
    case ToolKind::Dispatch: dispatchUnit(toolArg_, x1, z1); break;
    case ToolKind::Query: {
      if (audio_) audio_->notifyQuery(tileSoundContext(x1, z1));
      buildQueryInfo(x1, z1, queryInfo_);
      queryWin_.begin();   // opens at (40, 40) the first time, then where the player left it (spec 09 6.12.1)
      showQuery_ = true;
      break;
    }
    default: break;
  }
}

bool Game::treeTileBlocked(int x, int z) const {
  if (!world_.inTiles(x, z)) return true;
  if (!simReady_) {
    for (const Occupant& o : world_.occupants)
      if (x >= o.x && x < o.x + std::max(o.sizeX, 1) && z >= o.z && z < o.z + std::max(o.sizeZ, 1)) return true;
    return false;
  }
  const City& c = sim_.city();
  return c.occupantAt(x, z) >= 0 || c.isRoadTile(x, z) || c.isPowerLine(x, z);
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

std::string Game::cursorForTool() const {
  switch (tool_) {
    case ToolKind::None: return "CENTER CURSOR";
    case ToolKind::Query: return "QUERY CURSOR";
    case ToolKind::Demolish: return "DEMOLISH CLEAR CURSOR";
    case ToolKind::Snapshot: return "snapshot cursor";
    case ToolKind::Dispatch: return toolArg_ == 0 ? "DISPATCH POLICE" : toolArg_ == 1 ? "DISPATCH FIREFIGHTERS" : "DISPATCH PLANE";
    case ToolKind::Zone: {
      static const char* n[] = {"de-zone cursor", "ZONE RES LOW", "ZONE RES MED", "ZONE RES HIGH", "", "ZONE COM LOW", "ZONE COM MED", "ZONE COM HIGH",
                                "", "ZONE IND LOW", "ZONE IND MED", "ZONE IND HIGH", "", "", "ZONE AIR PORTS", "ZONE SEA PORTS", "", "ZONE LANDFILL"};
      if (toolArg_ >= 0 && toolArg_ < 18 && n[toolArg_][0]) return n[toolArg_];
      return "CENTER CURSOR";
    }
    case ToolKind::Terrain:
      switch (toolArg_) { case 1: return "RAISE TERRAIN"; case 2: return "LOWER TERRAIN"; case 4: return "LEVEL TERRAIN"; case 32: return "PAINT WATER"; default: return "PLANT TREE"; }
    case ToolKind::Road:
      if (toolArg_ == kToolOnRamp) return "ONRAMP";
      switch (NetworkKind(toolArg_ ? toolArg_ : 1)) {
        case NetworkKind::Rail: return "RAIL ROAD"; case NetworkKind::Power: return "POWER LINES"; case NetworkKind::Pipe: return "WATER PIPES";
        case NetworkKind::Subway: return "SUBWAY TUBE"; case NetworkKind::Highway: return "AVENUE"; default: return "ROAD";
      }
    case ToolKind::Plop: {
      struct P { uint32_t occ; const char* c; };
      static const P p[] = {{0xD2, "POLICE"}, {0x1F41, "JAIL"}, {0xD3, "FIRE"}, {0xD1, "HOSPITAL"}, {0x1F94, "SCHOOL"}, {0xD6, "COLLEGE"}, {0xF5, "LIBRARY"},
                            {0xD4, "MUSEUM"}, {0x0D, "SMALL PARK"}, {0xD5, "BIG PARK"}, {0xDA, "ZOO"}, {0xD7, "BALLPARK"}, {0x234B, "PLAYGROUND"},
                            {0x234C, "POND"}, {0x1F96, "BUS STOP"}, {0xED, "TRAIN STATION"}, {0xE9, "SUBWAY STATION"}, {0x3E8A, "SUBWAY RAIL CONNECT"}};
      for (const auto& e : p) if (e.occ == toolOcc_) return e.c;
      return "REWARDS CURSOR";
    }
    default: return "";
  }
}

// Zones layer (spec 25 9.2 [OBSERVED]; rows of the lighter zone types INFERRED): every zoned tile is a flat palBasic colour,
// non-zoned building lots are rust (row 34); empty unzoned land keeps its terrain colour. Roads, flora and vehicles draw on top.
void Game::buildZoneOverlay() {
  const World& w = world_;
  zoneOverlay_.slot = 0;
  zoneOverlay_.row.assign(size_t(w.W) * w.H, 0xFF);
  auto rowOf = [](int zone) -> int {
    switch (zone) {
      case 1: return 26; case 2: return 25; case 3: return 24;       // residential light / medium / dense
      case 5: return 29; case 6: return 28; case 7: return 27;       // commercial
      case 9: return 23; case 10: return 22; case 11: return 21;     // industrial
      case 15: return 30; case 14: return 31; case 17: return 32;    // seaport, airport, landfill
      default: return 0xFF;
    }
  };
  const City& c = sim_.city();
  for (int x = 0; x < w.W; x++)
    for (int z = 0; z < w.H; z++) {
      const size_t ti = w.tIdx(x, z);
      int r = ti < w.zone.size() ? rowOf(w.zone[ti]) : 0xFF;
      if (r == 0xFF) {
        const int oi = c.occupantAt(x, z);
        if (oi >= 0 && size_t(oi) < w.occupants.size()) {
          const uint32_t k = w.occupants[size_t(oi)].agentType / 1000;
          if (!(k >= 3 && k <= 9)) r = 34;      // civic buildings, landmarks: rust lots
        }
      }
      zoneOverlay_.row[ti] = uint8_t(r);
    }
}

// Water tiles whose water-pollution amount reaches WaterPollutedThreshold are drawn with the palBasic row 51 tint (spec 25 6.4).
void Game::updatePollutedWater() {
  const World& w = world_;
  const City& c = sim_.city();
  std::vector<uint8_t> t(size_t(w.W) * w.H, 0);
  bool any = false;
  for (int x = 0; x < w.W; x++)
    for (int z = 0; z < w.H; z++)
      if (w.waterAt(x, z) > w.altAt(x, z) && c.pollution.waterPolluted(x, z)) { t[w.tIdx(x, z)] = 1; any = true; }
  if (!any) t.clear();
  renderer_.setPollutedWater(t);
}

void Game::updateZoneTint(bool on) {
  if (!on) { if (!zoneTint_.empty()) { renderer_.clearTileTints(); zoneTint_.clear(); invalidateCityView(); } return; }
  const World& w = world_;
  std::vector<uint8_t> rows(size_t(w.W) * w.H, 0);
  for (size_t i = 0; i < rows.size() && i < w.zone.size(); i++) {
    switch (w.zone[i]) {
      case 1: rows[i] = 26; break; case 2: rows[i] = 25; break; case 3: rows[i] = 24; break;
      case 5: rows[i] = 29; break; case 6: rows[i] = 28; break; case 7: rows[i] = 27; break;
      case 9: rows[i] = 23; break; case 10: rows[i] = 22; break; case 11: rows[i] = 21; break;
      case 15: rows[i] = 30; break; case 14: rows[i] = 31; break; case 17: rows[i] = 32; break;
      default: break;
    }
  }
  if (rows == zoneTint_) return;
  zoneTint_ = rows;
  renderer_.setTileTints(rows);
  invalidateCityView();
}

void Game::drawSoftCursor(const std::string& name, int x, int y) {
  if (!haveData_ || name.empty()) return;
  auto it = softCur_.find(name);
  if (it == softCur_.end()) {
    CursorImage ci;
    if (!loadCursor(data_, name, ci, name == "anchor point cursor")) ci = CursorImage();   // the anchor marker is the .bmp (spec 04 8.3)
    it = softCur_.emplace(name, std::move(ci)).first;
  }
  if (it->second.ok()) ui_.blit(it->second.image, x - it->second.hotX, y - it->second.hotY);
}

// Holding Alt puts the bulldozer in hand, holding Ctrl the query tool; releasing the key gives back the selected tool.
void Game::updateModifierTool() {
  const Input& in = ui_.in;
  if (dragging_ || panning_) return;   // never swap the tool under a drag in progress
  const bool typing = ui_.focusId != 0 || cheatOpen_;
  const ToolKind want = typing ? ToolKind::None : in.modAlt ? ToolKind::Demolish : in.modCtrl ? ToolKind::Query : ToolKind::None;
  if (want == ToolKind::None) {
    if (!modTool_) return;
    modTool_ = false;
    tool_ = modSavedTool_; toolArg_ = modSavedArg_; toolOcc_ = modSavedOcc_; toolName_ = modSavedName_;
    hoverValid_ = false; scene_.previews.clear(); scene_.tints.clear(); scene_.cost.show = false; costShow_ = false;
    return;
  }
  if (modTool_ && tool_ == want) return;
  if (!modTool_) {
    modTool_ = true;
    modSavedTool_ = tool_; modSavedArg_ = toolArg_; modSavedOcc_ = toolOcc_; modSavedName_ = toolName_;
  }
  tool_ = want; toolArg_ = 0; toolOcc_ = 0; toolName_.clear();
  hoverValid_ = false; scene_.previews.clear(); scene_.tints.clear(); scene_.cost.show = false; costShow_ = false;
}

void Game::handleScrolling(uint32_t dtMs) {
  const Input& in = ui_.in;
  auto scrollBy = [&](float dx, float dy) {
    float Wt = 8.f * float(1 << cam_.zoom), Ht = Wt / 2;
    float du = (dy / (Ht / 2) - dx / (Wt / 2)) / 2, dv = (dy / (Ht / 2) + dx / (Wt / 2)) / 2;
    cam_.u = std::max(0.f, std::min(float(world_.W), cam_.u + du)); cam_.v = std::max(0.f, std::min(float(world_.H), cam_.v + dv));
  };
  if (in.rightPressed && !uiHit_) { panning_ = true; panAnchorX_ = in.mx; panAnchorY_ = in.my; }
  if (!in.rightDown) panning_ = false;
  const int updates = viewUpdates_;   // fixed-rate view updates (spec 25 10)
  cursorName_ = cursorForTool();
  if (panning_) {
    int dx = (in.mx - panAnchorX_) / 2, dy = (in.my - panAnchorY_) / 2;
    cursorName_ = panCursorName(dx, dy);
    int vx = dragScrollVelocity(dx), vy = dragScrollVelocity(dy);
    for (int i = 0; i < updates; i++) scrollBy(float(vx), float(vy));
    return;
  }
  // Edge scrolling (8-px border strips, spec 25 10) is off: project decision recorded in spec 29 3.5 (the original's
  // edge margin could not be measured; parking the pointer 0-12 px from the edge did not scroll). Flip kEdgeScroll
  // to bring it back.
  constexpr bool kEdgeScroll = false;
  if (!kEdgeScroll || uiHit_ || dragging_ || (!in.mx && !in.my)) return;
  // Edge scrolling: 8-px border strips of the city view.
  int ex = in.mx < 8 ? -1 : in.mx >= W_ - 8 ? 1 : 0, ey = in.my < 8 ? -1 : in.my >= H_ - 8 ? 1 : 0;
  if (ex || ey) {
    cursorName_ = panCursorName(ex * 12, ey * 12);
    for (int i = 0; i < updates; i++) scrollBy(float(ex * 24), float(ey * 24));
  }
}

void Game::handleMap() {
  const Input& in = ui_.in;
  if (!world_.W) return;
  handleScrolling(frameDt_);
  if (in.wheel && !uiHit_) cam_.zoom = std::max(0, std::min(4, cam_.zoom + (in.wheel > 0 ? 1 : -1)));
  int tx = 0, tz = 0;
  bool onTile = !uiHit_ && renderer_.pickGround(cam_, in.mx, in.my, tx, tz);
  // The query target is the object under the pointer by per-pixel sprite hit test, else the clicked tile (spec 09 6.12.2).
  if (onTile && tool_ == ToolKind::Query) {
    PickResult pr;
    if (renderer_.pick(cam_, in.mx, in.my, pr) && pr.hit && world_.inTiles(pr.tileX, pr.tileZ)) { tx = pr.tileX; tz = pr.tileZ; }
  }
  // Right button: cancel tool, else pan by dragging.
  if (in.rightPressed && !uiHit_) {
    if (dragging_) dragging_ = false;
    else if (tool_ != ToolKind::None) setTool(ToolKind::None);
  }
  updateHover(tx, tz, onTile);
  if (onTile && in.pressed) {
    // No tool: the "center" cursor is showing and a click scrolls that point to the screen centre (spec 08 8.2).
    if (tool_ == ToolKind::None) centreOn(tx, tz);
    else if (tool_ != ToolKind::Unavailable) { dragging_ = true; ax_ = tx; az_ = tz; }
  }
  // Terrain tools: press-and-hold repeats raise/lower/water/trees; level applies on release.
  if (onTile && dragging_ && in.down && tool_ == ToolKind::Terrain && toolArg_ != 4 && (terra_ || toolArg_ == 100)) {
    static uint32_t tick = 0;
    if ((++tick & 3) == 0 || in.pressed) {
      if (toolArg_ == 100) {
        FloraRng rng = floraRngFromLcg(floraLcg_);
        uint8_t cell = randomTreeCell(3, rng);
        TreePlan p = planTree(world_, tx, tz, cell, treeTool_, [this](int x, int z) { return treeTileBlocked(x, z); });
        if (p.legal && fundsRef() >= p.cost) { commitTree(world_, p); fundsRef() -= p.cost; invalidateCityView(); dirty_ = true; sound(17); }
        else if (in.pressed) sound(p.legal ? 32 : 30);
      } else if (terra_) {
        TerraPlan p = toolArg_ == 1 ? terra_->planRaise(tx, tz) : toolArg_ == 2 ? terra_->planLower(tx, tz) : terra_->planWater(tx, tz);
        auto r = terra_->apply(p, fundsRef());
        if (r.status != ApplyStatus::Applied && in.pressed) sound(r.status == ApplyStatus::NoMoney ? 32 : 30);
        invalidateCityView();
        if (scr_ == Screen::InCity) { sim_.city().markOccupantsDirty(); dirty_ = true; }
      }
    }
  }
  if (dragging_ && in.released) {
    dragging_ = false;
    if (onTile) executeTool(ax_, az_, tx, tz);
    // The query tool stays armed after a panel closes (spec 09 6.12.1).
  }
  buildScene();
}

// ---------------------------------------------------------------- in-city

void Game::drawInCity(uint32_t dtMs) {
  const Input& in = ui_.in;
  bool typing = ui_.focusId != 0 || cheatOpen_;
  if (!typing) {
    // Arrow keys on every view update: 32 px per update; one key, or two perpendicular keys (diagonal); any other combination
    // (three or four keys, or two opposite keys) does not scroll (spec 25 10 [STATIC]).
    const bool l = in.held[0], r = in.held[1], u = in.held[2], d = in.held[3];
    const int nh = int(l) + int(r), nv = int(u) + int(d);
    if (nh <= 1 && nv <= 1 && nh + nv >= 1 && !showQuery_) {   // arrow-key scrolling is ignored while the query panel is open (spec 09 6.12.1)
      const float s = 32.f * float(viewUpdates_);
      const float dx = (r ? s : 0) - (l ? s : 0), dy = (d ? s : 0) - (u ? s : 0);
      float Wt = 8.f * float(1 << cam_.zoom), Ht = Wt / 2;
      float du = (dy / (Ht / 2) - dx / (Wt / 2)) / 2, dv = (dy / (Ht / 2) + dx / (Wt / 2)) / 2;
      cam_.u = std::max(0.f, std::min(float(world_.W), cam_.u + du)); cam_.v = std::max(0.f, std::min(float(world_.H), cam_.v + dv));
    }
    handleKeys(in);
    if (in.key[K_TAB]) cheatOpen_ = true;
    if (in.key[K_ESC] && !showQuery_) {   // Esc does not close the query panel (spec 09 6.12.1)
      if (tool_ != ToolKind::None) setTool(ToolKind::None);
      else if (openRoot_) openRoot_ = 0;
      else if (!anyBlockingWindow()) { if (dirty_ && false) {} scr_ = Screen::MainMenu; }
    }
  }
  if (!panelsHidden_) { drawToolMenu(); drawHud(); }
  else {
    // F12 hid the tool column, the bottom bar and the navigator: only a small round "show UI" button stays, bottom-right
    // (about 20 x 20 px; spec 08 5.4 [OBSERVED]).
    const Rect b{W_ - 26, H_ - 26, 20, 20};
    const bool hot = ui_.hover(b);
    if (hot) { uiHit_ = true; ui_.tooltip = "Show UI"; }
    for (int y = 0; y < b.h; y++)
      for (int x = 0; x < b.w; x++) {
        const float dx = float(x) + 0.5f - 10.f, dy = float(y) + 0.5f - 10.f, d2 = dx * dx + dy * dy;
        if (d2 > 100.f) continue;
        ui_.fill({b.x + x, b.y + y, 1, 1}, d2 > 72.f ? rgb(0x31, 0x36, 0x7C) : hot ? rgb(0xDC, 0xE4, 0xF0) : rgb(0x7B, 0x92, 0xFF));
      }
    if (ui_.clicked(b)) panelsHidden_ = false;
  }
  drawWindows(dtMs);
  drawCheatBox();
  updateModifierTool();
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
