// Game shell: screens (intro, main menu, new city, terrain editor, load/save, in-city HUD, windows) built on
// the Ui toolkit. Platform independent: the host feeds Input each frame and presents screen().
//
// Implementation is split over several files:
//   game.cpp          init, menu tree, tool menu, HUD, map interaction
//   game_front.cpp    intro movie, main menu, new city, terrain editor, load/save, credits, tip
//   game_windows.cpp  in-city windows (budget, ordinances, advisors, query, maps ...)
//   game_sim.cpp      advisors / petitioners / news / disasters / street sim glue
//   game_scenario.cpp scenario controller (separate TU: scenario.h clashes with news.h names)
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core/audio.h"
#include "core/automata.h"
#include "core/cheats.h"
#include "core/city.h"
#include "core/disasters.h"
#include "core/flora.h"
#include "core/font.h"
#include "core/gamedata.h"
#include "core/intro.h"
#include "core/render.h"
#include "core/skin.h"
#include "core/settings.h"
#include "core/sim.h"
#include "core/terraform.h"
#include "core/terrain_gen.h"
#include "core/ui.h"
#include "core/video.h"
#include "core/windows.h"
#include "core/world.h"

namespace m3k {

struct MenuItemInfo { bool enabled = true; uint32_t nameTable = 0, nameId = 0; std::string kind; uint32_t target = 0, arg1 = 0, arg2 = 0; std::string fallbackName; };
struct MenuButton { uint32_t id = 0, item = 0, sub = 0; };
struct MenuDesc { uint32_t id = 0, titleTable = 0, titleId = 0, colour = 0; std::vector<uint32_t> buttons; std::string fallbackTitle; };

struct MenuTree {
  std::map<uint32_t, MenuItemInfo> items;
  std::map<uint32_t, MenuButton> buttons;
  std::map<uint32_t, MenuDesc> menus;
  bool parse(const IniFile& ini);
  // Built-in copy of the shipped tree (spec 09 2.4) used when no MenuItem.INI is available.
  void buildFallback();
};

enum class Screen { NeedData, Intro, MainMenu, NewCity, TerrainEdit, LoadList, InCity, Credits, ScenarioEnd };
enum class ToolKind { None, Terrain, Zone, Road, Demolish, Plop, Query, Dispatch, Snapshot, Unavailable };

class ScenarioController;  // game_scenario.h

class Game {
 public:
  Game();
  ~Game();
  bool init(const fs::path& gameRoot, std::string& err);  // empty root => NeedData screen
  void frame(const Input& in, int w, int h, uint32_t dtMs);
  const std::vector<uint32_t>& screen() const { return screen_; }
  bool wantQuit() const { return quit_; }
  void dropPath(const std::string& path);
  void requestFile(const std::string& path);  // open a city file directly (skips the intro)
  std::string windowTitle() const;
  // Audio hook: null until game data with Res/Sound/AUDIO.INI is open (silent). The host's audio callback
  // calls audio()->mixAudio() under its own lock (the same lock must be held around frame()).
  AudioSystem* audio() { return audio_.get(); }
  Sim& sim() { return sim_; }
  City& city() { return sim_.city(); }

  // Startup options.
  void setPersistence(bool on) { persist_ = on; }        // settings / prefs file (default on)
  void setIntroEnabled(bool on) { introEnabled_ = on; }  // default on
  // Without game data: use the built-in menu / texts and start the generated-terrain demo (tools, tests).
  void startDemo(int size = 64);
  Screen currentScreen() const { return scr_; }

