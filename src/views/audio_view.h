/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include "../app/app_state.h"
#include "../model/project_model.h"

class audio_view
{
public:
    void render(project_model &proj, app_state &state);

    std::string m_window_name{"Audio Control"};

private:
    int m_index{0};
};