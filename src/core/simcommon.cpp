#include "core/simcommon.h"

#include <cstdio>
#include <cstdlib>

namespace m3k { namespace us {

// ---- SimRng ----
uint32_t SimRng::rand(uint32_t n) {
  if (n == 0) return 0;
  for (;;) {
    uint64_t v = uint64_t(s_) * 0x41C64E6Du + 0x3039u;
    s_ = uint32_t(v);
    uint32_t r = uint32_t(v >> 16);
    if (n < 0x00FFFFFFu) return r % n;
    // Original behaviour: mask built from R itself, so R & m == R; accept when R <= n (inclusive).
    if (r <= n) return r;
  }
}

double SimRng::randUnit() {
  if (t_ == 0) t_ = 0x12345678u;
  t_ = t_ * 0x278DDE6Du;
  return double(t_) / 4294967296.0;
}

// ---- AgentHierarchy ----
void AgentHierarchy::loadIni(const IniFile& f) {
  for (const auto& kv : f.section("AgentTypeHierarchy")) {
    uint32_t c = uint32_t(parseDecimal(kv.first, -1));
    int64_t p = parseDecimal(kv.second, -1);
    if (int32_t(c) < 0 || p < 0) continue;
    parent_[c] = uint32_t(p);
  }
}

int AgentHierarchy::distance(uint32_t type, uint32_t anc) const {
  uint32_t cur = type;
  for (int d = 0; d < 64; d++) {
    if (cur == anc) return d;
    auto it = parent_.find(cur);
    if (it == parent_.end() || it->second == cur) return -1;
    cur = it->second;
  }
  return -1;
}

// ---- parsing ----
int64_t parseDecimal(const std::string& s, int64_t dflt) {
  size_t i = 0;
  while (i < s.size() && s[i] == ' ') i++;
  bool neg = false;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
  if (i >= s.size() || s[i] < '0' || s[i] > '9') return dflt;
  int64_t v = 0;
  for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (s[i] - '0');
  return neg ? -v : v;
}

uint32_t parseQuirky(const std::string& str) {
  size_t i = 0;
  while (i < str.size() && (str[i] == ' ' || str[i] == '\t')) i++;
  bool neg = false;
  if (i < str.size() && str[i] == '-') { neg = true; i++; }
  bool hex = false;
  if (i + 1 < str.size() && str[i] == '0' && (str[i + 1] == 'x' || str[i + 1] == 'X')) { hex = true; i += 2; }
  else
    for (size_t k = i; k < str.size(); k++) {
      char c = str[k];
      if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) { hex = true; break; }
    }
  uint32_t v = 0;
  for (; i < str.size(); i++) {
    char c = str[i];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else break;
    v = v * (hex ? 16u : 10u) + uint32_t(d);
  }
  return neg ? uint32_t(0) - v : v;
}

int parseFields(const std::string& s, const char* fmt, int* out, int maxOut) {
  int a[8] = {};
  int n = std::sscanf(s.c_str(), fmt, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7]);
  if (n < 0) n = 0;
  for (int i = 0; i < n && i < maxOut; i++) out[i] = a[i];
  return n;
}

int64_t IniReader::num(const std::string& sec, const std::string& key, int64_t dflt) const {
  std::string v;
  if (!f || !f->get(sec, key, v)) return dflt;
  return parseDecimal(v, dflt);
}
double IniReader::real(const std::string& sec, const std::string& key, double dflt) const {
  std::string v;
  if (!f || !f->get(sec, key, v)) return dflt;
  char* e = nullptr;
  double d = std::strtod(v.c_str(), &e);
  return e == v.c_str() ? dflt : d;
}
uint32_t IniReader::quirky(const std::string& sec, const std::string& key, uint32_t dflt) const {
  std::string v;
  if (!f || !f->get(sec, key, v)) return dflt;
  return parseQuirky(v);
}
bool IniReader::has(const std::string& sec, const std::string& key) const {
  std::string v;
  return f && f->get(sec, key, v);
}

