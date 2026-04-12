/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <imgui.h>
#include <implot.h>

#include "waveform_view.h"

void waveform_view::render(project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());
    ImGui::Button("This is the waveform view");

    if(ImPlot::BeginPlot("Waveform"))
    {
        ImPlot::SetupAxes(
            "Time", "Amplitude", ImPlotAxisFlags_NoGridLines, ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -1, 1, ImPlotCond_Always);

        auto plot_limits = ImPlot::GetPlotLimits();

        m_transform.m_view_start_s = plot_limits.X.Min;
        m_transform.m_view_end_s = plot_limits.X.Max;

        render_waveform(project, state);

        render_annotations(project, state);

        ImPlot::EndPlot();
    }
    ImGui::End();
}

void waveform_view::render_waveform(project_model &project, app_state &state)
{
    auto &audio_buf = state.get_audio_buffer();

    if(audio_buf.num_frames() == 0)
        return;

    const auto plot_width = static_cast<size_t>(ImPlot::GetPlotSize().x);
    const auto visible_frames =
        static_cast<size_t>(m_transform.view_duration() * audio_buf.m_sample_rate_hz);
    const size_t frames_per_px = std::max<size_t>(1U, visible_frames / plot_width);

    ImPlot::PlotShaded("Waveform",
        audio_buf.m_samples.data(),
        audio_buf.num_frames(),
        0.0,
        1.0 / audio_buf.m_sample_rate_hz);
}

void waveform_view::render_annotations(project_model &project, app_state &state) { }
