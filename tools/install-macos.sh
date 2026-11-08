#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs the latest Metroplex3K release on macOS (Apple silicon):
#
#   curl -fsSL https://raw.githubusercontent.com/jpsutton/Metroplex3K/release/tools/install-macos.sh | bash
#
# Options (pass them after `bash -s --`):  --tools   also install the developer tools (diag, conformance, probe_verify)
#
# The builds are not notarized by Apple, so an archive downloaded in a browser gets the quarantine attribute and
# Gatekeeper blocks the first launch. curl doesn't set that attribute, so what this script installs starts without a
# prompt. The download is checked against the release's SHA256SUMS. Installs to ~/Applications/Metroplex3K; set
# METROPLEX3K_INSTALL_DIR to choose another folder. Metroplex3K needs SDL2 (brew install sdl2); the script says so when
# it is missing. Run the same command again to update.
set -euo pipefail

base=${METROPLEX3K_RELEASE_URL:-https://github.com/jpsutton/Metroplex3K/releases/latest/download}   # the override is for testing
tools=0
for arg in "$@"; do
  case $arg in
    --tools) tools=1 ;;
    *) echo "install-macos: unknown option $arg" >&2; exit 2 ;;
  esac
done

die() { echo "install-macos: $*" >&2; exit 1; }

[[ $(uname -s) == Darwin ]] || die "this script is for macOS"
[[ $(uname -m) == arm64 ]] || die "Metroplex3K ships for Apple silicon (arm64) only"

dest=${METROPLEX3K_INSTALL_DIR:-$HOME/Applications/Metroplex3K}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

curl -fsSL -o "$work/SHA256SUMS" "$base/SHA256SUMS"

fetch() {  # fetch <name pattern>: download one asset named by SHA256SUMS and verify it
  local name
  name=$(grep -oE "$1" "$work/SHA256SUMS" | head -n 1) || true
  [[ -n $name ]] || die "no $2 in the latest release"
  echo "Downloading $name" >&2
  curl -fL --progress-bar -o "$work/$name" "$base/$name"
  (cd "$work" && grep " $name\$" SHA256SUMS | shasum -a 256 -c -s) || die "checksum mismatch for $name"
  echo "$work/$name"
}

app=$(fetch 'metroplex3k-[0-9][^ ]*-Darwin-arm64\.zip$' "macOS archive")
mkdir -p "$dest"
unzip -qo "$app" -d "$dest"
chmod +x "$dest/metroplex3k"
echo "Installed $dest/metroplex3k"

if (( tools )); then
  tools_zip=$(fetch 'metroplex3k-[0-9][^ ]*-Darwin-arm64-tools\.zip$' "macOS tools archive")
  unzip -qo "$tools_zip" -d "$dest"
  echo "Installed the tools in $dest/tools"
fi

if ! { [[ -e /opt/homebrew/lib/libSDL2-2.0.0.dylib ]] || [[ -e /usr/local/lib/libSDL2-2.0.0.dylib ]]; }; then
  echo
  echo "Metroplex3K needs SDL2, which was not found. Install it with Homebrew:  brew install sdl2"
fi
echo
echo "Run it with:  $dest/metroplex3k --game-dir \"/path/to/your/game\""
echo "Metroplex3K needs the data files of your own copy of the original game; none are included."
