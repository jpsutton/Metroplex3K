// Query tool popups (spec 09 section 6.12): line builder for every occupant family plus the popup window.
#include <algorithm>
#include "core/skin.h"
#include "core/windows.h"

namespace m3k {

using namespace wui;

uint32_t landValueBandId(int v) { return v < 35 ? 0x1C0 : v < 75 ? 9 : v < 130 ? 0xA : v < 175 ? 0xB : v < 225 ? 0xC : 0xD; }
uint32_t crimeBandId(int c) { return c <= 0 ? 0x1C : c < 47 ? 0x1D : c < 84 ? 0x1E : c < 122 ? 0x1F : c <= 150 ? 0x20 : 0x21; }
// Spec 09 6.12: 187..229 -> very high (27), above 229 -> extreme (28, drawn red).
uint32_t flammabilityBandId(int f) { return f <= 0 ? 0x23 : f < 72 ? 0x24 : f < 129 ? 0x25 : f < 187 ? 0x26 : f <= 229 ? 0x27 : 0x28; }
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
const char* const kU6[] = {"None", "Low", "Medium", "High", "Very High", "Hazardous"};
const char* const kCrime[] = {"None", "Low", "Medium", "High", "Very High", "Rampant"};
const char* const kGrade[] = {"A", "B", "C", "D", "F"};
const char* const kEff[] = {"None", "Low", "Medium", "High", "Unusable"};
const char* const kRec[] = {"Adequate", "Inadequate"};
const char* const kLV[] = {"Very Low", "Low", "Medium", "High", "Very High", "Astronomical"};

enum Kind { Num, Band, Bool, Text, Year, Money, Pair };
struct Spec {
  Kind kind;
  uint32_t label;          // label string id
  const char* fbLabel;
  uint32_t value;          // Num/Bool: template id (Bool: id of the "yes" string, "no" = +1); Band: first band id
  const char* fbValue;     // Num template or nullptr
  int slot;                // Num/Money/Year/Bool: v[slot]; Band: band[slot]
  int bands;               // Band: number of bands
  const char* const* fbBands;
  bool redZero = false;    // Num: the value is drawn red when it is 0 (a utility's current capacity)
  unsigned redMask = 0;    // Band: bit i = band index i is drawn red ("Inadequate", grade D / F, "Poor")
  bool hideZero = false;   // Num / Money: the row is omitted when the value is 0
};

std::string fmtNum(double v) {
  long long n = (long long)(v < 0 ? v - 0.5 : v + 0.5);
  return groupDigits(n);
}

const std::vector<Spec>& specsFor(QueryClass c) {
  static const std::map<QueryClass, std::vector<Spec>> t = {
    {QueryClass::Police, {{Num, 0x88, "Number of Crimes:", 0x89, "%s/Month", 0, 0, nullptr}, {Band, 0x8C, "Performance Rating:", 0x8D, nullptr, 0, 4, kG4, false, 9u},
                          {Num, 0x8A, "Number of Arrests:", 0x8B, "%s/Month", 1, 0, nullptr}}},
    {QueryClass::Jail, {{Num, 0x93, "Inmates:", 0x94, "%s", 0, 0, nullptr}, {Num, 0x95, "Cells:", 0x96, "%s", 1, 0, nullptr},
                        {Band, 0x97, "Conditions:", 0x98, nullptr, 0, 4, kG4}}},
    {QueryClass::Fire, {{Num, 0x9C, "Number of Fires:", 0x9D, "%s/Month", 0, 0, nullptr}, {Num, 0x9E, "Number of Calls:", 0x9F, "%s/Month", 1, 0, nullptr},
                        {Band, 0xA0, "Performance Rating:", 0xA1, nullptr, 0, 4, kG4, false, 1u}}},
    {QueryClass::Hospital, {{Num, 0xA7, "Number of Beds (Systemwide):", 0xA8, "%s", 0, 0, nullptr}, {Num, 0xA9, "Number of Patients (Systemwide):", 0xAA, "%s", 1, 0, nullptr, false, 0, false},
                            {Num, 0xAB, "Number of Doctors (Systemwide):", 0xAC, "%s", 2, 0, nullptr}, {Band, 0xAD, "Grade (Systemwide):", 0xAE, nullptr, 0, 5, kGrade, false, 24u}}},
    {QueryClass::School, {{Num, 0xB3, "Number of Desks (Systemwide):", 0xB4, "%s", 0, 0, nullptr}, {Num, 0xB5, "Number of Students (Systemwide):", 0xB6, "%s", 1, 0, nullptr},
                          {Num, 0xB7, "Number of Teachers (Systemwide):", 0xB8, "%s", 2, 0, nullptr}, {Band, 0xB9, "Grade (Systemwide):", 0xBA, nullptr, 0, 5, kGrade, false, 24u}}},
    {QueryClass::College, {{Num, 0xC8, "Capacity:", 0xC9, "%s", 0, 0, nullptr}, {Num, 0xCA, "Students in Attendance:", 0xCB, "%s", 1, 0, nullptr},
                           {Num, 0xCC, "Number of Professors:", 0xCD, "%s", 2, 0, nullptr}, {Band, 0xCE, "Grade:", 0xCF, nullptr, 0, 5, kGrade, false, 24u}}},
    {QueryClass::Library, {{Num, 0xD4, "Books Systemwide:", 0xD5, "%s", 0, 0, nullptr}, {Num, 0xD6, "Books in Circulation:", 0xD7, "%s", 1, 0, nullptr},
                           {Band, 0xD8, "Grade:", 0xD9, nullptr, 0, 5, kGrade, false, 24u}}},
    {QueryClass::Museum, {{Num, 0xDE, "Attendance:", 0xDF, "%s (last year)", 0, 0, nullptr}, {Band, 0xE4, "Grade:", 0xE5, nullptr, 0, 5, kGrade, false, 24u}}},
    {QueryClass::Park, {{Num, 0xEA, "Acres Systemwide:", 0xEB, "%s", 5, 0, nullptr, false, 0, true}, {Num, 0xEC, "Benches in Use:", 0xED, "%s", 6, 0, nullptr, false, 0, true}}},
    {QueryClass::Zoo, {{Num, 0xEA, "Acres Systemwide:", 0xEB, "%s", 5, 0, nullptr, false, 0, true}, {Num, 0xEC, "Benches in Use:", 0xED, "%s", 6, 0, nullptr, false, 0, true},
                       {Num, 0xEE, "Attendance:", 0xEF, "%s", 0, 0, nullptr}, {Num, 0xF0, "Manure Donated to Parks:", 0xF1, "%s Tons ", 1, 0, nullptr}}},
    {QueryClass::Ballpark, {{Num, 0xEA, "Acres Systemwide:", 0xEB, "%s", 5, 0, nullptr, false, 0, true}, {Num, 0xEC, "Benches in Use:", 0xED, "%s", 6, 0, nullptr, false, 0, true},
                            {Num, 0xF2, "Little Leaguers:", 0xF3, "%s", 0, 0, nullptr}, {Pair, 0x110, "Win - Loss Record:", 0x111, "%d - %d", 1, 0, nullptr},
                            {Num, 0x112, "Season Ticket Holders:", 0x113, "%s", 3, 0, nullptr}}},
    {QueryClass::Marina, {{Num, 0xF4, "Boats Berthed:", 0xF5, "%s", 0, 0, nullptr}}},
    {QueryClass::Landmark, {{Year, 0xF6, "Date Built:", 0xF7, "%s", 0, 0, nullptr}, {Money, 0x118, "Monthly Income:", 0x119, "%s", 1, 0, nullptr, false, 0, true}}},
    {QueryClass::Road, {{Band, 0x3C, "Traffic:", 0x3D, nullptr, 0, 5, kU5}}},
    {QueryClass::Rail, {{Band, 0x43, "Rail Usage:", 0x44, nullptr, 0, 5, kU5}}},
    {QueryClass::Subway, {{Band, 0x49, "Rail Usage:", 0x4A, nullptr, 0, 5, kU5}}},
    {QueryClass::Station, {{Num, 0x37, "Passengers Per Day:", 0x38, "%s", 0, 0, nullptr}}},   // service quality (181-185) not traced
    {QueryClass::Airport, {{Band, 0xFE, "Air Traffic:", 0xFF, nullptr, 0, 4, kU4, false, 8u}, {Num, 0x103, "On Time Flights:", 0x104, "%d%%", 1, 0, nullptr}}},
    {QueryClass::Seaport, {{Band, 0x105, "Port Traffic:", 0x106, nullptr, 0, 4, kU4, false, 8u}}},
    // Utility "Usage" lines show a number, not a band word (spec 09 6.12); a current capacity of 0 is drawn red.
    {QueryClass::PowerPlant, {{Num, 0x198, "Maximum Power Capacity:", 0x1B7, "%s MW-h/Month", 0, 0, nullptr}, {Num, 0x56, "Current Power Capacity:", 0x57, "%s MW-h/Month", 1, 0, nullptr, true},
                              {Num, 0x58, "Usage:", 0x57, "%s MW-h/Month", 4, 0, nullptr}}},   // "Age (Max)" is the first row of the panel (buildQueryLines)
    {QueryClass::WaterBuilding, {{Num, 0x180, "Maximum Water Capacity:", 0x5F, "%s m\u00b3/Month", 0, 0, nullptr}, {Num, 0x5E, "Current Water Capacity:", 0x5F, "%s m\u00b3/Month", 1, 0, nullptr, true},
                                 {Num, 0x6B, "Usage:", 0x5F, "%s m\u00b3/Month", 4, 0, nullptr}, {Band, 0x60, "Effect of Water Pollution:", 0x61, nullptr, 2, 5, kEff}}},
    {QueryClass::WaterTreatment, {{Num, 0x1A2, "Maximum Treatment Capacity:", 0x1A3, "Current Treatment Capacity:", 0, 0, nullptr}, {Band, 0x1A5, "Usage:", 0x1A6, nullptr, 0, 5, kU5}}},
    {QueryClass::Garbage, {{Num, 0x1AB, "Maximum Disposal Capacity:", 0, "%s", 0, 0, nullptr}, {Num, 0x76, "Current Disposal Capacity:", 0x77, "%s Tons/Month", 1, 0, nullptr},
                           {Band, 0x78, "Usage:", 0x79, nullptr, 0, 5, kU5}}},
        {QueryClass::Recycling, {{Band, 0x1AE, "Citywide Recycling Capacity:", 0x1AF, nullptr, 0, 2, kRec, false, 2u}}},
    {QueryClass::NeighbourConn, {{Text, 0x54, "Connected To:", 0x55, nullptr, 0, 0, nullptr}}},
  };
  static const std::vector<Spec> none;
  auto it = t.find(c);
  return it == t.end() ? none : it->second;
}

std::string zoneName(const WinEnv& E, int code) {
  struct Z { int code; uint32_t id; const char* fb; };
  static const Z z[] = {{1, 0xF, "Light Residential"}, {2, 0x10, "Medium Residential"}, {3, 0x11, "Dense Residential"},
                        {5, 0x12, "Light Commercial"}, {6, 0x13, "Medium Commercial"}, {7, 0x14, "Dense Commercial"},
                        {9, 0x15, "Light Industrial"}, {10, 0x16, "Medium Industrial"}, {11, 0x17, "Dense Industrial"},
                        {15, 0x18, "Seaport"}, {14, 0x19, "Airport"}, {17, 0x1A, "Landfill"}};
  for (auto& e : z) if (e.code == code) return E.query(e.id, e.fb);
  return E.query(0x195, "Unzoned");
}

std::string band(const WinEnv& E, uint32_t first, int idx, const char* fb) { return E.query(first + uint32_t(idx), fb); }

}  // namespace

