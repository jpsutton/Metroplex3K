#include "core/automata.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <sstream>

namespace m3k {

using namespace automata_ids;

// ================================================================ record reader (binary + text)
namespace {
class RecReader {
 public:
  // Header: line terminated by CR. "BIN" => binary.
  explicit RecReader(const Bytes& b, bool magic4 = false) : b_(b) {
    if (magic4) {
      if (b.size() < 4) throw FormatError("short");
      std::string m(reinterpret_cast<const char*>(b.data()), 4);
      if (m == "BIN!") bin_ = true;
      else if (m == "TXT!") bin_ = false;
      else throw FormatError("bad magic");
      pos_ = 4;
    } else {
      size_t e = 0;
      while (e < b.size() && b[e] != 0x0D && e < 32) e++;
      if (e >= b.size() || b[e] != 0x0D) throw FormatError("no header line");
      std::string h(reinterpret_cast<const char*>(b.data()), e);
      bin_ = h == "BIN";
      pos_ = e + 1;
    }
    if (!bin_) tokenise();
  }
  bool binary() const { return bin_; }
  // Nested sub-object header.
  void subHeader() {
    if (bin_) {
      need(4);
      if (std::string(reinterpret_cast<const char*>(&b_[pos_]), 3) != "BIN" || b_[pos_ + 3] != 0x0D) throw FormatError("bad sub header");
      pos_ += 4;
    } else if (tp_ < toks_.size() && !isNum(toks_[tp_])) tp_++;
  }
  uint8_t u8() { if (bin_) { need(1); return b_[pos_++]; } return uint8_t(tokInt()); }
  int8_t s8() { return int8_t(u8()); }
  uint32_t u32() { if (bin_) { need(4); uint32_t v = rd32(&b_[pos_]); pos_ += 4; return v; } return uint32_t(tokInt()); }
  int32_t s32() { return int32_t(u32()); }
  float f32() {
    if (bin_) { uint32_t v = u32(); float f; __builtin_memcpy(&f, &v, 4); return f; }
    if (tp_ >= toks_.size()) throw FormatError("eof");
    return float(std::atof(toks_[tp_++].c_str()));
  }
  uint16_t u16() { if (bin_) { need(2); uint16_t v = rd16(&b_[pos_]); pos_ += 2; return v; } return uint16_t(tokInt()); }
  ResKey key() { ResKey k; k.type = u32(); k.group = u32(); k.instance = u32(); return k; }
  std::string str() {
    if (bin_) {
      uint32_t n = u32();
      need(n);
      std::string s(reinterpret_cast<const char*>(&b_[pos_]), n);
      pos_ += n;
      return s;
    }
    if (tp_ >= toks_.size()) throw FormatError("eof");
    return toks_[tp_++];
  }
  size_t remaining() const { return bin_ ? b_.size() - pos_ : toks_.size() - tp_; }
  void need(size_t n) const { if (pos_ + n > b_.size()) throw FormatError("short record"); }
 private:
  static bool isNum(const std::string& s) {
    if (s.empty()) return false;
    size_t i = s[0] == '-' ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); i++) if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
  }
  int64_t tokInt() {
    if (tp_ >= toks_.size()) throw FormatError("eof");
    return std::strtoll(toks_[tp_++].c_str(), nullptr, 10);
  }
  void tokenise() {
    std::string s(reinterpret_cast<const char*>(b_.data()) + pos_, b_.size() - pos_);
    std::istringstream in(s);
    std::string t;
    while (in >> t) toks_.push_back(t);
  }
  const Bytes& b_;
  size_t pos_ = 0;
  bool bin_ = true;
  std::vector<std::string> toks_;
  size_t tp_ = 0;
};

template <class F> bool guarded(F&& f) {
  try { return f(); } catch (const FormatError&) { return false; } catch (const std::exception&) { return false; }
}

Pt readPt(RecReader& r) { Pt p; p.a = r.s32(); p.h = r.s32(); p.b = r.s32(); return p; }
}  // namespace

// ================================================================ parsers
bool parseVAttrib(const Bytes& b, VAttrib& o) {
  return guarded([&] {
    RecReader r(b);
    o.emergency = r.u8() == 1;
    o.motionSound = r.s32(); o.paramB = r.s32(); o.sirenSound = r.s32(); o.length = r.s32();
    o.anim = r.key(); o.script = r.key(); o.maxSpeedRaw = r.u32();
    return true;
  });
}

bool parseVclProd(const Bytes& b, VclProd& o) {
  return guarded([&] {
    RecReader r(b);
    o.rate = r.f32();
    uint32_t n = r.u32();
    if (n > 100000) return false;
    o.entries.clear();
    for (uint32_t i = 0; i < n; i++) { uint32_t w = r.u32(); o.entries.push_back({w, r.key()}); }
    return true;
  });
}

bool parsePAttrib(const Bytes& b, PAttrib& o) {
  return guarded([&] { RecReader r(b); o.anim = r.key(); o.script = r.key(); return true; });
}

bool PplMode::containsDate(int m, int d) const {
  if (!dated) return false;
  int v = m * 100 + d, s = startMonth * 100 + startDay, e = endMonth * 100 + endDay;
  return s <= e ? (v >= s && v <= e) : (v >= s || v <= e);
}

bool parsePplProd(const Bytes& b, PplProd& o) {
  return guarded([&] {
    RecReader r(b, true);
    uint32_t nc = r.u32();
    if (nc > 10000) return false;
    o.countries.clear();
    for (uint32_t c = 0; c < nc; c++) {
      PplCountry pc;
      pc.name = r.str();
      uint32_t nm = r.u32();
      if (nm > 10000) return false;
      int64_t running = -1;  // undated threshold accumulator
      bool absolute = false;
      for (uint32_t m = 0; m < nm; m++) {
        PplMode md;
        md.dateCode = r.u32();
        uint32_t n = r.u32();
        md.rate = r.f32();
        md.radius = r.u32();
        if (n > 100000) return false;
        if (m == 0) absolute = md.dateCode >= 1010101;
        if (absolute) {
          md.dated = true;
          md.startMonth = int((md.dateCode >> 24) & 0xFF); md.startDay = int((md.dateCode >> 16) & 0xFF);
          md.endMonth = int((md.dateCode >> 8) & 0xFF); md.endDay = int(md.dateCode & 0xFF);
        } else {
          running += int64_t(md.dateCode);
          md.cumThreshold = running;
        }
        int64_t cum = -1;
        for (uint32_t i = 0; i < n; i++) {
          uint32_t w = r.u32();
          md.entries.push_back({w, r.key()});
          cum += int64_t(w);
          md.cum.push_back(cum);
        }
        pc.modes.push_back(std::move(md));
      }
      o.countries.push_back(std::move(pc));
    }
    return true;
  });
}

const AnimSeq* AnSprAtt::find(uint32_t id) const {
  for (auto& s : seqs) if (s.id == id) return &s;
  return nullptr;
}

bool parseAnSprAtt(const Bytes& b, AnSprAtt& o) {
  return guarded([&] {
    RecReader r(b);
    o.sprite = r.key();
    uint32_t n = r.u32();
    if (n > 64) return false;
    o.seqs.clear();
    for (uint32_t i = 0; i < n; i++) {
      AnimSeq s; s.id = r.u32();
      uint8_t len = r.u8();
      for (int k = 0; k < len; k++) s.frames.push_back(r.u16());
      o.seqs.push_back(std::move(s));
    }
    return true;
  });
}

bool parsePPath(const Bytes& b, PPath& o) {
  return guarded([&] {
    RecReader r(b);
    int32_t n = r.s32();
    if (n < 1 || n > 4096) return false;
    o.pts.clear();
    for (int i = 0; i < n; i++) o.pts.push_back(readPt(r));
    o.entryHeading8 = int(r.u32() / 4) & 7;
    o.exitHeading8 = int(r.u32() / 4) & 7;
    o.cum.assign(size_t(n), 0);
    for (int i = 1; i < n; i++) {
      double da = o.pts[size_t(i)].a - o.pts[size_t(i - 1)].a, db = o.pts[size_t(i)].b - o.pts[size_t(i - 1)].b;
      double dh = 0.204125 * (o.pts[size_t(i)].h - o.pts[size_t(i - 1)].h);
      o.cum[size_t(i)] = o.cum[size_t(i - 1)] + int(std::sqrt(da * da + db * db + dh * dh));
    }
    return true;
  });
}

int heading32(double da, double db) {
  double a = std::atan2(db, da);
  if (a < 0) a += 2 * M_PI;
  int v = int(std::lround(a * 32.0 / (2 * M_PI)));
  return v >= 32 ? 0 : v;
}

bool parseVPath(const Bytes& b, VPath& o) {
  return guarded([&] {
    RecReader r(b);
    int32_t n = r.s32();
    if (n < 2 || n > 4096) return false;
    o.pts.clear();
    for (int i = 0; i < n; i++) o.pts.push_back(readPt(r));
    o.entryHeading = r.u8(); o.exitHeading = r.u8();
    o.lane = r.s32() + 1;
    o.cum.assign(size_t(n), 0);
    for (int i = 1; i < n; i++) {
      double da = o.pts[size_t(i)].a - o.pts[size_t(i - 1)].a, db = o.pts[size_t(i)].b - o.pts[size_t(i - 1)].b;
      double dh = 0.204125 * (o.pts[size_t(i)].h - o.pts[size_t(i - 1)].h);
      o.cum[size_t(i)] = o.cum[size_t(i - 1)] + int(std::sqrt(da * da + db * db + dh * dh));  // trunc toward zero
    }
    double da = o.pts.back().a - o.pts.front().a, db = o.pts.back().b - o.pts.front().b;
    double hd = std::sqrt(da * da + db * db);
    o.grade = hd > 0 ? (o.pts.back().h - o.pts.front().h) / hd : 0;
    o.heading0 = heading32(o.pts[1].a - o.pts[0].a, o.pts[1].b - o.pts[0].b);
    return true;
  });
}

const VehLane* TrBlk::vehLane(int id) const { for (auto& l : veh) if (int(l.id) == id) return &l; return nullptr; }
const PedLane* TrBlk::pedLane(int id) const { for (auto& l : ped) if (int(l.id) == id) return &l; return nullptr; }

