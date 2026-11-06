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
  // Spec 23 4.7.1: presenter panel (advisor portrait and name) and the start-dialog variant.
  uint32_t presenterAdvisor = 0;   // advisor id, 0 = none
  int presenterMood = 3;
  bool startMode = false;          // "Greetings Mayor:" with the mayor's name instead of "Scenario Status:"
  std::string mayor;
};

// A scenario pop-up (cmdPopUpMsg): title is the presenter's dialog title.
struct ScenarioPopupView {
  std::string text;
  uint32_t presenterAdvisor = 0;
  int mood = 3;
};

// The results dialog of cmdEndScenario (spec 23 4.7.1).
struct ScenarioEndView {
  std::string title, heading, resultsFor, text;
  std::vector<std::string> ranks;
  int achieved = -1;               // index into ranks, -1 = none
  uint32_t presenterAdvisor = 0;
  int mood = 3;
  std::string captions[3];         // replay, another scenario, main menu
};

}  // namespace m3k