namespace {

using Group = std::vector<QueryLine>;

std::string trimLead(std::string s) {
  size_t i = 0;
  while (i < s.size() && s[i] == ' ') i++;
  return s.substr(i);
}

// Which row set (spec 09 6.12.1 per-target table) a target gets.
enum class Target { Zoned, Flora, Wall, Bare, Water, Road, Rail, PowerLine, Landfill, Pipe, SubwayTile, Reward, Marina, PowerPlant, Utility, Service };

Target targetOf(const QueryInfo& q) {
  switch (q.cls) {
    case QueryClass::Zoned: return Target::Zoned;
    case QueryClass::Flora: return Target::Flora;
    case QueryClass::Road: return Target::Road;
    case QueryClass::Rail: return Target::Rail;
    case QueryClass::PowerLine: return Target::PowerLine;
    case QueryClass::LandfillZone: return Target::Landfill;
    case QueryClass::Pipe: return Target::Pipe;
    case QueryClass::Subway: return q.underground ? Target::SubwayTile : Target::Rail;
    case QueryClass::PowerPlant: return Target::PowerPlant;
    case QueryClass::WaterBuilding: case QueryClass::Recycling: return Target::Utility;
    case QueryClass::Marina: return Target::Marina;
    case QueryClass::Landmark: return q.canReplace ? Target::Reward : Target::Wall;
    case QueryClass::None: return q.waterDepth > 0 ? Target::Water : Target::Bare;
    default: return Target::Service;
  }
}

}  // namespace

