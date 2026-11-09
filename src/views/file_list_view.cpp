/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <imgui.h>

#include "file_list_view.h"

void file_list_view::render(project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    if(!project.has_project())
    {
        const std::string hint = "No project loaded. Set \"project\" in " +
            app_settings::m_filename +
            " to the path of your project .json and restart.";
        ImGui::TextWrapped("%s", hint.c_str());
    }

    ImGui::Text("Audio Files");
    ImGui::PushItemWidth(-FLT_MIN);
    if(ImGui::BeginListBox("##Audio Files"))
    {
        int id = 0;
        for(auto &file : project.get_files())
        {
            bool is_selected = state.is_selected_file(file.m_path);
            ImGui::PushID(id++); // Names are only unique within their dataset root
            if(ImGui::Selectable(file.m_display_name.c_str(), &is_selected) && is_selected)
                state.m_actions.push_back(actions::load_file{file.m_path});
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    ImGui::PopItemWidth();

    if(ImGui::Button("Rescan files"))
        state.add_action(actions::rescan_files{});

    if(ImGui::Button("Export CSV"))
        state.add_action(actions::export_csv{});
    ImGui::End();
}
