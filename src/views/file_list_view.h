/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include "../model/project_model.h"

class file_list_view
{
public:
    void render(project_model &project)
    {
        ImGui::Begin(m_window_name.c_str());
        ImGui::Button("This is the file list view");
        ImGui::End();
    }

    std::string m_window_name{"File List View"};
};