std::string queryDisplayName(const WinEnv& E, const QueryInfo& q) {
  if (!q.name.empty()) return q.name;
  if (q.cls == QueryClass::None) {
    if (q.waterDepth > 0) return q.saltWater ? E.query(0xC3, "Salt Water") : E.query(0xC2, "Fresh Water");
    return E.query(0xC1, "Dirt");
  }
  return std::string();
}

std::string queryStatusLine(const WinEnv& E, const QueryInfo& q) {
  struct S { uint32_t bit, id; const char* fb; };
  static const S st[] = {{QsConstruction, 0xC4, "(Under Construction)"}, {QsAbandoned, 0xC5, "(Abandoned)"}, {QsGarbage, 0xC6, "(Covered in Garbage)"},
                         {QsDestroyed, 0xC7, "(Destroyed)"}, {QsStrike, 0x186, "(Workers on Strike)"}, {QsAging, 0x196, "(Aging)"},
                         {QsDefunct, 0x197, "(Defunct)"}, {QsDecommissioned, 0x1B4, "(Decommissioned)"}, {QsNoAccess, 0x1B8, "(No Access to City)"}};
  std::string out;
  for (const S& s : st) if (q.status & s.bit) { if (!out.empty()) out += " "; out += trimLead(E.query(s.id, s.fb)); }
  return out;
}

