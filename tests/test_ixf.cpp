#include "core/ixf.h"
#include "core/resdb.h"
#include "core/ini.h"
#include "testutil.h"
using namespace m3k;

int main() {
  IxfWriter w;
  w.add({0x2026960B, 0x1111, 1}, {'h', 'i'});
  w.add({0x2026960B, 0x1111, 2}, {'y', 'o', '!'});
  Bytes img = w.build();
  CHECK(img.size() == 0x5004 + 5);
  IxfFile f; CHECK(f.openMemory(img));
  Bytes b;
  CHECK(f.getBytes({0x2026960B, 0x1111, 2}, b) && b.size() == 3 && b[0] == 'y');
  CHECK(!f.has({0x2026960B, 0x1111, 3}));
  // On-disk order is Group, Instance, Type.
  CHECK(rd32(&img[4]) == 0x1111 && rd32(&img[8]) == 1 && rd32(&img[12]) == 0x2026960B);

  // Missing terminator => open fails.
  Bytes trunc(img.begin(), img.begin() + 24);
  IxfFile g; CHECK(!g.openMemory(trunc));
  // Free slot is skipped; duplicate keeps first.
  Bytes fr = img;
  for (int i = 0; i < 20; i++) fr[4 + i] = 0xFF;
  IxfFile h; CHECK(h.openMemory(fr)); CHECK(!h.has({0x2026960B, 0x1111, 1})); CHECK(h.has({0x2026960B, 0x1111, 2}));

  // Later mount wins.
  ResourceDb db;
  db.mountMemory(img, "a");
  IxfWriter w2; w2.add({0x2026960B, 0x1111, 1}, {'N', 'E', 'W'});
  db.mountMemory(w2.build(), "set");
  auto r = db.find({0x2026960B, 0x1111, 1});
  CHECK(r && r.data[0] == 'N');
  db.unmountTag("set");
  r = db.find({0x2026960B, 0x1111, 1});
  CHECK(r && r.data[0] == 'h');

  // INI quirks.
  auto ini = IniFile::fromPackedLines({"[A]", "k1 = v1 ; c", "K1=dup", "bad", "k3=v3", "[B]", "x=1"});
  auto s = ini.section("A");
  CHECK(s.size() == 2 && s[0].second == "v1 ; c");  // stops at line without '='
  std::string v; CHECK(ini.get("B", "X", v) && v == "1");
  CHECK(parseIdNumber("0x21fd8e4") == 0x021FD8E4);
  CHECK(parseIdNumber("1234") == 1234);
  CHECK(parseIdNumber("A1B") == 0xA1B);
  TEST_MAIN_END();
}
