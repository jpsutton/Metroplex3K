#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Adds Metroplex3K to the application menu from an unpacked Linux archive (the Arch package does this itself):
#   ./install-desktop.sh            install for the current user (~/.local/share)
#   ./install-desktop.sh --remove   remove it again
# The menu entry starts the metroplex3k next to this script.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
desktop=$data/applications/metroplex3k.desktop
icon=$data/icons/hicolor/scalable/apps/metroplex3k.svg

if [[ ${1:-} == --remove ]]; then
  rm -f "$desktop" "$icon"
  echo "Removed the Metroplex3K menu entry."
  exit 0
fi

[[ -x $here/metroplex3k ]] || { echo "install-desktop: no metroplex3k next to this script" >&2; exit 1; }
src=$here/launcher
[[ -f $src/metroplex3k.desktop && -f $src/metroplex3k.svg ]] || { echo "install-desktop: launcher files missing in $src" >&2; exit 1; }

mkdir -p "$(dirname "$desktop")" "$(dirname "$icon")"
cp "$src/metroplex3k.svg" "$icon"
# Absolute path to the program; quote it for the Exec line.
exec_path=$here/metroplex3k
sed -e "s|^Exec=.*|Exec=\"${exec_path//\\/\\\\}\"|" -e "s|^Path=.*||" "$src/metroplex3k.desktop" > "$desktop"
chmod 644 "$desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$data/applications" >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t "$data/icons/hicolor" >/dev/null 2>&1 || true
echo "Installed the Metroplex3K menu entry for $exec_path"
