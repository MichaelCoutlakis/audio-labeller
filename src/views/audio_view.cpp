/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <imgui.h>
#include <vector>

#include "audio_view.h"

void audio_view::render(const project_model &proj, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    if(ImGui::Checkbox("Loop", &state.m_loop))
        state.add_action(actions::playback_loop{state.m_loop});
    ImGui::Checkbox("Auto play", &state.m_auto_start);
    ImGui::Checkbox("Class lanes", &state.m_class_lanes);

    std::vector<const char *> names;
    for(auto &dev : state.m_audio_devs)
        names.push_back((dev.name.c_str()));

    // Devices are remembered by name, the first being the system default:
    auto it = std::ranges::find(state.m_audio_devs, state.settings.m_device_name, &audio_dev::name);
    m_index = it != state.m_audio_devs.end()
        ? static_cast<int>(std::distance(state.m_audio_devs.begin(), it))
        : 0;

    if(ImGui::Combo("Playback Device", &m_index, names.data(), static_cast<int>(names.size())))
        state.add_action(actions::select_playback_device{state.m_audio_devs.at(m_index)});

    if(ImGui::CollapsingHeader("Keyboard shortcuts"))
    {
        static constexpr const char *keys[][2] = {
            {"1-9", "Choose a palette class (applies to the selection)"},
            {"Space", "Play / stop"},
            {"Tab, Shift+Tab", "Select and play the next / previous label"},
            {"Left, Right", "Move the selected labels"},
            {"Ctrl+Left, Ctrl+Right", "Move their start edges"},
            {"Shift+Left, Shift+Right", "Move their stop edges"},
            {"Delete", "Delete the selected labels"},
            {"Escape", "Clear the selection and palette class"},
            {"Ctrl+Z, Ctrl+Y", "Undo, redo"},
            {"N, P", "Next / previous file in the list"},
            {"D", "Mark the file done and open the next one to do"},
            {"S", "Mark the file skipped and open the next one to do"},
        };
        if(ImGui::BeginTable("##Keys", 2, ImGuiTableFlags_SizingFixedFit))
        {
            for(const auto &[key, what] : keys)
            {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(key);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(what);
            }
            ImGui::EndTable();
        }
    }

    ImGui::End();
}
