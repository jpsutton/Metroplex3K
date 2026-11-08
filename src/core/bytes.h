// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Little-endian byte helpers and a bounds-checked reader.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace m3k {

using Bytes = std::vector<uint8_t>;

struct FormatError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void wr16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void wr32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
}
inline void put16(Bytes& b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
inline void put32(Bytes& b, uint32_t v) { put16(b, uint16_t(v)); put16(b, uint16_t(v >> 16)); }

// Sequential reader over a byte range. Throws FormatError on overrun.
class Reader {
 public:
  Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
  explicit Reader(const Bytes& b) : p_(b.data()), n_(b.size()) {}
  size_t pos() const { return pos_; }
  size_t size() const { return n_; }
  size_t remaining() const { return n_ - pos_; }
  void seek(size_t p) { if (p > n_) fail(); pos_ = p; }
  void skip(size_t k) { need(k); pos_ += k; }
  uint8_t u8() { need(1); return p_[pos_++]; }
  uint16_t u16() { need(2); auto v = rd16(p_ + pos_); pos_ += 2; return v; }
  uint32_t u32() { need(4); auto v = rd32(p_ + pos_); pos_ += 4; return v; }
  int16_t s16() { return int16_t(u16()); }
  int32_t s32() { return int32_t(u32()); }
  float f32() { uint32_t v = u32(); float f; static_assert(sizeof f == 4, ""); std::memcpy(&f, &v, 4); return f; }
  const uint8_t* ptr() const { return p_ + pos_; }
  std::string str(size_t k) { need(k); std::string s(reinterpret_cast<const char*>(p_ + pos_), k); pos_ += k; return s; }
  Bytes bytes(size_t k) { need(k); Bytes b(p_ + pos_, p_ + pos_ + k); pos_ += k; return b; }

 private:
  void need(size_t k) const { if (k > n_ - pos_) fail(); }
  [[noreturn]] static void fail() { throw FormatError("read past end of data"); }
  const uint8_t* p_;
  size_t n_;
  size_t pos_ = 0;
};

}  // namespace m3k
