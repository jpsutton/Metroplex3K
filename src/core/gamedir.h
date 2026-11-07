// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Locating the user's installation of the original game, and remembering it.
#pragma once
#include <string>
#include <vector>
#include "core/fsutil.h"

namespace m3k {

// If `p` (or a folder within `maxDepth` levels below it) is a game install, returns its game root
// (the directory containing Apps/). Empty path if none. Accepts the root, the Apps dir itself, or
// a parent such as "GOG Games".
fs::path findGameRoot(const fs::path& p, int maxDepth = 3);

// Typical install locations on this platform, most likely first.
std::vector<fs::path> defaultGameLocations();

// Auto-detect: environment variable M3K_GAME_DIR, saved config, then defaults.
// `exeDir` (optional): folder of the executable, searched too. Also searched: the working directory, its parent
// (so a sibling game folder is found) and the exe folder's parent.
fs::path detectGameRoot(const fs::path& exeDir = {});

// Per-user config directory for this application (created on demand).
fs::path configDir();
bool saveGameRoot(const fs::path& root);
fs::path loadSavedGameRoot();

// Path of an installer executable pattern (a GOG "setup_...exe" installer) -> true.
bool looksLikeGogInstaller(const fs::path& p);
// Extract a GOG (Inno Setup) installer into `dest` using `innoextract` if present on PATH.
// Returns the game root on success, empty path otherwise; `msg` receives a human-readable note.
fs::path extractGogInstaller(const fs::path& installer, const fs::path& dest, std::string& msg);

}  // namespace m3k
