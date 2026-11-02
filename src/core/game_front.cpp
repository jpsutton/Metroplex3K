// Game shell front end: intro movie, main menu, new city, terrain editor, load/save, credits, settings.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/game.h"
#include "core/game_scenario.h"
#include "core/gamedir.h"
#include "core/newcity_template.h"
#include "core/peicon.h"
#include "core/skin.h"
#include "core/terrain_gen.h"

namespace m3k {

// ---------------------------------------------------------------- settings persistence

void Game::loadSettings() {
  general_ = GeneralSettings::defaults(5);
  if (persist_) {
    prefsPath_ = defaultPrefsDir() / "prefs.cfg";
    prefs_ = Preferences();
    prefs_.load(prefsPath_);
    general_.musicOn = prefs_.getBool("music.on", general_.musicOn);
    general_.sfxOn = prefs_.getBool("sfx.on", general_.sfxOn);
    general_.ambienceOn = prefs_.getBool("ambient.on", general_.ambienceOn);
    general_.musicVolume = int(prefs_.getInt("music.volume", general_.musicVolume));
    general_.sfxVolume = int(prefs_.getInt("sfx.volume", general_.sfxVolume));
    general_.ambienceVolume = int(prefs_.getInt("ambient.volume", general_.ambienceVolume));
    general_.repairVolumes();
    nc_name_ = prefs_.get("newcity.name", nc_name_);
    nc_mayor_ = prefs_.get("newcity.mayor", nc_mayor_);
    nc_size_ = int(prefs_.getInt("newcity.size", nc_size_));
  }
  prefsState_ = prefsFromSettings(general_, prefs_);
  showTip_ = persist_ && prefsState_.showTips && false;
}

void Game::saveSettings() {
  prefsToSettings(prefsState_, general_, prefs_);
  prefs_.setBool("music.on", general_.musicOn); prefs_.setBool("sfx.on", general_.sfxOn); prefs_.setBool("ambient.on", general_.ambienceOn);
  prefs_.setInt("music.volume", general_.musicVolume); prefs_.setInt("sfx.volume", general_.sfxVolume); prefs_.setInt("ambient.volume", general_.ambienceVolume);
  prefs_.set("newcity.name", nc_name_); prefs_.set("newcity.mayor", nc_mayor_); prefs_.setInt("newcity.size", nc_size_);
  if (persist_ && !prefsPath_.empty()) {
    std::error_code ec; fs::create_directories(prefsPath_.parent_path(), ec);
    prefs_.save(prefsPath_);
  }
}

void Game::applySettingsToAudio() {
  if (!audio_) return;
  audio_->setCategoryVolume(AudioCategory::Music, general_.musicVolume);
  audio_->setCategoryVolume(AudioCategory::Sfx, general_.sfxVolume);
  audio_->setCategoryVolume(AudioCategory::Ambience, general_.ambienceVolume);
  audio_->setCategoryEnabled(AudioCategory::Music, general_.musicOn);
  audio_->setCategoryEnabled(AudioCategory::Sfx, general_.sfxOn);
  audio_->setCategoryEnabled(AudioCategory::Ambience, general_.ambienceOn);
  audio_->setMusicEnabled(general_.musicOn);
  if (prefsState_.tracks.empty()) {
    for (const auto& m : audio_->ini().music) if (m.inSongList) prefsState_.tracks.push_back({m.name.empty() ? m.file : m.name, prefs_.getBool("track." + std::to_string(m.id), true)});
  }
  size_t i = 0;
  for (const auto& m : audio_->ini().music) if (m.inSongList && i < prefsState_.tracks.size()) audio_->setSongEnabled(m.id, prefsState_.tracks[i++].second);
}

// ---------------------------------------------------------------- schemes (spec 09 3.5)

// SC3CityScheme.ini, in file order (the order the selector icons appear in, observed). Value fields used: (2) selector
// icon instance in group 85535958; the preview sprite id in GAME_UI.DAT is field 6 for landscapes, field 4 otherwise.
void Game::loadSchemes() {
  schemeIdsL_.assign(1, 0); schemeIdsF_.assign(1, 0); schemeIdsB_.assign(1, 0);
  schemeIconL_.assign(1, 0); schemeIconF_.assign(1, 0); schemeIconB_.assign(1, 0);
  schemePrevL_.assign(1, 0); schemePrevF_.assign(1, 0); schemePrevB_.assign(1, 0);
  schemesLoaded_ = true;
  if (!haveData_) return;
  IniFile ini;
  if (!data_.ini().load("SC3CityScheme.ini", ini)) return;
  auto fill = [&](const char* sec, std::vector<uint32_t>& ids, std::vector<uint32_t>& icons, std::vector<uint32_t>& prev, size_t prevField) {
    for (const auto& kv : ini.section(sec)) {
      uint32_t id = parseIdNumber(kv.first);
      std::vector<uint32_t> f;
      for (size_t p = 0; p <= kv.second.size();) {
        size_t e = kv.second.find(',', p); if (e == std::string::npos) e = kv.second.size();
        f.push_back(parseIdNumber(kv.second.substr(p, e - p))); p = e + 1;
      }
      size_t at = size_t(std::find(ids.begin(), ids.end(), id) - ids.begin());
      if (at == ids.size()) { ids.push_back(id); icons.push_back(0); prev.push_back(0); }
      if (f.size() > 1) icons[at] = f[1];
      if (f.size() > prevField) prev[at] = f[prevField];
    }
  };
  fill("LandScapes", schemeIdsL_, schemeIconL_, schemePrevL_, 5);
  fill("FloraSets", schemeIdsF_, schemeIconF_, schemePrevF_, 3);
  fill("BuildingSets", schemeIdsB_, schemeIconB_, schemePrevB_, 3);
}


// ---------------------------------------------------------------- world adoption

void Game::attachWorld() {
  if (haveData_) {
    data_.unmountSets();
    if (world_.scheme.buildingSet) data_.mountBuildingSet(world_.scheme.buildingSet);
    if (world_.scheme.floraSet) data_.mountFloraSet(world_.scheme.floraSet);
  }
  renderer_.setWorld(&world_, haveData_ ? &data_ : nullptr);
  terra_ = std::make_unique<Terraformer>(world_);
  IniFile bl;
  if (haveData_ && data_.ini().load("SC3BudgetLayer.ini", bl)) terra_->setCosts(TerraformCosts::fromIni(bl));
  cam_.zoom = world_.view.valid ? std::max(0, std::min(4, world_.view.zoom)) : 2;
  cam_.rot = world_.view.valid ? (world_.view.rotation & 3) : 0;
  renderer_.centreOnWorld(cam_, world_.view.valid ? world_.view.centreX : world_.W / 2.f, world_.view.valid ? world_.view.centreZ : world_.H / 2.f);
  renderer_.invalidateShade();
  scene_ = RenderScene();
  hoverValid_ = false;
}

void Game::enterCity() {
  // Sim.init rebuilds the City; windows referencing it are closed first.
  closeModelessWindows();
  ticker_ = TickerWidget();   // every new, loaded or scenario city starts with an empty strip
  streetHost_.reset(); street_.reset();
  sim_.init(&world_, haveData_ ? &data_ : nullptr, &cityCfg_, 12345u + uint32_t(world_.W));
  sim_.city().onSound = [this](int id) { sound(id); };
  sim_.city().onMessage = [this](const std::string& m) { message(m); };
  // Building news (spec 22 s4.7.1): a ticker item for the listed agent types and the landmark family.
  sim_.city().onBuildingAdded = [this](uint32_t type, int countAfter, int x, int z) {
    if (!managersReady_) return;
    const bool landmark = sim_.city().agentHierarchy().isAncestorOrSelf(type, 18000);
    NewsKey k = buildingNews(type, countAfter, landmark, newsRng_);
    if (!k.valid()) return;
    WinEnv E = env();
    std::string text = E.text(k.table, k.id, "");
    if (text.empty()) return;
    cal::Date d = sim_.city().clock.date();
    news_.post(k.table, k.id, tickerStamp(d.month, d.day, d.year), true, x, z, &text);
  };
  simReady_ = true; paused_ = true; dirty_ = false; simAccum_ = 0;
  speed_ = std::max(1, std::min(4, world_.info.gameSpeed));
  showPicker_ = false;
  cheats_.resetSession();
  initManagers();
  setupStreetSim();
  scr_ = Screen::InCity;
  setTool(ToolKind::None);
  openRoot_ = openFly_ = 0;
  lastMonth_ = sim_.city().clock.date().month;
  // City start / load ticker items (spec 22 4.7.2); mode 3 is used for every ordinary city [OPEN: mode names].
  if (haveData_) for (const NewsKey& k : cityStartNews(3, 0, rng_)) postNews(k.table, k.id);
}

void Game::adopt(World&& w) {
  world_ = std::move(w);
  scen_.reset();
  attachWorld();
  enterCity();
}

void Game::loadFile(const fs::path& p) {
  Bytes b;
  if (!readFile(p, b)) { message("Cannot read " + p.filename().string()); return; }
  try {
    std::vector<std::string> warn;
    World w = loadCity(b, &warn);
    bool isTerrain = w.info.fileKind == 4;
    std::unique_ptr<ScenarioController> sc;
    {
      auto s = std::make_unique<ScenarioController>();
      std::string err;
      std::string ext = toLower(p.extension().string());
      if (!isTerrain && (ext == ".snr" || fileKind_ == 3) && s->load(b, err)) sc = std::move(s);
      else if (!isTerrain && s->load(b, err)) sc = std::move(s);
    }
    world_ = std::move(w);
    attachWorld();
    userSet_ = UserBuildingSet();
    readUserBuildingSet(b, userSet_);
    if (isTerrain) {
      world_.info.cityName = p.stem().string();
      // Real-city terrains go through the New City dialog like a generated terrain.
      beginTerrainEditFrom(true);
      currentFile_.clear();
      return;
    }
    scen_ = std::move(sc);
    // City-info flags: flagA = Disasters, flagB = Auto Budget (world.h).
    disastersOn_ = world_.info.flagA != 0; autoBudget_ = world_.info.flagB != 0;
    disasters_.setDisastersEnabled(disastersOn_);
    enterCity();
    currentFile_ = p;
    if (scen_) startScenarioHooks();
    else if (p.extension() == ".st3" || p.extension() == ".ST3") currentFile_.clear();
    ticker_.items.clear();
    ticker_ = TickerWidget();
  } catch (const std::exception& e) {
    message(std::string("Could not load file: ") + e.what());
  }
}

bool Game::loadScenarioFile(const fs::path& p) { fileKind_ = 3; loadFile(p); return scenarioActive(); }

void Game::saveCurrent(const std::string& name, const fs::path& into) {
  fs::path dir = into.empty() ? resolveCI(root_, "Cities") : into;
  if (dir.empty()) { dir = (root_.empty() ? configDir() : root_) / "Cities"; std::error_code ec; fs::create_directories(dir, ec); }
  fs::path p = dir / (name + ".sc3");
  if (simReady_) sim_.city().flush();
  Bytes b = saveCity(world_);
  if (scen_) b = scen_->mergeState(b);
  { std::vector<uint32_t> log = cheats_.log(); if (!log.empty()) b = writeCheatLog(b, log); }
  if (!userSet_.pairs.empty()) b = writeUserBuildingSet(b, userSet_);
  FILE* f = std::fopen(p.string().c_str(), "wb");
  if (!f) { message("Could not write " + p.string()); return; }
  std::fwrite(b.data(), 1, b.size(), f); std::fclose(f);
  currentFile_ = p; dirty_ = false;
  message("Saved " + p.filename().string());
}

// Prompt texts observed on the original (2.0.949): Exit -> "Now leaving" (225872FE:55 / 015E410C:16, OK/Cancel),
// then for a modified city "Do you want to save the game?" (015E410C:1F, Yes/No). New city asks 015E410C:12 and
// Load asks 029541F4:250, both titled 015E410C:6 "Save", Yes/No. All show the warning icon.
void Game::requestLeave(int action) {
  auto cap = [&](uint32_t t, uint32_t i, const char* d) { std::string s = str(t, i); return s.empty() ? std::string(d) : s; };
  if (action == kLeaveQuit) {
    msgBox_.open(cap(0x225872FE, 0x55, "Exit"), cap(0x015E410C, 0x16, "Now leaving Sim City 3000."), 1);
    msgBoxPurpose_ = 1; msgBoxAction_ = action;
    return;
  }
  askSaveBeforeLeave(action);
}

void Game::askSaveBeforeLeave(int action) {
  if (!(dirty_ && world_.W && simReady_)) { doLeave(action); return; }
  auto cap = [&](uint32_t t, uint32_t i, const char* d) { std::string s = str(t, i); return s.empty() ? std::string(d) : s; };
  if (action == kLeaveQuit) msgBox_.open(cap(0x225872FE, 0x55, "Exit"), cap(0x015E410C, 0x1F, "Do you want to save the game?"), 3);
  else if (action == kLeaveNew) msgBox_.open(cap(0x015E410C, 6, "Save"), cap(0x015E410C, 0x12, "Do you want to save your city?"), 3);
  else msgBox_.open(cap(0x015E410C, 6, "Save"), cap(0x029541F4, 0x250, "Would you like to save this city?"), 3);
  msgBoxPurpose_ = 2; msgBoxAction_ = action;
}

void Game::doLeave(int action) {
  pendingAfterLeave_ = 0;
  if (action == kLeaveQuit) quit_ = true;
  else if (action == kLeaveNew) scr_ = Screen::NewCity;
  else if (action >= kLeaveLoad) { saveMode_ = false; refreshFiles(action - kLeaveLoad); scr_ = Screen::LoadList; }
}

void Game::onMessageResult(MsgButton b) {
  int purpose = msgBoxPurpose_, action = msgBoxAction_;
  msgBoxPurpose_ = 0; msgBoxAction_ = 0;
  if (purpose == 1) { if (b == MsgButton::OK) askSaveBeforeLeave(action); return; }
  if (purpose == 3 || purpose == 4 || purpose == 5) {
    // Spec 12 §6.4 / §8: bridge/tunnel decline cancels the whole drag; connection decline still builds the drag.
    City& city = sim_.city();
    pendingNetAsk_ = false;
    auto path = std::move(pendingNetPath_);
    NetworkKind nk = pendingNetKind_;
    pendingNetPath_.clear();
    pendingNetAskKind_ = City::NetworkConfirmKind::None;
    if (purpose == 4 || purpose == 5) {
      if (b == MsgButton::Yes && !path.empty()) { sim_.buildNetwork(nk, path); dirty_ = true; }
      return;
    }
    city.connectionConfirmOverride = (b == MsgButton::Yes);
    if (!path.empty()) { sim_.buildNetwork(nk, path); dirty_ = true; }
    city.connectionConfirmOverride.reset();
    return;
  }
  if (purpose != 2) return;
  if (b != MsgButton::Yes) { doLeave(action); return; }
  if (!currentFile_.empty()) { saveCurrent(currentFile_.stem().string(), currentFile_.parent_path()); if (!dirty_) doLeave(action); return; }   // failed save: stay
  // Unnamed city: Save As first; the action runs once it has saved (cancel drops it).
  pendingAfterLeave_ = action;
  openSaveAs();
}

// ---------------------------------------------------------------- new city / terrain editor

void Game::startNewCity() {
  edSeed_ = uint32_t(std::rand()) * 2654435761u ^ uint32_t(nc_size_);
  beginTerrainEdit();
}

void Game::beginTerrainEdit() {
  TerrainGenOptions o;
  o.seed = edSeed_;
  o.difficulty = nc_diff_;
  o.hills = edHills_; o.water = edWater_; o.trees = edTrees_; o.flags = edFlags_;
  World w; generateTerrain(w, nc_size_, nc_size_, o);
  w.info.cityName = nc_name_; w.info.mayorName = nc_mayor_; w.info.year = nc_year_; w.info.foundingYear = nc_year_;
  w.info.fileKind = 3;
  w.scheme.landscape = schemeIdsL_.empty() ? 0 : schemeIdsL_[size_t(nc_landscape_) % schemeIdsL_.size()];
  w.scheme.floraSet = schemeIdsF_.empty() ? 0 : schemeIdsF_[size_t(nc_flora_) % schemeIdsF_.size()];
  w.scheme.buildingSet = schemeIdsB_.empty() ? 0 : schemeIdsB_[size_t(nc_buildings_) % schemeIdsB_.size()];
  w.view = {nc_size_ / 2.f, nc_size_ / 2.f, 1, 0, true};
  world_ = std::move(w);
  scen_.reset();
  edFunds_ = 1000000000;
  attachWorld();
  scr_ = Screen::TerrainEdit;
  setTool(ToolKind::None);
}

// Terrain loaded from a .sct: the editor accepts it as is.
void Game::beginTerrainEditFrom(bool) {
  nc_size_ = world_.W;
  edFunds_ = 1000000000;
  scr_ = Screen::TerrainEdit;
  setTool(ToolKind::None);
}

void Game::acceptTerrain() {
  world_.info.cityName = nc_name_; world_.info.mayorName = nc_mayor_;
  world_.info.year = nc_year_; world_.info.foundingYear = nc_year_; world_.info.dayOfYear = 1;
  world_.info.fileKind = 3;
  world_.view = {cam_.u, cam_.v, cam_.zoom, cam_.rot, false};
  world_.view.valid = false;
  // Observed new-city records (spec 30 s4, s5): the City parses the ones it models, the rest are saved verbatim.
  // Records a loaded terrain file already carries are kept.
  if (!world_.raw) world_.raw = std::make_shared<RawRecordStore>();
  seedNewCityRecords(*world_.raw, world_.W, world_.H);
  attachWorld();
  enterCity();
  // World record: only its last word differs between new cities, a fresh 32-bit value each (spec 30 s7).
  sim_.city().valves.world.last = (uint32_t(std::rand()) << 16) ^ uint32_t(std::rand()) ^ edSeed_;
  Budget& b = sim_.city().budget;
  int64_t start = Budget::startingFunds(nc_diff_);
  if (nc_diff_ == 3) { b.newCity(0); b.issueBond(10000, 1, nc_year_); }
  else b.newCity(start);
  disastersOn_ = nc_disasters_; autoBudget_ = nc_autoBudget_;
  world_.info.flagA = nc_disasters_ ? 1 : 0; world_.info.flagB = nc_autoBudget_ ? 1 : 0;
  world_.info.gameSpeed = 1;
  disasters_.setDisastersEnabled(disastersOn_);
  b.showBudgetOnlyInDebt = true;  // spec 19 7 [OBSERVED]: no year-end budget window while funds are positive
  currentFile_.clear();
  saveSettings();
  postNews(kTblNewsTriggered, 1);
}

void Game::refreshFiles(int kind) {
  fileKind_ = kind; confirmOverwrite_ = false; fileComboOpen_ = false;
  const char* sub[] = {"Cities", "Cities/StarterTowns", "Cities/Terrains", "Cities/Scenarios"};
  fs::path dir = resolveCI(root_, sub[kind]);
  openFileDir(dir);
}

// File dialog folder (spec 09 7a.5, observed): parent-folder row, sub-folders, then the files of the dialog's
// kind, each block sorted case-insensitively.
void Game::openFileDir(const fs::path& dir) {
  files_.clear(); fileLabels_.clear(); fileSel_ = -1; fileScroll_ = 0; previewFor_ = -2; fileLastClickRow_ = -1;
  fileDir_ = dir;
  if (dir.empty()) return;
  const char* ext[] = {".sc3", ".st3", ".sct", ".snr"};
  if (dir.has_parent_path() && dir.parent_path() != dir) files_.push_back({dir.parent_path(), "", 0});
  std::vector<fs::path> dirs;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir, ec)) if (e.is_directory(ec)) dirs.push_back(e.path());
  std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return toLower(a.filename().string()) < toLower(b.filename().string()); });
  for (const auto& d : dirs) files_.push_back({d, d.filename().string(), 1});
  for (const auto& f : listFilesCI(dir, ext[fileKind_])) files_.push_back({f, f.stem().string(), 2});
  for (const auto& f : files_) fileLabels_.push_back(f.label);
  // The first file starts selected; Save As keeps its own default name in the field.
  for (int i = 0; i < int(files_.size()); i++)
    if (files_[size_t(i)].type == 2) { fileSel_ = i; if (!saveMode_) saveName_ = files_[size_t(i)].label; break; }
}

