/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <array>
#include <string>

#include "../app/app_state.h"
#include "../model/project_model.h"

class file_list_view
{
public:
    void render(const project_model &project, app_state &state);

    std::string m_window_name{"File List View"};

private:
    void render_filters(const project_model &project, app_state &state);
    void render_class_chooser(const project_model &project, app_state &state);

    std::array<char, 128> m_class_query{}; ///< Text typed to find a class to add
};
