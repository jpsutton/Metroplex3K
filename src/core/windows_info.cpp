// Preferences, city properties, scenario goals, building picker (spec 09 sections 6.8-6.11).
#include <algorithm>
#include "core/skin.h"
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

// Layout measured on the original at 800x600 (dialog-shots/main-menu--preferences.png): 480x472 frame, three sunken
// groups (performance, sound, general options), GZRadioBtn / GZCheckBox controls with main9 labels, the volume block
// with GZSliderH knobs, the music list with a GZScrollbar, the preview band with play / stop buttons and the check-mark
// (GZBtnDone) button. The close box cancels.
WindowResult PreferencesDialog::draw(Ui& ui, const WinEnv& E, PrefsState& s) {
  WindowResult R;
  Rect r = centered(ui, 480, 472);
  if (startResolution < 0) startResolution = s.resolution;
  const Skin* sk = ui.skin;
  const uint32_t label = rgb(0x7B, 0x82, 0xB5), ink = rgb(0x31, 0x34, 0x7B), white = rgb(255, 255, 255);
  const uint32_t dark = rgb(0x7B, 0x8A, 0xFF), light = rgb(0xFF, 0xF3, 0xFF), blue = rgb(0x7B, 0x92, 0xFF);
  if (frame(ui, E, r, E.win(0x14E, "Preferences"))) { R.close = true; R.add(Act::Close); }
  auto changed = [&](const char* key) { R.add(Act::PrefsChanged, 0, 0, 0, key); };
  auto sunkenLines = [&](Rect b) {
    ui.fill({b.x, b.y, b.w, 1}, dark); ui.fill({b.x, b.y, 1, b.h}, dark);
    ui.fill({b.x, b.y + b.h - 1, b.w, 1}, light); ui.fill({b.x + b.w - 1, b.y, 1, b.h}, light);
  };
  auto lab = [&](int x, int y, const std::string& t, uint32_t c) { ui.text(r.x + x, r.y + y, t, "main9", c); };
  ui.fill({r.x + 6, r.y + 42, 200, 300}, rgb(222, 223, 255));   // flat fill inside the Performance and General Options panels (observed)
  ui.fill({r.x + 6, r.y + 346, 468, 76}, rgb(222, 223, 255));
  sunkenLines({r.x + 6, r.y + 42, 200, 300});
  sunkenLines({r.x + 212, r.y + 42, 262, 300});
  sunkenLines({r.x + 6, r.y + 346, 468, 76});

  // Radio button (GZRadioBtn, 8 cells of 12x12) 1 px below its main9 label's top; label at x 32.
  const Image* radioArt = sk ? sk->get("radio") : nullptr;
  auto radio = [&](const std::string& name, int y, const std::string& lbl, int& var, int val, const char* key) {
    Rect hit{r.x + 18, r.y + y, 14 + ui.textWidth(lbl, "main9"), 14};
    E.spot(name, hit);
    bool hot = ui.hover(hit);
    if (!(sk && sk->drawCheck(ui, radioArt, r.x + 18, r.y + y + 1, var == val, true, hot, hot && ui.in.down))) {
      ui.fill({r.x + 18, r.y + y + 1, 12, 12}, white); ui.frame({r.x + 18, r.y + y + 1, 12, 12}, false);
      if (var == val) ui.fill({r.x + 21, r.y + y + 4, 6, 6}, ink);
    }
    lab(32, y, lbl, ink);
    if (ui.clicked(hit) && var != val) { var = val; changed(key); }
  };
  // Check box (GZCheckBox, 8 cells of 13x13) 1 px below the label's top; label 16 px right of the box.
  const Image* checkArt = sk ? sk->get("check") : nullptr;
  auto check = [&](const std::string& name, int x, int y, const std::string& lbl, bool& v, const char* key) {
    Rect hit{r.x + x, r.y + y, 16 + ui.textWidth(lbl, "main9"), 15};
    E.spot(name, hit);
    bool hot = ui.hover(hit);
    if (!(sk && sk->drawCheck(ui, checkArt, r.x + x, r.y + y + 1, v, true, hot, hot && ui.in.down))) {
      ui.fill({r.x + x, r.y + y + 1, 13, 13}, white); ui.frame({r.x + x, r.y + y + 1, 13, 13}, false);
      if (v) ui.fill({r.x + x + 3, r.y + y + 4, 7, 7}, ink);
    }
    lab(x + 16, y, lbl, ink);
    if (ui.clicked(hit)) { v = !v; changed(key); return true; }
    return false;
  };

  // ---- Performance (left group)
  lab(14, 48, E.win(0x1FF, "Performance"), label);
  lab(14, 70, E.win(0x200, "Traffic Visible"), label);
  const uint32_t tr[4] = {0x201, 0x202, 0x203, 0x20A};
  const char* trFb[4] = {"Closest 3 Zooms", "Closest 2 Zooms", "Closest Zoom", "Off"};
  for (int i = 0; i < 4; i++) radio("traffic." + std::to_string(i), 88 + 18 * i, E.win(tr[i], trFb[i]), s.traffic, i, "traffic");
  lab(14, 164, E.win(0x207, "Sims Visible"), label);
  const uint32_t sm[3] = {0x208, 0x209, 0x20A};
  const char* smFb[3] = {"Closest 2 Zooms", "Closest Zoom", "Off"};
  for (int i = 0; i < 3; i++) radio("sims." + std::to_string(i), 182 + 18 * i, E.win(sm[i], smFb[i]), s.sims, i, "sims");
  lab(14, 240, E.win(0x204, "Other Animations"), label);
  int oa = s.otherAnimations ? 1 : 0;
  radio("anim.on", 258, E.win(0x205, "On"), oa, 1, "otherAnimations");
  radio("anim.off", 276, E.win(0x206, "Off"), oa, 0, "otherAnimations");
  s.otherAnimations = oa != 0;
  lab(14, 298, E.win(0x20B, "Screen Resolution"), label);
  // Resolution drop-down: sunken white field (system9 text) and the combo arrow.
  Rect combo{r.x + 18, r.y + 314, 159, 20}, arrow{r.x + 178, r.y + 316, 16, 16};
  ui.fill(combo, white); sunkenLines(combo);
  auto modeName = [&](int i) {
    if (s.modes.empty()) return std::string();
    const DisplayMode& m = s.modes[size_t(std::max(0, std::min(int(s.modes.size()) - 1, i)))];
    return std::to_string(m.w) + "x" + std::to_string(m.h);
  };
  ui.text(combo.x + 3, combo.y + 3, modeName(s.resolution), "system9", ink);
  E.spot("res", arrow);
  { bool c = false;
    if (!(sk && sk->imageButtonCells(ui, sk->get("combo"), 4, arrow, "", false, true, c))) c = ui.button(arrow, "v");
    if (c || ui.clicked(combo)) resOpen = !resOpen; }

  // ---- Sound (right group): check boxes, volume block with the level wedge and three sliders.
  lab(220, 48, E.win(0x292, "Sound"), label);
  Rect block{r.x + 372, r.y + 46, 96, 110};
  ui.fill(block, blue); ui.fill({block.x, block.y, block.w, 1}, dark); ui.fill({block.x, block.y + block.h - 1, block.w, 1}, dark);
  ui.fill({block.x, block.y, 1, block.h}, dark); ui.fill({block.x + block.w - 1, block.y, 1, block.h}, dark);
  // Level wedge: the volume-ramp art 22DEC92D:00000001 (68 x 10) drawn unscaled at (386, 50) [OBSERVED, 0 px]; without art, seven drawn bars.
  const Image* ramp = sk ? sk->image(skin_group::Other, 1) : nullptr;
  if (ramp) ui.blit(*ramp, r.x + 386, r.y + 50);
  else for (int k = 0; k < 7; k++)
    for (int x = 0; x < 8; x++) {
      int gx = 16 + k * 10 + x, top = 59 - (gx - 16) * 9 / 67;
      ui.fill({r.x + 370 + gx, r.y + top, 1, 60 - top}, rgb(0xD6, 0xDB, 0xFF));
      ui.fill({r.x + 370 + gx, r.y + top, 1, 1}, rgb(0x94 + x * 8, 0xA2 + x * 6, 0xFF));
    }
  struct Vol { const char* key; uint32_t id; const char* fb; int* vol; bool* on; };
  Vol vols[3] = {{"music", 0x153, "Music", &s.musicVolume, &s.musicOn}, {"ambient", 0x219, "Ambient Sound", &s.ambientVolume, &s.ambientOn},
                 {"sfx", 0x14F, "Sound Effects", &s.sfxVolume, &s.sfxOn}};
  const Image* knobArt = sk ? sk->image(skin_group::Sys, 0xC2B99E09) : nullptr;
  for (int i = 0; i < 3; i++) {
    Vol& vo = vols[i];
    int y = 69 + 24 * i;
    check(std::string(vo.key) + ".on", 222, y, E.win(vo.id, vo.fb), *vo.on, vo.key);
    // Groove from x 379 to 454 (dark, darker, darker, white rows); the 20 px knob is centred on the value.
    const int x0 = r.x + 379, x1 = r.x + 454, gy = r.y + y + 6;
    ui.fill({x0, gy, x1 - x0 + 1, 1}, rgb(0x39, 0x38, 0x7B)); ui.fill({x0, gy + 1, x1 - x0 + 1, 2}, rgb(0x08, 0x0C, 0x18));
    ui.fill({x0, gy + 3, x1 - x0 + 1, 1}, white);
    Rect sl{x0, r.y + y - 2, x1 - x0 + 1, 20};
    E.spot(std::string(vo.key) + ".slider", sl);
    if (ui.in.pressed && ui.hover(sl)) dragSlider = i;
    if (dragSlider == i && ui.in.down) {
      int v = std::max(0, std::min(1024, (ui.in.mx - x0) * 1024 / (x1 - x0)));
      if (v != *vo.vol) { *vo.vol = v; changed(vo.key); if (i == 1) ambientDirty = true; }
    }
    int kx = x0 + *vo.vol * (x1 - x0) / 1024 - 10;
    if (knobArt && knobArt->w >= 80) Skin::drawCell(ui, knobArt, 4, dragSlider == i && ui.in.down ? 2 : 3, kx, r.y + y - 2);
    else { ui.fill({kx + 4, r.y + y + 2, 12, 12}, white); ui.frame({kx + 4, r.y + y + 2, 12, 12}, true); }
  }
  if (!ui.in.down) dragSlider = -1;
  if (ambientDirty && !ui.in.down) { ambientDirty = false; R.add(Act::PlaySound, 0, 67); }   // ambience slider plays sound 67 on release
  check("sound3d", 222, 141, E.win(0x291, "Enable 3D Sounds"), s.sounds3D, "sound3d");

  // Music choices: sunken lavender list (system9 rows of 17 px with a check box) and a GZScrollbar.
  lab(220, 162, E.win(0x21A, "Music Choices"), label);
  Rect lr{r.x + 218, r.y + 178, 234, 130};
  ui.fill(lr, rgb(0xDE, 0xDF, 0xFF)); sunkenLines(lr);
  const int rowH = 17, visible = (lr.h - 2) / rowH, total = int(s.tracks.size());
  if (ui.hover(lr) && ui.in.wheel) scroll -= ui.in.wheel;
  scroll = std::max(0, std::min(scroll, std::max(0, total - visible)));
  for (int i = 0; i < visible && scroll + i < total; i++) {
    auto& tk = s.tracks[size_t(scroll + i)];
    Rect row{lr.x + 1, lr.y + 1 + i * rowH, lr.w - 2, rowH};
    bool sel = s.trackSel == scroll + i;
    if (sel) ui.fill(row, rgb(0, 0, 255));
    E.spot("track." + std::to_string(i), row);
    if (!(sk && sk->drawCheck(ui, checkArt, row.x + 2, row.y + 2, tk.second))) {
      ui.fill({row.x + 2, row.y + 3, 10, 10}, white); ui.frame({row.x + 2, row.y + 3, 10, 10}, false);
      if (tk.second) ui.fill({row.x + 4, row.y + 5, 6, 6}, ink);
    }
    ui.text(row.x + 19, row.y + 2, tk.first, "system9", sel ? white : ink);
    if (ui.clicked(row)) {
      if (ui.in.mx < row.x + 15) { tk.second = !tk.second; changed("musicChoice"); }
      s.trackSel = scroll + i;
    }
  }
  {
    const Image* sb = sk ? sk->image(skin_group::Sys, 0xE2B99E01) : nullptr;
    Rect up{r.x + 453, lr.y, 16, 16}, dn{r.x + 453, lr.y + lr.h - 16, 16, 16};
    Rect track{up.x, up.y + 16, 16, dn.y - up.y - 16};
    int range = std::max(0, total - visible), ty = track.y + (range ? (track.h - 16) * scroll / range : 0);
    if (sb && sb->w >= 192) {
      for (int y = track.y; y < track.y + track.h; y += 16) ui.blitRect(*sb, track.x, y, 9 * 16, 0, 16, std::min(16, track.y + track.h - y));
      Skin::drawCell(ui, sb, 12, ui.hover(up) && ui.in.down ? 7 : 6, up.x, up.y);
      Skin::drawCell(ui, sb, 12, ui.hover(dn) && ui.in.down ? 11 : 10, dn.x, dn.y);
      Skin::drawCell(ui, sb, 12, 8, track.x, ty);
    } else { ui.fill(track, ui.panelDark); ui.button(up, "^"); ui.button(dn, "v"); }
    E.spot("scroll.up", up); E.spot("scroll.down", dn);
    if (ui.clicked(up)) scroll--;
    if (ui.clicked(dn)) scroll++;
    scroll = std::max(0, std::min(scroll, range));
  }
  // Preview band: play (82FAB6D2) and stop (22FAB6DE) buttons, cells of 24x25.
  Rect band{r.x + 218, r.y + 310, 250, 28};
  ui.fill(band, blue); ui.fill({band.x, band.y, band.w, 1}, dark); ui.fill({band.x, band.y + band.h - 1, band.w, 1}, dark);
  ui.fill({band.x, band.y, 1, band.h}, dark); ui.fill({band.x + band.w - 1, band.y, 1, band.h}, dark);
  lab(226, 317, E.win(0x294, "Preview Highlighted Score"), ink);
  Rect play{r.x + 414, r.y + 313, 24, 25}, stop{r.x + 438, r.y + 313, 24, 25};
  E.spot("preview", play); E.spot("preview.stop", stop);
  { bool c = false;
    if (!(sk && sk->imageButtonCells(ui, sk->image(skin_group::Sys, 0x82FAB6D2), 4, play, "", false, true, c))) c = ui.button(play, ">");
    if (c && s.trackSel >= 0) R.add(Act::PreviewMusic, 0, s.trackSel); }
  { bool c = false;
    if (!(sk && sk->imageButtonCells(ui, sk->image(skin_group::Sys, 0x22FAB6DE), 4, stop, "", false, true, c))) c = ui.button(stop, "#");
    if (c) R.add(Act::PreviewMusic, 0, -1); }

  // ---- General options (bottom group)
  lab(14, 348, E.win(0x2D0, "General Options:"), label);
  check("showTips", 18, 365, E.win(0x293, "Show tips at startup"), s.showTips, "showTips");
  check("autoDisaster", 18, 382, E.win(0x21F, "Auto Go To Disasters"), s.autoGoToDisasters, "autoGoToDisasters");

  // Done (check mark).
  Rect done{r.x + 432, r.y + 434, 42, 32};
  E.spot("ok", done);
  { bool c = false;
    if (!(sk && sk->imageButtonCells(ui, sk->image(skin_group::Sys, 0xA2DEFD9A), 4, done, "", false, true, c))) c = ui.button(done, E.app(0xB4, "OK"));
    if (c || (ui.in.key[K_ENTER] && !resOpen)) {
      if (s.resolution != startResolution) R.add(Act::RestartPrompt);
      R.add(Act::Done);
      R.close = true;
    } }
  if (ui.in.key[K_ESC] && !resOpen) { R.close = true; R.add(Act::Close); }

  // Resolution drop-down list, drawn last (on top).
  if (resOpen) {
    Rect dd{combo.x, combo.y + combo.h, combo.w + 17, 17 * int(s.modes.size()) + 2};
    ui.fill(dd, white); ui.frame(dd, false);
    for (int i = 0; i < int(s.modes.size()); i++) {
      Rect row{dd.x + 1, dd.y + 1 + 17 * i, dd.w - 2, 17};
      E.spot("res." + std::to_string(i), row);
      bool hot = ui.hover(row) || (i == s.resolution && !ui.hover(dd));
      if (hot) ui.fill(row, rgb(0, 0, 255));
      ui.text(row.x + 2, row.y + 2, modeName(i), "system9", hot ? white : ink);
      if (ui.clicked(row)) { resOpen = false; if (i != s.resolution) { s.resolution = i; changed("resolution"); } }
    }
    if (ui.in.released && !dd.contains(ui.in.mx, ui.in.my) && !arrow.contains(ui.in.mx, ui.in.my) && !combo.contains(ui.in.mx, ui.in.my)) resOpen = false;
    if (ui.in.key[K_ESC]) resOpen = false;
  }
  return R;
}

