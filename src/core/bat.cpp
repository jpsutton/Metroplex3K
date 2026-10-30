#include "core/bat.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "core/refpack.h"

namespace m3k {

namespace {
std::string rdStr32(Reader& r) {
  uint32_t n = r.u32();
  if (n > r.remaining()) throw FormatError("string overrun");
  return r.str(n);
}
void wrStr32(Bytes& b, const std::string& s) {
  put32(b, uint32_t(s.size()));
  b.insert(b.end(), s.begin(), s.end());
}
void putKey(Bytes& b, const ResKey& k) { put32(b, k.type); put32(b, k.group); put32(b, k.instance); }
ResKey rdKey(Reader& r) { ResKey k; k.type = r.u32(); k.group = r.u32(); k.instance = r.u32(); return k; }
}  // namespace

// ---- model ----------------------------------------------------------------------------------
std::vector<uint32_t> BldModel::column(int r, int c) const {
  std::vector<uint32_t> out;
  uint32_t i = head(r, c);
  while (i != 0) {
    if (i >= cells.size() || out.size() > cells.size()) throw FormatError("bad column link");
    out.push_back(i);
    i = cells[i].next();
  }
  return out;
}

void BldModel::rotate(bool forward) {
  std::vector<uint32_t> nh(columnHead.size(), 0);
  for (int x = 0; x < N; x++)
    for (int y = 0; y < N; y++) {
      uint32_t h = head(x, y);
      int nx = forward ? N - 1 - y : y;
      int ny = forward ? x : N - 1 - x;
      nh[size_t(nx) * N + size_t(ny)] = h;
      for (uint32_t i = h; i != 0 && i < cells.size();) {
        cells[i].setRot((cells[i].rot() + (forward ? 1 : 3)) & 3);
        i = cells[i].next();
        if (i == h) break;
      }
    }
  columnHead = std::move(nh);
}

void BldModel::normalizeOrientation() {
  if (orientation == 1) rotate(false);
  else if (orientation == 3) rotate(true);
  else return;
  orientation = 0;
}

void BldModel::ensureFloor() {
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) {
      uint32_t h = head(r, c);
      if (h != 0 && h < cells.size() && cells[h].z() == 0) continue;
      if (cells.size() >= (1u << 17) - 1) return;
      BldCell f;
      f.packed = h & 0x1FFFF;  // next = old head, z 0, shape 0, rot 0
      cells.push_back(f);
      columnHead[size_t(r) * N + size_t(c)] = uint32_t(cells.size() - 1);
    }
}

bool BldModel::validate(std::string* err) const {
  auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
  if (cells.empty()) return fail("no cells");
  if (columnHead.size() != size_t(N) * N) return fail("grid size");
  std::vector<uint8_t> seen(cells.size(), 0);
  size_t visited = 0;
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) {
      int lastZ = -1;
      uint32_t i = head(r, c);
      size_t steps = 0;
      while (i != 0) {
        if (i >= cells.size()) return fail("cell link out of range");
        if (seen[i]) return fail("cell reachable twice");
        if (++steps > cells.size()) return fail("link cycle");
        seen[i] = 1; visited++;
        const BldCell& ce = cells[i];
        if (int(ce.z()) <= lastZ) return fail("z not increasing");
        lastZ = int(ce.z());
        if (ce.shape() == 15) {
          for (int s = 1; s <= 4; s++) if (ce.face[s][0] >= palette.size()) return fail("prop palette index");
        } else {
          for (int s = 0; s < 6; s++)
            if (ce.face[s][0] >= palette.size() || ce.face[s][2] >= palette.size()) return fail("palette index");
        }
        i = ce.next();
      }
    }
  if (visited != cells.size() - 1) return fail("unreachable cells");
  return true;
}

// ---- parse / write --------------------------------------------------------------------------
static BldModel parseModel(const Bytes& b) {
  Reader r(b);
  BldModel m;
  m.version = r.u8();
  if (m.version != 8) throw FormatError("unsupported .bld model version " + std::to_string(m.version));
  m.orientation = r.u8();
  m.N = r.u8();
  m.lotSize = r.u8();
  r.u8();
  m.H = r.u16();
  r.u16();
  uint32_t cellCount = r.u32();
  r.u32(); r.u32(); r.u32(); r.u32();  // marker, 4, 32, marker
  uint32_t gridBytes = r.u32();
  if (m.N == 0 || m.N > 48) throw FormatError("bad grid size");
  if (gridBytes != uint32_t(m.N) * m.N * 4) throw FormatError("bad grid byte count");
  if (cellCount == 0 || cellCount > (1u << 17)) throw FormatError("bad cell count");
  m.columnHead.resize(size_t(m.N) * m.N);
  for (auto& h : m.columnHead) h = r.u32();
  r.u32();
  uint32_t P = r.u32();
  if (P < 4 || P > r.remaining()) throw FormatError("bad packed size");
  Bytes block = r.bytes(P);
  r.u32();
  Bytes raw = refpackDecode(block.data() + 4, block.size() - 4);
  if (raw.size() < size_t(cellCount) * 32) throw FormatError("cell block too short");
  m.cells.resize(cellCount);
  for (uint32_t i = 0; i < cellCount; i++) {
    const uint8_t* p = &raw[size_t(i) * 32];
    m.cells[i].packed = rd32(p);
    m.cells[i].faceFlags = rd32(p + 4);
    std::memcpy(m.cells[i].face, p + 8, 24);
  }
  uint32_t pc = r.u32();
  if (pc == 0 || pc > 256) throw FormatError("bad palette count");
  for (uint32_t i = 0; i < pc; i++) {
    BldPaletteEntry e;
    uint32_t nl = r.u32();
    if (nl > 4096 || nl > r.remaining()) throw FormatError("bad palette name");
    e.name = r.str(nl);
    e.textureId = r.u32();
    m.palette.push_back(e);
  }
  return m;
}