std::vector<QueryLine> buildQueryLines(const WinEnv& E, const QueryInfo& q) {
  auto num = [&](uint32_t id, const char* fbTpl, double v) { return subst(E.query(id, fbTpl), {fmtNum(v)}); };
  auto yesNo = [&](uint32_t label, const char* fbLabel, bool yes) {
    QueryLine l; l.label = E.query(label, fbLabel);
    l.value = yes ? E.query(5, "Yes") : E.query(6, "No");
    l.red = !yes;   // "No" is drawn red (spec 09 6.12.1)
    return l;
  };
  const Target tg = targetOf(q);
  auto elevation = [&] { return QueryLine{E.query(2, "Elevation:"), num(3, "%s Meters", q.elevation)}; };
  auto depth = [&] { return QueryLine{E.query(0x17F, "Depth:"), num(3, "%s Meters", q.waterDepth)}; };
  auto powered = [&] { return yesNo(4, "Powered:", q.powered); };
  auto watered = [&] { return yesNo(7, "Watered:", q.watered); };
  auto landValue = [&] {
    int idx = q.landValue < 35 ? 0 : q.landValue < 75 ? 1 : q.landValue < 130 ? 2 : q.landValue < 175 ? 3 : q.landValue < 225 ? 4 : 5;
    return QueryLine{E.query(8, "Land Value:"), E.query(landValueBandId(q.landValue), kLV[idx])};
  };
  auto zone = [&] { return QueryLine{E.query(0xE, "Zone Type:"), zoneName(E, q.zoneCode)}; };
  // Reason rows (spec 09 6.12.2): strings of SEStringsUI (041F2625). Code -> value id; 1-8 abandonment, 9-18 lack of development.
  auto reason = [&] {
    static const struct { int code; uint32_t id; const char* fb; } rs[] = {
        {1, 0x2CB, "Radiation levels are too high"}, {2, 0x2C3, "Land value is too low"}, {3, 0x2C7, "Lack of demand for this zone"},
        {4, 0x2C8, "No power"}, {5, 0x2C9, "No water"}, {6, 0x2CA, "Poor transportation to other zones"}, {7, 0x2C4, "Pollution levels are too high"},
        {8, 0x2C5, "Lack of chocolate sprinkles"}, {9, 0x2CC, "Zone is too small"}, {10, 0x2CB, "Radiation levels are too high"},
        {11, 0x2CD, "No demand for low land value"}, {12, 0x2CE, "No demand for high land value"}, {13, 0x2C7, "Lack of demand for this zone"},
        {14, 0x2C8, "No power"}, {15, 0x2C9, "No water"}, {16, 0x2CA, "Poor transportation to other zones"}, {17, 0x2CF, "Zone is not on a coast line"},
        {18, 0x2D1, "Planets not aligned"}};
    const bool abandoned = (q.status & QsAbandoned) != 0;
    int code = q.reasonCode;
    if (code <= 0 || code > 18) code = abandoned ? 8 : 18;   // nothing stored: "Lack of chocolate sprinkles" / "Planets not aligned"
    const auto& r = rs[code - 1];
    const bool aband = code <= 8;
    return QueryLine{E.se(aband ? 0x2C1 : 0x2C2, aband ? "Reason for Abandonment:" : "Reason for Lack of Development:"), E.se(r.id, r.fb)};
  };
  auto neighbourRow = [&] { return QueryLine{E.query(0x54, "Connected To:"), q.neighbour}; };
  auto crime = [&] {
    int idx = q.crime <= 0 ? 0 : q.crime < 47 ? 1 : q.crime < 84 ? 2 : q.crime < 122 ? 3 : q.crime <= 150 ? 4 : 5;
    return QueryLine{E.query(0x1B, "Crime:"), E.query(crimeBandId(q.crime), kCrime[idx])};
  };
  auto flammability = [&] {
    uint32_t id = flammabilityBandId(q.flammability);
    static const char* fb[] = {"None", "Low", "Medium", "High", "Very High", "Extreme"};
    QueryLine l{E.query(0x22, "Flammability:"), E.query(id, fb[id - 0x23])};
    l.red = id == 0x28;   // "Extreme" is drawn red
    return l;
  };
  auto airPollution = [&] { int i = std::min(5, std::max(0, q.airPollution) * 6 / 256); return QueryLine{E.query(0x187, "Air Pollution:"), band(E, 0x188, i, kU6[i])}; };
  auto waterPollution = [&] { int i = std::min(5, std::max(0, q.waterPollution) * 6 / 256); return QueryLine{E.query(0x18E, "Water Pollution:"), band(E, 0x18F, i, kU6[i])}; };
  auto pollutionGenerated = [&] {
    uint32_t id = pollutionBandId(q.pollution, q.pollutionBaseline);
    static const char* fb[] = {"None", "Low", "Medium", "High", "Very High", "Hazardous"};
    QueryLine l{E.query(0x29, "Pollution Generated:"), E.query(id, fb[id - 0x2A])};
    l.red = id == 0x2F;   // "Hazardous" is drawn red
    return l;
  };
  // Rows of the occupant family, then its flavour lines.
  auto classRows = [&] {
    Group g;
    for (const Spec& s : specsFor(q.cls)) {
      switch (s.kind) {
        case Num: {
          if (s.hideZero && q.v[s.slot] == 0) break;
          QueryLine l{E.query(s.label, s.fbLabel), num(s.value, s.fbValue, q.v[s.slot])};
          l.red = (s.redZero && q.v[s.slot] <= 0) || ((q.redMask >> s.slot) & 1u);
          g.push_back(l);
          break;
        }
        case Pair: g.push_back({E.query(s.label, s.fbLabel), subst(E.query(s.value, s.fbValue), {fmtNum(q.v[s.slot]), fmtNum(q.v[s.slot + 1])})}); break;
        case Money: if (!(s.hideZero && q.monthlyIncome == 0)) g.push_back({E.query(s.label, s.fbLabel), subst(E.query(s.value, s.fbValue), {E.money(q.monthlyIncome)})}); break;
        case Year: g.push_back({E.query(s.label, s.fbLabel), subst(E.query(s.value, s.fbValue), {q.builtDate.empty() ? std::to_string(q.builtYear) : q.builtDate})}); break;
        case Bool: g.push_back({E.query(s.label, s.fbLabel), q.v[s.slot] > 0 ? E.query(s.value, "Yes") : E.query(s.value + 1, "No")}); break;
        case Text: g.push_back({E.query(s.label, s.fbLabel), q.neighbour.empty() ? E.query(s.value, "-") : q.neighbour}); break;
        case Band: {
          int n = s.bands, idx = std::min(n - 1, int(q.band[s.slot]) * n / 256);
          QueryLine l{E.query(s.label, s.fbLabel), band(E, s.value, idx, s.fbBands[idx])};
          l.red = (s.redMask >> idx) & 1u;
          g.push_back(l);
          break;
        }
      }
    }
    return g;
  };
  auto flavour = [&] {
    Group g;
    for (int i = 0; i < 3; i++)
      if (q.flavour[i]) {
        // Flavour labels come in (label, value-template) pairs of consecutive ids; the value is a made-up number.
        std::string lab = E.query(uint32_t(q.flavour[i]), "");
        if (!lab.empty()) g.push_back({lab, subst(E.query(uint32_t(q.flavour[i]) + 1, "%s"), {fmtNum(q.flavourValue[i])})});
      }
    return g;
  };

  std::vector<Group> groups;
  switch (tg) {
    case Target::Zoned: {
      Group g{elevation(), landValue(), zone()};
      if (q.reasonRow) g.push_back(reason());
      g.insert(g.end(), {powered(), watered(), crime(), flammability(), airPollution(), waterPollution()});
      groups.push_back(g);
      break;
    }
    case Target::Flora:
      groups.push_back({elevation(), landValue(), zone(), powered(), watered(), flammability(), airPollution(), waterPollution()});
      break;
    case Target::Wall: case Target::Bare: {
      Group g{elevation(), landValue(), zone()};
      if (q.reasonRow) g.push_back(reason());
      g.insert(g.end(), {powered(), watered(), crime(), flammability(), airPollution(), waterPollution()});
      groups.push_back(g);
      break;
    }
    case Target::Water:
      groups.push_back({depth(), airPollution(), waterPollution()});
      break;
    case Target::Road: case Target::Rail: {
      Group g;
      if (!q.neighbour.empty()) g.push_back(neighbourRow());   // a connection to a neighbour city (map edge piece)
      g.push_back(elevation());
      groups.push_back(g);
      groups.push_back(classRows());
      break;
    }
    case Target::PowerLine: {
      Group g;
      if (!q.neighbour.empty()) g.push_back(neighbourRow());
      g.push_back(elevation()); g.push_back(powered());
      groups.push_back(g);
      break;
    }
    case Target::Landfill:
      groups.push_back({elevation()});
      groups.push_back({QueryLine{E.query(0x10C, "Capacity Citywide:"), num(0x10D, "%s Tons", q.landfillCapacity)},
                        QueryLine{E.query(0x10E, "Usage:"), num(0x10F, "%s%% Full", q.landfillUsage)}});
      break;
    case Target::Pipe:
      groups.push_back({watered()});
      break;
    case Target::SubwayTile:
      groups.push_back(classRows());
      break;
    case Target::Reward: {
      Group f = flavour();
      if (!f.empty()) groups.push_back(f);
      groups.push_back(classRows());
      break;
    }
    case Target::Marina:
      groups.push_back({depth()});
      groups.push_back(classRows());
      break;
    case Target::PowerPlant: {
      Group g;
      if (q.ageYears >= 0) g.push_back({E.query(0x1B5, "Age (Max):"), subst(E.query(0x1B6, "%s yrs. (%s yrs.)"), {fmtNum(q.ageYears), fmtNum(q.maxAgeYears)})});
      g.push_back(elevation());
      if (q.pollution >= 0) g.push_back(pollutionGenerated());
      groups.push_back(g);
      groups.push_back(classRows());
      break;
    }
    case Target::Utility: {   // water tower, recycling centre: Age (Max), Elevation, Powered | the family rows
      Group g;
      if (q.ageYears >= 0) g.push_back({E.query(0x1B5, "Age (Max):"), subst(E.query(0x1B6, "%s yrs. (%s yrs.)"), {fmtNum(q.ageYears), fmtNum(q.maxAgeYears)})});
      g.push_back(elevation());
      g.push_back(powered());
      groups.push_back(g);
      Group c = classRows();
      Group f = flavour();
      c.insert(c.end(), f.begin(), f.end());
      if (!c.empty()) groups.push_back(c);
      break;
    }
    case Target::Service: {
      groups.push_back({elevation(), powered(), watered()});
      Group c = classRows();
      Group f = flavour();
      c.insert(c.end(), f.begin(), f.end());
      if (!c.empty()) groups.push_back(c);
      if (q.cleanupCost >= 0) groups.push_back({QueryLine{E.query(0x31, "Clean-up Cost:"), subst(E.query(0x32, "%s"), {E.money(q.cleanupCost)})}});
      if (q.fundingCost >= 0) groups.push_back({QueryLine{E.query(0x86, "Funding Cost:"), subst(E.query(0x87, "%s/Month"), {E.money(q.fundingCost)})}});
      break;
    }
  }
  if (tg != Target::Service && q.cleanupCost >= 0)
    groups.push_back({QueryLine{E.query(0x31, "Clean-up Cost:"), subst(E.query(0x32, "%s"), {E.money(q.cleanupCost)})}});
  if (tg != Target::Service && q.fundingCost >= 0)
    groups.push_back({QueryLine{E.query(0x86, "Funding Cost:"), subst(E.query(0x87, "%s/Month"), {E.money(q.fundingCost)})}});

  // Groups are separated by one blank row.
  std::vector<QueryLine> L;
  for (const Group& g : groups) {
    if (g.empty()) continue;
    if (!L.empty()) L.push_back(QueryLine());
    L.insert(L.end(), g.begin(), g.end());
  }
  return L;
}

