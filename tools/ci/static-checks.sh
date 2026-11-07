#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fast repository checks (a few seconds, no build). Run by CI on every PR to `staging` and on the release PR; run it
# locally with `tools/ci/static-checks.sh`. These are the repeatable checks from docs/IP-REVIEW.md section 4 plus
# basic hygiene. Exit status 1 when any check fails.
set -u
cd "$(git rev-parse --show-toplevel)"
fail=0
bad() { echo "FAIL: $1"; shift; printf '  %s\n' "$@" | head -20; fail=1; }

# 1. Every .cpp/.h file has an SPDX license header in its first 3 lines.
missing=$(git ls-files '*.cpp' '*.h' | while read -r f; do head -n3 "$f" | grep -q 'SPDX-License-Identifier: GPL-3.0-or-later' || echo "$f"; done)
[ -z "$missing" ] || bad "source files without an SPDX GPL-3.0-or-later header" $missing

# 2. The original game's name appears only where allowed (README, spec, compat_names.h).
hits=$(git grep -nIiE 'sim ?city' -- . ':!README.md' ':!spec' ':!src/core/compat_names.h' ':!docs/IP-REVIEW.md' ':!tools/ci/static-checks.sh' || true)
[ -z "$hits" ] || bad "the original game's name outside README.md, spec/ and src/core/compat_names.h" "$hits"

# 3. No code internals of the original program (addresses, module names, decompiler names) in source or spec.
hits=$(git grep -nIE '\b(SIM(DIRT|NTWRK|RCI|SERV|MISC|ADV|ECO|UI|SPR|DSTR|UTIL|INIT|GEOM)|SimTransit|STRTSIM|GZServiceD)\b|\bFUN_[0-9a-fA-F]{6,}|\bsub_[0-9a-fA-F]{6,}|disassembl|decompil' -- src tests tools spec ':!spec/00-README.md' ':!tools/ci/static-checks.sh' || true)
[ -z "$hits" ] || bad "internals of the original program (module names, decompiler names, disassembly)" "$hits"

# 4. No references to the analysis process in source or spec (the spec rules ban them).
hits=$(git grep -nIiE 'dirty[ -]side|clean[ -]side|dirty room|analysis team' -- src tests tools spec ':!spec/00-README.md' ':!tools/ci/static-checks.sh' || true)
[ -z "$hits" ] || bad "references to the analysis process" "$hits"

# 5. No game assets or binaries tracked.
hits=$(git ls-files | grep -iE '\.(png|bmp|jpe?g|tga|ixf|dat|xa|wav|mp3|ogg|tgq|cur|pak|exe|dll|so|dylib|a|o|obj|sc3|sct|snr|ssc|bld)$' || true)
[ -z "$hits" ] || bad "tracked binary or game-asset files" $hits

# 6. No merge-conflict markers, no oversized files.
hits=$(git grep -nIE '^(<<<<<<<|>>>>>>>) ' -- . ':!tools/ci/static-checks.sh' || true)
[ -z "$hits" ] || bad "merge-conflict markers" "$hits"
big=$(git ls-files -z | xargs -0 du -k 2>/dev/null | awk '$1 > 2048 {print $2 " (" $1 " KB)"}')
[ -z "$big" ] || bad "tracked files over 2 MB" $big

# 7. CHANGELOG.md has a section for the CMake project version.
ver=$(sed -n 's/^project(Metroplex3K VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$ver" ] || bad "cannot read the project version from CMakeLists.txt"
grep -q "^## \[$ver\]" CHANGELOG.md || bad "CHANGELOG.md has no '## [$ver]' section"

[ "$fail" = 0 ] && echo "static checks: ok"
exit "$fail"