static void readConns(RecReader& r, std::vector<Conn>& v) {
  uint32_t m = r.u32();
  if (m > 4096) throw FormatError("conns");
  for (uint32_t i = 0; i < m; i++) {
    Conn c; c.next = r.s8(); c.heading = r.u8(); c.path = r.u32(); c.value = r.u32();
    v.push_back(c);
  }
}

bool parseTrBlk(const Bytes& b, TrBlk& o) {
  return guarded([&] {
    RecReader r(b);
    for (int i = 0; i < 4; i++) o.box[i] = r.s32() / 256;
    o.flag = r.u8() == 1;
    r.subHeader();
    uint32_t n = r.u32();
    if (n > 4096) return false;
    o.ped.clear(); o.veh.clear();
    for (uint32_t i = 0; i < n; i++) { PedLane l; l.id = r.u32(); l.pos = readPt(r); readConns(r, l.conns); o.ped.push_back(std::move(l)); }
    r.subHeader();
    n = r.u32();
    if (n > 4096) return false;
    for (uint32_t i = 0; i < n; i++) {
      VehLane l; l.id = r.u32(); l.type = r.s8(); l.pos = readPt(r); readConns(r, l.conns); o.veh.push_back(std::move(l));
    }
    return true;
  });
}

bool parseSpriteInst(const Bytes& b, SpriteInst& o) {
  return guarded([&] {
    RecReader r(b);
    if (!r.binary()) return false;
    r.s32();  // version
    r.u32();  // class id
    o.zoomCount = r.u8(); o.rotCount = r.u8(); o.zoomSetCount = r.u8(); o.animSetCount = r.u8(); o.animFrames = r.u8();
    r.u8();   // reg rects
    o.layerFlags = r.u32();
    uint16_t n = r.u16();
    o.frames.clear();
    for (int i = 0; i < n; i++) {
      SpriteFrame f; f.index = r.u16(); f.category = r.u32(); f.spriteId = r.u32(); f.flags = r.s8();
      f.ox = int16_t(r.u16()); f.oy = int16_t(r.u16());
      o.frames.push_back(f);
    }
    return true;
  });
}

