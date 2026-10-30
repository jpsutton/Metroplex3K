// Preferences, city properties, scenario goals, building picker (spec 09 sections 6.8-6.11).
#include <algorithm>
#include "core/windows.h"

namespace m3k {

using namespace wui;

// ================================================================ Preferences
PrefsState prefsFromSettings(const GeneralSettings& g, const Preferences& p) {
  PrefsState s;
  s.musicOn = g.musicOn; s.musicVolume = g.musicVolume;
  s.sfxOn = g.sfxOn; s.sfxVolume = g.sfxVolume;
  s.ambientOn = g.ambienceOn; s.ambientVolume = g.ambienceVolume;
  s.resolution = 1;
  for (size_t i = 0; i < s.modes.size(); i++)
    if (int(g.width) == s.modes[i].w && int(g.height) == s.modes[i].h) s.resolution = int(i);
  s.traffic = int(std::max<long long>(0, std::min<long long>(3, p.getInt("trafficVisible", 0))));
  s.sims = int(std::max<long long>(0, std::min<long long>(2, p.getInt("simsVisible", 0))));
  s.otherAnimations = p.getBool("otherAnimations", true);
  s.sounds3D = p.getBool("sound3d", true);
  s.showTips = p.getBool("showTips", true);
  s.autoGoToDisasters = p.getBool("autoGoToDisasters", false);
  return s;
}

void prefsToSettings(const PrefsState& s, GeneralSettings& g, Preferences& p) {
  g.musicOn = s.musicOn; g.musicVolume = s.musicVolume;
  g.sfxOn = s.sfxOn; g.sfxVolume = s.sfxVolume;
  g.ambienceOn = s.ambientOn; g.ambienceVolume = s.ambientVolume;
  if (s.resolution >= 0 && s.resolution < int(s.modes.size())) { g.width = uint32_t(s.modes[size_t(s.resolution)].w); g.height = uint32_t(s.modes[size_t(s.resolution)].h); }
  p.setInt("trafficVisible", s.traffic);
  p.setInt("simsVisible", s.sims);
  p.setBool("otherAnimations", s.otherAnimations);
  p.setBool("sound3d", s.sounds3D);
  p.setBool("showTips", s.showTips);
  p.setBool("autoGoToDisasters", s.autoGoToDisasters);
}

WindowResult PreferencesDialog::draw(Ui& ui, const WinEnv& E, PrefsState& s) {
  WindowResult R;
  int lh = lineHeight(ui);
  Rect r = centered(ui, 620, 470);
  if (startResolution < 0) startResolution = s.resolution;
  if (frame(ui, E, r, E.win(0x14E, "Preferences"))) { R.close = true; R.add(Act::Close); }
  auto changed = [&](const char* key) { R.add(Act::PrefsChanged, 0, 0, 0, key); };
  auto radio = [&](const std::string& name, Rect rc, const std::string& lbl, int& var, int val, const char* key) {
    if (button(ui, E, name, rc, lbl, true, var == val)) { if (var != val) { var = val; changed(key); } }
  };
  int lx = r.x + 14, y = r.y + 34;
  // ---- Performance
  ui.text(lx, y, E.win(0x1FF, "Performance"), "title12");
  y += lh + 6;
  ui.text(lx, y, E.win(0x200, "Traffic visible"));
  y += lh + 2;
  const uint32_t tr[4] = {0x201, 0x202, 0x203, 0x20A};
  const char* trFb[4] = {"Closest 3 zooms", "Closest 2 zooms", "Closest zoom", "Off"};
  for (int i = 0; i < 4; i++) radio("traffic." + std::to_string(i), {lx + (i % 2) * 140, y + (i / 2) * 24, 136, 22}, E.win(tr[i], trFb[i]), s.traffic, i, "traffic");
  y += 54;
  ui.text(lx, y, E.win(0x207, "Sims visible"));
  y += lh + 2;
  const uint32_t sm[3] = {0x208, 0x209, 0x20A};
  const char* smFb[3] = {"Closest 2 zooms", "Closest zoom", "Off"};
  for (int i = 0; i < 3; i++) radio("sims." + std::to_string(i), {lx + (i % 2) * 140, y + (i / 2) * 24, 136, 22}, E.win(sm[i], smFb[i]), s.sims, i, "sims");
  y += 54;
  ui.text(lx, y, E.win(0x204, "Other animations"));
  y += lh + 2;
  int oa = s.otherAnimations ? 1 : 0;
  radio("anim.on", {lx, y, 136, 22}, E.win(0x205, "On"), oa, 1, "otherAnimations");
  radio("anim.off", {lx + 140, y, 136, 22}, E.win(0x206, "Off"), oa, 0, "otherAnimations");
  s.otherAnimations = oa != 0;
  y += 32;
  ui.text(lx, y, E.win(0x20B, "Screen resolution"));
  y += lh + 2;
  if (button(ui, E, "res.prev", {lx, y, 24, 22}, "<") && s.resolution > 0) { s.resolution--; changed("resolution"); }
  if (!s.modes.empty()) {
    const DisplayMode& m = s.modes[size_t(std::max(0, std::min(int(s.modes.size()) - 1, s.resolution)))];
    ui.textCentered({lx + 28, y, 120, 22}, std::to_string(m.w) + "x" + std::to_string(m.h));
  }
  if (button(ui, E, "res.next", {lx + 152, y, 24, 22}, ">") && s.resolution + 1 < int(s.modes.size())) { s.resolution++; changed("resolution"); }
  y += 40;
  // ---- General
  ui.text(lx, y, E.win(0x2D0, "General"), "title12");
  y += lh + 6;
  bool v = s.showTips;
  if (check(ui, E, "showTips", {lx, y, 280, 18}, E.win(0x293, "Show tips at startup"), v)) { s.showTips = v; changed("showTips"); }
  v = s.autoGoToDisasters;
  if (check(ui, E, "autoDisaster", {lx, y + 22, 280, 18}, E.win(0x21F, "Auto go to disasters"), v)) { s.autoGoToDisasters = v; changed("autoGoToDisasters"); }

  // ---- Sound (right column)
  int rx = r.x + 320;
  y = r.y + 34;
  ui.text(rx, y, E.win(0x292, "Sound"), "title12");
  y += lh + 6;
  struct Vol { const char* key; uint32_t id; const char* fb; int* vol; bool* on; };
  Vol vols[3] = {{"music", 0x153, "Music", &s.musicVolume, &s.musicOn}, {"ambient", 0x219, "Ambient sound", &s.ambientVolume, &s.ambientOn},
                 {"sfx", 0x14F, "Sound effects", &s.sfxVolume, &s.sfxOn}};
  for (auto& vo : vols) {
    bool on = *vo.on;
    if (check(ui, E, std::string(vo.key) + ".on", {rx, y, 200, 18}, E.win(vo.id, vo.fb), on)) { *vo.on = on; changed(vo.key); }
    Rect sl{rx + 20, y + 20, 240, 14};
    E.spot(std::string(vo.key) + ".slider", sl);
    int val = *vo.vol;
    if (ui.slider(sl, val, 0, 1024)) {
      *vo.vol = val;
      changed(vo.key);
      if (std::string(vo.key) == "ambient") ambientDirty = true;
    }
    y += 42;
  }
  if (ambientDirty && !ui.in.down) { ambientDirty = false; R.add(Act::PlaySound, 0, 67); }   // ambience slider plays sound 67 on release
  v = s.sounds3D;
  if (check(ui, E, "sound3d", {rx, y, 260, 18}, E.win(0x291, "Enable 3D sounds"), v)) { s.sounds3D = v; changed("sound3d"); }
  y += 26;
  ui.text(rx, y, E.win(0x21A, "Music choices"));
  y += lh + 2;
  int rowH = lh + 4, visible = 5;
  Rect lr{rx, y, 270, visible * rowH};
  ui.fill(lr, rgb(255, 255, 255)); ui.frame(lr, false);
  scrollBar(ui, E, lr, int(s.tracks.size()), visible, scroll);
  for (int i = 0; i < visible && scroll + i < int(s.tracks.size()); i++) {
    auto& tk = s.tracks[size_t(scroll + i)];
    Rect row{lr.x + 2, lr.y + 2 + i * rowH, lr.w - 20, rowH};
    if (s.trackSel == scroll + i) ui.fill(row, rgb(0x90, 0xA8, 0xE0));
    bool on = tk.second;
    E.spot("track." + std::to_string(i), row);
    ui.fill({row.x + 2, row.y + 3, 10, 10}, rgb(255, 255, 255));
    ui.frame({row.x + 2, row.y + 3, 10, 10}, false);
    if (on) ui.fill({row.x + 4, row.y + 5, 6, 6}, ui.textCol);
    ui.text(row.x + 18, row.y + 2, tk.first);
    if (ui.clicked(row)) {
      if (ui.in.mx < row.x + 14) { tk.second = !tk.second; changed("musicChoice"); }
      s.trackSel = scroll + i;
    }
  }
  y += visible * rowH + 8;
  if (button(ui, E, "preview", {rx, y, 270, 22}, E.win(0x294, "Preview highlighted score"), s.trackSel >= 0)) R.add(Act::PreviewMusic, 0, s.trackSel);
  // ---- OK / Cancel
  if (button(ui, E, "ok", {r.x + r.w - 200, r.y + r.h - 34, 90, 24}, E.app(0xB4, "OK"))) {
    if (s.resolution != startResolution) R.add(Act::RestartPrompt);
    R.add(Act::Done);
    R.close = true;
  }
  if (button(ui, E, "cancel", {r.x + r.w - 104, r.y + r.h - 34, 90, 24}, E.app(0xB6, "Cancel"))) { R.close = true; R.add(Act::Close); }
  return R;
}

// ================================================================ City properties (6.9)
WindowResult CityPropertiesDialog::draw(Ui& ui, const WinEnv& E, CityPropsState& s) {
  WindowResult R;
  int lh = lineHeight(ui);
  Rect r = centered(ui, 480, 380);
  if (frame(ui, E, r, E.win(0x281, "City properties"))) { R.close = true; R.add(Act::Close); }
  int x0 = r.x + 16, y = r.y + 38;
  ui.text(x0, y + 4, E.win(0x229, "City name"));
  E.spot("name", {x0 + 120, y, 300, 22});
  if (s.editable) ui.textBox({x0 + 120, y, 300, 22}, s.name, 31);
  else ui.text(x0 + 124, y + 4, s.name);
  y += 30;
  ui.text(x0, y + 4, E.win(0x22A, "Mayor name"));
  E.spot("mayor", {x0 + 120, y, 300, 22});
  if (s.editable) ui.textBox({x0 + 120, y, 300, 22}, s.mayor, 31);
  else ui.text(x0 + 124, y + 4, s.mayor);
  y += 40;
  struct Sch { const char* key; uint32_t id; const char* fb; int* sel; const std::vector<std::string>* names; };
  Sch sch[3] = {{"landscape", 0x28, "Landscape", &s.landscape, &s.landscapes}, {"flora", 0x29, "Trees", &s.flora, &s.floraSets},
                {"buildings", 0x2A, "Buildings", &s.buildings, &s.buildingSets}};
  for (auto& c : sch) {
    ui.text(x0, y + 4, E.se(c.id, c.fb));
    int n = int(c.names->size());
    if (button(ui, E, std::string(c.key) + ".prev", {x0 + 120, y, 24, 22}, "<", n > 1)) *c.sel = (*c.sel + n - 1) % n;
    ui.textCentered({x0 + 148, y, 200, 22}, n ? (*c.names)[size_t(std::max(0, std::min(n - 1, *c.sel)))] : "-");
    if (button(ui, E, std::string(c.key) + ".next", {x0 + 352, y, 24, 22}, ">", n > 1)) *c.sel = (*c.sel + 1) % n;
    y += 30;
  }
  y += 6;
  ui.text(x0, y, E.se(0x2E, "Description"));
  if (s.landscape >= 0 && s.landscape < int(s.descriptions.size()))
    paragraph(ui, {x0, y + lh + 2, r.w - 32, 60}, s.descriptions[size_t(s.landscape)]);
  if (button(ui, E, "ok", {r.x + r.w - 200, r.y + r.h - 34, 90, 24}, E.app(0xB4, "OK"))) { R.add(Act::ApplyCityProps); R.add(Act::Done); R.close = true; }
  if (button(ui, E, "cancel", {r.x + r.w - 104, r.y + r.h - 34, 90, 24}, E.app(0xB6, "Cancel"))) { R.close = true; R.add(Act::Close); }
  return R;
}

// ================================================================ Scenario goals (6.10)
WindowResult ScenarioGoalsWindow::draw(Ui& ui, const WinEnv& E, const ScenarioGoalsView& v) {
  WindowResult R;
  int lh = lineHeight(ui);
  Rect r = centered(ui, 540, 420);
  if (frame(ui, E, r, v.title.empty() ? "Scenario goals" : v.title)) { R.close = true; R.add(Act::Close); }
  int x0 = r.x + 16, y = r.y + 36;
  if (!v.inScenario) { ui.text(x0, y, "-"); return R; }
  ui.text(x0, y, v.briefingLabel.empty() ? "Briefing" : v.briefingLabel, "title12");
  y += lh + 6;
  y += paragraph(ui, {x0, y, r.w - 32, 110}, v.briefing) + 10;
  ui.text(x0, y, v.goalsLabel.empty() ? "Goals" : v.goalsLabel, "title12");
  y += lh + 6;
  int rowH = lh + 4, bottom = r.y + r.h - 70;
  int visible = std::max(1, (bottom - y) / rowH);
  scrollBar(ui, E, {x0, y, r.w - 32, visible * rowH}, int(v.goals.size()), visible, scroll);
  for (int i = 0; i < visible && scroll + i < int(v.goals.size()); i++) {
    const auto& g = v.goals[size_t(scroll + i)];
    Rect box{x0, y + i * rowH + 2, 12, 12};
    ui.fill(box, rgb(255, 255, 255)); ui.frame(box, false);
    if (g.met) ui.fill({box.x + 3, box.y + 3, 6, 6}, rgb(0, 150, 0));
    ui.text(x0 + 20, y + i * rowH + 2, g.text, "main10", g.met ? rgb(0, 110, 0) : 0);
  }
  ui.text(x0, r.y + r.h - 62, (v.statusLabel.empty() ? std::string("Status") : v.statusLabel) + ": " + v.status);
  if (button(ui, E, "ok", {r.x + r.w - 104, r.y + r.h - 34, 90, 24}, E.app(0xB4, "OK"))) { R.close = true; R.add(Act::Close); }
  return R;
}

// ================================================================ Building picker (6.11)
bool pickerCategoryFromParam(uint32_t param, PickerCategory& out) {
  for (int i = 0; i < 5; i++) if (kPickerParam[i] == param) { out = PickerCategory(i); return true; }
  return false;
}

uint32_t pickerTitleId(PickerCategory c) {
  switch (c) {
    case PickerCategory::Landmarks: return 0x44;
    case PickerCategory::Rewards: return 0x2A;
    case PickerCategory::PowerPlants: return 0x1;
    case PickerCategory::Garbage: return 0x75;
    case PickerCategory::Water: return 0x1D;
  }
  return 0x7C;
}
uint32_t pickerEmptyId(PickerCategory c) {
  switch (c) {
    case PickerCategory::Landmarks: return 0x77;
    case PickerCategory::Rewards: return 0x6E;
    case PickerCategory::PowerPlants: return 0x7A;
    case PickerCategory::Garbage: return 0x79;
    case PickerCategory::Water: return 0x7B;
  }
  return 0xFFFFFFFF;
}

PickerFilter PickerFilter::defaults(PickerCategory c) {
  PickerFilter f;
  switch (c) {
    case PickerCategory::PowerPlants: f.ranges = {{12111, 12118}}; break;                 // coal .. wind (spec 16 s2)
    case PickerCategory::Garbage: f.ranges = {{12852, 12853}, {12005, 12005}}; break;     // incinerator, WTE, garbage family
    case PickerCategory::Landmarks: f.ranges = {{18000, 18045}, {19999, 20002}}; break;   // spec 13 section 4
    case PickerCategory::Rewards: f.ranges = {{19000, 19004}, {11001, 11099}}; break;
    case PickerCategory::Water: f.ranges = {{12400, 12499}}; break;                       // [OPEN] water facility agent types not listed in the spec
  }
  return f;
}
bool PickerFilter::match(uint32_t t) const {
  for (auto& r : ranges) if (t >= r.first && t <= r.second) return true;
  return false;
}

std::vector<PickerEntry> collectPickerEntries(GameData& data, const PickerFilter& filter, const Budget* budget,
                                              const AgentHierarchy* hier,
                                              const std::function<bool(uint32_t, const OccAttr&)>& isAvailable,
                                              const std::function<bool(uint32_t, const OccAttr&)>& isPlaced) {
  std::vector<PickerEntry> out;
  AgentHierarchy empty;
  for (const ResKey& k : data.db().keysOfType(T_OCC_ATTR)) {
    if (k.group != 0x57E) continue;
    const OccAttr* a = data.occupant(k.instance);
    if (!a || !a->valid || !filter.match(a->agentType) || a->filler()) continue;
    if (isAvailable && !isAvailable(k.instance, *a)) continue;
    PickerEntry e;
    e.occupantId = k.instance;
    e.name = data.string(0xA1096A4F, k.instance);        // display-name string 2026960B:A1096A4F:<occupant id>
    if (e.name.empty() && a->nameKey.instance) e.name = data.string(a->nameKey.group, a->nameKey.instance);
    if (e.name.empty()) e.name = a->designName.empty() ? "Building " + std::to_string(k.instance) : a->designName;
    e.sizeX = int(a->sizeX); e.sizeZ = int(a->sizeZ);
    if (budget) e.cost = budget->placementCost(a->agentType, hier ? *hier : empty, false);
    e.placed = isPlaced ? isPlaced(k.instance, *a) : false;
    out.push_back(e);
  }
  std::sort(out.begin(), out.end(), [](const PickerEntry& a, const PickerEntry& b) { return a.name != b.name ? a.name < b.name : a.occupantId < b.occupantId; });
  return out;
}

std::vector<PickerEntry> rewardPickerEntries(const RewardTable& t, const Budget* budget, const AgentHierarchy* hier) {
  std::vector<PickerEntry> out;
  AgentHierarchy empty;
  for (auto& r : t.entries()) {
    if (!r.available) continue;
    PickerEntry e;
    e.buildingType = r.buildingType;
    e.name = r.name.empty() ? "Reward " + std::to_string(r.buildingType) : r.name;
    e.placed = r.owned;
    if (budget) e.cost = budget->placementCost(uint32_t(r.buildingType), hier ? *hier : empty, false);
    auto it = t.typeToInstance.find(r.buildingType);
    if (it != t.typeToInstance.end()) e.occupantId = it->second;
    out.push_back(e);
  }
  return out;
}

WindowResult BuildingPicker::draw(Ui& ui, const WinEnv& E, const std::vector<PickerEntry>& entries) {
  WindowResult R;
  int lh = lineHeight(ui);
  Rect r = centered(ui, 560, 420);
  if (frame(ui, E, r, E.text(kTblCatalog, pickerTitleId(category), "Buildings"))) { R.close = true; R.add(Act::Close); }
  int x0 = r.x + 12, y = r.y + 34;
  ui.text(x0, y, E.text(kTblCatalog, 0x76, "Buildings available"), "title12");
  y += lh + 8;
  std::vector<const PickerEntry*> shown;
  for (auto& e : entries) if (e.available) shown.push_back(&e);
  int rowH = 40, visible = std::max(1, (r.y + r.h - 50 - y) / rowH);
  if (shown.empty()) {
    paragraph(ui, {x0, y, r.w - 24, 60}, E.text(kTblCatalog, pickerEmptyId(category), "No buildings are available."));
  }
  scrollBar(ui, E, {x0, y, r.w - 24, visible * rowH}, int(shown.size()), visible, scroll);
  for (int i = 0; i < visible && scroll + i < int(shown.size()); i++) {
    const PickerEntry& e = *shown[size_t(scroll + i)];
    Rect row{x0, y + i * rowH, r.w - 44, rowH - 2};
    bool selRow = sel == scroll + i;
    ui.fill(row, selRow ? rgb(0x90, 0xA8, 0xE0) : rgb(255, 255, 255));
    E.spot("row." + std::to_string(i), row);
    portrait(ui, {row.x + 2, row.y + 2, 36, 36}, e.picture, e.name.empty() ? "?" : e.name.substr(0, 1), rgb(0x7A, 0x84, 0xB8));
    uint32_t col = e.placed ? rgb(0x90, 0x94, 0xA0) : 0;
    ui.text(row.x + 44, row.y + 4, e.name, "main10", col);
    std::string size = subst(E.se(0x133, "%sx%s"), {std::to_string(e.sizeX), std::to_string(e.sizeZ)});
    ui.text(row.x + 44, row.y + 4 + lh, size, "main10", col);
    rightText(ui, row.x + row.w - 8, row.y + 4, E.money(e.cost), "main10", col);
    if (e.placed) rightText(ui, row.x + row.w - 8, row.y + 4 + lh, E.text(kTblCatalog, 0x70, "Already built"), "main10", col);
    if (ui.clicked(row)) sel = scroll + i;
  }
  bool canPick = sel >= 0 && sel < int(shown.size()) && !shown[size_t(sel)]->placed;
  if (button(ui, E, "ok", {r.x + r.w - 200, r.y + r.h - 34, 90, 24}, E.app(0xB4, "OK"), canPick)) {
    const PickerEntry& e = *shown[size_t(sel)];
    static const char* names[5] = {"power", "water", "garbage", "landmarks", "rewards"};
    R.add(Act::PickBuilding, e.occupantId, e.buildingType, 0, names[int(category)]);
    R.close = true;
  }
  if (button(ui, E, "cancel", {r.x + r.w - 104, r.y + r.h - 34, 90, 24}, E.app(0xB6, "Cancel"))) { R.close = true; R.add(Act::Close); }
  return R;
}

}  // namespace m3k
