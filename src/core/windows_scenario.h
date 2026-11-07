// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Builds ScenarioGoalsView from the scenario runtime state (spec 09 6.10, spec 23).
// Separate header: keeps scenario.h/script.h (ScenarioRng) out of translation units using windows.h.
#pragma once
#include <functional>
#include <string>
#include "core/scenario.h"
#include "core/win_scenario_view.h"

namespace m3k {

// `text(group, id)` resolves string-table entries (empty = missing -> embedded scenario strings, then
// a literal). Strings are token-expanded with `ctx`.
ScenarioGoalsView makeScenarioGoalsView(const std::function<std::string(uint32_t, uint32_t)>& text,
                                        const ScenarioState& st, const ScenarioFile* file,
                                        const ScenarioTextContext& ctx, const std::function<std::string(int)>& neighbourMayor = nullptr);

// Presenter (id, kind) of a scenario: for an advisor `advisor` is set, otherwise `pic` (picture and name). `neighbourMayor(n)` gives a neighbour's
// mayor name (kind 0 ids 0x14..0x17). `text` as above.
struct PresenterChoice { uint32_t advisor = 0; ScenarioPresenterPic pic; };
PresenterChoice resolvePresenterChoice(const std::function<std::string(uint32_t, uint32_t)>& text, const std::function<std::string(int)>& neighbourMayor,
                                       const ScenarioFile& file, uint32_t id, uint32_t kind);

}  // namespace m3k
