#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by the CI scripts that run in an archlinux container (as root): the packages Metroplex3K builds with.
set -euo pipefail
pacman -Syu --noconfirm --needed base-devel git cmake ninja sdl2 glew >/dev/null
