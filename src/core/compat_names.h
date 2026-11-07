// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Names of the third-party game's files, folders, registry key and cheat phrase that the engine must match at run
// time to interoperate with a copy of the game the user owns. They are interface facts, not branding. Keep every
// occurrence of the game's name in the source tree in this file; elsewhere refer to "the original game".
#pragma once

namespace m3k {
namespace compat {

// Folder names an install is commonly found under (auto-detect, README "Supplying your game files").
inline constexpr const char* kGameFolderNames[] = {"SimCity 3000 Unlimited", "SimCity 3000", "simcity-3000-unlimited",
                                                    "simcity_3000_unlimited"};

// Lower-case word that marks an installer executable's file name (GOG "setup_<word>_3000...exe").
inline constexpr const char* kInstallerNameWord = "simcity";

// Lower-case tail of the registry key the original writes its settings under (spec 27 section 1).
inline constexpr const char* kRegistryKeyTail = "electronic arts\\maxis\\simcity 3000 unlimited";

// Cheat phrase the original recognises (spec 27 section 5).
inline constexpr const char* kCheatPhraseGame = "simcity";

}  // namespace compat
}  // namespace m3k
