#include "core/fsutil.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace m3k {

std::string toUpper(std::string s) {
  for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::string toLower(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool iequals(const std::string& a, const std::string& b) {
  return a.size() == b.size() && toLower(a) == toLower(b);
}

fs::path resolveCI(const fs::path& base, const std::string& rel) {
  fs::path cur = base;
  std::string comp;
  std::vector<std::string> parts;
  for (char c : rel) {
    if (c == '/' || c == '\\') { if (!comp.empty()) parts.push_back(comp); comp.clear(); }
    else comp.push_back(c);
  }
  if (!comp.empty()) parts.push_back(comp);
  std::error_code ec;
  for (const auto& part : parts) {
    if (part == ".") continue;
    fs::path direct = cur / part;
    if (fs::exists(direct, ec)) { cur = direct; continue; }
    bool found = false;
    for (const auto& e : fs::directory_iterator(cur, ec)) {
      if (iequals(e.path().filename().string(), part)) { cur = e.path(); found = true; break; }
    }
    if (!found) return {};
  }
  return cur;
}

std::vector<fs::path> listFilesCI(const fs::path& dir, const std::string& ext) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir, ec)) {
    if (!e.is_regular_file(ec)) continue;
    if (iequals(e.path().extension().string(), ext)) out.push_back(e.path());
  }
  std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
    return toLower(a.filename().string()) < toLower(b.filename().string());
  });
  return out;
}

bool readFile(const fs::path& p, Bytes& out) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f) return false;
  auto sz = f.tellg();
  if (sz < 0) return false;
  out.resize(size_t(sz));
  f.seekg(0);
  if (sz > 0) f.read(reinterpret_cast<char*>(out.data()), sz);
  return bool(f);
}

}  // namespace m3k
