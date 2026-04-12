/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <imgui.h>

#include "file_list_view.h"

#include <iostream>

void file_list_view::render(project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    ImGui::Text("Audio Files");
    ImGui::PushItemWidth(-FLT_MIN);
    if(ImGui::BeginListBox("##Audio Files"))
    {
        for(auto &file : project.get_files())
        {
            bool is_selected =
                state.m_active_file.has_value() && state.m_active_file.value() == file.m_path;

            ImGui::Selectable(file.m_path.string().c_str(), &is_selected);

            // TODO: If selection changed, clear selected labels, etc

            if(is_selected)
                state.m_active_file = file.m_path;
        }
        ImGui::EndListBox();
    }
    ImGui::PopItemWidth();
    ImGui::End();
}
