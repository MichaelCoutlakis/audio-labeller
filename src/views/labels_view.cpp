/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <cfloat>
#include <memory>
#include <string>

#include <algorithm>

#include <fmt/format.h>
#include <imgui.h>

#include "../app/colours.h"
#include "labels_view.h"

void labels_view::render(const project_model &proj, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    if(ImGui::BeginChild("##LabelsScroll", ImVec2{}))
    {
        auto labels = state.visible_labels(proj);
        std::ranges::stable_sort(
            labels,
            {},
            [](const label *l) { return std::pair{l->m_start_s, l->m_id}; });

        for(const auto *l : labels)
        {
            std::string text = l->is_onset() ? fmt::format("{:.3f} s", l->m_start_s)
                                             : fmt::format("{:.3f}-{:.3f} s", l->m_start_s, l->m_stop_s);

            ImU32 col = colours::grey;
            if(auto defn = proj.m_label_dict.find_ptr(l->m_defn_id))
            {
                col = defn->m_color_rgba;
                text += "  " + defn->m_class;
            }
            if(l->m_state == label_state::suggested)
            {
                col = colours::clearer(col, -0.4f);
                text += "  (suggested" + (l->m_score ? fmt::format(", {:.2f}", *l->m_score) : "") + ")";
            }
            if(l->m_state == label_state::rejected)
            {
                col = colours::clearer(colours::grey, -0.3f);
                text += "  (rejected)";
            }
            if(state.is_label_selected(l->m_id))
                col = colours::brighter(col);
            text += "##" + std::to_string(l->m_id);
            ImGui::PushStyleColor(ImGuiCol_Button, col);
            if(ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0)))
                state.add_action(actions::toggle_label_selection{l->m_id, !ImGui::GetIO().KeyCtrl});
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
