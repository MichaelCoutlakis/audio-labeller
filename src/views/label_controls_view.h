/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "../app/app_state.h"

/// The label palette: a row of pinned favourites, a box that finds classes by name, and every
/// class below them
class label_control_view
{
public:
    void render(const project_model &project, app_state &state);

    std::string m_window_name{"Label Control View"};

private:
    void render_favourites(const project_model &project, app_state &state, float width);
    void render_filter(const project_model &project, app_state &state, float width);
    void render_palette(const project_model &project, app_state &state);

    /// A button that toggles the class, and right-clicked, pins or unpins it
    /// @param hotkey the number key that also chooses it, if any
    void class_button(
        const label_defn &def,
        app_state &state,
        float width,
        std::optional<size_t> hotkey);

    std::array<char, 128> m_filter{};
};
