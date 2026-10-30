#include "core/gamedir.h"

#include <cstdlib>
#include <fstream>

namespace m3k {

static bool isRoot(const fs::path& p) {
  return !resolveCI(p, "Apps/Sys/SYS.PAK").empty();
}
static bool isAppsDir(const fs::path& p) {
  return !resolveCI(p, "Sys/SYS.PAK").empty() && !resolveCI(p, "Res").empty();
}

static fs::path search(const fs::path& p, int depth) {
  std::error_code ec;
  if (!fs::is_directory(p, ec)) return {};
  if (isRoot(p)) return p;
  if (isAppsDir(p)) return p.parent_path();
  if (depth <= 0) return {};
  for (const auto& e : fs::directory_iterator(p, ec)) {
    if (!e.is_directory(ec)) continue;
    std::string n = toLower(e.path().filename().string());
    if (n == "res" || n == "sys" || n == "cities" || n == "windows" || n == "proc" || n[0] == '.') continue;
    fs::path r = search(e.path(), depth - 1);
    if (!r.empty()) return r;
  }
  return {};
}

fs::path findGameRoot(const fs::path& p, int maxDepth) { return search(p, maxDepth); }

static std::string homeDir() {
  if (const char* h = std::getenv("HOME")) return h;
  if (const char* h = std::getenv("USERPROFILE")) return h;
  return {};
}

std::vector<fs::path> defaultGameLocations() {
  std::vector<fs::path> v;
  std::string h = homeDir();
  const char* names[] = {"SimCity 3000 Unlimited", "SimCity 3000", "simcity-3000-unlimited", "simcity_3000_unlimited"};
#ifdef _WIN32
  for (auto n : names) {
    v.push_back(std::string("C:\\GOG Games\\") + n);
    v.push_back(std::string("C:\\Program Files (x86)\\GOG Galaxy\\Games\\") + n);
    v.push_back(std::string("D:\\GOG Games\\") + n);
  }
#else
  for (auto n : names) {
    v.push_back(h + "/GOG Games/" + n);
    v.push_back(h + "/Games/" + n);
    v.push_back(h + "/Games/" + n + "/drive_c/GOG Games/" + n);
    v.push_back(h + "/Games/gog/" + n);
    v.push_back(h + "/Games/gog/" + n + "/drive_c/GOG Games/" + n);
    v.push_back(h + "/.wine/drive_c/GOG Games/" + n);
    v.push_back(h + "/.local/share/" + n);
    v.push_back(std::string("/Applications/") + n);
    v.push_back(h + "/Applications/" + n);
    v.push_back(std::string("/opt/") + n);
  }
  v.push_back(h + "/GOG Games");
  v.push_back(h + "/Games");
#endif
  return v;
}

fs::path configDir() {
  fs::path d;
#ifdef _WIN32
  if (const char* a = std::getenv("APPDATA")) d = fs::path(a) / "Metroplex3K";
#elif defined(__APPLE__)
  d = fs::path(homeDir()) / "Library/Application Support/Metroplex3K";
#else
  if (const char* x = std::getenv("XDG_CONFIG_HOME")) d = fs::path(x) / "metroplex3k";
  else d = fs::path(homeDir()) / ".config/metroplex3k";
#endif
  std::error_code ec;
  if (!d.empty()) fs::create_directories(d, ec);
  return d;
}

bool saveGameRoot(const fs::path& root) {
  fs::path d = configDir();
  if (d.empty()) return false;
  std::ofstream f(d / "gamedir.txt");
  if (!f) return false;
  f << root.string() << "\n";
  return bool(f);
}

fs::path loadSavedGameRoot() {
  fs::path d = configDir();
  if (d.empty()) return {};
  std::ifstream f(d / "gamedir.txt");
  std::string line;
  if (f && std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    return line;
  }
  return {};
}

fs::path detectGameRoot(const fs::path& exeDir) {
  if (const char* e = std::getenv("M3K_GAME_DIR")) {
    fs::path r = findGameRoot(e);
    if (!r.empty()) return r;
  }
  fs::path s = loadSavedGameRoot();
  if (!s.empty()) { fs::path r = findGameRoot(s, 0); if (!r.empty()) return r; }
  {
    std::error_code ec;
    std::vector<fs::path> here;
    fs::path cwd = fs::current_path(ec);
    if (!ec) { here.push_back(cwd); if (cwd.has_parent_path()) here.push_back(cwd.parent_path()); }
    if (!exeDir.empty()) { here.push_back(exeDir); if (exeDir.has_parent_path()) here.push_back(exeDir.parent_path()); }
    for (const auto& c : here) { fs::path r = findGameRoot(c, 2); if (!r.empty()) return r; }
  }
  for (const auto& c : defaultGameLocations()) {
    fs::path r = findGameRoot(c, 3);
    if (!r.empty()) return r;
  }
  return {};
}

bool looksLikeGogInstaller(const fs::path& p) {
  std::string n = toLower(p.filename().string());
  std::string ext = toLower(p.extension().string());
  return ext == ".exe" && (n.find("simcity") != std::string::npos || n.find("setup") != std::string::npos);
}

fs::path extractGogInstaller(const fs::path& installer, const fs::path& dest, std::string& msg) {
#if defined(__ANDROID__)
  msg = "Installer extraction is not available on this platform. Copy the installed game folder instead.";
  return {};
#else
  std::error_code ec;
  fs::create_directories(dest, ec);
  // innoextract is the standard open-source Inno Setup extractor (GOG installers use Inno Setup).
  std::string cmd = "innoextract --silent --exclude-temp -d \"" + dest.string() + "\" \"" + installer.string() + "\"";
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    msg = "Could not run 'innoextract' (exit code " + std::to_string(rc) + "). Install innoextract "
          "(https://constexpr.org/innoextract/) and try again, or install the game normally and point "
          "Metroplex3K at the install folder.";
    return {};
  }
  fs::path root = findGameRoot(dest, 3);
  if (root.empty()) { msg = "Extraction finished but no game data (Apps/Sys/SYS.PAK) was found."; return {}; }
  msg = "Extracted game data to " + root.string();
  return root;
#endif
}

}  // namespace m3k
