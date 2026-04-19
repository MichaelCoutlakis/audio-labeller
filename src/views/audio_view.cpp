/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <imgui.h>
#include <iostream>
#include <vector>

#include "audio_view.h"

void audio_view::render(project_model &proj, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    if(ImGui::Checkbox("Loop", &state.m_loop))
        state.add_action(actions::playback_loop{state.m_loop});
    ImGui::Checkbox("Auto play", &state.m_auto_start);

    std::vector<const char *> names;
    for(auto &dev : state.m_audio_devs)
        names.push_back((dev.name.c_str()));

    if(state.settings.dev)
    {
        auto it = std::ranges::find(state.m_audio_devs, state.settings.dev.value());
        if(it != state.m_audio_devs.end())
            m_index = std::distance(state.m_audio_devs.begin(), it);
    }
    m_index = std::clamp<int>(m_index, 0, names.size() - 1);
    if(ImGui::Combo("Playback Device", &m_index, names.data(), names.size()))
        state.add_action(actions::select_playback_device{state.m_audio_devs.at(m_index)});

    if(ImGui::IsKeyPressed(ImGuiKey_Space, false))
        state.add_action(actions::toggle_playback{});

    ImGui::End();
}