// ================================================================ City properties (6.9)
// City Options: the New City Options frame (639x368, measured on dialog-shots/hud--options--city-properties.png) with
// the name fields, a Description box, the Disasters / Auto Budget check boxes, the scheme selectors and preview.
WindowResult CityPropertiesDialog::draw(Ui& ui, const WinEnv& E, CityPropsState& s) {
  WindowResult R;
  Rect r = centered(ui, 639, 368);
  const Skin* sk = ui.skin;
  const uint32_t ink = rgb(0x31, 0x34, 0x7B), white = rgb(255, 255, 255);
  if (frame(ui, E, r, E.win(0x21C, "City Options"))) { R.close = true; R.add(Act::Close); }
  blueBand(ui, {r.x + 4, r.y + 39, 10, 287}); blueBand(ui, {r.x + 218, r.y + 39, 10, 287}); blueBand(ui, {r.x + 4, r.y + 330, 631, 34});
  auto lab = [&](int x, int y, const std::string& t) { ui.text(r.x + x, r.y + y, t, "main9", ink); };
  lab(17, 38, E.win(0x229, "City Name:"));
  lab(17, 76, E.win(0x22A, "Mayor Name:"));
  if (s.editable) {
    editField(ui, E, "name", {r.x + 23, r.y + 54, 190, 20}, s.name, 0x4350524E);
    editField(ui, E, "mayor", {r.x + 23, r.y + 92, 190, 20}, s.mayor, 0x4350524D);
  } else { lab(27, 57, s.name); lab(27, 95, s.mayor); }
  lab(17, 116, E.se(0x2E, "Description:"));
  editField(ui, E, "description", {r.x + 23, r.y + 132, 190, 144}, s.description, 0x43505244, 255, true);
  const Image* checkArt = sk ? sk->get("check") : nullptr;
  auto check = [&](const std::string& name, int y, const std::string& lbl, bool& v) {
    Rect hit{r.x + 17, r.y + y, 16 + ui.textWidth(lbl, "main9"), 15};
    E.spot(name, hit);
    bool hot = ui.hover(hit);
    if (!(sk && sk->drawCheck(ui, checkArt, r.x + 17, r.y + y + 1, v, true, hot, hot && ui.in.down))) {
      ui.fill({r.x + 17, r.y + y + 1, 13, 13}, white); ui.frame({r.x + 17, r.y + y + 1, 13, 13}, false);
      if (v) ui.fill({r.x + 20, r.y + y + 4, 7, 7}, ink);
    }
    lab(33, y, lbl);
    if (ui.clicked(hit)) v = !v;
  };
  check("disasters", 295, E.win(0x21D, "Disasters"), s.disasters);
  check("autoBudget", 312, E.win(0x21E, "Auto Budget"), s.autoBudget);
  // Scheme selector icons (cell 1 normal, 2 selected, 3 hover); spots "<kind>.<index>".
  auto selector = [&](const char* key, int x0, int y, int pitch, int n, const std::vector<uint32_t>& icons, int& sel) {
    for (int i = 0; i < n; i++) {
      const Image* strip = i < int(icons.size()) && sk ? sk->image(0x85535958, icons[size_t(i)]) : nullptr;
      int cw = strip ? strip->w / 4 : pitch - 4;
      Rect hit{r.x + x0 + i * pitch, r.y + y, cw, strip ? strip->h : 30};
      E.spot(std::string(key) + "." + std::to_string(i), hit);
      bool hot = ui.hover(hit);
      if (!(strip && Skin::drawCell(ui, strip, 4, i == sel ? 2 : hot ? 3 : 1, hit.x, hit.y))) ui.button(hit, std::to_string(i + 1), true, i == sel);
      if (ui.clicked(hit)) sel = i;
    }
  };
  lab(234, 46, E.se(0x28, "Landscape:"));
  selector("landscape", 248, 63, 43, int(s.landscapes.size()), s.landscapeIcons, s.landscape);
  lab(234, 108, E.se(0x29, "Trees:"));
  selector("flora", 248, 121, 39, int(s.floraSets.size()), s.floraIcons, s.flora);
  lab(234, 190, E.se(0x2A, "Buildings:"));
  selector("buildings", 249, 205, 49, int(s.buildingSets.size()), s.buildingIcons, s.buildings);
  auto pick = [](const std::vector<uint32_t>& v, int i) { return i >= 0 && i < int(v.size()) ? v[size_t(i)] : 0u; };
  schemePreview(ui, E.data, r.x, r.y, pick(s.landscapePreview, s.landscape), pick(s.floraPreview, s.flora), pick(s.buildingPreview, s.buildings));
  lab(306, 294, E.se(0x2B, "User-Made Buildings:"));
  Rect sel{r.x + 434, r.y + 282, 78, 33};
  ui.fill(sel, rgb(0x7B, 0x92, 0xFF));
  E.spot("select", sel);
  if (ui.button(sel, E.se(0x2F, "Select"), true, false, "title9")) R.add(Act::OpenUserBuildings);
  // Check mark.
  Rect done{r.x + 589, r.y + 331, 42, 32};
  E.spot("ok", done);
  bool ok = false;
  if (!(sk && sk->imageButtonCells(ui, sk->image(skin_group::Sys, 0xA2DEFD9A), 4, done, "", false, true, ok))) ok = ui.button(done, E.app(0xB4, "OK"));
  if (ok) { R.add(Act::ApplyCityProps); R.add(Act::Done); R.close = true; }
  if (ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }
  return R;
}

