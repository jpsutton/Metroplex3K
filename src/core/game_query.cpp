// Game: query popup data (spec 09 6.12), news ticker strip (09 4.4), snapshots (09 6.13).
#include <algorithm>
#include <cstdio>
#include <fstream>
#include "core/game.h"
#include "core/news.h"

namespace m3k {

namespace {

constexpr uint32_t kTblOccName = 0xA1096A4F;   // display-name strings (spec 13): group A1096A4F, instance = occupant id

bool isa(const std::map<uint32_t, uint32_t>& parent, uint32_t t, uint32_t anc) {
  for (int guard = 0; guard < 32; guard++) {
    if (t == anc) return true;
    auto it = parent.find(t);
    if (it == parent.end() || it->second == t) return false;
    t = it->second;
  }
  return false;
}

uint8_t clamp8(int v) { return uint8_t(std::max(0, std::min(255, v))); }

// Occupant family from the agent type (spec 13 4; hierarchy from CitySim.ini when loaded).
QueryClass classify(const std::map<uint32_t, uint32_t>& par, uint32_t t, bool& powerPlant) {
  powerPlant = us::powerCategory(t) >= 0;
  if (powerPlant) return QueryClass::PowerPlant;
  auto is = [&](uint32_t a) { return isa(par, t, a); };
  uint32_t k = t / 1000;
  if (k >= 3 && k <= 9) return QueryClass::Zoned;
  if (is(18000) || (t >= 18000 && t <= 18045) || t == 19999) return QueryClass::Landmark;
  if (is(13130)) return QueryClass::Police;
  if (is(13881)) return QueryClass::Jail;
  if (is(13002)) return QueryClass::Fire;
  if (is(13310)) return QueryClass::Hospital;
  if (is(13010)) return QueryClass::School;
  if (is(13873)) return QueryClass::College;
  if (is(14130)) return QueryClass::Library;
  if (is(14140)) return QueryClass::Museum;
  if (is(14450)) return QueryClass::Zoo;
  if (is(14430)) return QueryClass::Marina;
  if (is(14600)) return QueryClass::Ballpark;
  if (is(14300) || (t >= 14300 && t < 14400)) return QueryClass::Park;
  if (is(12310) || is(12350)) return QueryClass::Airport;
  if (is(12330)) return QueryClass::Seaport;
  if (t == 12810 || t == 12840 || t == 12830) return QueryClass::WaterBuilding;
  if (t == 12820) return QueryClass::WaterTreatment;
  if (t == 12852 || t == 12853 || t == 12850) return QueryClass::Garbage;
  if (t == 12851) return QueryClass::Recycling;
  if (t == 12210 || t == 12230 || t == 12360 || t == 12361) return QueryClass::Station;
  return QueryClass::None;
}

int absMonth(const Clock& c) { cal::Date d = c.date(); return 12 * d.year + d.month; }

// Minimal uncompressed PNG (same layout as tools/png.h; pixel byte 0 = R). Returns false on I/O failure.
bool writePngFile(const fs::path& path, int w, int h, const std::vector<uint32_t>& px) {
  static uint32_t table[256]; static bool init = false;
  if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } init = true; }
  auto crc = [&](const uint8_t* d, size_t n) { uint32_t c = 0xFFFFFFFFu; for (size_t i = 0; i < n; i++) c = table[(c ^ d[i]) & 255] ^ (c >> 8); return c ^ 0xFFFFFFFFu; };
  std::vector<uint8_t> raw;
  raw.reserve(size_t(h) * (size_t(w) * 3 + 1));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    for (int x = 0; x < w; x++) { uint32_t p = px[size_t(y) * w + x]; raw.push_back(p & 255); raw.push_back((p >> 8) & 255); raw.push_back((p >> 16) & 255); }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  size_t pos = 0;
  do {
    size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.push_back(uint8_t(n & 255)); z.push_back(uint8_t(n >> 8)); z.push_back(uint8_t(~n & 255)); z.push_back(uint8_t((~n >> 8) & 255));
    z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
    pos += n;
  } while (pos < raw.size());
  uint32_t ad = (b << 16) | a;
  for (int i = 3; i >= 0; i--) z.push_back(uint8_t((ad >> (i * 8)) & 255));
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  auto be = [&](uint32_t v) { char t[4] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v)}; f.write(t, 4); };
  auto chunk = [&](const char* type, const std::vector<uint8_t>& d) {
    be(uint32_t(d.size()));
    std::vector<uint8_t> td(type, type + 4); td.insert(td.end(), d.begin(), d.end());
    f.write(reinterpret_cast<const char*>(td.data()), long(td.size()));
    be(crc(td.data(), td.size()));
  };
  const char sig[8] = {char(137), 80, 78, 71, 13, 10, 26, 10}; f.write(sig, 8);
  std::vector<uint8_t> ihdr;
  for (int i = 3; i >= 0; i--) ihdr.push_back(uint8_t((uint32_t(w) >> (i * 8)) & 255));
  for (int i = 3; i >= 0; i--) ihdr.push_back(uint8_t((uint32_t(h) >> (i * 8)) & 255));
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk("IHDR", ihdr); chunk("IDAT", z); chunk("IEND", {});
  return bool(f);
}

}  // namespace

