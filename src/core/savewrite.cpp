#include <algorithm>
#include <cstring>
#include "core/refpack.h"
#include "core/records.h"
#include "core/world.h"

namespace m3k {
namespace {

void hdr(Bytes& b, uint16_t version) { put16(b, version); b.push_back(2); b.push_back(0); put32(b, 0xDEADBEEF); }
void str(Bytes& b, const std::string& s) { put32(b, uint32_t(s.size())); b.insert(b.end(), s.begin(), s.end()); }
uint32_t packPlace(int x, int z, int y, int rot) {
  return uint32_t(x & 0x7FF) | (uint32_t(z & 0x7FF) << 11) | (uint32_t(y & 0xFF) << 22) | (uint32_t(rot & 3) << 30);
}

}  // namespace

static Bytes saveGenerated(const World& w) {
  // Inner records, in directory form.
  struct R { ResKey k; Bytes d; };
  std::vector<R> recs;

  { Bytes d; hdr(d, 3); put32(d, 0);  // heightScale placeholder, patched below
    float hs = w.heightScale; uint32_t hv; std::memcpy(&hv, &hs, 4); wr32(&d[d.size() - 4], hv);
    for (int row = 0; row <= w.W; row++) for (int z = 0; z <= w.H; z++) d.push_back(w.altAt(w.W - row, z));
    d.push_back(w.seaLevel);
    for (int row = 0; row <= w.W; row++) for (int z = 0; z <= w.H; z++) d.push_back(w.waterAt(w.W - row, z));
    d.insert(d.end(), w.tileBits.begin(), w.tileBits.end());
    recs.push_back({{0x206C6E7C, 0x21737DE5, 0}, std::move(d)}); }

  { Bytes d; hdr(d, 2); put32(d, uint32_t(w.occupants.size())); put32(d, 1);
    recs.push_back({{0x20631788, 0xA0AB89F0, 0}, std::move(d)}); }
  { Bytes d; hdr(d, 2);
    for (const auto& o : w.occupants) {
      put32(d, packPlace(o.x, o.z, o.y, o.rot));
      uint32_t fl = uint32_t(o.sizeX & 0x3F) | (uint32_t(o.sizeZ & 0x3F) << 6) | (1u << 20) | (1u << 27) |
                    (o.boolA ? 1u << 29 : 0) | (o.boolB ? 1u << 30 : 0);
      if (!o.name.empty()) fl |= 1u << 21;
      if (o.hasDate) fl |= 1u << 22;
      if (!o.intProps.empty()) fl |= 1u << 23;
      if (!o.strProps.empty()) fl |= 1u << 24;
      if (!o.floatProps.empty()) fl |= 1u << 25;
      put32(d, fl); put32(d, o.agentType);
      put32(d, o.keyType); put32(d, 0x0000057E); put32(d, o.occId);
      if (!o.name.empty()) str(d, o.name);
      if (o.hasDate) { put32(d, uint32_t(o.dateYear)); put32(d, uint32_t(o.dateDay)); }
      put32(d, o.state);
      if (!o.intProps.empty()) { put32(d, uint32_t(o.intProps.size())); for (auto& p : o.intProps) { put32(d, p.first); put32(d, p.second); } }
      if (!o.strProps.empty()) { put32(d, uint32_t(o.strProps.size())); for (auto& p : o.strProps) { put32(d, p.first); str(d, p.second); } }
      if (!o.floatProps.empty()) { put32(d, uint32_t(o.floatProps.size()));
        for (auto& p : o.floatProps) { uint32_t u; std::memcpy(&u, &p.second, 4); put32(d, p.first); put32(d, u); } }
    }
    recs.push_back({{0xE0FAADC7, 0xA0AB89F0, 1}, std::move(d)}); }

  { Bytes d; hdr(d, 4); put32(d, uint32_t(w.surface.size())); put32(d, uint32_t(w.pipes.size())); put32(d, uint32_t(w.subway.size())); put32(d, 1);
    recs.push_back({{0x206C6E7C, 0x2147C2DD, 0}, std::move(d)}); }
  const std::vector<NetPiece>* lists[3] = {&w.surface, &w.pipes, &w.subway};
  for (int i = 0; i < 3; i++) {
    if (lists[i]->empty()) continue;
    Bytes d; hdr(d, 4);
    for (const auto& p : *lists[i]) { put32(d, packPlace(p.x, p.z, p.y, p.rot)); put32(d, p.pieceWord ? p.pieceWord : (0x30000000u | p.pieceId)); }
    recs.push_back({{0x206C6E7C, 0x2147C2DD, uint32_t(i + 1)}, std::move(d)});
  }

  { Bytes d; hdr(d, 3); put32(d, w.W); put32(d, w.H);
    for (int row = 0; row < w.W; row++) for (int z = 0; z < w.H; z++) d.push_back(w.flora[w.tIdx(w.W - 1 - row, z)]);
    recs.push_back({{0x406B1196, 0x80AB8AB0, 0}, std::move(d)}); }

  { Bytes d;  // zone: no header; only grid + empty tables (nValve=0, tables zero)
    for (int row = 0; row < w.W; row++) for (int z = 0; z < w.H; z++) d.push_back(w.zone[w.tIdx(w.W - 1 - row, z)]);
    put32(d, 0);
    recs.push_back({{0x206C6E7C, 0x409FF3BA, 0}, std::move(d)}); }

  // Inner database: count, dirOffset, payloads, directory sorted by (group, instance, type).
  Bytes inner(8, 0);
  struct D { ResKey k; uint32_t off; };
  std::vector<D> dir;
  for (auto& r : recs) { dir.push_back({r.k, uint32_t(inner.size())}); inner.insert(inner.end(), r.d.begin(), r.d.end()); }
  std::sort(dir.begin(), dir.end(), [](const D& a, const D& b) {
    if (a.k.group != b.k.group) return a.k.group < b.k.group;
    if (a.k.instance != b.k.instance) return a.k.instance < b.k.instance;
    return a.k.type < b.k.type; });
  wr32(&inner[0], uint32_t(dir.size())); wr32(&inner[4], uint32_t(inner.size()));
  for (auto& e : dir) { put32(inner, e.k.type); put32(inner, e.k.group); put32(inner, e.k.instance); put32(inner, e.off); }

  Bytes comp = refpackEncode(inner);
  Bytes seg;
  put32(seg, 0x67); str(seg, "0.90"); put32(seg, uint32_t(comp.size() + 4)); put32(seg, uint32_t(inner.size())); put32(seg, uint32_t(comp.size() + 4));
  seg.insert(seg.end(), comp.begin(), comp.end());

  IxfWriter ix;
  { Bytes c; hdr(c, 4);
    put32(c, 2); put32(c, 0); put32(c, 949); put32(c, 0); put32(c, 4);  // appVersion 2.0.949.0 + format revision 4 [OBSERVED]
    str(c, "m3k"); put32(c, w.W); put32(c, w.H); put32(c, w.W);
    str(c, w.info.cityName); str(c, w.info.mayorName);
    put32(c, w.info.dayOfYear); put32(c, w.info.year); put32(c, w.info.gameSpeed); put32(c, w.info.foundingYear);
    c.push_back(w.info.flagA); c.push_back(w.info.flagB); c.push_back(0);
    ix.add({0x00000FA1, 0x617E198D, 0}, std::move(c)); }
  { Bytes d; put32(d, uint32_t(w.info.fileKind)); ix.add({0xC359837E, 0x43598384, 0}, std::move(d)); }
  { Bytes d; put32(d, w.scheme.buildingSet); put32(d, w.scheme.floraSet); put32(d, w.scheme.landscape); ix.add({0x0525A6B9, 0x4525A6E2, 1}, std::move(d)); }
  { Bytes d; hdr(d, 1); float f[2] = {w.view.centreX, w.view.centreZ};
    for (float v : f) { uint32_t u; std::memcpy(&u, &v, 4); put32(d, u); }
    put32(d, uint32_t(w.view.zoom)); put32(d, uint32_t(w.view.rotation)); ix.add({0x620EDFCE, 0xA20FB3B9, 0}, std::move(d)); }
  { Bytes d; put32(d, 0x67); put32(d, 1); put32(d, 0x035F62A4); put32(d, 0x035F62A4); put32(d, 1); ix.add({0x035F62A4, 0x035F62A4, 0}, std::move(d)); }
  ix.add({0x035F62A4, 0x035F62A4, 1}, std::move(seg));
  return ix.build();
}


namespace {
bool sameOcc(const Occupant& a, const Occupant& b) {
  return a.x == b.x && a.z == b.z && a.y == b.y && a.rot == b.rot && a.sizeX == b.sizeX && a.sizeZ == b.sizeZ &&
         a.occId == b.occId && a.keyType == b.keyType && a.agentType == b.agentType && a.state == b.state && a.name == b.name &&
         a.hasDate == b.hasDate && a.dateYear == b.dateYear && a.dateDay == b.dateDay && a.intProps == b.intProps &&
         a.strProps == b.strProps && a.floatProps == b.floatProps && a.boolA == b.boolA && a.boolB == b.boolB;
}
bool sameNet(const std::vector<NetPiece>& a, const std::vector<NetPiece>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (a[i].x != b[i].x || a[i].z != b[i].z || a[i].y != b[i].y || a[i].rot != b[i].rot || a[i].pieceWord != b[i].pieceWord) return false;
  return true;
}
}  // namespace

// With w.raw: every record of the loaded file is kept verbatim; a record the engine models is replaced by
// freshly generated data only when the model differs from what that record decodes to (so unknown
// occupant fields, preview images, unknown records etc. survive). Layer records flushed by City::flush
// already live in w.raw.
Bytes saveCity(const World& w) {
  Bytes legacy = saveGenerated(w);
  if (!w.raw) return legacy;
  RawRecordStore gen = RawRecordStore::parse(legacy);
  const RawRecordStore& src = *w.raw;
  RawRecordStore out;
  std::unique_ptr<World> base;
  if (src.findOuter(keys::CityInfo) && !src.inner.empty()) {
    out = src;
    try { base = std::make_unique<World>(loadCity(src.serialize())); } catch (const FormatError&) { base.reset(); }
  }
  if (!base) {
    out = gen;
    for (const auto& r : src.inner) if (!out.findInner(r.key)) out.setInner(r.key, r.data);
    for (const auto& r : src.outer) if (!out.findOuter(r.key)) out.setOuter(r.key, r.data);
    return out.serialize();
  }
  auto takeInner = [&](const ResKey& k) { if (const InnerRecord* r = gen.findInner(k)) out.setInner(k, r->data); else out.removeInner(k); };
  auto takeOuter = [&](const ResKey& k) { if (const OuterRecord* r = gen.findOuter(k)) out.setOuter(k, r->data); };
  const World& b = *base;
  bool dimsOk = b.W == w.W && b.H == w.H;
  if (!dimsOk || b.alt != w.alt || b.water != w.water || b.seaLevel != w.seaLevel || b.heightScale != w.heightScale ||
      b.tileBits != w.tileBits || !out.findInner(keys::Terrain)) takeInner(keys::Terrain);
  if (!dimsOk || b.flora != w.flora) takeInner(keys::Flora);
  if (!dimsOk || !out.findInner(keys::ZoneMgr)) takeInner(keys::ZoneMgr);
  else if (b.zone != w.zone) {
    InnerRecord* r = out.findInner(keys::ZoneMgr);
    size_t N = size_t(w.W) * w.H;
    if (r && r->data.size() >= N)
      for (int row = 0; row < w.W; row++)
        for (int z = 0; z < w.H; z++) r->data[size_t(row) * w.H + z] = w.zone[w.tIdx(w.W - 1 - row, z)];
    else takeInner(keys::ZoneMgr);
  }
  bool occSame = b.occupants.size() == w.occupants.size();
  for (size_t i = 0; occSame && i < b.occupants.size(); i++) occSame = sameOcc(b.occupants[i], w.occupants[i]);
  if (!occSame || !out.findInner(keys::OccEntries)) { takeInner(keys::OccCounters); takeInner(keys::OccEntries); }
  if (!sameNet(b.surface, w.surface) || !sameNet(b.pipes, w.pipes) || !sameNet(b.subway, w.subway)) {
    takeInner(keys::NetHeader);
    for (uint32_t i = 1; i <= 3; i++) takeInner({0x206C6E7C, 0x2147C2DD, i});
  }
  const CityInfo &ci = b.info, &ni = w.info;
  if (ci.cityName != ni.cityName || ci.mayorName != ni.mayorName || ci.dayOfYear != ni.dayOfYear || ci.year != ni.year ||
      ci.gameSpeed != ni.gameSpeed || ci.foundingYear != ni.foundingYear || ci.flagA != ni.flagA || ci.flagB != ni.flagB) {
    OuterRecord* o = out.findOuter(keys::CityInfo);
    try {
      CityInfoRec r = CityInfoRec::parse(o->data);
      r.cityName = ni.cityName; r.mayorName = ni.mayorName; r.dayOfYear = uint32_t(ni.dayOfYear); r.year = uint32_t(ni.year);
      r.gameSpeed = uint32_t(ni.gameSpeed); r.foundingYear = uint32_t(ni.foundingYear);
      r.flagA = ni.flagA; r.flagB = ni.flagB;
      out.setOuter(keys::CityInfo, r.write(r.hdr.markers()));
    } catch (const FormatError&) { takeOuter(keys::CityInfo); }
  }
  if (b.info.fileKind != w.info.fileKind) takeOuter(keys::FileKind);
  if (b.scheme.buildingSet != w.scheme.buildingSet || b.scheme.floraSet != w.scheme.floraSet || b.scheme.landscape != w.scheme.landscape)
    takeOuter(keys::SchemeSel);
  if (b.view.valid && (b.view.centreX != w.view.centreX || b.view.centreZ != w.view.centreZ || b.view.zoom != w.view.zoom ||
                       b.view.rotation != w.view.rotation))
    takeOuter(keys::ViewIso);
  return out.serialize();
}

}  // namespace m3k
