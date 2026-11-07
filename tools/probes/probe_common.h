// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared helpers for the probes in this directory: locate the install, boot a Game, load a saved city by name.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/game.h"
#include "core/gamedir.h"

namespace probe {
using namespace m3k;

// GAME_DIR is argv[1] unless it is absent or looks like an option; then $M3K_GAME_DIR. Returns the number of
// leading arguments consumed (0 or 1).
inline int gameDirFromArgs(int argc, char** argv, fs::path& root) {
  const char* env = std::getenv("M3K_GAME_DIR");
  int used = 0;
  std::string cand;
  if (argc > 1 && argv[1][0] != '-' && fs::exists(argv[1])) { cand = argv[1]; used = 1; }
  else if (env) cand = env;
  if (!cand.empty()) root = findGameRoot(cand);
  return used;
}

inline std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }

// Saved cities (*.sc3) of the install whose file name contains any of `filters` (all of them when empty), sorted.
inline std::vector<fs::path> findCities(const fs::path& root, const std::vector<std::string>& filters) {
  std::vector<fs::path> out;
  fs::path dir;
  for (const auto& e : fs::directory_iterator(root)) if (lower(e.path().filename().string()) == "cities") dir = e.path();
  if (dir.empty()) return out;
  for (const auto& e : fs::directory_iterator(dir)) {
    if (lower(e.path().extension().string()) != ".sc3") continue;
    std::string n = lower(e.path().filename().string());
    bool ok = filters.empty();
    for (const auto& f : filters) if (n.find(lower(f)) != std::string::npos) ok = true;
    if (ok) out.push_back(e.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

// A Game booted on the install with persistence and the intro off, past the first frames.
struct Session {
  Game game;
  Input in;
  bool open(const fs::path& root) {
    game.setPersistence(false);
    game.setIntroEnabled(false);
    std::string err;
    if (!game.init(root, err)) { std::fprintf(stderr, "cannot open install %s: %s\n", root.string().c_str(), err.c_str()); return false; }
    frames(3);
    return true;
  }
  void frames(int n, uint32_t dtMs = 16) { for (int i = 0; i < n; i++) game.frame(in, 1280, 800, dtMs); }
  bool load(const fs::path& city) { game.requestFile(city.string()); frames(3); return game.currentScreen() == Screen::InCity; }
};

inline bool needInstall(const fs::path& root, const char* usage) {
  if (!root.empty()) return false;
  std::fprintf(stderr, "no game install found.\nusage: %s\n(or set M3K_GAME_DIR)\n", usage);
  return true;
}
}  // namespace probe
