#include "core/bat.h"
#include <cstdio>
#include "testutil.h"
using namespace m3k;

static BldFile makeBld(int lot, uint32_t id) {
  BldFile b;
  BldModel& m = b.model;
  m.N = uint8_t(8 * lot); m.lotSize = uint8_t(lot); m.H = 28; m.orientation = 0;
  m.columnHead.assign(size_t(m.N) * m.N, 0);
  m.cells.assign(1, BldCell());
  m.palette = {{"", 0xDEAD}, {"color 200,40,40", 1}, {"brick 06.tga", 2}};
  for (int r = 0; r < m.N; r++)
    for (int c = 0; c < m.N; c++) {
      BldCell floor;  // z 0 cube
      floor.packed = 0; floor.faceFlags = 1;
      floor.face[0][0] = 2;
      m.cells.push_back(floor);
      uint32_t fi = uint32_t(m.cells.size() - 1);
      m.columnHead[size_t(r) * m.N + size_t(c)] = fi;
      if (r >= 2 && r < 5 && c >= 2 && c < 5) {
        BldCell up;
        up.packed = (1u << 17) | (0u << 26) | (uint32_t(r & 3) << 30);
        for (int s = 0; s < 6; s++) up.face[s][0] = 1;
        m.cells.push_back(up);
        uint32_t ui = uint32_t(m.cells.size() - 1);
        m.cells[fi].packed = (m.cells[fi].packed & ~0x1FFFFu) | ui;
      }
    }
  b.lotSize = uint8_t(lot);
  b.name = "Test House"; b.author = "Me"; b.description = "desc"; b.category = 0x209;
  b.buildingId = id;
  b.hasThumb = true; b.thumb.assign(200 * 200, 0xF81F); b.thumb[5] = 0x1234;
  BldTexture t;
  t.textureId = 2; t.name = "brick 06.tga"; t.category = 6; t.sourceKind = 5; t.width = 16; t.height = 16;
  t.invWidth = t.invHeight = 1.0f / 16; t.pxW = t.pxH = 16; t.bgra.assign(16 * 16 * 4, 0x55);
  b.textures.push_back(t);
  return b;
}

