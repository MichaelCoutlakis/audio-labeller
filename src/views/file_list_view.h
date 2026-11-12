/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include "../app/app_state.h"
#include "../model/project_model.h"

class file_list_view
{
public:
    void render(const project_model &project, app_state &state);

    std::string m_window_name{"File List View"};
};