static std::vector<std::string> splitLines(const std::string& t) {
  std::vector<std::string> out;
  std::string cur;
  for (size_t i = 0; i < t.size(); i++) {
    char c = t[i];
    if (c == '\r') { out.push_back(cur); cur.clear(); if (i + 1 < t.size() && t[i + 1] == '\n') i++; }
    else if (c == '\n') { out.push_back(cur); cur.clear(); }
    else cur += c;
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool parseScript(const std::string& text, Script& o) {
  auto lines = splitLines(text);
  if (lines.empty()) return false;
  size_t li = 0;
  auto num = [&](const std::string& s) { return std::strtoll(s.c_str(), nullptr, 10); };
  int16_t S = int16_t(num(lines[li++]));
  if (S < 0) return false;
  o.steps.clear();
  for (int i = 0; i < S; i++) {
    if (li >= lines.size()) return false;
    const std::string& l = lines[li++];
    size_t tab = l.find_first_of("\t ");
    if (tab == std::string::npos) return false;
    ScriptStep st;
    st.op = uint16_t(num(l.substr(0, tab)));
    int ac = int(uint8_t(num(l.substr(tab + 1))));
    for (int a = 0; a < ac; a++) {
      if (li >= lines.size()) return false;
      st.args.push_back(int32_t(uint32_t(uint64_t(num(lines[li++])) & 0xFFFFFFFFu)));
    }
    if (li < lines.size()) li++;  // separator line
    o.steps.push_back(std::move(st));
  }
  return true;
}

static std::vector<int64_t> allInts(const std::string& t) {
  std::vector<int64_t> v;
  std::istringstream in(t);
  std::string s;
  while (in >> s) v.push_back(std::strtoll(s.c_str(), nullptr, 10));
  return v;
}

bool parseTrnProd(const std::string& text, TrnProd& o) {
  auto v = allInts(text);
  size_t i = 0;
  if (v.size() < 3) return false;
  o.minUs = v[i++] * 1000000; o.maxUs = v[i++] * 1000000;
  int64_t nc = v[i++];
  o.consists.clear();
  for (int64_t c = 0; c < nc; c++) {
    if (i >= v.size()) return false;
    Consist cs; cs.loco = int(v[i++]);
    for (int l = 0; l < 4; l++) {
      if (i >= v.size()) return false;
      int64_t n = v[i++];
      for (int64_t k = 0; k < n; k++) { if (i >= v.size()) return false; cs.list[l].push_back(int(v[i++])); }
    }
    o.consists.push_back(std::move(cs));
  }
  return true;
}

bool parseAirTrMgr(const std::string& text, AirTrMgr& o) {
  auto v = allInts(text);
  if (v.size() < 2) return false;
  o.maxActive = int(v[0]);
  int64_t n = v[1];
  o.models.clear();
  for (int64_t i = 0; i < n; i++) { if (size_t(2 + i) >= v.size()) return false; o.models.push_back(int(v[size_t(2 + i)])); }
  return true;
}

StreetSimTunables StreetSimTunables::fromIni(const IniFile& ini) {
  StreetSimTunables t;
  std::string v;
  if (ini.get("MiscStrtSimTunables", "MaxMarinaToBoatDistance", v)) t.maxMarinaToBoatDistance = int(parseIdNumber(v));
  auto rd = [&](const char* sec, std::vector<std::pair<std::string, int>>& out) {
    for (auto& kv : ini.section(sec)) out.push_back({kv.first, int(parseIdNumber(kv.second))});
  };
  rd("KeyVehiclesForLowMemoryMachines", t.keyVehiclesLow);
  rd("KeyVehiclesForHighMemoryMachines", t.keyVehiclesHigh);
  rd("EmergencyVehiclesForLowMemoryMachines", t.emergencyLow);
  rd("EmergencyVehiclesForHighMemoryMachines", t.emergencyHigh);
  return t;
}

Pt polylinePoint(const std::vector<Pt>& pts, const std::vector<int>& cum, double dist, int* heading) {
  Pt out;
  if (pts.empty()) return out;
  if (pts.size() == 1 || cum.size() != pts.size()) { if (heading) *heading = 0; return pts[0]; }
  if (dist < 0) dist = 0;
  size_t i = 1;
  while (i + 1 < pts.size() && double(cum[i]) < dist) i++;
  const Pt& A = pts[i - 1];
  const Pt& B = pts[i];
  double seg = double(cum[i] - cum[i - 1]);
  double t = seg > 0 ? (dist - double(cum[i - 1])) / seg : 1.0;
  t = std::clamp(t, 0.0, 1.0);
  out.a = int(std::lround(A.a + (B.a - A.a) * t));
  out.b = int(std::lround(A.b + (B.b - A.b) * t));
  out.h = int(std::lround(A.h + (B.h - A.h) * t));
  if (heading) *heading = heading32(B.a - A.a, B.b - A.b);
  return out;
}

Pt vpathPoint(const VPath& p, double dist, int* heading) { return polylinePoint(p.pts, p.cum, dist, heading); }

// ================================================================ data store
void AutomataData::addRecord(const ResKey& k, const Bytes& b) {
  bool ok = true;
  uint32_t inst = k.instance;
  std::string text(b.begin(), b.end());
  switch (k.type) {
    case kVAttribType: { VAttrib v; if ((ok = parseVAttrib(b, v))) vattrib[inst] = v; break; }
    case kVclProdType: { VclProd v; if ((ok = parseVclProd(b, v))) vclprod[inst] = v; break; }
    case kPAttribType: { PAttrib v; if ((ok = parsePAttrib(b, v))) pattrib[inst] = v; break; }
    case kPplProdType: {
      PplProd v;
      if ((ok = parsePplProd(b, v))) {
        auto it = pplprod.find(inst);
        if (it == pplprod.end()) pplprod[inst] = std::move(v);
        else for (auto& c : v.countries) it->second.countries.push_back(std::move(c));  // merge holiday blocks
      }
      break;
    }
    case kAnSprAttType: { AnSprAtt v; if ((ok = parseAnSprAtt(b, v))) ansprat[inst] = v; break; }
    case kPPathType: { PPath v; if ((ok = parsePPath(b, v))) ppath[inst] = v; break; }
    case kVPathType: { VPath v; if ((ok = parseVPath(b, v))) vpath[inst] = v; break; }
    case kTrBlkType: { TrBlk v; if ((ok = parseTrBlk(b, v))) trblk[inst] = v; break; }
    case kScriptType: { Script v; if ((ok = parseScript(text, v))) scripts[inst] = v; break; }
    case kTrnProdType: { TrnProd v; if ((ok = parseTrnProd(text, v))) trnprod[inst] = v; break; }
    case kAirTrMgrType: { AirTrMgr v; if ((ok = parseAirTrMgr(text, v))) airtrmgr[inst] = v; break; }
    case kSpriteInstType: { SpriteInst v; if ((ok = parseSpriteInst(b, v))) sprites[k] = std::move(v); break; }
    default: return;
  }
  if (!ok) loadErrors++;
}

int AutomataData::loadIxf(const IxfFile& f) {
  int n = 0;
  for (auto& e : f.entries()) {
    switch (e.key.type) {
      case kVAttribType: case kVclProdType: case kPAttribType: case kPplProdType: case kAnSprAttType:
      case kPPathType: case kVPathType: case kTrBlkType: case kScriptType: case kTrnProdType:
      case kAirTrMgrType: case kSpriteInstType: {
        Bytes b;
        if (f.getBytes(e.key, b)) { addRecord(e.key, b); n++; }
        break;
      }
      default: break;
    }
  }
  return n;
}

const TrBlk* AutomataData::trblkFor(uint32_t piece, int rot) const {
  auto it = trblk.find((piece << 8) | uint32_t(rot & 3));
  if (it != trblk.end()) return &it->second;
  it = trblk.find(kDefaultTrBlk);
  return it == trblk.end() ? nullptr : &it->second;
}
const PPath* AutomataData::ppathFor(uint32_t piece, int rot) const {
  auto it = ppath.find((piece << 8) | uint32_t(rot & 3));
  return it == ppath.end() ? nullptr : &it->second;
}

const PplMode* AutomataData::pickPplMode(const PplProd& t, const std::string& country, bool holidays, int month, int day,
                                         uint32_t draw1000) const {
  auto lc = [](std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; };
  const PplCountry* base = nullptr;
  const PplCountry* hol = nullptr;
  for (const char* want : {country.c_str(), "def"}) {
    for (auto& c : t.countries)
      if (lc(c.name) == lc(want)) { base = &c; break; }
    if (base) break;
  }
  if (holidays) {
    std::string name = lc(base ? base->name : "def") + ":holidays";
    for (auto& c : t.countries) if (lc(c.name) == name) hol = &c;
    if (!hol) for (auto& c : t.countries) if (lc(c.name) == "def:holidays") hol = &c;
  }
  for (const PplCountry* c : {hol, base}) {
    if (!c) continue;
    for (auto& m : c->modes) if (m.dated && m.containsDate(month, day)) return &m;
    if (!c->modes.empty() && !c->modes[0].dated) {
      for (auto& m : c->modes) if (m.cumThreshold >= int64_t(draw1000)) return &m;
      return &c->modes.back();
    }
  }
  return nullptr;
}

int vclProdPick(const VclProd& t, uint32_t r) {
  uint64_t cum = 0;
  for (size_t i = 0; i < t.entries.size(); i++) {
    cum += t.entries[i].first;
    if (cum > r) return int(i);
  }
  return t.entries.empty() ? -1 : int(t.entries.size()) - 1;
}

int pplModePick(const PplMode& m, uint32_t r) {
  for (size_t i = 0; i < m.cum.size(); i++) if (m.cum[i] >= int64_t(r)) return int(i);
  return m.cum.empty() ? -1 : int(m.cum.size()) - 1;
}


// ================================================================ street sim
namespace {

struct SimRng {
  uint32_t s;
  explicit SimRng(uint32_t seed = 1) : s(seed ? seed : 1) {}
  uint32_t next() { s = s * 1103515245u + 12345u; return (s >> 16) & 0x7FFF; }
  uint32_t big() { return (next() << 15) ^ next() ^ (next() << 8); }
  uint32_t below(uint32_t n) { return n ? big() % n : 0; }
  float f01() { return float(next()) / 32768.f; }
  // (value, jitter) draw: [v-j, v+j)
  int jittered(int v, int j) { return j <= 0 ? v : v - j + int(below(uint32_t(2 * j))); }
};

struct Rect {
  int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
  bool valid() const { return x1 >= x0 && y1 >= y0; }
  bool contains(int x, int y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
  bool operator==(const Rect& o) const { return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1; }
};

struct Frame { const Script* s = nullptr; int step = -1; int limit = 1; int repeat = 0; };
struct TrailPt { double t, x, y, h; int heading; };
enum class Beh { None, Walk, Drive, Anim, Follow, Fly, Wander };
enum class FlyEnd { Remove, Land };

const int kEdgeDx[4] = {0, 1, 0, -1};  // heading/8 -> edge (north, east, south, west) [OPEN convention]
const int kEdgeDy[4] = {-1, 0, 1, 0};

struct Auto {
  int id = 0;
  AutomatonKind kind = AutomatonKind::Vehicle;
  uint32_t attribInst = 0;
  const VAttrib* va = nullptr;
  const PAttrib* pa = nullptr;
  const AnSprAtt* anim = nullptr;
  const SpriteInst* spr = nullptr;
  std::vector<Frame> stack;
  Beh beh = Beh::None;
  int piecesLeft = 0;
  double speedRate = 0;   // ped walk speed (units/us)
  int loopsLeft = 0, loopsDone = 0;
  int64_t animAcc = 0, tickAcc = 0;
  uint32_t animSeq = 20;
  int animIdx = 0;
  // tile / path state
  int tx = 0, ty = 0;
  PieceRef piece;
  const TrBlk* blk = nullptr;
  const VPath* vpath = nullptr;
  uint32_t vpathInst = 0;
  Conn conn;
  double dist = 0, prevDist = 0;
  const PPath* ppath = nullptr;
  bool rev = false, graph = false;
  int pedNode = -1;
  // free movement (boats, aircraft)
  double fx = 0, fy = 0, fh = 0;
  std::vector<std::pair<double, double>> wps;
  size_t wp = 0;
  double dirX = 1, dirY = 0;
  int64_t nextTurnUs = 0;
  FlyEnd flyEnd = FlyEnd::Remove;
  double climbTiles = 0, startAlt = 0, endAlt = 0, totalFly = 0, flown = 0;
  int owner = 0;        // spawner id (port / airport)
  bool heli = false;
  // output pose
  double x = 0, y = 0, h = 0;
  int heading = 0;
  // followers
  int leader = 0;
  double followOffset = 0, travelled = 0;
  std::deque<TrailPt> trail;
  bool visible = true;
  bool alive = true;
  double length = 40;
  int pieces = 0, updates = 0;
  bool roadVehicle = false;
};

struct Timer { int64_t acc = 0, interval = 0; };

struct PedSource {
  int id = 0, x = 0, y = 0;
  ResKey key;
  const PplProd* table = nullptr;
  const PplMode* mode = nullptr;
  int cap = 0, radius = 8, live = 0;
  int endMonth = 0, endDay = 0;
  bool global = false;
  int64_t refillAcc = 0;
};

struct Spawner {
  int id = 0, x = 0, y = 0;
  bool ready = false;
  Timer t;
  int live = 0;
  int arrived = 0;                 // airports: aircraft ready to leave
  int kind = 0;                    // 0 seaport 1 airport 2 marina 3 rail station 4 subway station
  bool dirKnown = false;
  int dirX = 0, dirY = 0;
  std::vector<std::pair<double, double>> route;
};

struct BuildingSrc { int id = 0, x = 0, y = 0; ResKey ped, veh; int pedSrc = 0; };

struct MixCell { double rate = 0; std::map<uint32_t, double> w; std::vector<uint32_t> tables; };

}  // namespace

struct StreetSim::Impl {
  const AutomataData& D;
  AutomataHost& host;
  Config cfg;
  SimRng rng;
  ViewState view;
  Rect rect;
  bool active = false;
  int zoomOk = 0;
  std::vector<std::unique_ptr<Auto>> autos;
  std::unordered_map<int, Auto*> byId;
  int nextId = 1, nextSrc = 1;
  int roadCount = 0, pedCount = 0;
  int limitL = 0, avgA = 0, trafficLevel = 0;
  int eventMode_ = 0;
  int64_t eventAcc = 0;
  int eventCounter = 0;
  std::vector<int> eventGrid;  // 70x70
  std::vector<SoundEvent> sounds;
  std::vector<MixCell> mix;
  int cellsPerRow = 1, cellsPerCol = 1;
  std::vector<BuildingSrc> buildings;
  std::vector<PedSource> pedSources;
  std::vector<Spawner> spawners;
  std::vector<std::pair<int, int>> helipads;
  Timer heliTimer;
  bool globalPedSrc = false;

  Impl(const AutomataData& d, AutomataHost& h, const Config& c) : D(d), host(h), cfg(c), rng(c.seed) {
    cellsPerRow = std::max(1, (host.mapWidth() + 31) / 32);
    cellsPerCol = std::max(1, (host.mapHeight() + 31) / 32);
    mix.resize(size_t(cellsPerRow) * cellsPerCol);
    for (auto& m : mix) resetCell(m);
    heliTimer.interval = 1000000 + int64_t(rng.below(2000000));
  }

  // ---------------------------------------------------------- mix tables
  void mergeTable(MixCell& c, uint32_t inst) {
    auto it = D.vclprod.find(inst);
    if (it == D.vclprod.end()) return;
    const VclProd& t = it->second;
    double scale = c.rate != 0 ? double(t.rate) / c.rate : 1.0;
    for (auto& e : t.entries) c.w[e.second.instance] += double(e.first) * scale;
    c.rate += double(t.rate);
    double sum = 0;
    for (auto& kv : c.w) sum += kv.second;
    int guard = 0;
    while (sum > 0 && std::fabs(sum - 1e7) > 1e6 && guard++ < 8) {
      double f = 1e7 / sum;
      sum = 0;
      for (auto& kv : c.w) { kv.second *= f; sum += kv.second; }
    }
  }
  void resetCell(MixCell& c) { c = MixCell(); mergeTable(c, 1); }
  MixCell& cellAt(int x, int y) {
    int cx = std::clamp(x / 32, 0, cellsPerRow - 1), cy = std::clamp(y / 32, 0, cellsPerCol - 1);
    return mix[size_t(cy) * cellsPerRow + cx];
  }
  uint32_t drawModel(int x, int y) {
    if (eventMode_ == 2) {
      const auto& lst = cfg.highMemory ? D.tunables.keyVehiclesHigh : D.tunables.keyVehiclesLow;
      if (!lst.empty()) return uint32_t(lst[rng.below(uint32_t(lst.size()))].second);
    }
    MixCell& c = cellAt(x, y);
    double sum = 0;
    for (auto& kv : c.w) sum += kv.second;
    if (sum <= 0) return 0;
    double r = double(rng.below(uint32_t(std::max(1.0, std::floor(sum))))) ;
    double run = 0;
    for (auto& kv : c.w) { run += kv.second; if (run > r) return kv.first; }
    return c.w.rbegin()->first;
  }

  // ---------------------------------------------------------- automaton bookkeeping
  Auto* find(int id) { auto it = byId.find(id); return it == byId.end() ? nullptr : it->second; }

  void soundEvent(SoundEvent::Type t, int sid, const Auto& a, int variant = 0) {
    if (!sid) return;
    SoundEvent e; e.type = t; e.soundId = sid; e.automatonId = a.id; e.variant = variant; e.tileX = a.x; e.tileY = a.y;
    sounds.push_back(e);
  }

  void setPose(Auto& a, const Pt& p, int heading32v) {
    a.x = double(a.tx) + p.a / 256.0;
    a.y = double(a.ty) + p.b / 256.0;
    a.h = p.h;
    a.heading = heading32v;
  }

  Auto* create(AutomatonKind k) {
    auto u = std::make_unique<Auto>();
    u->id = nextId++;
    u->kind = k;
    Auto* p = u.get();
    autos.push_back(std::move(u));
    byId[p->id] = p;
    return p;
  }

  void kill(Auto* a) {
    if (!a->alive) return;
    a->alive = false;
    if (a->va) {
      soundEvent(SoundEvent::Stop, a->va->motionSound, *a);
      soundEvent(SoundEvent::Stop, a->va->sirenSound, *a);
    }
    if (a->roadVehicle) roadCount--;
    if (a->kind == AutomatonKind::Pedestrian) {
      pedCount--;
      for (auto& s : pedSources) if (s.id == a->owner) s.live--;
    }
    if (a->kind == AutomatonKind::Boat || a->kind == AutomatonKind::Aircraft)
      for (auto& s : spawners) if (s.id == a->owner) s.live--;
  }

  void sweep() {
    bool any = false;
    for (auto& u : autos) if (!u->alive) { any = true; break; }
    if (!any) return;
    for (auto& u : autos) if (!u->alive) byId.erase(u->id);
    autos.erase(std::remove_if(autos.begin(), autos.end(), [](const std::unique_ptr<Auto>& u) { return !u->alive; }), autos.end());
  }

  // ---------------------------------------------------------- script VM
  bool startScript(Auto& a, const ResKey& key) {
    auto it = D.scripts.find(key.instance);
    if (key.instance == 0 || it == D.scripts.end()) return false;
    Frame f; f.s = &it->second; f.limit = 1;
    a.stack.clear();
    a.stack.push_back(f);
    return advanceScript(a);
  }

  void setSeq(Auto& a, uint32_t seq) { a.animSeq = seq; a.animIdx = 0; a.tickAcc = 0; }

  // Move to the next runnable step. False when the stack is empty / failed (automaton must go).
  bool advanceScript(Auto& a) {
    for (int guard = 0; guard < 4096; guard++) {
      if (a.stack.empty()) return false;
      Frame& f = a.stack.back();
      f.step++;
      if (f.step >= int(f.s->steps.size())) {
        f.repeat++;
        if (f.repeat >= f.limit) { a.stack.pop_back(); continue; }  // done: caller advances
        f.step = 0;
        if (f.s->steps.empty()) { a.stack.pop_back(); continue; }
      }
      const ScriptStep& st = f.s->steps[size_t(f.step)];
      const auto& g = st.args;
      auto arg = [&](size_t i) { return i < g.size() ? g[i] : 0; };
      bool ped = a.kind == AutomatonKind::Pedestrian;
      switch (st.op) {
        case 0:
          if (ped) {
            a.beh = Beh::Walk; a.piecesLeft = rng.jittered(arg(0), arg(1));
            setSeq(a, uint32_t(arg(2)));
            a.speedRate = double(uint32_t(rng.jittered(arg(3), arg(4)))) * automata_ids::kSpeedFactor;
          } else {
            a.beh = Beh::Drive; a.piecesLeft = rng.jittered(arg(0), arg(1));
            setSeq(a, uint32_t(arg(2)));
          }
          return true;
        case 1:
          a.beh = Beh::Anim; setSeq(a, uint32_t(arg(0)));
          a.loopsLeft = std::max(1, rng.jittered(arg(1), arg(2))); a.loopsDone = 0; a.animAcc = 0;
          return true;
        case 2: {
          auto it = D.scripts.find(uint32_t(arg(2)));
          if (it == D.scripts.end() || uint32_t(arg(0)) != kScriptType) { a.stack.clear(); return false; }  // fail unwinds all
          Frame nf; nf.s = &it->second; nf.limit = rng.jittered(arg(3), arg(4));
          a.stack.push_back(nf);
          continue;
        }
        case 3:
          if (!ped) {
            if (a.animSeq != uint32_t(arg(0))) setSeq(a, uint32_t(arg(0)));
            a.beh = Beh::Follow;
            return true;
          }
          a.stack.clear(); return false;
        default:  // 200 (parade) and unknown ops: never run on ordinary automata
          a.stack.clear(); return false;
      }
    }
    a.stack.clear();
    return false;
  }

  // ---------------------------------------------------------- vehicle lanes
  bool placeVehicleOnTile(Auto& a, int x, int y, NetKind nk, bool randomDist) {
    PieceRef pr;
    if (!host.pieceAt(x, y, nk, pr)) return false;
    const TrBlk* b = D.trblkFor(pr.pieceId, pr.rotation);
    if (!b || b->veh.empty()) return false;
    const VehLane& lane = b->veh[rng.below(uint32_t(b->veh.size()))];
    if (lane.conns.empty()) return false;
    const Conn& c = lane.conns[rng.below(uint32_t(lane.conns.size()))];
    auto pit = D.vpath.find(c.path);
    if (pit == D.vpath.end()) return false;
    a.tx = x; a.ty = y; a.piece = pr; a.blk = b; a.conn = c;
    a.vpath = &pit->second; a.vpathInst = c.path;
    a.dist = randomDist ? rng.f01() * a.vpath->length() : 0;
    a.prevDist = a.dist;
    int hd = 0;
    Pt p = vpathPoint(*a.vpath, a.dist, &hd);
    setPose(a, p, hd);
    return true;
  }

  Auto* spawnVehicle(uint32_t inst, int x, int y, bool randomDist, bool counted = true) {
    auto vit = D.vattrib.find(inst);
    if (vit == D.vattrib.end()) return nullptr;
    const VAttrib& v = vit->second;
    bool rail = v.script.instance == 2001 && false;
    (void)rail;
    auto ait = D.ansprat.find(v.anim.instance);
    Auto* a = create(AutomatonKind::Vehicle);
    a->attribInst = inst; a->va = &v; a->length = v.length > 0 ? v.length : 40;
    a->roadVehicle = counted;
    if (ait != D.ansprat.end()) { a->anim = &ait->second; auto sp = D.sprites.find(ait->second.sprite); if (sp != D.sprites.end()) a->spr = &sp->second; }
    if (!placeVehicleOnTile(*a, x, y, NetKind::Road, randomDist) || !startScript(*a, v.script)) {
      a->roadVehicle = false; a->alive = false;
      return nullptr;
    }
    if (counted) roadCount++;
    soundEvent(SoundEvent::Start, v.motionSound, *a);
    soundEvent(SoundEvent::Siren, v.sirenSound, *a, 0);
    return a;
  }

  // Leave the current piece through the edge nearest the end of the path. False = fail.
  bool vehicleNextPath(Auto& a, double overflow) {
    if (a.conn.next >= 0) {
      const VehLane* l = a.blk->vehLane(a.conn.next);
      if (!l || l->conns.empty()) return false;
      const Conn& c = l->conns[rng.below(uint32_t(l->conns.size()))];
      auto pit = D.vpath.find(c.path);
      if (pit == D.vpath.end()) return false;
      a.conn = c; a.vpath = &pit->second; a.vpathInst = c.path; a.dist = overflow;
      return true;
    }
    // exit via the tile edge nearest the final point
    const Pt& last = a.vpath->pts.back();
    int dx = 0, dy = 0;
    int dW = last.a, dE = 256 - last.a, dN = last.b, dS = 256 - last.b;
    int m = std::min(std::min(dW, dE), std::min(dN, dS));
    if (m == dW) dx = -1; else if (m == dE) dx = 1; else if (m == dN) dy = -1; else dy = 1;
    int nx = a.tx + dx, ny = a.ty + dy;
    if (nx < 0 || ny < 0 || nx >= host.mapWidth() || ny >= host.mapHeight()) return false;
    if (a.kind != AutomatonKind::Train && !rect.contains(nx, ny)) return false;  // left the active area
    if (a.kind == AutomatonKind::Train && !rect.contains(nx, ny)) return false;
    NetKind nk = a.kind == AutomatonKind::Train ? NetKind::Rail : NetKind::Road;
    PieceRef pr;
    if (!host.pieceAt(nx, ny, nk, pr)) return false;
    const TrBlk* b = D.trblkFor(pr.pieceId, pr.rotation);
    if (!b || b->veh.empty()) return false;
    int ea = last.a - 256 * dx, eb = last.b - 256 * dy;
    const VehLane* best = nullptr;
    double bd = 1e18;
    for (auto& l : b->veh) {
      if (l.conns.empty()) continue;
      double d = std::hypot(double(l.pos.a - ea), double(l.pos.b - eb));
      if (d < bd) { bd = d; best = &l; }
    }
    if (!best || bd > 128) return false;
    const Conn& c = best->conns[rng.below(uint32_t(best->conns.size()))];
    auto pit = D.vpath.find(c.path);
    if (pit == D.vpath.end()) return false;
    a.tx = nx; a.ty = ny; a.piece = pr; a.blk = b; a.conn = c;
    a.vpath = &pit->second; a.vpathInst = c.path; a.dist = overflow;
    a.pieces++;
    if (a.va && a.va->sirenSound && a.pieces % 4 == 0 && rng.below(2) == 0) {
      uint32_t r = rng.below(100);
      int variant = r < 80 ? 0 : (rng.below(2) ? 1 : 2);
      soundEvent(SoundEvent::Siren, a.va->sirenSound, a, variant);
    }
    return true;
  }

  // Returns false on fail; sets done when the Drive count is exhausted.
  bool driveStep(Auto& a, int64_t dtUs, bool& done) {
    double v = std::min(a.va ? a.va->maxSpeed() : 0.0, double(a.conn.value) * automata_ids::kSpeedFactor);
    a.prevDist = a.dist;
    a.dist += v * double(dtUs);
    for (int guard = 0; guard < 64; guard++) {
      double len = double(a.vpath->length());
      if (a.dist < len) break;
      double over = a.dist - len;
      bool leaving = a.conn.next < 0;
      if (!vehicleNextPath(a, over)) return false;
      if (leaving) {
        a.prevDist = 0;
        if (--a.piecesLeft <= 0) { done = true; return true; }
      } else a.prevDist = 0;
    }
    int hd = 0;
    Pt p = vpathPoint(*a.vpath, a.dist, &hd);
    setPose(a, p, hd);
    return true;
  }

  // ---------------------------------------------------------- pedestrian movement
  static void edgeOf(const Pt& pt, int& dx, int& dy) {
    dx = dy = 0;
    int dW = pt.a, dE = 256 - pt.a, dN = pt.b, dS = 256 - pt.b;
    int m = std::min(std::min(dW, dE), std::min(dN, dS));
    if (m == dW) dx = -1; else if (m == dE) dx = 1; else if (m == dN) dy = -1; else dy = 1;
  }

  bool pedPieceAt(int x, int y, PieceRef& pr) {
    if (host.pieceAt(x, y, NetKind::Walk, pr)) return true;
    pr.pieceId = 0x1D; pr.rotation = 0;  // default piece
    return true;
  }

  // Start the pedestrian on a tile. entry (neighbour coordinates) is used when it walked in.
  bool pedEnter(Auto& a, int x, int y, bool haveEntry, Pt entry, bool randomDist) {
    PieceRef pr; pedPieceAt(x, y, pr);
    const TrBlk* b = D.trblkFor(pr.pieceId, pr.rotation);
    a.tx = x; a.ty = y; a.piece = pr; a.blk = b;
    if (b && !b->ped.empty()) {
      a.graph = true;
      const PedLane* best = nullptr;
      if (haveEntry) {
        double bd = 1e18;
        for (auto& l : b->ped) { double d = std::hypot(double(l.pos.a - entry.a), double(l.pos.b - entry.b)); if (d < bd) { bd = d; best = &l; } }
      } else best = &b->ped[rng.below(uint32_t(b->ped.size()))];
      a.pedNode = int(best->id);
      Pt p = best->pos; setPose(a, p, a.heading);
      return pedPickSegment(a);
    }
    a.graph = false;
    const PPath* pp = D.ppathFor(pr.pieceId, pr.rotation);
    if (!pp || pp->pts.size() < 1) return false;
    a.ppath = pp;
    if (haveEntry) {
      double df = std::hypot(double(pp->pts.front().a - entry.a), double(pp->pts.front().b - entry.b));
      double dr = std::hypot(double(pp->pts.back().a - entry.a), double(pp->pts.back().b - entry.b));
      a.rev = dr < df;
      if (std::min(df, dr) > 64) return false;
      a.dist = 0;
    } else {
      a.rev = rng.below(2) == 1;
      a.dist = randomDist ? rng.f01() * pp->length() : 0;
    }
    pedPose(a);
    return true;
  }

  // Graph mode: choose a connection from the current node and start walking its path.
  bool pedPickSegment(Auto& a) {
    const PedLane* l = a.blk->pedLane(a.pedNode);
    if (!l || l->conns.empty()) return false;
    a.conn = l->conns[rng.below(uint32_t(l->conns.size()))];
    if (a.conn.path) {
      auto it = D.ppath.find(a.conn.path);
      if (it == D.ppath.end()) return false;
      a.ppath = &it->second; a.rev = false; a.dist = 0;
    } else { a.ppath = nullptr; a.dist = 0; }
    return true;
  }

  void pedPose(Auto& a) {
    if (!a.ppath) return;
    double d = a.rev ? double(a.ppath->length()) - a.dist : a.dist;
    int hd = 0;
    Pt p = polylinePoint(a.ppath->pts, a.ppath->cum, d, &hd);
    if (a.rev) hd = (hd + 16) & 31;
    setPose(a, p, hd);
  }

  bool pedLeaveTile(Auto& a, int dx, int dy, Pt exitPt) {
    int nx = a.tx + dx, ny = a.ty + dy;
    if (nx < 0 || ny < 0 || nx >= host.mapWidth() || ny >= host.mapHeight() || !rect.contains(nx, ny)) return false;
    Pt e = exitPt; e.a -= 256 * dx; e.b -= 256 * dy;
    a.pieces++;
    return pedEnter(a, nx, ny, true, e, false);
  }

  bool walkStep(Auto& a, int64_t dtUs, bool& done) {
    double rem = a.speedRate * double(dtUs);
    for (int guard = 0; guard < 64; guard++) {
      double len = a.ppath ? double(a.ppath->length()) : 0;
      if (a.ppath && a.dist + rem < len) { a.dist += rem; rem = 0; break; }
      rem = a.ppath ? a.dist + rem - len : rem;
      if (a.graph) {
        if (a.conn.next >= 0) {
          a.pedNode = a.conn.next;
          const PedLane* l = a.blk->pedLane(a.pedNode);
          if (!l) return false;
          setPose(a, l->pos, a.heading);
          if (!pedPickSegment(a)) return false;
        } else {
          int e = (a.conn.heading / 8) & 3;
          const PedLane* l = a.blk->pedLane(a.pedNode);
          Pt ex = l ? l->pos : Pt();
          if (a.ppath && !a.ppath->pts.empty()) ex = a.ppath->pts.back();
          if (!pedLeaveTile(a, kEdgeDx[e], kEdgeDy[e], ex)) return false;
          if (--a.piecesLeft <= 0) { done = true; pedPose(a); return true; }
        }
      } else {
        const Pt& last = a.rev ? a.ppath->pts.front() : a.ppath->pts.back();
        int dx, dy; edgeOf(last, dx, dy);
        if (!pedLeaveTile(a, dx, dy, last)) return false;
        if (--a.piecesLeft <= 0) { done = true; pedPose(a); return true; }
      }
      a.dist = 0;
      if (!a.ppath) { if (rem <= 0) break; continue; }
    }
    pedPose(a);
    return true;
  }

  // ---------------------------------------------------------- free movers (boats, aircraft)
  void flyStep(Auto& a, int64_t dtUs, bool& done) {
    double v = (a.va ? a.va->maxSpeed() : 0.0) * double(dtUs) / 256.0;  // tiles
    while (v > 0 && a.wp < a.wps.size()) {
      double dx = a.wps[a.wp].first - a.fx, dy = a.wps[a.wp].second - a.fy;
      double d = std::hypot(dx, dy);
      if (d <= v) { a.fx = a.wps[a.wp].first; a.fy = a.wps[a.wp].second; v -= d; a.flown += d; a.wp++; continue; }
      a.fx += dx / d * v; a.fy += dy / d * v; a.flown += v;
      a.dirX = dx / d; a.dirY = dy / d;
      v = 0;
    }
    if (a.kind == AutomatonKind::Aircraft && a.totalFly > 0) {
      double t = std::clamp(a.flown / a.totalFly, 0.0, 1.0);
      double climb = a.climbTiles > 0 ? std::min(1.0, a.flown / a.climbTiles) : 1.0;
      a.fh = a.startAlt == a.endAlt ? a.startAlt : (a.endAlt < a.startAlt ? a.startAlt + (a.endAlt - a.startAlt) * t : a.startAlt + (a.endAlt - a.startAlt) * climb);
    }
    a.x = a.fx; a.y = a.fy; a.h = a.fh;
    a.tx = int(std::floor(a.fx)); a.ty = int(std::floor(a.fy));
    a.heading = heading32(a.dirX, a.dirY);
    if (a.wp >= a.wps.size()) done = true;
  }

  void wanderStep(Auto& a, int64_t dtUs) {
    double v = (a.va ? a.va->maxSpeed() : 0.0) * double(dtUs) / 256.0;
    a.nextTurnUs -= dtUs;
    if (a.nextTurnUs <= 0) {
      double ang = rng.f01() * 2 * M_PI;
      a.dirX = std::cos(ang); a.dirY = std::sin(ang);
      a.nextTurnUs = 5000000 + int64_t(rng.below(10000000));
    }
    double nx = a.fx + a.dirX * v, ny = a.fy + a.dirY * v;
    if (nx >= 0 && ny >= 0 && nx < host.mapWidth() && ny < host.mapHeight() && host.isWater(int(nx), int(ny))) { a.fx = nx; a.fy = ny; }
    else a.nextTurnUs = 0;  // blocked: pick a new heading next update
    a.x = a.fx; a.y = a.fy; a.tx = int(a.fx); a.ty = int(a.fy);
    a.heading = heading32(a.dirX, a.dirY);
  }

  // ---------------------------------------------------------- spawning helpers
  void attachAnim(Auto& a, const ResKey& animKey) {
    auto ait = D.ansprat.find(animKey.instance);
    if (ait == D.ansprat.end()) return;
    a.anim = &ait->second;
    auto sp = D.sprites.find(ait->second.sprite);
    if (sp != D.sprites.end()) a.spr = &sp->second;
  }

  Auto* spawnBoat(uint32_t inst, double x, double y, int owner, bool wander, std::vector<std::pair<double, double>> route) {
    auto vit = D.vattrib.find(inst);
    if (vit == D.vattrib.end()) return nullptr;
    Auto* a = create(AutomatonKind::Boat);
    a->va = &vit->second; a->attribInst = inst; a->length = a->va->length;
    attachAnim(*a, a->va->anim);
    a->fx = x; a->fy = y; a->owner = owner;
    a->tx = int(x); a->ty = int(y); a->x = x; a->y = y;
    a->animSeq = 20;
    if (wander) {
      a->beh = Beh::Wander;
      double ang = rng.f01() * 2 * M_PI; a->dirX = std::cos(ang); a->dirY = std::sin(ang); a->nextTurnUs = 5000000;
    } else { a->beh = Beh::Fly; a->wps = std::move(route); a->wp = 0; }
    soundEvent(SoundEvent::Start, a->va->motionSound, *a);
    return a;
  }

  Auto* spawnAircraft(uint32_t inst, double x, double y, double tx, double ty, bool heli, int owner, FlyEnd end, bool takeoff) {
    auto vit = D.vattrib.find(inst);
    if (vit == D.vattrib.end()) return nullptr;
    Auto* a = create(AutomatonKind::Aircraft);
    a->va = &vit->second; a->attribInst = inst; a->length = a->va->length; a->heli = heli; a->owner = owner;
    attachAnim(*a, a->va->anim);
    a->fx = x; a->fy = y; a->wps = {{tx, ty}}; a->wp = 0; a->flyEnd = end; a->beh = Beh::Fly;
    a->totalFly = std::hypot(tx - x, ty - y);
    a->startAlt = takeoff ? 0 : 2048; a->endAlt = takeoff ? 2048 : 0; a->fh = a->startAlt;
    a->climbTiles = takeoff ? std::min(10.0, a->totalFly) : 0;
    a->x = x; a->y = y; a->tx = int(x); a->ty = int(y);
    a->dirX = tx - x; a->dirY = ty - y;
    double n = std::hypot(a->dirX, a->dirY); if (n > 0) { a->dirX /= n; a->dirY /= n; }
    soundEvent(SoundEvent::Start, a->va->motionSound, *a);
    return a;
  }

  // ---------------------------------------------------------- road traffic (spec 5.5)
  void recompute() {
    long S = 0; int C = 0;
    if (rect.valid())
      for (int y = rect.y0; y <= rect.y1; y++)
        for (int x = rect.x0; x <= rect.x1; x++) {
          PieceRef pr;
          if (!host.pieceAt(x, y, NetKind::Road, pr)) continue;
          S += host.trafficDensity(x, y); C++;
        }
    if (eventMode_ == 2) limitL = int(double(S) * 0.2);
    else limitL = std::min(int(double(S) * double(0.008f)), kMaxRoadVehicles);
    avgA = C ? int(S / C) : 0;
    if (avgA > 255) avgA = 255;
    trafficLevel = limitL * 100 / 200;
  }

  bool roomForVehicle() const { return !(roadCount > limitL || roadCount > 199); }

  void fillTile(int x, int y) {
    PieceRef pr;
    if (!host.pieceAt(x, y, NetKind::Road, pr)) return;
    float v = float(host.trafficDensity(x, y)) * 0.008f;
    int n = int(v);
    if (rng.f01() < v - float(n)) n++;
    for (int i = 0; i < n; i++) {
      if (!roomForVehicle()) return;
      uint32_t m = drawModel(x, y);
      if (m) spawnVehicle(m, x, y, true);
    }
  }

  void initialFill(const Rect& r, const Rect* exclude) {
    if (!r.valid()) return;
    for (int y = r.y0; y <= r.y1; y++)
      for (int x = r.x0; x <= r.x1; x++) {
        if (exclude && exclude->contains(x, y)) continue;
        fillTile(x, y);
      }
  }

  void trickle() {
    if (!active || roadCount >= limitL || !rect.valid()) return;
    int k = std::max(1, int(double(limitL - roadCount) * 0.1));
    int w = rect.x1 - rect.x0 + 1, h = rect.y1 - rect.y0 + 1;
    for (int i = 0; i < k; i++) {
      if (!roomForVehicle()) return;
      int sx = rect.x0 + int(rng.below(uint32_t(w))), sy = rect.y0 + int(rng.below(uint32_t(h)));
      int x = sx, y = sy;
      do {
        PieceRef pr;
        if (host.pieceAt(x, y, NetKind::Road, pr)) {
          uint32_t r = rng.below(uint32_t(std::max(avgA, 1)));
          if (int(r) <= host.trafficDensity(x, y)) {
            uint32_t m = drawModel(x, y);
            if (m) spawnVehicle(m, x, y, false);
            break;
          }
        }
        if (++x > rect.x1) { x = rect.x0; if (++y > rect.y1) y = rect.y0; }
      } while (!(x == sx && y == sy));
    }
  }

  // ---------------------------------------------------------- pedestrians (spec 5.8)
  void chooseMode(PedSource& s) {
    int m = 1, d = 1; host.gameDate(m, d);
    const PplMode* md = D.pickPplMode(*s.table, cfg.country, cfg.holidays, m, d, rng.below(1000));
    s.mode = md;
    if (!md) { s.cap = 0; return; }
    s.cap = int(md->rate);
    if (rng.f01() < md->rate - float(int(md->rate))) s.cap++;
    s.radius = int(md->radius);
    if (md->dated) { s.endMonth = md->endMonth; s.endDay = md->endDay; }
    else s.endMonth = s.endDay = 0;
  }

  bool spawnPed(PedSource& s, int x, int y) {
    if (pedCount >= kMaxPedestrians || !s.mode) return false;
    int idx = pplModePick(*s.mode, rng.below(uint32_t(std::max<int64_t>(1, s.mode->cum.empty() ? 1 : s.mode->cum.back() + 1))));
    if (idx < 0) return false;
    auto pit = D.pattrib.find(s.mode->entries[size_t(idx)].second.instance);
    if (pit == D.pattrib.end()) return false;
    Auto* a = create(AutomatonKind::Pedestrian);
    a->pa = &pit->second; a->attribInst = pit->first; a->owner = s.id;
    attachAnim(*a, a->pa->anim);
    a->speedRate = 0;
    if (!pedEnter(*a, x, y, false, Pt(), true) || !startScript(*a, a->pa->script)) { a->alive = false; return false; }
    pedCount++; s.live++;
    return true;
  }

  void fillPedSource(PedSource& s) {
    if (!active || !s.mode || s.live >= s.cap) return;
    int x0 = std::max({0, s.x - s.radius, rect.x0}), x1 = std::min({host.mapWidth() - 1, s.x + s.radius, rect.x1});
    int y0 = std::max({0, s.y - s.radius, rect.y0}), y1 = std::min({host.mapHeight() - 1, s.y + s.radius, rect.y1});
    std::vector<std::pair<int, int>> tiles;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) if (host.pedEligible(x, y)) tiles.push_back({x, y});
    int C = int(tiles.size());
    if (C == 0) return;
    int n = std::min(s.cap - s.live, 4 * C);
    if (C < n) {
      int per = s.cap / C, rem = s.cap % C, acc = 0;
      for (auto& t : tiles) {
        int cnt = per;
        acc += rem;
        if (acc >= C) { acc -= C; cnt++; }
        for (int i = 0; i < cnt && s.live < s.cap; i++) spawnPed(s, t.first, t.second);
      }
    } else {
      for (int i = 0; i < n; i++) { auto& t = tiles[rng.below(uint32_t(C))]; spawnPed(s, t.first, t.second); }
    }
  }

  // ---------------------------------------------------------- spawners (timers)
  static bool timerFire(Timer& t, int64_t dt) {
    t.acc += dt;
    if (t.acc >= t.interval) { t.acc = 0; return true; }
    return false;
  }

  void stepSpawners(int64_t dt) {
    for (auto& sp : spawners) {
      if (!timerFire(sp.t, dt)) continue;
      switch (sp.kind) {
        case 0: seaportTick(sp); break;
        case 1: airportTick(sp); break;
        case 2: marinaTick(sp); break;
        default: stationTick(sp); break;
      }
    }
    // helicopters
    if (timerFire(heliTimer, dt)) {
      heliTimer.interval = 1000000 + int64_t(rng.below(2000000));
      auto it = D.airtrmgr.find(2);
      if (it != D.airtrmgr.end() && !helipads.empty() && !it->second.models.empty()) {
        int heli = countKind(AutomatonKind::Aircraft, true);
        if (heli < it->second.maxActive) {
          auto& a = helipads[rng.below(uint32_t(helipads.size()))];
          auto& b = helipads[rng.below(uint32_t(helipads.size()))];
          double tx = b.first + 0.5, ty = b.second + 0.5;
          if (a == b) { tx = std::clamp(a.first + 0.5 + double(int(rng.below(21)) - 10), 0.0, double(host.mapWidth() - 1)); ty = std::clamp(a.second + 0.5 + double(int(rng.below(21)) - 10), 0.0, double(host.mapHeight() - 1)); }
          spawnAircraft(uint32_t(it->second.models[rng.below(uint32_t(it->second.models.size()))]), a.first + 0.5, a.second + 0.5, tx, ty, true, 0, FlyEnd::Remove, true);
        }
      }
    }
  }

  void seaportTick(Spawner& sp) {
    if (!sp.ready) {
      sp.route = host.seaRoute(sp.id);
      sp.ready = !sp.route.empty();
      if (!sp.ready) { sp.t.interval = 360000000; return; }
    }
    sp.t.interval = 120000000 + int64_t(rng.below(240000000));
    int idx = (host.cityCondition(4) || host.cityCondition(5) ? 1 : 0) | (host.hasOccupantType2AFA() ? 2 : 0);
    auto vp = D.vclprod.find(uint32_t(0x3FC + idx));
    if (vp == D.vclprod.end()) return;
    if (sp.live >= int(vp->second.rate)) return;
    int pick = vclProdPick(vp->second, rng.below(uint32_t(std::max<uint64_t>(1, [&] { uint64_t s = 0; for (auto& e : vp->second.entries) s += e.first; return s; }()))));
    if (pick < 0) return;
    spawnBoat(vp->second.entries[size_t(pick)].second.instance, sp.route[0].first, sp.route[0].second, sp.id, false, sp.route);
    if (Auto* a = autos.empty() ? nullptr : autos.back().get()) if (a->kind == AutomatonKind::Boat && a->owner == sp.id) sp.live++;
  }

  void marinaTick(Spawner& sp) {
    sp.t.interval = 5000000 + int64_t(rng.below(10000000));  // assumption: spec gives no leisure-boat rate
    uint32_t tab = 0x400 + rng.below(2);
    auto vp = D.vclprod.find(tab);
    if (vp == D.vclprod.end()) return;
    if (sp.live >= std::max(1, int(vp->second.rate))) return;
    int R = D.tunables.maxMarinaToBoatDistance;
    for (int tries = 0; tries < 16; tries++) {
      int x = sp.x + int(rng.below(uint32_t(2 * R + 1))) - R, y = sp.y + int(rng.below(uint32_t(2 * R + 1))) - R;
      if (x < 0 || y < 0 || x >= host.mapWidth() || y >= host.mapHeight()) continue;
      if ((x - sp.x) * (x - sp.x) + (y - sp.y) * (y - sp.y) > R * R || !host.isWater(x, y)) continue;
      if (active && !rect.contains(x, y)) continue;
      uint64_t sum = 0; for (auto& e : vp->second.entries) sum += e.first;
      int pick = vclProdPick(vp->second, rng.below(uint32_t(std::max<uint64_t>(1, sum))));
      if (pick < 0) return;
      if (Auto* a = spawnBoat(vp->second.entries[size_t(pick)].second.instance, x + 0.5, y + 0.5, sp.id, true, {})) { a->owner = sp.id; sp.live++; }
      return;
    }
  }

  void airportTick(Spawner& sp) {
    sp.t.interval = 20000000 + int64_t(rng.below(20000000));
    auto it = D.airtrmgr.find(1);
    if (it == D.airtrmgr.end() || it->second.models.empty()) return;
    int W = host.mapWidth(), H = host.mapHeight();
    if (sp.arrived > 0) {
      sp.arrived--;
      int side = int(rng.below(4));
      double along = 5 + double(rng.below(uint32_t(std::max(1, (side < 2 ? W : H) - 10))));
      double ex = side == 0 ? along : side == 1 ? along : (side == 2 ? 5 : W - 5), ey = side == 0 ? 5 : side == 1 ? H - 5 : along;
      if (side >= 2) ey = along;
      spawnAircraft(uint32_t(it->second.models[rng.below(uint32_t(it->second.models.size()))]), sp.x + 0.5, sp.y + 0.5, ex, ey, false, sp.id, FlyEnd::Remove, true);
    }
    if (countKind(AutomatonKind::Aircraft, false) >= it->second.maxActive) return;
    int side = int(rng.below(4));
    double along = 5 + double(rng.below(uint32_t(std::max(1, (side < 2 ? W : H) - 10))));
    double sx, sy;
    if (side == 0) { sx = along; sy = 5; } else if (side == 1) { sx = along; sy = H - 5; }
    else if (side == 2) { sx = 5; sy = along; } else { sx = W - 5; sy = along; }
    spawnAircraft(uint32_t(it->second.models[rng.below(uint32_t(it->second.models.size()))]), sx, sy, sp.x + 0.5, sp.y + 0.5, false, sp.id, FlyEnd::Land, false);
  }

  int countKind(AutomatonKind k, bool heli) const {
    int n = 0;
    for (auto& u : autos) if (u->alive && u->kind == k && u->heli == heli) n++;
    return n;
  }

  void stationTick(Spawner& sp) {
    uint32_t inst = sp.kind == 4 ? 2 : 1;
    auto tp = D.trnprod.find(inst);
    if (tp == D.trnprod.end()) return;
    const TrnProd& t = tp->second;
    sp.t.interval = t.minUs + int64_t(rng.below(uint32_t(std::max<int64_t>(1, t.maxUs - t.minUs))));
    if (t.consists.empty()) return;
    NetKind nk = NetKind::Rail;
    if (!sp.dirKnown) {
      static const int order[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
      for (auto& o : order) {
        PieceRef pr;
        if (host.pieceAt(sp.x + o[0], sp.y + o[1], nk, pr)) { sp.dirX = o[0]; sp.dirY = o[1]; sp.dirKnown = true; break; }
      }
      if (!sp.dirKnown) return;
    }
    int tx = sp.x + sp.dirX, ty = sp.y + sp.dirY;
    if (active && !rect.contains(tx, ty)) return;
    const Consist& c = t.consists[rng.below(uint32_t(t.consists.size()))];
    std::vector<uint32_t> models{uint32_t(c.loco)};
    auto addFrom = [&](const std::vector<int>& l, int n) { for (int i = 0; i < n && !l.empty(); i++) models.push_back(uint32_t(l[rng.below(uint32_t(l.size()))])); };
    if (sp.kind == 4) addFrom(c.list[2], 1 + int(rng.below(3)));
    else {
      addFrom(c.list[0], 1);
      addFrom(c.list[2], 1 + int(rng.below(3)));
      addFrom(c.list[1], 1 + int(rng.below(4)));
      addFrom(c.list[3], 1);
    }
    std::vector<Auto*> built;
    for (size_t i = 0; i < models.size(); i++) {
      Auto* a = buildTrainCar(models[i], tx, ty, i == 0 ? nullptr : built.back());
      if (!a) { for (auto* b : built) kill(b); return; }
      built.push_back(a);
    }
  }

  Auto* buildTrainCar(uint32_t inst, int x, int y, Auto* leader) {
    auto vit = D.vattrib.find(inst);
    if (vit == D.vattrib.end()) return nullptr;
    const VAttrib& v = vit->second;
    Auto* a = create(AutomatonKind::Train);
    a->attribInst = inst; a->va = &v; a->length = v.length > 0 ? v.length : 70;
    attachAnim(*a, v.anim);
    if (!leader) {
      if (!placeVehicleOnTile(*a, x, y, NetKind::Rail, false) || !startScript(*a, v.script)) { a->alive = false; return nullptr; }
      a->trail.push_back({0, a->x, a->y, a->h, a->heading});
    } else {
      a->tx = x; a->ty = y; a->leader = leader->id;
      a->followOffset = (leader->length + a->length) / 2.0 / 256.0;  // tiles
      a->visible = false;
      if (!startScript(*a, v.script)) { a->alive = false; return nullptr; }
      a->x = leader->x; a->y = leader->y;
    }
    soundEvent(SoundEvent::Start, v.motionSound, *a);
    return a;
  }

  void followStep(Auto& a) {
    Auto* L = find(a.leader);
    if (!L || !L->alive) { a.stack.clear(); a.alive = false; kill(&a); return; }
    a.travelled = L->travelled - a.followOffset;
    if (L->trail.empty() || a.travelled < L->trail.front().t) { a.visible = false; return; }
    // find bracketing trail points
    const auto& tr = L->trail;
    size_t i = 0;
    while (i + 1 < tr.size() && tr[i + 1].t <= a.travelled) i++;
    const TrailPt& p0 = tr[i];
    const TrailPt& p1 = tr[std::min(i + 1, tr.size() - 1)];
    double f = p1.t > p0.t ? std::clamp((a.travelled - p0.t) / (p1.t - p0.t), 0.0, 1.0) : 0;
    a.x = p0.x + (p1.x - p0.x) * f; a.y = p0.y + (p1.y - p0.y) * f; a.h = p0.h + (p1.h - p0.h) * f;
    a.heading = p0.heading;
    a.tx = int(a.x); a.ty = int(a.y);
    a.visible = true;
    a.trail.push_back({a.travelled, a.x, a.y, a.h, a.heading});
    while (a.trail.size() > 4096) a.trail.pop_front();
  }

  // ---------------------------------------------------------- per-update
  void animTick(Auto& a, int64_t dt) {
    a.tickAcc += dt;
    while (a.tickAcc >= kAnimTickUs) { a.tickAcc -= kAnimTickUs; a.animIdx++; }
  }

  bool animStep(Auto& a, int64_t dt, bool& done) {
    a.animAcc += dt;
    if (a.animAcc > 300000) {
      a.animIdx++;
      const AnimSeq* s = a.anim ? a.anim->find(a.animSeq) : nullptr;
      int len = s ? int(s->frames.size()) : 1;
      if (a.animIdx >= len) {
        a.animIdx = 0;
        if (++a.loopsDone >= a.loopsLeft) { done = true; return true; }
        a.animAcc = 0;
      }
    }
    return true;
  }

  void updateOne(Auto& a, int64_t dt) {
    a.updates++;
    bool done = false, ok = true;
    switch (a.beh) {
      case Beh::Drive: {
        double before = a.dist; (void)before;
        double sx = a.x, sy = a.y;
        ok = driveStep(a, dt, done);
        animTick(a, dt);
        if (a.kind == AutomatonKind::Train && ok) {
          a.travelled += std::hypot(a.x - sx, a.y - sy);
          if (a.trail.empty() || a.travelled - a.trail.back().t >= 1.0 / 128) a.trail.push_back({a.travelled, a.x, a.y, a.h, a.heading});
          while (a.trail.size() > 4096) a.trail.pop_front();
        }
        break;
      }
      case Beh::Walk: ok = walkStep(a, dt, done); animTick(a, dt); break;
      case Beh::Anim: ok = animStep(a, dt, done); break;
      case Beh::Follow: followStep(a); animTick(a, dt); if (!a.alive) return; break;
      case Beh::Fly: {
        flyStep(a, dt, done);
        animTick(a, dt);
        if (done) {
          if (a.flyEnd == FlyEnd::Land) for (auto& s : spawners) if (s.id == a.owner) s.arrived++;
          kill(&a);
          return;
        }
        if (a.updates % 10 == 0 && a.va) soundEvent(SoundEvent::Move, a.va->motionSound, a);
        return;
      }
      case Beh::Wander: wanderStep(a, dt); animTick(a, dt); if (a.updates % 10 == 0 && a.va) soundEvent(SoundEvent::Move, a.va->motionSound, a); return;
      case Beh::None: break;
    }
    if (!ok) { kill(&a); return; }
    if (done) {
      if (!advanceScript(a)) { kill(&a); return; }
    }
    if (a.updates % 10 == 0 && a.va && a.kind != AutomatonKind::Pedestrian) soundEvent(SoundEvent::Move, a.va->motionSound, a);
  }

  void spacing() {
    std::unordered_map<uint64_t, std::vector<Auto*>> b;
    for (auto& u : autos)
      if (u->alive && u->beh == Beh::Drive && u->vpath && u->kind == AutomatonKind::Vehicle)
        b[(uint64_t(uint32_t(u->tx)) << 40) ^ (uint64_t(uint32_t(u->ty)) << 20) ^ u->vpathInst].push_back(u.get());
    for (auto& kv : b) {
      auto& v = kv.second;
      if (v.size() < 2) continue;
      std::sort(v.begin(), v.end(), [](Auto* p, Auto* q) { return p->dist > q->dist; });
      for (size_t i = 1; i < v.size(); i++) {
        Auto* lead = v[i - 1]; Auto* me = v[i];
        double gap = lead->dist - (lead->length + me->length) / 2.0;
        double lim = std::max(me->prevDist, gap);
        if (me->dist > lim) {
          me->dist = lim;
          int hd = 0; Pt p = vpathPoint(*me->vpath, me->dist, &hd);
          setPose(*me, p, hd);
        }
      }
    }
  }

  void update(int64_t dt) {
    if (dt <= 0) return;
    if (eventMode_ == 1) {
      eventAcc += dt;
      if (eventAcc >= 15000000) { eventMode_ = 2; recompute(); ensureGlobalPed(); }
    }
    if (!active) return;
    stepSpawners(dt);
    trickle();
    for (auto& s : pedSources) {
      if (s.mode && s.mode->dated && s.endMonth) {
        int m = 1, d = 1; host.gameDate(m, d);
        if (m * 100 + d > s.endMonth * 100 + s.endDay || m * 100 + d < s.mode->startMonth * 100 + s.mode->startDay) chooseMode(s);
      }
      s.refillAcc += dt;
      if (s.refillAcc >= 1000000) { s.refillAcc = 0; fillPedSource(s); }
    }
    // iterate by index: spawning inside updateOne can append
    size_t n = autos.size();
    for (size_t i = 0; i < n; i++) if (autos[i]->alive) updateOne(*autos[i], dt);
    spacing();
    sweep();
  }

  void ensureGlobalPed() {
    if (globalPedSrc || eventMode_ != 2) return;
    auto it = D.pplprod.find(1);
    if (it == D.pplprod.end()) return;
    globalPedSrc = true;
    PedSource s; s.id = nextSrc++; s.global = true;
    s.x = (rect.x0 + rect.x1) / 2; s.y = (rect.y0 + rect.y1) / 2;
    s.table = &it->second;
    chooseMode(s);
    pedSources.push_back(s);
    if (active) fillPedSource(pedSources.back());
  }

  // ---------------------------------------------------------- view
  void removeAll() {
    for (auto& u : autos) kill(u.get());
    sweep();
  }

  void setView(const ViewState& v) {
    Rect nr{v.x0, v.y0, v.x1, v.y1};
    nr.x0 = std::max(0, nr.x0); nr.y0 = std::max(0, nr.y0);
    nr.x1 = std::min(host.mapWidth() - 1, nr.x1); nr.y1 = std::min(host.mapHeight() - 1, nr.y1);
    bool want = v.zoom >= cfg.zoomThreshold && v.secondaryMode == 0 && cfg.zoomThreshold <= 4;
    Rect old = rect;
    view = v;
    rect = nr;
    if (!want) {
      if (active) { removeAll(); active = false; trafficLevel = 0; }
      return;
    }
    if (!active) {
      active = true;
      recompute();
      initialFill(rect, nullptr);
      for (auto& s : pedSources) fillPedSource(s);
      return;
    }
    if (nr == old) return;
    for (auto& u : autos) {
      if (!u->alive) continue;
      if (!rect.contains(int(std::floor(u->x)), int(std::floor(u->y)))) kill(u.get());
    }
    sweep();
    recompute();
    initialFill(rect, &old);
    for (auto& s : pedSources) fillPedSource(s);
  }
};

// ================================================================ public API
StreetSim::StreetSim(const AutomataData& d, AutomataHost& h, const Config& c) : p_(new Impl(d, h, c)) {}
StreetSim::~StreetSim() = default;

void StreetSim::setView(const ViewState& v) { p_->setView(v); }
void StreetSim::update(int64_t dt) { p_->update(dt); }
bool StreetSim::active() const { return p_->active; }

int StreetSim::addBuilding(int x, int y, const ResKey& ped, const ResKey& veh) {
  Impl& I = *p_;
  BuildingSrc b; b.id = I.nextSrc++; b.x = x; b.y = y; b.ped = ped; b.veh = veh;
  if (veh.instance) I.mergeTable(I.cellAt(x, y), veh.instance), I.cellAt(x, y).tables.push_back(veh.instance);
  if (ped.instance) {
    auto it = I.D.pplprod.find(ped.instance);
    if (it != I.D.pplprod.end()) {
      PedSource s; s.id = b.id; s.x = x; s.y = y; s.key = ped; s.table = &it->second;
      I.chooseMode(s);
      b.pedSrc = s.id;
      I.pedSources.push_back(s);
      if (I.active) I.fillPedSource(I.pedSources.back());
    }
  }
  I.buildings.push_back(b);
  return b.id;
}

void StreetSim::removeBuilding(int id) {
  Impl& I = *p_;
  for (size_t i = 0; i < I.buildings.size(); i++) {
    if (I.buildings[i].id != id) continue;
    BuildingSrc b = I.buildings[i];
    I.buildings.erase(I.buildings.begin() + long(i));
    if (b.veh.instance) {
      MixCell& c = I.cellAt(b.x, b.y);
      auto tabs = c.tables;
      auto it = std::find(tabs.begin(), tabs.end(), b.veh.instance);
      if (it != tabs.end()) tabs.erase(it);
      I.resetCell(c);
      for (uint32_t t : tabs) { I.mergeTable(c, t); c.tables.push_back(t); }
    }
    I.pedSources.erase(std::remove_if(I.pedSources.begin(), I.pedSources.end(), [&](const PedSource& s) { return s.id == b.pedSrc && b.pedSrc; }), I.pedSources.end());
    return;
  }
}

static int addSpawner(StreetSim::Impl& I, int x, int y, int kind) {
  Spawner s; s.id = I.nextSrc++; s.x = x; s.y = y; s.kind = kind;
  if (kind == 0) s.t.interval = 1000000;  // first attempt soon after creation
  else if (kind == 1) s.t.interval = 20000000 + int64_t(I.rng.below(20000000));
  else if (kind == 2) s.t.interval = 1000000;
  else {
    auto it = I.D.trnprod.find(kind == 4 ? 2 : 1);
    int64_t lo = it == I.D.trnprod.end() ? 30000000 : it->second.minUs, hi = it == I.D.trnprod.end() ? 90000000 : it->second.maxUs;
    s.t.interval = lo + int64_t(I.rng.below(uint32_t(std::max<int64_t>(1, hi - lo))));
  }
  I.spawners.push_back(s);
  return s.id;
}
int StreetSim::addSeaport(int x, int y) { return addSpawner(*p_, x, y, 0); }
int StreetSim::addAirport(int x, int y) { return addSpawner(*p_, x, y, 1); }
int StreetSim::addMarina(int x, int y) { return addSpawner(*p_, x, y, 2); }
int StreetSim::addStation(int x, int y, bool subway) { return addSpawner(*p_, x, y, subway ? 4 : 3); }
void StreetSim::addHelipad(int x, int y) { p_->helipads.push_back({x, y}); }

void StreetSim::pointSpawnPedestrians(int x, int y, int n) {
  Impl& I = *p_;
  if (!I.active || !I.host.pedEligible(x, y) || !I.rect.contains(x, y)) return;
  PedSource* src = nullptr;
  for (auto& s : I.pedSources) if (s.mode) { src = &s; break; }
  if (!src) {
    auto it = I.D.pplprod.find(1);
    if (it == I.D.pplprod.end()) return;
    PedSource s; s.id = I.nextSrc++; s.table = &it->second; s.x = x; s.y = y;
    I.chooseMode(s);
    I.pedSources.push_back(s);
    src = &I.pedSources.back();
  }
  for (int i = 0; i < n; i++) I.spawnPed(*src, x, y);
}

bool StreetSim::dispatchEmergency(EmergencyType t, int x, int y) {
  Impl& I = *p_;
  const auto& lst = I.cfg.highMemory ? I.D.tunables.emergencyHigh : I.D.tunables.emergencyLow;
  const char* sub = t == EmergencyType::FirePumper ? "fire" : t == EmergencyType::Ambulance ? "ambul" : "polic";
  uint32_t inst = t == EmergencyType::FirePumper ? 43 : t == EmergencyType::Ambulance ? 41 : 28;
  for (auto& kv : lst) {
    std::string n = kv.first;
    for (auto& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (n.find(sub) != std::string::npos || (t == EmergencyType::FirePumper && n.find("pump") != std::string::npos)) { inst = uint32_t(kv.second); break; }
  }
  return I.spawnVehicle(inst, x, y, false, true) != nullptr;
}

void StreetSim::notifyEvent() {
  Impl& I = *p_;
  if (I.eventMode_ != 0) return;
  I.eventMode_ = 1; I.eventAcc = 0;
  I.eventGrid.assign(70 * 70, 150);
  I.eventCounter++;
}
int StreetSim::eventMode() const { return p_->eventMode_; }

std::vector<AutomatonSprite> StreetSim::sprites() const {
  const Impl& I = *p_;
  std::vector<AutomatonSprite> out;
  if (!I.active) return out;
  for (auto& u : I.autos) {
    const Auto& a = *u;
    if (!a.alive || !a.visible || !a.spr) continue;
    const SpriteInst& s = *a.spr;
    if (s.zoomCount <= 0 || s.animSetCount <= 0) continue;
    int z = I.view.zoom - (5 - s.zoomCount);
    if (z < 0 || z >= s.zoomCount) continue;
    int o = 0;
    if (s.rotCount > 0) o = (((a.heading + I.view.rotation * 8) & 31) * s.rotCount) / 32;
    int frameVal = 0;
    if (a.anim) {
      const AnimSeq* seq = a.anim->find(a.animSeq);
      if (!seq && !a.anim->seqs.empty()) seq = &a.anim->seqs[0];
      if (seq && !seq->frames.empty()) frameVal = seq->frames[size_t(a.animIdx) % seq->frames.size()];
    }
    long idx = long(frameVal) * s.animSetCount + long(z) * s.zoomSetCount + o;
    if (idx < 0 || idx >= long(s.frames.size())) continue;
    const SpriteFrame& f = s.frames[size_t(idx)];
    AutomatonSprite r;
    r.id = a.id; r.kind = a.kind; r.category = f.category; r.spriteId = f.spriteId; r.flags = f.flags;
    r.tileX = a.x; r.tileY = a.y; r.height = a.h; r.heading32 = a.heading; r.frameIndex = int(idx);
    out.push_back(r);
  }
  return out;
}

std::vector<SoundEvent> StreetSim::takeSoundEvents() { auto v = std::move(p_->sounds); p_->sounds.clear(); return v; }
int StreetSim::trafficLevelPercent() const { return p_->trafficLevel; }
int StreetSim::roadVehicleCount() const { return p_->roadCount; }
int StreetSim::pedestrianCount() const { return p_->pedCount; }
int StreetSim::automatonCount() const { int n = 0; for (auto& u : p_->autos) n += u->alive; return n; }
int StreetSim::vehicleLimit() const { return p_->limitL; }
int StreetSim::averageDensity() const { return p_->avgA; }
int StreetSim::countKind(AutomatonKind k) const { int n = 0; for (auto& u : p_->autos) n += u->alive && u->kind == k; return n; }

std::vector<std::pair<uint32_t, double>> StreetSim::mixTable(int x, int y) const {
  std::vector<std::pair<uint32_t, double>> v;
  int cx = std::clamp(x / 32, 0, p_->cellsPerRow - 1), cy = std::clamp(y / 32, 0, p_->cellsPerCol - 1);
  for (auto& kv : p_->mix[size_t(cy) * p_->cellsPerRow + cx].w) v.push_back(kv);
  return v;
}

}  // namespace m3k
