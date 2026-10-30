// City-family file loader. See spec/10-save-format.md.
#include <algorithm>
#include "core/records.h"
#include "core/refpack.h"
#include "core/world.h"

namespace m3k {

void World::init(int w, int h) {
  W = w; H = h;
  alt.assign(size_t(W + 1) * (H + 1), 127);
  water.assign(size_t(W + 1) * (H + 1), 0);
  tileBits.assign(size_t(W) * H / 8, 0);
  flora.assign(size_t(W) * H, 0);
  zone.assign(size_t(W) * H, 0);
  occupants.clear(); surface.clear(); pipes.clear(); subway.clear();
}

void InnerDb::parse(Bytes data) {
  data_ = std::move(data);
  recs_.clear();
  if (data_.size() < 8) throw FormatError("inner db too small");
  uint32_t count = rd32(data_.data()), dirOff = rd32(data_.data() + 4);
  if (dirOff > data_.size() || size_t(count) * 16 > data_.size() - dirOff) throw FormatError("inner db: bad directory");
  struct E { ResKey k; uint32_t off; };
  std::vector<E> es;
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t* e = data_.data() + dirOff + size_t(i) * 16;
    E x{{rd32(e), rd32(e + 4), rd32(e + 8)}, rd32(e + 12)};
    if (x.off >= dirOff) continue;
    es.push_back(x);
  }
  std::vector<uint32_t> offs;
  for (auto& e : es) offs.push_back(e.off);
  std::sort(offs.begin(), offs.end());
  for (auto& e : es) {
    auto nx = std::upper_bound(offs.begin(), offs.end(), e.off);
    size_t end = (nx == offs.end()) ? dirOff : *nx;
    recs_[e.k] = {e.off, end - e.off};
  }
}

bool InnerDb::find(const ResKey& k, const uint8_t*& p, size_t& n) const {
  auto it = recs_.find(k);
  if (it == recs_.end()) return false;
  p = data_.data() + it->second.off; n = it->second.len;
  return true;
}

