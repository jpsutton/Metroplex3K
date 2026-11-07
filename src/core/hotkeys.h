// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Keyboard bindings (spec/08-ui-framework.md section 5): Res/Hotkeys/<LANGUAGE>.IXF tables, matching rule and
// the command numbers of the original. Windows virtual-key codes are the interface to the host.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace m3k {

enum HotkeyMod { HK_SHIFT = 1, HK_CTRL = 2, HK_ALT = 4 };

struct HotkeyBinding { int cmd = 0, arg1 = 0, arg2 = 0; };

class HotkeyTable {
 public:
  // Parses the payload grammar of section 5.2. Bad lines are ignored; a later line with the same
  // (vk, modifier mask) replaces an earlier one. Returns the number of bindings accepted.
  int parse(const std::string& text);
  // Built-in copy of the shipped ENGLISH table 1 (used when no game data / no file).
  void loadDefaults();
  // vk = Windows virtual-key code, mods = HotkeyMod mask. Auto-repeat must not be passed in.
  const HotkeyBinding* find(int vk, int mods) const;
  size_t size() const { return map_.size(); }
 private:
  std::map<std::pair<int, int>, HotkeyBinding> map_;
};

// Tool class id (MenuItem.INI COMM target) and optional picker/load parameter for a command number, 0 if the
// command is not a plain tool command. Fills `param` when the command passes a parameter.
uint32_t hotkeyToolClass(int cmd, uint32_t* param);

}  // namespace m3k