// ================================================================================== query (spec 09 6.12)
void Game::buildQueryInfo(int x, int z, QueryInfo& q) {
  q = QueryInfo();
  q.x = x; q.z = z;
  if (!world_.W || !world_.inTiles(x, z)) return;
  City& c = sim_.city();
  const World& w = world_;
  const size_t ti = w.tIdx(x, z);

  // Terrain: tile elevation = mean of the four corner altitudes; water depth from the surface vertex.
  int a00 = w.altAt(x, z), a10 = w.altAt(x + 1, z), a01 = w.altAt(x, z + 1), a11 = w.altAt(x + 1, z + 1);
  q.elevation = (a00 + a10 + a01 + a11) / 4;
  int lo = std::min(std::min(a00, a10), std::min(a01, a11));
  int surf = w.waterAt(x, z);
  if (surf > lo) {
    q.waterDepth = std::max(1, surf - lo);
    q.saltWater = surf <= w.seaLevel;   // raised lakes / rivers above sea level are fresh [INFERRED]
  }
  q.zoneCode = ti < w.zone.size() ? w.zone[ti] : 0;
  q.landValue = c.landValue.lv.empty() ? -1 : c.landValue.landValueAtTile(x, z);
  if (!c.crime.crime.empty()) q.crime = c.crime.crimeAt(x, z);
  if (ti < c.flammability.grid.size()) q.flammability = c.flammability.grid[ti];
  q.airPollution = clamp8(c.pollution.airAt(x, z) / 16);       // 15-bit state scaled to the 6-band byte [INFERRED]
  q.waterPollution = clamp8(c.pollution.waterAt(x, z) / 16);
  q.powered = c.powered(x, z);

  const auto& par = c.config().parent;
  const int idx = c.occupantAt(x, z);
  const Occupant* o = (idx >= 0 && size_t(idx) < w.occupants.size()) ? &w.occupants[size_t(idx)] : nullptr;
  q.showPower = o != nullptr || c.isPowerLine(x, z);
  if (!o) {
    if (c.isRoadTile(x, z)) q.cls = QueryClass::Road;
    for (const auto& p : w.surface) {
      if (p.x != x || p.z != z) continue;
      uint32_t k = c.kindOf(p);
      if (k == uint32_t(NetworkKind::Rail)) q.cls = QueryClass::Rail;
      else if (k == uint32_t(NetworkKind::Power)) q.cls = c.isRoadTile(x, z) ? q.cls : QueryClass::PowerLine;
    }
    if (q.cls == QueryClass::None) for (const auto& p : w.subway) if (p.x == x && p.z == z) q.cls = QueryClass::Subway;
    if (q.cls == QueryClass::None && c.isPowerLine(x, z)) q.cls = QueryClass::PowerLine;
    if (q.cls == QueryClass::Road || q.cls == QueryClass::Rail || q.cls == QueryClass::Subway)
      q.band[0] = uint8_t(c.trafficDensityAt(x, z));   // usage band of the panel from the live traffic load
    return;
  }

  // Occupant
  std::string nm = haveData_ ? str(kTblOccName, o->occId) : std::string();
  if (nm.empty()) nm = o->name;
  if (nm.empty()) nm = "Building " + std::to_string(o->agentType);
  q.name = nm;
  switch (o->state) {
    case 2: q.status |= QsAbandoned; break;
    case 4: q.status |= QsConstruction; break;
    case 8: case 16: q.status |= QsDestroyed; break;
    default: break;
  }
  bool plant = false;
  q.cls = classify(par, o->agentType, plant);
  if (q.status & QsDestroyed) q.cleanupCost = c.budget.specialCost(12);
  q.growable = q.cls == QueryClass::Zoned && o->state == 1;
  q.showPower = true;

  switch (q.cls) {
    case QueryClass::PowerPlant:
      for (const auto& p : c.power.plants()) {
        if (p.x != o->x || p.z != o->z) continue;
        q.v[0] = p.capacity; q.v[1] = p.output;
        q.band[0] = clamp8(p.output > 0 ? p.load * 255 / p.output : 0);
        q.v[2] = std::max(0, (absMonth(c.clock) - p.buildAbsMonth) / 12);
        if (p.expired) q.status |= QsDefunct;
        else if (p.lmax > 0 && absMonth(c.clock) - p.buildAbsMonth > p.lmax - 12) q.status |= QsAging;
        break;
      }
      break;
    case QueryClass::WaterBuilding:
      for (const auto& f : c.water.facilities()) {
        if (f.x != o->x || f.z != o->z) continue;
        q.v[0] = q.v[1] = f.output;
        q.band[0] = clamp8(c.water.stats.supply > 0 ? int(c.water.stats.used * 255 / c.water.stats.supply) : 0);
        q.v[2] = f.status == us::WaterFacility::Good || f.status == us::WaterFacility::Far;
        q.v[3] = std::max(0, (absMonth(c.clock) - f.buildAbsMonth) / 12);
        if (f.expired) q.status |= QsDefunct;
        break;
      }
      break;
    case QueryClass::WaterTreatment:
      q.v[0] = double(c.water.stats.supply);
      break;
    case QueryClass::Police: case QueryClass::Fire:
      q.band[0] = clamp8((q.cls == QueryClass::Police ? c.police : c.fire).coverageAt(o->x, o->z));
      break;
    case QueryClass::Jail:
      for (const auto& j : c.police.jails()) { q.v[0] += j.inmates; q.v[1] += j.capacity; }
      if (q.v[1] > 0) q.band[0] = clamp8(int(255 - q.v[0] * 255 / q.v[1]));
      break;
    case QueryClass::Landmark:
      q.builtYear = w.info.foundingYear;   // build date is not stored per occupant [OPEN]
      q.historical = true;
      break;
    case QueryClass::Station:
      q.band[0] = 128;
      break;
    default: break;
  }

  // Monthly funding cost of funded civic buildings: department charge shared over its buildings.
  uint32_t dept = 0;
  switch (q.cls) {
    case QueryClass::Police: case QueryClass::Jail: dept = dept::Police; break;
    case QueryClass::Fire: dept = dept::Fire; break;
    case QueryClass::Hospital: dept = dept::Health; break;
    case QueryClass::School: case QueryClass::College: case QueryClass::Library: case QueryClass::Museum: dept = dept::Education; break;
    case QueryClass::Station: dept = dept::Transit; break;
    default: break;
  }
  if (dept) {
    if (const Department* d = c.budget.dept(dept)) {
      int n = 0;
      for (const auto& oc : w.occupants) {
        bool pl = false;
        QueryClass k = classify(par, oc.agentType, pl);
        bool same = k == q.cls || ((dept == dept::Police) && (k == QueryClass::Police || k == QueryClass::Jail)) ||
                    ((dept == dept::Education) && (k == QueryClass::School || k == QueryClass::College || k == QueryClass::Library || k == QueryClass::Museum));
        if (same && oc.state == 1) n++;
      }
      q.fundingCost = d->charge() / std::max(1, n);
    }
  }
}