// Preview-record thumbnail (one pixel per tile, column = x, row = H-1-z) turned into the view diamond (rotation 0),
// with its sunken outline baked in. Fitted on the original's previews of shipped files: in dialog coordinates the fill
// is |x-114| + |y-139| <= 96, the outline the next 2 px (dark on the left edges, light on the right ones), and a pixel
// samples the thumbnail at 195 px per map width around (114.5, 139.5). The image covers dialog x 16..212, y 41..237.
static Image previewDiamond(const Image& thumb) {
  const int S = 197, N = std::max(thumb.w, thumb.h);
  const float s = float(N) / 195.f;
  Image dm;
  dm.w = dm.h = S; dm.px.assign(size_t(S) * S, 0);
  for (int j = 0; j < S; j++)
    for (int i = 0; i < S; i++) {
      int x = 16 + i, y = 41 + j, d = std::abs(x - 114) + std::abs(y - 139);
      uint32_t& out = dm.px[size_t(j) * S + i];
      if (d > 98) continue;
      if (d > 96) { out = x < 114 ? rgb(0x7B, 0x8A, 0xFF) : rgb(0xFF, 0xF3, 0xFF); continue; }
      float dx = float(x) - 114.5f, dy = float(y) - 139.5f;
      int u = int(std::floor((dy - dx) * s + float(N) * 0.5f)), v = int(std::floor((dy + dx) * s + float(N) * 0.5f));
      int col = std::max(0, std::min(thumb.w - 1, u)), row = std::max(0, std::min(thumb.h - 1, thumb.h - 1 - v));
      out = thumb.px[size_t(row) * thumb.w + col];
    }
  return dm;
}

