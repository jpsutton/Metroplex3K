// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstdio>
#include <cstdlib>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)
#define TEST_MAIN_END() do { if (g_failures) { std::fprintf(stderr, "%d failure(s)\n", g_failures); return 1; } std::puts("ok"); return 0; } while (0)
