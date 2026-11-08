// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/refpack.h"

#include <algorithm>
#include <unordered_map>

namespace m3k {

Bytes refpackDecode(const uint8_t* in, size_t n) {
  if (n < 5) throw FormatError("refpack: stream too short");
  size_t ip = 0;
  uint8_t flags = in[ip++];
  ip++;  // magic 0xFB, not checked
  if (flags & 1) ip += 3;  // compressed size, skipped
  if (ip + 3 > n) throw FormatError("refpack: truncated header");
  size_t raw = (size_t(in[ip]) << 16) | (size_t(in[ip + 1]) << 8) | in[ip + 2];
  ip += 3;

  Bytes out;
  out.reserve(raw);
  auto need = [&](size_t k) { if (ip + k > n) throw FormatError("refpack: truncated stream"); };
  for (;;) {
    need(1);
    uint8_t b0 = in[ip++];
    size_t lit = 0, len = 0, dist = 0;
    bool stop = false;
    if (b0 < 0x80) {
      need(1);
      uint8_t b1 = in[ip++];
      lit = b0 & 3;
      len = ((b0 >> 2) & 7) + 3;
      dist = ((size_t(b0) & 0x60) << 3) + b1 + 1;
    } else if (b0 < 0xC0) {
      need(2);
      uint8_t b1 = in[ip++], b2 = in[ip++];
      lit = b1 >> 6;
      len = (b0 & 0x3F) + 4;
      dist = ((size_t(b1) & 0x3F) << 8) + b2 + 1;
    } else if (b0 < 0xE0) {
      need(3);
      uint8_t b1 = in[ip++], b2 = in[ip++], b3 = in[ip++];
      lit = b0 & 3;
      len = ((size_t(b0) & 0x0C) << 6) + b3 + 5;
      dist = ((size_t(b0) & 0x10) << 12) + (size_t(b1) << 8) + b2 + 1;
    } else if (b0 < 0xFC) {
      lit = (size_t(b0 & 0x1F) + 1) * 4;
    } else {
      lit = b0 & 3;
      stop = true;
    }
    need(lit);
    out.insert(out.end(), in + ip, in + ip + lit);
    ip += lit;
    if (stop) break;
    if (len) {
      if (dist > out.size()) throw FormatError("refpack: match distance before start of output");
      size_t from = out.size() - dist;
      for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);  // byte-wise: may overlap
    }
  }
  if (out.size() != raw) {
    // Original does not detect overrun; be lenient on overrun, strict on underrun.
    if (out.size() < raw) throw FormatError("refpack: output shorter than declared size");
    out.resize(raw);
  }
  return out;
}

namespace {

struct Match { size_t len = 0; size_t dist = 0; };

int opCost(size_t len, size_t d0) {
  if (d0 < 1024 && len <= 10) return 2;
  if (d0 < 16384 && len <= 67) return 3;
  return 4;
}

}  // namespace

Bytes refpackEncode(const uint8_t* in, size_t n) {
  if (n > 0xFFFFFF) throw FormatError("refpack: input too large");
  Bytes out;
  out.push_back(0x10);
  out.push_back(0xFB);
  out.push_back(uint8_t(n >> 16));
  out.push_back(uint8_t(n >> 8));
  out.push_back(uint8_t(n));

  // Anchor positions bucketed by their first 3 bytes. Candidates are visited nearest first.
  std::unordered_map<uint32_t, std::vector<uint32_t>> anchors;
  auto key3 = [&](size_t p) { return (uint32_t(in[p]) << 16) | (uint32_t(in[p + 1]) << 8) | in[p + 2]; };

  size_t pendingStart = 0;  // literals in [pendingStart, p) are pending
  auto flushRuns = [&](size_t upto) {
    // Emit E0-FB literal runs while more than 3 literals are pending.
    size_t pend = upto - pendingStart;
    while (pend > 3) {
      size_t k = std::min<size_t>(pend & ~size_t(3), 112);
      out.push_back(uint8_t(0xE0 + (k / 4 - 1)));
      out.insert(out.end(), in + pendingStart, in + pendingStart + k);
      pendingStart += k;
      pend -= k;
    }
  };

  size_t p = 0;
  while (p < n) {
    Match best;
    int bestGain = 0;
    size_t bestLenForCmp = 2;
    if (n - p >= 3) {
      size_t maxLen = std::min<size_t>(1028, n - p);
      auto it = anchors.find(key3(p));
      if (it != anchors.end()) {
        const auto& v = it->second;
        for (size_t i = v.size(); i-- > 0;) {
          size_t c = v[i];
          size_t D = p - c;
          if (D > 131071) break;  // older anchors are only farther
          size_t L = 0;
          while (L < maxLen && in[c + L] == in[p + L]) L++;
          int gain = int(L) - opCost(L, D - 1);
          if (L > bestLenForCmp && gain > bestGain) {
            best.len = L; best.dist = D; bestGain = gain; bestLenForCmp = L;
          }
          if (L == 1028) break;
        }
      }
      anchors[key3(p)].push_back(uint32_t(p));
    }
    if (best.len == 0) { p++; continue; }

    flushRuns(p);
    size_t t = p - pendingStart;  // 0..3 carried literals
    size_t L = best.len, d = best.dist - 1;
    int cost = opCost(L, d);
    if (cost == 2) {
      out.push_back(uint8_t(((d >> 3) & 0x60) + ((L - 3) << 2) + t));
      out.push_back(uint8_t(d & 0xFF));
    } else if (cost == 3) {
      out.push_back(uint8_t(0x80 + (L - 4)));
      out.push_back(uint8_t(((d >> 8) & 0x3F) + (t << 6)));
      out.push_back(uint8_t(d & 0xFF));
    } else {
      out.push_back(uint8_t(0xC0 + (((L - 5) >> 6) & 0x0C) + ((d >> 12) & 0x10) + t));
      out.push_back(uint8_t((d >> 8) & 0xFF));
      out.push_back(uint8_t(d & 0xFF));
      out.push_back(uint8_t((L - 5) & 0xFF));
    }
    out.insert(out.end(), in + pendingStart, in + pendingStart + t);
    p += L;
    pendingStart = p;
  }
  flushRuns(n);
  size_t t = n - pendingStart;
  out.push_back(uint8_t(0xFC + t));
  out.insert(out.end(), in + pendingStart, in + pendingStart + t);
  return out;
}

}  // namespace m3k
