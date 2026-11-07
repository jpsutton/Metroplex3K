// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/ini.h"

#include <cctype>

namespace m3k {

std::string trimSpaces(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && s[a] == ' ') a++;
  while (b > a && s[b - 1] == ' ') b--;
  return s.substr(a, b - a);
}

uint32_t parseIdNumber(const std::string& str) {
  std::string s = trimSpaces(str);
  bool hex = false;
  size_t i = 0;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { hex = true; i = 2; }
  else {
    for (char c : s) if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) { hex = true; break; }
  }
  uint32_t v = 0;
  for (; i < s.size(); i++) {
    char c = s[i];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else break;
    v = v * (hex ? 16u : 10u) + uint32_t(d);
  }
  return v;
}

IniFile IniFile::fromPackedLines(std::vector<std::string> lines) {
  IniFile f; f.packed_ = true; f.lines_ = std::move(lines); return f;
}

IniFile IniFile::fromText(const std::string& text) {
  IniFile f; f.packed_ = false;
  std::string cur;
  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    if (c == '\r' || c == '\n') {
      if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') i++;
      f.lines_.push_back(cur); cur.clear();
    } else cur.push_back(c);
  }
  if (!cur.empty()) f.lines_.push_back(cur);
  return f;
}

bool IniFile::has(const std::string& name) const {
  for (const auto& l : lines_) {
    std::string t = packed_ ? l : trimSpaces(l);
    if (packed_ ? t == "[" + name + "]" : (t.size() > 1 && t[0] == '[' && iequals(t, "[" + name + "]"))) return true;
  }
  return false;
}

IniPairs IniFile::section(const std::string& name) const {
  IniPairs out;
  bool in = false;
  const std::string header = "[" + name + "]";
  for (const auto& raw : lines_) {
    std::string l = packed_ ? raw : trimSpaces(raw);
    if (!l.empty() && l[0] == '[') {
      if (in) break;
      in = packed_ ? (l == header) : iequals(l, header);
      continue;
    }
    if (!in) continue;
    if (!packed_ && (l.empty() || l[0] == ';')) continue;
    size_t eq = l.find('=');
    std::string k, v;
    if (eq != std::string::npos) { k = trimSpaces(l.substr(0, eq)); v = trimSpaces(l.substr(eq + 1)); }
    if (k.empty() || v.empty()) {
      if (packed_) break;  // ends the section in packed copies
      continue;
    }
    out.emplace_back(k, v);
  }
  return out;
}

bool IniFile::get(const std::string& sec, const std::string& key, std::string& out) const {
  for (const auto& kv : section(sec)) {
    if (iequals(kv.first, key)) { out = kv.second; return true; }
  }
  return false;
}

std::string IniFile::getOr(const std::string& s, const std::string& k, const std::string& d) const {
  std::string v;
  return get(s, k, v) ? v : d;
}

bool PakFile::open(const fs::path& path) {
  ents_.clear(); data_.clear();
  if (!readFile(path, data_)) return false;
  try {
    Reader r(data_);
    uint32_t count = r.u32();
    if (count > 100000) return false;
    for (uint32_t i = 0; i < count; i++) {
      uint32_t nl = r.u32();
      std::string name = r.str(nl);
      uint32_t off = r.u32();
      ents_.push_back({name, off});
    }
  } catch (const FormatError&) { ents_.clear(); return false; }
  return true;
}

bool PakFile::has(const std::string& name) const {
  for (const auto& e : ents_) if (iequals(e.name, name)) return true;
  return false;
}

std::vector<std::string> PakFile::names() const {
  std::vector<std::string> v;
  for (const auto& e : ents_) v.push_back(e.name);
  return v;
}

bool PakFile::load(const std::string& name, IniFile& out) const {
  for (size_t i = 0; i < ents_.size(); i++) {
    if (!iequals(ents_[i].name, name)) continue;
    size_t start = ents_[i].offset;
    size_t end = (i + 1 < ents_.size()) ? ents_[i + 1].offset : data_.size();
    if (start > data_.size() || end > data_.size() || start > end) return false;
    try {
      Reader r(data_.data() + start, end - start);
      uint32_t lc = r.u32();
      std::vector<std::string> lines;
      for (uint32_t k = 0; k < lc; k++) { uint32_t len = r.u32(); lines.push_back(r.str(len)); }
      out = IniFile::fromPackedLines(std::move(lines));
      return true;
    } catch (const FormatError&) { return false; }
  }
  return false;
}

void IniStore::setSources(const fs::path& sysDir) {
  sysDir_ = sysDir;
  fs::path pakPath = resolveCI(sysDir, "SYS.PAK");
  havePak_ = !pakPath.empty() && pak_.open(pakPath);
}

bool IniStore::load(const std::string& name, IniFile& out) const {
  if (havePak_ && pak_.has(name)) return pak_.load(name, out);
  fs::path p = resolveCI(sysDir_, name);
  if (p.empty()) return false;
  Bytes b;
  if (!readFile(p, b)) return false;
  out = IniFile::fromText(std::string(b.begin(), b.end()));
  return true;
}

}  // namespace m3k
