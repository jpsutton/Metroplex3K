// In-game windows, dialogs and widgets (spec/09-ui-screens.md sections 4.4, 6, 7; spec/08 section 9).
//
// Every window is a small class (or free function) that draws itself with the immediate-mode Ui each
// frame and returns a WindowResult. The windows hold only their own UI state (page, selection,
// scroll). Simulation state is passed in by reference from the already-merged subsystems; where a
// control edits subsystem state directly (budget funding, tax rate, ordinance enactment, loans) the
// edit is applied AND reported as an event so the shell can play sounds, notify advisors, etc.
//
// Text: every string comes from WinEnv::text(table, id, fallback). With no GameData and no text
// callback the English fallback literal is used, so everything works headless.
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "core/advisors.h"
#include "core/chart.h"
#include "core/economy.h"
#include "core/font.h"
#include "core/gamedata.h"
#include "core/news.h"
#include "core/petitioners.h"
#include "core/population.h"
#include "core/render.h"
#include "core/win_scenario_view.h"
#include "core/settings.h"
#include "core/ui.h"

namespace m3k {

// ---- string tables (spec 09 section 1.1) --------------------------------------------------------
constexpr uint32_t kTblGui = 0x225872FE, kTblMenu = 0x03C09AFF, kTblWindow = 0x029541F4, kTblQuery = 0x82E0074C,
                   kTblMessage = 0x015E410C, kTblApp = 0x209E6378, kTblTod = 0x03C09B1B, kTblCredits = 0x830CDF29,
                   kTblSE = 0x041F2625, kTblCatalog = 0x62E69238, kTblOrdinance = 0xC29A6083;

// ---- environment shared by all windows ---------------------------------------------------------
struct Spot { std::string name; Rect r; };

struct WinEnv {
  GameData* data = nullptr;                                          // optional
  std::function<std::string(uint32_t, uint32_t)> textFn;             // optional override (tests)
  std::function<const Image*(uint32_t group, uint32_t inst)> image;  // UI image provider (optional)
  TextContext tc;                                                    // %MAYOR% / %CITY% ... tokens
  std::string currency = "$";                                        // the game fonts draw "$" as the simoleon sign (observed in the file dialogs)
  // Test hook: when non-null every interactive control appends its (name, rect) here.
  mutable std::vector<Spot>* spots = nullptr;

