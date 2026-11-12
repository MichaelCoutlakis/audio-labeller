/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <imgui.h>

#include "../app/app_state.h"
#include "../model/project_model.h"

class waveform_view
{
public:
    void render(const project_model &project, app_state &state);

    std::string m_window_name{"Waveform View"};

private:
    void render_cursor(app_state &state);
    void render_waveform(app_state &state);
    void render_annotations(const project_model &project, app_state &state);
    void update_waveform_drag(const project_model &proj, app_state &state);

    void process_selections(app_state &state);

    void render_resize_label_rect(app_state &state, const label &l, ImColor col);

    coord_transform m_transform;

    using tp = std::chrono::steady_clock::time_point;
    using ms = std::chrono::milliseconds;
    tp m_drag_start;
    std::optional<label_id> m_drag_arm_id;
    uint64_t m_drag_seq{}; ///< Incremented per mouse press, so each drag is one undo step
    std::optional<std::filesystem::path> m_last_active_file;
};
