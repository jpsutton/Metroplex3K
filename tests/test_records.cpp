// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include "core/growth.h"
#include "core/landvalue.h"
#include "core/records.h"
#include "core/refpack.h"
#include "core/valves.h"
#include "core/world.h"
#include "testutil.h"
using namespace m3k;

static Bytes literalRefpack(const Bytes& raw) {  // valid, non-canonical: literal-only RefPack stream
  Bytes o = {0x10, 0xFB, uint8_t(raw.size() >> 16), uint8_t(raw.size() >> 8), uint8_t(raw.size())};
  size_t pos = 0;
  while (raw.size() - pos >= 4) {
    size_t n = std::min<size_t>((raw.size() - pos) / 4 * 4, 112);
    o.push_back(uint8_t(0xE0 + (n / 4 - 1)));
    o.insert(o.end(), raw.begin() + pos, raw.begin() + pos + n); pos += n;
  }
  size_t rem = raw.size() - pos;
  o.push_back(uint8_t(0xFC + rem));
  o.insert(o.end(), raw.begin() + pos, raw.end());
  return o;
}

static World makeWorld() {
  World w; w.init(128, 128);
  w.info.cityName = "Recordville"; w.info.mayorName = "Mayor"; w.info.year = 1950; w.seaLevel = 40;
  for (int x = 0; x <= 128; x++) for (int z = 0; z <= 128; z++) { w.alt[w.vIdx(x, z)] = uint8_t(30 + (x * 3 + z) % 50); w.water[w.vIdx(x, z)] = 40; }
  w.zone[w.tIdx(100, 3)] = 2;
  Occupant o; o.x = 10; o.z = 20; o.y = 33; o.sizeX = 3; o.sizeZ = 3; o.occId = 3000; o.keyType = 0x207EDC0E; o.agentType = 3300; w.occupants.push_back(o);
  w.view = {64, 64, 3, 1, true};
  return w;
}

