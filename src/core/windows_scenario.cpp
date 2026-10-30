#include "core/windows_scenario.h"

namespace m3k {

namespace {
std::string resolve(const std::function<std::string(uint32_t, uint32_t)>& text, const ScenarioFile* file, StringRef r,
                    const ScenarioTextContext& ctx) {
  if (r.id == 0 && r.group == 0) return "";
  std::string s = text ? text(r.group, r.id) : std::string();
  if (s.empty() && file) {
    auto it = file->strings.find(r.id);
    if (it != file->strings.end()) s = it->second;
  }
  if (s.empty()) return "";
  return expandScenarioTokens(s, ctx);
}
}  // namespace

ScenarioGoalsView makeScenarioGoalsView(const std::function<std::string(uint32_t, uint32_t)>& text,
                                        const ScenarioState& st, const ScenarioFile* file,
                                        const ScenarioTextContext& ctx) {
  ScenarioGoalsView v;
  v.inScenario = st.active;
  auto lbl = [&](uint32_t id, const char* fb) {
    std::string s = text ? text(0x041F2625, id) : std::string();
    return s.empty() ? std::string(fb) : s;
  };
  v.title = lbl(0x24, "Scenario goals");
  v.briefingLabel = lbl(3, "Briefing");
  v.goalsLabel = lbl(4, "Goals");
  v.statusLabel = lbl(5, "Status");
  v.briefing = resolve(text, file, st.dialogText, ctx);
  for (auto& extra : st.dialogExtra) {
    std::string e = resolve(text, file, extra, ctx);
    if (!e.empty()) v.briefing += (v.briefing.empty() ? "" : "\n") + e;
  }
  int met = 0;
  for (auto& kv : st.goals) {
    ScenarioGoalsView::Goal g;
    g.text = resolve(text, file, kv.second.text, ctx);
    if (g.text.empty()) g.text = "Goal " + std::to_string(kv.first);
    g.met = kv.second.met;
    met += g.met;
    v.goals.push_back(g);
  }
  v.status = std::to_string(met) + " of " + std::to_string(v.goals.size()) + " goals met";
  return v;
}

}  // namespace m3k
