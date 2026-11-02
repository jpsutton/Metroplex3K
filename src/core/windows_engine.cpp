// Engine options dialog: Metroplex3K-only settings (no counterpart in the original). Uses the original dialog
// frame, check boxes and check-mark OK button when the skin provides them, the flat fallback look otherwise.
#include "core/skin.h"
#include "core/windows.h"

namespace m3k {

using namespace wui;

EngineOptions EngineOptions::fromPreferences(const Preferences& p) {
  EngineOptions s;
  s.showFps = p.getBool("engine.showFps", s.showFps);
  s.vsync = p.getBool("vsync", s.vsync);   // shared with OwnPrefs
  return s;
}

void EngineOptions::toPreferences(Preferences& p) const {
  p.setBool("engine.showFps", showFps);
  p.setBool("vsync", vsync);
}

WindowResult EngineOptionsDialog::draw(Ui& ui, const WinEnv& E, EngineOptions& s) {
  WindowResult R;
  const Skin* sk = ui.skin;
  Rect r = centered(ui, 320, 180);
  if (frame(ui, E, r, "Metroplex3K Options")) { R.close = true; R.add(Act::Close); }
  if (check(ui, E, "showFps", {r.x + 20, r.y + 52, r.w - 40, 22}, "Show frame counter", s.showFps))
    R.add(Act::PrefsChanged, 0, 0, 0, "showFps");
  if (check(ui, E, "vsync", {r.x + 20, r.y + 78, r.w - 40, 22}, "Vertical sync", s.vsync))
    R.add(Act::PrefsChanged, 0, 0, 0, "vsync");
  Rect done{r.x + r.w - 58, r.y + r.h - 44, 42, 32};
  E.spot("ok", done);
  bool ok = false;
  if (!(sk && sk->imageButtonCells(ui, sk->image(skin_group::Sys, 0xA2DEFD9A), 4, done, "", false, true, ok))) ok = ui.button(done, E.app(0xB4, "OK"));
  if (ok || ui.in.key[K_ESC] || ui.in.key[K_ENTER]) { R.close = true; R.add(Act::Done); }
  return R;
}

}  // namespace m3k