  // Table string or `fallback` when missing/empty. Tokens (%MAYOR% ...) are expanded.
  std::string text(uint32_t table, uint32_t id, const std::string& fallback) const;
  std::string win(uint32_t id, const std::string& fb) const { return text(kTblWindow, id, fb); }
  std::string query(uint32_t id, const std::string& fb) const { return text(kTblQuery, id, fb); }
  std::string se(uint32_t id, const std::string& fb) const { return text(kTblSE, id, fb); }
  std::string gui(uint32_t id, const std::string& fb) const { return text(kTblGui, id, fb); }
  std::string app(uint32_t id, const std::string& fb) const { return text(kTblApp, id, fb); }
  bool has(uint32_t table, uint32_t id) const;
  std::string money(int64_t v) const;                    // "$1,234" / "-$1,234"
  const Image* img(uint32_t group, uint32_t inst) const { return image ? image(group, inst) : nullptr; }
  void spot(const std::string& n, Rect r) const { if (spots) spots->push_back({n, r}); }
};

// printf-lite: replaces %s / %d (sequentially) and numbered %1 %2 with `args`; "%%" -> "%". Also
// handles the %x% / %y% pair used by the selection-count string.
std::string subst(const std::string& tpl, const std::vector<std::string>& args);

// ---- results ------------------------------------------------------------------------------------
enum class Act {
  Close,              // window asked to close
  OpenBudget, OpenOrdinances, OpenNeighbours, OpenBusinessDeals, OpenAdvisor /*id = advisor*/, OpenPetitioner /*id = petition*/,
  OpenMapWindow,
  SetFunding,         // id = department, a = percent
  SetTax,             // id = group 0..2, a = rate
  IssueBond,          // a = amount, b = 1 accepted / 0 refused
  SetOrdinance,       // id = ordinance, a = 1 enacted / 0 repealed
  AcceptDeal, DeclineDeal,   // id = pending index (neighbour offer) or business-deal index
  CancelDeal,         // id = deal type
  DiscussDeal,        // id = neighbour index
  SelectAdvisor,      // id = advisor
  DismissTopic,       // id = advisor, a = topic
  AnswerPetition,     // id = petition, a = 1 accepted / 0 declined (button 1 / 2)
  DismissPetitioner,  // id = petition
  PrefsChanged,       // text = key
  PreviewMusic,       // a = track index
  PlaySound,          // a = sound id (AUDIO.INI number)
  RestartPrompt,      // restart-only setting changed (message 015E410C:18)
  ApplyCityProps,
  PickBuilding,       // id = occupant id, a = building type (rewards), text = category
  SelectDataMap,      // a = DataLayer
  CenterView,         // a = x, b = z (tile)
  DataMapOptionChanged,
  MessageResult,      // a = MsgButton
  OpenUserBuildings,  // City Options / New City "Select": open the user-made buildings dialog
  TipChanged, ShowTipsChanged,
  TickerClick,        // a = x, b = z (also PlaySound 47; urgent items PlaySound 54)
  ToggleHistorical,   // a = x, b = z
  SetTimeRange,       // a = 0 year / 1 decade / 2 century
  OpenBuildingList,   // Building Replacement Manager "Edit List": open the Building List Editor
  BatRemove,          // Building List Editor "Remove": id = BAT building ID
  BatAdd,             // Building List Editor "Add": text = .bld path
  BatSelectFile,      // Building List Editor: a .bld row was selected (text = path; the shell fills the preview)
  BatOpenFolder,      // Building List Editor: folder row activated (text = path)
  ApplyLayers,        // View City Layers Apply / Default View / OK: the shell applies CityLayersDialog::edit
  ToggleGrid,         // View City Layers grid button
  Done                // OK pressed on a form
};

struct WindowEvent {
  Act act = Act::Close;
  uint32_t id = 0;
  int64_t a = 0, b = 0;
  std::string text;
};

struct WindowResult {
  bool close = false;                  // the shell should close the window
  std::vector<WindowEvent> events;
  void add(Act act, uint32_t id = 0, int64_t a = 0, int64_t b = 0, const std::string& text = "") {
    events.push_back({act, id, a, b, text});
  }
  const WindowEvent* find(Act act) const {
    for (auto& e : events) if (e.act == act) return &e;
    return nullptr;
  }
  bool has(Act act) const { return find(act) != nullptr; }
  void merge(const WindowResult& o) { close = close || o.close; events.insert(events.end(), o.events.begin(), o.events.end()); }
};

// ---- shared drawing helpers (windows_common.cpp) ----------------------------------------------------
namespace wui {
// Centred w x h window, moved by any title-bar drag (Ui::movable); registers the frame in ui.windowRects.
Rect centered(Ui& ui, int w, int h);
// Window chrome with title bar and close button. Returns true when the close button was clicked.
bool frame(Ui& ui, const WinEnv& E, Rect r, const std::string& title);
// Button that registers a test spot.
bool button(Ui& ui, const WinEnv& E, const std::string& name, Rect r, const std::string& label, bool enabled = true, bool toggled = false);
bool check(Ui& ui, const WinEnv& E, const std::string& name, Rect r, const std::string& label, bool& v);
// Coloured-caption button (message box).
bool colourButton(Ui& ui, const WinEnv& E, const std::string& name, Rect r, const std::string& label, uint32_t col);
std::vector<std::string> wrap(const Ui& ui, const std::string& s, int width, const char* font = "main10");
// Draws wrapped text, returns the height used. Stops drawing past maxH (<=0: unlimited).
int paragraph(Ui& ui, Rect r, const std::string& s, const char* font = "main10", uint32_t col = 0);
int lineHeight(const Ui& ui);
void labelValue(Ui& ui, int x, int y, int w, const std::string& label, const std::string& value, uint32_t col = 0);
void rightText(Ui& ui, int right, int y, const std::string& s, const char* font = "main10", uint32_t col = 0);
// Horizontal bar, frac in 0..1.
void bar(Ui& ui, Rect r, float frac, uint32_t col);
// Adjust control of the budget pages: [-] value [+] and a slider. Returns true when value changed.
bool stepper(Ui& ui, const WinEnv& E, const std::string& name, Rect r, int& v, int lo, int hi, int step = 1, const std::string& suffix = "");
// Vertical scrolling helper: clamps `scroll`, handles the wheel and up/down arrow buttons at the right edge.
// Returns the first visible row. `visible` rows fit in `r`.
void scrollBar(Ui& ui, const WinEnv& E, Rect r, int total, int visible, int& scroll);
// Portrait or filler box when no image is available.
void portrait(Ui& ui, Rect r, const Image* im, const std::string& fallbackLetter, uint32_t col);
// Original-look pieces measured on the shipped dialogs:
// sunken outline (dark 7B8AFF top/left, light FFF3FF bottom/right), lines only.
void sunkenLines(Ui& ui, Rect r);
// Blue band with a 1 px lighter ring (title-bar style).
void blueBand(Ui& ui, Rect r);
// White edit field with the original's outline (main9 text, dark ink); keyboard focus by click. `multiLine` wraps the
// text and adds the inner bottom line of the description box. `filter` drops these characters. Returns true if edited.
bool editField(Ui& ui, const WinEnv& E, const std::string& name, Rect r, std::string& val, int focusId, size_t maxLen = 31,
               bool multiLine = false, const char* filter = nullptr);
// Colour sprite from the mounted sprite data blended through its opacity plane (id + 1, 0..31), drawn at
// (ax, ay) minus the sprite's span origin. Missing sprite: nothing drawn.
void spriteAt(Ui& ui, GameData* gd, uint32_t cat, uint32_t id, int ax, int ay);
// City-scheme preview (New City / City Options): landscape, a tree, the house and another tree at the original's
// fixed positions relative to the dialog origin (x, y). Ids are GAME_UI.DAT sprite ids (0 = skip).
void schemePreview(Ui& ui, GameData* gd, int x, int y, uint32_t land, uint32_t flora, uint32_t building);
// Composite-sprite record (frame 0, orientation 0) at the largest zoom that fits maxW x maxH (windows_userbldg.cpp).
bool thumbSprite(GameData* gd, const ResKey& appearance, int maxW, int maxH, uint32_t& cat, uint32_t& id, bool* dbl = nullptr);
// That sprite blended through its opacity plane, centred in `box`, clipped to `clip`.
void spriteCentred(Ui& ui, GameData* gd, const ResKey& appearance, Rect box, Rect clip);
}  // namespace wui

// ---- 6.1 Budget window --------------------------------------------------------------------------------
// Budget indicator strip (spec 09 s6.1): the newest sample of each series of the Graphs window (spec 10 s6.13) and that
// chart's scale maximum: world charts 1-5 (city size, commerce, industry, unemployment %, SimNation population) and the
// pollution chart.
struct IndicatorData {
  uint32_t citySize = 0, citySizeMax = 0;
  uint32_t nationalPop = 0;
  uint32_t commerce = 0, commerceMax = 0, industry = 0, industryMax = 0;
  int unemployment = 0;       // percent
  uint32_t pollution = 0, pollutionMax = 0;
};

struct BudgetView {
  MonthEndInput in;           // current month/year, zone totals, avg land value, ordinance/deal/fares of this month
  IndicatorData ind;
  // Tax-page "approval" (string 1D2): city-aura history chart (spec 09 §6.1 / 17 §8.5), not a tax %.
  // Samples are cityAura+127 (0…254); fresh chart is all 127s. showAuraChart false → widget hidden.
  Chart auraChart;
  bool showAuraChart = false;
};

class BudgetWindow {
 public:
  enum class Page { Main, Dept, Tax, Loans, Indicators };
  Page page = Page::Main;
  uint32_t dept = 0;          // dept:: id on Page::Dept
  int taxGroup = 0;           // 0 R, 1 C, 2 I on Page::Tax
  int newLoanThousands = 5;   // loan amount selector (thousands, multiples of 5)
  std::string loanMessage;
  bool incomeTab = false;     // main page: false = Expenditures tab, true = Income tab