// ---- Chart ----
Chart Chart::fresh(uint32_t initial, uint32_t scale) {
  Chart c;
  for (auto& v : c.monthly) v = initial;
  for (auto& v : c.yearly) v = initial;
  for (auto& v : c.decade) v = initial;
  c.max = scale;
  return c;
}
void Chart::pushMonthly(uint32_t v) { for (int i = 11; i > 0; i--) monthly[i] = monthly[i - 1]; monthly[0] = v; }
void Chart::pushYearly(uint32_t v) { for (int i = 9; i > 0; i--) yearly[i] = yearly[i - 1]; yearly[0] = v; }
void Chart::pushDecade(uint32_t v) { for (int i = 9; i > 0; i--) decade[i] = decade[i - 1]; decade[0] = v; }
void Chart::recomputeAutoMax(uint32_t floor) {
  uint32_t m = floor;
  for (auto v : monthly) m = v > m ? v : m;
  for (auto v : yearly) m = v > m ? v : m;
  for (auto v : decade) m = v > m ? v : m;
  max = m;
}
void Chart::parse(Reader& r) {
  for (auto& v : monthly) v = r.u32();
  for (auto& v : yearly) v = r.u32();
  for (auto& v : decade) v = r.u32();
  max = r.u32();
}
void Chart::write(Bytes& out) const {
  for (auto v : monthly) put32(out, v);
  for (auto v : yearly) put32(out, v);
  for (auto v : decade) put32(out, v);
  put32(out, max);
}
bool Chart::operator==(const Chart& o) const {
  for (int i = 0; i < 12; i++) if (monthly[i] != o.monthly[i]) return false;
  for (int i = 0; i < 10; i++) if (yearly[i] != o.yearly[i] || decade[i] != o.decade[i]) return false;
  return max == o.max;
}

// ---- Records ----
RecHeader readRecHeader(Reader& r) {
  RecHeader h;
  h.version = r.u16();
  h.flags = r.u8();
  if (h.flags & 2) r.u8();
  if (r.u32() != 0xDEADBEEFu) throw FormatError("record sentinel missing");
  return h;
}
void writeRecHeader(Bytes& out, uint16_t version, uint8_t flags) {
  put16(out, version);
  out.push_back(uint8_t(flags | 2));
  out.push_back(0);
  put32(out, 0xDEADBEEFu);
}
void putStr(Bytes& out, const std::string& s) {
  put32(out, uint32_t(s.size()));
  out.insert(out.end(), s.begin(), s.end());
}
void expectStr(Reader& r, const std::string& s) {
  uint32_t n = r.u32();
  if (r.str(n) != s) throw FormatError("unexpected marker string");
}
void put64(Bytes& out, int64_t v) { put32(out, uint32_t(uint64_t(v))); put32(out, uint32_t(uint64_t(v) >> 32)); }
int64_t rd64(Reader& r) { uint64_t lo = r.u32(); uint64_t hi = r.u32(); return int64_t(lo | (hi << 32)); }
void putS32(Bytes& out, int32_t v) { put32(out, uint32_t(v)); }

// ---- SweepOrder ----
void SweepOrder::init(int nx, int nz) {
  state = 1; mode = 0; rebuild = 1;
  xMin = 0; xMax = nx - 1; zMin = 0; zMax = nz - 1; total = nx * nz;
  list.clear();
  for (int x = 0; x < nx; x++)
    for (int z = 0; z < nz; z++) list.push_back(uint16_t((x << 8) | z));
  index = 1; stridePos = 0; stride = 1;
}
void SweepOrder::shuffle(MsRand& r) {
  for (size_t i = 1; i < list.size(); i++) {
    size_t j = size_t(r.next()) % (i + 1);
    std::swap(list[i], list[j]);
  }
}
void SweepOrder::parse(Reader& r) {
  state = r.u8(); mode = r.u32(); rebuild = r.u8();
  xMin = r.s32(); xMax = r.s32(); zMin = r.s32(); zMax = r.s32(); total = r.s32();
  uint32_t n = r.u32();
  if (n > r.remaining() / 2) throw FormatError("sweep order too long");
  list.assign(n, 0);
  for (uint32_t i = 0; i < n; i++) list[n - 1 - i] = r.u16();
  index = r.s32(); stridePos = r.s32(); stride = r.s32();
}
void SweepOrder::write(Bytes& out) const {
  out.push_back(state); put32(out, mode); out.push_back(rebuild);
  putS32(out, xMin); putS32(out, xMax); putS32(out, zMin); putS32(out, zMax); putS32(out, total);
  put32(out, uint32_t(list.size()));
  for (size_t i = list.size(); i-- > 0;) put16(out, list[i]);
  putS32(out, index); putS32(out, stridePos); putS32(out, stride);
}
bool SweepOrder::operator==(const SweepOrder& o) const {
  return state == o.state && mode == o.mode && rebuild == o.rebuild && xMin == o.xMin && xMax == o.xMax &&
         zMin == o.zMin && zMax == o.zMax && total == o.total && list == o.list && index == o.index &&
         stridePos == o.stridePos && stride == o.stride;
}

}}  // namespace m3k::us
