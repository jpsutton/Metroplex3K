// Plain data for the scenario goals window (spec 09 6.10). Kept free of heavy includes so that both
// windows.h (news.h world) and windows_scenario.h (scenario.h world) can use it.
#pragma once
#include <string>
#include <vector>

namespace m3k {

struct ScenarioGoalsView {
  bool inScenario = true;
  std::string title, briefingLabel, goalsLabel, statusLabel;
  std::string briefing;
  struct Goal { std::string text; bool met = false; };
  std::vector<Goal> goals;
  std::string status;        // e.g. "2 of 3 goals met"
};

}  // namespace m3k
