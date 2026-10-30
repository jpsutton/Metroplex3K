// Game shell front end: intro movie, main menu, new city, terrain editor, load/save, credits, settings.
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "core/game.h"
#include "core/game_scenario.h"
#include "core/gamedir.h"
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

void Game::loadSchemes() {
  schemeIdsL_.assign(1, 0); schemeIdsF_.assign(1, 0); schemeIdsB_.assign(1, 0);
  schemesLoaded_ = true;
  if (!haveData_) return;
  IniFile ini;
  if (!data_.ini().load("SC3CityScheme.ini", ini)) return;
  auto fill = [&](const char* sec, std::vector<uint32_t>& out) {
    for (const auto& kv : ini.section(sec)) { uint32_t id = parseIdNumber(kv.first); if (id && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id); }
  };
  fill("LandScapes", schemeIdsL_); fill("FloraSets", schemeIdsF_); fill("BuildingSets", schemeIdsB_);
}

static std::string schemeName(uint32_t id, const char* kind) {
  if (!id) return std::string("Default ") + kind;
  char b[48]; std::snprintf(b, sizeof b, "%s %08X", kind, id); return b;
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
  streetHost_.reset(); street_.reset();
  sim_.init(&world_, haveData_ ? &data_ : nullptr, &cityCfg_, 12345u + uint32_t(world_.W));
  sim_.city().onSound = [this](int id) { sound(id); };
  sim_.city().onMessage = [this](const std::string& m) { message(m); };
  simReady_ = true; paused_ = true; dirty_ = false; simAccum_ = 0;
  showPicker_ = false;
  cheats_.resetSession();
  initManagers();
  setupStreetSim();
  scr_ = Screen::InCity;
  setTool(ToolKind::None);
  openRoot_ = openFly_ = 0;
  lastMonth_ = sim_.city().clock.date().month;
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
    if (isTerrain) {
      world_.info.cityName = p.stem().string();
      // Real-city terrains go through the New City dialog like a generated terrain.
      beginTerrainEditFrom(true);
      currentFile_.clear();
      return;
    }
    scen_ = std::move(sc);
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

void Game::saveCurrent(const std::string& name) {
  fs::path dir = resolveCI(root_, "Cities");
  if (dir.empty()) { dir = (root_.empty() ? configDir() : root_) / "Cities"; std::error_code ec; fs::create_directories(dir, ec); }
  fs::path p = dir / (name + ".sc3");
  if (simReady_) sim_.city().flush();
  Bytes b = saveCity(world_);
  if (scen_) b = scen_->mergeState(b);
  { std::vector<uint32_t> log = cheats_.log(); if (!log.empty()) b = writeCheatLog(b, log); }
  FILE* f = std::fopen(p.string().c_str(), "wb");
  if (!f) { message("Could not write " + p.string()); return; }
  std::fwrite(b.data(), 1, b.size(), f); std::fclose(f);
  currentFile_ = p; dirty_ = false;
  message("Saved " + p.filename().string());
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
  attachWorld();
  enterCity();
  Budget& b = sim_.city().budget;
  int64_t start = Budget::startingFunds(nc_diff_);
  if (nc_diff_ == 3) { b.newCity(0); b.issueBond(10000, 1, nc_year_); }
  else b.newCity(start);
  disastersOn_ = nc_disasters_; autoBudget_ = nc_autoBudget_;
  disasters_.setDisastersEnabled(disastersOn_);
  b.showBudgetOnlyInDebt = false;
  currentFile_.clear();
  saveSettings();
  postNews(kTblNewsTriggered, 1);
}

void Game::refreshFiles(int kind) {
  files_.clear(); fileLabels_.clear(); fileSel_ = -1; fileScroll_ = 0; fileKind_ = kind; previewFor_ = -2; confirmOverwrite_ = false;
  const char* sub[] = {"Cities", "Cities/StarterTowns", "Cities/Terrains", "Cities/Scenarios"};
  const char* ext[] = {".sc3", ".st3", ".sct", ".snr"};
  fs::path dir = resolveCI(root_, sub[kind]);
  if (dir.empty()) return;
  for (const auto& f : listFilesCI(dir, ext[kind])) { files_.push_back({f, f.stem().string()}); fileLabels_.push_back(f.stem().string()); }
  if (kind == 0) {
    // Also list Windows-era .sc2 files? Not loadable: only .sc3.
  }
}

void Game::updatePreview() {
  if (fileSel_ == previewFor_) return;
  previewFor_ = fileSel_;
  preview_ = FilePreview();
  if (fileSel_ < 0 || fileSel_ >= int(files_.size())) return;
  Bytes b;
  if (!readFile(files_[size_t(fileSel_)].path, b)) return;
  try {
    World w = loadCity(b, nullptr);
    preview_.ok = true; preview_.name = w.info.cityName; preview_.mayor = w.info.mayorName;
    preview_.date = std::to_string(w.info.year);
    if (fileKind_ == 3) {
      ScenarioInfo si;
      auto tf = [this](uint32_t g, uint32_t i) { return str(g, i); };
      if (ScenarioController::peek(b, tf, si)) { preview_.name = si.title.empty() ? preview_.name : si.title; preview_.mayor = si.author; preview_.extra = si.description; }
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
    if (haveData_)
      for (const auto& k : data_.db().keysOfType(T_UI_IMAGE)) if (k.group == 0x82B9B75C) {
        const Image* im = icon(k.group, k.instance);
        if (im && im->w * im->h > best) { best = im->w * im->h; menuBg_ = *im; }
      }
  }
  if (!menuBg_.empty()) ui_.blit(menuBg_, (W_ - menuBg_.w) / 2, (H_ - menuBg_.h) / 2);
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
        default: quit_ = true; break;
      }
    }
    y += 42;
  }
  if (world_.W && simReady_ && ui_.button({r.x + 30, y, r.w - 60, 28}, "Back to city")) scr_ = Screen::InCity;
  std::string c1 = str(0x041F2625, 197);
  if (!c1.empty()) ui_.textCentered({0, H_ - 40, W_, 16}, c1, "main10", rgb(220, 220, 220));
}