  // Scripted access for headless tools/tests.
  void openWindow(const std::string& name);        // budget, ordinances, neighbours, deals, advisors, prefs, props, goals, map, graphs, charts
  void runMenuItem(uint32_t menuItemId) { execute(menuItemId); }
  // Run a MenuItem.INI command class id directly (tools, windows, ...). For tools/tests.
  void runCommand(uint32_t target, uint32_t arg1 = 0) { runTarget(target, arg1); }
  void startDisaster(DisasterType t);
  void typeCheat(const std::string& s);
  void setPaused(bool p) { paused_ = p; }
  bool paused() const { return paused_; }
  const std::string& lastMessage() const { return lastMsg_; }
  NewsTicker& news() { return news_; }
  AdvisorManager& advisors() { return advisors_; }
  PetitionerManager& petitioners() { return petitioners_; }
  DisasterManager& disasters() { return disasters_; }
  bool loadScenarioFile(const fs::path& p);
  bool scenarioActive() const;
  void gotoScreen(Screen s) { scr_ = s; }
  void setView(int tx, int tz, int zoom);
  // Host callback: receives snapshot bytes (raw RGBA8 w,h + pixels, no PNG encoder in core) and a suggested file name.
  std::function<void(const std::string& name, int w, int h, const std::vector<uint32_t>& rgba)> onSnapshot;
  const std::vector<std::string>& snapshots() const { return snapshotNames_; }
  const RenderScene& scene() const { return scene_; }
  // Screen position (inside the last rendered frame) of the middle of a tile; false if off-screen / not pickable. For tools & tests.
  bool tileScreenPos(int tx, int tz, int& sx, int& sy);
  const World& world() const { return world_; }
  World& mutableWorld() { return world_; }
  void terrainChanged() { renderer_.invalidateShade(); }  // after editing world_.alt/water directly (tests/tools)
  bool windowOpen(const std::string& name) const;
  // Test hook: every window control drawn registers (name, rect) here (cleared by the caller).
  void setSpotSink(std::vector<Spot>* s) { spotSink_ = s; }

 private:
  // screens
  void drawNeedData();
  void drawIntro(uint32_t dtMs);
  void drawMainMenu();
  void drawNewCity();
  void drawTerrainEdit();
  void drawLoadList();
  void drawCredits(uint32_t dtMs);
  void drawInCity(uint32_t dtMs);
  void drawHud();
  void drawToolMenu();
  bool drawSkinnedToolMenu();
  void drawWindows(uint32_t dtMs);
  void drawMessage();
  void drawScenarioEnd();
  void handleMap();
  // front end
  void startIntro();
  void finishIntro();
  void startNewCity();
  void beginTerrainEdit();
  void beginTerrainEditFrom(bool fromNewCity);
  void acceptTerrain();
  void loadFile(const fs::path& p);
  void saveCurrent(const std::string& name);
  void adopt(World&& w);
  void attachWorld();
  void refreshFiles(int kind);
  void updatePreview();
  void loadSettings();
  void saveSettings();
  void applySettingsToAudio();
  void loadSchemes();
  // menu
  void execute(uint32_t menuItemId);
  void dispatch(const MenuItemInfo& mi);
  void message(const std::string& s);
  std::string str(uint32_t table, uint32_t id);
  WinEnv env();
  void setTool(ToolKind k, int a = 0, uint32_t occ = 0);
  void tickSim(uint32_t dtMs);
  void ensureFonts();
  void initAudio();
  void openPicker(uint32_t category);
  void sound(int id) { if (audio_) audio_->request(id); }
  int64_t& fundsRef() { return scr_ == Screen::TerrainEdit ? edFunds_ : sim_.funds; }
  void closeModelessWindows();
  void enterCity();
  void runTarget(uint32_t target, uint32_t arg1 = 0, const std::string& name = std::string());
  void drawTicker(Rect r);
  void drawCheatBox();
  void drawFrontCommon();
  void drawQueryPopup();
  void modalOpened();
  void modalClosed();
  bool anyBlockingWindow() const;
  void applyCheat(const CheatResult& r);
  void updateHover(int tx, int tz, bool onTile);
  void postNews(uint32_t table, uint32_t id);
  void sirenAndGo();
  void startScenarioHooks();
  void checkScenarioEnd();
  void loadHotkeys();
  // windows (game_windows.cpp)
  void handleWindowResult(const WindowResult& r);
  void buildQueryInfo(int tx, int tz, QueryInfo& q);
  void buildMapSource(MapSource& src);
  void buildBudgetView(BudgetView& v);
  void buildPickerEntries(PickerCategory c);
  void takeSnapshot(int size);
  // sim glue (game_sim.cpp)
  void initManagers();
  void dailyHook();
  void weeklyHook();
  void monthlyHook(int month, int year);
  void fillAdvisorInputs(AdvisorInputs& in);
  void presentPetitions();
  void applyPetitionEffect(const PetitionEffect& e);
  void applyDisasterOutput(DisasterOutput& out);
  void gotoDisaster();
  void dispatchUnit(int kind, int tx, int tz);
  void setupStreetSim();
  void streetSimFrame(uint32_t dtMs);
  void postTicker(uint32_t table, uint32_t id, bool hasLoc = false, int x = 0, int z = 0);
  void centreOn(int x, int z);
  // tool preview / quote
  City::Quote quoteTool(int x0, int z0, int x1, int z1);
  std::vector<std::pair<int, int>> routePath(int x0, int z0, int x1, int z1);
  void executeTool(int x0, int z0, int x1, int z1);
  void buildScene();
  // scenarios (game_scenario.cpp bridge)
  void scenarioDayTick();
  void scenarioMinuteTick();

