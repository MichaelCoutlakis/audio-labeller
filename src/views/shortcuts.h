/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstdint>

#include "../app/app_state.h"
#include "../model/project_model.h"

/// Global keyboard shortcuts, turned into actions. See the README for the list.
class keyboard_controller
{
public:
    void handle(const project_model &project, app_state &state);

    /// Seconds an edge moves per nudge
    static constexpr double nudge_s = 0.01;

private:
    void handle_nudges(const project_model &project, app_state &state);
    void handle_files(const project_model &project, app_state &state);
    void select_adjacent_label(const project_model &project, app_state &state, bool forward);

    bool m_nudging{false};
    uint64_t m_nudge_seq{};
};