  void openMain() { page = Page::Main; }
  // Dialog size for the current page (the main page and its Loans sub-dialog use the original 500x480 box).
  int boxW() const { return page == Page::Main || page == Page::Loans ? 500 : 620; }
  int boxH() const { return page == Page::Main || page == Page::Loans ? 480 : 460; }
  void openDept(uint32_t id) { page = Page::Dept; dept = id; }
  void openTax(int g) { page = Page::Tax; taxGroup = g; }
  // Edits Budget funding / taxes / loans in place and reports each as an event (SetFunding, SetTax,
  // IssueBond). Details-row buttons emit OpenOrdinances / OpenNeighbours / OpenBusinessDeals; the
  // advisor portrait emits OpenAdvisor(kAdvBudget).
  WindowResult draw(Ui& ui, const WinEnv& E, Budget& b, const BudgetView& v);

  // Figures (exposed for tests): ytd / year-end estimate of every budget line.
  struct Line { std::string key; std::string label; int64_t ytd = 0, est = 0; bool income = false; uint32_t labelId = 0; };
  static std::vector<Line> lines(const WinEnv& E, const Budget& b, const BudgetView& v);
  static constexpr int kMaxFundingUi = 120;

 private:
  // Main page: Expenditures / Income tabs with inline funding sliders (fund.<dept key>) and tax spinners
  // (tax.r/c/i.plus/minus), Adjust-column icons (link.ord / link.deals / link.biz / link.loans), Meet-advisor "?"
  // buttons (details:<line key>), date box (indicators), advisor "?" and Done (ok; Esc also closes).
  void drawMainPage(Ui& ui, const WinEnv& E, Budget& b, const BudgetView& v, Rect r, WindowResult& R);
  // Loans sub-dialog (360x400) over the inert expenditures page: amount.plus/minus, accept, advisor, back (Done).
  void drawLoans(Ui& ui, const WinEnv& E, Budget& b, const BudgetView& v, Rect r, WindowResult& R);
};

// ---- 6.3 Ordinances ----------------------------------------------------------------------------------------
class OrdinancesWindow {
 public:
  int scroll = 0;
  // Lists available ordinances with check boxes. `advisorFilter` != 0 lists only that advisor's ordinances.
  WindowResult draw(Ui& ui, const WinEnv& E, Ordinances& o, const OrdinanceMoneyInput& money, uint32_t advisorFilter = 0, bool framed = true, Rect area = {});
};
std::string ordinanceName(const WinEnv& E, const OrdinanceDef& d);

// ---- 6.4 Business deals -----------------------------------------------------------------------------------------------
class BusinessDealsWindow {
 public:
  int scroll = 0;
  int selType = -1;   // selected deal (building type), -1 none
  // `briefing` = finance briefing text from the financial advisor (may be empty). A pending offer is
  // shown with Accept / Decline buttons (events AcceptDeal / DeclineDeal; the deal is applied in place).
  WindowResult draw(Ui& ui, const WinEnv& E, BusinessDeals& d, const std::string& briefing);
};

// ---- 6.7 Neighbours window and neighbour deals ------------------------------------------------------------------------------
std::string dealKindName(const WinEnv& E, int type);        // 143 sells you power ...
std::string dealRateText(const WinEnv& E, const Deal& d);   // "$10 per 1000 MW-h"
class NeighboursWindow {
 public:
  int selected = -1;
  // Filled by the game before drawing: the player's city and the nation.
  std::string cityName;
  int64_t cityPop = 0;
  uint64_t nationalPop = 0;
  const Image* regionMap = nullptr;   // 256 x 256 region image (spec 11 section 9), may be null
  int viewRotation = 0;               // city view rotation 0-3: turns the diamond (spec 11 section 9.7)
  // Compass placement: index 0 N, 1 E, 2 S, 3 W [OPEN: spec does not say which edge is which side].
  WindowResult draw(Ui& ui, const WinEnv& E, NeighbourDeals& n);
};
// Petitioner-style dialog for the first pending neighbour deal offer (accept / decline).
struct PetitionerView;
PetitionerView makeDealOfferView(const WinEnv& E, const Deal& d, size_t pendingIndex);

// ---- 6.5 Advisor window --------------------------------------------------------------------------------------------------------
class AdvisorWindow {
 public:
  uint32_t selected = 0;      // 0 = overview, else advisor id
  int tab = 0;                // 0 news topics, 1 briefings, 2 ordinances
  int topicSel = -1, scroll = 0;
  Ordinances* ordinances = nullptr;      // optional: enables the ordinances tab list
  OrdinanceMoneyInput money;
  OrdinancesWindow ordWindow;

