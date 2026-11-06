// 132-byte graph-history block (spec/10-save-format.md §6.12 / Q-BL).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "core/bytes.h"

namespace m3k {

struct Chart {
  static constexpr size_t kBytes = 132;
  uint32_t monthly[12] = {};
  uint32_t yearly[10] = {};
  uint32_t decade[10] = {};
  uint32_t max = 0;
  // Behaviour properties (not stored in the file).
  bool autoScale = false;
  uint32_t floorMax = 10;
  double scaleFactor = 1.0;   // auto-scale: max = round(largest sample * factor), never below floorMax (spec 10 6.12)

  // New chart: initial sample in all 32 slots, creation scale in max.
  static Chart make(uint32_t initial, uint32_t scale, bool autoScale = false, uint32_t floorMax = 10, double scaleFactor = 1.0) {
    Chart c;
    for (auto& v : c.monthly) v = initial;
    for (auto& v : c.yearly) v = initial;
    for (auto& v : c.decade) v = initial;
    c.max = scale;
    c.autoScale = autoScale;
    c.floorMax = floorMax;
    c.scaleFactor = scaleFactor;
    return c;
  }
  // Auto-scale behaviour of a chart loaded from a file (the properties are not stored).
  void setAutoScale(uint32_t floor, double factor) { autoScale = true; floorMax = floor; scaleFactor = factor; }
  // Spec 10 §6.12 [STATIC Q-BL]: index 0 = newest (FIFO insert at 0).
  void pushMonthly(uint32_t v) { push(monthly, 12, v); }
  void pushYearly(uint32_t v) { push(yearly, 10, v); }
  void pushDecade(uint32_t v) { push(decade, 10, v); }

  uint32_t largest() const {
    uint32_t m = 0;
    for (auto v : monthly) m = std::max(m, v);
    for (auto v : yearly) m = std::max(m, v);
    for (auto v : decade) m = std::max(m, v);
    return m;
  }

  static Chart parse(const uint8_t* p, size_t n) {
    if (n < kBytes) throw FormatError("chart: short block");
    Chart c;
    for (int i = 0; i < 12; i++) c.monthly[i] = rd32(p + 4 * i);
    for (int i = 0; i < 10; i++) c.yearly[i] = rd32(p + 0x30 + 4 * i);
    for (int i = 0; i < 10; i++) c.decade[i] = rd32(p + 0x58 + 4 * i);
    c.max = rd32(p + 0x80);
    return c;
  }
  void serialize(Bytes& out) const {
    for (auto v : monthly) put32(out, v);
    for (auto v : yearly) put32(out, v);
    for (auto v : decade) put32(out, v);
    put32(out, max);
  }

 private:
  void push(uint32_t* arr, int n, uint32_t v) {
    if (!autoScale && v > max) v = max;
    for (int i = n - 1; i > 0; i--) arr[i] = arr[i - 1];  // Q-BL: index 0 = newest
    arr[0] = v;
    if (autoScale && v > max) {
      const uint32_t m = uint32_t(std::floor(double(largest()) * scaleFactor + 0.5));   // rounded to nearest, [DATA]
      max = std::max(m, floorMax);
    }
  }
};

}  // namespace m3k
