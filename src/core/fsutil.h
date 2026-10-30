// Filesystem helpers: case-insensitive path resolution (the original relies on it), file I/O.
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "core/bytes.h"

namespace m3k {
namespace fs = std::filesystem;

std::string toUpper(std::string s);
std::string toLower(std::string s);
bool iequals(const std::string& a, const std::string& b);

// Resolve `rel` (either '/' or '\\' separated) under `base`, matching each component
// case-insensitively. Returns empty path if any component is missing.
fs::path resolveCI(const fs::path& base, const std::string& rel);

// List regular files directly in `dir` whose extension equals `ext` (case-insensitive, with dot).
// Result sorted by lower-cased file name for deterministic order.
std::vector<fs::path> listFilesCI(const fs::path& dir, const std::string& ext);

bool readFile(const fs::path& p, Bytes& out);

}  // namespace m3k
