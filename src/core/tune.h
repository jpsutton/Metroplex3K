// Small helpers for reading tunables from IniFile with defaults (used by clock/economy/population).
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include "core/ini.h"

namespace m3k {
namespace tune {

inline bool parseInt(const std::string& raw, int64_t& out) {
  std::string s = trimSpaces(raw);
  if (s.empty()) return false;
  const char* p = s.c_str();
  char* end = nullptr;
  int base = 10;
  size_t off = 0;
  bool neg = false;
  if (p[off] == '-') { neg = true; off++; } else if (p[off] == '+') off++;
  if (p[off] == '0' && (p[off + 1] == 'x' || p[off + 1] == 'X')) { base = 16; off += 2; }
  long long v = std::strtoll(p + off, &end, base);
  if (end == p + off) return false;
  out = neg ? -v : v;
  return true;
}
inline int64_t getInt(const IniFile& ini, const std::string& sec, const std::string& key, int64_t dflt) {
  std::string s;
  int64_t v;
  if (ini.get(sec, key, s) && parseInt(s, v)) return v;
  return dflt;
}
inline double getDouble(const IniFile& ini, const std::string& sec, const std::string& key, double dflt) {
  std::string s;
  if (!ini.get(sec, key, s)) return dflt;
  s = trimSpaces(s);
  if (s.empty()) return dflt;
  char* end = nullptr;
  double v = std::strtod(s.c_str(), &end);
  return end == s.c_str() ? dflt : v;
}

}  // namespace tune
}  // namespace m3k
