// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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

PresenterChoice resolvePresenterChoice(const std::function<std::string(uint32_t, uint32_t)>& text, const std::function<std::string(int)>& neighbourMayor,
                                       const ScenarioFile& file, uint32_t id, uint32_t kind) {
  PresenterChoice c;
  const ResolvedPresenter rp = file.resolvePresenter(id, kind);
  if (rp.advisor) { c.advisor = rp.advisorId; return c; }
  c.pic.custom = true;
  if (!rp.found) return c;   // unknown id: no portrait, no name
  c.pic.group = rp.picture.group; c.pic.inst = rp.picture.instance;
  auto str = [&](const ResKey& k) {
    std::string s = text ? text(k.group, k.instance) : std::string();
    if (s.empty() && k.group == file.header.stringTable) { auto it = file.strings.find(k.instance); if (it != file.strings.end()) s = it->second; }
    return s;
  };
  if (rp.neighbour >= 0) c.pic.name = neighbourMayor ? neighbourMayor(rp.neighbour) : std::string();
  else if (rp.nameString.instance) c.pic.name = str(rp.nameString);
  return c;
}

ScenarioGoalsView makeScenarioGoalsView(const std::function<std::string(uint32_t, uint32_t)>& text,
                                        const ScenarioState& st, const ScenarioFile* file,
                                        const ScenarioTextContext& ctx, const std::function<std::string(int)>& neighbourMayor) {
  ScenarioGoalsView v;
  v.inScenario = st.active;
  auto lbl = [&](uint32_t id, const char* fb) {
    std::string s = text ? text(0x041F2625, id) : std::string();
    return s.empty() ? std::string(fb) : s;
  };
  v.title = lbl(0x24, "Scenario goals");
  v.briefingLabel = lbl(3, "Briefing");
  v.goalsLabel = lbl(4, "Goals");
  v.statusLabel = lbl(5, "Scenario Status:");   // the string already ends in a colon
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
  if (file) {
    PresenterChoice pc = resolvePresenterChoice(text, neighbourMayor, *file, file->header.presenter, file->header.presenterKind);
    v.presenterAdvisor = pc.advisor; v.presenterPic = pc.pic;
  }
  v.presenterMood = st.presenterMood ? int(st.presenterMood) : 3;
  v.mayor = ctx.mayor;
  return v;
}

}  // namespace m3k