// Save As: the edited mayor / city names go into the city before it is written.
void Game::applySaveNames() {
  if (!saveMayor_.empty()) world_.info.mayorName = saveMayor_;
  if (!saveCity_.empty()) world_.info.cityName = saveCity_;
}

// Save As shows the city being saved: its live stats and, when it came from a file, that file's map preview.
void Game::openSaveAs() {
  saveMode_ = true;
  refreshFiles(0);
  // File name: the city's own file when it has one (observed: "Berlin, Germany" for city "Berlin"), else the city name.
  saveName_ = currentFile_.empty() ? world_.info.cityName : currentFile_.stem().string();
  saveMayor_ = world_.info.mayorName; saveCity_ = world_.info.cityName;
  savePreview_ = FilePreview();
  savePreview_.ok = savePreview_.hasStats = true;
  savePreview_.population = sim_.population(); savePreview_.funds = sim_.funds;
  cal::Date d = sim_.city().clock.date();
  savePreview_.date = std::to_string(d.month) + "/" + std::to_string(d.day) + "/" + std::to_string(d.year);
  Bytes b; CityPreview cp;
  if (!currentFile_.empty() && readFile(currentFile_, b) && readCityPreview(b, cp) && !cp.image.empty()) savePreview_.diamond = previewDiamond(cp.image);
  scr_ = Screen::LoadList;
}

void Game::updatePreview() {
  if (fileSel_ == previewFor_) return;
  previewFor_ = fileSel_;
  preview_ = FilePreview();
  if (fileSel_ < 0 || fileSel_ >= int(files_.size()) || files_[size_t(fileSel_)].type != 2) return;
  Bytes b;
  if (!readFile(files_[size_t(fileSel_)].path, b)) return;
  try {
    CityPreview cp;
    if (readCityPreview(b, cp)) {
      preview_.ok = true;
      preview_.hasStats = cp.hasStats;
      preview_.population = cp.population; preview_.funds = cp.funds;
      preview_.name = cp.cityName; preview_.mayor = cp.mayorName;
      if (cp.hasStats) {
        cal::Date d = cal::fromJdn(cal::jdnFromDayOfYear(int(cp.dayOfYear), int(cp.year)));
        preview_.date = std::to_string(d.month) + "/" + std::to_string(d.day) + "/" + std::to_string(d.year);
      }
      // Starter towns show their 162x152 picture as is; everything else the view diamond.
      if (!cp.image.empty()) preview_.diamond = fileKind_ == 1 ? cp.image : previewDiamond(cp.image);
    }
    if (fileKind_ == 3) {
      ScenarioInfo si;
      auto tf = [this](uint32_t g, uint32_t i) { return str(g, i); };
      if (ScenarioController::peek(b, tf, si)) {
        preview_.ok = true; preview_.name = si.title.empty() ? preview_.name : si.title;
        preview_.mayor = si.shortAuthor; preview_.extra = si.description;
        preview_.diamond = Image();   // the chooser shows the scenario picture, not the map
        if (!si.picture.empty()) { try { preview_.diamond = decodeBmp(si.picture.data(), si.picture.size()); } catch (const FormatError&) {} }
      }
    }
  } catch (const std::exception&) {}
}

// ---------------------------------------------------------------- screens

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

void Game::startIntro() {
  introTried_ = true;
  movie_.reset(); intro_.reset();
  if (!haveData_ || !introEnabled_) { finishIntro(); return; }
  fs::path p = resolveCI(data_.appsDir(), "Res/UI/Shared/Movies/INTRO.TGQ");
  Bytes b;
  if (p.empty() || !readFile(p, b)) { finishIntro(); return; }
  auto m = std::make_unique<TgqMovie>();
  if (!m->parse(std::vector<uint8_t>(b.begin(), b.end()))) { finishIntro(); return; }
  movie_ = std::move(m);
  intro_ = std::make_unique<IntroPlayer>(*movie_);
  intro_->start();
  introMs_ = 0;
  if (intro_->state() == IntroState::Finished) { finishIntro(); return; }
  if (audio_) audio_->setMusicHeld(true);  // movie carries its own soundtrack
  if (audio_ && general_.musicOn) {
    float vol = introVolume(general_.musicVolume);
    auto pcm = std::make_shared<PcmBuffer>();
    if (vol > 0 && movie_->decodeAudio(*pcm) && !pcm->samples.empty()) {
      VoiceParams vp; vp.category = AudioCategory::Music; vp.volume = int(vol * 1024);
      introVoice_ = audio_->mixer().play(pcm, vp);
    }
  }
}

void Game::finishIntro() {
  if (audio_ && introVoice_) audio_->mixer().stop(introVoice_);
  if (audio_) audio_->setMusicHeld(false);
  introVoice_ = 0;
  intro_.reset(); movie_.reset();
  if (scr_ == Screen::Intro) scr_ = haveData_ ? Screen::MainMenu : Screen::NeedData;
}

void Game::drawIntro(uint32_t dtMs) {
  if (!introTried_) startIntro();
  if (scr_ != Screen::Intro || !intro_) { if (scr_ == Screen::Intro) finishIntro(); return; }
  std::fill(screen_.begin(), screen_.end(), 0xFF000000u);
  if (ui_.in.key[K_ESC] || ui_.in.key[K_ENTER] || ui_.in.pressed || ui_.in.rightPressed) { intro_->skip(); finishIntro(); return; }
  introMs_ += dtMs;
  intro_->update(introMs_);
  if (intro_->state() == IntroState::Finished) { finishIntro(); return; }
  const std::vector<uint32_t>* f = intro_->frameAt(introMs_);
  if (f) {
    std::vector<uint32_t> win = composeIntroWindow(*f, intro_->frameWidth(), intro_->frameHeight(), W_, H_);
    // composeIntroWindow yields 0xAARRGGBB; the screen format is the Ui's (RGBA8 little endian = A<<24|B<<16|G<<8|R).
    for (size_t i = 0; i < win.size() && i < screen_.size(); i++) {
      uint32_t c = win[i];
      screen_[i] = 0xFF000000u | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
    }
  }
  ui_.textCentered({0, H_ - 24, W_, 20}, "Press Escape to skip", "main10", rgb(160, 160, 160));
}

