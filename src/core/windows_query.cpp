// Query tool popups (spec 09 section 6.12): line builder for every occupant family plus the popup window.
#include <algorithm>
#include "core/windows.h"

namespace m3k {

using namespace wui;

uint32_t landValueBandId(int v) { return v < 35 ? 0x1C0 : v < 75 ? 9 : v < 130 ? 0xA : v < 175 ? 0xB : v < 225 ? 0xC : 0xD; }
uint32_t crimeBandId(int c) { return c <= 0 ? 0x1C : c < 47 ? 0x1D : c < 84 ? 0x1E : c < 122 ? 0x1F : c <= 150 ? 0x20 : 0x21; }
// [OPEN] split between "very high" (27) and "extreme" (28): 220 assumed.
uint32_t flammabilityBandId(int f) { return f <= 0 ? 0x23 : f < 72 ? 0x24 : f < 129 ? 0x25 : f < 187 ? 0x26 : f < 220 ? 0x27 : 0x28; }
uint32_t pollutionBandId(int p, int b) {
  if (b < 1) b = 1;
  if (p <= 0) return 0x2A;
  if (p < b / 5) return 0x2B;
  if (p < 5 * b) return 0x2C;
  if (p < 10 * b) return 0x2D;
  if (p < 50 * b) return 0x2E;
  return 0x2F;
}

namespace {

const char* const kG4[] = {"Poor", "Fair", "Good", "Excellent"};
const char* const kG5[] = {"Poor", "Fair", "Average", "Good", "Excellent"};
const char* const kU4[] = {"Low", "Moderate", "High", "Very high"};
const char* const kU5[] = {"Very low", "Low", "Moderate", "High", "Very high"};
const char* const kU6[] = {"None", "Very low", "Low", "Moderate", "High", "Very high"};
const char* const kLV[] = {"Very low", "Low", "Medium", "High", "Very high", "Excellent"};

enum Kind { Num, Band, Bool, Text, Year, Money };
struct Spec {
  Kind kind;
  uint32_t label;          // label string id
  const char* fbLabel;
  uint32_t value;          // Num/Bool: template id (Bool: id of the "yes" string, "no" = +1); Band: first band id
  const char* fbValue;     // Num template or nullptr
  int slot;                // Num/Money/Year/Bool: v[slot]; Band: band[slot]
  int bands;               // Band: number of bands
  const char* const* fbBands;
};

std::string fmtNum(double v) {
  long long n = (long long)(v < 0 ? v - 0.5 : v + 0.5);
  return groupDigits(n);
}

const std::vector<Spec>& specsFor(QueryClass c) {
  static const std::map<QueryClass, std::vector<Spec>> t = {
    {QueryClass::Police, {{Num, 0x88, "Crimes per month:", 0x89, "%s", 0, 0, nullptr}, {Num, 0x8A, "Arrests per month:", 0x8B, "%s", 1, 0, nullptr},
                          {Band, 0x8C, "Rating:", 0x8D, nullptr, 0, 4, kG4}}},
    {QueryClass::Jail, {{Num, 0x93, "Inmates:", 0x94, "%s", 0, 0, nullptr}, {Num, 0x95, "Cells:", 0x96, "%s", 1, 0, nullptr},
                        {Band, 0x97, "Conditions:", 0x98, nullptr, 0, 4, kG4}}},
    {QueryClass::Fire, {{Num, 0x9C, "Fires per month:", 0x9D, "%s", 0, 0, nullptr}, {Num, 0x9E, "Calls per month:", 0x9F, "%s", 1, 0, nullptr},
                        {Band, 0xA0, "Rating:", 0xA1, nullptr, 0, 4, kG4}}},
    {QueryClass::Hospital, {{Num, 0xA7, "Beds:", 0xA8, "%s", 0, 0, nullptr}, {Num, 0xA9, "Patients:", 0xAA, "%s", 1, 0, nullptr},
                            {Num, 0xAB, "Doctors:", 0xAC, "%s", 2, 0, nullptr}, {Band, 0xAD, "Grade:", 0xAE, nullptr, 0, 5, kG5}}},
    {QueryClass::School, {{Num, 0xB3, "Desks:", 0xB4, "%s", 0, 0, nullptr}, {Num, 0xB5, "Students:", 0xB6, "%s", 1, 0, nullptr},
                          {Num, 0xB7, "Teachers:", 0xB8, "%s", 2, 0, nullptr}, {Band, 0xB9, "Grade:", 0xBA, nullptr, 0, 5, kG5}}},
    {QueryClass::College, {{Num, 0xC8, "Capacity:", 0xC9, "%s", 0, 0, nullptr}, {Num, 0xCA, "Students:", 0xCB, "%s", 1, 0, nullptr},
                           {Num, 0xCC, "Professors:", 0xCD, "%s", 2, 0, nullptr}, {Band, 0xCE, "Grade:", 0xCF, nullptr, 0, 5, kG5}}},
    {QueryClass::Library, {{Num, 0xD4, "Books:", 0xD5, "%s", 0, 0, nullptr}, {Num, 0xD6, "Books in circulation:", 0xD7, "%s", 1, 0, nullptr},
                           {Band, 0xD8, "Grade:", 0xD9, nullptr, 0, 5, kG5}}},
    {QueryClass::Museum, {{Num, 0xDE, "Attendance last year:", 0xDF, "%s", 0, 0, nullptr}, {Band, 0xE4, "Grade:", 0xE5, nullptr, 0, 5, kG5}}},
    {QueryClass::Park, {{Num, 0xEA, "Acres of parks:", 0xEB, "%s", 0, 0, nullptr}, {Num, 0xEC, "Benches:", 0xED, "%s", 1, 0, nullptr}}},
    {QueryClass::Zoo, {{Num, 0xEE, "Animals:", 0xEF, "%s", 0, 0, nullptr}, {Num, 0xF0, "Visitors:", 0xF1, "%s", 1, 0, nullptr}}},
    {QueryClass::Ballpark, {{Num, 0xF2, "Wins:", 0xF3, "%s", 0, 0, nullptr}, {Num, 0x110, "Losses:", 0x111, "%s", 1, 0, nullptr}}},
    {QueryClass::Marina, {{Num, 0xF4, "Boats:", 0xF5, "%s", 0, 0, nullptr}}},
    {QueryClass::Landmark, {{Year, 0xF6, "Date built:", 0xF7, "%s", 0, 0, nullptr}, {Money, 0x118, "Monthly income:", 0x119, "%s", 1, 0, nullptr}}},
    {QueryClass::Road, {{Band, 0x3C, "Traffic:", 0x3D, nullptr, 0, 5, kU5}}},
    {QueryClass::Rail, {{Band, 0x43, "Rail usage:", 0x44, nullptr, 0, 5, kU5}}},
    {QueryClass::Subway, {{Band, 0x49, "Subway usage:", 0x4A, nullptr, 0, 5, kU5}}},
    {QueryClass::Station, {{Num, 0x37, "Passengers per day:", 0x38, "%s", 0, 0, nullptr}, {Band, 0x181, "Service quality:", 0x182, nullptr, 0, 4, kG4}}},
    {QueryClass::Airport, {{Band, 0xFE, "Air traffic:", 0xFF, nullptr, 0, 4, kU4}, {Num, 0x103, "On-time flights:", 0x104, "%s%%", 0, 0, nullptr}}},
    {QueryClass::Seaport, {{Band, 0x105, "Port traffic:", 0x106, nullptr, 0, 4, kU4}}},
    {QueryClass::PowerPlant, {{Num, 0x198, "Maximum capacity:", 0x1B7, "%s", 0, 0, nullptr}, {Num, 0x56, "Current capacity:", 0x57, "%s", 1, 0, nullptr},
                              {Band, 0x58, "Usage:", 0x59, nullptr, 0, 5, kU5}, {Num, 0x1B5, "Age (years):", 0x1B6, "%s", 2, 0, nullptr}}},
    {QueryClass::WaterBuilding, {{Num, 0x180, "Maximum capacity:", 0, "%s", 0, 0, nullptr}, {Num, 0x5E, "Current capacity:", 0x5F, "%s", 1, 0, nullptr},
                                 {Band, 0x6B, "Usage:", 0x6C, nullptr, 0, 5, kU5}, {Bool, 0x73, "Connected to pipes:", 0x74, nullptr, 2, 0, nullptr},
                                 {Num, 0x1B5, "Age (years):", 0x1B6, "%s", 3, 0, nullptr}}},
    {QueryClass::WaterTreatment, {{Num, 0x1A2, "Capacity:", 0x1A3, "%s", 0, 0, nullptr}, {Band, 0x1A5, "Usage:", 0x1A6, nullptr, 0, 5, kU5}}},
    {QueryClass::Garbage, {{Num, 0x1AB, "Maximum disposal:", 0, "%s", 0, 0, nullptr}, {Num, 0x76, "Current disposal:", 0x77, "%s", 1, 0, nullptr},
                           {Band, 0x78, "Usage:", 0x79, nullptr, 0, 5, kU5}}},
    {QueryClass::Recycling, {{Num, 0x1AE, "Citywide recycling:", 0x1AF, "%s%%", 0, 0, nullptr}}},
    {QueryClass::NeighbourConn, {{Text, 0x54, "Connected to:", 0x55, nullptr, 0, 0, nullptr}}},
  };
  static const std::vector<Spec> none;
  auto it = t.find(c);
  return it == t.end() ? none : it->second;
}

std::string zoneName(const WinEnv& E, int code) {
  struct Z { int code; uint32_t id; const char* fb; };
  static const Z z[] = {{1, 0xF, "Residential, low density"}, {2, 0x10, "Residential, medium density"}, {3, 0x11, "Residential, high density"},
                        {5, 0x12, "Commercial, low density"}, {6, 0x13, "Commercial, medium density"}, {7, 0x14, "Commercial, high density"},
                        {9, 0x15, "Industrial, low density"}, {10, 0x16, "Industrial, medium density"}, {11, 0x17, "Industrial, high density"},
                        {15, 0x18, "Seaport"}, {14, 0x19, "Airport"}, {17, 0x1A, "Landfill"}};
  for (auto& e : z) if (e.code == code) return E.query(e.id, e.fb);
  return E.query(0x195, "Unzoned");
}

std::string band(const WinEnv& E, uint32_t first, int idx, const char* fb) { return E.query(first + uint32_t(idx), fb); }

}  // namespace

std::vector<QueryLine> buildQueryLines(const WinEnv& E, const QueryInfo& q) {
  std::vector<QueryLine> L;
  auto num = [&](uint32_t id, const char* fbTpl, double v) { return subst(E.query(id, fbTpl), {fmtNum(v)}); };
  // 1. terrain
  if (q.waterDepth > 0) L.push_back({E.query(0x17F, "Water depth:"), num(3, "%s m", q.waterDepth)});
  else L.push_back({E.query(2, "Elevation:"), num(3, "%s m", q.elevation)});
  if (q.name.empty() && q.cls == QueryClass::None) {
    if (q.waterDepth > 0) L.push_back({"", q.saltWater ? E.query(0xC3, "Salt water") : E.query(0xC2, "Fresh water")});
    else L.push_back({"", E.query(0xC1, "Dirt")});
  }
  // 2. zone / land value
  if (q.zoneCode || q.cls == QueryClass::Zoned || q.name.empty()) L.push_back({E.query(0xE, "Zone:"), zoneName(E, q.zoneCode)});
  if (q.landValue >= 0) {
    int idx = q.landValue < 35 ? 0 : q.landValue < 75 ? 1 : q.landValue < 130 ? 2 : q.landValue < 175 ? 3 : q.landValue < 225 ? 4 : 5;
    L.push_back({E.query(8, "Land value:"), E.query(landValueBandId(q.landValue), kLV[idx])});
  }
  // 3. occupant
  if (!q.name.empty()) {
    std::string n = q.name;
    struct S { uint32_t bit, id; const char* fb; };
    static const S st[] = {{QsConstruction, 0xC4, "under construction"}, {QsAbandoned, 0xC5, "abandoned"}, {QsGarbage, 0xC6, "covered in garbage"},
                           {QsDestroyed, 0xC7, "destroyed"}, {QsStrike, 0x186, "workers on strike"}, {QsAging, 0x196, "aging"},
                           {QsDefunct, 0x197, "defunct"}, {QsDecommissioned, 0x1B4, "decommissioned"}, {QsNoAccess, 0x1B8, "no access to city"}};
    for (auto& s : st) if (q.status & s.bit) n += " (" + E.query(s.id, s.fb) + ")";
    L.push_back({"", n});
  }
  for (const Spec& s : specsFor(q.cls)) {
    switch (s.kind) {
      case Num: L.push_back({E.query(s.label, s.fbLabel), num(s.value, s.fbValue, q.v[s.slot])}); break;
      case Money: L.push_back({E.query(s.label, s.fbLabel), subst(E.query(s.value, s.fbValue), {E.money(q.monthlyIncome)})}); break;
      case Year: L.push_back({E.query(s.label, s.fbLabel), subst(E.query(s.value, s.fbValue), {std::to_string(q.builtYear)})}); break;
      case Bool: L.push_back({E.query(s.label, s.fbLabel), q.v[s.slot] > 0 ? E.query(s.value, "Yes") : E.query(s.value + 1, "No")}); break;
      case Text: L.push_back({E.query(s.label, s.fbLabel), q.neighbour.empty() ? E.query(s.value, "-") : q.neighbour}); break;
      case Band: {
        int n = s.bands, idx = std::min(n - 1, int(q.band[s.slot]) * n / 256);
        L.push_back({E.query(s.label, s.fbLabel), band(E, s.value, idx, s.fbBands[idx])});
        break;
      }
    }
  }
  for (int i = 0; i < 2; i++)
    if (q.flavour[i]) { std::string f = E.query(uint32_t(q.flavour[i]), ""); if (!f.empty()) L.push_back({"", f}); }
  // 4. common status lines
  if (q.showPower) L.push_back({E.query(4, "Power:"), q.powered ? E.query(5, "Powered") : E.query(6, "Not powered")});
  if (q.crime >= 0) {
    int idx = q.crime <= 0 ? 0 : q.crime < 47 ? 1 : q.crime < 84 ? 2 : q.crime < 122 ? 3 : q.crime <= 150 ? 4 : 5;
    L.push_back({E.query(0x1B, "Crime:"), E.query(crimeBandId(q.crime), kU6[idx])});
  }
  if (q.flammability >= 0) {
    uint32_t id = flammabilityBandId(q.flammability);
    static const char* fb[] = {"None", "Low", "Moderate", "High", "Very high", "Extreme"};
    L.push_back({E.query(0x22, "Flammability:"), E.query(id, fb[id - 0x23])});
  }
  if (q.pollution >= 0) {
    uint32_t id = pollutionBandId(q.pollution, q.pollutionBaseline);
    static const char* fb[] = {"None", "Very low", "Low", "Moderate", "High", "Very high"};
    L.push_back({E.query(0x29, "Pollution generated:"), E.query(id, fb[id - 0x2A])});
  }
  if (q.airPollution >= 0) { int i = std::min(5, q.airPollution * 6 / 256); L.push_back({E.query(0x187, "Air pollution:"), band(E, 0x188, i, kU6[i])}); }
  if (q.waterPollution >= 0) { int i = std::min(5, q.waterPollution * 6 / 256); L.push_back({E.query(0x18E, "Water pollution:"), band(E, 0x18F, i, kU6[i])}); }
  // Spec 16 §4.1 / 17 §7.2.2 / Q-BT [STATIC]: trunc(G × s). Value template is query 0x1BF ("%s").
  // Label ID [OPEN]: query table 82E0074C has Air 0x187 / Water 0x18E only — no Garbage Pollution label.
  if (q.garbageScaled >= 0) L.push_back({E.query(0x2A4, "Garbage pollution:"), num(0x1BF, "%s", q.garbageScaled)});
  if (q.cleanupCost >= 0) L.push_back({E.query(0x31, "Clean-up cost:"), subst(E.query(0x32, "%s"), {E.money(q.cleanupCost)})});
  if (q.fundingCost >= 0) L.push_back({E.query(0x86, "Funding cost:"), subst(E.query(0x87, "%s per month"), {E.money(q.fundingCost)})});
  return L;
}

WindowResult QueryWindow::draw(Ui& ui, const WinEnv& E, const QueryInfo& q) {
  WindowResult R;
  auto lines = buildQueryLines(E, q);
  int lh = lineHeight(ui);
  int w = 300, h = 30 + int(lines.size()) * lh + 10 + (q.growable ? 28 : 0) + 6;
  Rect r = placed ? rect : Rect{(ui.W - w) / 2, (ui.H - h) / 2, w, h};
  r.w = w; r.h = h;
  if (r.x + r.w > ui.W) r.x = ui.W - r.w;
  if (r.y + r.h > ui.H) r.y = ui.H - r.h;
  if (r.x < 0) r.x = 0;
  if (r.y < 0) r.y = 0;
  rect = r; placed = true;   // home position; the drawn rect adds any title-bar drag
  r = ui.movable(r, Ui::windowKey("query"));
  ui.windowRects.push_back(r);
  if (frame(ui, E, r, E.query(1, "Query"))) { R.close = true; R.add(Act::Close); }
  int y = r.y + 30;
  for (auto& l : lines) {
    if (l.label.empty()) ui.text(r.x + 10, y, l.value, "main10", rgb(0x20, 0x20, 0x70));
    else labelValue(ui, r.x + 10, y, r.w - 20, l.label, l.value);
    y += lh;
  }
  if (q.growable) {
    std::string lbl = E.query(0x30, "Make historical");
    if (button(ui, E, "historical", {r.x + 10, y + 6, r.w - 20, 22}, lbl, true, q.historical)) R.add(Act::ToggleHistorical, 0, q.x, q.z);
  }
  return R;
}

}  // namespace m3k
