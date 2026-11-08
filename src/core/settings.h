// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Platform settings: language/country selection, command line, display modes, machine profile,
// SC3.cfg preferences and an own key/value preferences file. spec/27-platform.md, spec/05 section 2.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "core/bytes.h"
#include "core/fsutil.h"

namespace m3k {

// ---- languages and countries ----------------------------------------------------------------
constexpr int kLangCount = 43;            // IDs 1..43; only 1..20 have folders
constexpr int kDefaultLanguage = 1;
constexpr int kDefaultCountry = 54;       // United States
constexpr int kCountryLimit = 57;         // country indices 0..56

const char* languageFolder(int id);       // "English", ... ; nullptr for 0 / 21..43 / out of range
const char* languageLocaleName(int id);   // "english-us", ... ; nullptr as above
// Match (case-insensitively) a folder name, locale name, or decimal number < 44. 0 if none.
int languageFromName(const std::string& name);
// Windows LANGID (low 16 bits) -> language ID (spec 05 section 2.4). Never returns 0.
int languageFromLangId(uint16_t langid);
// Installed = Res/Text/<Folder>/SC3StringsApp.IXF exists (case-insensitive path match) under `appsDir`.
bool languageInstalled(const fs::path& appsDir, int id);
std::vector<int> installedLanguages(const fs::path& appsDir);

// Parade name string instance for a country (spec 05 3.3); 0 if none.
uint32_t paradeNameInstance(int country);

struct LanguageInputs {
  std::optional<uint32_t> registryDword;     // `Language` value read as DWORD
  std::optional<std::string> registryString; // `Language` value read as string (used if no DWORD)
  int cfgLanguage = -1;                      // SC3.cfg record 1 language, -1 unset
  std::string cmdLine;                       // -l argument ("" if none)
  std::string defaultLocale;                 // `Default Locale` of the start-up INI [Language]
  bool ctrlShiftHeld = false;                // start-up quirk: skip registry/cfg/INI
  std::function<bool(int)> installed;        // required
};
// Result is always an installed language when any is installed, else kDefaultLanguage.
int chooseLanguage(const LanguageInputs& in);

struct CountryInputs {
  std::optional<uint32_t> registryDword;
  std::optional<std::string> registryString;  // numeric text accepted; names are [OPEN]
  std::string cmdLine;
  bool ctrlShiftHeld = false;
};
int chooseCountry(const CountryInputs& in);

// Registry-equivalent values (spec 27 section 1): the original's key
// HKLM\\Software\\<publisher>\\<game> (see compat_names.h; or the WOW6432Node twin).
struct RegistryValues {
  std::string installedPath, scenarioCreator, ergc;
  std::optional<uint32_t> languageDword, countryDword, sku;
  std::optional<std::string> languageString, countryString;
  bool found = false;  // the key was present
  void applyTo(LanguageInputs& l, CountryInputs& c) const;
};
// Parse a Windows `.reg` export or a Wine system.reg/user.reg text and pick the values of that key.
// Key-path matching is case-insensitive; `Wow6432Node` is accepted. First matching key wins.
RegistryValues parseRegFile(const std::string& text);
// POSIX locale name (`de_DE.UTF-8`, `en_GB`) -> Windows LANGID, 0 if unknown. For picking a default
// from the OS locale (spec 05 section 2.4).
uint16_t langIdFromPosixLocale(const std::string& locale);

// ---- command line (spec 27 section 2) -------------------------------------------------------
struct MachineProfile {
  int cpuMHz = 200;    // -p a, clamped 100..1000
  int memoryMB = 128;  // -p b, >= 16
  int cpuGen = 6;      // -p c, clamped 5..10
  int memClass = 5;    // -p d, >= 4
};

struct CommandLine {
  bool hasResolution = false;
  int width = 0, height = 0;           // -r<W>x<H>; depth forced 16 bpp
  std::string device;                  // -d<name>, <= 31 chars
  std::optional<bool> fullscreen;      // -f / -w (last wins)
  std::string language;                // -l
  std::string country;                 // -c
  bool cheatsEnabled = false;          // -cheats:<value>
  std::string cheatsValue;
  bool intro = true, sound = true, music = true;
  bool restart = false;                // -restart:on
  bool restartPresent = false;         // any -restart: switch (drops -r and -d)
  std::string updaterApp;
  std::optional<MachineProfile> profile;  // -p
  std::vector<std::string> ignored;    // -x... and unknown switches
  std::vector<std::string> positional; // non-switch tokens
};

// Split a command-line string: space/tab/newline separated, double quotes group, quotes removed.
std::vector<std::string> tokenizeCommandLine(const std::string& s);
CommandLine parseCommandLine(const std::vector<std::string>& tokens);
inline CommandLine parseCommandLine(const std::string& s) { return parseCommandLine(tokenizeCommandLine(s)); }
CommandLine parseCommandLine(int argc, const char* const* argv);  // argv[0] skipped

// ---- display (spec 27 section 3) ------------------------------------------------------------
struct DisplayMode { int w = 800, h = 600; };
const std::vector<DisplayMode>& displayModes();              // 640x480, 800x600, 1024x768
std::string uiSegmentName(const DisplayMode& m);             // "00800600_MainUI.IXF"
int machineClass(int generation, int cpuMHz);                // 1..7
int memoryClass(int memoryMB);                               // floor(log2(MB))
int roundMemoryMB(uint64_t bytes);                           // (bytes>>20) rounded up to a multiple of 4
MachineProfile profileFromMeasurement(int cpuGen, int measuredMHz, uint64_t memBytes);
// Default: 640x480 below machine class 4, else 800x600. Priority: -r > cfg (non-zero) > default.
DisplayMode chooseDisplayMode(int machineCls, int cfgW, int cfgH, const CommandLine& cl);

// ---- SC3.cfg (spec 27 section 4) ------------------------------------------------------------
constexpr uint32_t kCfgGroup = 0x230E18C3;
constexpr int kCfgRecordCount = 15;
struct CfgRecordId { uint32_t type, instance; };
CfgRecordId cfgRecordId(int record);  // record 1..15

struct DetailLevels { int32_t l1 = 3, l2 = 2, l3 = 1; };
DetailLevels defaultDetail(int machineCls);

struct GeneralSettings {  // record 1
  std::string string1, string2;
  int32_t language = -1;
  bool flagA = false;
  uint32_t width = 0, height = 0;
  bool windowed = false;
  bool flagB = false;
  uint8_t device[32];   // all 0xFF = none selected
  bool flagC = false;
  bool musicOn = true;   int32_t musicVolume = 1024;
  bool sfxOn = true;     int32_t sfxVolume = 1024;
  bool ambienceOn = true; int32_t ambienceVolume = 1024;
  bool flagD = false, flagE = false, flagF = true;
  uint32_t unknown3[3] = {0, 0, 0};
  DetailLevels detail;
  bool flagG = false;
  uint32_t value = 0;
  GeneralSettings();
  // First-run defaults with detail levels from the machine class (spec 27 section 4).
  static GeneralSettings defaults(int machineClass);
  Bytes serialize() const;
  static bool parse(const Bytes& payload, GeneralSettings& out);
  std::string deviceName() const;  // up to first NUL; empty if unset (all 0xFF)
  void setDeviceName(const std::string& n);
  // Sound repair at start-up: a volume outside 0..1024 becomes music 1000 / sfx 700 / ambience 900.
  void repairVolumes();
};

struct IdFlag { uint32_t id; bool flag; };
struct Sc3Cfg {
  GeneralSettings general;
  bool hasGeneral = false;
  // Records 2..15: raw payloads (layout-validated on load); absent when missing or malformed.
  std::map<int, Bytes> blocks;
  std::vector<std::string> warnings;