// ================================================================ Scenario dialogs (6.10, spec 23 4.7.1)
namespace {
const uint32_t kPanelFill = 0xFFFF927Bu;   // (123,146,255) in the frame's 0xAABBGGRR order
const uint32_t kInset = 0xFFFFDFDEu;       // (222,223,255): every inset area (text boxes, goals list, results panel, button panels)
const int kPitch = 14;                      // body, goal and rank text pitch
const char* const kBodyFont = "main9";      // scenario text boxes and goals: 11 ink rows [OBSERVED]; main10 is 13 rows and 0.5 % wider

// Presenter panel: the advisor portrait (92 x 115 cells) in a 1 px frame, and the person's name centred below it.
void presenterPanel(Ui& ui, const WinEnv& E, Rect panel, int portraitX, int portraitY, int nameY, uint32_t advisor, int mood,
                    const ScenarioPresenterPic* pic = nullptr, int picTop = 0, int colW = 0) {
  ui.fill({panel.x, panel.y - 1, panel.w, panel.h + 1}, kPanelFill);   // the fill continues up over the dialog's inner top border (no top line, observed)
  // One-pixel border lines (173,182,255) on the left (just outside the fill), right and bottom; none on top [OBSERVED 800 x 600, spec 23 4.7.0].
  const uint32_t edge = rgb(173, 182, 255);
  ui.fill({panel.x - 1, panel.y, 1, panel.h}, edge);
  ui.fill({panel.x + panel.w - 1, panel.y, 1, panel.h}, edge);
  ui.fill({panel.x, panel.y + panel.h - 1, panel.w - 1, 1}, edge);
  const Rect pr{portraitX, portraitY, 91, 114};
  const uint32_t nameInk = rgb(0x31, 0x36, 0x7C);
  if (pic && pic->custom && !advisor) {
    // Kinds 0 / 2 (spec 23 4.7.0 [STATIC; crop INFERRED]): the picture drawn at x = column left + (colW - w) / 2 from picTop, cropped from its top-left
    // to at most 94 x 117, and a 1 px frame laid over its outer ring (dark top/left (66,65,66), white right/bottom; the right line owns the top-right
    // corner, the bottom line the bottom-left one) [OBSERVED]. The name below it, centred in the rest of the panel; no title line. No picture: the
    // name starts at the top.
    int bottom = panel.y;
    if (pic->group) {
      if (const Image* im = E.img(pic->group, pic->inst)) {
        const int w = std::min(94, im->w), h = std::min(117, im->h);
        const int x0 = panel.x - 1 + ((colW ? colW : panel.w + 1) - w) / 2, y0 = picTop ? picTop : pr.y - 1;
        ui.blitRect(*im, x0, y0, 0, 0, w, h);
        const uint32_t dark = rgb(66, 65, 66), white = rgb(255, 255, 255);
        ui.fill({x0, y0, w - 1, 1}, dark); ui.fill({x0, y0, 1, h - 1}, dark);
        ui.fill({x0 + w - 1, y0, 1, h - 1 + 1}, white); ui.fill({x0, y0 + h - 1, w, 1}, white);
        bottom = y0 + h;
      }
    }
    if (!pic->name.empty()) {
      const int rest = panel.y + panel.h - 1 - bottom;
      ui.textCentered({panel.x, bottom + std::max(0, (rest - 12) / 2), panel.w, 14}, pic->name, "main9", nameInk);   // 12 = measured: ink rows 271..281
    }
    return;
  }
  const int idx = AdvisorWindow::advisorIndex(advisor);
  if (idx < 1) return;
  if (const Image* im = E.img(0x52DB60A0, AdvisorWindow::portraitInstance(idx, mood)))
    ui.blitRect(*im, pr.x, pr.y, 0, 0, std::min(92, im->w), std::min(115, im->h));
  ui.frame({pr.x - 1, pr.y - 1, pr.w + 2, pr.h + 2}, false);
  const std::string name = AdvisorWindow::personName(E, advisor);
  ui.textCentered({panel.x, nameY, panel.w, 14}, name, "main9", nameInk);
}

bool checkButton(Ui& ui, const WinEnv& E, const char* spot, Rect r) {
  E.spot(spot, r);
  bool ok = false;
  if (!(ui.skin && ui.skin->imageButtonCells(ui, ui.skin->image(skin_group::Sys, 0xA2DEFD9A), 4, r, "", false, true, ok))) ok = ui.button(r, E.app(0xB4, "OK"));
  return ok;
}

// Bevelled frame of the scenario text box and goals list: dark (123,138,255) left/top, light (255,243,255) right/bottom [OBSERVED 800 x 600].
void insetBevel(Ui& ui, Rect b) {
  ui.fill({b.x, b.y, 1, b.h}, rgb(123, 138, 255)); ui.fill({b.x, b.y, b.w, 1}, rgb(123, 138, 255));
  ui.fill({b.x + b.w - 1, b.y, 1, b.h}, rgb(255, 243, 255)); ui.fill({b.x, b.y + b.h - 1, b.w, 1}, rgb(255, 243, 255));
}

// Vertical scroll bar from the skin strip Sys 82B9B75B:E2B99E01 (192 x 16: twelve 16 x 16 cells; 6/7 up arrow normal/pressed, 8 vertical thumb,
// 9 vertical groove, 10/11 down arrow) [OBSERVED 800 x 600: up arrow at the top of r, thumb below it, groove tiled, down arrow at the bottom].
// Scrolls by item. r is 16 px wide.
void scenarioScrollBar(Ui& ui, const WinEnv& E, Rect r, Rect wheelArea, int total, int visible, int& scroll) {
  const Image* strip = ui.skin ? ui.skin->image(skin_group::Sys, 0xE2B99E01) : nullptr;
  ui.fill(r, rgb(123, 146, 255));
  const Rect up{r.x, r.y, 16, 16}, dn{r.x, r.y + r.h - 16, 16, 16};
  E.spot("scroll.up", up); E.spot("scroll.down", dn);
  if (ui.hover(wheelArea) && ui.in.wheel) scroll -= ui.in.wheel;
  if (ui.clicked(up)) scroll--;
  if (ui.clicked(dn)) scroll++;
  const int range = std::max(0, total - visible);
  const Rect track{r.x, r.y + 16, 16, r.h - 32};
  const int th = 16;
  if (range > 0) {
    E.spot("scroll.track", track);
    if (ui.hover(track) && ui.in.down) {
      const int pos = ui.in.my - track.y - th / 2;
      scroll = (pos * range + (track.h - th) / 2) / std::max(1, track.h - th);
    }
    scroll = std::max(0, std::min(scroll, range));
  } else scroll = 0;
  const int ty = track.y + (range > 0 ? (track.h - th) * scroll / range : 0);
  if (!strip) {   // no art: flat stand-ins
    ui.fill(up, rgb(214, 215, 255)); ui.fill(dn, rgb(214, 215, 255)); ui.fill({track.x, ty, 16, th}, rgb(214, 215, 255));
    return;
  }
  for (int y = track.y; y < track.y + track.h; y += 16)   // groove, tiled (the last tile clipped to the track)
    ui.blitRect(*strip, track.x, y, 9 * 16, 0, 16, std::min(16, track.y + track.h - y));
  const bool upDown = ui.hover(up) && ui.in.down, dnDown = ui.hover(dn) && ui.in.down;
  Skin::drawCell(ui, strip, 12, upDown ? 7 : 6, up.x, up.y);
  Skin::drawCell(ui, strip, 12, dnDown ? 11 : 10, dn.x, dn.y);
  if (range > 0) Skin::drawCell(ui, strip, 12, 8, track.x, ty);
}

// Scenario text box [OBSERVED 800 x 600]: fill (222,223,255) inside a bevel (dark left/top (123,138,255), light right/bottom (255,243,255)); the
// text wraps at about 312 px (narrower by the 16 px scroll bar when the text overflows), 14 px pitch.
void textBox(Ui& ui, const WinEnv& E, Rect box, const std::string& text, int& scroll) {
  ui.fill(box, kInset);
  insetBevel(ui, box);
  const int lh = kPitch;
  const int visible = std::max(1, (box.h - 4) / lh);
  auto lines = wrapKeepSpaces(ui, text, box.w - 11, kBodyFont);
  const bool overflow = int(lines.size()) > visible;
  if (overflow) lines = wrapKeepSpaces(ui, text, box.w - 11 - 16, kBodyFont);
  if (overflow) scenarioScrollBar(ui, E, {box.x + box.w - 17, box.y + 1, 16, box.h - 2}, box, int(lines.size()), visible, scroll); else scroll = 0;
  for (int i = 0; i < visible && i + scroll < int(lines.size()); i++) ui.text(box.x + 6, box.y + 3 + i * lh, lines[size_t(i + scroll)], kBodyFont, 0xFF000000u);
}
}  // namespace

