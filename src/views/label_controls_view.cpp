/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <imgui.h>
#include <implot.h>

#include "label_controls_view.h"

void label_control_view::render(project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());
    render_label_palette(project, state);
    ImGui::End();
}

void label_control_view::render_label_palette(const project_model &project, app_state &state)
{
    const float button_w = 310.0f;
    const float button_h = 32.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = ImGui::GetContentRegionAvail().x;
    const int num_cols = std::max(1, (int)(width / (button_w + spacing)));

    if(ImGui::BeginTable("Label Palette", num_cols))
    {
        for(const auto &def : project.m_label_dict)
        {
            const bool active = state.m_active_label_defn == def.m_id;
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(def.m_id));

            // Colour with a slightly more opaque "active"
            ImVec4 col_v = ImGui::ColorConvertU32ToFloat4(def.m_color_rgba);

            if(active)
            {
                col_v.x = std::min(col_v.x + 0.25f, 1.0f);
                col_v.y = std::min(col_v.y + 0.25f, 1.0f);
                col_v.z = std::min(col_v.z + 0.25f, 1.0f);

                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }
            ImGui::PushStyleColor(ImGuiCol_Button, col_v);

            if(ImGui::Button(def.m_label.c_str(), {button_w, button_h}))
            {
                // Allow state to be toggled:
                if(active && state.m_active_label_defn.has_value())
                    state.m_active_label_defn.reset();
                else
                    state.m_active_label_defn = def.m_id;
            }

            if(active)
                ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