// ================================================================================== ticker (spec 09 4.4)
void Game::drawTicker(Rect r) {
  cal::Date d = sim_.city().clock.date();
  int32_t today = tickerStamp(d.month, d.day, d.year);
  ticker_.update(frameDt_, r.w, ui_, &news_, today, &rng_, paused_);
  // Paused indicator flashes 750 ms on / 750 ms off, starting lit.
  pauseFlashMs_ = paused_ ? (pauseFlashMs_ + frameDt_) % 1500 : 0;
  WinEnv E = env();
  WindowResult R = ticker_.draw(ui_, E, r);
  for (const auto& e : R.events) {
    if (e.act == Act::PlaySound) sound(int(e.a));
    else if (e.act == Act::TickerClick) { centreOn(int(e.a), int(e.b)); uiHit_ = true; }
  }
}

void Game::postNews(uint32_t table, uint32_t id) {
  WinEnv E = env();
  std::string text = E.text(table, id, "");   // expands %CITY% / %MAYOR% style tokens
  if (text.empty()) return;
  cal::Date d = sim_.city().clock.date();
  news_.post(table, id, tickerStamp(d.month, d.day, d.year), false, 0, 0, &text);
}

// ================================================================================== snapshots (spec 09 6.13)
void Game::takeSnapshot(int size) {
  if (!world_.W) { message("No city to photograph."); return; }
  size = std::max(1, std::min(3, size));
  static const int dims[3][2] = {{320, 240}, {640, 480}, {1024, 768}};
  int sw = dims[size - 1][0], sh = dims[size - 1][1];
  // Render the current view (same camera / options, no overlay UI) at the snapshot resolution.
  renderer_.resize(sw, sh);
  renderer_.render(cam_, ropt_, nullptr);
  std::vector<uint32_t> px = renderer_.pixels();
  if (W_ > 0 && H_ > 0) renderer_.resize(W_, H_);
  if (px.size() != size_t(sw) * sh) { message("Snapshot failed."); return; }
  for (auto& p : px) p |= 0xFF000000u;

  fs::path dir = (root_.empty() ? fs::current_path() : root_) / "Snapshots";
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::string base;
  for (char ch : sim_.city().world() ? world_.info.cityName : std::string("City"))
    base += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') ? ch : '_';
  if (base.empty()) base = "City";
  fs::path file;
  std::string name;
  for (int n = 1; n < 100000; n++) {
    char buf[32]; std::snprintf(buf, sizeof buf, "_%03d.png", n);
    name = base + buf;
    file = dir / name;
    if (!fs::exists(file, ec)) break;
  }
  if (!writePngFile(file, sw, sh, px)) { message("Could not write snapshot: " + file.string()); return; }

  // Companion info file (spec 6.13): city, mayor, date, population, funds.
  cal::Date d = sim_.city().clock.date();
  std::ofstream info(dir / (name.substr(0, name.size() - 4) + ".txt"));
  if (info) {
    info << "City: " << world_.info.cityName << "\nMayor: " << world_.info.mayorName << "\nDate: "
         << d.month << "/" << d.day << "/" << d.year << "\nPopulation: " << sim_.population()
         << "\nFunds: " << sim_.funds << "\n";
  }
  snapshotNames_.push_back(name);
  if (onSnapshot) onSnapshot(name, sw, sh, px);
  message("Snapshot saved: " + name);
}

}  // namespace m3k
