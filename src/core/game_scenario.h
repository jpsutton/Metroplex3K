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
  std::function<void(const std::string& text, int advisorIdx, uint32_t advisorId, int mood, bool important)> popUp;
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
};

struct ScenarioInfo {
  std::string title, description, author;
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
  void setPopupOpen(bool open);
  ScenarioGoalsView goalsView() const;
  struct Outcome { bool ended = false, win = false; int rank = -1; std::string rankTitle, text; int goalsMet = 0, goalsTotal = 0; };
  Outcome outcome() const;
  std::string title() const;
  std::string briefing() const;
  Bytes mergeState(const Bytes& cityFile) const;   // city file with scenario records + state (for saves)
 private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};

}  // namespace m3k