  fs::path root_;
  Skin skin_;
  int skinH_ = 0;
  GameData data_;
  bool haveData_ = false;
  Fonts fonts_;
  Ui ui_;
  std::vector<uint32_t> screen_;
  int W_ = 0, H_ = 0;
  Screen scr_ = Screen::NeedData;
  bool quit_ = false;
  bool persist_ = true;
  bool introEnabled_ = true;
  std::string pathInput_;
  std::string msgText_, lastMsg_;
  Image menuBg_;
  bool menuBgTried_ = false;

  // intro movie
  std::unique_ptr<TgqMovie> movie_;
  std::unique_ptr<IntroPlayer> intro_;
  int64_t introMs_ = 0;
  bool introTried_ = false;
  AudioMixer::Handle introVoice_ = 0;

  // settings
  Preferences prefs_;
  GeneralSettings general_;
  PrefsState prefsState_;
  PreferencesDialog prefsDlg_;
  bool showPrefs_ = false;
  fs::path prefsPath_;

  // in-city
  World world_;
  Renderer renderer_;
  Camera cam_;
  RenderOptions ropt_;
  CityConfig cityCfg_;
  std::unique_ptr<AudioSystem> audio_;
  Sim sim_;
  std::unique_ptr<Terraformer> terra_;
  Lcg floraLcg_{99};
  bool simReady_ = false;
  bool paused_ = true;
  int speed_ = 1;
  double simAccum_ = 0;
  MenuTree menu_;
  bool menuLoaded_ = false;
  uint32_t openRoot_ = 0, openFly_ = 0;
  ToolKind tool_ = ToolKind::None;
  int toolArg_ = 0; uint32_t toolOcc_ = 0;
  std::string toolName_;
  bool dragging_ = false; int ax_ = 0, az_ = 0;
  bool uiHit_ = false;
  std::string statusLine_;
  int64_t edFunds_ = 1000000000;
  RenderScene scene_;
  int snapSize_ = 0;
  uint32_t frameDt_ = 0;   // dt of the current frame (ticker scrolling)
  bool disastersOn_ = true, autoBudget_ = false;
  bool dirty_ = false;
  int pendingAfterLeave_ = 0;

