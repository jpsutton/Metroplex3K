#include "core/settings.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include "core/ini.h"
#include "core/ixf.h"

namespace m3k {

// ---- languages ------------------------------------------------------------------------------
namespace {
struct Lang { const char* folder; const char* locale; };
const Lang kLangs[20] = {
  {"English", "english-us"}, {"English-UK", "english-uk"}, {"French", "french"}, {"German", "german"},
  {"Italian", "italian"}, {"Spanish", "spanish"}, {"Dutch", "dutch"}, {"Danish", "danish"},
  {"Swedish", "swedish"}, {"Norwegian", "norwegian"}, {"Finnish", "finnish"}, {"Hebrew", "hebrew"},
  {"Russian", "russian"}, {"Portuguese", "portuguese"}, {"Japanese", "japanese"}, {"Polish", "polish"},
  {"SimplifiedChinese", "chinese-simplified"}, {"TraditionalChinese", "chinese-traditional"},
  {"Thai", "thai"}, {"Korean", "korean"}};

bool allDigits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) if (c < '0' || c > '9') return false;
  return true;
}
}  // namespace

const char* languageFolder(int id) { return id >= 1 && id <= 20 ? kLangs[id - 1].folder : nullptr; }
const char* languageLocaleName(int id) { return id >= 1 && id <= 20 ? kLangs[id - 1].locale : nullptr; }

int languageFromName(const std::string& nameIn) {
  std::string n = trimSpaces(nameIn);
  if (n.empty()) return 0;
  if (allDigits(n) && n.size() < 6) {
    long v = std::strtol(n.c_str(), nullptr, 10);
    return v >= 1 && v <= kLangCount ? int(v) : 0;
  }
  for (int i = 0; i < 20; i++)
    if (iequals(n, kLangs[i].folder) || iequals(n, kLangs[i].locale)) return i + 1;
  return 0;
}

int languageFromLangId(uint16_t id) {
  switch (id) {
    case 0x0809: case 0x0C09: case 0x1409: case 0x1809: case 0x1C09: case 0x2009: case 0x2409: case 0x2809:
    case 0x2C09: case 0x3009: case 0x3409: return 2;
    case 0x040C: case 0x080C: case 0x0C0C: case 0x100C: case 0x140C: case 0x180C: return 3;
    case 0x0407: case 0x0807: case 0x0C07: case 0x1007: case 0x1407: return 4;
    case 0x0410: case 0x0810: return 5;
    case 0x080A: return 41;
    case 0x0413: case 0x0813: return 7;
    case 0x0406: return 8;
    case 0x041D: case 0x081D: return 9;
    case 0x0414: case 0x0814: return 10;
    case 0x040B: return 11;
    case 0x040D: return 12;
    case 0x0419: return 13;
    case 0x0416: case 0x0816: return 14;
    case 0x0411: return 15;
    case 0x0415: return 16;
    case 0x0804: case 0x1004: case 0x1404: return 17;
    case 0x0404: case 0x0C04: return 18;
    case 0x041E: return 19;
    case 0x0412: case 0x0812: return 20;
    case 0x0439: return 21;
    case 0x0402: return 23;
    case 0x0422: return 25;
    case 0x0405: return 26;
    case 0x0408: return 27;
    case 0x040E: return 28;
    case 0x040F: return 29;
    case 0x0418: return 30;
    case 0x041B: return 32;
    case 0x041C: return 33;
    case 0x041F: return 34;
    case 0x042A: return 43;
    default: break;
  }
  if ((id & 0x3FF) == 0x00A && (id >> 10) >= 1 && (id >> 10) <= 20) return 6;   // Spanish 0x040A..0x500A
  if ((id & 0x3FF) == 0x001 && (id >> 10) >= 1 && (id >> 10) <= 16) return 22;  // Arabic 0x0401..0x4001
  return 1;
}

bool languageInstalled(const fs::path& apps, int id) {
  const char* f = languageFolder(id);
  if (!f) return false;
  return !resolveCI(apps, std::string("Res/Text/") + f + "/SC3StringsApp.IXF").empty();
}
std::vector<int> installedLanguages(const fs::path& apps) {
  std::vector<int> v;
  for (int i = 1; i <= kLangCount; i++) if (languageInstalled(apps, i)) v.push_back(i);
  return v;
}

