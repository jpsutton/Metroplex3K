// Game shell: screens (intro, main menu, new city, terrain editor, load/save, in-city HUD, windows) built on
// the Ui toolkit. Platform independent: the host feeds Input each frame; an optional FrameBackend supplies
// the present target (SDL today, GPU later). Headless tools use screen().
//
// Implementation is split over several files:
//   game.cpp          init, menu tree, tool menu, HUD, map interaction
//   game_front.cpp    intro movie, main menu, new city, terrain editor, load/save, credits, tip
//   game_windows.cpp  in-city windows (budget, ordinances, advisors, query, maps ...)
//   game_sim.cpp      advisors / petitioners / news / disasters / street sim glue
//   game_scenario.cpp scenario controller (separate TU: scenario.h clashes with news.h names)
#pragma once
#include <functional>
#include <array>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>
#include "core/audio.h"
#include "core/frame_backend.h"
#include "core/bat.h"
#include "core/automata.h"
#include "core/cheats.h"
#include "core/city.h"
#include "core/disasters.h"
#include "core/flora.h"
#include "core/cursor.h"
#include "core/font.h"
#include "core/hotkeys.h"
#include "core/gamedata.h"
#include "core/intro.h"
#include "core/render.h"
#include "core/sim_worker.h"
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
  // If outPixels is non-null, the frame is drawn into that pitched buffer (pitchBytes = bytes per row;
  // 0 = tightly packed w*4) and screen() is left untouched. Hosts that lock a streaming texture can pass
  // the locked pointer to avoid a post-frame upload memcpy.
  void frame(const Input& in, int w, int h, uint32_t dtMs, uint32_t* outPixels = nullptr, int outPitchBytes = 0);
  // Same as frame(..., dest.pixels, dest.pitchBytes) when a FrameBackend::beginFrame filled `dest`.
  void frame(const Input& in, uint32_t dtMs, const FrameBuffer& dest) {
    citySink_ = dest.citySink;
    if (citySink_) citySinkSeen_ = citySink_;
    frame(in, dest.w, dest.h, dtMs, dest.pixels, dest.pitchBytes);
    citySink_ = nullptr;
  }
  const std::vector<uint32_t>& screen() const { return screen_; }
  bool wantQuit() const { return quit_; }
  // The window's close button / quit hotkeys: runs the original's Exit flow ("Now leaving" OK/Cancel, then "Do you
  // want to save the game?" for a modified city). Quits at once when there is nothing to ask (intro, no data, or the
  // flow is already running).
  void requestQuit();
  void dropPath(const std::string& path);
  void requestFile(const std::string& path);  // open a city file directly (skips the intro)
  std::string windowTitle() const;
  // Audio hook: null until game data with Res/Sound/AUDIO.INI is open (silent). The host's audio callback
  // calls audio()->mixAudio() under its own lock; AudioMixer synchronises the voices itself, so frame() runs
  // without that lock.
  AudioSystem* audio() { return audio_.get(); }
  // Optional host hooks: the host's audio-callback lock. Game takes it only while it replaces audio_ (data
  // (re)load), so the callback never sees a dangling pointer.
  std::function<void()> audioLock, audioUnlock;
  Sim& sim() { return sim_; }
  City& city() { return sim_.city(); }
  // The per-tile values of a View Data map layer (empty when the layer has none); verification harness.
  std::vector<uint8_t> dataMapValues(DataLayer l);
  Renderer& renderer() { return renderer_; }
  ICityRasterizer& cityRasterizer() { return renderer_; }

  // Startup options.
  const std::string& language() const { return data_.language; }
  // Terrain noise tables: the original seeds them from the system time at start-up (spec 25 6.3 [STATIC]); the desktop host does
  // the same. Headless tools and tests keep the fixed default seed so their output is reproducible.
  void seedTerrainNoise(uint32_t seed) { renderer_.setNoiseSeed(seed); invalidateCityView(); }
  void setLanguage(const std::string& name) { languageRequest_ = name; }   // before init(); empty = from the OS locale
  void setPersistence(bool on) { persist_ = on; }        // settings / prefs file (default on)
  void setIntroEnabled(bool on) { introEnabled_ = on; }  // default on
  // Without game data: use the built-in menu / texts and start the generated-terrain demo (tools, tests).
  void startDemo(int size = 64);
  Screen currentScreen() const { return scr_; }

  // Scripted access for headless tools/tests.
  void openWindow(const std::string& name);        // budget, ordinances, neighbours, deals, advisors, prefs, props, goals, map, layers, graphs, charts, engine
  void runMenuItem(uint32_t menuItemId) { execute(menuItemId); }
  // Run a MenuItem.INI command class id directly (tools, windows, ...). For tools/tests.
  void runCommand(uint32_t target, uint32_t arg1 = 0) { runTarget(target, arg1); }
  ToolKind currentTool() const { return tool_; }
  int toolArg() const { return toolArg_; }
  const CityLayers& layers() const { return layers_; }
  void setLayers(const CityLayers& l) { layers_ = l; layersDlg_.edit = l; applyLayers(); }   // tools / tests
  void startDisaster(DisasterType t);
  void setDisastersOn(bool on);   // city flag + schedule monthly DM gate; probes and scripts
  bool disasterActive() const { return disasters_.anyActiveNonParade(); }
  void typeCheat(const std::string& s);
  void setPaused(bool p);
  void setDirty(bool d) { dirty_ = d; }            // mark the city modified (save prompts); tools/tests
  // City-raster skip stats (Phase 1 leftover): frames that ran renderer_.render vs reused the city cache.
  uint64_t cityRasterFrames() const { return cityRasterFrames_; }
  uint64_t cityCacheHits() const { return cityCacheHits_; }
  bool paused() const { return paused_; }
  // Run the sim day tick on a worker thread (opt-in; see game_async.cpp). Off by default. Tests and tools
  // leave it off and get the synchronous behaviour. Turning it off waits for a running tick.
  void setAsyncSim(bool on);
  bool asyncSim() const { return asyncSim_; }
  // Wait for a running async tick and apply its results. No-op when async is off or idle.
  void syncSim();
  const std::string& lastMessage() const { return lastMsg_; }
  NewsTicker& news() { return news_; }
  AdvisorManager& advisors() { return advisors_; }
  PetitionerManager& petitioners() { return petitioners_; }
  DisasterManager& disasters() { return disasters_; }
  bool loadScenarioFile(const fs::path& p);
  bool scenarioActive() const;
  void gotoScreen(Screen s) { scr_ = s; }
  void setView(int tx, int tz, int zoom);
  const Camera& camera() const { return cam_; }
  bool startParadeNow();   // the holiday parade (petition 0x12D accepted); tools / tests
  bool paradeRunning() const { return paradeRunning_; }
  int paradeX() const { return paradeX_; }
  int paradeZ() const { return paradeZ_; }
  const QueryInfo& queryInfo() const { return queryInfo_; }   // the last query (tests)
  bool panelsHidden() const { return panelsHidden_; }   // F12
  bool layersDialogOpen() const { return showLayers_; }
  // Host callback: receives snapshot bytes (raw RGBA8 w,h + pixels, no PNG encoder in core) and a suggested file name.
  std::function<void(const std::string& name, int w, int h, const std::vector<uint32_t>& rgba)> onSnapshot;
  const std::vector<std::string>& snapshots() const { return snapshotNames_; }
  const RenderScene& scene() const { return scene_; }
  // Cursor for the next frame: file stem in Res/UI/Shared/Cursors (empty = host default arrow). The host calls
  // cursorImage() when the name changes.
  const std::string& cursorName() const { return cursorName_; }
  // While right-drag scrolling: the PAN/EARLY MOVER cursor drawn at the pointer (empty otherwise).
  const std::string& panCursor() const { return panCursor_; }
  bool cursorImage(const std::string& name, CursorImage& out) { return haveData_ && loadCursor(data_, name, out); }
  // Screen position (inside the last rendered frame) of the middle of a tile; false if off-screen / not pickable. For tools & tests.
  bool tileScreenPos(int tx, int tz, int& sx, int& sy);
  const World& world() const { return world_; }
  World& mutableWorld() { return world_; }
  void terrainChanged() { invalidateCityView(); }  // after editing world_.alt/water directly (tests/tools)
  bool windowOpen(const std::string& name) const;
  void closeAllWindows();   // every window and prompt (tools: scripted screenshots, probes)
  const EngineOptions& engineOptions() const { return engineOpts_; }
  EngineOptions& mutableEngineOptions() { return engineOpts_; }
  // Timings of the most recent frame() call (microsecond resolution), for the host's frame log.
  struct FramePhases {
    double simMs = 0;      // tickSim: the sim cascade (days, refresh)
    double streetMs = 0;   // streetSimFrame
    double rasterMs = 0;   // city rasteriser, 0 on a cache hit
    double simWaitMs = 0;  // async sim: time the frame spent waiting for the worker (join) and applying its result
    double asyncEndMs = 0; // async sim: publishing snapshots and starting the next job
    bool rastered = false; // true when the city was rasterised this frame (false = cached blit)
    bool simDay = false;   // at least one sim day ran this frame
    int zoom = 0, speed = 0;
    bool paused = false;
  };
  const FramePhases& lastFramePhases() const { return phases_; }
  int framesPerSecond() const { return fps_; }   // half-second average, updated whether or not the counter shows
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
  void drawFpsCounter(uint32_t dtMs);
  void drawNavigator(int barH);
  void rebuildMiniMap();
  bool navigatorArt() const;
  Rect navigatorRect(int barH) const;
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
  void saveCurrent(const std::string& name, const fs::path& dir = fs::path());   // dir empty = Cities/
  // Leaving the current city (Exit / New / Load). `action`: kLeaveQuit, kLeaveNew, or kLeaveLoad + file kind (0..3).
  // Exit first shows the "Now leaving" prompt; a city that is open then asks to save (Yes / No / Cancel) before the action runs.
  static constexpr int kLeaveQuit = 1, kLeaveNew = 2, kLeaveLoad = 10;
  void requestLeave(int action);
  void askSaveBeforeLeave(int action);
  void doLeave(int action);
  void onMessageResult(MsgButton b);
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
  // Spec 09 §7.2 / 25 §9.3 [INFERRED]: pipe/subway tools force the underground view so pieces are visible.
  void syncViewForTool(ToolKind k, int a, uint32_t occ);
  void tickSim(uint32_t dtMs);
  void ensureFonts();
  void initAudio();
  void openPicker(uint32_t category);
  void sound(int id) { if (audio_) audio_->request(id); }
  // Looping disaster sounds (fire, riot): start on the first call, SetLevel to `pct`, stop when `on` is false (SoundLoop command).
  void soundLoop(int id, bool on, int pct);
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
  // Spec 14 §2.3 (Q-AZ): save/load, terrain editor, scenario UI, and other non-InCity screens
  // while a city is loaded raise pending/modal (not the player pause count).
  void syncScreenPending();
  void applyCheat(const CheatResult& r);
  void updateHover(int tx, int tz, bool onTile);
  // Posts a ticker item; returns its item number, or 0 if the string is missing.
  uint32_t postNews(uint32_t table, uint32_t id);
  void sirenAndGo();
  void startScenarioHooks();
  void checkScenarioEnd();
  void loadHotkeys();
  void hotkeyCommand(int cmd, int arg1, int arg2);
  void handleKeys(const Input& in);
  // windows (game_windows.cpp)
  void handleWindowResult(const WindowResult& r);
  void buildQueryInfo(int tx, int tz, QueryInfo& q);
  void stopAmbience();
  void updateAmbience(uint32_t dtMs);                  // picks the ambient FSC area from the view (game_query.cpp)
  TileSoundContext tileSoundContext(int tx, int tz);   // audio classification of a tile (game_query.cpp)
  void updateSparseAmbience();                         // spec 06 s6: building sounds around the view centre (game_query.cpp)
  void notifyBuildingDestroyed(int occIdx);            // spec 06 s10.5 explosion sound (game_query.cpp)
  int landscapeAudioCode() const;                      // spec 06 s10.4: 1 desert, 2 jungle, else 0 (game_query.cpp)
  // pollutionKind: 0 air, 1 water, 2 garbage; -1 = the map window's selector.
  void buildMapSource(MapSource& src, int pollutionKind = -1);
  // View City Layers: copy layers_ into the render options; refresh the data-layer overlay from live data.
  void applyLayers();
  void refreshLayerOverlay();
  void buildBudgetView(BudgetView& v);
  void buildPickerEntries(PickerCategory c);
  void takeSnapshot(int size);
  // sim glue (game_sim.cpp)
  void initManagers();
  void dailyHook();
  void weekNotification();   // Monday: raises the advisors' and petitioner manager's evaluation flag (spec 22 3.6)
  void dayNotification();    // every day: runs the evaluation when the flag is up (so on Tuesday)
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
  // async sim (game_async.cpp)
  struct HudSnap {
    int dayOfYear = 1, year = 0, pop = 0, month = 1, day = 1, clockYear = 0;
    int64_t funds = 0;
    Valve valveR, valveC, valveI;
    std::string neighbourName;
    std::array<std::string, 4> neighbourNames;   // slots 0..3
  };
  HudSnap liveHud() const;
  const HudSnap& hudNow();                 // live when the sim is idle, else the last published snapshot
  const World& viewWorld() const { return asyncSim_ ? renderWorld_ : world_; }
  bool simBusy() const { return asyncSim_ && simJobInFlight_; }
  void asyncFrameBegin(const Input& in);
  void asyncFrameEnd();
  bool canRunFast(const Input& in) const;
  bool anyWindowOpen() const;
  void publishSnapshots(bool jobJustFinished);
  void launchSimJob();
  void finishSimJob();
  void daySimStep(bool disasterActive);    // the sim half of one day (runs on the worker when async)
  void dayPostStep();                      // month / week hooks (always on the owner thread, sim idle)
  void bindRenderWorld();
  void bumpCityVisual() { ++cityVisualSerial_; }
  void invalidateCityView() { renderer_.invalidateShade(); bumpCityVisual(); }
  uint64_t cityViewSignature() const;
  void blitCityToDest();
  void applyCostLabelFromScene();
  // scenarios (game_scenario.cpp bridge)
  void scenarioDayTick();
  void scenarioMinuteTick();

  fs::path root_;
  Skin skin_;
  std::string dataSummary_;
  int skinH_ = 0, skinW_ = 0;
  GameData data_;
  bool haveData_ = false;
  Fonts fonts_;
  Ui ui_;
  std::vector<uint32_t> screen_;
  std::vector<uint32_t> cityFb_;  // last city-only raster (no HUD); reused when the view signature matches
  bool cityFbValid_ = false;
  CityGpuSink* citySinkSeen_ = nullptr;   // last sink seen, kept across frames to invalidate its sprite atlas
  CityGpuSink* citySink_ = nullptr;   // set for the duration of frame(in, dt, dest) when the backend composites on the GPU
  bool cityGpuValid_ = false;         // the sink holds the city for lastCitySig_ (alternative to cityFb_)
  uint64_t lastCitySig_ = 0;
  uint64_t cityVisualSerial_ = 0;   // bumps when the world / shade / layer overlay changes
  uint64_t cityRasterFrames_ = 0, cityCacheHits_ = 0;
  uint32_t* drawPx_ = nullptr;   // active frame destination (screen_ or host-locked buffer)
  int drawPitch_ = 0;            // pixels per row for drawPx_
  int W_ = 0, H_ = 0;
  Screen scr_ = Screen::NeedData;
  bool quit_ = false;
  bool persist_ = true;
  std::string languageRequest_;
  bool introEnabled_ = true;
  std::string pathInput_;
  std::string msgText_, lastMsg_;
  Image menuBg_;
  Image miniMap_;   // navigator diamond, 128x128, view orientation
  int miniFrame_ = 0;
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
  EngineOptions engineOpts_;
  EngineOptionsDialog engineDlg_;
  bool showEngine_ = false;
  // Frame counter overlay (engine option): built-in monospace font, scale follows the window height.
  FramePhases phases_;
  uint32_t fpsAccMs_ = 0;
  int fpsFrames_ = 0, fps_ = 0, fpsScale_ = 0;
  Font fpsFont_;
  static constexpr int kFpsHist = 64;
  uint16_t fpsHist_[kFpsHist] = {};   // recent frame times (ms), ring buffer
  int fpsHistPos_ = 0, fpsHistCount_ = 0;
  fs::path prefsPath_;

  // in-city
  World world_;
  Renderer renderer_;
  Camera cam_;
  RenderOptions ropt_ = [] { RenderOptions o; o.grid = true; return o; }();   // grid on by default
  CityConfig cityCfg_;
  std::unique_ptr<AudioSystem> audio_;
  AmbientAreaSelector ambSel_;
  bool ambCfg_ = false;
  int64_t ambAcc_ = 0;
  int sparseTick_ = 0;
  std::set<int> disasterLoops_;   // looping disaster sounds currently started
  float ambLastU_ = 0, ambLastV_ = 0;
  Sim sim_;
  std::unique_ptr<Terraformer> terra_;
  TreeTool treeTool_;   // [BuildingPlacementCosts] 300 (the flora price)
  bool treeTileBlocked(int x, int z) const;   // networks, buildings and park pieces refuse the tree tool and block terraforming
  Lcg floraLcg_{99};
  bool simReady_ = false;
  bool paused_ = true;
  int speed_ = 1;
  double simAccum_ = 0;
  bool minuteModeOn_ = false;   // a disaster is active: the clock runs in minutes (tickSim)
  int minutesDone_ = 0;
  int vortexMs_ = 0;            // remaining life of an active whirlpool vortex
  uint32_t minuteAccMs_ = 0;
  // Wall-clock ms accumulated while the sim is running (diagnostics / catch-up).
  uint32_t clockCbAccMs_ = 0;
  MenuTree menu_;
  bool menuLoaded_ = false;
  uint32_t openRoot_ = 0, openFly_ = 0;
  ToolKind tool_ = ToolKind::None;
  int toolArg_ = 0; uint32_t toolOcc_ = 0;
  std::string toolName_;
  bool panelsHidden_ = false;
  bool dragging_ = false; int ax_ = 0, az_ = 0;
  bool uiHit_ = false;
  std::string statusLine_;
  int64_t edFunds_ = 1000000000;
  RenderScene scene_;
  int snapSize_ = 0;
  double viewAcc_ = 0; int viewUpdates_ = 0;   // view updates due this frame at kViewUpdateHz (keyboard / drag scroll)
  uint32_t frameDt_ = 0;   // dt of the current frame (ticker scrolling)
  uint32_t pauseFlashMs_ = 0;   // position in the 1500 ms paused-indicator flash cycle (0 when running)
  bool disastersOn_ = true, autoBudget_ = false;
  // Transport tools greyed until the transportation advisor's tech flag is set (spec 22 s3.10, Q-AK).
  bool transportToolEnabled(uint32_t menuItemId) const;
  bool cheatAllRewards_ = false;   // `pay tribute to your king` (spec 27 s5.3)
  // Cousin Vinnie (spec 22 petition 0xD7, 27 s5.3): answered or expired once per session; refusing sets the flag read by
  // `zyxwvu`. Neither is saved (whether the original saves the refused flag is OPEN).
  bool vinnieDone_ = false, vinnieRefused_ = false;
  bool dirty_ = false;
  int pendingAfterLeave_ = 0;   // leave action to run once the Save As screen has saved (0 = none)

  // windows
  bool showBudget_ = false, showOrd_ = false, showNeigh_ = false, showDeals_ = false, showAdv_ = false,
       showProps_ = false, showGoals_ = false, showMap_ = false,
       showPicker_ = false, showQuery_ = false, showTip_ = false, showLayers_ = false;
  BudgetWindow budgetWin_;
  OrdinancesWindow ordWin_;
  NeighboursWindow neighWin_;
  BusinessDealsWindow dealsWin_;
  AdvisorWindow advWin_;
  CityPropertiesDialog propsDlg_;
  CityPropsState propsState_;
  Image neighRegion_;   // Neighbors Map region image (spec 11 section 9), rebuilt when the window opens
  ScenarioGoalsWindow goalsWin_;
  ViewDataWindow viewData_;   // data maps, charts and graphs in one window (spec 09 7.1)
  CityLayersDialog layersDlg_;
  CityLayers layers_;            // applied View City Layers settings
  DataOverlay layerOverlay_;     // City Layer Views overlay (mode LayerView)
  // async sim state
  bool asyncSim_ = false;
  bool simJobInFlight_ = false;
  bool fastFrame_ = false;       // this frame runs while the tick is still going; it must not touch sim state
  bool streetCacheDirty_ = true;
  uint64_t streetCacheSerial_ = 0;
  SimWorker simWorker_;
  World renderWorld_;            // copy of world_ taken while the sim was idle; what the renderer reads
  HudSnap hud_;
  uint64_t frameNo_ = 0, hudFrame_ = ~0ull;   // hudNow() recomputes live at most once per frame
  int layerOverlayAge_ = 0;      // frames since layerOverlay_ was rebuilt
  TickerWidget ticker_;
  QueryWindow queryWin_;
  QueryInfo queryInfo_;
  BuildingPicker picker_;
  std::vector<PickerEntry> pickerEntries_;
  TipWindow tipWin_;
  CreditsWindow creditsWin_;
  MessageBox msgBox_;
  int msgBoxPurpose_ = 0;   // 0 info, 1 "Now leaving", 2 save before leave, 3 connection, 4 bridge, 5 tunnel
  int msgBoxAction_ = 0;    // leave action the open prompt belongs to
  // Pending network drag waiting on a bridge / tunnel / neighbour-connection dialog (spec 12 §6.4 / §8).
  bool pendingNetAsk_ = false;
  City::NetworkConfirmKind pendingNetAskKind_ = City::NetworkConfirmKind::None;
  NetworkKind pendingNetKind_ = NetworkKind::Road;
  std::vector<std::pair<int, int>> pendingNetPath_;
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
  bool screenPending_ = false;  // Game-side latch for syncScreenPending()
  std::vector<std::string> snapshotNames_;
  int snapSizeIdx_ = 1;
  bool panning_ = false; int panLastX_ = 0, panLastY_ = 0;
  int panAnchorX_ = 0, panAnchorY_ = 0;
  double scrollAcc_ = 0;
  std::string cursorName_;
  void handleScrolling(uint32_t dtMs);
  // Tunables the query panel's service figures need (SC3Tune.ini [PoliceStation] [FireStation] [Library] [Museum]).
  struct QueryTune { bool loaded = false; us::StationParams police, fire; us::LibraryParams library, museum; };
  QueryTune queryTune_;
  void fillServiceValues(QueryInfo& q, const Occupant& o);   // spec 09 6.12.3
  bool isReplaceableOccupant(uint32_t occId);   // in [ReplaceableBuildings] (Replace button of the query panel)
  std::set<uint32_t> replaceableOcc_; bool replaceableLoaded_ = false;
  void buildZoneOverlay();
  std::array<uint32_t, 12> mapRamp_{}; bool mapRampLoaded_ = false;   // map window base colours (palette 000005E5:21 row 4)
  void updateZoneTint(bool on);
  void updatePollutedWater();
  // Parade event (spec 26 5.7): route search, lineup and spectators; the street sim only moves the vehicles.
  bool startParadeEvent();
  void paradeTick();
  bool paradeRunning_ = false; int paradeX_ = 0, paradeZ_ = 0;   // route start of the running parade
  std::map<uint64_t, int> streetSpawners_;   // (kind, x, z) -> street-sim spawner id, kept across source syncs
  uint64_t pollutedWaterSerial_ = ~0ull;
  std::vector<uint8_t> zoneTint_;
  DataOverlay zoneOverlay_;
  void drawSoftCursor(const std::string& name, int x, int y);   // cursor image blitted into the frame at a hotspot
  std::map<std::string, CursorImage> softCur_;
  std::string panCursor_;
  void updateModifierTool();   // holding Alt/Ctrl swaps in the bulldozer/query tool until released
  bool modTool_ = false;       // a modifier tool is standing in for the selected one
  ToolKind modSavedTool_ = ToolKind::None; int modSavedArg_ = 0; uint32_t modSavedOcc_ = 0; std::string modSavedName_;
  std::string cursorForTool() const;
  std::vector<RenderScene> unusedScenes_;
  std::vector<Spot>* spotSink_ = nullptr;
  uint64_t streetSig_ = 0, streetSigSerial_ = ~0ull;   // signature of the city the street sources were built from
  bool streetSyncDue_ = false;
  int streetSyncCooldown_ = 0;   // frames until the next full street-source rebuild is allowed
  bool simDayThisFrame_ = false;   // set by tickSim; street sync skips that frame
  int animTick_ = 0;
  uint32_t animAccMs_ = 0;
  static constexpr uint32_t kAnimPeriodMs = uint32_t(kAnimTickMs);   // ~15 Hz; matches automata::kAnimTickUs
  int shake_ = 0;
  int sweepMinute_ = 0;
  uint32_t newsClickX_ = 0;
  bool introLoadedAudio_ = false;
  int lastWeekDay_ = -1;
  bool petWeekFlag_ = false;   // petitioner manager's "needs update" flag (not saved)
  int autoNameCounter_ = 0;

  // simulation glue
  AdvisorManager advisors_;
  std::unique_ptr<DealHandler> dealHandler_;   // utilities advisor's neighbour-deal hook (game_sim.cpp)
  PetitionerManager petitioners_;
  NewsTicker news_;
  TickerDb tickerDb_;
  std::unique_ptr<NewsTickerSink> sink_;
  uint32_t pauseNewsItem_ = 0;   // live 0x2F9 "Simulation Paused" item (spec 22 4.7.2); withdrawn while running
  GameRng rng_{12345};
  GameRng newsRng_{4242};   // the building subsystem's own generator for building-news variants (spec 22 s4.7.1)
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
    // Async sim: per-tile copies of the City answers, refreshed while the sim is idle (empty = ask the City).
    std::vector<uint8_t> roadPieceCache, densityCache;
    void refreshCaches();
    int liveDensity(int x, int y) const;
    void clearCaches() { roadPieceCache.clear(); densityCache.clear(); }
    int mapWidth() const override { return w ? w->W : 0; }
    int mapHeight() const override { return w ? w->H : 0; }
    bool pieceAt(int x, int y, AutoNet k, PieceRef& out) const override;
    bool isWater(int x, int y) const override;
    bool pedEligible(int x, int y) const override;
    int trafficDensity(int x, int y) const override;
    int groundHeight(int x, int y) const override;
    void gameDate(int& m, int& d) const override { m = month; d = day; }
    std::vector<std::pair<double, double>> seaRouteFrom(int portId, int x, int y) const override;
    void rebuild();
  };
  std::vector<int> streetSources_;
  void syncStreetSources();
  std::unique_ptr<StreetHost> streetHost_;
  std::unique_ptr<StreetSim> street_;
  std::unique_ptr<ScenarioController> scen_;
  std::map<char, uint32_t> hotkeys_;
  HotkeyTable hotkeyTable_;

  // dialogs
  struct FileRow { fs::path path; std::string label; int type = 2; };   // type: 0 parent folder, 1 folder, 2 file
  std::vector<FileRow> files_;
  std::vector<std::string> fileLabels_;
  int fileSel_ = -1, fileScroll_ = 0, fileKind_ = 0, previewFor_ = -2;
  fs::path fileDir_;                    // folder shown in the file dialog
  bool fileComboOpen_ = false;          // "Look In" drop-down
  int fileLastClickRow_ = -1, fileFrame_ = 0, fileLastClickFrame_ = -100;   // double click
  bool saveMode_ = false;
  std::string saveName_;
  struct FilePreview { bool ok = false; std::string name, mayor, date, extra; int64_t population = 0, funds = 0; bool hasStats = false; Image diamond; };   // diamond: view diamond (flat picture for starter towns)
  FilePreview preview_;
  // 0 parent folder, 1 folder, 2 placeholder diamond, 3 placeholder starter-town picture, 4 + kind: document icon per file kind
  Image fileIcons_[8];
  bool fileIconsTried_ = false;
  void openFileDir(const fs::path& dir);
  void openSaveAs();                    // Save As dialog for the current city
  void applySaveNames();
  std::string saveMayor_, saveCity_;    // Save As: editable mayor / city names
  FilePreview savePreview_;             // Save As: the current city (stats + diamond)
  bool showStats_ = true;
  bool confirmOverwrite_ = false;
  std::string nc_name_ = "New City", nc_mayor_ = "Defacto";   // defaults observed (029541F4:282 / 281)
  int nc_size_ = 256, nc_diff_ = 1, nc_year_ = 1900;
  bool nc_disasters_ = true, nc_autoBudget_ = true;
  int nc_landscape_ = 0, nc_flora_ = 0, nc_buildings_ = 0;
  std::vector<uint32_t> schemeIdsL_, schemeIdsF_, schemeIdsB_;
  // Per scheme entry (parallel to schemeIds*_): selector icon instance (group 85535958) and preview sprite id (GAME_UI.DAT).
  std::vector<uint32_t> schemeIconL_, schemeIconF_, schemeIconB_, schemePrevL_, schemePrevF_, schemePrevB_;
  bool schemesLoaded_ = false;
  uint32_t edSeed_ = 1;
  int edHills_ = 64, edWater_ = 64, edTrees_ = 64, edFlags_ = 0x24;   // terrain editor generator parameters (spec 11 3.1)
  int edDrag_ = -1;                                                   // terrain editor slider being dragged
  fs::path currentFile_;
  std::map<std::string, Image> iconCache_;
  const Image* icon(uint32_t group, uint32_t inst);
  void ensureFileIcons();

  // User-made buildings (game_userbldg.cpp): Building Replacement Manager over New City / City Options, and the
  // Building List Editor over it. The set applies to the city being set up / played and is written on save.
  UserBuildingSet userSet_;
  BatDatabase batDb_;
  bool showBrm_ = false, showBle_ = false;
  UserBuildingsDialog brmDlg_;
  UserBuildingsState brmState_;
  BuildingListEditor bleDlg_;
  BuildingListState bleState_;
  fs::path bleDir_;
  void loadBatDb();
  void fillBrmBat();
  void openUserBuildings();
  void openBuildingList();
  void refreshBleFiles();
  void drawUserBuildings();
  void handleUserBuildingsResult(const WindowResult& r, bool editor);
};

}  // namespace m3k
