// Adapter: INetworkMap over a World (spec/12-networks.md). Header-only.
// Pieces live in World::surface/pipes/subway; other occupants (buildings) block the surface layer.
// Occupant flags come from `flagsOf(occupantId)`; build one from the Set files with
// flagsFromRuleSets() when real occupant attributes are not at hand.
#pragma once
#include <algorithm>
#include <functional>
#include <unordered_map>
#include "core/tiler.h"
#include "core/world.h"

namespace m3k {

class WorldNetworkMap : public INetworkMap {
 public:
  using FlagsFn = std::function<uint32_t(uint32_t occupantId)>;
  using ExitFn = std::function<unsigned(uint32_t occupantId, int rot)>;

  WorldNetworkMap(World& w, FlagsFn flagsOf) : w_(w), flagsOf_(std::move(flagsOf)) { rebuild(); }
  void setExitFn(ExitFn f) { exitFn_ = std::move(f); }
  void setBuildingFlags(FlagsFn f) { buildingFlags_ = std::move(f); }  // flags of non-network occupants by occId
  void setWaterTest(std::function<bool(int, int)> f) { waterFn_ = std::move(f); }

  // Rebuild indices after external edits of World.
  void rebuild() {
    for (int l = 0; l < kNumLayers; l++) idx_[l].clear();
    for (int l = 0; l < kNumLayers; l++) {
      auto& v = vec(NetLayer(l));
      for (size_t i = 0; i < v.size(); i++) idx_[l][key(v[i].x, v[i].z)] = i;
    }
    bld_.clear();
    for (size_t i = 0; i < w_.occupants.size(); i++) {
      const Occupant& o = w_.occupants[i];
      for (int dx = 0; dx < o.sizeX; dx++)
        for (int dz = 0; dz < o.sizeZ; dz++) bld_[key(o.x + dx, o.z + dz)] = i;
    }
  }

