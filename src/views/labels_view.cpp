/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <cfloat>
#include <memory>
#include <string>

#include <imgui.h>

#include "../app/colours.h"
#include "labels_view.h"

void labels_view::render(const project_model &proj, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    if(ImGui::BeginChild("##LabelsScroll", ImVec2{}))
    {
        std::vector<const label *> labels;
        for(const auto &l : state.m_unlabelled)
            labels.push_back(std::addressof(l));

        if(state.has_active_file())
            for(const auto &l : proj.get_labels(state.m_active_file.value()))
                labels.push_back(std::addressof(l));

        for(const auto *l : labels)
        {
            std::string text{std::to_string(l->m_id)};

            ImU32 col = colours::grey;
            if(auto defn = proj.m_label_dict.find_ptr(l->m_defn_id))
            {
                col = defn->m_color_rgba;
                text += ": " + defn->m_class;
            }
            if(state.is_label_selected(l->m_id))
                col = colours::brighter(col);
            ImGui::PushStyleColor(ImGuiCol_Button, col);
            if(ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0)))
                state.add_action(actions::toggle_label_selection{l->m_id, !ImGui::GetIO().KeyCtrl});
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