static Bytes serializeModel(const BldModel& m) {
  Bytes b;
  b.push_back(8); b.push_back(m.orientation); b.push_back(m.N); b.push_back(m.lotSize); b.push_back(0xEA);
  put16(b, m.H); put16(b, 0xEAEA);
  put32(b, uint32_t(m.cells.size()));
  put32(b, 0xEAEAEAEA); put32(b, 4); put32(b, 32); put32(b, 0xEAEAEAEA);
  put32(b, uint32_t(m.columnHead.size() * 4));
  for (uint32_t h : m.columnHead) put32(b, h);
  put32(b, 0xEAEAEAEA);
  Bytes raw;
  for (const auto& c : m.cells) {
    put32(raw, c.packed); put32(raw, c.faceFlags);
    raw.insert(raw.end(), c.face[0], c.face[0] + 24);
  }
  size_t n = raw.size();
  raw.resize(n + n / 9 + 1000, 0);  // original over-long compression buffer
  Bytes rp = refpackEncode(raw);
  put32(b, uint32_t(rp.size() + 4));
  put32(b, uint32_t(rp.size() + 4));
  b.insert(b.end(), rp.begin(), rp.end());
  put32(b, 0xEAEAEAEA);
  put32(b, uint32_t(m.palette.size()));
  for (const auto& e : m.palette) { wrStr32(b, e.name); put32(b, e.textureId); }
  put32(b, 0xEAEAEAEA);
  return b;
}

static Bytes packedImage(uint16_t fmt, uint16_t w, uint16_t h, const Bytes& raw) {
  Bytes b;
  put16(b, fmt); put16(b, w); put16(b, h);
  Bytes rp = refpackEncode(raw);
  put32(b, uint32_t(rp.size() + 4));
  b.insert(b.end(), rp.begin(), rp.end());
  return b;
}
static Bytes unpackImage(const Bytes& b, uint16_t& fmt, uint16_t& w, uint16_t& h) {
  if (b.size() < 10) throw FormatError("image record too short");
  fmt = rd16(&b[0]); w = rd16(&b[2]); h = rd16(&b[4]);
  return refpackDecode(b.data() + 10, b.size() - 10);
}

BldFile BldFile::parse(const Bytes& data) {
  IxfFile f;
  if (!f.openMemory(data) || f.entries().empty()) throw FormatError("not an IXF .bld file");
  BldFile o;
  Bytes b;
  if (!f.getBytes(kBldModelKey, b)) throw FormatError(".bld: no model record");
  o.model = parseModel(b);
  std::map<uint32_t, BldTexture> tex;
  std::map<uint32_t, BldProp> props;
  for (const auto& e : f.entries()) {
    const ResKey& k = e.key;
    if (k.group != kBldGroup) { Bytes u; if (f.getBytes(k, u)) o.unknown.emplace_back(k, u); continue; }
    if (!f.getBytes(k, b)) continue;
    Reader r(b);
    switch (k.type) {
      case 0xE3F87B0C: break;
      case 0xE3F87B0D: {
        BldTexture t;
        t.version = r.u8();
        for (auto& v : t.imageRect) v = r.u32();
        for (auto& v : t.swatchRect) v = r.u32();
        t.textureId = r.u32(); t.sourceKind = r.u32();
        t.width = r.f32(); t.height = r.f32(); t.invWidth = r.f32(); t.invHeight = r.f32();
        uint32_t nl = r.u32();
        if (nl > r.remaining()) throw FormatError("texture name overrun");
        t.name = r.str(nl);
        r.skip(12);
        t.category = r.u32();
        BldTexture& dst = tex[k.instance];
        Bytes keepPx = std::move(dst.bgra); uint16_t pw = dst.pxW, ph = dst.pxH, pf = dst.pxFormat;
        dst = t; dst.bgra = std::move(keepPx); dst.pxW = pw; dst.pxH = ph; dst.pxFormat = pf;
        break;
      }
      case 0xE3F87B10: {
        BldTexture& t = tex[k.instance];
        t.bgra = unpackImage(b, t.pxFormat, t.pxW, t.pxH);
        break;
      }
      case 0xFA662D17: {
        BldProp p;
        p.index = k.instance;
        r.u8();
        for (auto& v : p.view) v = rdStr32(r);
        for (auto& v : p.size) v = r.u32();
        props[k.instance] = p;
        break;
      }
      case 0xE3F87B0F: {
        o.lotSize = r.u8();
        o.name = r.str(r.u8());
        o.author = r.str(r.u8());
        o.hasInfoTrailer = r.remaining() >= 8;
        if (o.hasInfoTrailer) { o.reserved = r.u32(); o.buildingId = r.u32(); }
        break;
      }
      case 0x644D7178: if (!b.empty()) o.description = r.str(std::min<size_t>(r.u8(), r.remaining())); break;
      case 0x644D7179: if (b.size() >= 4) o.category = r.u32(); break;
      case 0xB9EC8F72: if (b.size() >= 4) o.languageCode = r.u32(); break;
      case 0x99F88B9E: for (size_t i = 0; i < 4 && i < b.size(); i++) o.revision[i] = b[i]; break;
      case 0xE3F87B0E: {
        uint16_t fmt, w, h;
        Bytes raw = unpackImage(b, fmt, w, h);
        o.thumbFormat = fmt; o.thumbW = w; o.thumbH = h;
        o.thumb.assign(size_t(w) * h, 0);
        for (size_t i = 0; i < o.thumb.size() && i * 2 + 1 < raw.size(); i++) o.thumb[i] = rd16(&raw[i * 2]);
        o.hasThumb = true;
        break;
      }
      case 0xDA6DC9A9: {
        o.owner = rdStr32(r);
        o.ownerBuildingId = r.remaining() >= 4 ? r.u32() : 0;
        o.hasOwner = true;
        break;
      }
      default: o.unknown.emplace_back(k, b);
    }
  }
  if (!tex.empty()) {
    o.textures.resize(size_t(tex.rbegin()->first) + 1);
    for (auto& kv : tex) o.textures[kv.first] = std::move(kv.second);
  }
  for (auto& kv : props) o.props.push_back(kv.second);
  return o;
}