  // windows
  bool showBudget_ = false, showOrd_ = false, showNeigh_ = false, showDeals_ = false, showAdv_ = false,
       showProps_ = false, showGoals_ = false, showMap_ = false, showGraphs_ = false, showCharts_ = false,
       showPicker_ = false, showQuery_ = false, showTip_ = false;
  BudgetWindow budgetWin_;
  OrdinancesWindow ordWin_;
  NeighboursWindow neighWin_;
  BusinessDealsWindow dealsWin_;
  AdvisorWindow advWin_;
  CityPropertiesDialog propsDlg_;
  CityPropsState propsState_;
  ScenarioGoalsWindow goalsWin_;
  MapWindow mapWin_;
  GraphsWindow graphsWin_;
  ChartsWindow chartsWin_;
  TickerWidget ticker_;
  QueryWindow queryWin_;
  QueryInfo queryInfo_;
  BuildingPicker picker_;
  std::vector<PickerEntry> pickerEntries_;
  TipWindow tipWin_;
  CreditsWindow creditsWin_;
  MessageBox msgBox_;
  int msgBoxPurpose_ = 0;
  PetitionerDialog petDlg_;
  std::unique_ptr<PetitionerView> petView_;
  uint32_t petViewId_ = 0;
  std::vector<uint32_t> petShown_;
  std::string lastBriefing_;
  // tool preview / ticker / misc UI state
  int hoverX_ = -1, hoverZ_ = -1;
  bool hoverValid_ = false;
  std::string costText_; int costX_ = 0, costY_ = 0; bool costOk_ = true, costShow_ = false;
  bool cheatOpen_ = false;
  bool wasPausedBeforeModal_ = false; int modalDepth_ = 0;
  std::vector<std::string> snapshotNames_;
  int snapSizeIdx_ = 1;
  bool panning_ = false; int panLastX_ = 0, panLastY_ = 0;
  std::vector<RenderScene> unusedScenes_;
  std::vector<Spot>* spotSink_ = nullptr;
  int streetAcc_ = 0;
  int shake_ = 0;
  int sweepMinute_ = 0;
  uint32_t newsClickX_ = 0;
  bool introLoadedAudio_ = false;
  int lastWeekDay_ = -1;
  int autoNameCounter_ = 0;

  // simulation glue
  AdvisorManager advisors_;
  PetitionerManager petitioners_;
  NewsTicker news_;
  TickerDb tickerDb_;
  std::unique_ptr<NewsTickerSink> sink_;
  GameRng rng_{12345};
  DisasterManager disasters_;
  CheatRegistry cheats_;
  std::string cheatBuf_;
  bool managersReady_ = false;
  int lastMonth_ = 0;
  int disasterX_ = -1, disasterZ_ = -1;
  AutomataData automata_;
  bool automataLoaded_ = false;
  struct StreetHost : public AutomataHost {
    World* w = nullptr; City* c = nullptr; int month = 1, day = 1;
    std::vector<int32_t> piece;   // tile -> index into w->surface or -1
    int mapWidth() const override { return w ? w->W : 0; }
    int mapHeight() const override { return w ? w->H : 0; }
    bool pieceAt(int x, int y, AutoNet k, PieceRef& out) const override;
    bool isWater(int x, int y) const override;
    bool pedEligible(int x, int y) const override;
    int trafficDensity(int x, int y) const override;
    void gameDate(int& m, int& d) const override { m = month; d = day; }
    void rebuild();
  };
  std::vector<int> streetSources_;
  size_t streetOccCount_ = 0, streetNetCount_ = 0;
  void syncStreetSources();
  std::unique_ptr<StreetHost> streetHost_;
  std::unique_ptr<StreetSim> street_;
  std::unique_ptr<ScenarioController> scen_;
  std::map<char, uint32_t> hotkeys_;

  // dialogs
  struct FileRow { fs::path path; std::string label; };
  std::vector<FileRow> files_;
  std::vector<std::string> fileLabels_;
  int fileSel_ = -1, fileScroll_ = 0, fileKind_ = 0, previewFor_ = -2;
  bool saveMode_ = false;
  std::string saveName_;
  struct FilePreview { bool ok = false; std::string name, mayor, date, extra; int64_t population = 0, funds = 0; bool hasStats = false; };
  FilePreview preview_;
  bool showStats_ = true;
  bool confirmOverwrite_ = false;
  std::string nc_name_ = "New City", nc_mayor_ = "Mayor";
  int nc_size_ = 256, nc_diff_ = 1, nc_year_ = 2000;
  bool nc_disasters_ = true, nc_autoBudget_ = false;
  int nc_landscape_ = 0, nc_flora_ = 0, nc_buildings_ = 0;
  std::vector<uint32_t> schemeIdsL_, schemeIdsF_, schemeIdsB_;
  bool schemesLoaded_ = false;
  uint32_t edSeed_ = 1;
  fs::path currentFile_;
  std::map<std::string, Image> iconCache_;
  const Image* icon(uint32_t group, uint32_t inst);
};

}  // namespace m3k