  int width() const override { return w_.W; }
  int height() const override { return w_.H; }
  int cornerHeight(int vx, int vz) const override {
    if (vx < 0 || vz < 0 || vx > w_.W || vz > w_.H) return 23;
    return w_.altAt(vx, vz);
  }
  void setCornerHeight(int vx, int vz, int h) override {
    if (vx < 0 || vz < 0 || vx > w_.W || vz > w_.H) return;
    w_.alt[w_.vIdx(vx, vz)] = uint8_t(h < 0 ? 0 : h > 255 ? 255 : h);
  }
  bool isWaterTile(int x, int z) const override {
    if (waterFn_) return waterFn_(x, z);
    if (!w_.inTiles(x, z)) return false;
    // all four corners have water surface above terrain
    for (int dx = 0; dx <= 1; dx++)
      for (int dz = 0; dz <= 1; dz++)
        if (w_.waterAt(x + dx, z + dz) <= w_.altAt(x + dx, z + dz)) return false;
    return true;
  }
  TileOcc occupantAt(int x, int z, NetLayer layer) const override {
    TileOcc o;
    auto& v = vec(layer);
    auto it = idx_[layer].find(key(x, z));
    if (it != idx_[layer].end()) {
      const NetPiece& p = v[it->second];
      o.present = true; o.id = p.pieceId; o.rot = p.rot; o.flags = flagsOf_ ? flagsOf_(p.pieceId) : 0;
      return o;
    }
    if (layer == LAYER_SUBWAY) {   // a subway station (12360 / 12361) occupies the subway layer: always considered connected (flags 0x40100, spec 12 4.6)
      auto b = bld_.find(key(x, z));
      if (b != bld_.end()) {
        const Occupant& oc = w_.occupants[b->second];
        if (oc.agentType == 12360 || oc.agentType == 12361) {
          o.present = true; o.id = oc.occId; o.rot = oc.rot; o.flags = 0x40100; o.removable = false; o.fixed = true;
        }
      }
      return o;
    }
    if (layer == LAYER_SURFACE) {
      auto b = bld_.find(key(x, z));
      if (b != bld_.end()) {
        const Occupant& oc = w_.occupants[b->second];
        o.present = true; o.id = oc.occId; o.rot = oc.rot;
        // Spec 13 §3.1a: prefer the occupant's class flag word (generic buildings = 0x104).
        o.flags = oc.classFlags ? oc.classFlags
                                : (buildingFlags_ ? buildingFlags_(oc.occId) : runtimeOccupantFlags(oc.agentType));
        o.removable = true;
      }
    }
    return o;
  }
  void placePiece(int x, int z, NetLayer layer, uint32_t id, int rot, int altitude) override {
    if (!w_.inTiles(x, z)) return;
    auto& v = vec(layer);
    auto it = idx_[layer].find(key(x, z));
    NetPiece np;
    np.x = x; np.z = z; np.y = altitude; np.rot = rot; np.pieceId = id; np.pieceWord = makeWord(id);
    if (it != idx_[layer].end()) v[it->second] = np;
    else { idx_[layer][key(x, z)] = v.size(); v.push_back(np); }
  }
  void setPieceSpan(int x, int z, NetLayer layer, int dir, int len) override {
    auto it = idx_[layer].find(key(x, z));
    if (it == idx_[layer].end()) return;
    NetPiece& p = vec(layer)[it->second];
    p.pieceWord = (p.pieceWord & ~(0x7FFu << 16)) | (1u << 16) | (uint32_t(dir & 3) << 17) | (uint32_t(len & 0xFF) << 19);
  }
  void removePiece(int x, int z, NetLayer layer) override {
    auto& v = vec(layer);
    auto it = idx_[layer].find(key(x, z));
    if (it == idx_[layer].end()) return;
    size_t i = it->second;
    idx_[layer].erase(it);
    if (i + 1 != v.size()) {
      v[i] = v.back();
      idx_[layer][key(v[i].x, v[i].z)] = i;
    }
    v.pop_back();
  }
  bool hasConnection(int kind, int x, int z) const override { return conns_.count({kind, x, z}) != 0; }
  void setConnection(int kind, int x, int z, bool on) override {
    if (on) conns_.insert({kind, x, z}); else conns_.erase({kind, x, z});
  }
  unsigned exitMask(uint32_t id, int rot) const override { return exitFn_ ? exitFn_(id, rot) : 0xF; }
  bool canChangeCorner(int vx, int vz) const override {
    for (int dx = -1; dx <= 0; dx++)
      for (int dz = -1; dz <= 0; dz++) {
        int x = vx + dx, z = vz + dz;
        if (!w_.inTiles(x, z)) continue;
        if (idx_[LAYER_SURFACE].count(key(x, z))) return false;
        if (bld_.count(key(x, z))) return false;
      }
    return true;
  }
  std::vector<PlacedPiece> pieces(NetLayer layer) const override {
    std::vector<PlacedPiece> out;
    for (auto& p : vec(layer)) { PlacedPiece q; q.x = p.x; q.z = p.z; q.y = p.y; q.rot = p.rot; q.id = p.pieceId; out.push_back(q); }
    std::sort(out.begin(), out.end(), [](const PlacedPiece& a, const PlacedPiece& b) { return a.x != b.x ? a.x < b.x : a.z < b.z; });
    return out;
  }
  const std::set<std::tuple<int, int, int>>& connections() const { return conns_; }

 private:
  static uint64_t key(int x, int z) { return (uint64_t(uint32_t(x)) << 32) | uint32_t(z); }
  static uint32_t makeWord(uint32_t id) { return (id & 0xFFFF) | 0x30000000u; }
  std::vector<NetPiece>& vec(NetLayer l) { return l == LAYER_SURFACE ? w_.surface : l == LAYER_PIPE ? w_.pipes : w_.subway; }
  const std::vector<NetPiece>& vec(NetLayer l) const { return l == LAYER_SURFACE ? w_.surface : l == LAYER_PIPE ? w_.pipes : w_.subway; }

  World& w_;
  FlagsFn flagsOf_, buildingFlags_;
  ExitFn exitFn_;
  std::function<bool(int, int)> waterFn_;
  std::unordered_map<uint64_t, size_t> idx_[kNumLayers];
  std::unordered_map<uint64_t, size_t> bld_;
  std::set<std::tuple<int, int, int>> conns_;
};

// Flags synthesised from Set files: membership in a network's set gives its default flags.
inline WorldNetworkMap::FlagsFn flagsFromRuleSets(const TilingRuleSet& rules) {
  return [&rules](uint32_t id) -> uint32_t {
    uint32_t f = 0;
    static const int order[] = {NK_ROAD, NK_HIGHWAY, NK_RAIL, NK_SUBWAY, NK_PIPE, NK_POWER};
    for (int k : order)
      if (rules.inSet(netKindToTable(k), id)) f |= defaultFlagsForKind(k);
    return f;
  };
}

}  // namespace m3k