Bytes BldFile::serialize() const {
  IxfWriter w;
  w.add(kBldModelKey, serializeModel(model));
  for (size_t n = 0; n < textures.size(); n++) {
    const BldTexture& t = textures[n];
    Bytes h;
    h.push_back(t.version);
    for (uint32_t v : t.imageRect) put32(h, v);
    for (uint32_t v : t.swatchRect) put32(h, v);
    put32(h, t.textureId); put32(h, t.sourceKind);
    for (float f : {t.width, t.height, t.invWidth, t.invHeight}) { uint32_t u; std::memcpy(&u, &f, 4); put32(h, u); }
    wrStr32(h, t.name);
    putKey(h, ResKey{0xE3F87B10, kBldGroup, uint32_t(n)});
    put32(h, t.category);
    w.add({0xE3F87B0D, kBldGroup, uint32_t(n)}, h);
    w.add({0xE3F87B10, kBldGroup, uint32_t(n)}, packedImage(t.pxFormat, t.pxW, t.pxH, t.bgra));
  }
  for (const auto& p : props) {
    Bytes h;
    h.push_back(0x0A);
    for (const auto& v : p.view) wrStr32(h, v);
    for (uint32_t v : p.size) put32(h, v);
    w.add({0xFA662D17, kBldGroup, p.index}, h);
  }
  Bytes info;
  info.push_back(lotSize);
  info.push_back(uint8_t(std::min<size_t>(name.size(), 255)));
  info.insert(info.end(), name.begin(), name.begin() + std::min<size_t>(name.size(), 255));
  info.push_back(uint8_t(std::min<size_t>(author.size(), 255)));
  info.insert(info.end(), author.begin(), author.begin() + std::min<size_t>(author.size(), 255));
  put32(info, reserved); put32(info, buildingId);
  w.add(kBldInfoKey, info);
  Bytes d;
  d.push_back(uint8_t(std::min<size_t>(description.size(), 255)));
  d.insert(d.end(), description.begin(), description.begin() + std::min<size_t>(description.size(), 255));
  w.add(kBldDescKey, d);
  Bytes cat; put32(cat, category); w.add(kBldCategoryKey, cat);
  Bytes lang; put32(lang, languageCode); w.add(kBldLangKey, lang);
  Bytes rev(revision, revision + 4); w.add(kBldRevisionKey, rev);
  if (hasThumb) {
    Bytes raw;
    for (uint16_t v : thumb) put16(raw, v);
    w.add(kBldThumbKey, packedImage(thumbFormat, thumbW, thumbH, raw));
  }
  if (hasOwner) {
    Bytes o; wrStr32(o, owner); put32(o, ownerBuildingId);
    w.add(kBldOwnerKey, o);
  }
  for (const auto& u : unknown) w.add(u.first, u.second);
  return w.build();
}

uint32_t BldFile::effectiveId() const {
  if (buildingId) return buildingId;
  if (ownerBuildingId) return ownerBuildingId;
  uint32_t h = 2166136261u;
  for (char c : name + "|" + author) h = (h ^ uint8_t(c)) * 16777619u;
  h ^= uint32_t(model.N) << 24;
  return h ? h : 1;
}

// ---- helpers --------------------------------------------------------------------------------
bool BldPaletteEntry::split(std::string& baseName, uint32_t& id) const {
  // >= 10 characters, containing " |" followed by 8 hex digits.
  if (name.size() < 10) return false;
  size_t p = name.rfind(" |");
  if (p == std::string::npos || name.size() - (p + 2) != 8) return false;
  uint32_t v = 0;
  for (size_t i = p + 2; i < name.size(); i++) {
    char c = name[i];
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) return false;
    v = v << 4 | uint32_t(d);
  }
  baseName = name.substr(0, p);
  id = v;
  return true;
}
bool BldPaletteEntry::solidColor(int rgb[3]) const {
  std::string low = toLower(name);
  return low.compare(0, 6, "color ") == 0 && std::sscanf(low.c_str() + 6, "%d , %d , %d", &rgb[0], &rgb[1], &rgb[2]) == 3;
}

const char* batCategoryName(uint32_t code) {
  static const char* const n[16] = {"Residential", "Commercial", "Industrial", "Police Station", "Jail", "Fire Station",
                                    "Hospital", "School", "College", "Library", "Museum", "Historic Statue",
                                    "Mayor's House", "City Hall", "Performing Arts Center", "Other"};
  return code >= 0x208 && code <= 0x217 ? n[code - 0x208] : nullptr;
}

bool BldFile::needsRevisionConversion() const { return revision[0] < 2; }

// ---- rendering ------------------------------------------------------------------------------
BatLights BatLights::fromIni(const IniFile& ini) {
  BatLights l;
  auto rd = [&](const char* key, uint8_t* dst) {
    std::string v;
    if (!ini.get("Lights", key, v)) return;
    int a, b, c;
    if (std::sscanf(v.c_str(), "%d , %d , %d", &a, &b, &c) == 3 || std::sscanf(v.c_str(), "%d,%d,%d", &a, &b, &c) == 3) {
      dst[0] = uint8_t(a); dst[1] = uint8_t(b); dst[2] = uint8_t(c);
    }
  };
  rd("PreviewNorth", l.north); rd("PreviewWest", l.west); rd("PreviewSouth", l.south);
  rd("PreviewEast", l.east); rd("PreviewTop", l.top);
  return l;
}