uint32_t paradeNameInstance(int country) {
  switch (country) {
    case 6: return 0x258; case 17: return 0x259; case 19: return 0x25A; case 31: return 0x25B;
    case 32: return 0x25C; case 45: return 0x25D; case 46: return 0x25E; case 49: return 0x25F;
    case 54: return 0x260; default: return 0;
  }
}

int chooseLanguage(const LanguageInputs& in) {
  auto ok = [&](int id) { return id >= 1 && id <= kLangCount && in.installed && in.installed(id); };
  int cl = languageFromName(in.cmdLine);
  if (cl && ok(cl)) return cl;
  auto fallback = [&]() {
    if (ok(kDefaultLanguage)) return kDefaultLanguage;
    for (int i = 1; i <= kLangCount; i++) if (ok(i)) return i;
    return kDefaultLanguage;
  };
  if (in.ctrlShiftHeld) return fallback();
  int reg = 0;
  if (in.registryDword) {
    uint32_t v = *in.registryDword;
    reg = v == 0 ? 0 : (v < 44 ? int(v) : languageFromLangId(uint16_t(v & 0xFFFF)));
  } else if (in.registryString) {
    reg = languageFromName(*in.registryString);
  }
  if (reg && ok(reg)) return reg;
  if (in.cfgLanguage >= 1 && ok(in.cfgLanguage)) return in.cfgLanguage;
  int dl = languageFromName(in.defaultLocale);
  if (dl && ok(dl)) return dl;
  return fallback();
}

int chooseCountry(const CountryInputs& in) {
  auto num = [](const std::string& s) {
    std::string t = trimSpaces(s);
    if (!allDigits(t) || t.size() > 5) return -1;
    long v = std::strtol(t.c_str(), nullptr, 10);
    return v < kCountryLimit ? int(v) : -1;
  };
  int c = num(in.cmdLine);
  if (c >= 0) return c;
  if (in.ctrlShiftHeld) return kDefaultCountry;
  if (in.registryDword && *in.registryDword < uint32_t(kCountryLimit)) return int(*in.registryDword);
  if (!in.registryDword && in.registryString) { c = num(*in.registryString); if (c >= 0) return c; }
  return kDefaultCountry;
}

// ---- command line ---------------------------------------------------------------------------
std::vector<std::string> tokenizeCommandLine(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  bool inQ = false, have = false;
  for (char c : s) {
    if (c == '"') { inQ = !inQ; have = true; continue; }
    if (!inQ && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
      if (have) { out.push_back(cur); cur.clear(); have = false; }
      continue;
    }
    cur.push_back(c); have = true;
  }
  if (have) out.push_back(cur);
  return out;
}

namespace {
bool isSwitchChar(char c) { return c == '/' || c == '-' || c == '\\'; }
bool onOff(const std::string& v) {  // off iff value starts with "of"
  return !(v.size() >= 2 && (v[0] == 'o' || v[0] == 'O') && (v[1] == 'f' || v[1] == 'F'));
}
std::string unquote(std::string v) {
  v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
  return v;
}
bool numeric(const std::string& s) { return allDigits(s); }
}  // namespace

CommandLine parseCommandLine(const std::vector<std::string>& toks) {
  CommandLine cl;
  for (size_t i = 0; i < toks.size(); i++) {
    const std::string& t = toks[i];
    if (t.empty()) continue;
    if (!isSwitchChar(t[0]) || t.size() < 2) { cl.positional.push_back(t); continue; }
    std::string body = t.substr(1);
    size_t colon = body.find(':');
    if (colon != std::string::npos && colon >= 2) {
      std::string name = toLower(body.substr(0, colon)), val = unquote(body.substr(colon + 1));
      if (name == "cheats") { cl.cheatsEnabled = true; cl.cheatsValue = val; }
      else if (name == "intro") cl.intro = onOff(val);
      else if (name == "sound") cl.sound = onOff(val);
      else if (name == "music") cl.music = onOff(val);
      else if (name == "restart") cl.restart = onOff(val);
      else if (name == "updaterapp") cl.updaterApp = val;
      else cl.ignored.push_back(t);
      continue;
    }
    char c = char(std::tolower(static_cast<unsigned char>(body[0])));
    std::string val = unquote(body.substr(1));
    switch (c) {
      case 'r': {
        int w, h;
        if (std::sscanf(val.c_str(), "%d%*[xX]%d", &w, &h) == 2 && w > 0 && h > 0) { cl.hasResolution = true; cl.width = w; cl.height = h; }
        else cl.ignored.push_back(t);
        break;
      }
      case 'd': cl.device = val.substr(0, 31); break;
      case 'f': cl.fullscreen = true; break;
      case 'w': cl.fullscreen = false; break;
      case 'l': cl.language = val; break;
      case 'c': cl.country = val; break;
      case 'x': cl.ignored.push_back(t); break;
      case 'p': {
        std::vector<long> n;
        if (numeric(val)) n.push_back(std::strtol(val.c_str(), nullptr, 10));
        while (n.size() < 4 && i + 1 < toks.size() && numeric(toks[i + 1])) n.push_back(std::strtol(toks[++i].c_str(), nullptr, 10));
        MachineProfile p;
        if (n.size() > 0) p.cpuMHz = int(std::clamp(n[0], 100L, 1000L));
        if (n.size() > 1) p.memoryMB = int(std::max(16L, n[1]));
        if (n.size() > 2) p.cpuGen = int(std::clamp(n[2], 5L, 10L));
        if (n.size() > 3) p.memClass = int(std::max(4L, n[3]));
        cl.profile = p;
        break;
      }
      default: cl.ignored.push_back(t);
    }
  }
  if (cl.restart) { cl.hasResolution = false; cl.width = cl.height = 0; cl.device.clear(); }
  return cl;
}

