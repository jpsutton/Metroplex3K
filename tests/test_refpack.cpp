// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <random>
#include "core/refpack.h"
#include "testutil.h"
using namespace m3k;

int main() {
  // Empty input.
  { Bytes in; auto enc = refpackEncode(in); CHECK(refpackDecode(enc).empty()); }
  // Short literals.
  for (size_t n = 1; n < 12; n++) {
    Bytes in; for (size_t i = 0; i < n; i++) in.push_back(uint8_t(i * 37 + 5));
    CHECK(refpackDecode(refpackEncode(in)) == in);
  }
  // Highly repetitive (overlapping matches, long matches).
  { Bytes in(100000, 0xAB); auto enc = refpackEncode(in); CHECK(enc.size() < 2000); CHECK(refpackDecode(enc) == in); }
  // Structured + random.
  std::mt19937 rng(12345);
  for (int trial = 0; trial < 30; trial++) {
    Bytes in;
    size_t n = rng() % 60000;
    while (in.size() < n) {
      if (rng() % 3 == 0 && in.size() > 10) {
        size_t from = rng() % in.size(), len = 3 + rng() % 200;
        for (size_t i = 0; i < len && in.size() < n; i++) in.push_back(in[from + i % (in.size() - from)]);
      } else in.push_back(uint8_t(rng() % (trial % 2 ? 256 : 6)));
    }
    auto enc = refpackEncode(in);
    CHECK(enc[0] == 0x10 && enc[1] == 0xFB);
    CHECK(refpackDecode(enc) == in);
  }
  // Hand-built stream: "ABCD" literal run (E0 -> 4 literals), then match len 4 dist 4 (3-byte op), stop.
  {
    Bytes s = {0x10, 0xFB, 0, 0, 8, 0xE0, 'A', 'B', 'C', 'D', 0x80 + 0, 0x00, 0x03, 0xFC};
    auto d = refpackDecode(s);
    CHECK(d.size() == 8 && d[4] == 'A' && d[7] == 'D');
  }
  // Malformed.
  { Bytes bad = {0x10, 0xFB, 0, 0, 4, 0xE0, 'A'}; bool threw = false; try { refpackDecode(bad); } catch (const FormatError&) { threw = true; } CHECK(threw); }
  TEST_MAIN_END();
}