namespace {
struct PtD { double x, y; };

void fillPoly(Image& img, const PtD* p, int n, uint32_t col) {
  double miny = p[0].y, maxy = p[0].y;
  for (int i = 1; i < n; i++) { miny = std::min(miny, p[i].y); maxy = std::max(maxy, p[i].y); }
  int y0 = std::max(0, int(std::ceil(miny - 0.5))), y1 = std::min(img.h - 1, int(std::floor(maxy - 0.5)));
  for (int y = y0; y <= y1; y++) {
    double cy = y + 0.5, xl = 1e18, xr = -1e18;
    for (int i = 0; i < n; i++) {
      PtD a = p[i], b = p[(i + 1) % n];
      if ((a.y <= cy && b.y > cy) || (b.y <= cy && a.y > cy)) {
        double x = a.x + (cy - a.y) * (b.x - a.x) / (b.y - a.y);
        xl = std::min(xl, x); xr = std::max(xr, x);
      }
    }
    if (xl > xr) continue;
    int xs = std::max(0, int(std::ceil(xl - 0.5))), xe = std::min(img.w - 1, int(std::floor(xr - 0.5)));
    for (int x = xs; x <= xe; x++) img.px[size_t(y) * img.w + x] = col;
  }
}

void paintColour(const BldModel& m, uint8_t idx, uint8_t out[3]) {
  out[0] = out[1] = out[2] = 180;
  if (idx == 0 || idx >= m.palette.size()) return;
  const std::string& nm = m.palette[idx].name;
  std::string low = toLower(nm);
  int r, g, b;
  if (low.compare(0, 6, "color ") == 0 && std::sscanf(low.c_str() + 6, "%d , %d , %d", &r, &g, &b) == 3) {
    out[0] = uint8_t(r); out[1] = uint8_t(g); out[2] = uint8_t(b);
    return;
  }
  uint32_t h = 2166136261u;
  for (char c : low) h = (h ^ uint8_t(c)) * 16777619u;
  out[0] = uint8_t(110 + (h & 0x3F)); out[1] = uint8_t(110 + ((h >> 8) & 0x3F)); out[2] = uint8_t(110 + ((h >> 16) & 0x3F));
}

// ---- shape table (spec 24 section 3.2.4), rot = 0 --------------------------------------------
// Polygon k of a shape owns face slot k. Bottom polygons are never visible from the camera side and
// are omitted. Kinds: 0 wall, 1 45 slope, 2 top, 3 steep, 4 shallow, 5 underside, 7 floor.
struct ShapePoly { uint8_t kind, dir, n; float v[4][3]; };
struct Shape { int count; ShapePoly p[7]; };
#define PW(d, ...) ShapePoly{0, d, 4, {__VA_ARGS__}}
#define PW3(d, ...) ShapePoly{0, d, 3, {__VA_ARGS__}}
#define PT(...) ShapePoly{2, 0, 4, {__VA_ARGS__}}
#define PT3(...) ShapePoly{2, 0, 3, {__VA_ARGS__}}
#define PS(k, d, ...) ShapePoly{k, d, 4, {__VA_ARGS__}}
#define PS3(k, d, ...) ShapePoly{k, d, 3, {__VA_ARGS__}}
const Shape kShapes[16] = {
  /*0*/ {5, {PT({1,1,1},{1,0,1},{0,0,1},{0,1,1}), PW(3,{0,1,1},{0,0,1},{0,0,0},{0,1,0}), PW(5,{0,0,1},{1,0,1},{1,0,0},{0,0,0}),
             PW(7,{1,0,1},{1,1,1},{1,1,0},{1,0,0}), PW(1,{1,1,1},{0,1,1},{0,1,0},{1,1,0})}},
  /*1*/ {4, {PT3({1,1,1},{1,0,1},{0,0,1}), PW(2,{1,1,1},{0,0,1},{0,0,0},{1,1,0}), PW(5,{0,0,1},{1,0,1},{1,0,0},{0,0,0}),
             PW(7,{1,0,1},{1,1,1},{1,1,0},{1,0,0})}},
  /*2*/ {4, {PW3(1,{1,1,1},{0,1,0},{1,1,0}), PS(1,3,{1,1,1},{1,0,1},{0,0,0},{0,1,0}), PW3(5,{1,0,1},{1,0,0},{0,0,0}),
             PW(7,{1,0,1},{1,1,1},{1,1,0},{1,0,0})}},
  /*3*/ {6, {PS3(1,1,{1,0,1},{0,0,1},{0,1,0}), PS3(1,3,{1,1,1},{1,0,1},{0,1,0}), PW(5,{0,0,1},{1,0,1},{1,0,0},{0,0,0}),
             PW(7,{1,0,1},{1,1,1},{1,1,0},{1,0,0}), PW3(1,{1,1,1},{0,1,0},{1,1,0}), PW3(3,{0,0,1},{0,0,0},{0,1,0})}},
  /*4*/ {4, {PS3(1,1,{1,0,1},{0,1,0},{1,1,0}), PS3(1,3,{1,0,1},{0,0,0},{0,1,0}), PW3(5,{1,0,1},{1,0,0},{0,0,0}),
             PW3(7,{1,0,1},{1,1,0},{1,0,0})}},
  /*5*/ {5, {PT({1,1,1},{1,0,1},{0,0,1},{0,1,1}), PW3(5,{0,0,1},{1,0,1},{1,0,0}), PW(7,{1,0,1},{1,1,1},{1,1,0},{1,0,0}),
             PW3(1,{1,1,1},{0,1,1},{1,1,0}), PS(5,0,{0,1,1},{0,0,1},{1,0,0},{1,1,0})}},
  /*6*/ {6, {PT3({1,1,1},{1,0,1},{0,0,1}), PW3(3,{0,0,1},{0,0,0},{0,1,0}), PW(5,{0,0,1},{1,0,1},{1,0,0},{0,0,0}),
             PW(7,{1,0,1},{1,1,1},{1,1,0},{1,0,0}), PW3(1,{1,1,1},{0,1,0},{1,1,0}), PS3(3,2,{1,1,1},{0,0,1},{0,1,0})}},
  /*7*/ {3, {PS3(3,2,{1,0,1},{0,0,0},{1,1,0}), PW3(5,{1,0,1},{1,0,0},{0,0,0}), PW3(7,{1,0,1},{1,1,0},{1,0,0})}},
  /*8*/ {1, {PS(7,0,{1,1,0},{1,0,0},{0,0,0},{0,1,0})}},
  /*9*/ {5, {PW(1,{1,1,1},{0,1,.5f},{0,1,0},{1,1,0}), PS(4,3,{1,1,1},{1,0,1},{0,0,.5f},{0,1,.5f}), PW(5,{1,0,1},{1,0,0},{0,0,0},{0,0,.5f}),
             PW(7,{1,0,1},{1,1,1},{1,1,0},{1,0,0}), PW(3,{0,1,.5f},{0,0,.5f},{0,0,0},{0,1,0})}},
  /*10*/ {4, {PW3(1,{1,1,.5f},{0,1,0},{1,1,0}), PS(4,3,{1,1,.5f},{1,0,.5f},{0,0,0},{0,1,0}), PW3(5,{1,0,.5f},{1,0,0},{0,0,0}),
              PW(7,{1,0,.5f},{1,1,.5f},{1,1,0},{1,0,0})}},
  /*11*/ {6, {PS3(4,1,{1,0,1},{0,0,1},{0,1,.5f}), PS3(4,3,{1,1,1},{1,0,1},{0,1,.5f}), PW(5,{0,0,1},{1,0,1},{1,0,0},{0,0,0}),
              PW(7,{1,0,1},{1,1,1},{1,1,0},{1,0,0}), PW(1,{1,1,1},{0,1,.5f},{0,1,0},{1,1,0}), PW(3,{0,0,1},{0,0,0},{0,1,0},{0,1,.5f})}},
  /*12*/ {6, {PS3(4,1,{1,0,.5f},{0,0,.5f},{0,1,0}), PS3(4,3,{1,1,.5f},{1,0,.5f},{0,1,0}), PW(5,{0,0,.5f},{1,0,.5f},{1,0,0},{0,0,0}),
              PW(7,{1,0,.5f},{1,1,.5f},{1,1,0},{1,0,0}), PW3(1,{1,1,.5f},{0,1,0},{1,1,0}), PW3(3,{0,0,.5f},{0,0,0},{0,1,0})}},
  /*13*/ {6, {PS3(4,1,{1,0,1},{0,1,.5f},{1,1,.5f}), PS3(4,3,{1,0,1},{0,0,.5f},{0,1,.5f}), PW(5,{1,0,1},{1,0,0},{0,0,0},{0,0,.5f}),
              PW(7,{1,0,1},{1,1,.5f},{1,1,0},{1,0,0}), PW(1,{1,1,.5f},{0,1,.5f},{0,1,0},{1,1,0}), PW(3,{0,1,.5f},{0,0,.5f},{0,0,0},{0,1,0})}},
  /*14*/ {4, {PS3(4,1,{1,0,.5f},{0,1,0},{1,1,0}), PS3(4,3,{1,0,.5f},{0,0,0},{0,1,0}), PW3(5,{1,0,.5f},{1,0,0},{0,0,0}),
              PW3(7,{1,0,.5f},{1,1,0},{1,0,0})}},
  /*15*/ {0, {}},  // prop cell: never drawn as geometry
};
#undef PW
#undef PW3
#undef PT
#undef PT3
#undef PS
#undef PS3

// Horizontal / vertical part of the lighting normal per surface kind (spec 3.2.2).
void kindParts(int kind, double& h, double& v) {
  switch (kind) {
    case 0: h = 1; v = 0; break;
    case 1: h = 0.7071; v = 0.7071; break;
    case 3: h = 0.866; v = 0.5; break;
    case 4: h = 0.5; v = 0.866; break;
    case 5: case 6: h = 0; v = -1; break;
    default: h = 0; v = 1; break;  // top, floor
  }
}
// Unit horizontal vector for a direction code (0 = +x+y, 1 = +y, 2 = -x+y, 3 = -x, ...), mod 8.
void dirVec(int dir, double& x, double& y) {
  static const double d[8][2] = {{1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0}};
  x = d[dir & 7][0]; y = d[dir & 7][1];
  if (x != 0 && y != 0) { x *= 0.70710678; y *= 0.70710678; }
}
}  // namespace