CommandLine parseCommandLine(int argc, const char* const* argv) {
  std::vector<std::string> t;
  for (int i = 1; i < argc; i++) t.push_back(argv[i]);
  return parseCommandLine(t);
}

// ---- display --------------------------------------------------------------------------------
const std::vector<DisplayMode>& displayModes() {
  static const std::vector<DisplayMode> m = {{640, 480}, {800, 600}, {1024, 768}};
  return m;
}
std::string uiSegmentName(const DisplayMode& m) {
  char b[64];
  std::snprintf(b, sizeof b, "%05d%03d_MainUI.IXF", m.w, m.h);
  return b;
}
int machineClass(int g, int s) {
  if (g < 5) return 1;
  if (g == 5) return s > 189 ? 3 : 2;
  if (g == 6) return s > 259 ? 5 : 4;
  if (g <= 9) return s > 299 ? 6 : 5;
  return 7;
}
int memoryClass(int mb) {
  int c = 0;
  while (mb > 1) { mb >>= 1; c++; }
  return c;
}
int roundMemoryMB(uint64_t bytes) {
  uint64_t mb = bytes >> 20;
  return int((mb + 3) / 4 * 4);
}
MachineProfile profileFromMeasurement(int gen, int mhz, uint64_t memBytes) {
  MachineProfile p;
  p.cpuGen = gen;
  p.cpuMHz = mhz < 100 ? 180 : mhz;
  p.memoryMB = roundMemoryMB(memBytes);
  p.memClass = memoryClass(p.memoryMB);
  return p;
}
DisplayMode chooseDisplayMode(int cls, int cfgW, int cfgH, const CommandLine& cl) {
  if (cl.hasResolution) return {cl.width, cl.height};
  if (cfgW > 0 && cfgH > 0) return {cfgW, cfgH};
  return cls < 4 ? DisplayMode{640, 480} : DisplayMode{800, 600};
}

// ---- SC3.cfg --------------------------------------------------------------------------------
static const CfgRecordId kCfgIds[kCfgRecordCount] = {
  {0xA352F1B5, 0x39EF6FF9}, {0x6314C631, 0x6313BF98}, {0x4324F379, 0x39EF6F00}, {0x0324F37E, 0x4324F45E},
  {0xA324F381, 0xA324F46A}, {0x0324F385, 0xC324F475}, {0xC324F388, 0xC324F47F}, {0x2324F38B, 0x2324F488},
  {0xA324F390, 0x0324F492}, {0xC324F393, 0xE324F49A}, {0x633350DA, 0x233350DF}, {0x03357D61, 0x43357D6C},
  {0xA334F3F4, 0xE334F3FE}, {0x1A35002A, 0x5A35004A}, {0xBA3512E9, 0xBA3512FD}};
CfgRecordId cfgRecordId(int r) { return kCfgIds[std::clamp(r, 1, kCfgRecordCount) - 1]; }

DetailLevels defaultDetail(int c) {
  if (c < 2) return {1, 1, 0};
  if (c == 2) return {2, 1, 0};
  if (c == 3) return {2, 1, 1};
  if (c == 4) return {2, 2, 1};
  return {3, 2, 1};
}

GeneralSettings::GeneralSettings() { std::memset(device, 0xFF, sizeof device); }

