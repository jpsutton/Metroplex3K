// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// SYS.PAK reader and INI parsing with the original's quirks. See spec/02-containers.md section 5.
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "core/bytes.h"
#include "core/fsutil.h"

namespace m3k {

using IniPairs = std::vector<std::pair<std::string, std::string>>;

class IniFile {
 public:
  // From packed lines (SYS.PAK body) or loose text lines.
  static IniFile fromPackedLines(std::vector<std::string> lines);
  static IniFile fromText(const std::string& text);

  // All key/value pairs of a section, in order, including duplicates.
  // Packed copies stop at the first line with an empty key or value (spec 5.3).
  IniPairs section(const std::string& name) const;
  // First matching key (case-insensitive). Returns false if absent.
  bool get(const std::string& section, const std::string& key, std::string& out) const;
  bool has(const std::string& section) const;
  std::string getOr(const std::string& section, const std::string& key, const std::string& dflt) const;

 private:
  bool packed_ = false;
  std::vector<std::string> lines_;  // raw lines
};

class PakFile {
 public:
  bool open(const fs::path& path);
  bool has(const std::string& name) const;  // case-insensitive
  bool load(const std::string& name, IniFile& out) const;
  std::vector<std::string> names() const;
 private:
  struct Ent { std::string name; uint32_t offset; };
  Bytes data_;
  std::vector<Ent> ents_;
};

// Source resolution: SYS.PAK wins, otherwise loose file `<sysDir>/<name>`.
class IniStore {
 public:
  void setSources(const fs::path& sysDir);
  bool load(const std::string& name, IniFile& out) const;
 private:
  fs::path sysDir_;
  PakFile pak_;
  bool havePak_ = false;
};

// Number parse per spec 5.3: 0x prefix or any a-f letter => hex, else decimal; stops at invalid char.
uint32_t parseIdNumber(const std::string& s);
std::string trimSpaces(const std::string& s);  // only 0x20, per the original

}  // namespace m3k