Image renderBldView(const BldModel& model, int view, int reduce, const BatLights& L) {
  if (model.columnHead.size() != size_t(model.N) * model.N || model.cells.empty()) return Image();
  BldModel m = model;
  m.normalizeOrientation();
  for (int i = 0; i < (view & 3); i++) m.rotate(true);
  const int N = m.N, s = 8;
  struct Item { int r, c, z; const BldCell* cell; };
  std::vector<Item> items;
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) {
      std::vector<uint32_t> col;
      try { col = m.column(r, c); } catch (const FormatError&) { continue; }
      for (uint32_t i : col) if (m.cells[i].shape() != 15) items.push_back({r, c, int(m.cells[i].z()), &m.cells[i]});
    }
  Image img;
  if (items.empty()) return img;
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    return a.r + a.c != b.r + b.c ? a.r + a.c < b.r + b.c : a.z < b.z;
  });
  int minY = 0;
  for (const auto& it : items) minY = std::min(minY, s * (it.r + it.c) / 2 - s * (it.z + 1));
  int maxY = s * N;
  int rawH = maxY - minY;
  int red = std::max(1, reduce);
  int H = (rawH + red - 1) / red * red, pad = H - rawH;
  int W = 2 * s * N;
  Image raw; raw.w = W; raw.h = H; raw.px.assign(size_t(W) * H, 0);
  auto P = [&](double x, double y, double z) { return PtD{s * (y - x) + s * N, s * (x + y) / 2 - s * z - minY + pad}; };
  auto lightFor = [&](double nx, double ny, double nz, uint8_t* out) {
    struct Ld { double x, y, z; const uint8_t* c; };
    const Ld lights[6] = {{1, 0, 0, L.north}, {0, 1, 0, L.west}, {-1, 0, 0, L.south}, {0, -1, 0, L.east},
                          {0, 0, 1, L.top}, {0, 0, -1, L.top}};
    double acc[3] = {0, 0, 0};
    for (const auto& l : lights) {
      double d = nx * l.x + ny * l.y + nz * l.z;
      if (d <= 0) continue;
      for (int k = 0; k < 3; k++) acc[k] += d * l.c[k] / 255.0;
    }
    for (int k = 0; k < 3; k++) out[k] = uint8_t(std::min(1.0, acc[k]) * 255.0 + 0.5);
  };
  for (const auto& it : items) {
    const BldCell& ce = *it.cell;
    const Shape& sh = kShapes[ce.shape() & 15];
    int rot = int(ce.rot());
    for (int k = 0; k < sh.count; k++) {
      const ShapePoly& sp = sh.p[k];
      // Camera-facing test on the turned normal; lights stay fixed to the unturned model.
      double hp, vp, hx, hy;
      kindParts(sp.kind, hp, vp);
      dirVec(sp.dir + 2 * rot, hx, hy);
      if (hp * (hx + hy) + vp < -1e-6) continue;
      dirVec(sp.dir + 2 * rot - 2 * (view & 3) + 8, hx, hy);
      uint8_t lc[3], col[3], sc[3];
      lightFor(hp * hx, hp * hy, vp, lc);
      paintColour(m, ce.face[k][0], col);
      for (int q = 0; q < 3; q++) sc[q] = uint8_t(col[q] * lc[q] / 255);
      PtD pts[4] = {};
      for (int q = 0; q < sp.n; q++) {
        double vx = sp.v[q][0], vy = sp.v[q][1];
        for (int r2 = 0; r2 < rot; r2++) { double nx = 1 - vy, ny = vx; vx = nx; vy = ny; }  // quarter-turn about the cell centre
        pts[q] = P(it.r + vx, it.c + vy, it.z + double(sp.v[q][2]));
      }
      fillPoly(raw, pts, sp.n, rgba(sc[0], sc[1], sc[2]));
    }
  }
  if (red == 1) return raw;
  img.w = W / red; img.h = H / red;
  img.px.assign(size_t(img.w) * img.h, 0);
  for (int y = 0; y < img.h; y++)
    for (int x = 0; x < img.w; x++) {
      int cnt = 0, rs = 0, gs = 0, bs = 0;
      for (int dy = 0; dy < red; dy++)
        for (int dx = 0; dx < red; dx++) {
          uint32_t p = raw.px[size_t(y * red + dy) * W + size_t(x * red + dx)];
          if (!(p >> 24)) continue;
          cnt++; rs += p & 255; gs += (p >> 8) & 255; bs += (p >> 16) & 255;
        }
      if (cnt * 2 >= red * red) img.px[size_t(y) * img.w + x] = rgba(uint8_t(rs / cnt), uint8_t(gs / cnt), uint8_t(bs / cnt));
    }
  return img;
}