WindowResult ScenarioGoalsWindow::draw(Ui& ui, const WinEnv& E, const ScenarioGoalsView& v) {
  WindowResult R;
  Rect r = centered(ui, 474, 432);
  if (frame(ui, E, r, v.title.empty() ? "Scenario goals" : v.title)) { R.close = true; R.add(Act::Close); }
  if (!v.inScenario) { ui.text(r.x + 20, r.y + 50, "-"); return R; }
  auto at = [&](int x, int y, int w, int h) { return Rect{r.x + x, r.y + y, w, h}; };
  const uint32_t ink = rgb(0x31, 0x36, 0x7C);
  // Upper-left panel: heading, (start dialog: the mayor's name), the scrollable text.
  ui.fill(at(13, 34, 328, 178), kPanelFill);
  const std::string head = v.startMode ? E.se(3, "Greetings Mayor:") : (v.statusLabel.empty() ? std::string("Scenario Status:") : v.statusLabel);
  ui.text(r.x + 20, r.y + 36, head, "title12", ink);
  int textTop = 56;
  if (v.startMode) {
    if (!mayorInit) { mayorEdit = v.mayor; mayorInit = true; }
    editField(ui, E, "mayor", at(146, 44, 181, 17), mayorEdit, 0x53434D59, 31);
    textTop = 71;
  } else mayorInit = false;
  textBox(ui, E, at(18, textTop - 1, 323, 211 - textTop), v.briefing, scroll);   // frame (181,154)-(503,293) on the start dialog
  presenterPanel(ui, E, at(350, 35, 120, 177), r.x + 364, r.y + 56, r.y + 187, v.presenterAdvisor, v.presenterMood, &v.presenterPic, r.y + 55, 121);
  // Goals [OBSERVED 800 x 600]: variable rows of 12 + 14 x lines px (minimum the 28 px icon), the icon at the row top, the text wrapped at about
  // 392 px from x 220 with its first ink row 10 px below the icon top. The list clips at the frame; the 16 px scroll bar sits at its right edge.
  ui.text(r.x + 18, r.y + 224, v.goalsLabel.empty() ? "Scenario Goals:" : v.goalsLabel, "title12", ink);
  const Rect lb = at(18, 244, 453, 142);
  ui.fill(lb, kInset);
  std::vector<std::vector<std::string>> wrapped;
  std::vector<int> rowH;
  int total = 5;
  for (const auto& g : v.goals) {
    wrapped.push_back(wrapKeepSpaces(ui, g.text, 390, kBodyFont, true));
    rowH.push_back(std::max(28, 12 + kPitch * int(wrapped.back().size())));
    total += rowH.back();
  }
  const bool bar = total > lb.h - 2;
  if (bar) {   // the bar column is not part of the frame: top/bottom lines stop before it and it has no right line
    ui.fill({lb.x, lb.y, 1, lb.h}, rgb(123, 138, 255)); ui.fill({lb.x, lb.y, lb.w - 16, 1}, rgb(123, 138, 255)); ui.fill({lb.x, lb.y + lb.h - 1, lb.w, 1}, rgb(255, 243, 255));
  } else insetBevel(ui, lb);
  int fit = 0;
  { int acc = 5; for (size_t k = 0; k < rowH.size(); k++) { if (acc + rowH[k] > lb.h - 1) break; acc += rowH[k]; fit++; } }
  if (bar) scenarioScrollBar(ui, E, {r.x + 455, lb.y, 16, 141}, lb, int(v.goals.size()), std::max(1, fit), goalScroll); else goalScroll = 0;
  {
    int acc = 5;
    const int bottom = lb.y + lb.h - 1;
    for (size_t gi = size_t(goalScroll); gi < v.goals.size(); gi++) {
      const auto& g = v.goals[gi];
      const int y = lb.y + acc;
      if (y >= bottom) break;
      const int ix = r.x + 22;
      const Image* icon = E.img(0xC2BF3646u, g.met ? 0xE50ED85Au : 0x252A754Fu);   // DLG.IXF: the red SimCity stamp (met) / the "!" triangle (unmet)
      if (icon) {
        const int iy = y + (g.met ? -2 : 0);
        ui.blitRect(*icon, r.x + 21 - (g.met ? 3 : 0), iy, 0, 0, icon->w, std::max(0, std::min(icon->h, bottom - iy)));
      } else if (y + 28 <= bottom) {   // no art: flat stand-ins
        if (g.met) { for (int k = 0; k < 5; k++) ui.fill({ix + 7 + k, y + 14 + k, 2, 3}, rgb(0, 150, 0)); for (int k = 0; k < 9; k++) ui.fill({ix + 11 + k, y + 18 - k, 2, 3}, rgb(0, 150, 0)); }
        else { for (int yy = 0; yy < 26; yy++) { const int half = yy / 2 + 1; ui.fill({ix + 14 - half, y + 1 + yy, half * 2, 1}, rgb(255, 255, 255)); }
               ui.fill({ix + 13, y + 9, 3, 10}, 0xFF000000u); ui.fill({ix + 13, y + 21, 3, 3}, 0xFF000000u); }
      }
      const auto& lines = wrapped[gi];
      for (size_t k = 0; k < lines.size(); k++) {
        const int ty = y + 7 + int(k) * kPitch;
        if (ty + 10 > bottom) break;
        ui.text(r.x + 57, ty, lines[k], kBodyFont, rgb(49, 52, 123));
      }
      acc += rowH[gi];
    }
  }
  const bool done = checkButton(ui, E, "ok", at(426, 395, 42, 32));
  if (done || ui.in.key[K_ESC]) {
    if (v.startMode && !mayorEdit.empty()) R.add(Act::ScenarioMayor, 0, 0, 0, mayorEdit);   // the typed name renames the mayor
    R.close = true; R.add(Act::Close);
  }
  return R;
}