// ---- picture ------------------------------------------------------------------------------------------------------------------------
// The thumbnail zoom is [INFERRED] (spec 09 6.12.2): the largest zoom whose image fits in about 128 x 135 px. The observed
// boxes (4x4 plant 130, 3x3 industry 98, 1x1 tall building 66 x 83, flora 102 x 135) agree with it. The fit allows a few pixels.
constexpr int kQueryPictureMaxW = 132, kQueryPictureMaxH = 135;

bool queryPictureSprite(GameData* gd, const ResKey& appearance, uint32_t& cat, uint32_t& id, bool& dbl) {
  if (!gd || !appearance.type) return false;
  const CsAttrib* cs = gd->csattrib(appearance);
  if (!cs || !cs->valid || cs->zoomCount <= 0) return false;
  bool have = false;
  for (int slot = cs->zoomCount - 1; slot >= 0; slot--) {
    const CsFrame* f = cs->frame(0, slot, 0);
    if (!f) continue;
    const SpriteImage* sp = gd->sprite(f->category, f->spriteId);
    if (!sp || sp->img.empty()) continue;
    const int k = f->flags == 2 ? 2 : 1;
    cat = f->category; id = f->spriteId; dbl = k == 2; have = true;   // smallest zoom is the fallback
    if (sp->img.w * k <= kQueryPictureMaxW && sp->img.h * k <= kQueryPictureMaxH) return true;
  }
  return have;
}

