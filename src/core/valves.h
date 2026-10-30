// RCI demand ("valve") layer, world economy, tax effect and the persisted growth records
// (spec/15-sim-growth.md section 3; spec/10-save-format.md sections 6.7, 6.11, 6.13).
// Standalone: takes an abstract per-month input struct, does not touch World/Sim.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/ini.h"

namespace m3k {
namespace growth {

constexpr int kValveRes = 3000, kValveResFun = 3001, kValveCom = 5000, kValveComConn = 5001,
              kValveJobs = 7000, kValveInd = 8000, kValveIndConn = 8001;

// 132-byte history chart (spec 10 section 6.19): u32[12] monthly, u32[10] yearly, u32[10] decade, u32 max.
struct ChartBlock {
  std::array<uint32_t, 33> w{};
  static ChartBlock filled(uint32_t sample, uint32_t scale) { ChartBlock c; for (int i = 0; i < 32; i++) c.w[i] = sample; c.w[32] = scale; return c; }
  void read(Reader& r) { for (auto& v : w) v = r.u32(); }
  void write(Bytes& b) const { for (auto v : w) put32(b, v); }
};

// Curve interpolation shared by the economic phases and the land-value city-centre curves
// (spec 15 section 6.2). Points are (population/1000, value).
struct CurvePoint { int x = 0, y = 0; };
int curveValue(const std::vector<CurvePoint>& pts, int64_t population);

// One valve (spec 15 section 3.2). Serialises as the 41-byte block of spec 10 section 6.11.
struct Valve {
  uint32_t id = 0;
  int32_t Slast = 0, Dlast = 0, Sacc = 0, Dacc = 0;
  float f1 = 0, f2 = 0;
  uint8_t capPct = 0;
  int32_t V = 0, Sbase = 0, Dbase = 0;
  // Not saved:
  int32_t Max = 2000, Min = -2000;
  bool smooth = false;
  int32_t N = 0;  // normalised reading, -1000..1000 (published by update())

  void setMax(int64_t m) { Max = int32_t(m < Min ? Min : (m > INT32_MAX ? INT32_MAX : m)); }
  void setMin(int64_t m) { Min = int32_t(m > Max ? Max : (m < INT32_MIN ? INT32_MIN : m)); }
  void setLimits(int64_t mx, int64_t mn) { Max = int32_t(mx > INT32_MAX ? INT32_MAX : mx); Min = int32_t(mn < INT32_MIN ? INT32_MIN : mn); setMax(Max); setMin(Min); }
  void update();  // monthly update, spec 15 section 3.2 steps 1-6
  void write(Bytes& b) const;
  static Valve read(Reader& r);
};

struct ValveItem { int valve = 0, supply = 0, demand = 0; };
struct OrdinanceValveEffect { int valve = 0, permille = 0; };

// Tunables of the valve layer (all INI-sourced; defaults are neutral).
struct ValveTuning {
  std::map<uint32_t, std::vector<ValveItem>> agentEffects;           // [AgentValveEffects]
  std::map<int, std::vector<ValveItem>> connectionEffects;           // [ConnectionValveEffects], kind 0..3
  std::map<uint32_t, std::vector<OrdinanceValveEffect>> ordinanceEffects;  // [OrdinanceEffects]
  int jobToResFactor = 1;      // 0 treated as 1
  int externalJobDemand = 0;
  // New-city bootstrap (not in the original's INIs, see spec 15 3.4 [OPEN]): while the city has no residents,
  // this much residential demand is added each cycle so the first houses can be built.
  int bootstrapResDemand = 0;
  std::vector<CurvePoint> phaseA, phaseB;  // EconomicPhases: pop/1000 -> a, b
  struct Cycle { int duration = 0, pct = 0; };
  std::map<int, std::vector<Cycle>> worldCycles;  // [ListOfWorldValveMapping]
  int externalFunSupply = 0, comInitialConnection = 0, indInitialConnection = 0;
  struct TaxBreak { int pop = 0; std::array<float, 23> e{}; };
  std::vector<TaxBreak> taxTable;  // sorted by pop

  void parseAgentValveEffects(const IniFile& ini);   // SC3ValveLayer.ini
  void parseConnectionEffects(const IniFile& ini);
  void parseOrdinanceEffects(const IniFile& ini);
  void parseWorldLayer(const IniFile& ini);          // SC3WorldLayer.ini
  void parseResLayer(const IniFile& ini) ;           // SC3ResLayer.ini ExternalFunSupply
  void parseComLayer(const IniFile& ini);
  void parseIndLayer(const IniFile& ini);
  void parseTaxation(const std::string& taxationDemandEffect);  // SC3BudgetLayer.ini [TuningParameters]
  // Text helpers (exposed for tests)
  static std::vector<ValveItem> parseItems(const std::string& text);
  void parseEconomicPhases(const std::string& text);
  void parseWorldMapping(int valveId, const std::string& text);