// Pop-up (cmdPopUpMsg), spec 23 4.7.1: 391 x 237, text panel with the warning icon on the left, presenter panel on the right.
WindowResult ScenarioPopupWindow::draw(Ui& ui, const WinEnv& E, const ScenarioPopupView& v) {
  WindowResult R;
  Rect r = centered(ui, 391, 237);
  const std::string title = v.presenterAdvisor ? AdvisorWindow::advisorName(E, v.presenterAdvisor) : std::string();
  if (frame(ui, E, r, title.empty() ? "Message" : title)) { R.close = true; R.add(Act::Close); }
  auto at = [&](int x, int y, int w, int h) { return Rect{r.x + x, r.y + y, w, h}; };
  ui.fill(at(4, 34, r.w - 8, r.h - 38), kPanelFill);   // solid blue behind the margin and the button strip
  const Rect tp = at(23, 41, 236, 150);
  ui.fill(tp, kInset);
  if (ui.skin) Skin::drawCell(ui, ui.skin->image(skin_group::Sys, 0x42E55FD4), 4, 1, r.x + 31, r.y + 58);   // the yellow warning triangle (MBIcons cell 1)
  const int lh = kPitch;
  const auto lines = wrap(ui, v.text, 167, "main10");
  const int visible = std::max(1, 138 / lh);
  const bool overflow = int(lines.size()) > visible;
  if (overflow) scrollBar(ui, E, {r.x + 241, tp.y, 16, tp.h}, int(lines.size()), visible, scroll); else scroll = 0;
  for (int i = 0; i < visible && i + scroll < int(lines.size()); i++) ui.text(r.x + 74, r.y + 51 + i * lh, lines[size_t(i + scroll)], "main10", 0xFF000000u);
  presenterPanel(ui, E, at(266, 40, 121, 150), r.x + 281, r.y + 41, r.y + 167, v.presenterAdvisor, v.mood, &v.presenterPic, r.y + 40, 122);
  if (checkButton(ui, E, "ok", at(338, 199, 42, 32)) || ui.in.key[K_ENTER] || ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }
  return R;
}

