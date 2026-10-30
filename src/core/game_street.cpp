// Street automata (vehicles, pedestrians, trains, boats) glue: host adapter, source sync, view and sprites.
#include <algorithm>
#include <cmath>

#include "core/game.h"

namespace m3k {
using namespace automata_ids;

void Game::StreetHost::rebuild() {
  if (!w) return;
  piece.assign(size_t(w->W) * w->H, -1);
  for (size_t i = 0; i < w->surface.size(); i++) {
    const NetPiece& p = w->surface[i];
    if (w->inTiles(p.x, p.z)) piece[w->tIdx(p.x, p.z)] = int32_t(i);
  }
}

bool Game::StreetHost::pieceAt(int x, int y, AutoNet k, PieceRef& out) const {
  if (!w || !w->inTiles(x, y) || piece.empty()) return false;
  int i = piece[w->tIdx(x, y)];
  if (i < 0) return false;
  if (k == AutoNet::Rail && w->surface[i].pieceId < 1000) return false;   // crude: rail pieces are not distinguished here
  out.pieceId = w->surface[i].pieceId; out.rotation = w->surface[i].rot;
  return true;
}

bool Game::StreetHost::isWater(int x, int y) const { return w && w->inTiles(x, y) && w->altAt(x, y) < w->waterAt(x, y); }

bool Game::StreetHost::pedEligible(int x, int y) const {
  return w && w->inTiles(x, y) && c && (c->occupantAt(x, y) >= 0 || (w->zone[w->tIdx(x, y)] != 0));
}

int Game::StreetHost::trafficDensity(int x, int y) const {
  if (!w || !c) return 0;
  // Cheap stand-in for the traffic layer: density of road pieces around the tile.
  int n = 0;
  for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) if (c->isRoadTile(x + dx, y + dy)) n++;
  return std::min(255, n * 12);
}

int Game::StreetHost::groundHeight(int x, int y) const {
  if (!w || !w->W || !w->H) return 0;
  x = std::clamp(x, 0, w->W - 1); y = std::clamp(y, 0, w->H - 1);
  if (!piece.empty()) {
    int i = piece[w->tIdx(x, y)];
    if (i >= 0) return w->surface[size_t(i)].y * 256;
  }
  return std::max(w->altAt(x, y), w->waterAt(x, y)) * 256;
}

void Game::setupStreetSim() {
  street_.reset();
  streetHost_.reset();
  streetSources_.clear();
  streetOccCount_ = streetNetCount_ = 0;
  if (!haveData_) return;   // without game data there are no vehicle/people sprites to draw
  if (!automataLoaded_) {
    automata_ = AutomataData();
    static const uint32_t types[] = {kVAttribType, kVclProdType, kPAttribType, kPplProdType, kAnSprAttType, kPPathType,
                                     kVPathType, kTrBlkType, kScriptType, kTrnProdType, kAirTrMgrType, kSpriteInstType};
    for (uint32_t t : types)
      for (const ResKey& k : data_.db().keysOfType(t)) {
        ResourceRef r = data_.db().find(k);
        if (!r) continue;
        size_t n = r.declared && r.declared < r.available ? r.declared : r.available;
        automata_.addRecord(k, Bytes(r.data, r.data + n));
      }
    automataLoaded_ = true;
  }
  streetHost_ = std::make_unique<StreetHost>();
  streetHost_->w = &world_; streetHost_->c = &city();
  streetHost_->rebuild();
  StreetSim::Config cfg;
  cfg.seed = 1u + uint32_t(world_.W);
  street_ = std::make_unique<StreetSim>(automata_, *streetHost_, cfg);
}

void Game::syncStreetSources() {
  if (!street_) return;
  streetHost_->rebuild();
  for (int id : streetSources_) street_->removeBuilding(id);
  streetSources_.clear();
  for (const Occupant& o : world_.occupants) {
    if (o.state != 1) continue;
    const OccAttr* a = data_.occupant(o.occId, false, o.keyType == T_OCC_ATTR_PORT);
    if (!a) continue;
    if (a->agentType == 12330) { street_->addSeaport(o.x, o.z); continue; }
    if (a->agentType == 12310) { street_->addAirport(o.x, o.z); continue; }
    if (a->agentType == 12230) { street_->addStation(o.x, o.z, false); continue; }
    if (a->agentType == 12360 || a->agentType == 12361) { street_->addStation(o.x, o.z, true); continue; }
    if (a->pedProdKey.instance || a->vehProdKey.instance) streetSources_.push_back(street_->addBuilding(o.x, o.z, a->pedProdKey, a->vehProdKey));
  }
  streetOccCount_ = world_.occupants.size();
  streetNetCount_ = world_.surface.size();
}

void Game::streetSimFrame(uint32_t dtMs) {
  scene_.sprites.clear();
  if (!street_ || !world_.W) return;
  // Detail preferences: traffic/sims off disables the system (spec 08 preferences).
  if (prefsState_.traffic == 3 && prefsState_.sims == 2) return;
  if (world_.occupants.size() != streetOccCount_ || world_.surface.size() != streetNetCount_ || (++streetAcc_ % 300) == 0)
    syncStreetSources();
  streetHost_->month = city().clock.date().month;
  streetHost_->day = city().clock.date().day;
  // Visible tile rectangle from the screen corners.
  AutoView v;
  float us[4], vs[4];
  const int cx[4] = {0, W_, 0, W_}, cy[4] = {0, 0, H_, H_};
  for (int i = 0; i < 4; i++) renderer_.screenToView(cam_, cx[i], cy[i], us[i], vs[i]);
  float umin = *std::min_element(us, us + 4), umax = *std::max_element(us, us + 4);
  float vmin = *std::min_element(vs, vs + 4), vmax = *std::max_element(vs, vs + 4);
  int xa, za, xb, zb;
  Renderer::viewToWorldVertex(cam_.rot, world_.W, world_.H, int(umin) - 4, int(vmin) - 4, xa, za);
  Renderer::viewToWorldVertex(cam_.rot, world_.W, world_.H, int(umax) + 4, int(vmax) + 4, xb, zb);
  v.x0 = std::max(0, std::min(xa, xb)); v.x1 = std::min(world_.W - 1, std::max(xa, xb));
  v.y0 = std::max(0, std::min(za, zb)); v.y1 = std::min(world_.H - 1, std::max(za, zb));
  v.zoom = cam_.zoom; v.rotation = cam_.rot;
  street_->setView(v);
  if (!paused_ && !anyBlockingWindow()) {
    street_->update(int64_t(dtMs) * 1000);
  }
  for (const AutomatonSprite& s : street_->sprites()) {
    SceneSprite sp;
    sp.category = s.category; sp.spriteId = s.spriteId;
    sp.x = int32_t(s.tileX * 256.0); sp.z = int32_t(s.tileY * 256.0);
    sp.height = int32_t(s.height);
    sp.magnify2x = s.flags == 2;
    sp.moving = true;
    scene_.sprites.push_back(sp);
  }
  for (const SoundEvent& e : street_->takeSoundEvents()) if (e.type == SoundEvent::Start || e.type == SoundEvent::Siren) sound(e.soundId);
}

}  // namespace m3k