QueryWindow::Layout QueryWindow::layoutFor(const WinEnv& E, const QueryInfo& q) const {
  Layout L;
  uint32_t cat = 0, id = 0; bool dbl = false;
  if (queryPictureSprite(E.data, q.picture, cat, id, dbl)) {
    if (const SpriteImage* sp = E.data->sprite(cat, id)) {
      const int k = dbl ? 2 : 1;
      L.picW = sp->img.w * k; L.picH = sp->img.h * k;
    }
  }
  L.type = L.picW <= 0 ? 1 : (L.picH * 10 > L.picW * 12 ? 3 : 2);   // height / width > 1.2: picture on the left
  L.w = L.type == 3 ? 423 : 308;
  L.h = L.type == 1 ? 280 : L.type == 2 ? 395 : 320;
  return L;
}

WindowResult QueryWindow::draw(Ui& ui, const WinEnv& E, const QueryInfo& q) {
  WindowResult R;
  const Layout L = layoutFor(E, q);
  const std::string name = queryDisplayName(E, q), status = queryStatusLine(E, q);
  const auto lines = buildQueryLines(E, q);

  // Position: (40, 40) the first time, then wherever the player left it; dragged by the title band. [STATIC][OBSERVED]
  Rect r{pos.x, pos.y, L.w, L.h};
  if (!placed) { r.x = 40; r.y = 40; placed = true; }
  if (dragging_) {
    if (ui.in.down) { r.x = ui.in.mx - grabX_; r.y = ui.in.my - grabY_; }
    else dragging_ = false;
  } else if (ui.in.pressed && ui.in.my >= r.y && ui.in.my < r.y + 35 && ui.in.mx >= r.x && ui.in.mx < r.x + r.w - 36) {
    dragging_ = true; grabX_ = ui.in.mx - r.x; grabY_ = ui.in.my - r.y;
  }
  // Keep the whole panel on screen (its close box and OK button stay reachable).
  r.x = std::max(0, std::min(ui.W - r.w, r.x));
  r.y = std::max(0, std::min(ui.H - r.h, r.y));
  pos = r; rect = r;
  ui.windowRects.push_back(r);
  if (frame(ui, E, r, E.query(1, "Query"))) { R.close = true; R.add(Act::Close); }

  // Picture (centred on (160, 97) for type 2, (77, 160) for type 3), framed by a 1 px #7B8AFF rectangle outside the image.
  if (L.picW > 0) {
    const int cx = L.type == 2 ? 160 : 77, cy = L.type == 2 ? 97 : 160;
    Rect box{r.x + cx - L.picW / 2, r.y + cy - L.picH / 2, L.picW, L.picH};
    ui.fill({box.x - 1, box.y - 1, box.w + 2, 1}, rgb(0x7B, 0x8A, 0xFF)); ui.fill({box.x - 1, box.y + box.h, box.w + 2, 1}, rgb(0x7B, 0x8A, 0xFF));
    ui.fill({box.x - 1, box.y, 1, box.h}, rgb(0x7B, 0x8A, 0xFF)); ui.fill({box.x + box.w, box.y, 1, box.h}, rgb(0x7B, 0x8A, 0xFF));
    spriteCentred(ui, E.data, q.picture, box, box);
  }

  // Name and status (main10).
  const int nameX = L.type == 3 ? 140 : 22, nameTop = L.type == 2 ? 160 : 42;
  if (!name.empty()) ui.text(r.x + nameX, r.y + nameTop, name, "main10");
  if (!status.empty()) ui.text(r.x + nameX + 15, r.y + nameTop + 20, status, "main10");
  const int firstRow = (status.empty() ? nameTop : nameTop + 20) + 29;

  // Rows (main9, 14 px pitch). Labels wrap before the value column, values wrap inside it; a value sits on its label's last line.
  const int labelX = L.type == 3 ? 141 : 23;
  const int viewBottom = L.h - 41;   // rows are clipped just above the buttons
  struct Row { std::string label, value; bool red = false; };
  auto layoutRows = [&](int valueX, std::vector<Row>& rows) {
    rows.clear();
    const int labelW = valueX - labelX - 4, valueW = r.w - valueX - (L.type == 3 ? 12 : 20) - (valueX == 150 ? 16 : 0);
    for (const QueryLine& l : lines) {
      if (l.label.empty() && l.value.empty()) { rows.push_back(Row()); continue; }
      auto ll = wrap(ui, l.label, labelW, "main9"), vl = wrap(ui, l.value, std::max(20, valueW), "main9");
      if (ll.empty()) ll.push_back("");
      if (vl.empty()) vl.push_back("");
      const size_t n = std::max(ll.size(), ll.size() - 1 + vl.size());
      const size_t base = rows.size();
      rows.resize(base + n);
      for (size_t i = 0; i < ll.size(); i++) rows[base + i].label = ll[i];
      for (size_t i = 0; i < vl.size(); i++) { Row& w = rows[base + ll.size() - 1 + i]; w.value = vl[i]; w.red = l.red; }
    }
  };
  std::vector<Row> rows;
  int valueX = L.type == 3 ? 276 : 160;
  layoutRows(valueX, rows);
  const int visible = std::max(1, (viewBottom - firstRow) / 14);
  const bool overflow = int(rows.size()) > visible;
  if (overflow && L.type != 3) { valueX = 150; layoutRows(valueX, rows); }
  if (overflow) {
    Rect bar{r.x + 285, r.y + firstRow - 1, 16, viewBottom - firstRow + 1};
    scrollBar(ui, E, bar, int(rows.size()), visible, scroll_);
  } else scroll_ = 0;
  const uint32_t red = rgb(0xFF, 0, 0);
  for (int i = 0; i < visible && i + scroll_ < int(rows.size()); i++) {
    const Row& w = rows[size_t(i + scroll_)];
    const int y = r.y + firstRow + i * 14;
    if (!w.label.empty()) ui.text(r.x + labelX, y, w.label, "main9");
    if (!w.value.empty()) ui.text(r.x + valueX, y, w.value, "main9", w.red ? red : 0);
  }

  // Buttons.
  const bool historical = q.growable;
  if (q.canReplace) {
    Rect b = historical ? Rect{r.x + 20, r.y + L.h - 58, 73, 33} : Rect{r.x + 16, r.y + L.h - 38, 73, 33};
    if (button(ui, E, "replace", b, E.se(0x12C, "Replace"))) { R.add(Act::QueryReplace, 0, q.x, q.z); R.close = true; R.add(Act::Close); }
  }
  if (historical) {
    const int bx = r.x + (L.type == 3 ? 138 : 20), by = r.y + L.h - 23;
    Rect box{bx, by, 13, 13};
    ui.fill(box, rgb(0x31, 0x36, 0x7C)); ui.fill({bx + 1, by + 1, 11, 11}, rgb(255, 255, 255));
    if (q.historical) { ui.fill({bx + 3, by + 3, 7, 7}, rgb(0x31, 0x36, 0x7C)); }
    ui.text(bx + 27, by - 2, E.query(0x30, "Make Historical"), "main10");
    Rect hit{bx, by - 2, 27 + ui.textWidth(E.query(0x30, "Make Historical")), 17};
    E.spot("historical", hit);
    if (ui.clicked(hit)) R.add(Act::ToggleHistorical, 0, q.x, q.z);
  }
  Rect done{r.x + L.w - 50, r.y + L.h - 37, 42, 32};
  E.spot("ok", done);
  {
    bool c = false;
    if (!(ui.skin && ui.skin->imageButtonCells(ui, ui.skin->image(skin_group::Sys, 0xA2DEFD9A), 4, done, "", false, true, c))) c = ui.button(done, E.app(0xB4, "OK"));
    if (c) { R.add(Act::QueryOk, 0, q.x, q.z); R.close = true; }   // OK commits Make Historical; the close box discards it
  }
  return R;
}

}  // namespace m3k