  void open(uint32_t advisorId = 0) { selected = advisorId; tab = 0; topicSel = -1; scroll = 0; comboOpen = false; }
  WindowResult draw(Ui& ui, const WinEnv& E, AdvisorManager& am, const PetitionerManager& pm);
  bool comboOpen = false;     // "Discuss:" drop-down open
  static std::string advisorName(const WinEnv& E, uint32_t advisorId);   // title, e.g. "City Planner"
  static std::string personName(const WinEnv& E, uint32_t advisorId);    // e.g. "Constance Lee"
  static int advisorIndex(uint32_t advisorId);                  // 1..7 (Utilities, Budget, Planner, Environment, Safety, Transport, Demographics)
  static uint32_t portraitInstance(int advisorIdx, int mood);   // big portrait 52DB60A0:<inst> (92x115 cells)
  static uint32_t smallPortraitInstance(int advisorIdx, int mood);   // 45x56 portrait for the Meet list
  // Visible topics for a tab (0 news = non-persistent, 1 briefings = persistent).
  static std::vector<uint32_t> topicsFor(const TopicList& l, int tab);
};

// ---- 6.6 Petitioner dialog ------------------------------------------------------------------------------------------------------------
struct PetitionerView {
  uint32_t petition = 0;
  const Image* portrait = nullptr;
  std::string name, heading, message, button1, button2, advisorResponse, councilResponse;
  std::string tag;            // test spot prefix
  bool isDeal = false;
  size_t pendingIndex = 0;
};
// Resolves strings through the petition config tables (PetitionText categories).
PetitionerView makePetitionerView(const WinEnv& E, const PetitionerConfig& cfg, const PendingPetition& p, const TextContext* tc = nullptr);

class PetitionerDialog {
 public:
  bool impact = false;        // impact-analysis page showing the advisor/council responses
  // Events: AnswerPetition(id, accepted) / DismissPetitioner(id); for deal offers: AcceptDeal /
  // DeclineDeal(id = pending index).
  WindowResult draw(Ui& ui, const WinEnv& E, const PetitionerView& v);
};

// ---- 6.8 Preferences ------------------------------------------------------------------------------------------------------------------------
struct PrefsState {
  int traffic = 0;            // 0 closest 3 zooms, 1 closest 2, 2 closest, 3 off
  int sims = 0;               // 0 closest 2 zooms, 1 closest, 2 off
  bool otherAnimations = true;
  std::vector<DisplayMode> modes;     // displayModes()
  int resolution = 1;
  int musicVolume = 1024, ambientVolume = 1024, sfxVolume = 1024;   // 0..1024 (SC3.cfg scale)
  bool musicOn = true, ambientOn = true, sfxOn = true, sounds3D = true;
  std::vector<std::pair<std::string, bool>> tracks;                // music choices
  int trackSel = -1;
  bool showTips = true, autoGoToDisasters = false;
  PrefsState() : modes(displayModes()) {}
};
PrefsState prefsFromSettings(const GeneralSettings& g, const Preferences& p);
void prefsToSettings(const PrefsState& s, GeneralSettings& g, Preferences& p);

class PreferencesDialog {
 public:
  int scroll = 0;
  int startResolution = -1;
  bool ambientDirty = false;
  bool resOpen = false;       // resolution drop-down open
  int dragSlider = -1;        // volume slider being dragged (0 music, 1 ambient, 2 effects)
  WindowResult draw(Ui& ui, const WinEnv& E, PrefsState& s);
};

// ---- Engine options (Metroplex3K only, no counterpart in the original) ------------------------------------------------
// Opened from the HUD button right of the help "?" button. Drawn with the original dialog art when the skin has it.
struct EngineOptions {
  bool showFps = false;   // frame counter in the upper-left corner
  static EngineOptions fromPreferences(const Preferences& p);
  void toPreferences(Preferences& p) const;
};

class EngineOptionsDialog {
 public:
  // Edits `s` in place; each change is reported as PrefsChanged (text = key). OK / Esc / close box -> close.
  WindowResult draw(Ui& ui, const WinEnv& E, EngineOptions& s);
};

// ---- 6.9 City properties ----------------------------------------------------------------------------------------------------------------------------------
struct CityPropsState {
  std::string name, mayor, description;
  int landscape = 0, flora = 0, buildings = 0;                    // selected scheme index
  std::vector<std::string> landscapes, floraSets, buildingSets;   // scheme names
  std::vector<uint32_t> landscapeIcons, floraIcons, buildingIcons;      // selector icon instance (group 85535958) per entry
  std::vector<uint32_t> landscapePreview, floraPreview, buildingPreview; // GAME_UI.DAT preview sprite id per entry
  bool disasters = true, autoBudget = false;
  bool editable = true;
};
class CityPropertiesDialog {
 public:
  // Check mark -> Done + ApplyCityProps; close box -> Close. Layout as New City Options (same 639x368 frame).
  WindowResult draw(Ui& ui, const WinEnv& E, CityPropsState& s);
};

// ---- 6.10 Scenario goals --------------------------------------------------------------------------------------------------------------------------------------------------
class ScenarioGoalsWindow {
 public:
  int scroll = 0;
  // The view is built by makeScenarioGoalsView() (windows_scenario.h; separate because scenario.h and
  // news.h cannot be included in one translation unit).
  WindowResult draw(Ui& ui, const WinEnv& E, const ScenarioGoalsView& v);
};

// ---- 6.11 Building picker ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
enum class PickerCategory { PowerPlants, Water, Garbage, Landmarks, Rewards };
constexpr uint32_t kPickerParam[5] = {0x62E925A2, 0xE2E92527, 0xC2F291A9, 0x22E92581, 0x62E92553};   // MenuItem.INI arg1
bool pickerCategoryFromParam(uint32_t param, PickerCategory& out);
struct PickerEntry {
  uint32_t occupantId = 0;
  int buildingType = 0;       // reward building type when the entry comes from the reward table
  std::string name;
  int sizeX = 1, sizeZ = 1;
  int64_t cost = 0;
  const Image* picture = nullptr;
  ResKey appearance;          // occupant appearance (composite-sprite record) for the picture
  uint32_t agentType = 0;
  std::string description;    // description string (attr 0x7C); for landmarks this is the location line
  std::string capacity;       // second info column (e.g. "Power: 6,000 MW-h"); empty when unknown
  bool placed = false;        // landmarks/rewards already placed: greyed, marked "already built"
  bool available = true;
};
// Title / empty-list string ids in table 62E69238 (spec 09 6.11).
uint32_t pickerTitleId(PickerCategory c);
uint32_t pickerEmptyId(PickerCategory c);
// Default agent-type classification of the picker categories (power 12111-12118 + 12853, garbage
// 12852/12853 and 12005 family, landmarks 18000-18045 + 19999-20002, water [OPEN] range).
struct PickerFilter {
  std::vector<std::pair<uint32_t, uint32_t>> ranges;    // inclusive agent-type ranges
  static PickerFilter defaults(PickerCategory c);
  bool match(uint32_t agentType) const;
};
// Enumerates occupant attribute records (Group 0x57E) from GameData, keeps those matching `filter`,
// resolves name (name key string / design name), size and cost. `isAvailable` (optional) applies the
// availability conditions (year, population, rewards earned), `isPlaced` marks landmarks already built.
std::vector<PickerEntry> collectPickerEntries(GameData& data, const PickerFilter& filter, const Budget* budget,
                                              const AgentHierarchy* hier,
                                              const std::function<bool(uint32_t occ, const OccAttr&)>& isAvailable = {},
                                              const std::function<bool(uint32_t occ, const OccAttr&)>& isPlaced = {});
// Reward-table backed list (reward buildings: availability from the table, owned = placed).
// `allUnbuilt` (cheat `pay tribute to your king`): every reward not yet built and not a business-deal building.
std::vector<PickerEntry> rewardPickerEntries(const RewardTable& t, const Budget* budget, const AgentHierarchy* hier,
                                             bool allUnbuilt = false);

class BuildingPicker {
 public:
  PickerCategory category = PickerCategory::PowerPlants;
  int sel = -1, scroll = 0;
  // Events: PickBuilding on choose (also Close).
  WindowResult draw(Ui& ui, const WinEnv& E, const std::vector<PickerEntry>& entries);
};

// ---- 6.12 Query popups -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
enum class QueryClass {
  None, Zoned, Road, Rail, Subway, Station, PowerLine, PowerPlant, WaterBuilding, WaterTreatment, Garbage, Recycling,
  Police, Jail, Fire, Hospital, School, College, Library, Museum, Park, Zoo, Ballpark, Marina, Landmark, Airport,
  Seaport, NeighbourConn
};
// Status flags of the occupant (suffix on the building name).
enum QueryStatus : uint32_t {
  QsConstruction = 1, QsAbandoned = 2, QsGarbage = 4, QsDestroyed = 8, QsStrike = 16, QsAging = 32, QsDefunct = 64,
  QsDecommissioned = 128, QsNoAccess = 256
};
struct QueryInfo {
  int x = 0, z = 0;
  int elevation = 0;                 // metres
  int waterDepth = 0;                // > 0: water tile (depth label 17F)
  bool saltWater = false;
  int zoneCode = 0;                  // spec 09 5.2 zone codes; 0 unzoned
  int landValue = -1;                // byte, -1 = not shown
  int crime = -1, flammability = -1; // bytes, -1 = not shown
  int pollution = -1, pollutionBaseline = 100;   // generated vs baseline b
  int airPollution = -1, waterPollution = -1;    // bytes (6-band strings 188-18D / 18F-194)
  int garbageScaled = -1;                        // Spec 16/17 Q-BT: trunc(G × GarbageScalingFactor×0.001); -1 = hide
  bool powered = false, showPower = false;
  std::string name;                  // building name (string table / design name); empty = bare ground
  uint32_t status = 0;
  QueryClass cls = QueryClass::None;
  bool growable = false, historical = false;     // Make-historical button (zoned buildings)
  int64_t cleanupCost = -1;          // rubble / garbage / destroyed
  int64_t fundingCost = -1;          // per month, funded civic buildings
  // Per-class numbers (see windows_query.cpp table for the meaning of each slot) and 0..255 band bytes.
  double v[8] = {};
  uint8_t band[4] = {};
  int builtYear = 0;                 // landmarks/rewards
  int64_t monthlyIncome = 0;
  int flavour[2] = {};               // flavour string ids (0 none)
  std::string neighbour;             // neighbour connection
};
struct QueryLine { std::string label, value; };
// All popup lines in spec order: terrain -> zone/land value -> occupant -> common status.
std::vector<QueryLine> buildQueryLines(const WinEnv& E, const QueryInfo& q);
// Value band helpers (spec 09 6.12).
uint32_t landValueBandId(int v);      // 0x1C0, 9, A, B, C, D
uint32_t crimeBandId(int c);          // 1C..21
uint32_t flammabilityBandId(int f);   // 23..28
uint32_t pollutionBandId(int p, int b);   // 2A..2F

class QueryWindow {
 public:
  Rect rect{};                // popup position (set by open())
  bool placed = false;
  void open(Rect r) { rect = r; placed = true; }
  // Events: ToggleHistorical (a,b = tile), Close.
  WindowResult draw(Ui& ui, const WinEnv& E, const QueryInfo& q);
};

// ---- 7 Map / data-view window ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
struct DataMapDef {
  DataLayer layer;
  uint32_t titleId;
  uint32_t legendImage;                 // 000005E5:<inst>
  uint32_t lowId = 0, highId = 0;       // end labels (continuous)
  std::vector<uint32_t> categories;     // categorical labels
  uint32_t toggleId = 0;                // coverage toggle (crime police, fire station coverage)
  std::vector<uint32_t> selector;       // pollution selector (air, water, garbage)
};
const std::vector<DataMapDef>& dataMapDefs();

struct MapSource {
  int W = 0, H = 0;
  // Per-layer tile values (W*H, index x*H+z, spec 25 9.2 layout); nullptr = layer unavailable.
  std::function<const std::vector<uint8_t>*(DataLayer)> values;
  std::function<uint32_t(int x, int z)> baseColour;     // terrain mini-map colour (optional)
};

class MapWindow {
 public:
  int selected = -1;                    // index into dataMapDefs(); -1 = plain map
  bool showInCityView = true;
  bool coverage = false;                // police/fire coverage toggle
  int pollutionKind = 0;                // 0 air 1 water 2 garbage
  // Overlay for the main view: valid when a data map is selected and "show in city view" is on.
  const DataOverlay* activeOverlay() const { return overlayActive_ ? &overlay_ : nullptr; }
  // Events: SelectDataMap, CenterView (click on mini-map), OpenNeighbours, DataMapOptionChanged.
  WindowResult draw(Ui& ui, const WinEnv& E, const MapSource& src);
  void select(int index, const MapSource& src);
  static uint32_t legendColour(DataLayer l, int category, float t);   // ramp used by mini-map + legend
 private:
  DataOverlay overlay_;
  bool overlayActive_ = false;
  std::vector<uint32_t> mini_;
  int miniW_ = 0, miniH_ = 0;
  void rebuild(const MapSource& src);
};

// ---- 7.2 View City Layers dialog (navigator layers button) ---------------------------------------------------------------
// What the city view shows (spec 09 7.2). Three exclusive modes picked by the group header buttons: above-ground object
// classes (check boxes), one underground network (radio), or one data layer recolouring the ground (radio).
struct CityLayers {
  enum Mode { AboveGround = 0, Underground = 1, LayerView = 2 };
  int mode = AboveGround;
  bool transport = true, powerLines = true, flora = true, zonedBuildings = true, otherBuildings = true, zones = true;
  int underground = 1;   // 0 subways, 1 water pipes
  int layer = 0;         // index into cityLayerViews()
  bool operator==(const CityLayers& o) const {
    return mode == o.mode && transport == o.transport && powerLines == o.powerLines && flora == o.flora &&
           zonedBuildings == o.zonedBuildings && otherBuildings == o.otherBuildings && zones == o.zones &&
           underground == o.underground && layer == o.layer;
  }
  bool operator!=(const CityLayers& o) const { return !(*this == o); }
};
// The eight City Layer Views radio entries in dialog order: data layer and caption (029541F4:276..27D).
struct CityLayerView { DataLayer layer; uint32_t labelId; const char* fallback; };
const std::vector<CityLayerView>& cityLayerViews();

// 296x336 dialog measured on dialog-shots/hud--minimap-layers-button--view-city-layers.png. The controls edit `edit`;
// the shell copies it into the view on ApplyLayers. Events: ApplyLayers (Apply; Default View after resetting `edit`;
// the check mark, followed by Done), ToggleGrid (grid button), Close (close box / Esc, nothing applied).
class CityLayersDialog {
 public:
  CityLayers edit;
  WindowResult draw(Ui& ui, const WinEnv& E, bool gridOn);
};

// ---- 7.3 Graphs window -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
struct GraphSeries { uint32_t labelId = 0; std::string fallback; const Chart* chart = nullptr; };
// The 14 series of spec 09 7.3 in order with labels filled and chart == nullptr (caller sets the charts).
std::vector<GraphSeries> graphSeriesTemplate();
class GraphsWindow {
 public:
  int range = 0;                        // 0 = 12 months, 1 = 10 years, 2 = 10 decades
  uint32_t enabledMask = 0x3FFF;        // bit per series
  // Values oldest -> newest for a chart in a range.
  static std::vector<uint32_t> samples(const Chart& c, int range);
  WindowResult draw(Ui& ui, const WinEnv& E, const std::vector<GraphSeries>& series);
};

// ---- 7.4 Charts window ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
struct ChartsData {
  std::vector<float> eq, lifeExpectancy, population;   // per age 0..89
  float populationEQ = 0, workforceEQ = 0, populationLE = 0, workforceLE = 0;
  int64_t totalPopulation = 0; int workforcePercent = 0;
  double electricity[10] = {};          // coal, oil, gas, wind, solar, nuclear, microwave, fusion, WTE, neighbour deals
  double garbage[5] = {};               // landfill, deals, incinerators, recycling, streets
};
ChartsData chartsFromPopulation(const Population& p, uint32_t cityPopulation);
class ChartsWindow {
 public:
  int tab = 0;                          // 0 education 1 health 2 population 3 electricity 4 garbage
  WindowResult draw(Ui& ui, const WinEnv& E, const ChartsData& d);
};

// ---- 4.4 news ticker strip ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
class TickerWidget {
 public:
  struct Item { NewsItem n; int x = 0; int gap = TickerScroll::kGapPixels; };
  std::vector<Item> items;
  int accumMs = 0;
  bool alarm = false;                          // an urgent (class 1) item entered: draw() emits PlaySound(54) once
  // Advance by dtMs: step 2 px each time more than 50 ms elapsed, and pull the next item from `t`
  // when the strip has room (items separated by 20 px). `width` = strip width.
  // `paused`: keep scrolling, but fill the strip with repeated "Simulation Paused" copies (gap of ~4
  // characters) instead of pulling news from `t`.
  void update(uint32_t dtMs, int width, const Ui& ui, NewsTicker* t, int32_t today, GameRng* rng, bool paused = false);
  void push(const NewsItem& n, int width);     // append at the right edge / after the last item (glyph-count width estimate)
  void push(const NewsItem& n, int width, const Ui& ui);   // same with measured text width
  // Draws the strip inside r. Click on an item with a map location -> TickerClick (and PlaySound 47).
  WindowResult draw(Ui& ui, const WinEnv& E, Rect r);
};

// ---- 9.1 message box ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
enum class MsgButton { Abort, Cancel, Ignore, No, OK, Retry, Yes };
class MessageBox {
 public:
  bool active = false;
  // Icon cells of the SYS MBIcons sheet (82B9B75B:42E55FD4), measured on the art.
  enum Icon { NoIcon = -1, IconError = 0, IconWarning = 1, IconInfo = 2, IconQuestion = 3 };
  void open(const std::string& title, const std::string& message, int style = 0, int icon = IconWarning);   // style 0..5
  // Layout of spec 08 9.1 (exposed for tests). W/H = wrapped text block; the rest is the frame measured on the
  // original (dialog-relative): title band, sunken body panel with the icon at its left, blue button band.
  struct Layout {
    int W = 0, H = 0, buttonY = 0, buttonX0 = 0, base = 0;
    int boxW = 0, boxH = 0, bodyY = 0, bodyH = 0, textX = 0, bandY = 0;
    std::vector<std::string> lines;
  };
  Layout layout(const Ui& ui) const;
  static std::vector<MsgButton> buttonsFor(int style);
  // Event MessageResult(a = MsgButton). Enter = first button, Esc / close box = last.
  WindowResult draw(Ui& ui, const WinEnv& E);
 private:
  std::string title_, message_;
  int style_ = 0, icon_ = IconWarning;
};
std::string msgButtonCaption(const WinEnv& E, MsgButton b);

// ---- user-made buildings: Building Replacement Manager / Building List Editor (spec 24 section 2.5) -------------------
// One building shown by the dialogs: a BAT.DAT building (buildingId != 0) or a replaceable occupant (key = its TKB1 key).
struct UbBuilding {
  ResKey key;                 // replaceable occupant key (Type, Group, Instance); BAT: the appearance key
  ResKey appearance;          // composite-sprite key (BAT.DAT buildings: 00006300:BA1DBF1E:x)
  std::string name, author;
  uint32_t buildingId = 0;
  int lot = 1;                // square footprint (tiles)
  int zone = 0;               // filter class 0..4 (residential, commercial, industrial, utility/transport, civic/special)
};
// Filter class of an occupant agent type (the five zone filter buttons).
int ubZoneClass(uint32_t agentType);
// Pair list in the shape of bat.h's UserBuildingSet (kept local so windows.h stays independent of bat.h).
struct UbPairs {
  std::vector<std::pair<ResKey, ResKey>> pairs;   // (replaced occupant key, replacement appearance)
  const ResKey* find(const ResKey& k) const { for (auto& p : pairs) if (p.first == k) return &p.second; return nullptr; }
  void set(const ResKey& k, const ResKey& a) { for (auto& p : pairs) if (p.first == k) { p.second = a; return; } pairs.push_back({k, a}); }
  bool revert(const ResKey& k) { for (size_t i = 0; i < pairs.size(); i++) if (pairs[i].first == k) { pairs.erase(pairs.begin() + long(i)); return true; } return false; }
};

struct UserBuildingsState {
  std::vector<UbBuilding> bat;           // BAT.DAT buildings
  std::vector<UbBuilding> replaceable;   // replaceable occupants whose attributes resolve
  int totalReplaceable = 0;              // entries listed in [ReplaceableBuildings] ("of %y%")
  UbPairs set;                           // working copy (Done applies it, the close box discards it)
  int zoneFilter = 0, sizeFilter = 0;    // filter button selected in each group (0..4; size = lot - 1)
  int sel = 0;                           // index into filtered() of the building in the lower preview box
  int batSel = -1, batScroll = 0;        // index into bat (-1 none)
  // Indices into replaceable passing the filters / into bat with the lot size of the selected replaceable building.
  std::vector<int> filtered() const;
  std::vector<int> batChoices() const;
  const UbBuilding* selected() const;
};

// Building Replacement Manager (635x478, measured on dialog-shots/main-menu--start-new-city--user-made-buildings-select.png).
// Replace (green arrow) pairs the selected replaceable building with the selected BAT building, Revert removes the pair,
// Clear Set asks (041F2625:141) and clears. Events: Done (check mark; apply `set`), Close (close box; discard),
// OpenBuildingList ("Edit List"). Import Set is not implemented (shows a note).
class UserBuildingsDialog {
 public:
  WindowResult draw(Ui& ui, const WinEnv& E, UserBuildingsState& s);
  MessageBox prompt;    // Clear Set confirmation / notes, drawn on top
 private:
  int promptKind_ = 0;  // 1 clear-set confirmation
};

struct BuildingListState {
  std::vector<UbBuilding> active;        // BAT.DAT buildings ("Active List")
  struct File { std::string label, path; int type = 2; };   // type 0 parent folder, 1 folder, 2 .bld
  std::vector<File> files;
  std::string folder = "Buildings";      // "Look in" caption
  int activeSel = -1, activeScroll = 0, fileSel = -1, fileScroll = 0;
  bool previewFile = false;              // preview shows the selected .bld (else the selected active building)
  Image fileThumb;                       // selected .bld thumbnail (filled by the shell)
  int fileLot = 0;
  const Image* icons[3] = {};            // parent folder, folder, document
};

// Building List Editor (515x345, dialog-shots/...--edit-list.png). Events: BatRemove (id), BatAdd (text = path),
// BatSelectFile, BatOpenFolder (text = path), Done / Close.
class BuildingListEditor {
 public:
  WindowResult draw(Ui& ui, const WinEnv& E, BuildingListState& s);
 private:
  int lastClickRow_ = -1, lastClickFrame_ = -100, frame_ = 0;
};

// ---- 6.14 Tip of the Day, 6.15 Credits ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
class TipWindow {
 public:
  int tip = 1;
  bool showAtStartup = true;
  static int next(int t);               // 1..0x39 skipping 0x10, wrapping
  static int prev(int t);
  WindowResult draw(Ui& ui, const WinEnv& E);
};

class CreditsWindow {
 public:
  double scroll = 0;                    // pixels
  double speed = 30;                    // px per second [OPEN rate]
  std::vector<std::string> lines;       // filled on first draw from table 830CDF29 (ids ascending); raw, style digit included
  std::vector<uint32_t> ids;            // string id of each entry in `lines`
  // Laid-out row. Table ids 1..199 are left-hand entries (heading / role); id + 199 holds the matching name(s).
  struct Row {
    enum Kind { Blank, Heading, Centered, Pair } kind = Blank;
    std::string left;                   // heading / role / centred text, style digit removed
    std::vector<std::string> names;     // Pair only: one per line
    int lineCount() const { return kind == Pair ? std::max<int>(1, int(names.size())) : 1; }
  };
  std::vector<Row> rows;
  static std::vector<Row> layoutRows(const std::vector<uint32_t>& ids, const std::vector<std::string>& lines);
  void update(uint32_t dtMs) { scroll += speed * dtMs / 1000.0; }
  WindowResult draw(Ui& ui, const WinEnv& E);
};

}  // namespace m3k
