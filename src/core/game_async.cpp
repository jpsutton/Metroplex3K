// Async sim (opt-in): the sim day tick runs on a worker thread while the owner thread renders and draws the HUD
// from snapshots. See SimWorker for the thread/mailbox contract.
//
// Rules the code below relies on:
//  * The worker only touches sim state (world_, city(), sim_). Anything that touches Game-owned UI state is handed to
//    the owner through simWorker_ (sounds, messages, news, and the Game-bound schedule steps).
//  * While a job runs the owner must not touch sim state. Frames that cannot avoid it join the job first
//    (syncSim); "fast" frames (no input, no windows, no tool) read only snapshots: renderWorld_, hud_ and the street
//    host caches, all refreshed while the sim is idle.
#include <algorithm>

#include "core/game.h"

namespace m3k {

Game::HudSnap Game::liveHud() const {
  HudSnap h;
  h.dayOfYear = world_.info.dayOfYear;
  h.year = world_.info.year;
  h.funds = sim_.funds;
  h.pop = sim_.population();
  h.valveR = sim_.valveR; h.valveC = sim_.valveC; h.valveI = sim_.valveI;
  const cal::Date d = const_cast<Game*>(this)->city().clock.date();
  h.month = d.month; h.day = d.day; h.clockYear = d.year;
  h.neighbourName = const_cast<Game*>(this)->city().neighbours.neighbours[0].cityName;
  for (int i = 0; i < 4; i++) h.neighbourNames[size_t(i)] = const_cast<Game*>(this)->city().neighbours.neighbours[i].cityName;
  return h;
}

const Game::HudSnap& Game::hudNow() {
  if (!simBusy() && hudFrame_ != frameNo_) { hud_ = liveHud(); hudFrame_ = frameNo_; }
  return hud_;
}

bool Game::anyWindowOpen() const {
  return anyBlockingWindow() || showMap_ || showGraphs_ || showCharts_ || showNeigh_;
}

// A frame may run while the tick is still going only when it cannot reach sim state: nothing was clicked or typed,
// no window or message is open, and no tool is armed (previews and quotes read the city).
bool Game::canRunFast(const Input& in) const {
  if (scr_ != Screen::InCity || !simReady_) return false;
  if (tool_ != ToolKind::None || anyWindowOpen() || !msgText_.empty() || cheatOpen_ || dragging_ || panning_) return false;
  if (ui_.focusId != 0) return false;
  if (screenPending_) return false;   // syncScreenPending() is about to change the city's pending count
  if (in.pressed || in.released || in.rightPressed || in.rightDown || in.down) return false;
  if (!in.keyDowns.empty() || !in.text.empty()) return false;
  for (bool k : in.key) if (k) return false;
  return true;
}

void Game::setAsyncSim(bool on) {
  if (on == asyncSim_) return;
  if (!on) syncSim();
  asyncSim_ = on;
  streetCacheDirty_ = true;
  if (world_.W) bindRenderWorld();
}

void Game::syncSim() {
  if (!asyncSim_ || !simJobInFlight_) return;
  simWorker_.wait();
  finishSimJob();
}

void Game::bindRenderWorld() {
  if (asyncSim_ && !simJobInFlight_) renderWorld_ = world_;
  renderer_.setWorld(asyncSim_ ? &renderWorld_ : &world_, haveData_ ? &data_ : nullptr);
  if (streetHost_) {
    streetHost_->w = asyncSim_ ? &renderWorld_ : &world_;
    if (!asyncSim_) streetHost_->clearCaches();
  }
}

void Game::asyncFrameBegin(const Input& in) {
  fastFrame_ = false;
  if (!asyncSim_) return;
  simDayThisFrame_ = false;
  if (!simJobInFlight_) return;
  simWorker_.service();
  if (!simWorker_.busy()) { finishSimJob(); return; }
  if (canRunFast(in)) { fastFrame_ = true; return; }
  syncSim();
}

void Game::asyncFrameEnd() {
  if (!asyncSim_ || simJobInFlight_) return;
  publishSnapshots(false);
  if (scr_ != Screen::InCity || !simReady_ || paused_ || !managersReady_ || !world_.W) return;
  if (city().hold()) return;   // pause or modal latch still raised (e.g. the frame that left the main menu)
  if (simAccum_ < 1.0 || vortexMs_ > 0 || minuteModeOn_ || disasters_.anyActiveNonParade()) return;
  simAccum_ -= 1.0;
  launchSimJob();
}

void Game::launchSimJob() {
  simJobInFlight_ = true;
  const bool disasterActive = disasters_.anyActiveNonParade();
  simWorker_.start([this, disasterActive] { daySimStep(disasterActive); });
}

void Game::daySimStep(bool disasterActive) {
  // Sync item-7 gate before the day cascade (Q-BJ); emergency + scenario run as items 1 and 5 (Q-BI).
  city().sched.setDisasterActive(disasterActive);
  if (!simWorker_.onWorker()) { sim_.tickDay(); return; }
  // On the worker the schedule's hold wait must never block: it polls until the owner releases the hold, but the
  // owner may be joining this very job (frame start, quit). A hold raised mid-job takes effect at the next launch.
  auto savedHold = std::move(city().sched.isHold);
  city().sched.isHold = nullptr;
  sim_.tickDay();
  city().sched.isHold = std::move(savedHold);
}

void Game::finishSimJob() {
  simJobInFlight_ = false;
  simWorker_.service();
  simDayThisFrame_ = true;
  bumpCityVisual();
  dayPostStep();
  publishSnapshots(true);
}

void Game::publishSnapshots(bool jobJustFinished) {
  if (!asyncSim_ || simJobInFlight_ || !world_.W) return;
  renderWorld_ = world_;
  hud_ = liveHud();
  if (streetHost_ && (jobJustFinished || streetCacheDirty_ || cityVisualSerial_ != streetCacheSerial_)) {
    streetHost_->rebuild();
    streetHost_->refreshCaches();
    streetCacheDirty_ = false;
    streetCacheSerial_ = cityVisualSerial_;
  }
}

}  // namespace m3k
