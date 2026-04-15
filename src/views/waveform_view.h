/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <chrono>
#include <string>

#include "../app/app_state.h"
#include "../model/project_model.h"

class waveform_view
{
public:
    waveform_view();
    void render(project_model &project, app_state &state);

    std::string m_window_name{"Waveform View"};

private:
    void render_cursor(app_state &state);
    void render_waveform(project_model &project, app_state &state);
    void render_annotations(project_model &project, app_state &state);
    void upate_waveform_drag(app_state &state);

    coord_transform m_transform;

    using tp = std::chrono::steady_clock::time_point;
    using ms = std::chrono::milliseconds;
    tp m_drag_start;
    std::optional<std::filesystem::path> m_last_active_file;
};