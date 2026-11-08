// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared helpers for the utility/service simulation layers (spec 16, 17, 10 section 6).
// Platform independent; no SDL. Grids in memory are indexed x*H+z (x-major); on disk rows are
// stored last-row-first (row = x), see flipRows().
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/ini.h"

namespace m3k { namespace us {

// ---- Ordinance IDs (spec 19 section 9.5) ----
namespace ord {
constexpr uint32_t YouthCurfew = 0xC2BF1DC5, Gambling = 0xA0D07129, JrSports = 0x815B4CEF,
                   NeighborhoodWatch = 0x62BF1DB9, ConservationCorps = 0xA2F6E7DA,
                   ParkingFine = 0x015B4D1E, AltDriving = 0xE2BF1E2A, NukeFree = 0xE0D0722E,
                   PowerConservation = 0x82B9999B, StairwellLighting = 0x82E42E30,
                   // Spec 16 §3.4 / 19 §9.5 / Q-BX [STATIC]: water demand ordinances.
                   WaterConservation = 0x02BF1DFA, WaterMeters = 0xC2F90EB1,
                   CleanAir = 0xA2BF1E43, TrashPresort = 0x42BF1E18, IndPolluteFee = 0x02F6E7E2,
                   LawnChemBan = 0x62F6E808, TireRecycling = 0xC2F6E81F,
                   LeafBurningBan = 0x22BF1E35, BackyardComposting = 0x22F6E814,
                   PaperReduction = 0x22F6E81B, IndWasteDisposal = 0xA2F917CF,
                   LandfillGasRecovery = 0x22F6E80C, CarSmogging = 0xC2BF1E04,
                   SmokeDetectors = 0x62BF1DAA;
}

struct OrdinanceSet {
  std::set<uint32_t> enacted;
  bool active(uint32_t id) const { return enacted.count(id) != 0; }
  void set(uint32_t id, bool on) { if (on) enacted.insert(id); else enacted.erase(id); }
};

// ---- Random generators ----
// Simulation generator (spec 16 section 1.1). Two independent 32-bit states.
class SimRng {
 public:
  explicit SimRng(uint32_t seed = 1) : s_(seed), t_(seed) {}
  uint32_t rand(uint32_t n);                 // spec 1.1 Rand(n), incl. the inclusive >=0x00FFFFFF branch
  uint32_t randInt(uint32_t a, uint32_t b) { return a + rand(b - a); }  // [a, b-1]
  double randUnit();                         // [0,1)
  double randReal(double a, double b) { return a + (b - a) * randUnit(); }
  uint32_t stateS() const { return s_; }
 private:
  uint32_t s_, t_;
};

// Microsoft C runtime rand(): s = s*214013+2531011; (s>>16)&0x7FFF (spec 17 section 7.1.4).
class MsRand {
 public:
  explicit MsRand(uint32_t seed = 1) : s_(seed) {}
  int next() { s_ = s_ * 214013u + 2531011u; return int((s_ >> 16) & 0x7FFF); }
 private:
  uint32_t s_;
};

// ---- Agent type hierarchy (CitySim.ini [AgentTypeHierarchy], child=parent) ----
class AgentHierarchy {
 public:
  void set(uint32_t child, uint32_t parent) { parent_[child] = parent; }
  void loadIni(const IniFile& citySim);
  // Spec 13 §4.1 / Q-BO [STATIC]: parent-step count from `type` to ancestor `anc` (0 if equal);
  // -1 if `anc` is not an ancestor.
  int distance(uint32_t type, uint32_t anc) const;
  bool isAncestorOrSelf(uint32_t type, uint32_t anc) const { return distance(type, anc) >= 0; }
  bool parentOf(uint32_t type, uint32_t& parent) const { auto it = parent_.find(type); if (it == parent_.end()) return false; parent = it->second; return true; }
  bool empty() const { return parent_.empty(); }
  // `type` followed by its ancestors, nearest first (stops at a self-parent, a cycle, or after 64 steps).
  std::vector<uint32_t> chain(uint32_t type) const {
    std::vector<uint32_t> c{type};
    for (int d = 0; d < 64; d++) {
      auto it = parent_.find(c.back());
      if (it == parent_.end() || it->second == c.back()) break;
      // Cycle guard: CitySim.ini has a 0↔52 loop in the shipped hierarchy.
      bool seen = false;
      for (uint32_t x : c) if (x == it->second) { seen = true; break; }
      if (seen) break;
      c.push_back(it->second);
    }
    return c;
  }
  // Every type listed in [AgentTypeHierarchy], ascending ID.
  std::vector<uint32_t> types() const { std::vector<uint32_t> v; for (const auto& kv : parent_) v.push_back(kv.first); return v; }
 private:
  std::map<uint32_t, uint32_t> parent_;
};

// ---- INI helpers ----
// Decimal signed parse stopping at first invalid char (like %d). Returns dflt if string has no digits.
int64_t parseDecimal(const std::string& s, int64_t dflt = 0);
// Aura-style quirky parse (spec 17 section 8.1): hex if 0x prefix or any a-f anywhere; leading '-'
// allowed; result wraps modulo 2^32.
uint32_t parseQuirky(const std::string& s);
// Parse "a<sep>b..." decimal fields with sscanf %d semantics. Returns number of fields parsed.
int parseFields(const std::string& s, const char* fmt, int* out, int maxOut);

struct IniReader {
  const IniFile* f = nullptr;
  int64_t num(const std::string& sec, const std::string& key, int64_t dflt) const;
  double real(const std::string& sec, const std::string& key, double dflt) const;
  uint32_t quirky(const std::string& sec, const std::string& key, uint32_t dflt) const;
  bool has(const std::string& sec, const std::string& key) const;
};

// ---- 132-byte history chart (spec 10 §6.12 / 17 §8.6 / Q-BL) ----
struct Chart {
  uint32_t monthly[12] = {}, yearly[10] = {}, decade[10] = {}, max = 0;
  static Chart fresh(uint32_t initial, uint32_t scale);
  // Spec 10 §6.12 [STATIC Q-BL]: index 0 = newest; new sample inserts at 0, oldest drops off the end.
  void pushMonthly(uint32_t v);
  void pushYearly(uint32_t v);
  void pushDecade(uint32_t v);
  void recomputeAutoMax(uint32_t floor = 10);  // max = max(largest sample, floor)
  void parse(Reader& r);
  void write(Bytes& out) const;
  bool operator==(const Chart& o) const;
};

// ---- Record plumbing ----
// 8-byte versioned header (spec 10 section 2.1). flags bit0 = marker strings.
struct RecHeader { uint16_t version = 0; uint8_t flags = 2; };
RecHeader readRecHeader(Reader& r);               // throws FormatError if sentinel missing
void writeRecHeader(Bytes& out, uint16_t version, uint8_t flags = 2);
void putStr(Bytes& out, const std::string& s);    // u32 length + bytes
void expectStr(Reader& r, const std::string& s);  // reads length-prefixed string, throws if different
void put64(Bytes& out, int64_t v);
int64_t rd64(Reader& r);
void putS32(Bytes& out, int32_t v);
// Swap rows of an R x C grid (row = first coordinate). Self-inverse. Converts memory <-> file order.
template <class T>
std::vector<T> flipRows(const std::vector<T>& g, int R, int C) {
  std::vector<T> o(g.size());
  for (int x = 0; x < R; x++)
    for (int z = 0; z < C; z++) o[size_t(R - 1 - x) * C + z] = g[size_t(x) * C + z];
  return o;
}

// Sweep-order block (spec 10 section 6.10): permutation of cells, high byte x, low byte z.
struct SweepOrder {
  uint8_t state = 1;     // 1 running, 2 exhausted
  uint32_t mode = 0;
  uint8_t rebuild = 1;
  int32_t xMin = 0, xMax = 0, zMin = 0, zMax = 0, total = 0;
  std::vector<uint16_t> list;   // logical order (file stores it reversed)
  int32_t index = 1, stridePos = 0, stride = 1;
  void init(int nx, int nz);    // row order (x outer, z inner), bounds for an nx x nz cell grid
  void shuffle(MsRand& r);      // forward Fisher-Yates with rand() % (i+1)
  void parse(Reader& r);
  void write(Bytes& out) const;
  bool operator==(const SweepOrder& o) const;
};

// ---- Abstract occupant view shared by all layers ----
struct SimOccupant {
  uint32_t id = 0;
  uint32_t agentType = 0;
  uint32_t group = 0;
  int x = 0, z = 0;           // anchor (min corner)
  int sizeX = 1, sizeZ = 1;   // footprint in tiles
  bool hasState = false;      // has lot state word
  uint32_t state = 1;         // 1 occupied/operational
  int buildAbsMonth = 0;      // 12*year+month at construction
  int flammability = 0;       // attribute 0x6E, 0 = absent
  int powerUse = -1;          // attribute 0xC9 candidate; -1 = unset -> default rule
  int densityClass = 1;       // multiplier for the default power rule (spec 29 item 2.8)
  int occupantKind = 1;       // spec 13 Occupant kind / aura §8.4; 1 = building/facility (Q-BH)
  int x1() const { return x + sizeX - 1; }
  int z1() const { return z + sizeZ - 1; }
};

inline int clampi(int64_t v, int lo, int hi) { return int(v < lo ? lo : (v > hi ? hi : v)); }
inline int64_t truncDiv(int64_t a, int64_t b) { return b ? a / b : 0; }  // C++ division truncates

}}  // namespace m3k::us