// ---- sprite encoding ------------------------------------------------------------------------
Bytes encodeSprite565(const Image& img) {
  int w = std::max(1, img.w), h = std::max(1, img.h);
  const uint16_t key = 0xF81F;
  std::vector<uint16_t> pix;
  struct Row { uint32_t index; uint16_t skip, cf; };
  std::vector<Row> rows;
  for (int y = 0; y < h; y++) {
    int first = -1, last = -1;
    for (int x = 0; x < img.w && y < img.h; x++)
      if (img.px[size_t(y) * img.w + x] >> 24 >= 128) { if (first < 0) first = x; last = x; }
    Row r{uint32_t(pix.size()), 0, 0x8000};
    if (first >= 0) {
      r.skip = uint16_t(first);
      bool solid = true;
      for (int x = first; x <= last; x++) {
        uint32_t p = img.px[size_t(y) * img.w + x];
        uint16_t v = key;
        if (p >> 24 >= 128) {
          v = uint16_t(((p & 255) >> 3) << 11 | (((p >> 8) & 255) >> 2) << 5 | ((p >> 16) & 255) >> 3);
          if (v == key) v = 0xF81E;
        }
        if (v == key) solid = false;
        pix.push_back(v);
      }
      r.cf = uint16_t((last - first + 1) | (solid ? 0x8000 : 0));
    }
    rows.push_back(r);
  }
  Bytes body;
  put32(body, uint32_t(0x10 + rows.size() * 8 + pix.size() * 2));
  put16(body, uint16_t(w)); put16(body, uint16_t(h)); put16(body, 4); put16(body, 7); put16(body, key); put16(body, 0);
  for (const auto& r : rows) { put32(body, r.index); put16(body, r.skip); put16(body, r.cf); }
  for (uint16_t v : pix) put16(body, v);
  Bytes rp = refpackEncode(body);
  Bytes out;
  out.push_back(7); out.push_back(1); put16(out, 0);
  put32(out, 0x00080000); put32(out, uint32_t(w)); put32(out, uint32_t(h));
  put32(out, uint32_t(rp.size() + 4));
  out.insert(out.end(), rp.begin(), rp.end());
  return out;
}

Bytes makeSpanRecord(int16_t L, int16_t T, int16_t R, int16_t B) {
  Bytes b;
  put16(b, uint16_t(L)); put16(b, uint16_t(T)); put16(b, uint16_t(R)); put16(b, uint16_t(B));
  return b;
}

// ---- BAT.DAT --------------------------------------------------------------------------------
bool BatDirEntry::parse(const Bytes& b, BatDirEntry& o) {
  try {
    Reader r(b);
    o.lotSize = r.u8();
    o.appearance = rdKey(r);
    o.name = rdStr32(r); o.author = rdStr32(r); o.description = rdStr32(r); o.owner = rdStr32(r);
    o.buildingId = r.u32();
    return true;
  } catch (const FormatError&) { return false; }
}
Bytes BatDirEntry::serialize() const {
  Bytes b;
  b.push_back(lotSize);
  putKey(b, appearance);
  wrStr32(b, name); wrStr32(b, author); wrStr32(b, description); wrStr32(b, owner);
  put32(b, buildingId);
  return b;
}