static void wrStr(Bytes& b, const std::string& s) {
  put32(b, uint32_t(s.size()));
  b.insert(b.end(), s.begin(), s.end());
}

Bytes GeneralSettings::serialize() const {
  Bytes b;
  wrStr(b, string1); wrStr(b, string2);
  put32(b, uint32_t(language));
  b.push_back(flagA); put32(b, width); put32(b, height);
  b.push_back(windowed); b.push_back(flagB);
  b.insert(b.end(), device, device + 32);
  b.push_back(flagC);
  b.push_back(musicOn); put32(b, uint32_t(musicVolume));
  b.push_back(sfxOn); put32(b, uint32_t(sfxVolume));
  b.push_back(ambienceOn); put32(b, uint32_t(ambienceVolume));
  b.push_back(flagD); b.push_back(flagE); b.push_back(flagF);
  for (uint32_t v : unknown3) put32(b, v);
  put32(b, uint32_t(detail.l1)); put32(b, uint32_t(detail.l2)); put32(b, uint32_t(detail.l3));
  b.push_back(flagG); put32(b, value);
  return b;
}

bool GeneralSettings::parse(const Bytes& p, GeneralSettings& o) {
  try {
    Reader r(p);
    GeneralSettings g;
    auto str = [&]() { uint32_t n = r.u32(); if (n > r.remaining()) throw FormatError("overrun"); return r.str(n); };
    auto bl = [&]() { return r.u8() != 0; };
    g.string1 = str(); g.string2 = str();
    g.language = r.s32();
    g.flagA = bl(); g.width = r.u32(); g.height = r.u32();
    g.windowed = bl(); g.flagB = bl();
    Bytes d = r.bytes(32); std::memcpy(g.device, d.data(), 32);
    g.flagC = bl();
    g.musicOn = bl(); g.musicVolume = r.s32();
    g.sfxOn = bl(); g.sfxVolume = r.s32();
    g.ambienceOn = bl(); g.ambienceVolume = r.s32();
    g.flagD = bl(); g.flagE = bl(); g.flagF = bl();
    for (auto& v : g.unknown3) v = r.u32();
    g.detail.l1 = r.s32(); g.detail.l2 = r.s32(); g.detail.l3 = r.s32();
    g.flagG = bl(); g.value = r.u32();
    o = g;
    return true;
  } catch (const FormatError&) { return false; }
}

std::string GeneralSettings::deviceName() const {
  bool allFF = true;
  for (uint8_t c : device) if (c != 0xFF) allFF = false;
  if (allFF) return "";
  size_t n = 0;
  while (n < 32 && device[n]) n++;
  return std::string(reinterpret_cast<const char*>(device), n);
}
void GeneralSettings::setDeviceName(const std::string& n) {
  std::memset(device, 0, sizeof device);
  std::memcpy(device, n.data(), std::min<size_t>(n.size(), 31));
}
void GeneralSettings::repairVolumes() {
  auto bad = [](int32_t v) { return v < 0 || v > 1024; };
  if (bad(musicVolume)) musicVolume = 1000;
  if (bad(sfxVolume)) sfxVolume = 700;
  if (bad(ambienceVolume)) ambienceVolume = 900;
}

bool validateCfgBlock(int rec, const Bytes& p) {
  try {
    Reader r(p);
    switch (rec) {
      case 2: case 13: {
        if (rec == 13) r.u32();
        uint32_t n = r.u32();
        if (size_t(n) * 5 > r.remaining()) return false;
        return true;
      }
      case 3: r.skip(12 + 128); return true;
      case 4: r.skip(1); return true;
      case 5: r.skip(13); return true;
      case 6: r.skip(12); return true;
      case 7: r.skip(8); return true;
      case 8: r.skip(12); return true;
      case 9: r.skip(8); return true;
      case 10: r.skip(44); return true;
      case 11: r.skip(28); return true;
      case 12: r.skip(4); return true;
      case 14: r.skip(8 + 24); return true;
      case 15: { uint32_t n = r.u32(); r.skip(n); return true; }
      default: return false;
    }
  } catch (const FormatError&) { return false; }
}

