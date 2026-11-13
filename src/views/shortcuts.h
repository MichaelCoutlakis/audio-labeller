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

    /// The shortcuts, as {keys, what they do}, for the help table
    static const std::vector<std::pair<const char *, const char *>> &descriptions();

private:
    void handle_nudges(const project_model &project, app_state &state);
    void handle_files(const project_model &project, app_state &state);
    void handle_review(const project_model &project, app_state &state);
    void select_adjacent_label(const project_model &project, app_state &state, bool forward);

    bool m_nudging{false};
    uint64_t m_nudge_seq{};
};