namespace {

struct Hdr { uint16_t version = 0; uint8_t flags = 0; bool legacy = false; };

// Reads the versioned header (spec 10 section 2.1). Leaves reader after the sentinel.
Hdr readHdr(Reader& r) {
  Hdr h;
  size_t start = r.pos();
  h.version = r.u16();
  h.flags = r.u8();
  if (h.flags & 2) r.u8();
  uint32_t s = r.u32();
  if (s != 0xDEADBEEF) { r.seek(start); h.legacy = true; h.version = 0; h.flags = 0; }
  return h;
}
void skipMarker(Reader& r, const Hdr& h) {
  if (h.flags & 1) { uint32_t l = r.u32(); r.skip(l); }
}

std::string rdString(Reader& r) { uint32_t l = r.u32(); return r.str(l); }

void parseCityInfo(const uint8_t* p, size_t n, World& w) {
  Reader r(p, n);
  Hdr h = readHdr(r);
  skipMarker(r, h);
  r.skip(16 + 4);
  rdString(r);  // author
  uint32_t mw = r.u32(), mh = r.u32(); r.u32();
  w.info.cityName = rdString(r);
  w.info.mayorName = rdString(r);
  w.info.dayOfYear = int(r.u32()); w.info.year = int(r.u32());
  w.info.gameSpeed = int(r.u32()); w.info.foundingYear = int(r.u32());
  if (mw == 0 || mh == 0 || mw > 1024 || mh > 1024) throw FormatError("city info: bad map size");
  w.init(int(mw), int(mh));
}

void parseTerrain(const uint8_t* p, size_t n, World& w) {
  Reader r(p, n);
  Hdr h = readHdr(r);
  skipMarker(r, h);
  w.heightScale = r.f32();
  size_t vn = size_t(w.W + 1) * (w.H + 1);
  if (r.remaining() < vn + 1 + vn) throw FormatError("terrain: truncated");
  // Rows stored from x = W down to 0; within a row z = 0..H.
  for (int row = 0; row <= w.W; row++) {
    int x = w.W - row;
    for (int z = 0; z <= w.H; z++) w.alt[w.vIdx(x, z)] = r.u8();
  }
  w.seaLevel = r.u8();
  for (int row = 0; row <= w.W; row++) {
    int x = w.W - row;
    for (int z = 0; z <= w.H; z++) w.water[w.vIdx(x, z)] = r.u8();
  }
  size_t tb = size_t(w.W) * w.H / 8;
  if (r.remaining() >= tb) { auto b = r.bytes(tb); w.tileBits = std::move(b); }
}

void parseFlora(const uint8_t* p, size_t n, World& w) {
  Reader r(p, n);
  Hdr h = readHdr(r);
  if (h.version <= 2) return;
  uint32_t fw = r.u32(), fh = r.u32();
  if (int(fw) != w.W || int(fh) != w.H) throw FormatError("flora: size mismatch");
  if (r.remaining() < size_t(fw) * fh) throw FormatError("flora: truncated");
  for (int row = 0; row < w.W; row++) {
    int x = w.W - 1 - row;
    for (int z = 0; z < w.H; z++) w.flora[w.tIdx(x, z)] = r.u8();
  }
}

void parseZone(const uint8_t* p, size_t n, World& w) {
  size_t N = size_t(w.W) * w.H;
  if (n < N) throw FormatError("zone: truncated");
  for (int row = 0; row < w.W; row++) {
    int x = w.W - 1 - row;
    for (int z = 0; z < w.H; z++) w.zone[w.tIdx(x, z)] = p[size_t(row) * w.H + z];
  }
}

void unpackPlace(uint32_t place, int& x, int& z, int& y, int& rot) {
  x = int(place & 0x7FF); z = int((place >> 11) & 0x7FF); y = int((place >> 22) & 0xFF); rot = int(place >> 30);
}

void parseOccupants(const uint8_t* cp, size_t cn, const uint8_t* ep, size_t en, World& w) {
  Reader cr(cp, cn);
  Hdr ch = readHdr(cr);
  if (ch.version != 2) return;
  uint32_t count = cr.u32(), fmt = cr.u32();
  if (fmt != 1) return;
  Reader r(ep, en);
  readHdr(r);
  w.occupants.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    Occupant o;
    uint32_t place = r.u32(), flags = r.u32();
    o.agentType = r.u32();
    ResKey k; k.type = r.u32(); k.group = r.u32(); k.instance = r.u32();
    unpackPlace(place, o.x, o.z, o.y, o.rot);
    o.sizeX = int(flags & 0x3F); o.sizeZ = int((flags >> 6) & 0x3F);
    if (o.sizeX == 0) o.sizeX = 1;
    if (o.sizeZ == 0) o.sizeZ = 1;
    o.keyType = k.type; o.occId = k.instance;
    if (flags & (1u << 21)) o.name = rdString(r);
    if (flags & (1u << 22)) r.skip(8);
    if (flags & (1u << 27)) o.state = r.u32();
    if (flags & (1u << 28)) r.u32();
    if (flags & (1u << 23)) { uint32_t m = r.u32(); r.skip(size_t(m) * 8); }
    if (flags & (1u << 24)) { uint32_t m = r.u32(); for (uint32_t j = 0; j < m; j++) { r.u32(); rdString(r); } }
    if (flags & (1u << 25)) { uint32_t m = r.u32(); r.skip(size_t(m) * 8); }
    if (flags & (1u << 26)) throw FormatError("occupant: object properties not supported");
    w.occupants.push_back(std::move(o));
  }
}

void parseNetList(const uint8_t* p, size_t n, uint32_t count, std::vector<NetPiece>& out) {
  Reader r(p, n);
  readHdr(r);
  out.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    NetPiece np;
    uint32_t place = r.u32(); np.pieceWord = r.u32();
    unpackPlace(place, np.x, np.z, np.y, np.rot);
    np.pieceId = np.pieceWord & 0xFFFF;
    out.push_back(np);
  }
}

}  // namespace

