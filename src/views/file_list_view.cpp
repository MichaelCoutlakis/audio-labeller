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
    const char *filters[] = {"All", "To do", "Done", "Skipped", "With suggestions"};
    int filter = static_cast<int>(state.settings.m_file_filter);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::Combo("##Filter", &filter, filters, IM_ARRAYSIZE(filters)))
        state.settings.m_file_filter = static_cast<file_filter>(filter);

    const auto files = state.filtered_files(project);
    size_t num_finished = 0U;
    for(const auto &file : project.get_files())
        num_finished += project.get_status(file.m_path) != file_status::todo;
    ImGui::Text("%zu of %zu files done or skipped", num_finished, project.get_files().size());

    const auto trees = state.m_annotation_trees.size();
    const float list_height = ImGui::GetContentRegionAvail().y -
        (5.f + static_cast<float>(trees)) * ImGui::GetFrameHeightWithSpacing();
    if(ImGui::BeginListBox("##Audio Files", ImVec2{-FLT_MIN, std::max(list_height, 100.f)}))
    {
        int id = 0;
        for(const auto *file : files)
        {
            size_t num_labels = 0U, num_suggested = 0U;
            for(const auto &l : project.get_labels(file->m_path))
            {
                num_labels += l.is_confirmed();
                num_suggested += l.m_state == label_state::suggested;
            }
            std::string counts;
            if(num_labels || num_suggested)
                counts = " (" + std::to_string(num_labels) +
                    (num_suggested ? ", " + std::to_string(num_suggested) + "?" : "") + ")";
            const auto *file_state = project.find_file_state(file->m_path);
            const bool exported =
                file_state && file_state->m_export && file_state->m_export->m_verified;
            const std::string text = status_marker(project.get_status(file->m_path)) +
                file->m_display_name +
                counts +
                (exported ? " - exported" : "");

            bool is_selected = state.is_selected_file(file->m_path);
            ImGui::PushID(id++); // Names are only unique within their dataset root
            if(ImGui::Selectable(text.c_str(), &is_selected) && is_selected)
                state.add_action(actions::load_file{file->m_path});
            if(exported && ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Exported %s and verified: the original is safe to delete",
                    file_state->m_export->m_time.c_str());
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

    if(state.is_exporting())
    {
        const auto &progress = *state.m_export_progress;
        const auto total = std::max<size_t>(1U, progress.m_total);
        const auto done = progress.m_done.load();
        const auto text = "Exporting " + std::to_string(done) + " of " + std::to_string(total);
        ImGui::ProgressBar(
            static_cast<float>(done) / static_cast<float>(total),
            ImVec2{-ImGui::GetFrameHeightWithSpacing() * 3.f, 0.f},
            text.c_str());
        ImGui::SameLine();
        if(ImGui::Button("Cancel"))
            state.add_action(actions::cancel_export{});
    }
    else
    {
        if(ImGui::Button("Export clips"))
            state.add_action(actions::export_dataset{});
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("Cut clips around the labels of files marked done, for training");
        ImGui::SameLine();
        if(ImGui::Button("Settings..."))
            state.m_show_export_settings = true;
    }

    // Annotation trees from the project, for exchanging label files with other tools
    for(size_t i = 0U; i != state.m_annotation_trees.size(); ++i)
    {
        const auto &tree = state.m_annotation_trees[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tree.m_name.c_str());
        if(tree.m_import != annotation_tree_settings::import_mode::none)
        {
            ImGui::SameLine();
            if(ImGui::Button("Import"))
                state.add_action(actions::import_annotations{i});
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    tree.m_import == annotation_tree_settings::import_mode::suggestions
                        ? "Read the label files as suggestions to review"
                        : "Read the label files as labels");
        }
        if(tree.m_export)
        {
            ImGui::SameLine();
            if(ImGui::Button("Export"))
                state.add_action(actions::export_annotations{i});
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Write a label file for each file marked done (empty if it has no events)");
        }
        ImGui::PopID();
    }
    ImGui::End();
}