  // Interpolated tax-effect table for a population, with the original's defect (spec 19 section 4.2).
  bool taxEffects(int64_t pop, std::array<float, 23>& out) const;
};

// One occupant as seen by the valve layer.
struct ValveBuilding {
  uint32_t agentType = 0;
  int x = 0, z = 0, sizeX = 1, sizeZ = 1;  // footprint from (x,z)
  int serviceMul = 1;      // 0/1 service multiplier for valve sums (not for the unit maps)
  bool operating = true;   // false for abandoned / construction / damaged: contributes nothing
  int family = 0;          // 0 = derive from agentType (3xxx -> 3000, 5xxx -> 5000, 8xxx -> 8000)
};

struct ValveCityInput {
  std::vector<ValveBuilding> buildings;
  int connectionCount[4] = {0, 0, 0, 0};  // road, rail, highway, subway
  std::set<uint32_t> enactedOrdinances;
  double workforceFraction = 0;  // 'w' of spec 15 section 3.4
  int64_t population = 0;
  int year = 1900, month = 1;    // month 1..12
  int taxRate[3] = {7, 7, 7};    // R, C, I
};

// Layers owned by growth: R/C/I valves, jobs valve, world layer state, RCI development layer.
class ValveLayer {
 public:
  void init(int W, int H, const ValveTuning& t);  // new-city state
  void setTuning(const ValveTuning& t) { tune_ = t; applyLimits(); }
  const ValveTuning& tuning() const { return tune_; }
  void applyLimits();  // sets Max/Min per spec table (limits are not saved)

  Valve& valve(int id);
  const Valve& valve(int id) const;
  // Normalised demand reading (-1000..1000) of a valve after the last update.
  int reading(int id) const { return valve(id).N; }

  // Monthly work (day 1, first of the month-day list). Adds contributions, then updates every valve.
  void monthlyUpdate(const ValveCityInput& in);

  // Individual steps (exposed for tests / custom scheduling)
  void addWorkforce(int64_t n);                 // jobs supply + C/I demand split
  void addJobsDemand(int64_t n);                // jobs demand + residents demand
  void addBuilding(const ValveBuilding& b);
  void setPhase(int64_t population);
  void setWorldModifiers(int year, int month);
  void setTaxModifiers(int64_t pop, const int rates[3]);
  int phaseA() const { return phaseA_; }
  int phaseB() const { return phaseB_; }

  // ---- persisted records ----
  // 206C6E7C:C106C4F5 (R, v3), 20EC9849 (C, v2), A106CF3D (I, v2): return false when absent/unparsable.
  bool parseResidential(const uint8_t* p, size_t n);
  bool parseCommercial(const uint8_t* p, size_t n);
  bool parseIndustrial(const uint8_t* p, size_t n);
  Bytes writeResidential(bool markers = false) const;
  Bytes writeCommercial(bool markers = false) const;
  Bytes writeIndustrial(bool markers = false) const;
  // Residential population state: opaque 1900 bytes owned by spec 20, preserved verbatim.
  std::vector<uint8_t> residentialState;

  // 206C6E7C:80F1E6D3 (RCI development layer, v2)
  bool parseRci(const uint8_t* p, size_t n);
  Bytes writeRci(bool markers = false) const;
  int W = 0, H = 0;
  std::vector<uint8_t> effectMap, effectAcc;  // tile-major: index x*H + z
  uint32_t resUnits = 0, comUnits = 0, indUnits = 0, resAcc = 0, comAcc = 0, indAcc = 0;

  // 206C6E7C:E11BDDF6 (world layer, v2)
  struct WorldRec {
    int32_t v[9] = {0, 0, 150, 0, 0, 0, 0, 0, 0};  // v0..v8 (0x08..0x2B)
    int32_t phaseParams[4] = {0, 0, 0, 0};
    uint8_t flag = 0;
    ChartBlock charts[5];
    uint32_t last = 0;
    bool hadV0 = true;  // version 1 lacks v0; kept so it can be reported
  } world;
  bool parseWorld(const uint8_t* p, size_t n);
  Bytes writeWorld(bool markers = false) const;

  Valve v3000, v3001, v5000, v5001, v8000, v8001, v7000;

 private:
  void finishCycle();
  void startJobsCycle();
  ValveTuning tune_;
  int phaseA_ = 1, phaseB_ = 1;
  int64_t sumC_ = 0, sumI_ = 0;
  double wf_ = 0;  // current workforce fraction
};

// Defaults for new-city records (replaceable by templates, see records.h).
Bytes defaultWorldRecord();

}  // namespace growth
}  // namespace m3k