void Game::drawNewCity() {
  Rect r{W_ / 2 - 250, H_ / 2 - 270, 500, 540};
  auto cap = [&](uint32_t t, uint32_t i, const char* d) { std::string s = str(t, i); return s.empty() ? std::string(d) : s; };
  ui_.panel(r, cap(0x029541F4, (world_.W && simReady_) ? 0x281 : 0x282, "New City"));
  int y = r.y + 38;
  ui_.text(r.x + 20, y + 4, cap(0x029541F4, 0x229, "City name"));
  ui_.textBox({r.x + 150, y, 330, 24}, nc_name_);
  y += 34;
  ui_.text(r.x + 20, y + 4, cap(0x029541F4, 0x22A, "Mayor name"));
  ui_.textBox({r.x + 150, y, 330, 24}, nc_mayor_);
  y += 40;
  ui_.text(r.x + 20, y + 4, cap(0x029541F4, 0x22B, "Difficulty"));
  const char* dn[] = {"Easy", "Medium", "Hard"};
  for (int i = 0; i < 3; i++) if (ui_.button({r.x + 150 + i * 110, y, 104, 26}, cap(0x029541F4, 0x22C + i, dn[i]), true, nc_diff_ == i + 1)) nc_diff_ = i + 1;
  y += 34;
  ui_.text(r.x + 20, y, "Starting funds: $" + groupDigits(Budget::startingFunds(nc_diff_)) + (nc_diff_ == 3 ? " (loan)" : ""), "main10");
  y += 26;
  ui_.text(r.x + 20, y + 4, cap(0x029541F4, 0x233, "City size"));
  const int sizes[] = {64, 128, 192, 256}; const char* sn[] = {"Mini", "Small", "Medium", "Large"}; const uint32_t sid[] = {0x2CE, 0x234, 0x2D3, 0x235};
  for (int i = 0; i < 4; i++) if (ui_.button({r.x + 150 + i * 84, y, 80, 26}, cap(0x029541F4, sid[i], sn[i]), true, nc_size_ == sizes[i])) nc_size_ = sizes[i];
  y += 38;
  ui_.text(r.x + 20, y + 4, cap(0x029541F4, 0x22F, "Start date"));
  const int years[] = {1900, 1950, 2000};
  for (int i = 0; i < 3; i++) if (ui_.button({r.x + 150 + i * 110, y, 104, 26}, std::to_string(years[i]), true, nc_year_ == years[i])) nc_year_ = years[i];
  y += 38;
  ui_.checkbox({r.x + 20, y, 200, 20}, cap(0x029541F4, 0x21D, "Disasters"), nc_disasters_);
  ui_.checkbox({r.x + 250, y, 220, 20}, cap(0x029541F4, 0x21E, "Auto budget"), nc_autoBudget_);
  y += 32;
  // City scheme selector (landscape / trees / buildings): click to cycle.
  if (!schemesLoaded_) loadSchemes();
  auto cycler = [&](const char* label, const std::vector<uint32_t>& ids, int& sel, const char* kind) {
    ui_.text(r.x + 20, y + 4, label);
    std::string nm = schemeName(ids.empty() ? 0 : ids[size_t(sel) % ids.size()], kind);
    if (ui_.button({r.x + 150, y, 330, 26}, nm + "   (click to change)")) sel = ids.empty() ? 0 : int((sel + 1) % int(ids.size()));
    y += 32;
  };
  cycler(cap(0x041F2625, 0x28, "Landscape").c_str(), schemeIdsL_, nc_landscape_, "Landscape");
  cycler(cap(0x041F2625, 0x29, "Trees").c_str(), schemeIdsF_, nc_flora_, "Trees");
  cycler(cap(0x041F2625, 0x2A, "Buildings").c_str(), schemeIdsB_, nc_buildings_, "Buildings");
  if (ui_.button({r.x + 60, r.y + r.h - 56, 160, 36}, "OK") || (ui_.in.key[K_ENTER] && ui_.focusId == 0)) { startNewCity(); }
  if (ui_.button({r.x + 280, r.y + r.h - 56, 160, 36}, "Cancel") || ui_.in.key[K_ESC])
    scr_ = (world_.W && simReady_) ? Screen::InCity : (haveData_ ? Screen::MainMenu : Screen::NeedData);
}

