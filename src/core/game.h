// Game shell: screens (main menu, new city, load/save, in-city HUD, windows) built on the Ui toolkit.
// Platform independent: the host feeds Input each frame and presents screen().
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core/flora.h"
#include "core/font.h"
#include "core/terraform.h"
#include "core/terrain_gen.h"
#include "core/gamedata.h"
#include "core/render.h"
#include "core/audio.h"
#include "core/city.h"
#include "core/sim.h"
#include "core/ui.h"
#include "core/world.h"

namespace m3k {

struct MenuItemInfo { bool enabled = true; uint32_t nameTable = 0, nameId = 0; std::string kind; uint32_t target = 0, arg1 = 0, arg2 = 0; };
struct MenuButton { uint32_t id = 0, item = 0, sub = 0; };
struct MenuDesc { uint32_t id = 0, titleTable = 0, titleId = 0, colour = 0; std::vector<uint32_t> buttons; };

struct MenuTree {
  std::map<uint32_t, MenuItemInfo> items;
  std::map<uint32_t, MenuButton> buttons;
  std::map<uint32_t, MenuDesc> menus;
  bool parse(const IniFile& ini);
};

enum class Screen { NeedData, MainMenu, NewCity, LoadList, InCity };
enum class ToolKind { None, Terrain, Zone, Road, Demolish, Plop, Query, Unavailable };

class Game {
 public:
  bool init(const fs::path& gameRoot, std::string& err);  // empty root => NeedData screen
  void frame(const Input& in, int w, int h, uint32_t dtMs);
  const std::vector<uint32_t>& screen() const { return screen_; }
  bool wantQuit() const { return quit_; }
  void dropPath(const std::string& path);
  void requestFile(const std::string& path);  // open a city file directly
  std::string windowTitle() const;
  // Audio hook: null until game data with Res/Sound/AUDIO.INI is open (silent). The host's audio callback
  // calls audio()->mixAudio() under its own lock (the same lock must be held around frame()).
  AudioSystem* audio() { return audio_.get(); }
  Sim& sim() { return sim_; }
  City& city() { return sim_.city(); }

 private:
  // screens
  void drawNeedData();
  void drawMainMenu();
  void drawNewCity();
  void drawLoadList();
  void drawInCity(uint32_t dtMs);
  void drawHud();
  void drawToolMenu();
  void drawWindows();
  void drawBudget(); void drawQuery(); void drawMessage();
  void handleMap();
  // actions
  void startNewCity();
  void loadFile(const fs::path& p);
  void saveCurrent(const std::string& name);
  void adopt(World&& w);
  void refreshFiles(int kind);
  void execute(uint32_t menuItemId);
  void dispatch(const MenuItemInfo& mi);
  void message(const std::string& s) { msgText_ = s; }
  std::string str(uint32_t table, uint32_t id);
  void setTool(ToolKind k, int a = 0, uint32_t occ = 0);
  void tickSim(uint32_t dtMs);
  void ensureFonts();
  void initAudio();
  void openPicker(uint32_t category);
  void drawPicker();
  void sound(int id) { if (audio_) audio_->request(id); }

  fs::path root_;
  GameData data_;
  bool haveData_ = false;
  Fonts fonts_;
  Ui ui_;
  std::vector<uint32_t> screen_;
  int W_ = 0, H_ = 0;
  Screen scr_ = Screen::NeedData;
  bool quit_ = false;
  std::string pathInput_;
  std::string msgText_;
  Image menuBg_;
  bool menuBgTried_ = false;

  // in-city
  World world_;
  Renderer renderer_;
  Camera cam_;
  RenderOptions ropt_;
  CityConfig cityCfg_;
  std::unique_ptr<AudioSystem> audio_;
  // building picker (power plants, water structures, garbage)
  struct PickItem { uint32_t occ; };
  std::vector<PickItem> pickItems_;
  std::vector<std::string> pickLabels_;
  bool showPicker_ = false;
  int pickSel_ = -1, pickScroll_ = 0;
  std::string pickTitle_;
  Sim sim_;
  std::unique_ptr<Terraformer> terra_;
  Lcg floraLcg_{99};
  bool simReady_ = false;
  bool paused_ = true;
  int speed_ = 1;
  double simAccum_ = 0;
  int cityAge_ = 0;
  MenuTree menu_;
  bool menuLoaded_ = false;
  uint32_t openRoot_ = 0, openFly_ = 0;
  ToolKind tool_ = ToolKind::None;
  int toolArg_ = 0; uint32_t toolOcc_ = 0;
  std::string toolName_;
  bool dragging_ = false; int ax_ = 0, az_ = 0;
  bool panning_ = false;
  bool showBudget_ = false, showQuery_ = false;
  std::string queryText_;
  bool uiHit_ = false;
  std::vector<std::string> ticker_;
  std::string statusLine_;

  // dialogs
  struct FileRow { fs::path path; std::string label; };
  std::vector<FileRow> files_;
  std::vector<std::string> fileLabels_;
  int fileSel_ = -1, fileScroll_ = 0, fileKind_ = 0;
  bool saveMode_ = false;
  std::string saveName_;
  std::string nc_name_ = "New City", nc_mayor_ = "Mayor";
  int nc_size_ = 128, nc_diff_ = 1, nc_year_ = 2000;
  fs::path currentFile_;
  std::map<std::string, Image> iconCache_;
  const Image* icon(uint32_t group, uint32_t inst);
};

}  // namespace m3k
