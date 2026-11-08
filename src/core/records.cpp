// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/records.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include "core/growth.h"
#include "core/landvalue.h"
#include "core/newcity_template.h"
#include "core/refpack.h"
#include "core/valves.h"

namespace m3k {

// ---------------- header / marker helpers ----------------
RecHeader parseRecHeader(const uint8_t* p, size_t n) {
  RecHeader h;
  h.legacy = true; h.bodyOffset = 0;
  if (n < 7) return h;
  uint16_t ver = rd16(p); uint8_t fl = p[2];
  size_t pos = 3; uint8_t fl2 = 0;
  if (fl & 2) { if (n < 8) return h; fl2 = p[3]; pos = 4; }
  if (n < pos + 4 || rd32(p + pos) != 0xDEADBEEF) return h;
  h.legacy = false; h.version = ver; h.flags = fl; h.flags2 = fl2; h.bodyOffset = pos + 4;
  return h;
}
void writeRecHeader(Bytes& b, uint16_t version, bool markers) {
  put16(b, version); b.push_back(markers ? 3 : 2); b.push_back(0); put32(b, 0xDEADBEEF);
}
void putString(Bytes& b, const std::string& s) { put32(b, uint32_t(s.size())); b.insert(b.end(), s.begin(), s.end()); }
std::string readString(Reader& r) { uint32_t l = r.u32(); return r.str(l); }
void putMarker(Bytes& b, bool enabled, const char* text) { if (enabled) putString(b, text); }
void skipMarker(Reader& r, const RecHeader& h) { if (h.markers()) { uint32_t l = r.u32(); r.skip(l); } }

// ---------------- store ----------------
namespace {
bool isSegmentKey(const ResKey& k) { return k.type == 0x035F62A4 && k.group == 0x035F62A4; }
}  // namespace

RawRecordStore RawRecordStore::parse(const Bytes& file) {
  if (file.size() < 4 || rd32(file.data()) != kIxfMagic) throw FormatError("not an IXF file");
  struct E { ResKey key; uint32_t off, size; };
  std::vector<E> live;
  size_t pos = 4; bool term = false;
  while (pos + 20 <= file.size()) {
    const uint8_t* e = file.data() + pos;
    uint32_t g = rd32(e), i = rd32(e + 4), t = rd32(e + 8), off = rd32(e + 12), sz = rd32(e + 16);
    pos += 20;
    if (g == 0 && i == 0 && t == 0) { term = true; break; }
    if (off == 0xFFFFFFFFu || sz == 0xFFFFFFFFu) continue;
    if (off > file.size()) continue;
    live.push_back({{t, g, i}, off, sz});
  }
  if (!term) throw FormatError("IXF index not terminated");
  std::vector<uint32_t> offs;
  for (auto& e : live) offs.push_back(e.off);
  std::sort(offs.begin(), offs.end());
  std::vector<OuterRecord> all;
  for (auto& e : live) {
    auto nx = std::upper_bound(offs.begin(), offs.end(), e.off);
    size_t end = nx == offs.end() ? file.size() : *nx;
    size_t len = end - e.off;
    if (len < e.size) len = std::min<size_t>(e.size, file.size() - e.off);
    OuterRecord r; r.key = e.key; r.declaredSize = e.size;
    r.data.assign(file.begin() + e.off, file.begin() + e.off + len);
    all.push_back(std::move(r));
  }
  RawRecordStore st;
  // segment directory
  std::vector<ResKey> segKeys;
  const OuterRecord* dir = nullptr;
  for (auto& r : all) if (r.key == keys::SegmentDir) { dir = &r; break; }
  if (dir) {
    Reader r(dir->data);
    st.codec = r.u32();
    int32_t cnt = r.s32();
    for (int32_t i = 0; i < cnt && r.remaining() >= 12; i++) { ResKey k; k.type = r.u32(); k.group = r.u32(); k.instance = r.u32(); segKeys.push_back(k); }
  } else {
    for (auto& r : all) if (r.key == keys::Segment1) { segKeys.push_back(keys::Segment1); break; }
  }
  if (segKeys.empty()) throw FormatError("no packed segment");  // spec 10 7.1 rule 5: aborts the load
  st.segmentKeys = segKeys;
  size_t kept = 0; bool first = true;
  for (auto& r : all) {
    bool isSeg = r.key == keys::SegmentDir || std::find(segKeys.begin(), segKeys.end(), r.key) != segKeys.end();
    if (isSeg) { if (first) { st.segmentPos = kept; first = false; } continue; }
    st.outer.push_back(std::move(r)); kept++;
  }
  // segments
  for (size_t si = 0; si < segKeys.size(); si++) {
    const OuterRecord* seg = nullptr;
    for (auto& r : all) if (r.key == segKeys[si]) { seg = &r; break; }
    if (!seg) throw FormatError("segment record missing");
    Reader r(seg->data);
    uint32_t codec = r.u32();
    if (codec != 0x65 && codec != 0x67) throw FormatError("bad segment codec");
    std::string cv = readString(r);
    if (si == 0) { st.codec = codec; st.codecVersion = cv; }
    uint32_t packed = r.u32(), raw = r.u32();
    (void)packed;
    uint32_t blk = r.u32();
    if (blk < 4 || blk - 4 > r.remaining()) throw FormatError("bad segment block");
    Bytes inner = refpackDecode(r.ptr(), blk - 4);
    if (inner.size() != raw) throw FormatError("segment raw size mismatch");
    if (inner.size() < 8) throw FormatError("inner db too small");
    uint32_t count = rd32(inner.data()), dirOff = rd32(inner.data() + 4);
    if (dirOff > inner.size() || size_t(count) * 16 > inner.size() - dirOff) throw FormatError("inner db: bad directory");
    struct D { ResKey k; uint32_t off; size_t order; };
    std::vector<D> ds;
    for (uint32_t i = 0; i < count; i++) {
      const uint8_t* e = inner.data() + dirOff + size_t(i) * 16;
      D d{{rd32(e), rd32(e + 4), rd32(e + 8)}, rd32(e + 12), i};
      if (d.off >= dirOff) { st.orphans.push_back({d.k, int64_t(d.off) - int64_t(dirOff), uint32_t(si)}); continue; }
      ds.push_back(d);
    }
    std::vector<uint32_t> o2;
    for (auto& d : ds) o2.push_back(d.off);
    std::sort(o2.begin(), o2.end());
    std::stable_sort(ds.begin(), ds.end(), [](const D& a, const D& b) { return a.off < b.off; });
    for (auto& d : ds) {
      auto nx = std::upper_bound(o2.begin(), o2.end(), d.off);
      size_t end = nx == o2.end() ? dirOff : *nx;
      InnerRecord ir; ir.key = d.k; ir.segment = uint32_t(si);
      ir.data.assign(inner.begin() + d.off, inner.begin() + end);
      st.inner.push_back(std::move(ir));
    }
    st.cache_.push_back({inner, seg->data});
  }
  return st;
}

Bytes RawRecordStore::serialize() const {
  std::vector<ResKey> segKeys = segmentKeys;
  if (segKeys.empty() && !inner.empty()) segKeys.push_back(keys::Segment1);
  std::vector<OuterRecord> segRecs;
  for (size_t si = 0; si < segKeys.size(); si++) {
    Bytes in(8, 0);
    struct D { ResKey k; uint32_t off; };
    std::vector<D> dir;
    for (const auto& r : inner) if (r.segment == si) { dir.push_back({r.key, uint32_t(in.size())}); in.insert(in.end(), r.data.begin(), r.data.end()); }
    std::stable_sort(dir.begin(), dir.end(), [](const D& a, const D& b) {
      if (a.k.group != b.k.group) return a.k.group < b.k.group;
      if (a.k.instance != b.k.instance) return a.k.instance < b.k.instance;
      return a.k.type < b.k.type; });
    uint32_t dirOff = uint32_t(in.size());
    size_t cnt = dir.size();
    for (const auto& o : orphans) if (o.segment == si) cnt++;
    wr32(&in[0], uint32_t(cnt)); wr32(&in[4], dirOff);
    for (auto& e : dir) { put32(in, e.k.type); put32(in, e.k.group); put32(in, e.k.instance); put32(in, e.off); }
    for (const auto& o : orphans) if (o.segment == si) { put32(in, o.key.type); put32(in, o.key.group); put32(in, o.key.instance); put32(in, uint32_t(int64_t(dirOff) + o.offsetFromDir)); }
    OuterRecord sr; sr.key = segKeys[si];
    if (si < cache_.size() && cache_[si].raw == in) sr.data = cache_[si].record;
    else {
      Bytes comp = refpackEncode(in);
      put32(sr.data, codec); putString(sr.data, codecVersion);
      put32(sr.data, uint32_t(comp.size() + 4)); put32(sr.data, uint32_t(in.size())); put32(sr.data, uint32_t(comp.size() + 4));
      sr.data.insert(sr.data.end(), comp.begin(), comp.end());
    }
    sr.declaredSize = uint32_t(sr.data.size());
    segRecs.push_back(std::move(sr));
  }
  std::vector<const OuterRecord*> order;
  OuterRecord dirRec;
  if (!segKeys.empty()) {
    dirRec.key = keys::SegmentDir;
    put32(dirRec.data, codec); put32(dirRec.data, uint32_t(segKeys.size()));
    for (auto& k : segKeys) { put32(dirRec.data, k.type); put32(dirRec.data, k.group); put32(dirRec.data, k.instance); }
    dirRec.declaredSize = uint32_t(dirRec.data.size());
  }
  size_t at = std::min(segmentPos, outer.size());
  for (size_t i = 0; i < at; i++) order.push_back(&outer[i]);
  if (!segKeys.empty()) { order.push_back(&dirRec); for (auto& s : segRecs) order.push_back(&s); }
  for (size_t i = at; i < outer.size(); i++) order.push_back(&outer[i]);

  size_t n = order.size();
  size_t dataStart = 0x5004 + 0x5000 * (n / 1024);
  Bytes out;
  put32(out, kIxfMagic);
  size_t p = dataStart;
  for (auto* r : order) {
    put32(out, r->key.group); put32(out, r->key.instance); put32(out, r->key.type);
    put32(out, uint32_t(p)); put32(out, r->declaredSize);
    p += r->data.size();
  }
  for (int i = 0; i < 5; i++) put32(out, 0);
  out.resize(dataStart, 0);
  for (auto* r : order) out.insert(out.end(), r->data.begin(), r->data.end());
  return out;
}

OuterRecord* RawRecordStore::findOuter(const ResKey& k) { for (auto& r : outer) if (r.key == k) return &r; return nullptr; }
const OuterRecord* RawRecordStore::findOuter(const ResKey& k) const { for (auto& r : outer) if (r.key == k) return &r; return nullptr; }
void RawRecordStore::setOuter(const ResKey& k, Bytes data) {
  if (OuterRecord* r = findOuter(k)) { r->data = std::move(data); r->declaredSize = uint32_t(r->data.size()); return; }
  OuterRecord r; r.key = k; r.declaredSize = uint32_t(data.size()); r.data = std::move(data);
  if (segmentPos == SIZE_MAX) outer.push_back(std::move(r));
  else { outer.insert(outer.begin() + std::min(segmentPos, outer.size()), std::move(r)); segmentPos++; }
}
bool RawRecordStore::removeOuter(const ResKey& k) {
  for (size_t i = 0; i < outer.size(); i++) if (outer[i].key == k) {
    outer.erase(outer.begin() + i);
    if (segmentPos != SIZE_MAX && i < segmentPos) segmentPos--;
    return true;
  }
  return false;
}
InnerRecord* RawRecordStore::findInner(const ResKey& k) { for (auto& r : inner) if (r.key == k) return &r; return nullptr; }
const InnerRecord* RawRecordStore::findInner(const ResKey& k) const { for (auto& r : inner) if (r.key == k) return &r; return nullptr; }
void RawRecordStore::setInner(const ResKey& k, Bytes data) {
  if (InnerRecord* r = findInner(k)) { r->data = std::move(data); return; }
  InnerRecord r; r.key = k; r.data = std::move(data); inner.push_back(std::move(r));
}
bool RawRecordStore::removeInner(const ResKey& k) {
  for (size_t i = 0; i < inner.size(); i++) if (inner[i].key == k) { inner.erase(inner.begin() + i); return true; }
  return false;
}
void RawRecordStore::overlayFrom(const RawRecordStore& o) {
  for (const auto& r : o.outer) { if (isSegmentKey(r.key)) continue; setOuter(r.key, r.data); }
  for (const auto& r : o.inner) setInner(r.key, r.data);
}
void RawRecordStore::overlayKeys(const RawRecordStore& o, const std::vector<ResKey>& only) {
  auto want = [&](const ResKey& k) { return std::find(only.begin(), only.end(), k) != only.end(); };
  for (const auto& r : o.outer) if (want(r.key)) setOuter(r.key, r.data);
  for (const auto& r : o.inner) if (want(r.key)) setInner(r.key, r.data);
}

// ---------------- outer record structs ----------------
CityInfoRec CityInfoRec::parse(const Bytes& b) {
  CityInfoRec c;
  c.hdr = parseRecHeader(b.data(), b.size());
  Reader r(b); r.seek(c.hdr.bodyOffset); skipMarker(r, c.hdr);
  for (auto& v : c.appVersion) v = r.s32();
  c.constant3 = r.u32(); c.author = readString(r);
  c.mapW = r.u32(); c.mapH = r.u32(); c.mapDepth = r.u32();
  c.cityName = readString(r); c.mayorName = readString(r);
  c.dayOfYear = r.u32(); c.year = r.u32(); c.gameSpeed = r.u32(); c.foundingYear = r.u32();
  c.flagA = r.u8(); c.flagB = r.u8(); c.flagC = r.u8();
  return c;
}
Bytes CityInfoRec::write(bool m) const {
  Bytes d; writeRecHeader(d, 4, m); putMarker(d, m, "City Data Start");
  for (int32_t v : appVersion) put32(d, uint32_t(v));
  put32(d, constant3); putString(d, author);
  put32(d, mapW); put32(d, mapH); put32(d, mapDepth ? mapDepth : mapW);
  putString(d, cityName); putString(d, mayorName);
  put32(d, dayOfYear); put32(d, year); put32(d, gameSpeed); put32(d, foundingYear);
  d.push_back(flagA); d.push_back(flagB); d.push_back(flagC);
  putMarker(d, m, "City Data End");
  return d;
}
StateWordsRec StateWordsRec::parse(const Bytes& b) {
  StateWordsRec s; s.fieldsPresent = 0;
  Reader r(b);
  if (r.remaining() >= 4) { s.stateA = r.u32(); s.fieldsPresent = 1; } else return s;
  if (r.remaining() >= 4) { s.cityId = r.u32(); s.fieldsPresent = 2; } else return s;
  if (r.remaining() >= 4) { s.stateB = r.s32(); s.fieldsPresent = 3; } else return s;
  if (r.remaining() >= 4) { s.stateC = r.s32(); s.fieldsPresent = 4; } else return s;
  if (r.remaining() >= 4) { s.stateD = r.s32(); s.fieldsPresent = 5; } else return s;
  if (r.remaining() >= 4) { s.stateE = r.s32(); s.fieldsPresent = 6; }
  return s;
}
Bytes StateWordsRec::write() const {
  Bytes d; put32(d, 0);
  if (fieldsPresent <= 1) return d;
  put32(d, cityId); put32(d, uint32_t(stateB)); put32(d, uint32_t(stateC)); put32(d, uint32_t(stateD)); put32(d, uint32_t(stateE));
  return d;
}
PreviewHeader PreviewHeader::parse(const Bytes& b, int32_t fileKind) {
  PreviewHeader p; Reader r(b);
  for (auto& v : p.appVersion) v = r.s32();
  p.cityFields = fileKind == 3;
  if (p.cityFields) {
    p.population = r.u32(); p.funds = r.s32(); p.reserved = r.s32(); p.dayOfYear = r.u32(); p.year = r.u32();
    p.mayorName = readString(r); p.cityName = readString(r);
  }
  p.imageVersion = r.u32(); p.packedSize = r.u32(); p.width = r.u32(); p.height = r.u32(); p.pixelFormat = r.u32();
  p.imageOffset = r.pos();
  return p;
}
CheatIdsRec CheatIdsRec::parse(const Bytes& b) {
  CheatIdsRec c;
  RecHeader h = parseRecHeader(b.data(), b.size());
  Reader r(b); r.seek(h.bodyOffset);
  uint32_t n = std::min<uint32_t>(r.u32(), 1024);
  for (uint32_t i = 0; i < n && r.remaining() >= 4; i++) c.ids.push_back(r.u32());
  return c;
}
Bytes CheatIdsRec::write() const {
  Bytes d; writeRecHeader(d, 1); put32(d, uint32_t(ids.size())); for (uint32_t i : ids) put32(d, i); return d;
}

// ---------------- templates ----------------
namespace {
Bytes zeros(size_t n) { return Bytes(n, 0); }
void putChart(Bytes& b, uint32_t sample, uint32_t scale) { growth::ChartBlock::filled(sample, scale).write(b); }
}  // namespace

TemplateSet TemplateSet::defaults(int W, int H) {
  TemplateSet t;
  using namespace keys;
  { Bytes d; put32(d, 3); t.setOuter(FileKind, d); }
  { Bytes d; put32(d, 0); put32(d, uint32_t(std::time(nullptr))); put32(d, 1); put32(d, 54); put32(d, 1); put32(d, 54);
    // 0, id, 1, 54, 1, 54 [OBSERVED, spec 10 3.4]; id is a creation-time stamp [INFERRED]
    t.setOuter(StateWords, d); }
  { Bytes d; writeRecHeader(d, 1); put32(d, 1); putString(d, ""); t.setOuter(Misc79FD, d); }
  t.setOuter(UserBuildingSet, zeros(16));
  t.setOuter(SchemeSel, zeros(12));
  { Bytes d; writeRecHeader(d, 2); put32(d, 0); t.setOuter(ViewWindow, d); }
  { Bytes d; writeRecHeader(d, 1); float f[2] = {float(W) / 2, float(H) / 2};
    for (float v : f) { uint32_t u; std::memcpy(&u, &v, 4); put32(d, u); }
    put32(d, 2); put32(d, 0); t.setOuter(ViewIso, d); }

  // Inner records
  std::vector<uint8_t> zoneGrid(size_t(W) * H, 0);
  { growth::CrtRand crt; growth::ZoneManager zm; zm.init(W, H, crt); t.setInner(ZoneMgr, zm.write(zoneGrid)); }
  const int devZones[12] = {1, 2, 3, 5, 6, 7, 9, 10, 11, 14, 15, 17};
  const uint32_t devClass[12] = {growth::kDevResidential, growth::kDevResidential, growth::kDevResidential, growth::kDevCommercial, growth::kDevCommercial,
      growth::kDevCommercial, growth::kDevIndustrial, growth::kDevIndustrial, growth::kDevIndustrial, growth::kDevAirport, growth::kDevSeaport, growth::kDevLandfill};
  for (int i = 0; i < 12; i++) { Bytes d; put32(d, devClass[i]); t.setInner(zoneDeveloperClass(devZones[i]), d); }
  { Bytes d; writeRecHeader(d, 1); d.resize(d.size() + size_t(W / 4) * (H / 4), 0); t.setInner(Spacing, d); }
  { growth::ValveLayer vl; growth::ValveTuning vt; vl.init(W, H, vt);
    t.setInner(RciLayer, vl.writeRci()); t.setInner(ResLayer, vl.writeResidential()); t.setInner(ComLayer, vl.writeCommercial());
    t.setInner(IndLayer, vl.writeIndustrial()); t.setInner(WorldLayer, vl.writeWorld()); }
  { growth::LandValueLayer lv; growth::LandValueTuning lt; lv.init(W, H, lt); t.setInner(LandValue, lv.write()); }
  { Bytes d; d.resize(size_t(W / 4) * (H / 4), 0);  // aura: grid, phase 0, cursor (4,8), sums 0, cityAura 0, chart of 127
    d.push_back(0); put32(d, 4); put32(d, 8); put32(d, 0); put32(d, 0); d.push_back(0); putChart(d, 127, 260); t.setInner(Aura, d); }
  { Bytes d; writeRecHeader(d, 1); put32(d, 0); d.resize(d.size() + 480 + 40, 0); putChart(d, 0, 10); put32(d, 0); t.setInner(PowerLayer, d); }
  { Bytes d; writeRecHeader(d, 4); put32(d, 0); put32(d, 0); putChart(d, 0, 100); d.resize(d.size() + 8 + 8 + 16 + 16 + 16, 0); t.setInner(WaterLayer, d); }
  { Bytes d; writeRecHeader(d, 1); d.resize(d.size() + size_t(W) * H + 17, 0); t.setInner(Flammability, d); }
  // Disaster lists, empty (spec 6.5): tornado, whirlpool, locust (v2), space junk, riot, toxic cloud, earthquake, fire, UFO
  const uint32_t dis[9] = {0x02963821, 0xC446F87C, 0x45326359, 0x24889F78, 0x4296380E, 0x22963800, 0xC336F77C, 0x621CDA33, 0x0336F76A};
  for (uint32_t g : dis) {
    Bytes d; writeRecHeader(d, g == 0x45326359 ? 2 : 1); if (g == 0x45326359) put32(d, 0); put32(d, 0);
    t.setInner({0xE1F6ABE2, g, 0}, d);
  }
  { Bytes d; writeRecHeader(d, 505); d.resize(d.size() + 20, 0); t.setInner({0x206C6E7C, 0x21F6ABCA, 0}, d); }
  // Records observed in new cities saved by the original (spec 30) replace the guesses above.
  for (const auto& r : newCityConstantRecords()) t.setInner(r.key, r.data);
  for (const auto& r : newCityLayerRecords(W, H)) t.setInner(r.key, r.data);
  return t;
}

TemplateSet TemplateSet::fromStore(const RawRecordStore& s) {
  TemplateSet t;
  for (const auto& r : s.outer) if (!isSegmentKey(r.key)) t.outer_[r.key] = r.data;
  for (const auto& r : s.inner) t.inner_[r.key] = r.data;
  return t;
}
void TemplateSet::mergeFrom(const TemplateSet& o) {
  for (const auto& kv : o.inner_) inner_[kv.first] = kv.second;
  for (const auto& kv : o.outer_) outer_[kv.first] = kv.second;
}
size_t TemplateSet::fillMissing(RawRecordStore& s) const {
  size_t n = 0;
  for (const auto& kv : outer_) if (!s.findOuter(kv.first)) { s.setOuter(kv.first, kv.second); n++; }
  for (const auto& kv : inner_) if (!s.findInner(kv.first)) { s.setInner(kv.first, kv.second); n++; }
  return n;
}

}  // namespace m3k
