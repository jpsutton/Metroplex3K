// Game: query popup data (spec 09 6.12), news ticker strip (09 4.4), snapshots (09 6.13).
#include <cmath>
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

// Rating 0..4 (spec 20 5.2: 4 = best) to the letter band of the panel (A..F = 5 bands, A first) [INFERRED mapping]; returns the
// byte that falls in the middle of band `idx` (panel bands are band * 5 / 256).
uint8_t gradeBand(int idx) { idx = std::max(0, std::min(4, idx)); return uint8_t((idx * 256 + 128) / 5); }

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

// Occupants listed in SC3CityScheme.ini [ReplaceableBuildings] are the ones the Replace button is shown for (spec 09 6.12.1, spec 24 2.4).
bool Game::isReplaceableOccupant(uint32_t occId) {
  if (!replaceableLoaded_) {
    replaceableLoaded_ = true;
    IniFile ini;
    if (haveData_ && data_.ini().load("SC3CityScheme.ini", ini))
      for (const auto& kv : ini.section("ReplaceableBuildings")) {
        unsigned long long a, b, c;
        if (std::sscanf(kv.first.c_str(), "%llu , %llu , %llu", &a, &b, &c) == 3) replaceableOcc_.insert(uint32_t(c));
      }
  }
  return replaceableOcc_.count(occId) != 0;
}

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
  // Spec 16 §4.1 / 17 §7.2.2 / Q-BT [STATIC]: one query line = trunc(G × s); G = standing garbage.
  if (!c.pollution.standing.empty()) q.garbageScaled = c.pollution.queryGarbageScaled(x, z);
  q.powered = c.powered(x, z);
  q.watered = c.watered(x, z);
  q.underground = ropt_.underground;

  const auto& par = c.config().parent;
  // Piece name from the occupant name table (spec 09 6.12.1: "Road", "Highway", "Railroad", "Power Line", ...).
  auto pieceName = [&](uint32_t pieceId, const char* fallback) {
    std::string n = haveData_ ? str(kTblOccName, pieceId) : std::string();
    return n.empty() ? std::string(fallback) : n;
  };
  // Underground view: the target is the pipe or subway piece on the tile (spec 09 6.12.1); a tile with nothing underground
  // gives the surface tile's bare-ground query. Surface buildings are hidden in that view, so they are not queried [INFERRED].
  if (ropt_.underground) {
    for (const auto& p : w.pipes) if (p.x == x && p.z == z) { q.cls = QueryClass::Pipe; q.name = pieceName(p.pieceId, "Water Pipe"); return; }
    for (const auto& p : w.subway) if (p.x == x && p.z == z) {
      q.cls = QueryClass::Subway; q.name = pieceName(p.pieceId, "Subway");
      q.band[0] = uint8_t(c.trafficDensityAt(x, z));
      return;
    }
    return;
  }
  const int idx = c.occupantAt(x, z);
  const Occupant* o = (idx >= 0 && size_t(idx) < w.occupants.size()) ? &w.occupants[size_t(idx)] : nullptr;
  q.showPower = o != nullptr || c.isPowerLine(x, z);
  if (!o) {
    const NetPiece* named = nullptr;
    if (c.isRoadTile(x, z)) q.cls = QueryClass::Road;
    for (const auto& p : w.surface) {
      if (p.x != x || p.z != z) continue;
      uint32_t k = c.kindOf(p);
      if (k == uint32_t(NetworkKind::Rail)) { q.cls = QueryClass::Rail; named = &p; }
      else if (k == uint32_t(NetworkKind::Power)) { if (!c.isRoadTile(x, z)) { q.cls = QueryClass::PowerLine; named = &p; } }
      else if (!named) named = &p;
    }
    if (q.cls == QueryClass::None) for (const auto& p : w.subway) if (p.x == x && p.z == z) q.cls = QueryClass::Subway;
    if (q.cls == QueryClass::None && c.isPowerLine(x, z)) q.cls = QueryClass::PowerLine;
    if (named) {
      const char* fb = q.cls == QueryClass::Rail ? "Railroad" : q.cls == QueryClass::PowerLine ? "Power Line" : "Road";
      q.name = pieceName(named->pieceId, fb);
    }
    if (q.cls == QueryClass::Road || q.cls == QueryClass::Rail || q.cls == QueryClass::Subway)
      q.band[0] = uint8_t(c.trafficDensityAt(x, z));   // usage band of the panel from the live traffic load
    // Neighbour connection: a network piece on the map edge names the neighbour city of that side (spec 09 6.12.2).
    if (named && (q.cls == QueryClass::Road || q.cls == QueryClass::Rail || q.cls == QueryClass::PowerLine)) {
      const int side = NeighbourDeals::edgeNeighbour(x, z, w.W, w.H);
      if (side >= 0 && c.neighbours.neighbours[side].exists) q.neighbour = c.neighbours.neighbours[side].cityName;
    }
    // Empty zoned tile (not landfill): the "Reason for Lack of Development" row. The stored per-tile reason code is not kept by
    // the growth simulation yet [OPEN], so the code-0 default shows.
    if (q.cls == QueryClass::None && q.zoneCode != 0 && q.zoneCode != 17) q.reasonRow = true;
    if (q.cls == QueryClass::None && q.zoneCode == 17) {   // landfill zone tile: capacity and usage of the landfill store
      q.cls = QueryClass::LandfillZone; q.name = "Landfill";
      q.landfillCapacity = int(std::min<int64_t>(c.pollution.disp.store.D, 2000000000));
      q.landfillUsage = c.pollution.disp.store.capPct;   // [INFERRED] percent full
    }
    if (q.cls == QueryClass::None && ti < w.flora.size() && w.flora[ti]) {   // a tree
      uint32_t inst = floraSpriteInstance(w.flora[ti], x, z);
      if (inst) {
        q.cls = QueryClass::Flora;
        q.name = haveData_ ? str(kTblOccName, inst) : std::string();
        if (q.name.empty()) q.name = "Tree";
        if (haveData_) if (const OccAttr* oa = data_.occupant(inst)) q.picture = oa->appearance;
      }
    }
    return;
  }

  // Occupant
  std::string nm = haveData_ ? str(kTblOccName, o->occId) : std::string();
  if (nm.empty()) nm = o->name;
  if (nm.empty()) nm = "Building " + std::to_string(o->agentType);
  q.name = nm;
  const uint32_t ostate = o->state & ~0x200u;   // bit 0x200 = protected (Make Historical) [INFERRED, spec 13 3]
  q.historical = (o->state & 0x200u) != 0;
  switch (ostate) {
    case 2: q.status |= QsAbandoned; break;
    case 4: q.status |= QsConstruction; break;
    case 8: case 16: q.status |= QsDestroyed; break;
    default: break;
  }
  q.footprint = std::max<int>(o->sizeX, o->sizeZ);
  if (haveData_) {
    if (const OccAttr* oa = data_.occupant(o->occId, false, o->keyType == T_OCC_ATTR_PORT)) q.picture = oa->appearance;
    q.canReplace = isReplaceableOccupant(o->occId);
  }
  bool plant = false;
  q.cls = classify(par, o->agentType, plant);
  if (q.cls == QueryClass::WaterBuilding || q.cls == QueryClass::WaterTreatment) q.canReplace = false;   // utilities have no Replace (spec 09 6.12.2)
  if (q.status & QsDestroyed) q.cleanupCost = c.budget.specialCost(12);
  q.reasonRow = q.cls == QueryClass::Zoned && ostate == 2;   // abandoned: "Reason for Abandonment" (stored code not kept yet [OPEN])
  q.growable = q.cls == QueryClass::Zoned && (ostate == 1 || ostate == 2);   // Make Historical: abandoned zoned buildings too
  if (q.cls == QueryClass::Zoned && ostate == 2) q.canReplace = false;           // but no Replace on an abandoned one
  q.showPower = true;
  // Business-deal reachability (spec 18 §10) [STATIC]: MegaMall/Casino/Toxic/Prison fire an uncounted
  // "reach anything" trip when their query window is built (powered/watered/state-1 is the rest of the
  // functional test, owned by the deal manager).
  if (c.traffic && o->agentType >= 19001 && o->agentType <= 19004)
    c.traffic->routeDealTrip(TripRect{o->x, o->z, o->x + o->sizeX - 1, o->z + o->sizeZ - 1});

  switch (q.cls) {
    case QueryClass::PowerPlant:
      for (const auto& p : c.power.plants()) {
        if (p.x != o->x || p.z != o->z) continue;
        q.v[0] = p.capacity; q.v[1] = p.output; q.v[4] = p.load;
        q.band[0] = clamp8(p.output > 0 ? p.load * 255 / p.output : 0);
        q.ageYears = std::max(0, (absMonth(c.clock) - p.buildAbsMonth) / 12);
        q.maxAgeYears = p.lmax > 0 ? p.lmax / 12 : 0;   // maximum life in whole years ("Age (Max)")
        {   // Pollution Generated (spec 09 6.12): p = air * round(1.0466667 rA^2) + water * round(1.0466667 rW^2); baseline 22000
          const us::PollutionEntry e = c.pollution.entryFor(o->agentType);
          q.pollution = int(std::min<int64_t>(int64_t(e.air) * std::lround(1.0466667 * e.rA * e.rA) + int64_t(e.water) * std::lround(1.0466667 * e.rW * e.rW), 2000000000));
          q.pollutionBaseline = 22000;
        }
        if (p.expired) q.status |= QsDefunct;
        else if (p.lmax > 0 && absMonth(c.clock) - p.buildAbsMonth > p.lmax - 12) q.status |= QsAging;
        break;
      }
      break;
    case QueryClass::WaterBuilding:
      for (const auto& f : c.water.facilities()) {
        if (f.x != o->x || f.z != o->z) continue;
        q.v[0] = q.v[1] = f.output;
        // This facility's share of the water used [INFERRED]: citywide use split by capacity.
        q.v[4] = c.water.stats.supply > 0 ? double(c.water.stats.used) * f.output / double(c.water.stats.supply) : 0;
        q.band[0] = clamp8(c.water.stats.supply > 0 ? int(c.water.stats.used * 255 / c.water.stats.supply) : 0);
        q.band[2] = q.waterPollution >= 0 ? uint8_t(std::min(255, q.waterPollution)) : 0;   // "Effect of Water Pollution"
        q.v[2] = f.status == us::WaterFacility::Good || f.status == us::WaterFacility::Far;
        q.ageYears = std::max(0, (absMonth(c.clock) - f.buildAbsMonth) / 12);
        q.maxAgeYears = f.lmax > 0 ? f.lmax / 12 : 0;
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
    case QueryClass::Hospital:   // systemwide figures of the population model (spec 20 5, 17 5)
      q.v[0] = c.pop.hospitalBeds; q.v[1] = c.pop.patients; q.v[2] = c.pop.doctors;
      q.band[0] = gradeBand(4 - c.pop.healthRating);
      break;
    case QueryClass::School:
      q.v[0] = c.pop.schoolDesks; q.v[1] = c.pop.schoolAgePop; q.v[2] = c.pop.teachers;
      q.band[0] = gradeBand(4 - c.pop.schoolRating);
      break;
    case QueryClass::College:
      q.v[0] = c.pop.collegeDesks; q.v[1] = c.pop.collegeAgePop; q.v[2] = c.pop.professors;
      q.band[0] = gradeBand(4 - c.pop.collegeRating);
      break;
    case QueryClass::Landmark:
      // Date Built: the occupant's own build date when it saves one, else the city's founding year [INFERRED].
      q.builtYear = (o->hasDate && o->dateYear > 0) ? o->dateYear : w.info.foundingYear;
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

// Sound classification of a tile (spec 06 s10.4). Classification of the tile content is [INFERRED] from the
// sound names; zoned families follow the agent-type table of spec 13 s4.
TileSoundContext Game::tileSoundContext(int x, int z) {
  TileSoundContext sc;
  if (!world_.W || !world_.inTiles(x, z)) return sc;
  const City& c = sim_.city();
  const size_t ti = world_.tIdx(x, z);
  if (ropt_.underground) {
    for (const auto& p : world_.subway) if (p.x == x && p.z == z) { sc.view = SoundView::Subway; sc.subwayPiece = true; return sc; }
    for (const auto& p : world_.pipes) if (p.x == x && p.z == z) { sc.view = SoundView::Pipes; sc.pipePiece = true; return sc; }
    sc.view = SoundView::Subway;
    return sc;
  }
  // desert / jungle landscapes (sounds 252 / 253) are not identifiable from the scheme id [OPEN]
  const int idx = c.occupantAt(x, z);
  if (idx >= 0 && size_t(idx) < world_.occupants.size()) {
    const Occupant& o = world_.occupants[size_t(idx)];
    if (haveData_) if (const OccAttr* a = data_.occupant(o.occId)) sc.sparseId = a->sparseId;
    bool plant = false;
    QueryClass qc = classify(c.config().parent, o.agentType, plant);
    const uint32_t t = o.agentType;
    if (o.state == 2) sc.kind = TileKind::Abandoned;
    else if (o.state == 4) sc.kind = TileKind::Construction;
    else if (qc == QueryClass::Zoned) sc.kind = t < 5000 ? TileKind::Residential : t < 8000 ? TileKind::Commercial : t < 9300 ? TileKind::Industrial : TileKind::Agricultural;
    else if (qc == QueryClass::Landmark) sc.kind = TileKind::Landmark;
    else if (qc == QueryClass::Airport) sc.kind = TileKind::Airport;
    else if (qc == QueryClass::Seaport) sc.kind = TileKind::Seaport;
    else if (qc == QueryClass::Garbage || qc == QueryClass::Recycling) sc.kind = TileKind::Landfill;
    else if (qc == QueryClass::WaterBuilding || qc == QueryClass::WaterTreatment) sc.kind = TileKind::WaterFacility;
    return sc;
  }
  if (c.isRoadTile(x, z)) { sc.kind = TileKind::Road; return sc; }
  for (const auto& p : world_.surface) {
    if (p.x != x || p.z != z) continue;
    uint32_t k = c.kindOf(p);
    if (k == uint32_t(NetworkKind::Rail)) { sc.kind = TileKind::Rail; return sc; }
    if (k == uint32_t(NetworkKind::Power)) { sc.kind = TileKind::PowerLine; return sc; }
  }
  if (c.isPowerLine(x, z)) { sc.kind = TileKind::PowerLine; return sc; }
  if (ti < world_.flora.size() && world_.flora[ti]) { sc.kind = TileKind::Tree; return sc; }
  int lo = std::min(std::min(world_.altAt(x, z), world_.altAt(x + 1, z)), std::min(world_.altAt(x, z + 1), world_.altAt(x + 1, z + 1)));
  if (world_.waterAt(x, z) > lo) sc.kind = TileKind::Water;
  return sc;
}

// Ambient area from what is on screen (spec 06 s5.3). Which weights the original uses is [OPEN]; this samples
// a grid of visible tiles and lets the dominant area win. Zoom rules are [INFERRED]: close views follow the tile
// type, audio zoom 3 uses the generic zoom-3 beds, far views the wind bed. Switching is held back by
// FreshnessSelectionAreaMinMsec and suppressed while scrolling faster than FreshnessMaxXSpeed.
void Game::updateAmbience(uint32_t dtMs) {
  if (!audio_ || !world_.W) return;
  ambAcc_ += dtMs;
  if (ambAcc_ < 250) return;
  if (simBusy()) return;   // samples live city state; ambAcc_ keeps counting, so it runs on the next idle frame
  const int64_t dt = ambAcc_;
  ambAcc_ = 0;
  const auto& opt = audio_->ini().options;
  if (!ambCfg_) { ambSel_.configure(opt.get("FreshnessSelectionAreaMinMsec", 0), float(opt.get("FreshnessMaxXSpeed", 1 << 20))); ambCfg_ = true; }
  const float pxPerTile = 4.f * float(1 << cam_.zoom);
  const float speed = (std::fabs(cam_.u - ambLastU_) + std::fabs(cam_.v - ambLastV_)) * pxPerTile * 1000.f / float(dt);
  ambLastU_ = cam_.u; ambLastV_ = cam_.v;
  enum { Res, Com, Ind, Agri, Nature, Desert, Jungle, Water, Wind, Arctic, Traffic, Constr, Seaport, Airport, Aband, Garbage, Crime, Unpwr, Pipes, Subway, Zm3City, Zm3 };
  std::vector<float> w(22, 0.f);
  const int az = cam_.zoom + 1;
  if (ropt_.underground) {
    w[world_.subway.empty() ? Pipes : Subway] = 1.f;
  } else if (az <= 2) {
    w[Wind] = 1.f;
  } else {
    int built = 0, total = 0;
    const City& c = sim_.city();
    for (int gy = 0; gy < 8; gy++)
      for (int gx = 0; gx < 12; gx++) {
        int tx, tz;
        if (!renderer_.pickTile(cam_, (2 * gx + 1) * W_ / 24, (2 * gy + 1) * H_ / 16, tx, tz)) continue;
        total++;
        TileSoundContext sc = tileSoundContext(tx, tz);
        if (az == 3) { if (sc.kind != TileKind::None && sc.kind != TileKind::Water && sc.kind != TileKind::Tree) built++; continue; }
        switch (sc.kind) {
          case TileKind::Residential: w[Res] += 1; break;
          case TileKind::Commercial: w[Com] += 1; break;
          case TileKind::Industrial: w[Ind] += 1; break;
          case TileKind::Agricultural: w[Agri] += 1; break;
          case TileKind::Water: w[Water] += 1; break;
          case TileKind::Road: w[Traffic] += 0.5f; break;
          case TileKind::Construction: w[Constr] += 1; break;
          case TileKind::Abandoned: w[Aband] += 1; break;
          case TileKind::Seaport: w[Seaport] += 1; break;
          case TileKind::Airport: w[Airport] += 1; break;
          case TileKind::Landfill: w[Garbage] += 1; break;
          case TileKind::None: case TileKind::Tree: w[Nature] += 1; break;
          default: break;
        }
        const int oi = sc.kind == TileKind::Road || sc.kind == TileKind::None || sc.kind == TileKind::Tree ? -1 : c.occupantAt(tx, tz);
        if (oi >= 0 && !c.powered(tx, tz)) w[Unpwr] += 1;
        if (oi >= 0 && !c.crime.crime.empty() && c.crime.crimeAt(tx, tz) > 200) w[Crime] += 1;
      }
    if (az == 3) w[total > 0 && built * 4 >= total ? Zm3City : Zm3] = 1.f;
  }
  const int before = ambSel_.current();
  int sel = ambSel_.update(dt, w, speed);
  if (sel != before || audio_->ambientArea() != sel) audio_->setAmbientArea(sel);
}

void Game::stopAmbience() {
  if (audio_ && audio_->ambientArea() >= 0) audio_->setAmbientArea(-1);
  ambSel_.reset(-1);
}

// ================================================================================== ticker (spec 09 4.4)
void Game::drawTicker(Rect r) {
  const HudSnap& hs = hudNow();
  int32_t today = tickerStamp(hs.month, hs.day, hs.clockYear);
  // Pause fillers use the same ticker-data colour as the real 0x2F9 news item.
  TickerStyle pauseStyle = tickerDb_.style(kTblNewsTriggered, 0x2F9);
  ticker_.update(frameDt_, r.w, ui_, &news_, today, &rng_, paused_, pauseStyle.r, pauseStyle.g, pauseStyle.b);
  // Paused indicator flashes 750 ms on / 750 ms off, starting lit.
  pauseFlashMs_ = paused_ ? (pauseFlashMs_ + frameDt_) % 1500 : 0;
  WinEnv E = env();
  WindowResult R = ticker_.draw(ui_, E, r);
  for (const auto& e : R.events) {
    if (e.act == Act::PlaySound) sound(int(e.a));
    else if (e.act == Act::TickerClick) { centreOn(int(e.a), int(e.b)); uiHit_ = true; }
  }
}

uint32_t Game::postNews(uint32_t table, uint32_t id) {
  WinEnv E = env();
  std::string text = E.text(table, id, "");   // expands %CITY% / %MAYOR% style tokens
  if (text.empty()) return 0;
  cal::Date d = sim_.city().clock.date();
  return news_.post(table, id, tickerStamp(d.month, d.day, d.year), false, 0, 0, &text);
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