void Game::drawTerrainEdit() {
  const Input& in = ui_.in;
  ui_.text(8, 8, "Terrain editor", "title12", rgb(255, 255, 255));
  Rect bar{0, H_ - 44, W_, 44};
  ui_.panel(bar);
  if (ui_.hover(bar)) uiHit_ = true;
  struct T { const char* n; ToolKind k; int a; };
  const T tools[] = {{"Raise", ToolKind::Terrain, 1}, {"Lower", ToolKind::Terrain, 2}, {"Level", ToolKind::Terrain, 4}, {"Water", ToolKind::Terrain, 32}, {"Trees", ToolKind::Terrain, 100}};
  int x = 8;
  for (const auto& t : tools) {
    if (ui_.button({x, H_ - 38, 66, 32}, t.n, true, tool_ == t.k && toolArg_ == t.a)) { setTool(t.k, t.a); toolName_ = t.n; }
    x += 70;
  }
  x += 10;
  if (ui_.button({x, H_ - 38, 36, 32}, "+")) cam_.zoom = std::min(4, cam_.zoom + 1);
  if (ui_.button({x + 40, H_ - 38, 36, 32}, "-")) cam_.zoom = std::max(0, cam_.zoom - 1);
  if (ui_.button({x + 80, H_ - 38, 36, 32}, "<")) { cam_.rot = (cam_.rot + 3) & 3; }
  if (ui_.button({x + 120, H_ - 38, 36, 32}, ">")) { cam_.rot = (cam_.rot + 1) & 3; }
  x += 170;
  if (ui_.button({x, H_ - 38, 110, 32}, "Regenerate")) { edSeed_ = edSeed_ * 1664525u + 1013904223u; beginTerrainEdit(); return; }
  if (ui_.button({x + 116, H_ - 38, 110, 32}, "Accept")) { acceptTerrain(); return; }
  if (ui_.button({x + 232, H_ - 38, 110, 32}, "Cancel") || in.key[K_ESC]) { scr_ = Screen::NewCity; return; }
  if (in.wheel && !uiHit_) cam_.zoom = std::max(0, std::min(4, cam_.zoom + (in.wheel > 0 ? 1 : -1)));
  float s = 32.f;
  float dx = (in.held[1] ? s : 0) - (in.held[0] ? s : 0), dy = (in.held[3] ? s : 0) - (in.held[2] ? s : 0);
  if ((dx || dy) && ui_.focusId == 0) {
    float Wt = 8.f * float(1 << cam_.zoom), Ht = Wt / 2;
    cam_.u = std::max(0.f, std::min(float(world_.W), cam_.u + (dy / (Ht / 2) - dx / (Wt / 2)) / 2));
    cam_.v = std::max(0.f, std::min(float(world_.H), cam_.v + (dy / (Ht / 2) + dx / (Wt / 2)) / 2));
  }
  handleMap();
  if (!statusLine_.empty()) ui_.text(8, H_ - 62, statusLine_, "main10", rgb(255, 255, 200));
}

