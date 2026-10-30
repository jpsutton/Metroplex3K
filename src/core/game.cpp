#include "core/game.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

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

bool Game::init(const fs::path& gameRoot, std::string& err) {
  ui_.fonts = &fonts_;
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
  IniFile mi;
  if (data_.ini().load("MenuItem.INI", mi)) menuLoaded_ = menu_.parse(mi);
  scr_ = Screen::MainMenu;
  return true;
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

void Game::requestFile(const std::string& path) { if (haveData_) loadFile(path); }

std::string Game::windowTitle() const {
  std::string t = "Metroplex3K";
  if (scr_ == Screen::InCity && world_.W) t += " - " + world_.info.cityName;
  return t;
}

void Game::adopt(World&& w) {
  world_ = std::move(w);
  if (haveData_) {
    data_.unmountSets();
    if (world_.scheme.buildingSet) data_.mountBuildingSet(world_.scheme.buildingSet);
    if (world_.scheme.floraSet) data_.mountFloraSet(world_.scheme.floraSet);
  }
  renderer_.setWorld(&world_, haveData_ ? &data_ : nullptr);
  terra_ = std::make_unique<Terraformer>(world_);
  { IniFile bl; if (haveData_ && data_.ini().load("SC3BudgetLayer.ini", bl)) terra_->setCosts(TerraformCosts::fromIni(bl)); }
  sim_.init(&world_, haveData_ ? &data_ : nullptr, haveData_ ? &cityCfg_ : nullptr, 12345u + uint32_t(world_.W));
  sim_.city().onSound = [this](int id) { sound(id); };
  sim_.city().onMessage = [this](const std::string& m) { message(m); };
  showPicker_ = false;
  simReady_ = true; paused_ = true;
  cam_.zoom = world_.view.valid ? std::max(0, std::min(4, world_.view.zoom)) : 2;
  cam_.rot = world_.view.valid ? (world_.view.rotation & 3) : 0;
  renderer_.centreOnWorld(cam_, world_.view.valid ? world_.view.centreX : world_.W / 2.f, world_.view.valid ? world_.view.centreZ : world_.H / 2.f);
  scr_ = Screen::InCity;
  setTool(ToolKind::None);
  openRoot_ = openFly_ = 0;
}

void Game::loadFile(const fs::path& p) {
  Bytes b;
  if (!readFile(p, b)) { message("Cannot read " + p.filename().string()); return; }
  try {
    std::vector<std::string> warn;
    World w = loadCity(b, &warn);
    bool isTerrain = w.info.fileKind == 4;
    adopt(std::move(w));
    currentFile_ = p;
    if (isTerrain) world_.info.cityName = p.stem().string();
  } catch (const std::exception& e) {
    message(std::string("Could not load file: ") + e.what());
  }
}

void Game::saveCurrent(const std::string& name) {
  fs::path dir = resolveCI(root_, "Cities");
  if (dir.empty()) { dir = root_ / "Cities"; std::error_code ec; fs::create_directories(dir, ec); }
  fs::path p = dir / (name + ".sc3");
  if (simReady_) sim_.city().flush();
  Bytes b = saveCity(world_);
  FILE* f = std::fopen(p.string().c_str(), "wb");
  if (!f) { message("Could not write " + p.string()); return; }
  std::fwrite(b.data(), 1, b.size(), f); std::fclose(f);
  currentFile_ = p;
  message("Saved " + p.filename().string());
}

void Game::startNewCity() {
  TerrainGenOptions o;
  o.seed = uint32_t(std::rand()) * 2654435761u ^ uint32_t(nc_size_);
  o.difficulty = nc_diff_;
  World w; generateTerrain(w, nc_size_, nc_size_, o);
  w.info.cityName = nc_name_; w.info.mayorName = nc_mayor_; w.info.year = nc_year_; w.info.foundingYear = nc_year_;
  w.info.fileKind = 3;
  w.view = {nc_size_ / 2.f, nc_size_ / 2.f, 2, 0, true};
  adopt(std::move(w));
  sim_.funds = nc_diff_ == 1 ? 50000 : nc_diff_ == 2 ? 20000 : 10000;
  currentFile_.clear();
}

void Game::refreshFiles(int kind) {
  files_.clear(); fileLabels_.clear(); fileSel_ = -1; fileScroll_ = 0; fileKind_ = kind;
  const char* sub[] = {"Cities", "Cities/StarterTowns", "Cities/Terrains", "Cities/Scenarios"};
  const char* ext[] = {".sc3", ".st3", ".sct", ".snr"};
  fs::path dir = resolveCI(root_, sub[kind]);
  if (dir.empty()) return;
  for (const auto& f : listFilesCI(dir, ext[kind])) { files_.push_back({f, f.stem().string()}); fileLabels_.push_back(f.stem().string()); }
}

// ---------------------------------------------------------------- tools & menu

void Game::setTool(ToolKind k, int a, uint32_t occ) { tool_ = k; toolArg_ = a; toolOcc_ = occ; dragging_ = false; if (k == ToolKind::None) toolName_.clear(); }

void Game::dispatch(const MenuItemInfo& mi) {
  struct Z { uint32_t cls; int zone; };
  static const Z zones[] = {{0x42E88586, 1}, {0x02E88915, 2}, {0xA2E8893B, 3}, {0x02E88949, 5}, {0xA2E8895A, 6}, {0x02E88968, 7},
                            {0xC2E8898F, 9}, {0xA2E8899D, 10}, {0x02E889AA, 11}, {0xE2E889D4, 14}, {0x02E889C4, 15}, {0xC2E889B6, 17}, {0x82E88C9C, 0}};
  struct P { uint32_t cls; uint32_t occ; };
  static const P plops[] = {{0x42FF052F, 0xD2}, {0x22FF053F, 0x1F41}, {0x22FF054C, 0xD3}, {0xE2FF0555, 0xD1}, {0xC2FF0563, 0x1F94}, {0xC2FF0571, 0xD6},
                            {0xC2FF057A, 0xF5}, {0x62FF0585, 0xD4}, {0x02FF05AA, 0x0D}, {0x82FF05B7, 0xD5}, {0x22FF05CA, 0xDA}, {0x62FF05D1, 0xD7},
                            {0xC2FF05E0, 0x234B}, {0x22FF05EA, 0x234C}, {0x22FF05F3, 0x234E}, {0x02FF04FE, 0x1F96}, {0x22FF058F, 0xED},
                            {0x62FF059B, 0xE9}, {0x22FF0521, 0x3E8A}};
  std::string name = str(mi.nameTable, mi.nameId);
  if (mi.kind == "COMM") {
    for (const auto& z : zones) if (mi.target == z.cls) { setTool(ToolKind::Zone, z.zone); toolName_ = name; return; }
    for (const auto& p : plops) if (mi.target == p.cls) { setTool(ToolKind::Plop, 0, p.occ); toolName_ = name; return; }
    switch (mi.target) {
      case 0xC2B5DE77: setTool(ToolKind::Road, int(NetworkKind::Road)); toolName_ = name; return;
      case 0x42B5DE86: setTool(ToolKind::Road, int(NetworkKind::Highway)); toolName_ = name; return;
      case 0x42B5DE98: setTool(ToolKind::Road, int(NetworkKind::Rail)); toolName_ = name; return;
      case 0xC2B5DEA6: setTool(ToolKind::Road, int(NetworkKind::Subway)); toolName_ = name; return;
      case 0x42C2F8BF: setTool(ToolKind::Road, int(NetworkKind::Power)); toolName_ = name; return;
      case 0x22C2F8C5: setTool(ToolKind::Road, int(NetworkKind::Pipe)); toolName_ = name; return;
      case 0xE2ED3560:
        if (mi.arg1 == 0x62E925A2 || mi.arg1 == 0xE2E92527 || mi.arg1 == 0xC2F291A9) { pickTitle_ = name; openPicker(mi.arg1); return; }
        break;
      case 0x62E925A2: case 0xE2E92527: case 0xC2F291A9: pickTitle_ = name; openPicker(mi.target); return;
      case 0x825AD7F6: setTool(ToolKind::Terrain, 1); toolName_ = name; return;
      case 0xC25AD94F: setTool(ToolKind::Terrain, 2); toolName_ = name; return;
      case 0x825AD954: setTool(ToolKind::Terrain, 4); toolName_ = name; return;
      case 0x02E763C1: setTool(ToolKind::Terrain, 32); toolName_ = name; return;
      case 0x4253EC3F: setTool(ToolKind::Terrain, 100); toolName_ = name; return;
      case 0xE3166801: setTool(ToolKind::Demolish); toolName_ = name; return;
      case 0x025DDC4A: showBudget_ = true; paused_ = true; return;
      case 0x410438DD: if (currentFile_.empty()) { saveMode_ = true; refreshFiles(0); saveName_ = world_.info.cityName; scr_ = Screen::LoadList; } else saveCurrent(currentFile_.stem().string()); return;
      case 0x610438A4: {
        saveMode_ = false;
        refreshFiles(mi.arg1 == 0x63172B74 ? 1 : mi.arg1 == 0x23172AF2 ? 2 : mi.arg1 == 0xA42C7725 ? 3 : 0);
        scr_ = Screen::LoadList; return;
      }
      case 0xE3270FE9: scr_ = Screen::NewCity; return;
      case 0xA10438E9: scr_ = Screen::MainMenu; return;
      default: break;
    }
    setTool(ToolKind::Unavailable); toolName_ = name; message("Not implemented yet: " + name);
  } else if (mi.kind == "PMSG") {
    message("Not implemented yet: " + name);
  }
}

void Game::execute(uint32_t menuItemId) {
  auto it = menu_.items.find(menuItemId);
  if (it != menu_.items.end()) dispatch(it->second);
}

void Game::tickSim(uint32_t dtMs) {
  if (!simReady_ || paused_ || !world_.W) return;
  static const double rate[] = {0.0, 1.0, 3.0, 8.0};
  simAccum_ += dtMs / 1000.0 * rate[std::max(1, std::min(3, speed_))];
  while (simAccum_ >= 0.25) {
    simAccum_ -= 0.25;
    sim_.tickDay();   // City advances world_.info date through its clock
  }
}

// ---------------------------------------------------------------- drawing

static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void Game::frame(const Input& in, int w, int h, uint32_t dtMs) {
  W_ = w; H_ = h;
  if (audio_) audio_->update(dtMs);
  if (screen_.size() != size_t(w) * h) screen_.assign(size_t(w) * h, 0xFF302820u);
  if (scr_ == Screen::InCity && world_.W) {
    if (renderer_.width() != w || renderer_.height() != h) renderer_.resize(w, h);
    tickSim(dtMs);
    renderer_.render(cam_, ropt_);
    screen_ = renderer_.pixels();
  } else {
    std::fill(screen_.begin(), screen_.end(), 0xFF3A2C20u);
  }
  ui_.begin(&screen_, w, h, in);
  uiHit_ = false;
  switch (scr_) {
    case Screen::NeedData: drawNeedData(); break;
    case Screen::MainMenu: drawMainMenu(); break;
    case Screen::NewCity: drawNewCity(); break;
    case Screen::LoadList: drawLoadList(); break;
    case Screen::InCity: drawInCity(dtMs); break;
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

void Game::drawNeedData() {
  Rect r{W_ / 2 - 300, H_ / 2 - 150, 600, 300};
  ui_.panel(r, "Metroplex3K - game data needed");
  ui_.text(r.x + 16, r.y + 40, "Metroplex3K needs the data files of your own copy of", "main10");
  ui_.text(r.x + 16, r.y + 58, "SimCity 3000 Unlimited (e.g. the GOG release). Nothing is bundled.", "main10");
  ui_.text(r.x + 16, r.y + 90, "- Drag the installed game folder (the one containing 'Apps') onto this window,", "main10");
  ui_.text(r.x + 16, r.y + 108, "- or drag the GOG installer (setup_simcity_3000...exe) here (needs 'innoextract'),", "main10");
  ui_.text(r.x + 16, r.y + 126, "- or type the folder path below and press Enter.", "main10");
  ui_.textBox({r.x + 16, r.y + 160, r.w - 32, 24}, pathInput_, 400);
  if (ui_.focusId == 0) ui_.focusId = 1;
  if (ui_.button({r.x + 16, r.y + 200, 140, 28}, "Use this folder") || ui_.in.key[K_ENTER]) {
    if (!pathInput_.empty()) dropPath(pathInput_);
  }
  if (ui_.button({r.x + 170, r.y + 200, 200, 28}, "Explore generated terrain")) {
    TerrainGenOptions o; o.seed = uint32_t(std::rand());
    World wd; generateTerrain(wd, 128, 128, o); wd.info.cityName = "Terrain demo";
    adopt(std::move(wd));
  }
  ui_.text(r.x + 16, r.y + 250, "Environment: M3K_GAME_DIR or --game-dir DIR also work.", "main10");
}

void Game::drawMainMenu() {
  if (!menuBgTried_) {
    menuBgTried_ = true;
    // The main-menu background is the largest image of group 82B9B75C.
    int best = 0;
    for (const auto& k : data_.db().keysOfType(T_UI_IMAGE)) if (k.group == 0x82B9B75C) {
      const Image* im = icon(k.group, k.instance);
      if (im && im->w * im->h > best) { best = im->w * im->h; menuBg_ = *im; }
    }
  }
  if (!menuBg_.empty()) {
    int bx = (W_ - menuBg_.w) / 2, by = (H_ - menuBg_.h) / 2;
    ui_.blit(menuBg_, bx, by);
  }
  Rect r{W_ / 2 - 160, H_ / 2 - 150, 320, 330};
  ui_.fillAlpha(r, rgb(10, 20, 50), 0.75f);
  ui_.textCentered({r.x, r.y + 6, r.w, 30}, "Metroplex3K", "title12", rgb(255, 255, 255));
  struct B { std::string label; int act; };
  auto cap = [&](uint32_t t, uint32_t i, const char* dflt) { std::string s = str(t, i); return s.empty() ? std::string(dflt) : s; };
  B bs[] = {{cap(0x03C09AFF, 1, "Start new city"), 0}, {cap(0x03C09AFF, 2, "Load city"), 1}, {cap(0x03C09AFF, 9, "Starter town"), 2},
            {cap(0x041F2625, 1, "Load scenario"), 3}, {cap(0x03C09AFF, 13, "Real city terrain"), 4}, {cap(0x03C09AFF, 6, "Exit"), 5}};
  int y = r.y + 50;
  for (auto& b : bs) {
    if (ui_.button({r.x + 30, y, r.w - 60, 36}, b.label)) {
      switch (b.act) {
        case 0: scr_ = Screen::NewCity; break;
        case 1: saveMode_ = false; refreshFiles(0); scr_ = Screen::LoadList; break;
        case 2: saveMode_ = false; refreshFiles(1); scr_ = Screen::LoadList; break;
        case 3: saveMode_ = false; refreshFiles(3); scr_ = Screen::LoadList; break;
        case 4: saveMode_ = false; refreshFiles(2); scr_ = Screen::LoadList; break;
        default: quit_ = true; break;
      }
    }
    y += 44;
  }
  if (world_.W && ui_.button({r.x + 30, y, r.w - 60, 28}, "Back to city")) scr_ = Screen::InCity;
}

void Game::drawNewCity() {
  Rect r{W_ / 2 - 230, H_ / 2 - 210, 460, 420};
  ui_.panel(r, str(0x029541F4, 0x282).empty() ? "New City" : str(0x029541F4, 0x282));
  ui_.text(r.x + 20, r.y + 42, "City name");
  ui_.textBox({r.x + 140, r.y + 38, 290, 24}, nc_name_);
  ui_.text(r.x + 20, r.y + 76, "Mayor name");
  ui_.textBox({r.x + 140, r.y + 72, 290, 24}, nc_mayor_);
  ui_.text(r.x + 20, r.y + 116, "Difficulty");
  const char* dn[] = {"Easy", "Medium", "Hard"};
  for (int i = 0; i < 3; i++) if (ui_.button({r.x + 140 + i * 100, r.y + 110, 92, 26}, dn[i], true, nc_diff_ == i + 1)) nc_diff_ = i + 1;
  ui_.text(r.x + 20, r.y + 156, "City size");
  const int sizes[] = {64, 128, 192, 256}; const char* sn[] = {"Mini", "Small", "Medium", "Large"};
  for (int i = 0; i < 4; i++) if (ui_.button({r.x + 140 + i * 76, r.y + 150, 70, 26}, sn[i], true, nc_size_ == sizes[i])) nc_size_ = sizes[i];
  ui_.text(r.x + 20, r.y + 196, "Start year");
  const int years[] = {1900, 1950, 2000};
  for (int i = 0; i < 3; i++) if (ui_.button({r.x + 140 + i * 100, r.y + 190, 92, 26}, std::to_string(years[i]), true, nc_year_ == years[i])) nc_year_ = years[i];
  ui_.text(r.x + 20, r.y + 240, "Start funds depend on difficulty: 50,000 / 20,000 / 10,000");
  if (ui_.button({r.x + 60, r.y + 360, 140, 34}, "Create city")) startNewCity();
  if (ui_.button({r.x + 260, r.y + 360, 140, 34}, "Cancel") || ui_.in.key[K_ESC]) scr_ = (world_.W && simReady_) ? Screen::InCity : (haveData_ ? Screen::MainMenu : Screen::NeedData);
}

void Game::drawLoadList() {
  Rect r{W_ / 2 - 250, H_ / 2 - 230, 500, 460};
  const char* titles[] = {"Load city", "Starter towns", "Real city terrains", "Scenarios"};
  ui_.panel(r, saveMode_ ? "Save city" : titles[fileKind_]);
  if (ui_.list({r.x + 16, r.y + 40, r.w - 32, 320}, fileLabels_, fileSel_, fileScroll_) && saveMode_ && fileSel_ >= 0) saveName_ = fileLabels_[fileSel_];
  if (saveMode_) { ui_.text(r.x + 16, r.y + 374, "File name"); ui_.textBox({r.x + 100, r.y + 370, 300, 24}, saveName_); }
  bool ok = ui_.button({r.x + 60, r.y + 410, 140, 32}, saveMode_ ? "Save" : "Open", saveMode_ ? !saveName_.empty() : fileSel_ >= 0);
  if (ui_.in.key[K_ENTER] && ui_.focusId == 0) ok = saveMode_ ? !saveName_.empty() : fileSel_ >= 0;
  if (ok) {
    if (saveMode_) { saveCurrent(saveName_); scr_ = Screen::InCity; }
    else if (fileSel_ >= 0) loadFile(files_[fileSel_].path);
  }
  if (ui_.button({r.x + 300, r.y + 410, 140, 32}, "Cancel") || ui_.in.key[K_ESC]) scr_ = (world_.W && simReady_) ? Screen::InCity : Screen::MainMenu;
}

void Game::drawToolMenu() {
  if (!menuLoaded_) return;
  auto rootIt = menu_.menus.find(0xD1001000);
  if (rootIt == menu_.menus.end()) return;
  int bh = 28, x = 4, y = 4;
  Rect panelR{0, 0, 118, int(rootIt->second.buttons.size()) * (bh + 2) + 8};
  ui_.panel(panelR);
  if (ui_.hover(panelR)) uiHit_ = true;
  for (uint32_t bid : rootIt->second.buttons) {
    const MenuButton& b = menu_.buttons[bid];
    auto sub = menu_.menus.find(b.sub);
    std::string label = sub != menu_.menus.end() ? str(sub->second.titleTable, sub->second.titleId) : "?";
    if (label.empty()) label = "Menu";
    if (ui_.button({x, y, 110, bh}, label, true, openRoot_ == b.sub)) { openRoot_ = (openRoot_ == b.sub) ? 0 : b.sub; openFly_ = 0; }
    y += bh + 2;
  }
  if (!openRoot_) return;
  auto sm = menu_.menus.find(openRoot_);
  if (sm == menu_.menus.end()) return;
  // Sub-menu panel.
  int sx = panelR.w + 2, rows = int(sm->second.buttons.size());
  Rect sr{sx, 0, 190, rows * 28 + 8};
  ui_.panel(sr);
  if (ui_.hover(sr)) uiHit_ = true;
  int yy = 4;
  uint32_t flyOpen = 0; int flyY = 0;
  for (uint32_t bid : sm->second.buttons) {
    const MenuButton& b = menu_.buttons[bid];
    auto ii = menu_.items.find(b.item);
    std::string label = ii != menu_.items.end() ? str(ii->second.nameTable, ii->second.nameId) : std::string();
    if (label.empty() && b.sub) { auto fm = menu_.menus.find(b.sub); if (fm != menu_.menus.end()) label = str(fm->second.titleTable, fm->second.titleId); }
    if (label.empty()) label = "(item)";
    if (b.sub) label += "  >";
    Rect br{sx + 4, yy, 182, 26};
    if (ui_.button(br, label, true, openFly_ == b.sub && b.sub)) {
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
        std::string label = ii != menu_.items.end() ? str(ii->second.nameTable, ii->second.nameId) : "(item)";
        if (ui_.button({fr.x + 10, fy, 176, 26}, label)) { execute(b.item); if (tool_ != ToolKind::Unavailable) { openFly_ = 0; openRoot_ = 0; } }
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
  std::string funds = "$" + groupDigits(sim_.funds);
  ui_.text(280, H_ - bh + 8, funds, "main10", sim_.funds < 0 ? rgb(255, 0, 0) : 0);
  ui_.text(400, H_ - bh + 8, "Pop " + groupDigits(sim_.population()));
  // Speed.
  int bx = 520;
  if (ui_.button({bx, H_ - bh + 3, 44, 24}, paused_ ? "Run" : "Pause")) paused_ = !paused_;
  for (int i = 1; i <= 3; i++) if (ui_.button({bx + 48 + (i - 1) * 26, H_ - bh + 3, 24, 24}, std::to_string(i), true, !paused_ && speed_ == i)) { speed_ = i; paused_ = false; }
  // View controls.
  int vx = bx + 140;
  if (ui_.button({vx, H_ - bh + 3, 26, 24}, "+")) cam_.zoom = std::min(4, cam_.zoom + 1);
  if (ui_.button({vx + 28, H_ - bh + 3, 26, 24}, "-")) cam_.zoom = std::max(0, cam_.zoom - 1);
  auto rot = [&](int d) {
    float Wf = float(world_.W), Hf = float(world_.H), x, z;
    switch (cam_.rot & 3) { case 0: x = cam_.u; z = cam_.v; break; case 1: x = cam_.v; z = Wf - cam_.u; break; case 2: x = Wf - cam_.u; z = Hf - cam_.v; break; default: x = Hf - cam_.v; z = cam_.u; }
    cam_.rot = (cam_.rot + d + 4) & 3; renderer_.centreOnWorld(cam_, x, z);
  };
  if (ui_.button({vx + 56, H_ - bh + 3, 26, 24}, "<")) rot(-1);
  if (ui_.button({vx + 84, H_ - bh + 3, 26, 24}, ">")) rot(1);
  if (ui_.button({vx + 112, H_ - bh + 3, 40, 24}, "Grid", true, ropt_.grid)) ropt_.grid = !ropt_.grid;
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
  // Status / tool line.
  if (tool_ != ToolKind::None) ui_.text(8, H_ - bh - 16, "Tool: " + toolName_ + "   (right click or Esc to cancel)", "main10", rgb(255, 255, 255));
  if (!statusLine_.empty()) ui_.text(8, H_ - bh - 32, statusLine_, "main10", rgb(255, 255, 200));
}

void Game::drawBudget() {
  Rect r{W_ / 2 - 260, H_ / 2 - 200, 520, 400};
  ui_.panel(r, str(0x029541F4, 0x89).empty() ? "Budget" : str(0x029541F4, 0x89));
  ui_.text(r.x + 20, r.y + 40, "Funds: $" + groupDigits(sim_.funds));
  ui_.text(r.x + 20, r.y + 62, "Population: " + groupDigits(sim_.population()));
  ui_.text(r.x + 20, r.y + 84, "Estimated monthly income: $" + groupDigits(int64_t(sim_.population()) * sim_.taxRate / 10));
  ui_.text(r.x + 20, r.y + 106, "Estimated monthly road upkeep: $" + groupDigits(sim_.roadCount() / 2));
  ui_.text(r.x + 20, r.y + 140, "Tax rate: " + std::to_string(sim_.taxRate) + "%");
  ui_.slider({r.x + 160, r.y + 138, 260, 18}, sim_.taxRate, 0, 22);
  if (ui_.button({r.x + r.w - 110, r.y + r.h - 44, 90, 30}, "Close") || ui_.in.key[K_ESC]) { showBudget_ = false; paused_ = false; }
  uiHit_ = true;
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
}

void Game::openPicker(uint32_t category) {
  pickItems_.clear(); pickLabels_.clear(); pickSel_ = -1; pickScroll_ = 0;
  // Category by occupant agent type (spec 13): power plants 12111-12118 + 12853 waste-to-energy,
  // water structures 128xx (pump 12810, treatment 1282x, desalination 12830, tower 12840), garbage 129xx.
  auto match = [&](uint32_t at) {
    if (category == 0x62E925A2) return (at >= 12111 && at <= 12118) || at == 12853;
    if (category == 0xE2E92527) return at >= 12800 && at <= 12849;
    if (category == 0xC2F291A9) return at >= 12900 && at <= 12999;
    return false;
  };
  if (haveData_) {
    for (const ResKey& k : data_.db().keysOfType(T_OCC_ATTR)) {
      const OccAttr* a = data_.occupant(k.instance);
      if (!a || !a->valid || !match(a->agentType)) continue;
      pickItems_.push_back({k.instance});
      std::string nm = str(0xA1096A4F, k.instance);
      pickLabels_.push_back(nm.empty() ? (a->designName.empty() ? "Building " + std::to_string(k.instance) : a->designName) : nm);
    }
  }
  if (pickItems_.empty()) { message("No buildings found for this category."); return; }
  showPicker_ = true;
}

void Game::drawPicker() {
  Rect r{W_ / 2 - 200, H_ / 2 - 220, 400, 440};
  ui_.panel(r, pickTitle_.empty() ? "Buildings" : pickTitle_);
  ui_.list({r.x + 16, r.y + 40, r.w - 32, 320}, pickLabels_, pickSel_, pickScroll_);
  if (ui_.button({r.x + 60, r.y + r.h - 50, 120, 30}, "Select", pickSel_ >= 0)) {
    setTool(ToolKind::Plop, 0, pickItems_[size_t(pickSel_)].occ);
    toolName_ = pickLabels_[size_t(pickSel_)];
    showPicker_ = false;
  }
  if (ui_.button({r.x + r.w - 180, r.y + r.h - 50, 120, 30}, "Cancel") || ui_.in.key[K_ESC]) showPicker_ = false;
  uiHit_ = true;
}

void Game::drawQuery() {
  Rect r{W_ - 330, 10, 320, 150};
  ui_.panel(r, "Query");
  ui_.text(r.x + 12, r.y + 34, queryText_);
  if (ui_.button({r.x + r.w - 70, r.y + r.h - 34, 60, 24}, "Close")) showQuery_ = false;
  if (ui_.hover(r)) uiHit_ = true;
}

void Game::drawWindows() {
  if (showBudget_) drawBudget();
  if (showQuery_) drawQuery();
  if (showPicker_) drawPicker();
}

void Game::handleMap() {
  const Input& in = ui_.in;
  if (!world_.W) return;
  if (in.wheel && !uiHit_) cam_.zoom = std::max(0, std::min(4, cam_.zoom + (in.wheel > 0 ? 1 : -1)));
  int tx, tz;
  bool onTile = !uiHit_ && renderer_.pickGround(cam_, in.mx, in.my, tx, tz);
  if (in.rightPressed && !uiHit_) { if (tool_ != ToolKind::None) setTool(ToolKind::None); panning_ = true; }
  if (!in.down && !ui_.in.rightPressed) panning_ = panning_ && false;
  if (tool_ == ToolKind::None || tool_ == ToolKind::Unavailable) { statusLine_.clear(); }
  if (onTile) {
    if (tool_ == ToolKind::Terrain) {
      if (in.pressed) { dragging_ = true; ax_ = tx; az_ = tz; }
    } else if (tool_ == ToolKind::Zone || tool_ == ToolKind::Road || tool_ == ToolKind::Demolish || tool_ == ToolKind::Plop || tool_ == ToolKind::Query) {
      if (in.pressed) { dragging_ = true; ax_ = tx; az_ = tz; }
    } else if (in.pressed) {
      // Default pointer acts as query.
      tool_ = ToolKind::Query; dragging_ = true; ax_ = tx; az_ = tz;
    }
  }
  // Terrain tools: press-and-hold repeats raise/lower/water/trees; level applies on release.
  if (onTile && dragging_ && in.down && tool_ == ToolKind::Terrain && toolArg_ != 4 && terra_) {
    static uint32_t tick = 0;
    if ((++tick & 3) == 0 || in.pressed) {
      if (toolArg_ == 100) {
        FloraRng rng = floraRngFromLcg(floraLcg_);
        uint8_t cell = randomTreeCell(3, rng);
        TreeTool tt; TreePlan p = planTree(world_, tx, tz, cell, tt);
        if (p.legal && sim_.funds >= p.cost) { commitTree(world_, p); sim_.funds -= p.cost; }
      } else {
        TerraPlan p = toolArg_ == 1 ? terra_->planRaise(tx, tz) : toolArg_ == 2 ? terra_->planLower(tx, tz) : terra_->planWater(tx, tz);
        terra_->apply(p, sim_.funds);
        renderer_.invalidateShade();
      }
    }
  }
  if (dragging_ && in.released) {
    dragging_ = false;
    if (onTile) {
      int x0 = ax_, z0 = az_, x1 = tx, z1 = tz;
      switch (tool_) {
        case ToolKind::Terrain:
          if (toolArg_ == 4 && terra_) { TerraPlan p = terra_->planLevel(x0, z0, x1, z1); terra_->apply(p, sim_.funds); renderer_.invalidateShade(); }
          break;
        case ToolKind::Zone: sim_.paintZoneType(x0, z0, x1, z1, toolArg_); break;
        case ToolKind::Road: {
          std::vector<std::pair<int, int>> path;
          int x = x0, z = z0;
          while (x != x1) { path.push_back({x, z}); x += x1 > x ? 1 : -1; }
          while (z != z1) { path.push_back({x, z}); z += z1 > z ? 1 : -1; }
          path.push_back({x1, z1});
          sim_.buildNetwork(NetworkKind(toolArg_ ? toolArg_ : 1), path); break;
        }
        case ToolKind::Demolish: sim_.bulldoze(x0, z0, x1, z1); break;
        case ToolKind::Plop: if (!sim_.plop(toolOcc_, x1, z1, 100)) message(sim_.city().lastFailNoMoney ? "Insufficient funds." : "Cannot build here."); break;
        case ToolKind::Query: {
          int oi = sim_.occupantAt(tx, tz);
          char b[256];
          std::snprintf(b, sizeof b, "Tile (%d, %d)  altitude %d  zone %d%s", tx, tz, world_.altAt(tx, tz), world_.zone[world_.tIdx(tx, tz)], sim_.isRoadTile(tx, tz) ? "  road" : "");
          queryText_ = b;
          if (oi >= 0) {
            const Occupant& o = world_.occupants[oi];
            std::string nm = haveData_ ? str(0xA1096A4F, o.occId) : std::string();
            queryText_ += "\n" + (nm.empty() ? "Building " + std::to_string(o.occId) : nm) + "  state " + std::to_string(o.state);
          }
          showQuery_ = true; break;
        }
        default: break;
      }
      if (tool_ == ToolKind::Query && toolName_.empty()) tool_ = ToolKind::None;
    }
  }
  // Drag preview.
  if (onTile && dragging_ && (tool_ == ToolKind::Zone || tool_ == ToolKind::Demolish)) {
    char b[64]; std::snprintf(b, sizeof b, "%d x %d tiles", std::abs(tx - ax_) + 1, std::abs(tz - az_) + 1); statusLine_ = b;
  } else if (onTile && tool_ != ToolKind::None) {
    char b[64]; std::snprintf(b, sizeof b, "(%d, %d)", tx, tz); statusLine_ = b;
  }
}

void Game::drawInCity(uint32_t) {
  // Keyboard.
  const Input& in = ui_.in;
  bool typing = ui_.focusId != 0;
  if (!typing) {
    float s = 32.f;
    float dx = (in.held[1] ? s : 0) - (in.held[0] ? s : 0), dy = (in.held[3] ? s : 0) - (in.held[2] ? s : 0);
    if (dx || dy) {
      float Wt = 8.f * float(1 << cam_.zoom), Ht = Wt / 2;
      float du = (dy / (Ht / 2) - dx / (Wt / 2)) / 2, dv = (dy / (Ht / 2) + dx / (Wt / 2)) / 2;
      cam_.u = std::max(0.f, std::min(float(world_.W), cam_.u + du)); cam_.v = std::max(0.f, std::min(float(world_.H), cam_.v + dv));
    }
    for (char c : in.text) {
      if (c == ' ') paused_ = !paused_;
      else if (c == 'g') ropt_.grid = !ropt_.grid;
      else if (c == '+' || c == '=') cam_.zoom = std::min(4, cam_.zoom + 1);
      else if (c == '-') cam_.zoom = std::max(0, cam_.zoom - 1);
    }
    if (in.key[K_ESC]) { if (tool_ != ToolKind::None) setTool(ToolKind::None); else if (openRoot_) openRoot_ = 0; else scr_ = Screen::MainMenu; }
  }
  // Mouse drag pan with middle button is host-provided via rightPressed/held; simple: right drag handled by host moving held arrows.
  drawToolMenu();
  drawHud();
  drawWindows();
  if (!showBudget_) handleMap();
}

}  // namespace m3k
