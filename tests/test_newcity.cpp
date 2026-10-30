// New-city state against the records the original writes right after New City (spec/30-new-city-template.md).
#include "core/city.h"
#include "core/newcity_template.h"
#include "core/records.h"
#include "core/sim.h"
#include "testutil.h"
using namespace m3k;

namespace {

World makeWorld(int W) {
  World w;
  w.init(W, W);
  for (auto& a : w.alt) a = 100;
  for (auto& a : w.water) a = 50;
  w.info.year = 2000; w.info.foundingYear = 2000; w.info.dayOfYear = 1;
  w.info.cityName = "New"; w.info.mayorName = "Mayor";
  w.info.fileKind = 3;
  return w;
}

const Bytes* inner(const World& w, const ResKey& k) {
  const InnerRecord* r = w.raw ? w.raw->findInner(k) : nullptr;
  return r ? &r->data : nullptr;
}

// Payload (after the 8-byte header) is zero except at the listed record offsets.
bool zeroExcept(const Bytes& b, std::initializer_list<std::pair<size_t, uint8_t>> allowed) {
  for (size_t i = 8; i < b.size(); i++) {
    uint8_t want = 0;
    for (auto& a : allowed) if (a.first == i) want = a.second;
    if (b[i] != want) { std::fprintf(stderr, "  byte %zu of %zu: %d, want %d\n", i, b.size(), b[i], want); return false; }
  }
  return true;
}

void testConstantRecords() {
  const auto& recs = newCityConstantRecords();
  CHECK(recs.size() == 29 + 12);
  // A few sizes from the spec 30 s4 table.
  CHECK(newCityConstantRecord({0x206C6E7C, 0xC106C4F5, 0})->data.size() == 1994);
  CHECK(newCityConstantRecord({0x206C6E7C, 0xE0AFDF68, 0})->data.size() == 668);
  CHECK(newCityConstantRecord({0x013DEE82, 0x028D0FC5, 0xB})->data.size() == 108);
  CHECK(newCityConstantRecord({0xE1F6ABE2, 0xC4C90997, 0})->data.size() == 46);
  // Utilities-advisor flags: six booleans, all 1.
  const Bytes& flags = newCityConstantRecord({0x022E288E, 0x422E28E8, 0})->data;
  CHECK(flags.size() == 14 && rd32(flags.data() + 4) == 0xDEADBEEF);
  for (int i = 8; i < 14; i++) CHECK(flags[size_t(i)] == 1);
  CHECK(rd32(newCityConstantRecord(keys::zoneDeveloperClass(14))->data.data()) == 0xC1F81E7E);

  RawRecordStore s;
  CHECK(seedNewCityRecords(s, 64, 64) == recs.size() + 2);
  CHECK(seedNewCityRecords(s, 64, 64) == 0);   // existing records are kept
}

// Records the simulation models must serialise to the observed bytes even without the template (fresh defaults).
void testFreshDefaults() {
  CityConfig cfg; cfg.applySyntheticDefaults();
  World w = makeWorld(64);
  Sim s; s.init(&w, nullptr, &cfg, 7);
  s.city().flush(w);
  for (ResKey k : {ResKey{0x206C6E7C, 0x00ABF2EC, 0}, ResKey{0x206C6E7C, 0xA0F42214, 0}, ResKey{0x206C6E7C, 0xE0AFDF68, 0},
                   ResKey{0x206C6E7C, 0x02BF0033, 0}}) {
    const Bytes* got = inner(w, k);
    CHECK(got && *got == newCityConstantRecord(k)->data);
  }
}

void testSeededCity(int W, size_t flam, size_t rci, size_t spacing, size_t traffic, size_t lv, size_t aura) {
  CityConfig cfg; cfg.applySyntheticDefaults();
  World w = makeWorld(W);
  w.raw = std::make_shared<RawRecordStore>();
  seedNewCityRecords(*w.raw, W, W);
  Sim s; s.init(&w, nullptr, &cfg, 7);
  s.city().flush(w);
  // s4: every constant record survives a load + flush unchanged.
  int changed = 0;
  for (const auto& r : newCityConstantRecords()) {
    const Bytes* got = inner(w, r.key);
    if (!got || *got != r.data) { changed++; std::fprintf(stderr, "  %08X:%08X:%X changed\n", r.key.type, r.key.group, r.key.instance); }
  }
  CHECK(changed == 0);
  // s5: layer sizes and contents.
  const Bytes* f = inner(w, keys::Flammability);
  CHECK(f && f->size() == flam && zeroExcept(*f, {}));
  const Bytes* r = inner(w, keys::RciLayer);
  CHECK(r && r->size() == rci && zeroExcept(*r, {}));
  const Bytes* sp = inner(w, keys::Spacing);
  CHECK(sp && sp->size() == spacing && zeroExcept(*sp, {}));
  const Bytes* t = inner(w, {0x206C6E7C, 0x029CA804, 0});
  size_t tOff = size_t(W) * W * 13 / 4 + 16;
  CHECK(t && t->size() == traffic && zeroExcept(*t, {{tOff, 2}, {tOff + 133, 0xDC}}));
  const Bytes* l = inner(w, keys::LandValue);
  CHECK(l && l->size() == lv && zeroExcept(*l, {{lv - 153, 4}, {lv - 149, 4}, {lv - 4, 4}, {lv - 3, 1}}));
  const Bytes* a = inner(w, keys::Aura);
  CHECK(a && a->size() == aura);
  if (a) {
    // Spec 30 s5 lists 4, 8 and thirty-two 0x7F; the chart scale 260 (04 01) at the end is spec 10 s6.9.
    std::vector<std::pair<size_t, uint8_t>> want = {{aura - 149, 4}, {aura - 145, 8}, {aura - 4, 4}, {aura - 3, 1}};
    for (size_t i = 0; i < 32; i++) want.push_back({aura - 132 + 4 * i, 0x7F});
    bool ok = true;
    for (size_t i = 0; i < a->size(); i++) {
      uint8_t e = 0;
      for (auto& p : want) if (p.first == i) e = p.second;
      if ((*a)[i] != e) ok = false;
    }
    CHECK(ok);
  }

  // A save keeps the records the engine does not model (advisor topics, petitions, disaster table).
  RawRecordStore saved = RawRecordStore::parse(saveCity(w));
  for (const auto& rec : newCityConstantRecords()) {
    const InnerRecord* got = saved.findInner(rec.key);
    CHECK(got && got->data == rec.data);
  }
}

}  // namespace

int main() {
  testConstantRecords();
  testFreshDefaults();
  testSeededCity(64, 4121, 8224, 264, 25773, 1186, 406);
  testSeededCity(128, 16409, 32800, 1032, 102573, 4258, 1174);
  testSeededCity(192, 36889, 73760, 2312, 230573, 9378, 2454);
  testSeededCity(256, 65561, 131104, 4104, 409773, 16546, 4246);
  TEST_MAIN_END();
}
