// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/peicon.h"
#include <algorithm>
#include <vector>

namespace m3k {

namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

struct Pe {
  const Bytes& b;
  struct Sec { uint32_t va, vsize, raw, rawSize; };
  std::vector<Sec> secs;
  uint32_t rsrcRva = 0;
  explicit Pe(const Bytes& bytes) : b(bytes) {}
  bool open() {
    if (b.size() < 0x40 || b[0] != 'M' || b[1] != 'Z') return false;
    uint32_t pe = le32(&b[0x3C]);
    if (pe + 24 > b.size() || b[pe] != 'P' || b[pe + 1] != 'E' || b[pe + 2] || b[pe + 3]) return false;
    uint16_t nsec = le16(&b[pe + 6]), optSize = le16(&b[pe + 20]);
    size_t opt = pe + 24;
    if (opt + optSize > b.size() || optSize < 2) return false;
    uint16_t magic = le16(&b[opt]);
    size_t dirs = opt + (magic == 0x20B ? 112 : 96);   // PE32+ / PE32 data directories
    if (dirs + 3 * 8 > opt + optSize) return false;
    rsrcRva = le32(&b[dirs + 2 * 8]);
    size_t st = opt + optSize;
    for (uint16_t i = 0; i < nsec; i++) {
      size_t s = st + size_t(i) * 40;
      if (s + 40 > b.size()) return false;
      secs.push_back({le32(&b[s + 12]), le32(&b[s + 8]), le32(&b[s + 20]), le32(&b[s + 16])});
    }
    return rsrcRva != 0;
  }
  // File offset of an RVA, or 0 when outside every section.
  size_t off(uint32_t rva) const {
    for (const auto& s : secs) {
      uint32_t len = std::max(s.vsize, s.rawSize);
      if (rva >= s.va && rva < s.va + len) { size_t o = size_t(s.raw) + (rva - s.va); return o < b.size() ? o : 0; }
    }
    return 0;
  }
  // Entry `id` of the resource directory at `dirOff` (relative to the resource root); first entry when id == 0.
  bool child(size_t root, size_t dirOff, uint32_t id, uint32_t& value) const {
    size_t d = root + dirOff;
    if (d + 16 > b.size()) return false;
    size_t n = size_t(le16(&b[d + 12])) + le16(&b[d + 14]);
    for (size_t i = 0; i < n; i++) {
      size_t e = d + 16 + i * 8;
      if (e + 8 > b.size()) return false;
      uint32_t name = le32(&b[e]);
      if (id == 0 || (!(name & 0x80000000u) && name == id)) { value = le32(&b[e + 4]); return true; }
    }
    return false;
  }
  // Data of resource type/id (first language).
  bool resource(uint32_t type, uint32_t id, const uint8_t*& p, size_t& n) const {
    size_t root = off(rsrcRva);
    if (!root) return false;
    uint32_t v;
    if (!child(root, 0, type, v) || !(v & 0x80000000u)) return false;
    if (!child(root, v & 0x7FFFFFFF, id, v) || !(v & 0x80000000u)) return false;
    if (!child(root, v & 0x7FFFFFFF, 0, v) || (v & 0x80000000u)) return false;
    size_t de = root + v;
    if (de + 16 > b.size()) return false;
    size_t o = off(le32(&b[de]));
    n = le32(&b[de + 4]);
    if (!o || o + n > b.size()) return false;
    p = &b[o];
    return true;
  }
};
}  // namespace

bool decodeIconDib(const uint8_t* p, size_t n, Image& out) {
  if (n < 40 || le32(p) < 40) return false;
  uint32_t hs = le32(p);
  int w = int(le32(p + 4)), h = int(le32(p + 8)) / 2, bpp = le16(p + 14);
  if (w <= 0 || h <= 0 || w > 256 || h > 256 || le32(p + 16) != 0) return false;
  if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return false;
  size_t palN = bpp <= 8 ? (le32(p + 32) ? le32(p + 32) : 1u << bpp) : 0;
  size_t pal = hs, xorOff = pal + palN * 4;
  size_t xorStride = (size_t(w) * bpp + 31) / 32 * 4, andStride = (size_t(w) + 31) / 32 * 4;
  size_t andOff = xorOff + xorStride * h;
  if (andOff + andStride * h > n && bpp != 32) return false;
  if (xorOff + xorStride * h > n) return false;
  out.w = w; out.h = h; out.px.assign(size_t(w) * h, 0);
  for (int y = 0; y < h; y++) {
    const uint8_t* row = p + xorOff + xorStride * size_t(h - 1 - y);
    const uint8_t* mrow = andOff + andStride * h <= n ? p + andOff + andStride * size_t(h - 1 - y) : nullptr;
    for (int x = 0; x < w; x++) {
      uint8_t r = 0, g = 0, bl = 0, a = 255;
      if (bpp <= 8) {
        uint32_t idx = bpp == 8 ? row[x] : bpp == 4 ? (row[x / 2] >> (x & 1 ? 0 : 4)) & 15 : (row[x / 8] >> (7 - x % 8)) & 1;
        if (idx >= palN) continue;
        const uint8_t* c = p + pal + idx * 4;
        bl = c[0]; g = c[1]; r = c[2];
      } else {
        const uint8_t* c = row + size_t(x) * (bpp / 8);
        bl = c[0]; g = c[1]; r = c[2];
        if (bpp == 32) a = c[3];
      }
      if (mrow && (mrow[x / 8] >> (7 - x % 8)) & 1) a = 0;
      out.px[size_t(y) * w + x] = a ? rgba(r, g, bl, a) : 0u;
    }
  }
  return true;
}

bool loadPeIcon(const Bytes& exe, uint32_t groupId, int size, Image& out) {
  Pe pe(exe);
  if (!pe.open()) return false;
  const uint8_t* g; size_t gn;
  if (!pe.resource(14, groupId, g, gn) || gn < 6) return false;   // RT_GROUP_ICON
  uint16_t count = le16(g + 4);
  int bestBpp = -1; uint16_t bestId = 0;
  for (uint16_t i = 0; i < count; i++) {
    const uint8_t* e = g + 6 + size_t(i) * 14;
    if (size_t(e + 14 - g) > gn) break;
    int w = e[0] ? e[0] : 256, h = e[1] ? e[1] : 256, bpp = le16(e + 6);
    if (w == size && h == size && bpp > bestBpp) { bestBpp = bpp; bestId = le16(e + 12); }
  }
  if (bestBpp < 0) return false;
  const uint8_t* d; size_t dn;
  if (!pe.resource(3, bestId, d, dn)) return false;               // RT_ICON
  return decodeIconDib(d, dn, out);
}

}  // namespace m3k