int main() {
  BldFile b = makeBld(1, 0x45FEEAC6);
  std::string err;
  CHECK(b.model.validate(&err));
  Bytes file = b.serialize();
  // layout: preallocated 1024-entry index -> data at 20484
  CHECK(rd32(&file[0]) == kIxfMagic);
  BldFile p = BldFile::parse(file);
  CHECK(p.name == "Test House" && p.author == "Me" && p.description == "desc" && p.category == 0x209);
  CHECK(p.buildingId == 0x45FEEAC6 && p.model.N == 8 && p.model.cells.size() == b.model.cells.size());
  CHECK(p.model.columnHead == b.model.columnHead && p.model.palette[1].name == "color 200,40,40");
  CHECK(p.model.cells[5].packed == b.model.cells[5].packed && p.model.validate());
  CHECK(p.hasThumb && p.thumb[5] == 0x1234 && p.thumb[0] == 0xF81F);
  CHECK(p.textures.size() == 1 && p.textures[0].name == "brick 06.tga" && p.textures[0].bgra.size() == 1024);
  CHECK(p.revision[0] == 2 && p.languageCode == 1);
  Bytes again = p.serialize();
  CHECK(again == file);
  // model record keys
  {
    IxfFile f; CHECK(f.openMemory(file));
    CHECK(f.has(kBldModelKey) && f.has(kBldInfoKey) && f.has(kBldDescKey) && f.has(kBldCategoryKey) && f.has(kBldLangKey) &&
          f.has(kBldRevisionKey) && f.has(kBldThumbKey));
  }
  // bad inputs
  {
    bool threw = false;
    try { BldFile::parse(Bytes{1, 2, 3}); } catch (const FormatError&) { threw = true; }
    CHECK(threw);
    BldFile v7 = b;
    IxfFile f; f.openMemory(file);
    Bytes mod; f.getBytes(kBldModelKey, mod);
    mod[0] = 7;
    IxfWriter w; w.add(kBldModelKey, mod);
    threw = false;
    try { BldFile::parse(w.build()); } catch (const FormatError& e) { threw = std::string(e.what()).find("version") != std::string::npos; }
    CHECK(threw);
  }
  // rotation
  {
    BldModel m = b.model;
    uint32_t h = m.head(2, 3);
    m.rotate(true);
    CHECK(m.head(7 - 3, 2) == h);
    CHECK(m.cells[h].rot() == ((b.model.cells[h].rot() + 1) & 3));
    m.rotate(false);
    CHECK(m.columnHead == b.model.columnHead);
    for (size_t i = 1; i < m.cells.size(); i++) CHECK(m.cells[i].rot() == b.model.cells[i].rot());
    m.orientation = 1;
    m.normalizeOrientation();
    CHECK(m.orientation == 0 && m.head(3, 7 - 2) == h);
    BldModel m3 = b.model; m3.orientation = 3; m3.normalizeOrientation();
    CHECK(m3.head(7 - 3, 2) == h);
    BldModel m2 = b.model; m2.orientation = 2; m2.normalizeOrientation();
    CHECK(m2.orientation == 2 && m2.columnHead == b.model.columnHead);
    // floor insertion
    BldModel e = b.model; e.columnHead[0] = 0; e.ensureFloor();
    CHECK(e.head(0, 0) != 0 && e.cells[e.head(0, 0)].z() == 0);
    // invalid z order
    BldModel bad = b.model; bad.cells[bad.head(3, 3)].packed = (bad.cells[bad.head(3, 3)].packed & 0x1FFFF) | (5u << 17);
    CHECK(!bad.validate());
  }
  // rendering + sprite encoding
  {
    Image full = renderBldView(b.model, 0, 1);
    CHECK(full.w == 128 && full.h > 64);
    Image z3 = renderBldView(b.model, 0, 2);
    CHECK(z3.w == 64 && z3.h * 2 >= full.h && z3.h * 2 < full.h + 2);
    size_t opaque = 0;
    for (uint32_t px : z3.px) if (px >> 24) opaque++;
    CHECK(opaque > 500);
    // bottom row near centre should be opaque (lot diamond bottom corner)
    CHECK((z3.px[size_t(z3.h - 1) * z3.w + 32] >> 24) != 0 || (z3.px[size_t(z3.h - 2) * z3.w + 32] >> 24) != 0);
    Bytes sp = encodeSprite565(z3);
    DecodedSprite d = decodeSprite(sp.data(), sp.size());
    CHECK(!d.isPlane && d.image.w == z3.w && d.image.h == z3.h);
    size_t same = 0;
    for (size_t i = 0; i < z3.px.size(); i++) {
      bool a = (z3.px[i] >> 24) != 0, c = (d.image.px[i] >> 24) != 0;
      if (a == c) same++;
    }
    CHECK(same == z3.px.size());
    // different views render different silhouettes for an asymmetric model
    Image v1 = renderBldView(b.model, 1, 2);
    CHECK(v1.w == 64);
    Image empty = renderBldView(BldModel(), 0, 2);
    CHECK(empty.empty());
  }
  // BAT.DAT import
  {
    BatDatabase db;
    std::string e2;
    uint32_t id = db.import(b, BatLights(), &e2);
    CHECK(id == 0x45FEEAC6 && db.has(id));
    BldFile b2 = makeBld(2, 0x1111);
    CHECK(db.import(b2) == 0x1111);
    BldFile bad = makeBld(1, 5); bad.model.lotSize = 9;
    CHECK(db.import(bad, BatLights(), &e2) == 0 && !e2.empty());
    auto l = db.list();
    CHECK(l.size() == 2 && l[0].buildingId == 0x1111 && l[0].lotSize == 2 && l[1].name == "Test House");
    BatDirEntry de;
    CHECK(db.find(id, de) && de.appearance == (ResKey{0x6300, kBatGroup, id}) && de.description == "desc");
    auto ids = db.spriteIds(id);
    CHECK(ids.size() == 16);
    for (size_t i = 1; i < ids.size(); i++) CHECK(ids[i] == ids[0] + i);
    CHECK(db.record({0, kBatGroup, ids[0]}) && db.record({1, kBatGroup, ids[15]}));
    // second building uses different sprite IDs
    CHECK(db.spriteIds(0x1111)[0] != ids[0]);
    const Bytes* cs = db.record({0x6300, kBatGroup, id});
    CsAttrib ca = parseCsAttrib(cs->data(), cs->size());
    CHECK(ca.valid && ca.clsid == 0x7300 && ca.zoomCount == 5 && ca.rotCount == 4 && ca.zoomSetCount == 4 &&
          ca.animSetCount == 20 && ca.frames.size() == 20);
    CHECK(ca.frames[16].flags == 2 && ca.frames[16].spriteId == ca.frames[12].spriteId && ca.frames[19].spriteId == ca.frames[15].spriteId);
    CHECK(ca.frame(0, 3, 1)->spriteId == ids[13]);
    // span header: width 64*lot at zoom 3
    const Bytes* span = db.record({1, kBatGroup, ids[12]});
    CHECK(span && rd16(&(*span)[4]) == 64 && rd16(&(*span)[6]) == 16 && rd16(&(*span)[0]) == 0);
    // persistence
    Bytes dat = db.build();
    BatDatabase db2;
    CHECK(db2.load(dat) && db2.list().size() == 2 && db2.build() == dat);
    // re-import keeps ID, replaces sprites
    CHECK(db2.import(b) == id && db2.list().size() == 2);
    CHECK(db2.remove(0x1111) && !db2.has(0x1111) && !db2.record({0, kBatGroup, db.spriteIds(0x1111)[0]}));
    CHECK(!db2.remove(0x1111));
    BatImportResult r = batImport(db2, {b2}, nullptr);
    CHECK(r.imported.size() == 1);
    std::set<uint32_t> keep{0x1111};
    batImport(db2, {}, &keep);
    CHECK(db2.list().size() == 1 && db2.has(0x1111));
    BldFile noid = makeBld(1, 0);
    CHECK(noid.effectiveId() != 0 && noid.effectiveId() == makeBld(1, 0).effectiveId());
  }
  // user building set, replaceable, occupant attributes
  {
    ResKey occ{0x207EDC0E, 0x57E, 209};
    UserBuildingSet s;
    CHECK(s.serialize() == Bytes(16, 0));
    s.set(occ, {0x6300, kBatGroup, 0x1111});
    s.set(ResKey{0x207EDC0E, 0x57E, 5}, {0x6300, kBatGroup, 0x2222});
    s.set(occ, {0x6300, kBatGroup, 0x45FEEAC6});
    CHECK(s.pairs.size() == 2 && s.find(occ)->instance == 0x45FEEAC6);
    Bytes ser = s.serialize();
    CHECK(ser.size() == 16 + 48 && rd32(&ser[12]) == 2 && rd32(&ser[16 + 8]) == 5);  // sorted ascending
    UserBuildingSet t;
    CHECK(UserBuildingSet::parse(ser, t) && t.pairs.size() == 2);
    CHECK(!UserBuildingSet::parse(Bytes(ser.begin(), ser.end() - 1), t));
    CHECK(UserBuildingSet::parse(ser, t));
    CHECK(t.revert(occ) && !t.revert(occ) && t.pairs.size() == 1);
    ReplaceableSet rs;
    rs.load(IniFile::fromText("[ReplaceableBuildings]\n545184782,1406,209=Residential\n545184782,1406,5=Foo\n"));
    CHECK(rs.size() == 2 && rs.contains(occ) && !rs.contains(ResKey{1, 2, 3}));
    BatDatabase db;
    db.import(b);
    BatDirEntry de; db.find(0x45FEEAC6, de);
    OccAttr base = genericOccAttr(0x1234, 1, 1);
    base.appearance = {0x6300, 0x6400, 77};
    base.designName = "orig";
    CHECK(canReplace(rs, occ, base, de));
    OccAttr big = genericOccAttr(1, 2, 2);
    CHECK(!canReplace(rs, occ, big, de));
    OccAttr eff = effectiveOccAttr(base, occ, s, &db);
    CHECK(eff.appearance.group == kBatGroup && eff.agentType == 0x1234 && eff.designName == "orig");
    BatDatabase empty;
    CHECK(effectiveOccAttr(base, occ, s, &empty).appearance == base.appearance);
    CHECK(effectiveOccAttr(base, ResKey{9, 9, 9}, s, &db).appearance == base.appearance);
    // city record
    IxfWriter w; w.add({1, 2, 3}, Bytes{9});
    Bytes city = w.build();
    UserBuildingSet none;
    CHECK(!readUserBuildingSet(city, none));
    Bytes c2 = writeUserBuildingSet(city, s);
    UserBuildingSet back;
    CHECK(readUserBuildingSet(c2, back) && back.pairs.size() == 2);
    UserBuildingSet one; one.set(occ, {0x6300, kBatGroup, 1});
    Bytes c3 = writeUserBuildingSet(c2, one);
    UserBuildingSet back2;
    CHECK(readUserBuildingSet(c3, back2) && back2.pairs.size() == 1);
    IxfFile f; f.openMemory(c3);
    CHECK(f.has({1, 2, 3}));
  }
  // lights
  {
    BatLights l = BatLights::fromIni(IniFile::fromText("[Lights]\nPreviewNorth=190,174,164\nPreviewTop=141, 141, 141\n"));
    CHECK(l.north[0] == 190 && l.north[2] == 164 && l.top[1] == 141 && l.west[0] == 98);
  }
  // install import with temp dir
  {
    fs::path root = fs::temp_directory_path() / "m3k_bat_test";
    fs::remove_all(root);
    fs::create_directories(root / "Apps" / "Res" / "BA");
    fs::create_directories(root / "Buildings");
    Bytes f1 = makeBld(1, 0xABCD).serialize();
    FILE* fp = std::fopen((root / "Buildings" / "A.bld").string().c_str(), "wb");
    std::fwrite(f1.data(), 1, f1.size(), fp); std::fclose(fp);
    BatImportResult res; std::string er;
    CHECK(batImportInstall(root, {"a.BLD", "missing.bld"}, res, er));
    CHECK(res.imported.size() == 1);
    Bytes datb;
    CHECK(readFile(root / "Apps" / "BARender" / "BAT.DAT", datb));
    BatDatabase d; CHECK(d.load(datb) && d.has(0xABCD));
    BatImportResult res2;
    CHECK(batImportInstall(root, {}, res2, er));
    CHECK(fs::exists(root / "Apps" / "Res" / "BA" / "RenderedBuildingsBackup.IXF"));
    fs::remove_all(root);
  }
  TEST_MAIN_END();
}
