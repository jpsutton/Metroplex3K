// Scenario controller used by Game (spec 23). Separate header/TU because scenario.h / script.h share names
// (GameRng, Script, TickerStyle) with news.h / automata.h; this header only uses plain types.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/city.h"
#include "core/win_scenario_view.h"

namespace m3k {

struct ScenarioHooks {
  // Resolved text; advisorIdx 1..7 (0 = custom presenter), advisorId raw id, mood 1..5.
  std::function<void(const std::string& text, int advisorIdx, uint32_t advisorId, int mood, bool important, const ScenarioPresenterPic& pic)> popUp;
  std::function<std::string(int neighbour)> neighbourMayor;   // kind-0 presenters 0x14..0x17
  std::function<void(const std::string& text, int cls, bool underline)> ticker;
  std::function<void(int soundId)> sound;
  std::function<void(int x, int z, int zoom)> moveCamera;
  std::function<void(int speed)> setSpeed;
  std::function<void(int kind /*DisasterKind*/, const std::vector<int32_t>& params)> startDisaster;
  std::function<void(bool)> setDisastersEnabled;
  std::function<void(int dealType, bool blocked)> blockDeals;
  std::function<void(uint32_t buildingType)> offerReward;
  std::function<void(int dealId)> offerBusiness;
  std::function<void(int dealId, bool enabled)> enableBusiness;
  std::function<void(int rank)> ended;
  std::function<std::string(uint32_t group, uint32_t id)> text;   // game string tables (may return "")
  std::function<uint32_t(uint32_t pieceId)> pieceAgentType;       // agent type of a network piece (0 = unknown), for structure counts
};

struct ScenarioInfo {
  std::string title, description, author;
  std::string shortAuthor;   // 2026960B:19A6CEA1:2, the name the chooser shows ("Maxis")
  Bytes picture;             // chooser picture (BMP), may be empty
};

class ScenarioController {
 public:
  ScenarioController();
  ~ScenarioController();
  // Parse .SNR / city file image. False when it holds no scenario.
  bool load(const Bytes& file, std::string& err);
  // Chooser info (title, description, author) without keeping state.
  static bool peek(const Bytes& file, const std::function<std::string(uint32_t, uint32_t)>& text, ScenarioInfo& out);
  void start(City& city, const std::string& mayor, const std::string& cityName, ScenarioHooks hooks);
  void dayTick();
  void minuteTick();
  bool active() const;
  bool started() const;
  bool resumed() const;   // the file carried saved scenario state (a city saved mid-scenario)
  void setPopupOpen(bool open);
  bool scriptWaiting() const;   // the script is suspended inside a pop-up command
  void setMayor(const std::string& m);   // %MAYOR% / %YOURNAME% in scenario text
  void popupClosed();           // the player closed the pop-up: the script continues (and may queue the next one)
  ScenarioGoalsView goalsView() const;
  struct Outcome {
    bool ended = false, win = false; int rank = -1; std::string rankTitle, text; int goalsMet = 0, goalsTotal = 0;
    std::vector<std::string> ranks;   // every rank line (cmdAddRankInfo), in id order
    int achieved = -1;                // index of the achieved rank in `ranks`
    uint32_t presenterAdvisor = 0; int mood = 3;
    ScenarioPresenterPic presenterPic;
    std::string title, mayor, resultsFor;
  };
  Outcome outcome() const;
  std::string title() const;
  std::string briefing() const;
  Bytes mergeState(const Bytes& cityFile) const;   // city file with scenario records + state (for saves)
 private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};

}  // namespace m3k