void Game::drawMainMenu() {
  if (!menuBgTried_) {
    menuBgTried_ = true;
    int best = 0;
    // The 800x600 skyline (82B9B75C:22729921) is the original's main-menu art; the other 800x600 image (01140000) is not.
    if (haveData_)
      if (const Image* sky = icon(0x82B9B75C, 0x22729921)) { menuBg_ = *sky; best = sky->w * sky->h + 1; }
    if (haveData_)
      for (const auto& k : data_.db().keysOfType(T_UI_IMAGE)) if (k.group == 0x82B9B75C) {
        const Image* im = icon(k.group, k.instance);
        if (im && im->w * im->h > best) { best = im->w * im->h; menuBg_ = *im; }
      }
  }
  if (!menuBg_.empty()) ui_.blit(menuBg_, (W_ - menuBg_.w) / 2, (H_ - menuBg_.h) / 2);
  if (msgBox_.active) {   // modal prompt (Exit): the menu stays visible behind it but takes no input
    WindowResult mr = msgBox_.draw(ui_, env());
    handleWindowResult(mr);
    return;
  }
  if (showPrefs_) {
    WindowResult r = prefsDlg_.draw(ui_, env(), prefsState_);
    handleWindowResult(r);
    if (r.close || r.has(Act::Close)) { showPrefs_ = false; saveSettings(); applySettingsToAudio(); }
    return;
  }
  Rect r{W_ / 2 - 160, H_ / 2 - 190, 320, 410};
  ui_.fillAlpha(r, rgb(10, 20, 50), 0.75f);
  ui_.textCentered({r.x, r.y + 6, r.w, 30}, "Metroplex3K", "title12", rgb(255, 255, 255));
  auto cap = [&](uint32_t t, uint32_t i, const char* dflt) { std::string s = str(t, i); return s.empty() ? std::string(dflt) : s; };
  struct B { std::string label; int act; };
  B bs[] = {{cap(0x03C09AFF, 1, "Start new city"), 0}, {cap(0x03C09AFF, 2, "Load city"), 1}, {cap(0x03C09AFF, 9, "Starter town"), 2},
            {cap(0x03C09AFF, 4, "Preferences"), 6}, {cap(0x041F2625, 1, "Load scenario"), 3}, {cap(0x03C09AFF, 13, "Real city terrain"), 4},
            {"Credits", 7}, {cap(0x03C09AFF, 6, "Exit"), 5}};
  int y = r.y + 46;
  for (auto& b : bs) {
    if (ui_.button({r.x + 30, y, r.w - 60, 34}, b.label, haveData_ || b.act == 0 || b.act == 5 || b.act == 6 || b.act == 7)) {
      switch (b.act) {
        case 0: scr_ = Screen::NewCity; break;
        case 1: saveMode_ = false; refreshFiles(0); scr_ = Screen::LoadList; break;
        case 2: saveMode_ = false; refreshFiles(1); scr_ = Screen::LoadList; break;
        case 3: saveMode_ = false; refreshFiles(3); scr_ = Screen::LoadList; break;
        case 4: saveMode_ = false; refreshFiles(2); scr_ = Screen::LoadList; break;
        case 6: showPrefs_ = true; prefsState_ = prefsFromSettings(general_, prefs_); applySettingsToAudio(); break;
        case 7: creditsWin_ = CreditsWindow(); scr_ = Screen::Credits; break;
        default: requestLeave(kLeaveQuit); break;
      }
    }
    y += 42;
  }
  if (world_.W && simReady_ && ui_.button({r.x + 30, y, r.w - 60, 28}, "Back to city")) scr_ = Screen::InCity;
  std::string c1 = str(0x041F2625, 197);
  if (!c1.empty()) ui_.textCentered({0, H_ - 40, W_, 16}, c1, "main10", rgb(220, 220, 220));
}

// New City Options, laid out as measured on the original at 800x600 (dialog-shots/main-menu--start-new-city.png):
// 639x368 frame; blue ringed strips left (4..13) and between the columns (218..227) and a bottom band (y 330) with the
// check-mark button; left column: name fields, difficulty / start date / size radio groups and two check boxes (main9,
// dark ink); right column: scheme selector icons (group 85535958, cell 1 normal, 2 selected, 3 hover) in file order, the
// preview composed from GAME_UI.DAT sprites (landscape, then a tree, the house and another tree, placed by their spans),
// and "User-Made Buildings" with its Select button.
void Game::drawNewCity() {
  auto cap = [&](uint32_t t, uint32_t i, const char* d) { std::string s = str(t, i); return s.empty() ? std::string(d) : s; };
  const bool inCity = world_.W && simReady_;
  if (!inCity && !menuBg_.empty()) ui_.blit(menuBg_, (W_ - menuBg_.w) / 2, (H_ - menuBg_.h) / 2);
  if (!schemesLoaded_) loadSchemes();
  const uint32_t ink = rgb(0x31, 0x34, 0x7B), white = rgb(255, 255, 255);
  // From inside a city the dialog sits 47 px left and 32 px higher (spec 09 7a.5, observed).
  Rect r = ui_.movable({(W_ - 639) / 2 - (inCity ? 47 : 0), (H_ - 368) / 2 - (inCity ? 32 : 0), 639, 368});
  ui_.panel(r, cap(0x029541F4, 0x228, "New City Options"));
  bool cancel = false;
  { Rect hit; bool c = false; if (skin_.dialogCloseBox(ui_, r, hit, c) && c) cancel = true; }
  auto band = [&](Rect b) {
    ui_.fill(b, rgb(0xAD, 0xB6, 0xFF));
    ui_.fill({b.x + 1, b.y + 1, b.w - 2, b.h - 2}, rgb(0x7B, 0x92, 0xFF));
  };
  band({r.x + 4, r.y + 39, 10, 287}); band({r.x + 218, r.y + 39, 10, 287}); band({r.x + 4, r.y + 330, 631, 34});
  auto lab = [&](int x, int y, const std::string& t) { ui_.text(r.x + x, r.y + y, t, "main9", ink); };

  // Name fields (same look as the file dialog's File Name field).
  auto field = [&](Rect fr, std::string& val, int id) {
    ui_.fill(fr, white);
    ui_.fill({fr.x, fr.y, fr.w, 1}, rgb(0x6B, 0x71, 0xBD)); ui_.fill({fr.x, fr.y, 1, fr.h}, rgb(0x6B, 0x71, 0xBD));
    ui_.fill({fr.x + 1, fr.y + 1, fr.w - 2, 1}, ink); ui_.fill({fr.x + 1, fr.y + 1, 1, fr.h - 2}, ink);
    ui_.fill({fr.x + fr.w - 2, fr.y + 1, 1, fr.h - 2}, rgb(0x7B, 0x82, 0xBD));
    ui_.fill({fr.x + fr.w - 1, fr.y, 1, fr.h}, rgb(0xB5, 0xB6, 0xE7)); ui_.fill({fr.x, fr.y + fr.h - 1, fr.w, 1}, rgb(0xB5, 0xB6, 0xE7));
    if (ui_.in.pressed) { if (fr.contains(ui_.in.mx, ui_.in.my)) ui_.focusId = id; else if (ui_.focusId == id) ui_.focusId = 0; }
    bool focus = ui_.focusId == id;
    if (focus) {
      for (char c : ui_.in.text) if (c >= 32 && val.size() < 31) val.push_back(c);
      if (ui_.in.key[K_BACKSPACE] && !val.empty()) val.pop_back();
    }
    ui_.text(fr.x + 4, fr.y + 3, val + (focus ? "|" : ""), "main9", ink);
  };
  lab(17, 38, cap(0x029541F4, 0x229, "City Name:"));
  field({r.x + 23, r.y + 54, 190, 20}, nc_name_, 0x4E43434E);
  lab(17, 76, cap(0x029541F4, 0x22A, "Mayor Name:"));
  field({r.x + 23, r.y + 92, 190, 20}, nc_mayor_, 0x4E434D4E);

  // Radio groups: GZRadioBtn 1 px below the label top, label 14 px right of the button.
  const Image* radioArt = skin_.get("radio");
  auto radio = [&](int x, int y, const std::string& lbl, bool on) {
    Rect hit{r.x + x, r.y + y, 14 + ui_.textWidth(lbl, "main9"), 14};
    bool hot = ui_.hover(hit);
    if (!skin_.drawCheck(ui_, radioArt, r.x + x, r.y + y + 1, on, true, hot, hot && ui_.in.down)) {
      ui_.fill({r.x + x, r.y + y + 1, 12, 12}, white); ui_.frame({r.x + x, r.y + y + 1, 12, 12}, false);
      if (on) ui_.fill({r.x + x + 3, r.y + y + 4, 6, 6}, ink);
    }
    lab(x + 14, y, lbl);
    return ui_.clicked(hit);
  };
  lab(17, 116, cap(0x029541F4, 0x22B, "Difficulty Level:"));
  for (int i = 0; i < 3; i++) {
    const char* fb[] = {"Easy  (%s)", "Medium  (%s)", "Hard  (%s Loan)"};
    std::string lbl = subst(cap(0x029541F4, 0x22C + i, fb[i]), {"$" + groupDigits(Budget::startingFunds(i + 1))});   // font "$" = simoleon
    if (radio(23, 134 + 18 * i, lbl, nc_diff_ == i + 1)) nc_diff_ = i + 1;
  }
  lab(17, 188, cap(0x029541F4, 0x22F, "Start Date:"));
  const int years[] = {1900, 1950, 2000};
  for (int i = 0; i < 3; i++)
    if (radio(23, 206 + 18 * i, cap(0x029541F4, 0x230 + i, std::to_string(years[i]).c_str()), nc_year_ == years[i])) nc_year_ = years[i];
  lab(102, 188, cap(0x029541F4, 0x233, "City Size:"));
  const int sizes[] = {64, 128, 192, 256}; const uint32_t sid[] = {0x2CE, 0x234, 0x2D3, 0x235};
  const char* sn[] = {"Miniature", "Small", "Medium", "Large"};
  for (int i = 0; i < 4; i++)
    if (radio(108, 206 + 18 * i, cap(0x029541F4, sid[i], sn[i]), nc_size_ == sizes[i])) nc_size_ = sizes[i];
  // Check boxes.
  const Image* checkArt = skin_.get("check");
  auto check = [&](int y, const std::string& lbl, bool& v) {
    Rect hit{r.x + 17, r.y + y, 16 + ui_.textWidth(lbl, "main9"), 15};
    bool hot = ui_.hover(hit);
    if (!skin_.drawCheck(ui_, checkArt, r.x + 17, r.y + y + 1, v, true, hot, hot && ui_.in.down)) {
      ui_.fill({r.x + 17, r.y + y + 1, 13, 13}, white); ui_.frame({r.x + 17, r.y + y + 1, 13, 13}, false);
      if (v) ui_.fill({r.x + 20, r.y + y + 4, 7, 7}, ink);
    }
    lab(33, y, lbl);
    if (ui_.clicked(hit)) v = !v;
  };
  check(295, cap(0x029541F4, 0x21D, "Disasters"), nc_disasters_);
  check(312, cap(0x029541F4, 0x21E, "Auto Budget"), nc_autoBudget_);

  // Scheme selectors.
  auto selector = [&](int x0, int y, int pitch, int cells, const std::vector<uint32_t>& icons, int& sel) {
    for (int i = 0; i < int(icons.size()); i++) {
      const Image* strip = skin_.image(0x85535958, icons[size_t(i)]);
      int cw = strip ? strip->w / cells : pitch;
      Rect hit{r.x + x0 + i * pitch, r.y + y, cw, strip ? strip->h : 30};
      bool hot = ui_.hover(hit);
      int cell = i == sel ? 2 : hot ? 3 : 1;
      if (!(strip && Skin::drawCell(ui_, strip, cells, cell, hit.x, hit.y))) ui_.button(hit, std::to_string(i + 1), true, i == sel);
      if (ui_.clicked(hit)) sel = i;
    }
  };
  lab(234, 46, cap(0x041F2625, 0x28, "Landscape:"));
  selector(248, 63, 43, 4, schemeIconL_, nc_landscape_);
  lab(234, 108, cap(0x041F2625, 0x29, "Trees:"));
  selector(248, 121, 39, 4, schemeIconF_, nc_flora_);
  lab(234, 190, cap(0x041F2625, 0x2A, "Buildings:"));
  selector(249, 205, 49, 4, schemeIconB_, nc_buildings_);

  // Preview (GAME_UI.DAT sprites at the original's anchors).
  {
    auto at = [](const std::vector<uint32_t>& v, int i) { return v.empty() ? 0u : v[size_t(i) % v.size()]; };
    wui::schemePreview(ui_, haveData_ ? &data_ : nullptr, r.x, r.y, at(schemePrevL_, nc_landscape_), at(schemePrevF_, nc_flora_), at(schemePrevB_, nc_buildings_));
  }

  // User-made buildings.
  { std::string t = cap(0x041F2625, 0x2B, "User-Made Buildings:"); lab(306, 294, t); }
  Rect sel{r.x + 434, r.y + 282, 78, 33};
  ui_.fill(sel, rgb(0x7B, 0x92, 0xFF));
  if (ui_.button(sel, cap(0x041F2625, 0x2F, "Select"), true, false, "title9")) openUserBuildings();

  // Done (check mark) / cancel.
  Rect done{r.x + 589, r.y + 331, 42, 32};
  bool ok = false;
  if (!skin_.imageButtonCells(ui_, skin_.image(skin_group::Sys, 0xA2DEFD9A), 4, done, "", false, true, ok)) ok = ui_.button(done, "OK");
  if (ok || (ui_.in.key[K_ENTER] && ui_.focusId == 0)) { startNewCity(); return; }
  if (cancel || ui_.in.key[K_ESC]) { ui_.focusId = 0; scr_ = inCity ? Screen::InCity : (haveData_ ? Screen::MainMenu : Screen::NeedData); }
}

