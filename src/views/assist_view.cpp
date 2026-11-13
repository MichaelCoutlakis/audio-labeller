/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <string>

#include <imgui.h>

#include "assist_view.h"

namespace
{
void help(const char *text)
{
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}
}

void assist_view::render(const project_model &project, app_state &state)
{
    if(!ImGui::Begin(m_window_name.c_str()))
    {
        ImGui::End();
        return;
    }
    const auto &labellers = state.m_assisted_labellers;
    if(labellers.empty() || !project.has_project())
    {
        ImGui::TextWrapped("No project loaded.");
        ImGui::End();
        return;
    }

    m_labeller = std::clamp(m_labeller, 0, static_cast<int>(labellers.size()) - 1);
    const auto &labeller = labellers[static_cast<size_t>(m_labeller)];
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::BeginCombo("##Labeller", labeller.m_name.c_str()))
    {
        for(int i = 0; i != static_cast<int>(labellers.size()); ++i)
            if(ImGui::Selectable(labellers[static_cast<size_t>(i)].m_name.c_str(), i == m_labeller))
                m_labeller = i;
        ImGui::EndCombo();
    }

    std::string class_name;
    if(labeller.is_builtin())
    {
        // The class its suggestions get: from the project, else chosen here
        if(!labeller.m_class.empty())
        {
            ImGui::Text("Class: %s", labeller.m_class.c_str());
            class_name = labeller.m_class;
        }
        else
        {
            if(!m_class && state.m_active_label_defn)
                m_class = state.m_active_label_defn;
            const auto *defn = m_class ? project.m_label_dict.find_ptr(*m_class) : nullptr;
            if(ImGui::BeginCombo("Class", defn ? defn->m_class.c_str() : "Choose..."))
            {
                for(const auto &d : project.m_label_dict)
                    if(ImGui::Selectable(d.m_class.c_str(), m_class == d.m_id))
                        m_class = d.m_id;
                ImGui::EndCombo();
            }
            if(defn)
                class_name = defn->m_class;
        }

        if(m_loudness_for != m_labeller || !m_loudness)
        {
            m_loudness = labeller.m_loudness;
            m_loudness_for = m_labeller;
        }
        auto &l = *m_loudness;
        ImGui::InputDouble("Above floor (dB)", &l.m_margin_db, 1., 3., "%.1f");
        help("How far above the background a sound must be. The background is the quietest fifth "
             "of the recording.");
        ImGui::InputDouble("Shortest (s)", &l.m_min_length_s, 0.01, 0.1, "%.3f");
        ImGui::InputDouble("Join gaps under (s)", &l.m_merge_gap_s, 0.01, 0.1, "%.3f");
        ImGui::Checkbox("Onsets rather than regions", &l.m_onsets);
        help("Each sound's start, snapped to its sharpest rise");
        l.m_min_length_s = std::max(0., l.m_min_length_s);
        l.m_merge_gap_s = std::max(0., l.m_merge_gap_s);
    }
    else if(ImGui::IsItemHovered())
    {
        std::string command;
        for(const auto &arg : labeller.m_command)
            command += (command.empty() ? "" : " ") + arg;
        ImGui::SetTooltip("%s", command.c_str());
    }

    ImGui::RadioButton("This file", &m_scope, static_cast<int>(assist_scope::active_file));
    ImGui::SameLine();
    ImGui::RadioButton("Files to do", &m_scope, static_cast<int>(assist_scope::todo_files));
    help("Files to do that have no suggestions waiting for review");
    ImGui::SameLine();
    ImGui::RadioButton("All files", &m_scope, static_cast<int>(assist_scope::all_files));
    help("Every file not skipped. Suggestions already there aren't added twice.");

    if(state.is_assisting())
    {
        const auto &p = *state.m_assist_progress;
        const auto total = std::max<size_t>(1U, p.m_total);
        const auto done = p.m_done.load();
        const auto text = std::to_string(done) + " of " + std::to_string(total);
        ImGui::ProgressBar(
            static_cast<float>(done) / static_cast<float>(total),
            ImVec2{-ImGui::GetFrameHeightWithSpacing() * 3.f, 0.f},
            text.c_str());
        ImGui::SameLine();
        if(ImGui::Button("Cancel"))
            state.add_action(actions::cancel_assist{});
    }
    else
    {
        const bool needs_file = m_scope == static_cast<int>(assist_scope::active_file) &&
            !state.has_active_file();
        const bool needs_class = labeller.is_builtin() && class_name.empty();
        ImGui::BeginDisabled(needs_file || needs_class);
        if(ImGui::Button("Suggest labels"))
            state.add_action(
                actions::run_assist{
                    static_cast<size_t>(m_labeller),
                    static_cast<assist_scope>(m_scope),
                    class_name,
                    labeller.is_builtin() ? m_loudness : std::nullopt
                });
        ImGui::EndDisabled();
        if(needs_class)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Choose a class");
        }
    }

    if(!state.m_assist_report.empty())
        ImGui::TextWrapped("%s", state.m_assist_report.c_str());
    ImGui::TextDisabled("Review them with R and Tab");
    ImGui::End();
}
