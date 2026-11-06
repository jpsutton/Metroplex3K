#include "core/landvalue.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "core/records.h"

namespace m3k {
namespace growth {

const int LandValueLayer::kPhaseOffsets[16][2] = {{4, 4}, {0, 6}, {2, 0}, {4, 6}, {6, 4}, {0, 0}, {2, 2}, {6, 2},
                                                  {2, 6}, {0, 2}, {6, 0}, {2, 4}, {4, 2}, {6, 6}, {4, 0}, {0, 4}};

int lvZoneClass(int z) {
  switch (z) {
    case 1: case 2: case 3: return kLvRes;
    case 5: case 6: case 7: case 14: case 22: return kLvCom;
    case 9: case 10: case 11: case 15: return kLvInd;
    default: return kLvNone;
  }
}

namespace {
std::vector<std::string> split(const std::string& s, char d) {
  std::vector<std::string> v; std::string cur;
  for (char c : s) { if (c == d) { v.push_back(trimSpaces(cur)); cur.clear(); } else cur.push_back(c); }
  v.push_back(trimSpaces(cur));
  return v;
}
int toInt(const std::string& s) { return int(std::strtol(s.c_str(), nullptr, 10)); }
int rne(double d) { return int(std::nearbyint(d)); }
}  // namespace

LvCentre LvCentre::parse(const std::string& text) {
  LvCentre c;
  auto toks = split(text, ' ');
  bool first = true;
  for (const auto& t : toks) {
    if (t.empty()) continue;
    if (first) {
      first = false;
      size_t plus = t.find('+');
      if (plus != std::string::npos && t.find(':') == std::string::npos) {
        c.edge = toInt(t.substr(0, plus)); c.radiusPct = toInt(t.substr(plus + 1));
        if (!t.empty() && t.back() == 'p') c.radiusPct = toInt(t.substr(plus + 1, t.size() - plus - 2));
        continue;
      }
    }
    size_t col = t.find(':');
    if (col == std::string::npos) continue;
    c.pts.push_back({toInt(t.substr(0, col)), toInt(t.substr(col + 1))});
  }
  return c;
}

LvFactor LandValueTuning::parseTriple(const std::string& s, bool low8) {
  auto f = split(s, ',');
  LvFactor r;
  int t = f.size() > 0 ? std::min(toInt(f[0]), 32767) : 0;
  r.t = low8 ? (t & 0xFF) : std::max(0, std::min(t, 255));
  r.b = f.size() > 1 ? float(toInt(f[1])) / 1000.0f : 0;
  r.c = f.size() > 2 ? float(toInt(f[2])) / 1000.0f : 0;
  return r;
}
LvAgentMod LandValueTuning::parseMod(const std::string& s) {
  LvAgentMod m;
  std::string v = s;
  size_t sp = v.find_first_of(" \t;"); if (sp != std::string::npos) v = v.substr(0, sp);
  for (const auto& item : split(v, ',')) {
    auto f = split(item, ':');
    if (f.size() < 2 || f[1].empty()) continue;
    int n = std::max(-127, std::min(127, toInt(f[0])));
    switch (f[1][0]) {
      case 'R': case 'r': m.r = int8_t(m.r + n); break;
      case 'C': case 'c': m.c = int8_t(m.c + n); break;
      case 'I': case 'i': m.i = int8_t(m.i + n); break;
      case 'A': case 'a': m.all = int8_t(m.all + n); break;
    }
  }
  return m;
}
void LandValueTuning::parse(const IniFile& ini) {
  const char* S = "TuningParameters";
  std::string s;
  // Defaults are the shipped SC3LandValue.ini values (a pack that omits a key); the city-centre curves have no default.
  updatePeriod = 60; radB = -1.0f;
  water = parseTriple("100,-9000,100", true); air = parseTriple("400,-7000,250", true);
  garbage = parseTriple("0,-400000,0", false); crime = parseTriple("25,-1900,200", false);
  waterEffect = 50; waterRadiusBlocks = 3; hillEffect = 30; homelessShelterPct = 5;
  if (ini.get(S, "UpdatePeriod", s)) updatePeriod = std::max(1, toInt(s));
  if (ini.get(S, "RadPolluteFactor", s)) { auto f = split(s, ','); radB = f.size() > 1 ? float(toInt(f[1])) / 1000.0f : 0; }
  if (ini.get(S, "WaterPolluteFactor", s)) water = parseTriple(s, true);
  if (ini.get(S, "AirPolluteFactor", s)) air = parseTriple(s, true);
  if (ini.get(S, "GarbagePolluteFactor", s)) garbage = parseTriple(s, false);
  if (ini.get(S, "CrimeFactor", s)) crime = parseTriple(s, false);
  if (ini.get(S, "WaterEffect", s)) waterEffect = toInt(s);
  if (ini.get(S, "WaterEffectRadius", s)) waterRadiusBlocks = std::min(toInt(s) / 2, 126);
  if (ini.get(S, "HillEffect", s)) hillEffect = toInt(s);
  if (ini.get(S, "HomelessShelterOrdEffect", s)) homelessShelterPct = toInt(s);
  if (ini.get(S, "ResCityCenterEffect", s)) res = LvCentre::parse(s);
  if (ini.get(S, "ComCityCenterEffect", s)) com = LvCentre::parse(s);
  if (ini.get(S, "IndCityCenterEffect", s)) ind = LvCentre::parse(s);
  for (const auto& kv : ini.section("AgentIDModifiers")) agentMods[parseIdNumber(kv.first)] = parseMod(kv.second);
}
void LandValueTuning::parseHierarchy(const IniFile& ini) {
  for (const auto& kv : ini.section("AgentTypeHierarchy")) hierarchy[parseIdNumber(kv.first)] = parseIdNumber(kv.second);
}
const LvAgentMod* LandValueTuning::modFor(uint32_t t) const {
  auto it = agentMods.find(t);
  if (it != agentMods.end()) return &it->second;
  // ancestor chain; first entry in ascending agent-type order whose type is an ancestor
  std::vector<uint32_t> anc;
  uint32_t cur = t;
  for (int guard = 0; guard < 64; guard++) {
    auto h = hierarchy.find(cur);
    if (h == hierarchy.end() || h->second == cur) break;
    cur = h->second; anc.push_back(cur);
  }
  for (const auto& kv : agentMods) if (std::find(anc.begin(), anc.end(), kv.first) != anc.end()) return &kv.second;
  return nullptr;
}

void BuildingCentroid::add(int x0, int z0, int sizeX, int sizeZ, int sign) {
  int64_t dx = sizeX - 1, dz = sizeZ - 1, a = (dx + 1) * (dz + 1);
  int64_t ax = (dx / 2 + x0) * a, az = (dz / 2 + z0) * a;
  if (dx % 2 == 0) ax += dx * (dz + 1) / 2;
  if (dz % 2 == 0) az += dz * (dx + 1) / 2;
  sumX += sign * ax; sumZ += sign * az; n += sign * a;
}
void BuildingCentroid::get(int mapW, int mapH, int& cx, int& cz) const {
  if (n <= 0) { cx = mapW / 2; cz = mapH / 2; return; }
  cx = int(sumX / n); cz = int(sumZ / n);
}

int hillReferenceOf(const LandValueCity& c) {
  int64_t tot = 0, n = 0;
  const int sea = c.seaLevel();
  for (int x = 0; x <= c.mapW(); x++)
    for (int z = 0; z <= c.mapH(); z++) {
      int a = c.altitude(x, z);
      if (a > c.waterSurface(x, z)) { tot += a - sea; n++; }
    }
  return n ? int((tot / n) & 0xFF) : 0;
}

// ---------------- layer ----------------
void LandValueLayer::init(int w, int h, const LandValueTuning& t) {
  W = w; H = h; tune_ = t;
  lv.assign(size_t(W / 2) * (H / 2), 0);
  phase = 0; avgLV = 0; scanX = 4; scanY = 4; count = 0; tail = 260; prevSum = sum = 0; history.fill(0);
  derivedValid_ = false;
}

void LandValueLayer::buildWaterDist(const LandValueCity& c) {
  int bw = W / 2, bh = H / 2;
  waterDist_.assign(size_t(bw) * bh, 255);
  for (int bx = 0; bx < bw; bx++) for (int bz = 0; bz < bh; bz++) {
    bool w = false;
    for (int dx = 0; dx < 2 && !w; dx++) for (int dz = 0; dz < 2; dz++) if (c.hasWater(bx * 2 + dx, bz * 2 + dz)) { w = true; break; }
    if (w) waterDist_[size_t(bx) * bh + bz] = 0;
  }
  // exact Chebyshev distance by two-pass chamfer with the 8-neighbourhood
  auto at = [&](int x, int z) -> uint8_t& { return waterDist_[size_t(x) * bh + z]; };
  auto relax = [&](int x, int z, int nx, int nz) {
    if (nx < 0 || nz < 0 || nx >= bw || nz >= bh) return;
    int d = at(nx, nz); if (d < 255 && d + 1 < at(x, z)) at(x, z) = uint8_t(d + 1);
  };
  for (int x = 0; x < bw; x++) for (int z = 0; z < bh; z++) { relax(x, z, x - 1, z); relax(x, z, x, z - 1); relax(x, z, x - 1, z - 1); relax(x, z, x - 1, z + 1); }
  for (int x = bw - 1; x >= 0; x--) for (int z = bh - 1; z >= 0; z--) { relax(x, z, x + 1, z); relax(x, z, x, z + 1); relax(x, z, x + 1, z + 1); relax(x, z, x + 1, z - 1); }
}

void LandValueLayer::buildModMaps(const LandValueCity& c) {
  int cw = W / 4, ch = H / 4;
  size_t n = size_t(cw) * ch;
  modAll_.assign(n, 0); modR_.assign(n, 0); modC_.assign(n, 0); modI_.assign(n, 0);
  std::vector<LvOccupant> occ; c.occupants(occ);
  for (const auto& o : occ) {
    if (!o.occupied) continue;
    const LvAgentMod* m = tune_.modFor(o.agentType);
    if (!m) continue;
    for (int x = o.x; x < o.x + o.sizeX; x++) for (int z = o.z; z < o.z + o.sizeZ; z++) {
      if (x < 0 || z < 0 || x / 4 >= cw || z / 4 >= ch) continue;
      size_t i = size_t(x / 4) * ch + z / 4;
      modAll_[i] = int16_t(modAll_[i] + m->all); modR_[i] = int16_t(modR_[i] + m->r);
      modC_[i] = int16_t(modC_[i] + m->c); modI_[i] = int16_t(modI_[i] + m->i);
    }
  }
  for (std::vector<int16_t>* g : {&modAll_, &modR_, &modC_, &modI_})
    for (int pass = 0; pass < 2; pass++)
      for (int x = 0; x < cw; x++) for (int z = 0; z < ch; z++) {  // in place, outer x inner z
        int16_t s = 0; int cnt = 0;
        for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
          int nx = x + dx, nz = z + dz;
          if (nx < 0 || nz < 0 || nx >= cw || nz >= ch) continue;
          s = int16_t(s + (*g)[size_t(nx) * ch + nz]); cnt++;
        }
        (*g)[size_t(x) * ch + z] = cnt ? int16_t(s / cnt) : 0;
      }
}

void LandValueLayer::cycleStart(const LandValueCity& c, bool rollStats) {
  int64_t pop = c.population();
  cr_ = curveValue(tune_.res.pts, pop); cc_ = curveValue(tune_.com.pts, pop); ci_ = curveValue(tune_.ind.pts, pop);
  buildModMaps(c);
  buildWaterDist(c);
  if (rollStats) {
    prevSum = sum;
    avgLV = count ? uint8_t(std::min<int64_t>(std::max<int64_t>(sum, 0) / int64_t(count), 255)) : 0;
    for (size_t i = history.size() - 1; i > 0; i--) history[i] = history[i - 1];   // most recent first [INFERRED]; saved Berlin history[0] = avgLV
    history[0] = avgLV;
    sum = 0; count = 0;
  }
  int h = c.hillReference();
  if (h < 0) h = hillReferenceOf(c);
  href_ = std::max(0, std::min(h, 255));
  derivedValid_ = true;
}

void LandValueLayer::computeBlock(const LandValueCity& c, int x, int y, int cx, int cy, std::string* dbg) {
  int bh = H / 2;
  int rw = tune_.waterRadiusBlocks;
  // 1 water bonus
  int dist = waterDist_.empty() ? 255 : waterDist_[size_t(x / 2) * bh + y / 2];
  int wb = (dist <= rw) ? (rw - dist + 1) * tune_.waterEffect / (rw + 1) : 0;
  // 2 hill bonus: 8-bit difference, so vertices below the sea level wrap to ~226-255 and get the full bonus (original
  // defect, spec 15 s6.3 step 2) [STATIC]
  int h = (c.altitude(x, y) - c.seaLevel()) & 0xFF;
  int hb = 0;
  if (h > href_) { int d8 = href_ == 0 ? 1 : 8 * href_; hb = tune_.hillEffect * std::min(h, d8) / d8; }
  float Wf = float(wb + hb) / 100.0f;
  // 4 block class
  int blockCls = kLvNone, zc[4];
  int k = 0;
  for (int dx = 0; dx < 2; dx++) for (int dz = 0; dz < 2; dz++) zc[k++] = lvZoneClass(c.zoneAt(x + dx, y + dz));
  bool anyC = false, anyR = false, anyI = false;
  for (int i = 0; i < 4; i++) { anyC |= zc[i] == kLvCom; anyR |= zc[i] == kLvRes; anyI |= zc[i] == kLvInd; }
  blockCls = anyC ? kLvCom : anyR ? kLvRes : anyI ? kLvInd : kLvNone;
  // 5 agent modifiers at the block origin
  int cw = W / 4, ch = H / 4;
  size_t mi = size_t(std::min(x / 4, cw - 1)) * ch + std::min(y / 4, ch - 1);
  auto cl = [](int v) { return std::max(-150, std::min(150, v)); };
  int mAll = cl(modAll_.empty() ? 0 : modAll_[mi]), mR = cl(modR_.empty() ? 0 : modR_[mi]),
      mC = cl(modC_.empty() ? 0 : modC_[mi]), mI = cl(modI_.empty() ? 0 : modI_[mi]);
  // homeless shelter
  float s = c.ordinanceEnacted(0x20D07175) ? float(tune_.homelessShelterPct) / 100.0f : 0.0f;
  int64_t totalT = 0;
  const LvCentre* cent[4] = {nullptr, &tune_.res, &tune_.com, &tune_.ind};
  int ceVal[4] = {0, cr_, cc_, ci_};
  int mods[4] = {mAll, mR, mC, mI};
  int avgRadius = (W + H) / 2;
  k = 0;
  for (int dx = 0; dx < 2; dx++) for (int dz = 0; dz < 2; dz++, k++) {
    int tx = x + dx, tz = y + dz;
    int cls = zc[k] != kLvNone ? zc[k] : blockCls;
    int ddx = std::abs(cx - tx), ddz = std::abs(cy - tz);
    int dc = std::max(ddx, ddz) + std::min(ddx, ddz) / 2;
    float base, m;
    if (cls == kLvNone) { base = 1; m = float(mods[0]) / 100.0f; }
    else {
      const LvCentre& e = *cent[cls];
      int rK = std::max(1, e.radiusPct * avgRadius / 100);
      if (dc > rK) base = float(e.edge);
      else base = float(ceVal[cls]) + float(rne(double(float(dc) * float(e.edge - ceVal[cls]) / float(rK))));
      base += 1;
      m = float(mods[cls]) / 100.0f;
    }
    float p = 0, q = 0;
    if (c.radioactive(tx, tz)) p += tune_.radB;
    float tA = float(tune_.air.t), tW = float(tune_.water.t), tG = float(tune_.garbage.t), tCr = float(tune_.crime.t);
    {
      float v = float(c.airPollution(tx, tz));
      if (v < tA) p += (tA - v) * tune_.air.c / tA;
      else if (v > tA) p += (v - tA) * tune_.air.b / (32767.0f - tA);
    }
    {
      float v = float(c.waterPollution(tx, tz));
      if (v < tW) p += (tW - v) * tune_.water.c / tW;
      else if (v > tA) p += (v - tW) * tune_.water.b / (32767.0f - tW);  // original defect: compares with the air threshold
    }
    {
      float v = float(c.garbage(tx, tz));
      if (v < tG) p += (tG - v) * tune_.garbage.c / tG;
      else if (v > tG) p += (v - tG) * tune_.garbage.b / (32767.0f - tG);
    }
    {
      float v = float(c.crime(tx, tz));
      if (v < tCr) q = (tCr - v) * tune_.crime.c / tCr;
      else if (v > tCr && tCr < 255.0f) q = (v - tCr) * tune_.crime.b / (255.0f - tCr);
    }
    float B = float(rne(double(base * (1.0f + Wf))));
    int T = rne(double((1.0f + q + p + m + s) * B));
    totalT += T;
    if (dbg) { char b[200]; std::snprintf(b, sizeof b, " [cls %d base %.0f W %.2f B %.0f q %.3f p %.3f m %.2f s %.2f T %d]", cls, base, Wf, B, q, p, m, s, T); *dbg += b; }
    if (dbg) continue;
    if (cls != kLvNone) { sum += T; count++; }
  }
  if (dbg) return;
  int64_t v = totalT / 4;
  lv[size_t(x / 2) * bh + y / 2] = uint8_t(v < 1 ? 1 : (v > 255 ? 255 : v));
}

std::string LandValueLayer::explainBlock(const LandValueCity& c, int x, int y) const {
  int bh = H / 2, cw = W / 4, ch = H / 4;
  int dist = waterDist_.empty() ? 255 : waterDist_[size_t(x / 2) * bh + y / 2];
  int h = (c.altitude(x, y) - c.seaLevel()) & 0xFF;
  size_t mi = size_t(std::min(x / 4, cw - 1)) * ch + std::min(y / 4, ch - 1);
  int cx, cy; c.cityCentre(cx, cy);
  int ddx = std::abs(cx - x), ddz = std::abs(cy - y);
  char buf[400];
  std::snprintf(buf, sizeof buf, "zone %d/%d/%d/%d waterDist %d h %d dc %d mods A%d R%d C%d I%d crime %d air %d wat %d garb %d cR %d cC %d",
                c.zoneAt(x, y), c.zoneAt(x + 1, y), c.zoneAt(x, y + 1), c.zoneAt(x + 1, y + 1), dist, h,
                std::max(ddx, ddz) + std::min(ddx, ddz) / 2, modAll_.empty() ? 0 : modAll_[mi], modR_.empty() ? 0 : modR_[mi],
                modC_.empty() ? 0 : modC_[mi], modI_.empty() ? 0 : modI_[mi], c.crime(x, y), c.airPollution(x, y), c.waterPollution(x, y),
                c.garbage(x, y), cr_, cc_);
  std::string out = buf;
  const_cast<LandValueLayer*>(this)->computeBlock(c, x, y, cx, cy, &out);
  return out;
}

void LandValueLayer::recomputeAll(const LandValueCity& c) {
  int32_t sSave = sum; uint32_t cSave = count;
  if (!derivedValid_) cycleStart(c, false);
  int cx, cy; c.cityCentre(cx, cy);
  for (int x = 0; x + 1 < W; x += 2) for (int y = 0; y + 1 < H; y += 2) computeBlock(c, x, y, cx, cy);
  sum = sSave; count = cSave;
}

void LandValueLayer::update(const LandValueCity& c) {
  if (!derivedValid_) cycleStart(c, false);
  int cx, cy; c.cityCentre(cx, cy);
  int blocks = int((int64_t(W) * H) / (int64_t(std::max(1, tune_.updatePeriod)) * 4));
  for (int i = 0; i < blocks; i++) {
    int x = int(scanX), y = int(scanY);
    if (x + 1 < W && y + 1 < H) computeBlock(c, x, y, cx, cy);
    const int ox = kPhaseOffsets[phase][0];
    scanX += 8;
    if (int(scanX) >= W) {
      scanX = uint32_t(ox); scanY += 8;
      if (int(scanY) >= H) {
        phase = uint8_t((phase + 1) & 15);
        scanX = uint32_t(kPhaseOffsets[phase][0]); scanY = uint32_t(kPhaseOffsets[phase][1]);
        if (phase == 0) cycleStart(c, true);
      }
    }
  }
}

bool LandValueLayer::parse(const uint8_t* p, size_t n) {
  try {
    if (W <= 0 || H <= 0) return false;
    RecHeader h = parseRecHeader(p, n);
    if (h.version > 1) return false;
    Reader r(p, n); r.seek(h.bodyOffset);
    int bw = W / 2, bh = H / 2;
    std::vector<uint8_t> g(size_t(bw) * bh);
    for (int row = 0; row < bw; row++) for (int z = 0; z < bh; z++) g[size_t(bw - 1 - row) * bh + z] = r.u8();
    uint8_t ph = r.u8(); uint32_t sx = r.u32(), sy = r.u32(); uint8_t avg = r.u8();
    int32_t ps = r.s32(), sm = r.s32(); uint32_t cn = r.u32();
    std::array<uint32_t, 32> hist; for (auto& v : hist) v = r.u32();
    uint32_t tl = r.u32();
    lv = g; phase = ph > 15 ? 0 : ph; scanX = sx; scanY = sy; avgLV = avg; prevSum = ps; sum = sm; count = cn; history = hist; tail = tl;
    if (int(scanX) >= W || int(scanY) >= H) { scanX = uint32_t(kPhaseOffsets[phase][0]); scanY = uint32_t(kPhaseOffsets[phase][1]); }
    derivedValid_ = false;
    return true;
  } catch (const FormatError&) { return false; }
}

Bytes LandValueLayer::write() const {
  Bytes d; writeRecHeader(d, 1);
  int bw = W / 2, bh = H / 2;
  for (int row = 0; row < bw; row++) for (int z = 0; z < bh; z++) d.push_back(lv.size() == size_t(bw) * bh ? lv[size_t(bw - 1 - row) * bh + z] : 0);
  d.push_back(phase); put32(d, scanX); put32(d, scanY); d.push_back(avgLV);
  put32(d, uint32_t(prevSum)); put32(d, uint32_t(sum)); put32(d, count);
  for (uint32_t v : history) put32(d, v);
  put32(d, tail);
  return d;
}

}  // namespace growth
}  // namespace m3k