// Terrain editor panel (spec 09 3.4), drawn from the original art (group 22DEC90F) at the right edge, as measured on
// dialog-shots/main-menu--start-new-city--terrain-editor-panel.png: 172x480 panel art (0x02), Pick Features diagram
// (base 0x01, lake 0x04, river pictures 0x06 / 0x07, coast strips 0x0D..0x10, edge highlights 0x09..0x0C, hit map 0x12)
// with compass (0x1D..0x20), zoom (0x13 / 0x14) and rotate (0x15 / 0x16) buttons, three vertical sliders (GZSliderV knob
// = cell 3), Re-Generate (0x03) and Accept (0x05) buttons and the six tool buttons (0x17..0x1C).
void Game::drawTerrainEdit() {
  const Input& in = ui_.in;
  if (ui_.focusId == 0) handleKeys(in);
  auto cap = [&](uint32_t t, uint32_t i, const char* d) { std::string s = str(t, i); return s.empty() ? std::string(d) : s; };
  auto art = [&](uint32_t inst) { return skin_.image(0x22DEC90F, inst); };
  const uint32_t ink = rgb(0x31, 0x34, 0x7B), white = rgb(255, 255, 255);
  Rect P{W_ - 172, (H_ - 480) / 2, 172, 480};
  if (const Image* bg = art(0x02)) ui_.blit(*bg, P.x, P.y); else ui_.panel(P);
  if (ui_.hover(P)) uiHit_ = true;
  auto at = [&](int x, int y, int w, int h) { return Rect{P.x + x, P.y + y, w, h}; };
  ui_.text(P.x + 8, P.y + 8, cap(0x029541F4, 0x2CA, "Terrain Edit"), "title12", rgb(0xDE, 0xDF, 0xFF));
  ui_.text(P.x + 49, P.y + 37, cap(0x029541F4, 0x2CB, "Pick Features:"), "main9", ink);
  ui_.text(P.x + 34, P.y + 136, cap(0x029541F4, 0x2CC, "Adjust Parameters:"), "main9", ink);
  // 4-cell image button helper; returns click.
  auto cells = [&](uint32_t inst, Rect r, const std::string& tip, bool toggled = false) {
    bool c = false;
    if (!skin_.imageButtonCells(ui_, art(inst), 4, r, tip, toggled, true, c)) c = ui_.button(r, tip, true, toggled);
    return c;
  };

  // Compass and the Pick Features diagram (rotation turns the compass letter).
  if (const Image* nsew = art(0x1D + uint32_t(cam_.rot & 3))) ui_.blit(*nsew, P.x + 20, P.y + 48);
  Rect dg = at(29, 54, 124, 62);
  const bool lake = (edFlags_ & 0x40) != 0, rRow = (edFlags_ & 0x10) != 0, rCol = (edFlags_ & 0x20) != 0;
  const Image* pic = art(lake ? 0x04 : rCol && !rRow ? 0x06 : rRow && !rCol ? 0x07 : 0x01);
  if (pic) ui_.blit(*pic, dg.x, dg.y);
  if (!lake && rRow && rCol)   // both rivers: add the row river's pixels (0x07 differs from the plain tile only there)
    if (const Image* r7 = art(0x07)) if (const Image* b1 = art(0x01))
      if (r7->w == b1->w && r7->h == b1->h)
        for (int y = 0; y < r7->h; y++) for (int x = 0; x < r7->w; x++) {
          uint32_t c = r7->px[size_t(y) * r7->w + x];
          if (c != b1->px[size_t(y) * b1->w + x] && (c >> 24)) ui_.fill({dg.x + x, dg.y + y, 1, 1}, c);
        }
  // Coasts: dark-blue edge strips (bottom-right 0x0F at (54,28) fitted on the original; the others mirrored).
  const struct { int bit; uint32_t img; int x, y; } coasts[4] = {{0x04, 0x0F, 54, 28}, {0x02, 0x10, 8, 28}, {0x08, 0x0E, 54, 1}, {0x01, 0x0D, 8, 1}};
  for (const auto& c : coasts) if (edFlags_ & c.bit) if (const Image* o = art(c.img)) ui_.blit(*o, dg.x + c.x, dg.y + c.y);
  // Hovered edge: highlighted with its edge line (TL 0x0A, TR 0x09, BL 0x0C, BR 0x0B); the diagram itself shows no coast.
  const Image* hm = art(0x12);
  uint32_t hc = (hm && ui_.hover(dg) && hm->w == dg.w && hm->h == dg.h) ? hm->px[size_t(ui_.in.my - dg.y) * hm->w + (ui_.in.mx - dg.x)] & 0xFFFFFF : 0;
  const struct { uint32_t col, img; int x, y; } edges[4] = {{0x0000FF, 0x0A, 12, 0}, {0x00FFFF, 0x09, 48, 0}, {0xFFFF00, 0x0C, 12, 31}, {0x00FF00, 0x0B, 50, 31}};
  for (const auto& e : edges) if (hc == e.col) if (const Image* o = art(e.img)) ui_.blit(*o, dg.x + e.x, dg.y + e.y);
  if (ui_.clicked(dg)) {
    uint32_t c = hc;
    // Hit map colours (RGBA little endian): red TL edge, yellow TR, cyan BL, green BR, blue the centre.
    // Edges: top-left = column 0 (0x01), bottom-right = last column (0x04, the default coast), top-right = row 0 (0x08),
    // bottom-left = last row (0x02).
    if (c == 0x0000FF) edFlags_ ^= 0x01;
    else if (c == 0x00FF00) edFlags_ ^= 0x04;
    else if (c == 0x00FFFF) edFlags_ ^= 0x08;
    else if (c == 0xFFFF00) edFlags_ ^= 0x02;
    else if (c == 0xFF0000) {   // centre: cycle none -> river across columns -> river across rows -> both -> lake
      int st = lake ? 4 : (rCol ? 1 : 0) + (rRow ? 2 : 0);
      st = (st + 1) % 5;
      edFlags_ &= ~0x70;
      edFlags_ |= st == 4 ? 0x40 : (st & 1 ? 0x20 : 0) | (st & 2 ? 0x10 : 0);
    }
  }
  if (cells(0x13, at(22, 96, 20, 23), cap(0x225872FE, 0xA, "Zoom in"))) cam_.zoom = std::min(4, cam_.zoom + 1);
  if (cells(0x14, at(143, 96, 20, 23), cap(0x225872FE, 0xB, "Zoom out"))) cam_.zoom = std::max(0, cam_.zoom - 1);
  if (cells(0x15, at(41, 107, 20, 23), cap(0x225872FE, 0x93, "Rotate"))) cam_.rot = (cam_.rot + 3) & 3;
  if (cells(0x16, at(124, 107, 20, 23), cap(0x225872FE, 0x92, "Rotate"))) cam_.rot = (cam_.rot + 1) & 3;

  // Sliders: knob (GZSliderV cell 3) travels from y 241 (0) up to y 181 (128); value 64 sits in the middle.
  const Image* knob = skin_.image(skin_group::Sys, 0x02B99F6F);
  int* vals[3] = {&edHills_, &edWater_, &edTrees_};
  for (int i = 0; i < 3; i++) {
    int kx = P.x + 46 + 33 * i;
    Rect track{kx, P.y + 171, 20, 90};
    if (in.pressed && ui_.hover(track)) edDrag_ = i;
    if (edDrag_ == i && in.down) *vals[i] = std::max(0, std::min(128, (P.y + 251 - in.my) * 128 / 60));
    int ky = P.y + 241 - std::max(0, std::min(128, *vals[i])) * 60 / 128;
    if (knob && knob->h >= 80) ui_.blitRect(*knob, kx, ky, 0, 60, 20, 20);   // cells stacked vertically; knob = bottom cell
    else { ui_.fill({kx + 4, ky + 4, 12, 12}, white); ui_.frame({kx + 4, ky + 4, 12, 12}, true); }
  }
  if (!in.down) edDrag_ = -1;

  // Re-Generate / Accept (154x55 four-cell buttons, white title9 captions).
  bool regen = cells(0x03, at(16, 274, 154, 55), "");
  // Captions break into two title9 lines, 18 px apart.
  auto caption = [&](int y, const std::string& t) {
    for (const auto& l : wui::wrap(ui_, t, 90, "title9")) { ui_.text(P.x + 36, P.y + y, l, "title9", white); y += 18; }
  };
  caption(280, cap(0x029541F4, 0x254, "Re-Generate Terrain"));
  // Tools: lower, raise, level / trees, water, bulldoze (41x40 cells).
  struct T { uint32_t img; int x, y; ToolKind k; int a; uint32_t tip; const char* fb; };
  const T tools[] = {{0x17, 28, 342, ToolKind::Terrain, 2, 0x18, "Lower"}, {0x18, 71, 342, ToolKind::Terrain, 1, 0x19, "Raise"},
                     {0x19, 114, 342, ToolKind::Terrain, 4, 0x1A, "Level"}, {0x1B, 28, 382, ToolKind::Terrain, 100, 0x16, "Trees"},
                     {0x1A, 71, 382, ToolKind::Terrain, 32, 0x17, "Water"}, {0x1C, 114, 382, ToolKind::Demolish, 0, 0x1B, "Bulldoze"}};
  for (const auto& t : tools) {
    bool on = tool_ == t.k && (t.k != ToolKind::Terrain || toolArg_ == t.a);
    if (cells(t.img, at(t.x, t.y, 41, 40), cap(0x225872FE, t.tip, t.fb), on)) { setTool(t.k, t.a); toolName_ = t.fb; }
  }
  bool accept = cells(0x05, at(16, 421, 154, 55), "");
  caption(428, cap(0x029541F4, 0x2CD, "Accept this Terrain"));
  if (regen) { edSeed_ = edSeed_ * 1664525u + 1013904223u; beginTerrainEdit(); return; }
  if (accept) { acceptTerrain(); return; }
  if (in.key[K_ESC]) { scr_ = Screen::NewCity; return; }

  if (in.wheel && !uiHit_) cam_.zoom = std::max(0, std::min(4, cam_.zoom + (in.wheel > 0 ? 1 : -1)));
  float s = 32.f;
  float dx = (in.held[1] ? s : 0) - (in.held[0] ? s : 0), dy = (in.held[3] ? s : 0) - (in.held[2] ? s : 0);
  if ((dx || dy) && ui_.focusId == 0) {
    float Wt = 8.f * float(1 << cam_.zoom), Ht = Wt / 2;
    cam_.u = std::max(0.f, std::min(float(world_.W), cam_.u + (dy / (Ht / 2) - dx / (Wt / 2)) / 2));
    cam_.v = std::max(0.f, std::min(float(world_.H), cam_.v + (dy / (Ht / 2) + dx / (Wt / 2)) / 2));
  }
  if (!uiHit_) handleMap();
  if (!statusLine_.empty()) ui_.text(8, H_ - 20, statusLine_, "main10", rgb(255, 255, 200));
}

