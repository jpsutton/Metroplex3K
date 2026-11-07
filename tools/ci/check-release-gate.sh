#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Checks that a release carries a passing release-gate report (docs/RELEASE-GATE.md) for the version in CMakeLists.txt,
# that CHANGELOG.md has its section, and that the git tag for that version does not exist yet.
# Usage: tools/ci/check-release-gate.sh [--allow-existing-tag]
set -u
cd "$(git rev-parse --show-toplevel)"
allow_tag=0; [ "${1:-}" = "--allow-existing-tag" ] && allow_tag=1
fail=0
ver=$(sed -n 's/^project(Metroplex3K VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$ver" ] || { echo "FAIL: cannot read the project version from CMakeLists.txt"; exit 1; }
echo "version: $ver"

rep="release-gate/$ver.md"
if [ ! -f "$rep" ]; then
  echo "FAIL: $rep is missing. Run tools/release-gate.sh on a machine with the game install and commit the report."; fail=1
else
  grep -q '^Result: PASS$' "$rep" || { echo "FAIL: $rep does not contain the line 'Result: PASS'"; fail=1; }
  grep -q "^Version: $ver\$" "$rep" || { echo "FAIL: $rep is for a different version (expected 'Version: $ver')"; fail=1; }
  # The gate must have run on the code being released: no engine, test, tool or build change after the commit it names.
  c=$(sed -n 's/^Commit: \([0-9a-f]\{40\}\)$/\1/p' "$rep")
  if [ -z "$c" ] || ! git cat-file -e "$c^{commit}" 2>/dev/null; then
    echo "FAIL: $rep names no commit that exists in this history (checkout needs fetch-depth: 0)"; fail=1
  elif ! git merge-base --is-ancestor "$c" HEAD; then
    echo "FAIL: the commit named in $rep ($c) is not an ancestor of this tree"; fail=1
  else
    changed=$(git diff --name-only "$c" HEAD -- src tests 'tools/*.cpp' tools/probes CMakeLists.txt)
    if [ -n "$changed" ]; then
      echo "FAIL: code changed after the gate ran at $c; run tools/release-gate.sh again:"; printf '  %s\n' $changed | head -10; fail=1
    fi
  fi
fi

grep -q "^## \[$ver\]" CHANGELOG.md || { echo "FAIL: CHANGELOG.md has no '## [$ver]' section"; fail=1; }
if grep -q "^## \[$ver\] - Unreleased" CHANGELOG.md; then
  echo "FAIL: CHANGELOG.md still says 'Unreleased' for $ver; put the release date in"; fail=1
fi

if [ "$allow_tag" = 0 ] && git ls-remote --exit-code --tags origin "refs/tags/v$ver" >/dev/null 2>&1; then
  echo "FAIL: tag v$ver already exists; bump the version in CMakeLists.txt"; fail=1
fi
[ "$fail" = 0 ] && echo "release gate check: ok"
exit "$fail"