bool BatDatabase::load(const Bytes& ixf) {
  recs_.clear();
  IxfFile f;
  if (!f.openMemory(ixf)) return false;
  for (const auto& e : f.entries()) {
    Bytes b;
    if (f.getBytes(e.key, b)) recs_[e.key] = std::move(b);
  }
  return true;
}
Bytes BatDatabase::build() const {
  IxfWriter w;
  for (const auto& kv : recs_) w.add(kv.first, kv.second);
  return w.build();
}
const Bytes* BatDatabase::record(const ResKey& k) const {
  auto it = recs_.find(k);
  return it == recs_.end() ? nullptr : &it->second;
}
std::vector<BatDirEntry> BatDatabase::list() const {
  std::vector<BatDirEntry> v;
  for (const auto& kv : recs_) {
    if (kv.first.type != kBatDirType || kv.first.group != kBatGroup) continue;
    BatDirEntry e;
    if (BatDirEntry::parse(kv.second, e)) v.push_back(e);
  }
  std::sort(v.begin(), v.end(), [](const BatDirEntry& a, const BatDirEntry& b) { return a.buildingId < b.buildingId; });
  return v;
}
bool BatDatabase::has(uint32_t id) const { return recs_.count({kBatDirType, kBatGroup, id}) != 0; }
bool BatDatabase::find(uint32_t id, BatDirEntry& out) const {
  const Bytes* b = record({kBatDirType, kBatGroup, id});
  return b && BatDirEntry::parse(*b, out);
}
std::vector<uint32_t> BatDatabase::spriteIds(uint32_t id) const {
  std::vector<uint32_t> ids;
  const Bytes* b = record({T_CSATTRIB, kBatGroup, id});
  if (!b) return ids;
  CsAttrib c = parseCsAttrib(b->data(), b->size());
  for (size_t i = 0; i < c.frames.size() && i < 16; i++) ids.push_back(c.frames[i].spriteId);
  return ids;
}
bool BatDatabase::remove(uint32_t id) {
  if (!has(id)) return false;
  for (uint32_t s : spriteIds(id)) { recs_.erase({0, kBatGroup, s}); recs_.erase({1, kBatGroup, s}); }
  recs_.erase({T_CSATTRIB, kBatGroup, id});
  recs_.erase({kBatDirType, kBatGroup, id});
  return true;
}
uint32_t BatDatabase::allocateSpriteBase() const {
  uint32_t mx = 0x0000FFF0;
  for (const auto& kv : recs_)
    if (kv.first.group == kBatGroup && (kv.first.type == 0 || kv.first.type == 1)) mx = std::max(mx, kv.first.instance);
  return (mx + 16) & ~15u;
}

uint32_t BatDatabase::import(const BldFile& b, const BatLights& lights, std::string* err) {
  int lot = b.model.lotSize;
  if (lot < 1 || lot > 4) { if (err) *err = "lot size out of range"; return 0; }
  uint32_t id = b.effectiveId();
  remove(id);
  BldModel m = b.model;
  m.normalizeOrientation();
  m.ensureFloor();
  uint32_t base = allocateSpriteBase();
  for (int zoom = 0; zoom < 4; zoom++)
    for (int view = 0; view < 4; view++) {
      Image img = renderBldView(m, view, 1 << (4 - zoom), lights);
      if (img.empty()) { img.w = img.h = 1; img.px.assign(1, 0); }
      int W = 8 * (1 << zoom) * lot;
      uint32_t sid = base + uint32_t(zoom * 4 + view);
      recs_[{0, kBatGroup, sid}] = encodeSprite565(img);
      recs_[{1, kBatGroup, sid}] = makeSpanRecord(0, int16_t(img.h - W / 4), int16_t(img.w), int16_t(W / 4));
    }
  Bytes cs = {'B', 'I', 'N', '\r'};
  put32(cs, 1); put32(cs, 0x7300);
  cs.push_back(5); cs.push_back(4); cs.push_back(4); cs.push_back(20); cs.push_back(1); cs.push_back(0);
  put32(cs, 0); put16(cs, 20);
  for (int i = 0; i < 20; i++) {
    int src = i < 16 ? i : 12 + (i - 16);
    put16(cs, uint16_t(i)); put32(cs, kBatGroup); put32(cs, base + uint32_t(src));
    cs.push_back(i < 16 ? 0 : 2); put16(cs, 0); put16(cs, 0);
  }
  recs_[{T_CSATTRIB, kBatGroup, id}] = cs;
  BatDirEntry d;
  d.lotSize = uint8_t(lot);
  d.appearance = {T_CSATTRIB, kBatGroup, id};
  d.name = b.name; d.author = b.author; d.description = b.description; d.owner = b.hasOwner ? b.owner : "";
  d.buildingId = id;
  recs_[{kBatDirType, kBatGroup, id}] = d.serialize();
  return id;
}

BatImportResult batImport(BatDatabase& db, const std::vector<BldFile>& files, const std::set<uint32_t>* keep, const BatLights& lights) {
  BatImportResult res;
  for (const auto& f : files) {
    std::string err;
    uint32_t id = db.import(f, lights, &err);
    if (id) res.imported.push_back(id); else res.skipped.push_back(f.name + ": " + err);
  }
  if (keep)
    for (const auto& e : db.list())
      if (!keep->count(e.buildingId)) db.remove(e.buildingId);
  return res;
}

