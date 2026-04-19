/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <imgui.h>

#include "file_list_view.h"

void file_list_view::render(project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    ImGui::Text("Audio Files");
    ImGui::PushItemWidth(-FLT_MIN);
    if(ImGui::BeginListBox("##Audio Files"))
    {
        for(auto &file : project.get_files())
        {
            bool is_selected = state.is_selected_file(file.m_path);
            if(ImGui::Selectable(file.m_path.string().c_str(), &is_selected) && is_selected)
                state.m_actions.push_back(actions::load_file{file.m_path});
        }
        ImGui::EndListBox();
    }
    ImGui::PopItemWidth();

    if(ImGui::Button("Rescan files"))
        project.rescan_files();
    ImGui::End();
}
