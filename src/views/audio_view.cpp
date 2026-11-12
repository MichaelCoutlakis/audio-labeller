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

    ImGui::End();
}
