#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The release gate (docs/RELEASE-GATE.md): builds the committed tree in Release mode, runs the unit tests, then
# `conformance --strict` and `probe_verify` against a real game install, and writes release-gate/<version>.md.
# CI has no game data, so this runs on the machine that owns the install, before the release pull request.
#
# usage: tools/release-gate.sh GAME_DIR [--ref REFDIR] [--shots OUTDIR] [--build-dir DIR] [--allow-dirty]
set -u
cd "$(git rev-parse --show-toplevel)"
game=""; ref=""; shots=""; bdir="build-gate"; allow_dirty=0
while [ $# -gt 0 ]; do
  case "$1" in
    --ref) ref="$2"; shift 2;;
    --shots) shots="$2"; shift 2;;
    --build-dir) bdir="$2"; shift 2;;
    --allow-dirty) allow_dirty=1; shift;;
    -h|--help) sed -n '2,12p' "$0"; exit 0;;
    *) if [ -z "$game" ]; then game="$1"; shift; else echo "unexpected argument $1"; exit 2; fi;;
  esac
done
[ -n "$game" ] || { echo "usage: tools/release-gate.sh GAME_DIR [--ref REFDIR] [--shots OUTDIR] [--build-dir DIR] [--allow-dirty]"; exit 2; }
[ -d "$game" ] || { echo "not a directory: $game"; exit 2; }

if [ "$allow_dirty" = 0 ] && [ -n "$(git status --porcelain --untracked-files=no)" ]; then
  echo "The working tree has uncommitted changes. Commit them first: the report names a commit. (--allow-dirty for a trial run.)"; exit 2
fi
ver=$(sed -n 's/^project(Metroplex3K VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
sha=$(git rev-parse HEAD)
echo "== release gate for $ver at $sha"

gen=(); command -v ninja >/dev/null 2>&1 && gen=(-G Ninja)
cmake -S . -B "$bdir" "${gen[@]}" -DCMAKE_BUILD_TYPE=Release -DM3K_VERSION_SUFFIX="" || exit 2
cmake --build "$bdir" --parallel || exit 2

echo "== unit tests"
ctest --test-dir "$bdir" --output-on-failure --parallel 4; tests_rc=$?

echo "== conformance --strict"
"$bdir/conformance" --strict "$game"; conf_rc=$?

echo "== probe_verify"
tsv="$bdir/probe_verify.tsv"
args=("$game" --report "$tsv"); ref_used=0
[ -n "$ref" ] && { args+=(--ref "$ref"); ref_used=1; }
[ -n "$shots" ] && args+=(--shots "$shots")
"$bdir/probe_verify" "${args[@]}"; probe_rc=$?

mkdir -p release-gate
sargs=()
[ -n "$shots" ] && sargs=(--shots-dir "$shots")
python3 tools/ci/gate-report.py --version "$ver" --commit "$sha" --out "release-gate/$ver.md" \
  --tests-rc "$tests_rc" --conformance-rc "$conf_rc" --probe-rc "$probe_rc" \
  $( [ -f "$tsv" ] && echo --tsv "$tsv" ) --ref-used "$ref_used" "${sargs[@]}" \
  --host "$(uname -srm)" --date "$(date -u +%Y-%m-%dT%H:%MZ)"
rc=$?
echo
if [ "$rc" = 0 ]; then echo "GATE PASSED. Commit release-gate/$ver.md and open the release pull request."
else echo "GATE FAILED. Fix the failures; do not commit a failing report."; fi
exit "$rc"
