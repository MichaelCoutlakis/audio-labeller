/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include "../app/app_state.h"



class label_control_view
{
public:
    void render(project_model &project, app_state &state);

    std::string m_window_name{"Label Control View"};

private:
    void render_label_palette(const project_model &project, app_state &state);
};