// Simple folder icons (16x16) in the style of the dialog's shell icons; the shell art itself is not part of the game.
static Image makeFolderIcon(bool up) {
  Image im; im.w = im.h = 16; im.px.assign(256, 0);
  auto put = [&](int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < 16 && y < 16) im.px[size_t(y) * 16 + x] = c; };
  const uint32_t edge = rgb(0x63, 0x65, 0x31), fill = rgb(0xFF, 0xF3, 0x9C), lite = rgb(0xFF, 0xFF, 0xCE), dark = rgb(0x9C, 0x9A, 0x63);
  for (int x = 1; x <= 6; x++) put(x, 2, edge);
  put(0, 3, edge); put(7, 3, edge);
  for (int x = 8; x <= 14; x++) put(x, 3, edge);
  for (int y = 4; y <= 13; y++) { put(0, y, edge); put(15, y, edge); for (int x = 1; x <= 14; x++) put(x, y, y == 4 ? lite : fill); put(14, y, dark); }
  for (int x = 1; x <= 6; x++) put(x, 3, lite);
  for (int x = 0; x <= 15; x++) put(x, 14, edge);
  if (up) {   // bent arrow pointing up-left
    const uint32_t k = rgb(0, 0, 0);
    for (int y = 6; y <= 11; y++) put(5, y, k);
    for (int x = 5; x <= 11; x++) put(x, 11, k);
    put(4, 7, k); put(6, 7, k); put(3, 8, k); put(7, 8, k);
  }
  return im;
}

void Game::ensureFileIcons() {
  if (fileIconsTried_) return;
  fileIconsTried_ = true;
  fileIcons_[0] = makeFolderIcon(true); fileIcons_[1] = makeFolderIcon(false);
  if (const Image* ph = skin_.image(skin_group::Dlg, 0x22BF46CB)) fileIcons_[2] = *ph;   // diamond shown without a preview
  if (const Image* ph = skin_.image(skin_group::Dlg, 0xE34990E9)) fileIcons_[3] = *ph;   // starter-town picture placeholder
  // The program's document icons (SC3U.exe icon groups), one per file kind; city and starter town matched on the original.
  Bytes exe;
  fs::path ep = haveData_ ? resolveCI(data_.appsDir(), "SC3U.exe") : fs::path();
  const uint32_t groups[4] = {1001, 1002, 1003, 1004};
  if (!ep.empty() && readFile(ep, exe)) for (int k = 0; k < 4; k++) loadPeIcon(exe, groups[k], 16, fileIcons_[4 + k]);
}

