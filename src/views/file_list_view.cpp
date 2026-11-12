/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <algorithm>
#include <string>

#include <imgui.h>

#include "file_list_view.h"

namespace
{
const char *status_marker(file_status status)
{
    switch(status)
    {
    case file_status::done: return "[x] ";
    case file_status::skip: return "[-] ";
    case file_status::todo:
    default: return "[ ] ";
    }
}
}

void file_list_view::render(const project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    if(!project.has_project())
    {
        const std::string hint = "No project loaded. Set \"project\" in " +
            app_settings::m_filename +
            " to the path of your project .json and restart.";
        ImGui::TextWrapped("%s", hint.c_str());
    }

    // Filter
    const char *filters[] = {"All", "To do", "Done", "Skipped"};
    int filter = static_cast<int>(state.m_file_filter);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::Combo("##Filter", &filter, filters, IM_ARRAYSIZE(filters)))
        state.m_file_filter = static_cast<app_state::file_filter>(filter);

    const auto files = state.filtered_files(project);
    size_t num_finished = 0U;
    for(const auto &file : project.get_files())
        num_finished += project.get_status(file.m_path) != file_status::todo;
    ImGui::Text("%zu of %zu files done or skipped", num_finished, project.get_files().size());

    const float list_height =
        ImGui::GetContentRegionAvail().y - 4.f * ImGui::GetFrameHeightWithSpacing();
    if(ImGui::BeginListBox("##Audio Files", ImVec2{-FLT_MIN, std::max(list_height, 100.f)}))
    {
        int id = 0;
        for(const auto *file : files)
        {
            const auto num_labels = project.get_labels(file->m_path).size();
            const std::string text = status_marker(project.get_status(file->m_path)) +
                file->m_display_name +
                (num_labels ? " (" + std::to_string(num_labels) + ")" : "");

            bool is_selected = state.is_selected_file(file->m_path);
            ImGui::PushID(id++); // Names are only unique within their dataset root
            if(ImGui::Selectable(text.c_str(), &is_selected) && is_selected)
                state.add_action(actions::load_file{file->m_path});
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }

    // Status of the active file
    if(state.has_active_file())
    {
        const auto &active = state.m_active_file.value();
        int status = static_cast<int>(project.get_status(active));
        bool changed = ImGui::RadioButton("To do", &status, static_cast<int>(file_status::todo));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Done (D)", &status, static_cast<int>(file_status::done));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Skip (S)", &status, static_cast<int>(file_status::skip));
        if(changed)
            state.add_action(actions::set_file_status{active, static_cast<file_status>(status)});
    }

    if(project.num_unresolved() > 0U)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 80, 255));
        ImGui::TextWrapped(
            "%zu labels refer to audio files that weren't found. They're kept in the label file.",
            project.num_unresolved());
        ImGui::PopStyleColor();
    }

    if(ImGui::Button("Rescan files"))
        state.add_action(actions::rescan_files{});
    ImGui::SameLine();
    if(ImGui::Button("Export CSV"))
        state.add_action(actions::export_csv{});
    ImGui::End();
}