int main() {
  // ---- header and marker helpers ----
  {
    Bytes b; writeRecHeader(b, 3); CHECK(b.size() == 8 && b[2] == 2 && rd32(b.data() + 4) == 0xDEADBEEF);
    RecHeader h = parseRecHeader(b.data(), b.size()); CHECK(!h.legacy && h.version == 3 && h.bodyOffset == 8 && !h.markers());
    Bytes m; writeRecHeader(m, 2, true); putMarker(m, true, "Start"); put32(m, 77);
    RecHeader hm = parseRecHeader(m.data(), m.size()); CHECK(hm.markers() && hm.version == 2);
    Reader r(m); r.seek(hm.bodyOffset); skipMarker(r, hm); CHECK(r.u32() == 77);
    Bytes legacy = {1, 2, 3, 4, 5, 6, 7, 8, 9}; CHECK(parseRecHeader(legacy.data(), legacy.size()).legacy);
    Bytes one = {0x01, 0x00, 0x00, 0xEF, 0xBE, 0xAD, 0xDE, 9};   // flags bit 1 clear: 7-byte header
    RecHeader h1 = parseRecHeader(one.data(), one.size()); CHECK(!h1.legacy && h1.bodyOffset == 7 && h1.version == 1);
    Bytes none; CHECK(parseRecHeader(none.data(), 0).legacy);
  }
  // ---- outer structs ----
  {
    CityInfoRec c; c.mapW = c.mapH = 192; c.cityName = "A"; c.mayorName = "B"; c.dayOfYear = 17; c.year = 1999; c.gameSpeed = 2; c.foundingYear = 1950;
    for (bool mk : {false, true}) { Bytes b = c.write(mk); CityInfoRec p = CityInfoRec::parse(b);
      CHECK(p.mapW == 192 && p.mapDepth == 192 && p.cityName == "A" && p.mayorName == "B" && p.dayOfYear == 17 && p.year == 1999 && p.foundingYear == 1950 && p.flagB == 1 && p.write(mk) == b); }
    Bytes four; put32(four, 5); StateWordsRec s = StateWordsRec::parse(four); CHECK(s.fieldsPresent == 1 && s.stateA == 5 && s.write().size() == 4);
    Bytes six; for (int i = 0; i < 6; i++) put32(six, uint32_t(i + 1)); StateWordsRec s6 = StateWordsRec::parse(six);
    CHECK(s6.fieldsPresent == 6 && s6.stateE == 6 && s6.write().size() == 24 && rd32(s6.write().data()) == 0);
    Bytes pv; for (int i = 0; i < 4; i++) put32(pv, 2); put32(pv, 1000); put32(pv, 5000); put32(pv, 0); put32(pv, 3); put32(pv, 1900); putString(pv, "M"); putString(pv, "C");
    put32(pv, 2); put32(pv, 99); put32(pv, 128); put32(pv, 128); put32(pv, 7);
    PreviewHeader ph = PreviewHeader::parse(pv, 3); CHECK(ph.population == 1000 && ph.cityName == "C" && ph.width == 128 && ph.pixelFormat == 7 && ph.imageOffset == pv.size());
    CheatIdsRec ch; ch.ids = {0x62E8630B, 5}; CHECK(CheatIdsRec::parse(ch.write()).ids == ch.ids);
  }

  // ---- round trip of a game-format file ----
  World w = makeWorld();
  Bytes file = saveCity(w);
  RawRecordStore st = RawRecordStore::parse(file);
  CHECK(st.serialize() == file);
  CHECK(st.findInner(keys::ZoneMgr) && st.findInner(keys::Terrain) && st.findOuter(keys::CityInfo) && !st.findOuter(keys::SegmentDir));
  CHECK(st.segmentKeys.size() == 1 && st.segmentKeys[0] == keys::Segment1);

  // ---- unknown records survive load / modify / save ----
  const ResKey unkOuter{0xDEAD0001, 0x00112233, 0x7};
  const ResKey unkInner{0xABCD1234, 0x4321DCBA, 0x9};
  Bytes unkOuterData = {1, 2, 3, 4, 5, 6, 7}, unkInnerData(333);
  for (size_t i = 0; i < unkInnerData.size(); i++) unkInnerData[i] = uint8_t(i * 7);
  Bytes withUnknown;
  {
    RawRecordStore s = RawRecordStore::parse(file);
    s.setOuter(unkOuter, unkOuterData); s.setInner(unkInner, unkInnerData);
    withUnknown = s.serialize();
    World lw = loadCity(withUnknown);                     // existing loader tolerates unknown records
    CHECK(lw.info.cityName == "Recordville" && lw.occupants.size() == 1);
    RawRecordStore r2 = RawRecordStore::parse(withUnknown);
    CHECK(r2.findOuter(unkOuter) && r2.findOuter(unkOuter)->data == unkOuterData && r2.findInner(unkInner) && r2.findInner(unkInner)->data == unkInnerData);
    CHECK(r2.serialize() == withUnknown);
  }
  {
    World lw = loadCity(withUnknown);
    lw.info.cityName = "Renamed"; lw.zone[lw.tIdx(3, 4)] = 5;
    Bytes fresh = saveCity(lw);
    RawRecordStore s = RawRecordStore::parse(withUnknown);
    s.overlayFrom(RawRecordStore::parse(fresh));
    Bytes out = s.serialize();
    World again = loadCity(out);
    CHECK(again.info.cityName == "Renamed" && again.zone[again.tIdx(3, 4)] == 5 && again.occupants.size() == 1);
    RawRecordStore r3 = RawRecordStore::parse(out);
    CHECK(r3.findOuter(unkOuter) && r3.findOuter(unkOuter)->data == unkOuterData);
    CHECK(r3.findInner(unkInner) && r3.findInner(unkInner)->data == unkInnerData);
    // overlayKeys only touches the named records
    RawRecordStore t = RawRecordStore::parse(withUnknown);
    t.overlayKeys(RawRecordStore::parse(fresh), {keys::ZoneMgr});
    CHECK(loadCity(t.serialize()).info.cityName == "Recordville" && loadCity(t.serialize()).zone[lw.tIdx(3, 4)] == 5);
    CHECK(t.removeInner(unkInner) && !t.findInner(unkInner) && t.removeOuter(unkOuter) && !t.findOuter(unkOuter));
    CHECK(RawRecordStore::parse(t.serialize()).findInner(keys::Terrain) != nullptr);
  }

  // ---- non-canonical container: literal RefPack, orphan directory entry, payload gap, unknown records ----
  {
    RawRecordStore s = RawRecordStore::parse(file);
    s.setOuter(unkOuter, unkOuterData); s.setInner(unkInner, unkInnerData);
    // hand-build inner db with a directory entry that points past the directory
    Bytes inner(8, 0); struct D { ResKey k; uint32_t off; }; std::vector<D> dir;
    for (auto& r : s.inner) { dir.push_back({r.key, uint32_t(inner.size())}); inner.insert(inner.end(), r.data.begin(), r.data.end()); }
    std::sort(dir.begin(), dir.end(), [](const D& a, const D& b) { if (a.k.group != b.k.group) return a.k.group < b.k.group; if (a.k.instance != b.k.instance) return a.k.instance < b.k.instance; return a.k.type < b.k.type; });
    uint32_t dirOff = uint32_t(inner.size());
    wr32(&inner[0], uint32_t(dir.size() + 1)); wr32(&inner[4], dirOff);
    for (auto& d : dir) { put32(inner, d.k.type); put32(inner, d.k.group); put32(inner, d.k.instance); put32(inner, d.off); }
    put32(inner, 0x11111111); put32(inner, 0x22222222); put32(inner, 0x33333333); put32(inner, dirOff + 5000);   // ignored by the loader
    // the orphan entry sorts last only by luck of the keys; order is irrelevant to parse
    Bytes comp = literalRefpack(inner);
    CHECK(refpackDecode(comp) == inner);
    Bytes seg; put32(seg, 0x67); putString(seg, "0.90"); put32(seg, uint32_t(comp.size() + 4)); put32(seg, uint32_t(inner.size())); put32(seg, uint32_t(comp.size() + 4));
    seg.insert(seg.end(), comp.begin(), comp.end());
    IxfWriter ix;
    for (auto& o : s.outer) ix.add(o.key, o.data);
    Bytes d0; put32(d0, 0x67); put32(d0, 1); put32(d0, 0x035F62A4); put32(d0, 0x035F62A4); put32(d0, 1);
    ix.add(keys::SegmentDir, d0); ix.add(keys::Segment1, seg);
    Bytes f = ix.build();
    // shrink the declared size of the 3rd outer record by 4 bytes (payload gap, like the SNR string records)
    size_t sizeField = 4 + 20 * 2 + 16; uint32_t old = rd32(&f[sizeField]); CHECK(old > 8); wr32(&f[sizeField], old - 4);
    RawRecordStore p = RawRecordStore::parse(f);
    CHECK(p.orphans.size() == 1 && p.findInner(unkInner) && p.outer[2].declaredSize == old - 4 && p.outer[2].data.size() == old);
    CHECK(p.serialize() == f);                         // byte exact, including the non-canonical stream
    p.setInner(unkInner, Bytes{9, 9});                 // modification forces a re-encode
    RawRecordStore q = RawRecordStore::parse(p.serialize());
    CHECK(q.findInner(unkInner)->data == Bytes({9, 9}) && q.orphans.size() == 1 && q.findOuter(unkOuter));
    CHECK(loadCity(q.serialize()).info.cityName == "Recordville");
  }
  // ---- bad input ----
  { Bytes junk(100, 1); bool threw = false; try { RawRecordStore::parse(junk); } catch (const FormatError&) { threw = true; } CHECK(threw);
    Bytes cut(file.begin(), file.begin() + 200); threw = false; try { RawRecordStore::parse(cut); } catch (const FormatError&) { threw = true; } CHECK(threw); }

  // ---- templates ----
  {
    TemplateSet t = TemplateSet::defaults(128, 128);
    const size_t N = 128 * 128;
    CHECK(t.inner(keys::ZoneMgr) && t.inner(keys::ZoneMgr)->size() == 3 * N + 354);
    { growth::ZoneManager zm; std::vector<uint8_t> g; CHECK(zm.parse(t.inner(keys::ZoneMgr)->data(), t.inner(keys::ZoneMgr)->size(), 128, 128, &g) && g.size() == N); }
    CHECK(t.inner(keys::zoneDeveloperClass(14)) && rd32(t.inner(keys::zoneDeveloperClass(14))->data()) == growth::kDevAirport);
    CHECK(t.inner(keys::ResLayer)->size() == 1994 && t.inner(keys::ComLayer)->size() == 94 && t.inner(keys::IndLayer)->size() == 94);
    CHECK(t.inner(keys::RciLayer)->size() == 8 + 2 * N + 24 && t.inner(keys::WorldLayer)->size() == 725);
    CHECK(t.inner(keys::LandValue)->size() == 4258);   // spec: 4 258 for 128 maps
    CHECK(t.inner(keys::Aura)->size() == (32 * 32) + 150 && t.inner(keys::PowerLayer)->size() == 668 && t.inner(keys::WaterLayer)->size() == 212);
    CHECK(t.inner(keys::Flammability)->size() == 25 + N && t.inner(keys::Spacing)->size() == 8 + 32 * 32);
    { growth::ValveLayer v; v.init(128, 128, growth::ValveTuning()); CHECK(v.parseResidential(t.inner(keys::ResLayer)->data(), 1994) && v.parseRci(t.inner(keys::RciLayer)->data(), t.inner(keys::RciLayer)->size())); }
    { growth::LandValueLayer lv; lv.init(128, 128, growth::LandValueTuning()); CHECK(lv.parse(t.inner(keys::LandValue)->data(), t.inner(keys::LandValue)->size())); }
    CHECK(t.outer(keys::FileKind) && rd32(t.outer(keys::FileKind)->data()) == 3 && t.outer(keys::StateWords)->size() == 24);

    RawRecordStore s = RawRecordStore::parse(file);
    size_t zoneBefore = s.findInner(keys::ZoneMgr)->data.size();
    size_t added = t.fillMissing(s);
    CHECK(added > 10 && s.findInner(keys::ZoneMgr)->data.size() == zoneBefore && s.findInner(keys::LandValue) && s.findOuter(keys::SchemeSel));
    Bytes out = s.serialize();
    CHECK(loadCity(out).info.cityName == "Recordville");
    CHECK(t.fillMissing(s) == 0);

    // captured data replaces the defaults
    TemplateSet captured = TemplateSet::fromStore(RawRecordStore::parse(withUnknown));
    CHECK(captured.inner(unkInner) && captured.outer(unkOuter));
    Bytes custom = {1, 2, 3}; captured.setInner(keys::LandValue, custom);
    TemplateSet merged = TemplateSet::defaults(128, 128); merged.mergeFrom(captured);
    CHECK(*merged.inner(keys::LandValue) == custom && merged.inner(keys::ResLayer) && merged.inner(unkInner));
  }
  TEST_MAIN_END();
}