// Load / Save As file dialog (spec 09 3.3, 7a.5), laid out as measured on the original at 800x600: 478x353 frame,
// view-diamond preview with the city stats under it, "Look In" folder box, file list with a scroll bar, File Name
// field and the check-mark (GZBtnDone) button. Close box = cancel.
void Game::drawLoadList() {
  auto cap = [&](uint32_t t, uint32_t i, const char* d) { std::string s = str(t, i); return s.empty() ? std::string(d) : s; };
  const bool inCity = world_.W && simReady_;
  if (!inCity && !menuBg_.empty()) ui_.blit(menuBg_, (W_ - menuBg_.w) / 2, (H_ - menuBg_.h) / 2);
  ensureFileIcons();
  fileFrame_++;
  const uint32_t label = rgb(0x7B, 0x82, 0xB5), ink = rgb(0x31, 0x34, 0x7B), white = rgb(255, 255, 255);
  Rect r = ui_.movable({(W_ - 478) / 2, (H_ - 353) / 2, 478, 353});
  std::string titles[] = {cap(0x029541F4, 0x220, "Load Saved City"), cap(0x029541F4, 0x2CF, "Load Starter Town"),
                          cap(0x029541F4, 0x24C, "Load Real City Terrain"), cap(0x041F2625, 2, "Load Scenario")};
  ui_.panel(r, saveMode_ ? cap(0x029541F4, 0x226, "Save As") : titles[fileKind_]);
  bool cancel = false;
  { Rect hit; bool c = false; if (skin_.dialogCloseBox(ui_, r, hit, c) && c) cancel = true; }
  const bool modalOpen = confirmOverwrite_;
  auto clicked = [&](Rect a) { return !modalOpen && ui_.clicked(a); };

  // Sunken white box: dark top/left line, light bottom/right line.
  auto sunken = [&](Rect b) {
    ui_.fill(b, white);
    ui_.fill({b.x, b.y, b.w, 1}, rgb(0x7B, 0x8A, 0xFF)); ui_.fill({b.x, b.y, 1, b.h}, rgb(0x7B, 0x8A, 0xFF));
    ui_.fill({b.x, b.y + b.h - 1, b.w, 1}, rgb(0xFF, 0xF3, 0xFF)); ui_.fill({b.x + b.w - 1, b.y, 1, b.h}, rgb(0xFF, 0xF3, 0xFF));
  };
  auto rightText = [&](int right, int y, const std::string& s) { ui_.text(right - ui_.textWidth(s, "main9"), y, s, "main9", label); };

  // Edit field (main9, dark ink): outline dark at top/left (two lines), light at bottom/right, as measured.
  auto editField = [&](Rect fr, std::string& val, int id, bool fileName) {
    ui_.fill(fr, white);
    ui_.fill({fr.x, fr.y, fr.w, 1}, rgb(0x6B, 0x71, 0xBD)); ui_.fill({fr.x, fr.y, 1, fr.h}, rgb(0x6B, 0x71, 0xBD));
    ui_.fill({fr.x + 1, fr.y + 1, fr.w - 2, 1}, ink); ui_.fill({fr.x + 1, fr.y + 1, 1, fr.h - 2}, ink);
    ui_.fill({fr.x + fr.w - 2, fr.y + 1, 1, fr.h - 2}, rgb(0x7B, 0x82, 0xBD));
    ui_.fill({fr.x + fr.w - 1, fr.y, 1, fr.h}, rgb(0xB5, 0xB6, 0xE7)); ui_.fill({fr.x, fr.y + fr.h - 1, fr.w, 1}, rgb(0xB5, 0xB6, 0xE7));
    if (!modalOpen && ui_.in.pressed) { if (fr.contains(ui_.in.mx, ui_.in.my)) ui_.focusId = id; else if (ui_.focusId == id) ui_.focusId = 0; }
    bool focus = ui_.focusId == id;
    if (focus) {
      for (char c : ui_.in.text) if (c >= 32 && val.size() < 31 && !(fileName && std::strchr("\\/:*?\"<>|", c))) val.push_back(c);
      if (ui_.in.key[K_BACKSPACE] && !val.empty()) val.pop_back();
    }
    ui_.text(fr.x + 4, fr.y + 3, val + (focus ? "|" : ""), "main9", ink);
  };

  updatePreview();
  auto sunkenLines = [&](Rect fr) {
    ui_.fill({fr.x, fr.y, fr.w, 1}, rgb(0x7B, 0x8A, 0xFF)); ui_.fill({fr.x, fr.y, 1, fr.h}, rgb(0x7B, 0x8A, 0xFF));
    ui_.fill({fr.x, fr.y + fr.h - 1, fr.w, 1}, rgb(0xFF, 0xF3, 0xFF)); ui_.fill({fr.x + fr.w - 1, fr.y, 1, fr.h}, rgb(0xFF, 0xF3, 0xFF));
  };
  if (fileKind_ == 3 && !saveMode_) {
    // Scenarios: picture (162x152) at (12,70), description box below it, author line by the Done button.
    if (!preview_.diamond.empty()) {   // shown at most 162 px wide (the shipped pictures are 163)
      int pw = std::min(162, preview_.diamond.w);
      ui_.blit(preview_.diamond, r.x + 12, r.y + 70, 0, pw);
      sunkenLines({r.x + 11, r.y + 69, pw + 2, preview_.diamond.h + 2});
    }
    Rect box{r.x + 11, r.y + 231, 164, 110};
    ui_.fill({box.x + 1, box.y + 1, box.w - 2, box.h - 2}, rgb(0xDE, 0xDF, 0xFF));
    sunkenLines(box);
    int ty = r.y + 237;
    for (const auto& l : wui::wrap(ui_, preview_.extra, 150, "main10")) {
      if (ty + 16 > box.y + box.h) break;
      ui_.text(r.x + 17, ty, l, "main10", ink); ty += 16;
    }
    if (preview_.ok) ui_.text(r.x + 254, r.y + 320, cap(0x041F2625, 0x2C0, "Author:") + " " + preview_.mayor, "main9", label);
  } else if (fileKind_ == 1 && !saveMode_) {
    // Starter towns: flat 162x152 picture and the explanatory text (main10, wrapped at 150 px, 16 px lines).
    const Image& pic = !preview_.diamond.empty() ? preview_.diamond : fileIcons_[3];
    if (!pic.empty()) {
      ui_.blit(pic, r.x + 15, r.y + 70);
      Rect fr{r.x + 14, r.y + 69, pic.w + 2, pic.h + 2};   // sunken outline
      ui_.fill({fr.x, fr.y, fr.w, 1}, rgb(0x7B, 0x8A, 0xFF)); ui_.fill({fr.x, fr.y, 1, fr.h}, rgb(0x7B, 0x8A, 0xFF));
      ui_.fill({fr.x, fr.y + fr.h - 1, fr.w, 1}, rgb(0xFF, 0xF3, 0xFF)); ui_.fill({fr.x + fr.w - 1, fr.y, 1, fr.h}, rgb(0xFF, 0xF3, 0xFF));
    }
    int ty = r.y + 234;
    for (const auto& l : wui::wrap(ui_, cap(0x029541F4, 0x2DB, ""), 150, "main10")) { ui_.text(r.x + 17, ty, l, "main10", ink); ty += 16; }
  } else {
  // Preview diamond and stats (Save As: the city being saved, with editable mayor and city names).
  const FilePreview& pv = saveMode_ ? savePreview_ : preview_;
  const int dx = r.x + 18, dy = r.y + 43;   // placeholder diamond
  if (!pv.diamond.empty()) ui_.blit(pv.diamond, r.x + 16, r.y + 41);
  else if (!fileIcons_[2].empty()) ui_.blit(fileIcons_[2], dx, dy);
  if ((showStats_ || saveMode_) && pv.hasStats) {
    std::string lines[] = {
        subst(cap(0x029541F4, 0x223, "Population: %s"), {groupDigits(pv.population)}),
        subst(cap(0x029541F4, 0x224, "Funds: %s"), {"$" + groupDigits(pv.funds)}   /* the game font draws "$" as the simoleon sign */),
        subst(cap(0x029541F4, 0x225, "Date: %s"), {pv.date}),
        cap(0x029541F4, 0x22A, "Mayor Name:") + " " + pv.mayor,
        cap(0x029541F4, 0x229, "City Name:") + " " + pv.name};
    for (int i = 0; i < (saveMode_ ? 3 : 5); i++) ui_.text(r.x + 22, r.y + 247 + 14 * i, lines[i], "main9", label);
  }
  if (saveMode_) {
    ui_.text(r.x + 22, r.y + 294, cap(0x029541F4, 0x22A, "Mayor Name:"), "main9", label);
    editField({r.x + 106, r.y + 292, 168, 19}, saveMayor_, 0x4D41594F, false);   // "MAYO"
    ui_.text(r.x + 22, r.y + 317, cap(0x029541F4, 0x229, "City Name:"), "main9", label);
    editField({r.x + 106, r.y + 315, 168, 19}, saveCity_, 0x43495459, false);    // "CITY"
  }
  if (!saveMode_ && fileKind_ == 0) {   // only saved cities carry stats
    Rect cb{r.x + 22, r.y + 327, 13, 13};
    bool hot = !modalOpen && ui_.hover({cb.x, cb.y, 120, 13});
    if (!skin_.drawCheck(ui_, skin_.get("check"), cb.x, cb.y, showStats_, true, hot, hot && ui_.in.down)) { ui_.fill(cb, white); ui_.frame(cb, false); }
    ui_.text(r.x + 37, r.y + 327, cap(0x029541F4, 0x287, "Show City Stats"), "main9", label);
    if (clicked({cb.x, cb.y, 120, 13})) showStats_ = !showStats_;
  }
  }

  // Look In.
  rightText(r.x + 248, r.y + 42, cap(0x029541F4, 0x286, "Look In:"));
  Rect combo{r.x + 252, r.y + 40, 197, 19};
  sunken(combo);
  ui_.blit(fileIcons_[1], combo.x + 3, combo.y + 2);
  ui_.text(combo.x + 24, combo.y + 3, fileDir_.filename().string(), "system9", ink);
  Rect arrow{r.x + 450, r.y + 41, 16, 16};
  { bool c = false; if (!skin_.imageButtonCells(ui_, skin_.get("combo"), 4, arrow, "", false, true, c)) c = ui_.button(arrow, "v");
    if (c && !modalOpen) fileComboOpen_ = !fileComboOpen_; }

  // File list with scroll bar.
  Rect list{r.x + 252, r.y + 70, 195, 175};
  sunken(list);
  const int rowH = 17, visible = (list.h - 2) / rowH, total = int(files_.size());
  if (!modalOpen && ui_.hover(list) && ui_.in.wheel) fileScroll_ -= ui_.in.wheel;
  fileScroll_ = std::max(0, std::min(fileScroll_, std::max(0, total - visible)));
  int activate = -1;
  for (int i = 0; i < visible && fileScroll_ + i < total; i++) {
    int idx = fileScroll_ + i;
    Rect row{list.x + 1, list.y + 1 + i * rowH, list.w - 2, rowH};
    const FileRow& f = files_[size_t(idx)];
    bool sel = idx == fileSel_;
    if (sel) ui_.fill(row, rgb(0, 0, 255));
    const Image& ic = f.type == 2 ? fileIcons_[4 + fileKind_] : fileIcons_[f.type];
    if (!ic.empty()) {
      // Opaque rows centred in the row (half pixel rounds down), as measured for the city and starter-town icons.
      int top = ic.h, bot = -1;
      for (int y = 0; y < ic.h; y++) for (int x = 0; x < ic.w; x++) if (ic.px[size_t(y) * ic.w + x] >> 24) { top = std::min(top, y); bot = std::max(bot, y); }
      if (bot >= top) ui_.blit(ic, row.x + 2, row.y + (rowH - (bot - top + 1) + 1) / 2 - top);
    }
    ui_.text(row.x + 23, row.y + 2, f.label, "system9", sel ? white : ink);
    if (clicked(row) && !fileComboOpen_) {
      if (fileLastClickRow_ == idx && fileFrame_ - fileLastClickFrame_ < 30) activate = idx;
      fileSel_ = idx; fileLastClickRow_ = idx; fileLastClickFrame_ = fileFrame_;
      if (f.type == 2) saveName_ = f.label;
    }
  }
  // Scroll bar: GZScrollbar cells (16x16) up 6/7, thumb 8, track 9, down 10/11.
  {
    const Image* sb = skin_.image(skin_group::Sys, 0xE2B99E01);
    Rect up{r.x + 450, list.y, 16, 16}, dn{r.x + 450, list.y + list.h - 16, 16, 16};
    Rect track{r.x + 450, up.y + 16, 16, dn.y - up.y - 16};
    int th = 16, range = std::max(0, total - visible);
    int ty = track.y + (range ? (track.h - th) * fileScroll_ / range : 0);
    if (sb && sb->w >= 192) {
      for (int y = track.y; y < track.y + track.h; y += 16) ui_.blitRect(*sb, track.x, y, 9 * 16, 0, 16, std::min(16, track.y + track.h - y));
      bool upDown = ui_.hover(up) && ui_.in.down, dnDown = ui_.hover(dn) && ui_.in.down;
      Skin::drawCell(ui_, sb, 12, upDown ? 7 : 6, up.x, up.y);
      Skin::drawCell(ui_, sb, 12, dnDown ? 11 : 10, dn.x, dn.y);
      Skin::drawCell(ui_, sb, 12, 8, track.x, ty);
    } else { ui_.fill(track, ui_.panelDark); ui_.fill({track.x, ty, 16, th}, ui_.panelCol); ui_.button(up, "^"); ui_.button(dn, "v"); }
    if (clicked(up)) fileScroll_--;
    if (clicked(dn)) fileScroll_++;
    if (clicked(track)) fileScroll_ += ui_.in.my < ty ? -visible : ui_.in.my >= ty + th ? visible : 0;
    fileScroll_ = std::max(0, std::min(fileScroll_, range));
  }

  rightText(r.x + 250, r.y + 263, cap(0x029541F4, 0x222, "File Name:"));
  editField({r.x + 252, r.y + 260, 214, 19}, saveName_, 0x46494C45, true);   // focus id "FILE"

  // Done (check mark).
  Rect done{r.x + 425, r.y + 310, 42, 32};
  bool doneClicked = false;
  if (!skin_.imageButtonCells(ui_, skin_.image(skin_group::Sys, 0xA2DEFD9A), 4, done, "", false, !modalOpen, doneClicked))
    doneClicked = ui_.button(done, "OK", !modalOpen);
  if (!modalOpen && !fileComboOpen_ && ui_.in.key[K_ENTER]) doneClicked = true;
  if (!modalOpen && !fileComboOpen_) {
    if (ui_.in.key[K_DOWN] && fileSel_ + 1 < total) { fileSel_++; if (files_[size_t(fileSel_)].type == 2) saveName_ = files_[size_t(fileSel_)].label; }
    if (ui_.in.key[K_UP] && fileSel_ > 0) { fileSel_--; if (files_[size_t(fileSel_)].type == 2) saveName_ = files_[size_t(fileSel_)].label; }
    if (fileSel_ >= 0 && (fileSel_ < fileScroll_ || fileSel_ >= fileScroll_ + visible) && (ui_.in.key[K_DOWN] || ui_.in.key[K_UP]))
      fileScroll_ = std::max(0, std::min(fileSel_ - (fileSel_ < fileScroll_ ? 0 : visible - 1), std::max(0, total - visible)));
  }
  if (doneClicked && activate < 0) {
    if (fileSel_ >= 0 && files_[size_t(fileSel_)].type != 2) activate = fileSel_;
    else if (!saveName_.empty()) {
      activate = -2;   // by name
      for (int i = 0; i < total; i++) if (files_[size_t(i)].type == 2 && toLower(files_[size_t(i)].label) == toLower(saveName_)) { fileSel_ = i; activate = i; break; }
    }
  }
  if (activate >= 0 && files_[size_t(activate)].type != 2) { fs::path d = files_[size_t(activate)].path; openFileDir(d); return; }
  if (activate != -1) {
    if (saveMode_) {
      bool exists = activate >= 0 || !resolveCI(fileDir_, saveName_ + ".sc3").empty();
      if (exists && !confirmOverwrite_) confirmOverwrite_ = true;
      else { applySaveNames(); saveCurrent(saveName_, fileDir_); scr_ = Screen::InCity; confirmOverwrite_ = false; if (pendingAfterLeave_ && !dirty_) doLeave(pendingAfterLeave_); }
    } else if (activate >= 0) {
      fs::path p = files_[size_t(activate)].path;
      fileKind_ == 3 ? (void)loadScenarioFile(p) : loadFile(p);
    }
  }

  // "Look In" drop-down: the folder chain from the root down to the current folder.
  if (fileComboOpen_) {
    std::vector<fs::path> chain;
    for (fs::path p = fileDir_; !p.empty(); p = p.parent_path()) { chain.insert(chain.begin(), p); if (p == p.parent_path()) break; }
    Rect dd{combo.x, combo.y + combo.h, combo.w + 17, rowH * int(chain.size()) + 2};
    ui_.fill(dd, white); ui_.frame(dd, false);
    for (size_t i = 0; i < chain.size(); i++) {
      Rect row{dd.x + 1, dd.y + 1 + int(i) * rowH, dd.w - 2, rowH};
      bool hot = ui_.hover(row);
      if (hot) ui_.fill(row, rgb(0, 0, 255));
      ui_.blit(fileIcons_[1], row.x + 2 + int(i) * 4, row.y + 1);
      std::string nm = chain[i].filename().string(); if (nm.empty()) nm = chain[i].string();
      ui_.text(row.x + 23 + int(i) * 4, row.y + 2, nm, "system9", hot ? white : ink);
      if (ui_.clicked(row)) { fileComboOpen_ = false; openFileDir(chain[i]); return; }
    }
    if (ui_.in.released && !dd.contains(ui_.in.mx, ui_.in.my) && !arrow.contains(ui_.in.mx, ui_.in.my)) fileComboOpen_ = false;
    if (ui_.in.key[K_ESC]) { fileComboOpen_ = false; return; }
  }

  if (cancel || (ui_.in.key[K_ESC] && !fileComboOpen_)) {
    if (confirmOverwrite_) confirmOverwrite_ = false;
    else { pendingAfterLeave_ = 0; ui_.focusId = 0; scr_ = inCity ? Screen::InCity : Screen::MainMenu; }
  }
  if (confirmOverwrite_) {
    Rect d{W_ / 2 - 180, H_ / 2 - 50, 360, 100};
    ui_.panel(d, "Metroplex3K");
    ui_.textCentered({d.x, d.y + 30, d.w, 20}, cap(0x015E410C, 0xC, "Overwrite existing file?"));
    if (ui_.button({d.x + 40, d.y + 62, 120, 26}, "Yes")) { applySaveNames(); saveCurrent(saveName_, fileDir_); scr_ = Screen::InCity; confirmOverwrite_ = false; if (pendingAfterLeave_ && !dirty_) doLeave(pendingAfterLeave_); }
    if (ui_.button({d.x + 200, d.y + 62, 120, 26}, "No")) confirmOverwrite_ = false;
  }
}