World loadCity(const Bytes& file, std::vector<std::string>* warnings) {
  auto warn = [&](const std::string& s) { if (warnings) warnings->push_back(s); };
  IxfFile ixf;
  if (!ixf.openMemory(file) || ixf.entries().empty()) throw FormatError("not an IXF city file");
  World w;
  Bytes b;
  try { w.raw = std::make_shared<RawRecordStore>(RawRecordStore::parse(file)); } catch (const FormatError&) { w.raw.reset(); }
  if (!ixf.getBytes({0x00000FA1, 0x617E198D, 0}, b)) throw FormatError("missing city info record");
  parseCityInfo(b.data(), b.size(), w);

  if (ixf.getBytes({0xC359837E, 0x43598384, 0}, b) && b.size() >= 4) w.info.fileKind = int(rd32(b.data()));
  if (ixf.getBytes({0x0525A6B9, 0x4525A6E2, 1}, b) && b.size() >= 12) {
    w.scheme = {rd32(&b[0]), rd32(&b[4]), rd32(&b[8])};
  }
  if (ixf.getBytes({0x620EDFCE, 0xA20FB3B9, 0}, b)) {
    try {
      Reader r(b);
      Hdr h = readHdr(r); skipMarker(r, h);
      w.view.centreX = r.f32(); w.view.centreZ = r.f32();
      w.view.zoom = r.s32(); w.view.rotation = r.s32();
      w.view.valid = true;
    } catch (const FormatError&) { warn("view record unreadable"); }
  }

  // Packed segment.
  const uint8_t* sp; size_t sn; uint32_t decl;
  if (!ixf.get({0x035F62A4, 0x035F62A4, 1}, sp, sn, &decl)) throw FormatError("missing packed segment");
  Reader r(sp, sn);
  uint32_t codec = r.u32();
  if (codec != 0x65 && codec != 0x67) throw FormatError("unsupported segment codec");
  uint32_t vl = r.u32(); r.skip(vl);
  uint32_t packedSize = r.u32(), rawSize = r.u32();
  if (packedSize < 4 || packedSize - 0 > r.remaining() + 0) { if (packedSize > r.remaining()) throw FormatError("packed segment truncated"); }
  r.skip(4);  // blockSize
  Bytes raw = refpackDecode(r.ptr(), packedSize - 4);
  if (raw.size() != rawSize) throw FormatError("packed segment size mismatch");
  InnerDb db;
  db.parse(std::move(raw));

  const uint8_t* p; size_t n;
  if (!db.find({0x206C6E7C, 0x21737DE5, 0}, p, n)) throw FormatError("missing terrain record");
  parseTerrain(p, n, w);
  try { if (db.find({0x406B1196, 0x80AB8AB0, 0}, p, n)) parseFlora(p, n, w); }
  catch (const FormatError& e) { warn(std::string("flora: ") + e.what()); }
  try { if (db.find({0x206C6E7C, 0x409FF3BA, 0}, p, n)) parseZone(p, n, w); }
  catch (const FormatError& e) { warn(std::string("zone: ") + e.what()); }
  try {
    const uint8_t *cp, *ep; size_t cn, en;
    if (db.find({0x20631788, 0xA0AB89F0, 0}, cp, cn) && db.find({0xE0FAADC7, 0xA0AB89F0, 1}, ep, en))
      parseOccupants(cp, cn, ep, en, w);
  } catch (const FormatError& e) { warn(std::string("occupants: ") + e.what()); }
  try {
    if (db.find({0x206C6E7C, 0x2147C2DD, 0}, p, n)) {
      Reader hr(p, n);
      Hdr h = readHdr(hr);
      if (h.version == 4) {
        uint32_t cs = hr.u32(), cp2 = hr.u32(), cu = hr.u32(), fmt = hr.u32();
        if (fmt == 1) {
          if (cs && db.find({0x206C6E7C, 0x2147C2DD, 1}, p, n)) parseNetList(p, n, cs, w.surface);
          if (cp2 && db.find({0x206C6E7C, 0x2147C2DD, 2}, p, n)) parseNetList(p, n, cp2, w.pipes);
          if (cu && db.find({0x206C6E7C, 0x2147C2DD, 3}, p, n)) parseNetList(p, n, cu, w.subway);
        }
      }
    }
  } catch (const FormatError& e) { warn(std::string("networks: ") + e.what()); }
  return w;
}

}  // namespace m3k
