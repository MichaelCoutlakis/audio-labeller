/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <string>
#include <vector>

#include <imgui.h>

#include "audio_view.h"
#include "shortcuts.h"

void audio_view::render(const project_model &proj, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());
    auto &settings = state.settings;

    ImGui::SeparatorText("Display");
    const char *layouts[] = {"Stacked where they overlap", "Lane per class", "Full height"};
    int layout = static_cast<int>(settings.m_layout);
    if(ImGui::Combo("Labels", &layout, layouts, IM_ARRAYSIZE(layouts)))
        settings.m_layout = static_cast<label_layout>(layout);
    ImGui::Checkbox("Show rejected (H)", &settings.m_show_rejected);

    ImGui::SliderFloat(
        "Waveform gain",
        &settings.m_waveform_gain,
        1.f,
        100.f,
        "%.1fx",
        ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox("Spectrogram below the waveform", &settings.m_show_spectrogram);
    if(settings.m_show_spectrogram)
    {
        const char *scales[] = {"Linear", "Log", "Mel"};
        int scale = static_cast<int>(settings.m_spectrogram_scale);
        if(ImGui::Combo("Frequency scale", &scale, scales, IM_ARRAYSIZE(scales)))
            settings.m_spectrogram_scale = static_cast<frequency_scale>(scale);
        ImGui::SliderFloat("Gain", &settings.m_spectrogram_gain_db, -20.f, 60.f, "%.0f dB");
        ImGui::SliderFloat("Range", &settings.m_spectrogram_range_db, 20.f, 140.f, "%.0f dB");
    }

    // Channel of the active file
    if(state.has_active_file())
    {
        const auto &file = state.m_active_file.value();
        const auto channels = state.get_audio_buffer().m_source_channels;
        if(channels > 1U)
        {
            std::vector<std::string> options{"Mix of all"};
            for(uint32_t c = 1U; c <= channels; ++c)
                options.push_back("Channel " + std::to_string(c));
            const auto current = proj.get_channel(file);
            int index = current ? static_cast<int>(*current) + 1 : 0;
            if(ImGui::BeginCombo("Channel", options[static_cast<size_t>(index)].c_str()))
            {
                for(int i = 0; i != static_cast<int>(options.size()); ++i)
                    if(ImGui::Selectable(options[static_cast<size_t>(i)].c_str(), i == index))
                        state.add_action(
                            actions::set_channel{
                                file,
                                i == 0 ? std::nullopt
                                       : std::optional<uint32_t>{static_cast<uint32_t>(i - 1)}
                            });
                ImGui::EndCombo();
            }
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("Shown and played. Remembered for this file.");
        }
    }

    ImGui::SeparatorText("Playback");
    if(ImGui::Checkbox("Loop", &state.m_loop))
        state.add_action(actions::playback_loop{state.m_loop});
    ImGui::SameLine();
    ImGui::Checkbox("Auto play", &state.m_auto_start);

    float speed = settings.m_playback_speed;
    if(ImGui::SliderFloat("Speed", &speed, 0.1f, 1.f, "%.2fx"))
        state.add_action(actions::set_playback_speed{speed});
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Slower playback, which also lowers the pitch. Ctrl+click to type.");

    std::vector<const char *> names;
    for(auto &dev : state.m_audio_devs)
        names.push_back((dev.name.c_str()));

    // Devices are remembered by name, the first being the system default:
    auto it = std::ranges::find(state.m_audio_devs, settings.m_device_name, &audio_dev::name);
    m_index = it != state.m_audio_devs.end()
        ? static_cast<int>(std::distance(state.m_audio_devs.begin(), it))
        : 0;

    if(ImGui::Combo("Playback Device", &m_index, names.data(), static_cast<int>(names.size())))
        state.add_action(actions::select_playback_device{state.m_audio_devs.at(m_index)});

    ImGui::SeparatorText("Editing");
    double nudge_ms = settings.m_nudge_s * 1000.;
    if(ImGui::InputDouble("Nudge (ms)", &nudge_ms, 1., 10., "%.1f"))
        settings.m_nudge_s = std::clamp(nudge_ms, 0.1, 1000.) / 1000.;
    double fine_ms = settings.m_fine_nudge_s * 1000.;
    if(ImGui::InputDouble("Fine nudge, Alt (ms)", &fine_ms, 0.1, 1., "%.2f"))
        settings.m_fine_nudge_s = std::clamp(fine_ms, 0.01, 1000.) / 1000.;

    ImGui::SeparatorText("Review");
    ImGui::Checkbox("Review mode (R)", &state.m_review_mode);
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Tab steps through the suggestions in every file, lowest score first if they have "
            "scores, playing and zooming to each");
    ImGui::Checkbox("Next suggestion after a verdict", &settings.m_auto_advance);
    const auto pending = review_queue(proj, state.m_review).size();
    ImGui::Text("%zu suggestions to review", pending);

    if(ImGui::CollapsingHeader("Keyboard shortcuts"))
    {
        if(ImGui::BeginTable("##Keys", 2, ImGuiTableFlags_SizingFixedFit))
        {
            for(const auto &[key, what] : keyboard_controller::descriptions())
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