Sc3Cfg Sc3Cfg::parse(const Bytes& ixf) {
  Sc3Cfg c;
  IxfFile f;
  if (!f.openMemory(ixf)) { c.warnings.push_back("SC3.cfg is not a valid IXF container"); return c; }
  for (int r = 1; r <= kCfgRecordCount; r++) {
    CfgRecordId id = cfgRecordId(r);
    Bytes b;
    if (!f.getBytes({id.type, kCfgGroup, id.instance}, b)) continue;
    if (r == 1) {
      if (GeneralSettings::parse(b, c.general)) c.hasGeneral = true;
      else c.warnings.push_back("record 1 malformed");
    } else if (validateCfgBlock(r, b)) c.blocks[r] = std::move(b);
    else c.warnings.push_back("record " + std::to_string(r) + " malformed");
  }
  return c;
}

Bytes Sc3Cfg::serialize() const {
  IxfWriter w;
  if (hasGeneral) { CfgRecordId id = cfgRecordId(1); w.add({id.type, kCfgGroup, id.instance}, general.serialize()); }
  for (const auto& kv : blocks) { CfgRecordId id = cfgRecordId(kv.first); w.add({id.type, kCfgGroup, id.instance}, kv.second); }
  return w.build();
}

std::vector<IdFlag> Sc3Cfg::idFlags(int rec) const {
  std::vector<IdFlag> v;
  auto it = blocks.find(rec);
  if (it == blocks.end() || (rec != 2 && rec != 13)) return v;
  Reader r(it->second);
  if (rec == 13) r.u32();
  uint32_t n = r.u32();
  for (uint32_t i = 0; i < n && r.remaining() >= 5; i++) { IdFlag f; f.id = r.u32(); f.flag = r.u8() != 0; v.push_back(f); }
  return v;
}
void Sc3Cfg::setIdFlags(int rec, const std::vector<IdFlag>& v, uint32_t lead) {
  if (rec != 2 && rec != 13) return;
  Bytes b;
  if (rec == 13) put32(b, lead);
  put32(b, uint32_t(v.size()));
  for (const auto& f : v) { put32(b, f.id); b.push_back(f.flag ? 1 : 0); }
  blocks[rec] = b;
}

// ---- preferences ----------------------------------------------------------------------------
std::string Preferences::get(const std::string& k, const std::string& d) const {
  auto it = kv_.find(k);
  return it == kv_.end() ? d : it->second;
}
long long Preferences::getInt(const std::string& k, long long d) const {
  auto it = kv_.find(k);
  if (it == kv_.end() || it->second.empty()) return d;
  char* end = nullptr;
  long long v = std::strtoll(it->second.c_str(), &end, 10);
  return end && *end == 0 ? v : d;
}
bool Preferences::getBool(const std::string& k, bool d) const {
  auto it = kv_.find(k);
  if (it == kv_.end()) return d;
  std::string v = toLower(it->second);
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  return d;
}
std::string Preferences::serialize() const {
  std::string s = "# Metroplex3K preferences\n";
  for (const auto& kv : kv_) s += kv.first + "=" + kv.second + "\n";
  return s;
}
Preferences Preferences::parse(const std::string& text) {
  Preferences p;
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string t = trimSpaces(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') continue;
    size_t eq = t.find('=');
    if (eq == std::string::npos) continue;
    std::string k = trimSpaces(t.substr(0, eq));
    if (!k.empty()) p.kv_[k] = trimSpaces(t.substr(eq + 1));
  }
  return p;
}
bool Preferences::load(const fs::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss; ss << f.rdbuf();
  *this = parse(ss.str());
  return true;
}
bool Preferences::save(const fs::path& path) const {
  std::error_code ec;
  if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f << serialize();
  return bool(f);
}
fs::path defaultPrefsDir() {
  const char* x = std::getenv("XDG_CONFIG_HOME");
  if (x && *x) return fs::path(x) / "metroplex3k";
  const char* a = std::getenv("APPDATA");
  if (a && *a) return fs::path(a) / "metroplex3k";
  const char* h = std::getenv("HOME");
  if (h && *h) return fs::path(h) / ".config" / "metroplex3k";
  return fs::path("metroplex3k");
}

OwnPrefs OwnPrefs::fromPreferences(const Preferences& p) {
  OwnPrefs o;
  o.windowed = p.getBool("windowed", true);
  o.uiScale = int(p.getInt("ui_scale", 0));
  o.vsync = p.getBool("vsync", true);
  o.gameDir = p.get("game_dir", "");
  return o;
}
void OwnPrefs::toPreferences(Preferences& p) const {
  p.setBool("windowed", windowed);
  p.setInt("ui_scale", uiScale);
  p.setBool("vsync", vsync);
  if (!gameDir.empty()) p.set("game_dir", gameDir);
}

}  // namespace m3k