// Results dialog (cmdEndScenario), spec 23 4.7.1: 444 x 397, no close box or OK. Returns the chosen outcome 0 replay, 1 another scenario,
// 2 main menu, or -1.
int ScenarioEndWindow::draw(Ui& ui, const WinEnv& E, const ScenarioEndView& v) {
  Rect r = centered(ui, 444, 397);
  ui.panel(r, v.title);
  auto at = [&](int x, int y, int w, int h) { return Rect{r.x + x, r.y + y, w, h}; };
  const uint32_t ink = rgb(0x31, 0x36, 0x7C);
  ui.fill(at(13, 34, 308, 154), kInset);
  ui.text(r.x + 20, r.y + 39, v.heading.empty() ? "Scenario Completed!" : v.heading, "title12", ink);
  ui.text(r.x + 20, r.y + 61, v.resultsFor, "main10", ink);
  int y = r.y + 84;   // the first rank line (the original: y 193 on screen at 800 x 600)
  for (size_t i = 0; i < v.ranks.size(); i++) {
    const bool got = int(i) == v.achieved;
    if (got) {   // the red SimCity stamp left of the achieved rank (the same image as a met goal)
      if (const Image* st = E.img(0xC2BF3646u, 0xE50ED85Au)) ui.blit(*st, r.x + 20, r.y + 142);
      else { const Rect st2 = at(18, 142, 40, 26); ui.fill(st2, rgb(200, 30, 30)); ui.textCentered(st2, "SimCity", "main9", rgb(255, 255, 255)); }
    }
    const auto lines = wrap(ui, v.ranks[i], 250, "main10");
    for (size_t k = 0; k < lines.size() && k < 2; k++) ui.text(r.x + 61, y + int(k) * kPitch, lines[k], "main10", got ? rgb(49, 52, 123) : rgb(165, 162, 165));
    y += 19 + (lines.size() > 1 ? kPitch : 0);
  }
  presenterPanel(ui, E, at(321, 34, 119, 154), r.x + 334, r.y + 34, r.y + 163, v.presenterAdvisor, v.mood, &v.presenterPic, r.y + 34, 121);
  int scr = 0;
  textBox(ui, E, at(13, 203, 427, 89), v.text, scr);
  int choice = -1;
  static const char* kDefault[3] = {"Replay Scenario", "Play Another Scenario", "Return to Main Menu"};
  for (int i = 0; i < 3; i++) {
    const Rect bp = at(12 + i * 142, 305, 134, 76);
    ui.fill(bp, kInset);
    const auto cap = wrap(ui, v.captions[i].empty() ? kDefault[i] : v.captions[i], bp.w - 16, "main10");
    for (size_t k = 0; k < cap.size() && k < 2; k++) ui.textCentered({bp.x, bp.y + 10 + int(k) * kPitch, bp.w, kPitch}, cap[k], "main10", ink);
    const Rect logo{bp.x + 31, bp.y + 40, 73, 34};   // the 292 x 34 strip of 4 cells: disabled, normal, pressed, hover
    E.spot("end." + std::to_string(i), logo);
    bool clicked = false;
    if (ui.skin && ui.skin->imageButtonCells(ui, E.img(0xC2BF3646u, 0xC5171C41u), 4, logo, "", false, true, clicked)) { if (clicked) choice = i; }
    else if (ui.button(logo, "SimCity")) choice = i;
  }
  return choice;
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
    case PickerCategory::Garbage: f.ranges = {{12851, 12853}, {12005, 12005}}; break;     // recycling, incinerator, WTE, garbage family
    // Landmarks (spec 09 s6.11) [STATIC]: every exemplar whose agent type is 18000 or a descendant; the caller applies the
    // hierarchy test (the range only bounds the scan). No availability condition.
    case PickerCategory::Landmarks: f.ranges = {{0, 0xFFFFFFFFu}}; break;
    case PickerCategory::Rewards: f.ranges = {{19000, 19004}, {11001, 11099}}; break;
    case PickerCategory::Water: f.ranges = {{12810, 12840}}; break;                       // pump 12810, treatment 12820, desalination 12830, tower 12840 (observed in the picker)
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
    e.appearance = a->appearance;
    e.agentType = a->agentType;
    if (a->descKey.instance && a->descKey.instance != 0xFFFFFFFFu) e.description = data.string(a->descKey.group, a->descKey.instance);
    if (budget) e.cost = budget->placementCost(a->agentType, hier ? *hier : empty, false);
    e.placed = isPlaced ? isPlaced(k.instance, *a) : false;
    out.push_back(e);
  }
  // Order as observed in the original's pickers: power and water in a fixed order (probably by availability year),
  // garbage by agent type, landmarks by occupant id; duplicates (same agent type and name) shown once.
  static const uint32_t kOrder[] = {12111, 12112, 12113, 12115, 12118, 12117, 12114, 12116, 12810, 12840, 12820, 12830};
  auto rank = [](uint32_t t) { for (size_t i = 0; i < sizeof kOrder / sizeof kOrder[0]; i++) if (kOrder[i] == t) return int(i); return 1000; };
  std::stable_sort(out.begin(), out.end(), [&](const PickerEntry& a, const PickerEntry& b) {
    int ra = rank(a.agentType), rb = rank(b.agentType);
    if (ra != rb) return ra < rb;
    if (a.agentType != b.agentType && ra == 1000 && (a.agentType < 18000 || b.agentType < 18000)) return a.agentType < b.agentType;
    return a.occupantId < b.occupantId;
  });
  std::vector<PickerEntry> uniq;
  for (auto& e : out) {
    bool dup = false;
    for (auto& u : uniq) if (u.agentType == e.agentType && u.name == e.name) { dup = true; break; }
    if (!dup) uniq.push_back(std::move(e));
  }
  return uniq;
}

