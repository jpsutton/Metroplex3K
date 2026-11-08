#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# CI (release): sign every release asset.
#   sign-release.sh <dir>
# Needs METROPLEX3K_GPG_KEY (armored private key; the release workflow passes the repo secret). For every file in <dir>:
# a detached .sig. Also SHA256SUMS (+ .sig) and the public key (metroplex3k-signing-key.asc). The Arch packages' .sig is
# what jpsutton/arch-repo checks against the key pinned in its sources.txt. Runs in an archlinux container.
set -euo pipefail
dir=${1:?asset dir}
: "${METROPLEX3K_GPG_KEY:?METROPLEX3K_GPG_KEY is not set}"

pacman -Sy --noconfirm --needed gnupg >/dev/null
export GNUPGHOME
GNUPGHOME=$(mktemp -d)
trap 'gpgconf --kill all; rm -rf "$GNUPGHOME"' EXIT
gpg --batch --quiet --import <<<"$METROPLEX3K_GPG_KEY"
fpr=$(gpg --batch --with-colons --list-secret-keys | awk -F: '/^fpr/ {print $10; exit}')
echo "signing with $fpr"

cd "$dir"
rm -f ./*.sig SHA256SUMS* metroplex3k-signing-key.asc
for file in ./*; do
  [[ -f $file ]] || continue
  gpg --batch --yes --detach-sign --no-armor "$file"
done
sha256sum -- * | grep -v -e '\.sig$' -e ' SHA256SUMS$' > SHA256SUMS
gpg --batch --yes --detach-sign --no-armor SHA256SUMS
gpg --batch --armor --export "$fpr" > metroplex3k-signing-key.asc

# Prove it: every signature verifies against the imported key.
for sig in ./*.sig; do
  gpg --batch --quiet --verify "$sig" "${sig%.sig}"
done
ls -la
