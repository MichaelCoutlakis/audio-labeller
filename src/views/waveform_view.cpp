/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <iostream>

#include <imgui.h>
#include <implot.h>

#include "../app/colours.h"
#include "waveform_view.h"

waveform_view::waveform_view() { }

constexpr static auto as_ms = [](auto t)
{ return std::chrono::duration_cast<std::chrono::milliseconds>(t); };

void waveform_view::render(project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());
    ImGui::Button("This is the waveform view");

    // Rezoom the time axis if the active file changes
    if(state.m_active_file != m_last_active_file)
    {
        m_last_active_file = state.m_active_file;
        ImPlot::SetNextAxisToFit(ImAxis_X1);
    }

    /*
        LMB = place cursor / drag labels
        MMB = pan, double click zoom to fit
        wheel = zoom
        RMB drag = zoom selection
    */

    ImGui::Text("Ctrl: %d", ImGui::GetIO().KeyCtrl);
    // Don't use left click to pan - we want it to select label
    ImPlot::GetInputMap().Pan = ImGuiMouseButton_Middle;
    ImPlot::GetInputMap().Fit = ImGuiMouseButton_Middle; // Double click middle to zoom to fit

    if(ImPlot::BeginPlot("Waveform"))
    {
        ImPlot::SetupAxes(
            "Time",
            "Amplitude",
            ImPlotAxisFlags_NoGridLines,
            ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -1, 1, ImPlotCond_Always);

        auto plot_limits = ImPlot::GetPlotLimits();

        m_transform.m_view_start_s = plot_limits.X.Min;
        m_transform.m_view_end_s = plot_limits.X.Max;
        ImPlot::PushPlotClipRect();
        // Update drag:
        upate_waveform_drag(project, state);

        process_selections(project, state);

        render_cursor(state);

        render_waveform(project, state);

        render_annotations(project, state);

        ImPlot::PopPlotClipRect();
        ImPlot::EndPlot();
    }
    ImGui::End();
}

void waveform_view::render_cursor(app_state &state)
{
    auto *dl = ImPlot::GetPlotDrawList();

    if(state.get_audio_buffer().m_sample_rate_hz == 0)
        return;

    double t = double(state.m_cursor_sample) / state.get_audio_buffer().m_sample_rate_hz;
    const ImVec2 p0 = ImPlot::PlotToPixels(t, -1.0);
    const ImVec2 p1 = ImPlot::PlotToPixels(t, 1.0);

    dl->AddLine(p0, p1, IM_COL32(127, 0, 0, 255), 4.f);
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
    const auto audio_level_buf = state.get_audio_min_max_level(frames_per_px);

    if(audio_level_buf)
        ImPlot::PlotShaded(
            "Waveform",
            audio_level_buf->m_bin_times.data(),
            audio_level_buf->m_min_vals.data(),
            audio_level_buf->m_max_vals.data(),
            audio_level_buf->m_bin_times.size());
}

void waveform_view::render_annotations(project_model &project, app_state &state)
{
    if(!state.m_active_file)
        return;
    for(auto &label : project.get_labels(state.m_active_file.value()))
    {
        auto defn = project.m_label_dict.find_ptr(label.m_defn_id);

        if(!defn)
            throw std::runtime_error("Definition missing for label");

        auto col = defn->m_color_rgba;
        if(state.is_label_selected(label.m_id))
            col = colours::brighter(col);

        auto *dl = ImPlot::GetPlotDrawList();
        const ImVec2 p0 = ImPlot::PlotToPixels(label.m_start_s, -1.0);
        const ImVec2 p1 = ImPlot::PlotToPixels(label.m_stop_s, 1.0);
        dl->AddRectFilled(p0, p1, col);
    }
    // Unclassified labels:
    for(auto &label : state.m_unlabelled)
    {
        auto col = colours::grey;
        if(state.is_label_selected(label.m_id))
            col = colours::brighter(col);

        auto *dl = ImPlot::GetPlotDrawList();
        const ImVec2 p0 = ImPlot::PlotToPixels(label.m_start_s, -1.0);
        const ImVec2 p1 = ImPlot::PlotToPixels(label.m_stop_s, 1.0);
        dl->AddRectFilled(p0, p1, col);
    }
}

void waveform_view::upate_waveform_drag(project_model &proj, app_state &state)
{

    if(!state.has_active_file() || !ImPlot::IsPlotHovered())
        return;

    auto &drag = state.m_drag;
    const double start_time = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
    const tp t_now = std::chrono::steady_clock::now();

    // Start drag initial timer: Prevent drag on double clicks, single clicks to set the cursor etc
    if(!drag.is_dragging() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        m_drag_start = t_now;
        drag.begin_drag(start_time);
    }

    // Update drag
    if(drag.is_dragging() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        drag.update_drag(start_time);

    // End drag
    if(drag.is_dragging() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        // Only commit the drag selection if the drag duration and range exceeded some amount,
        // otherwise likely a cursor click:
        const auto drag_range = drag.get_drag_range();
        bool commit = as_ms(t_now - m_drag_start) > ms(100) &&
            std::abs(drag_range.second - drag_range.first) > 0.02;
        drag.end_drag(commit);

        if(commit)
        {
            auto a = actions::add_label{
                drag_range,
                state.m_active_file.value(),
                state.m_active_label_defn
            };
            state.add_action(a);
        }
        else if(!commit && state.get_audio_buffer().m_sample_rate_hz != 0)
        {
            size_t sample =
                drag.get_drag_range().second * state.get_audio_buffer().m_sample_rate_hz;
            state.m_cursor_sample = sample;
        }
    }

    // Render the drag:
    if(drag.is_dragging())
    {
        auto span = drag.get_drag_range();
        auto *dl = ImPlot::GetPlotDrawList();
        auto drag_colour = colours::drag_grey;

        if(state.m_active_label_defn)
        {
            auto def = proj.m_label_dict.find_ptr(state.m_active_label_defn.value());
            if(def)
                drag_colour = def->m_color_rgba;
        }
        const ImVec2 p0 = ImPlot::PlotToPixels(span.first, -1.0);
        const ImVec2 p1 = ImPlot::PlotToPixels(span.second, 1.0);

        dl->AddRectFilled(p0, p1, drag_colour);
    }
}
void waveform_view::process_selections(project_model &proj, app_state &state)
{
    if(!state.has_active_file() || !ImPlot::IsPlotHovered())
        return;

    // Hit test:
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        state.add_action(actions::select_labels{ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x});
}