void Game::drawLoadList() {
  Rect r{W_ / 2 - 280, H_ / 2 - 250, 560, 500};
  auto cap = [&](uint32_t t, uint32_t i, const char* d) { std::string s = str(t, i); return s.empty() ? std::string(d) : s; };
  std::string titles[] = {cap(0x029541F4, 0x220, "Load city"), cap(0x029541F4, 0x2CF, "Starter towns"), cap(0x029541F4, 0x24C, "Real city terrains"), cap(0x041F2625, 2, "Scenarios")};
  ui_.panel(r, saveMode_ ? cap(0x029541F4, 0x226, "Save city") : titles[fileKind_]);
  Rect lr{r.x + 16, r.y + 40, 300, 330};
  if (ui_.list(lr, fileLabels_, fileSel_, fileScroll_) && saveMode_ && fileSel_ >= 0) saveName_ = fileLabels_[size_t(fileSel_)];
  // Preview pane.
  updatePreview();
  Rect pr{r.x + 326, r.y + 40, 218, 330};
  ui_.frame(pr, false);
  if (preview_.ok) {
    int y = pr.y + 8;
    ui_.text(pr.x + 8, y, preview_.name, "main10"); y += 18;
    if (!preview_.mayor.empty()) { ui_.text(pr.x + 8, y, (fileKind_ == 3 ? "Author: " : "Mayor: ") + preview_.mayor); y += 18; }
    ui_.text(pr.x + 8, y, "Year: " + preview_.date); y += 22;
    if (!preview_.extra.empty()) wui::paragraph(ui_, {pr.x + 8, y, pr.w - 16, 200}, preview_.extra);
  }
  if (saveMode_) { ui_.text(r.x + 16, r.y + 384, cap(0x029541F4, 0x222, "File name")); ui_.textBox({r.x + 100, r.y + 380, 300, 24}, saveName_); }
  bool ok = ui_.button({r.x + 60, r.y + 440, 140, 32}, saveMode_ ? "Save" : "Open", saveMode_ ? !saveName_.empty() : fileSel_ >= 0);
  if (ui_.in.key[K_ENTER] && ui_.focusId == 0) ok = saveMode_ ? !saveName_.empty() : fileSel_ >= 0;
  if (ok) {
    if (saveMode_) {
      fs::path p = resolveCI(root_, "Cities");
      bool exists = !p.empty() && !resolveCI(p, saveName_ + ".sc3").empty();
      if (exists && !confirmOverwrite_) confirmOverwrite_ = true;
      else { saveCurrent(saveName_); scr_ = Screen::InCity; confirmOverwrite_ = false; }
    } else if (fileSel_ >= 0) { fileKind_ == 3 ? (void)loadScenarioFile(files_[size_t(fileSel_)].path) : loadFile(files_[size_t(fileSel_)].path); }
  }
  if (ui_.button({r.x + 360, r.y + 440, 140, 32}, "Cancel") || ui_.in.key[K_ESC]) {
    if (confirmOverwrite_) confirmOverwrite_ = false;
    else scr_ = (world_.W && simReady_) ? Screen::InCity : Screen::MainMenu;
  }
  if (confirmOverwrite_) {
    Rect d{W_ / 2 - 180, H_ / 2 - 50, 360, 100};
    ui_.panel(d, "Metroplex3K");
    ui_.textCentered({d.x, d.y + 30, d.w, 20}, cap(0x015E410C, 0xC, "Overwrite existing file?"));
    if (ui_.button({d.x + 40, d.y + 62, 120, 26}, "Yes")) { saveCurrent(saveName_); scr_ = Screen::InCity; confirmOverwrite_ = false; }
    if (ui_.button({d.x + 200, d.y + 62, 120, 26}, "No")) confirmOverwrite_ = false;
  }
}

void Game::drawCredits(uint32_t dtMs) {
  creditsWin_.update(dtMs);
  WindowResult r = creditsWin_.draw(ui_, env());
  if (r.close || r.has(Act::Close) || ui_.in.key[K_ESC]) scr_ = Screen::MainMenu;
}

void Game::drawScenarioEnd() {
  Rect r{W_ / 2 - 260, H_ / 2 - 180, 520, 360};
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