  static Sc3Cfg parse(const Bytes& ixf);   // never throws; missing/garbled records are noted in warnings
  Bytes serialize() const;
  // Record 3 text: the 128-byte string cut at the first control character, forced to end at byte 127.
  std::string block3Text() const;
  // Typed views of the ID->bool tables (records 2 and 13).
  std::vector<IdFlag> idFlags(int record) const;
  void setIdFlags(int record, const std::vector<IdFlag>& v, uint32_t record13Leading = 0);
};
// Check a record 2..15 payload against its stream layout (spec 27 section 4).
bool validateCfgBlock(int record, const Bytes& payload);

// ---- own preferences file -------------------------------------------------------------------
// Text file of `key=value` lines ('#' or ';' comments). Holds everything SC3.cfg has no field for.
class Preferences {
 public:
  void set(const std::string& key, const std::string& value) { kv_[key] = value; }
  void setInt(const std::string& key, long long v) { kv_[key] = std::to_string(v); }
  void setBool(const std::string& key, bool v) { kv_[key] = v ? "1" : "0"; }
  bool has(const std::string& key) const { return kv_.count(key) != 0; }
  std::string get(const std::string& key, const std::string& dflt = "") const;
  long long getInt(const std::string& key, long long dflt) const;
  bool getBool(const std::string& key, bool dflt) const;
  std::string serialize() const;
  static Preferences parse(const std::string& text);
  bool load(const fs::path& p);
  bool save(const fs::path& p) const;
  const std::map<std::string, std::string>& all() const { return kv_; }
 private:
  std::map<std::string, std::string> kv_;
};
// Per-user writable directory (XDG_CONFIG_HOME, APPDATA, or ~/.config) + "/metroplex3k".
fs::path defaultPrefsDir();

// Own preference keys and defaults (windowed by default, per spec 27 section 9).
struct OwnPrefs {
  bool windowed = true;
  int uiScale = 0;        // 0 = automatic integer scale
  bool vsync = true;
  std::string gameDir;
  static OwnPrefs fromPreferences(const Preferences& p);
  void toPreferences(Preferences& p) const;
};

}  // namespace m3k