void Game::drawCredits(uint32_t dtMs) {
  creditsWin_.update(dtMs);
  WindowResult r = creditsWin_.draw(ui_, env());
  if (r.close || r.has(Act::Close) || ui_.in.key[K_ESC]) scr_ = Screen::MainMenu;
}

void Game::drawScenarioEnd() {
  Rect r = ui_.movable({W_ / 2 - 260, H_ / 2 - 180, 520, 360});
  ui_.panel(r, str(0x041F2625, 4).empty() ? "Scenario over" : str(0x041F2625, 4));
  ScenarioController::Outcome o;
  if (scen_) o = scen_->outcome();
  ui_.text(r.x + 20, r.y + 44, o.win ? "Scenario completed!" : "Scenario failed.", "title12");
  ui_.text(r.x + 20, r.y + 74, "Rank: " + (o.rankTitle.empty() ? std::to_string(o.rank) : o.rankTitle));
  ui_.text(r.x + 20, r.y + 96, "Goals met: " + std::to_string(o.goalsMet) + " of " + std::to_string(o.goalsTotal));
  if (!o.text.empty()) wui::paragraph(ui_, {r.x + 20, r.y + 124, r.w - 40, 160}, o.text);
  if (ui_.button({r.x + 20, r.y + r.h - 50, 150, 32}, "Keep playing")) scr_ = Screen::InCity;
  if (ui_.button({r.x + 185, r.y + r.h - 50, 150, 32}, "Another scenario")) { saveMode_ = false; refreshFiles(3); scr_ = Screen::LoadList; }
  if (ui_.button({r.x + 350, r.y + r.h - 50, 150, 32}, "Main menu")) scr_ = Screen::MainMenu;
}

}  // namespace m3k
