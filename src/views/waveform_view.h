/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <string>

#include "../app/app_state.h"
#include "../model/project_model.h"

class waveform_view
{
public:
    void render(project_model &project, app_state &state);

    std::string m_window_name{"Waveform View"};

private:
    void render_waveform(project_model &project, app_state &state);
    void render_annotations(project_model &project, app_state &state);

    coord_transform m_transform;
};