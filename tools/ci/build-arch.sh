#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# CI (release): the Arch packages from packaging/linux/arch/PKGBUILD, written to out/.
#   docker run --rm -v "$PWD:/src" -w /src archlinux:latest tools/ci/build-arch.sh
set -euo pipefail
# shellcheck source=tools/ci/deps-arch.sh
source "$(dirname "$0")/deps-arch.sh"

# makepkg refuses to run as root.
id builder >/dev/null 2>&1 || useradd -m builder
chown -R builder: "$PWD"
# makepkg runs check() (the unit tests) too.
su builder -c 'git config --global --add safe.directory "*" && cd packaging/linux/arch && makepkg -f --nodeps --noconfirm'
mkdir -p out
mv packaging/linux/arch/*.pkg.tar.zst out/
ls -la out