std::vector<PickerEntry> rewardPickerEntries(const RewardTable& t, const Budget* budget, const AgentHierarchy* hier, bool allUnbuilt) {
  std::vector<PickerEntry> out;
  AgentHierarchy empty;
  for (auto& r : t.entries()) {
    if (allUnbuilt ? (r.owned || r.isDeal) : !r.available) continue;
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

// Layout measured on the original (dialog-shots/hud--utilities--power-plants-picker.png, 800x600): 370x412 frame with a
// blue strip down the left and a blue divider at y 225; building picture (sprite) in a sunken box, "Buildings
// Available:" list (main9 rows of 14 px, selected row 5A59FF / white), name, cost / capacity and the description
// below the divider, and the check-mark button on a blue plate. The close box cancels.
WindowResult BuildingPicker::draw(Ui& ui, const WinEnv& E, const std::vector<PickerEntry>& entries) {
  WindowResult R;
  const Skin* sk = ui.skin;
  const uint32_t ink = rgb(0x31, 0x34, 0x7B), white = rgb(255, 255, 255), lav = rgb(0xDE, 0xDF, 0xFF), grey = rgb(0x90, 0x94, 0xA0);
  Rect r = centered(ui, 370, 412);
  // Title drawn after the left strip (which runs into the title bar) so the strip does not cover it.
  if (frame(ui, E, r, " ")) { R.close = true; R.add(Act::Close); }
  blueBand(ui, {r.x + 4, r.y + 4, 8, 407});
  if (ui.skin) ui.fill({r.x + 5, r.y + 5, 8, 29}, rgb(0x7B, 0x92, 0xFF));   // the strip runs into the title bar seamlessly
  blueBand(ui, {r.x + 4, r.y + 225, 365, 8});
  ui.text(r.x + 8, r.y + 8, E.text(kTblCatalog, pickerTitleId(category), "Buildings"), "title12", ui.skin ? rgb(222, 223, 255) : rgb(255, 255, 255));
  std::vector<const PickerEntry*> shown;
  for (auto& e : entries) if (e.available) shown.push_back(&e);
  // Picture of the selected building.
  Rect pic{r.x + 26, r.y + 38, 138, 183};
  ui.fill(pic, lav); sunkenLines(ui, pic);
  const PickerEntry* cur = sel >= 0 && sel < int(shown.size()) ? shown[size_t(sel)] : nullptr;
  if (cur) spriteCentred(ui, E.data, cur->appearance, {pic.x, pic.y + 1, pic.w - 2, pic.h - 2}, pic);   // centring as observed
  // List.
  ui.text(r.x + 175, r.y + 40, E.text(kTblCatalog, 0x76, "Buildings Available:"), "main9", rgb(0x63, 0x61, 0x9C));
  Rect lr{r.x + 173, r.y + 56, 166, 164};
  ui.fill(lr, lav); sunkenLines(ui, lr);
  const int rowH = 14, visible = (lr.h - 2) / rowH, total = int(shown.size());
  const bool scrolls = false;   // the original list shows no scroll bar, even for the 45 landmarks (wheel scrolls)
  if (ui.hover(lr) && ui.in.wheel) scroll -= ui.in.wheel;
  scroll = std::max(0, std::min(scroll, std::max(0, total - visible)));
  if (shown.empty()) paragraph(ui, {lr.x + 4, lr.y + 4, lr.w - 8, lr.h - 8}, E.text(kTblCatalog, pickerEmptyId(category), "No buildings are available."), "main9", ink);
  for (int i = 0; i < visible && scroll + i < total; i++) {
    const PickerEntry& e = *shown[size_t(scroll + i)];
    Rect row{lr.x + 1, lr.y + 1 + i * rowH, lr.w - 2 - (scrolls ? 16 : 0), rowH};
    bool on = sel == scroll + i;
    if (on) ui.fill(row, rgb(0x5A, 0x59, 0xFF));
    E.spot("row." + std::to_string(i), row);
    // Names wider than the row end in "..." (observed: "American Museum of Nat...").
    std::string nm = e.name;
    if (ui.textWidth(nm, "main9") > row.w - 4) {
      while (!nm.empty() && ui.textWidth(nm + "...", "main9") > row.w - 1) nm.pop_back();
      nm += "...";
    }
    ui.text(row.x + 2, row.y, nm, "main9", on ? white : e.placed ? grey : ink);
    if (ui.clicked(row)) sel = scroll + i;
  }
  if (scrolls) {
    const Image* sb = sk ? sk->image(skin_group::Sys, 0xE2B99E01) : nullptr;
    Rect up{lr.x + lr.w - 17, lr.y + 1, 16, 16}, dn{lr.x + lr.w - 17, lr.y + lr.h - 17, 16, 16};
    Rect tr{up.x, up.y + 16, 16, dn.y - up.y - 16};
    int range = total - visible, ty = tr.y + (tr.h - 16) * scroll / std::max(1, range);
    if (sb && sb->w >= 192) {
      for (int y = tr.y; y < tr.y + tr.h; y += 16) ui.blitRect(*sb, tr.x, y, 9 * 16, 0, 16, std::min(16, tr.y + tr.h - y));
      Skin::drawCell(ui, sb, 12, ui.hover(up) && ui.in.down ? 7 : 6, up.x, up.y);
      Skin::drawCell(ui, sb, 12, ui.hover(dn) && ui.in.down ? 11 : 10, dn.x, dn.y);
      Skin::drawCell(ui, sb, 12, 8, tr.x, ty);
    } else { ui.fill(tr, ui.panelDark); ui.button(up, "^"); ui.button(dn, "v"); }
    E.spot("scroll.up", up); E.spot("scroll.down", dn);
    if (ui.clicked(up)) scroll--;
    if (ui.clicked(dn)) scroll++;
    scroll = std::max(0, std::min(scroll, range));
  }
  // Details of the selected building.
  if (cur) {
    ui.text(r.x + 28, r.y + 248, cur->name, "main9", ink);
    if (cur->placed) ui.text(r.x + 180, r.y + 248, E.text(kTblCatalog, 0x70, "Already Placed"), "main9", ink);
    int y = r.y + 262;
    if (category == PickerCategory::Landmarks) {
      if (!cur->description.empty()) ui.text(r.x + 28, y, cur->description, "main9", ink);   // location line
    } else {
      ui.text(r.x + 28, y, subst(E.text(kTblCatalog, 2, "Cost: %s"), {E.money(cur->cost)}), "main9", ink);
      if (!cur->capacity.empty()) ui.text(r.x + 180, y, cur->capacity, "main9", ink);
      if (!cur->description.empty()) paragraph(ui, {r.x + 52, r.y + 290, 255, 74}, cur->description, "main9", ink);
    }
  }
  // Check mark on its plate.
  ui.fill({r.x + 317, r.y + 369, 47, 34}, rgb(0x7B, 0x92, 0xFF));
  bool canPick = cur && !cur->placed;
  Rect done{r.x + 320, r.y + 370, 42, 32};
  E.spot("ok", done);
  bool ok = false;
  if (!(sk && sk->imageButtonCells(ui, sk->image(skin_group::Sys, 0xA2DEFD9A), 4, done, "", false, canPick, ok))) ok = ui.button(done, E.app(0xB4, "OK"), canPick);
  if ((ok || (ui.in.key[K_ENTER] && canPick)) && canPick) {
    static const char* names[5] = {"power", "water", "garbage", "landmarks", "rewards"};
    R.add(Act::PickBuilding, cur->occupantId, cur->buildingType, 0, names[int(category)]);
    R.close = true;
  }
  if (ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }
  return R;
}

}  // namespace m3k