bool batImportInstall(const fs::path& root, const std::vector<std::string>& names, BatImportResult& out, std::string& err) {
  fs::path apps = resolveCI(root, "Apps");
  if (apps.empty()) { err = "no Apps directory"; return false; }
  fs::path bdir = resolveCI(root, "Buildings");
  std::vector<BldFile> files;
  for (const auto& n : names) {
    if (bdir.empty()) break;
    fs::path p = resolveCI(bdir, n);
    Bytes data;
    if (p.empty() || !readFile(p, data)) continue;  // missing files skipped silently
    try { files.push_back(BldFile::parse(data)); }
    catch (const FormatError& e) { out.skipped.push_back(n + ": " + e.what()); }
  }
  std::error_code ec;
  fs::path rdir = resolveCI(apps, "BARender");
  if (rdir.empty()) { rdir = apps / "BARender"; fs::create_directories(rdir, ec); }
  fs::path dat = resolveCI(rdir, "BAT.DAT");
  if (dat.empty()) dat = rdir / "BAT.DAT";
  BatDatabase db;
  Bytes cur;
  if (fs::exists(dat) && readFile(dat, cur)) {
    db.load(cur);
    fs::path ba = resolveCI(apps, "Res/BA");
    if (!ba.empty()) {
      fs::path bak = ba / "RenderedBuildingsBackup.IXF";
      fs::remove(bak, ec);
      fs::copy_file(dat, bak, ec);
    }
  }
  BatLights lights;
  fs::path cfg = resolveCI(apps, "Res/BA/BATConfig.ini");
  Bytes cfgData;
  if (!cfg.empty() && readFile(cfg, cfgData)) lights = BatLights::fromIni(IniFile::fromText(std::string(cfgData.begin(), cfgData.end())));
  BatImportResult r = batImport(db, files, nullptr, lights);
  out.imported.insert(out.imported.end(), r.imported.begin(), r.imported.end());
  out.skipped.insert(out.skipped.end(), r.skipped.begin(), r.skipped.end());
  Bytes outBytes = db.build();
  FILE* f = std::fopen(dat.string().c_str(), "wb");
  if (!f) { err = "cannot write " + dat.string(); return false; }
  bool ok = std::fwrite(outBytes.data(), 1, outBytes.size(), f) == outBytes.size();
  std::fclose(f);
  if (!ok) err = "short write";
  return ok;
}

// ---- user building set ----------------------------------------------------------------------
bool UserBuildingSet::parse(const Bytes& b, UserBuildingSet& o) {
  o.pairs.clear();
  try {
    Reader r(b);
    r.skip(12);
    uint32_t n = r.u32();
    if (n > 1000000 || size_t(n) * 24 > r.remaining()) return false;
    for (uint32_t i = 0; i < n; i++) { ResKey a = rdKey(r); ResKey c = rdKey(r); o.pairs.emplace_back(a, c); }
    return true;
  } catch (const FormatError&) { return false; }
}
Bytes UserBuildingSet::serialize() const {
  Bytes b(12, 0);
  auto sorted = pairs;
  std::stable_sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
  put32(b, uint32_t(sorted.size()));
  for (const auto& p : sorted) { putKey(b, p.first); putKey(b, p.second); }
  return b;
}
const ResKey* UserBuildingSet::find(const ResKey& k) const {
  for (const auto& p : pairs) if (p.first == k) return &p.second;
  return nullptr;
}
void UserBuildingSet::set(const ResKey& k, const ResKey& a) {
  for (auto& p : pairs) if (p.first == k) { p.second = a; return; }
  pairs.emplace_back(k, a);
}
bool UserBuildingSet::revert(const ResKey& k) {
  for (size_t i = 0; i < pairs.size(); i++)
    if (pairs[i].first == k) { pairs.erase(pairs.begin() + long(i)); return true; }
  return false;
}

void ReplaceableSet::load(const IniFile& scheme) {
  keys_.clear();
  for (const auto& kv : scheme.section("ReplaceableBuildings")) {
    unsigned long long a, b, c;
    if (std::sscanf(kv.first.c_str(), "%llu , %llu , %llu", &a, &b, &c) == 3)
      keys_.insert(ResKey{uint32_t(a), uint32_t(b), uint32_t(c)});
  }
}

bool canReplace(const ReplaceableSet& rs, const ResKey& occKey, const OccAttr& occ, const BatDirEntry& bat) {
  return rs.contains(occKey) && occ.sizeX == bat.lotSize && occ.sizeZ == bat.lotSize;
}

OccAttr effectiveOccAttr(const OccAttr& base, const ResKey& occKey, const UserBuildingSet& set, const BatDatabase* db) {
  OccAttr o = base;
  const ResKey* r = set.find(occKey);
  if (r && db && r->type == T_CSATTRIB && r->group == kBatGroup && db->record(*r)) o.appearance = *r;
  return o;
}

OccAttr genericOccAttr(uint32_t agentType, uint32_t sizeX, uint32_t sizeZ) {
  OccAttr o;
  o.valid = true;
  o.agentType = agentType;
  o.sizeX = sizeX; o.sizeZ = sizeZ;
  return o;
}

static Bytes rebuildWith(const Bytes& city, const ResKey& key, const Bytes& payload) {
  IxfFile f;
  IxfWriter w;
  bool done = false;
  if (f.openMemory(city)) {
    for (const auto& e : f.entries()) {
      Bytes b;
      if (e.key == key) { w.add(key, payload); done = true; continue; }
      if (f.getBytes(e.key, b)) w.add(e.key, std::move(b));
    }
  }
  if (!done) w.add(key, payload);
  return w.build();
}

bool readUserBuildingSet(const Bytes& city, UserBuildingSet& out) {
  IxfFile f;
  Bytes b;
  if (!f.openMemory(city) || !f.getBytes(kUserBuildingSetKey, b)) return false;
  return UserBuildingSet::parse(b, out);
}
Bytes writeUserBuildingSet(const Bytes& city, const UserBuildingSet& set) {
  return rebuildWith(city, kUserBuildingSetKey, set.serialize());
}

}  // namespace m3k
