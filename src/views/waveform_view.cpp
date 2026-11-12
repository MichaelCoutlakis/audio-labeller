/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <algorithm>
#include <chrono>
#include <imgui.h>
#include <implot.h>

#include "../app/colours.h"
#include "../app/lanes.h"
#include "waveform_view.h"

constexpr static auto as_ms = [](auto t)
{ return std::chrono::duration_cast<std::chrono::milliseconds>(t); };

void waveform_view::render(const project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

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
    // Don't use left click to pan - we want it to select label
    ImPlot::GetInputMap().Pan = ImGuiMouseButton_Middle;
    ImPlot::GetInputMap().Fit = ImGuiMouseButton_Middle; // Double click middle to zoom to fit

    if(ImPlot::BeginPlot("Waveform", ImVec2{-1.f, -1.f}, ImPlotFlags_NoLegend))
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

        if(ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_drag_seq = state.next_edit_seq();

        ImPlot::PushPlotClipRect();
        update_waveform_drag(project, state);

        process_selections(state);

        render_cursor(state);

        render_waveform(state);

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

    // Ctrl click for now to place the cursor:
    if(ImPlot::IsPlotHovered() &&
       ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
       ImGui::GetIO().KeyCtrl)
    {
        const double start_time = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
        state.select_playback_region(start_time);
    }

    double t = state.m_playback_state.playhead_s;
    const ImVec2 p0 = ImPlot::PlotToPixels(t, -1.0);
    const ImVec2 p1 = ImPlot::PlotToPixels(t, 1.0);

    dl->AddLine(p0, p1, IM_COL32(127, 0, 0, 255), 4.f);
}

void waveform_view::render_waveform(app_state &state)
{
    auto &audio_buf = state.get_audio_buffer();

    if(audio_buf.num_frames() == 0)
        return;

    const auto plot_width = std::max<size_t>(1U, static_cast<size_t>(ImPlot::GetPlotSize().x));
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

void waveform_view::render_annotations(const project_model &project, app_state &state)
{
    if(!state.m_active_file)
        return;
    for(const auto &label : project.get_labels(state.m_active_file.value()))
    {
        auto defn = project.m_label_dict.find_ptr(label.m_defn_id);
        ImU32 col = defn ? defn->m_color_rgba : colours::grey;
        if(state.is_label_selected(label.m_id))
            col = colours::brighter(col);

        const auto band = lane_of(project.m_label_dict, label.m_defn_id, state.m_class_lanes);
        render_resize_label_rect(
            state,
            label,
            col,
            defn ? defn->m_class.c_str() : "",
            band.m_y_min,
            band.m_y_max);
    }
    // Unclassified labels:
    for(const auto &label : state.m_unlabelled)
    {
        auto col = colours::grey;
        if(state.is_label_selected(label.m_id))
            col = colours::brighter(col);

        render_resize_label_rect(state, label, col, "", -1., 1.);
    }
}

void waveform_view::update_waveform_drag(const project_model &proj, app_state &state)
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
        auto drag_range = drag.get_drag_range();
        const double duration_s = state.get_audio_buffer().duration_s();
        drag_range.first = std::clamp(drag_range.first, 0., duration_s);
        drag_range.second = std::clamp(drag_range.second, 0., duration_s);

        // Only commit the drag selection if the drag duration and range exceeded some amount,
        // otherwise likely a cursor click:
        const bool commit = as_ms(t_now - m_drag_start) > ms(100) &&
            std::abs(drag_range.second - drag_range.first) > 0.02;
        drag.end_drag();

        if(commit)
        {
            state.add_action(
                actions::add_label{
                    drag_range,
                    state.m_active_file.value(),
                    state.m_active_label_defn
                });
            state.add_action(actions::select_playback_region{drag_range, state.m_loop});
        }
    }

    // Render the drag:
    if(drag.is_dragging())
    {
        auto span = drag.get_drag_range();
        auto *dl = ImPlot::GetPlotDrawList();
        ImU32 drag_colour = colours::drag_grey;

        if(state.m_active_label_defn)
            if(auto def = proj.m_label_dict.find_ptr(state.m_active_label_defn.value()))
                drag_colour = def->m_color_rgba;

        const ImVec2 p0 = ImPlot::PlotToPixels(span.first, -1.0);
        const ImVec2 p1 = ImPlot::PlotToPixels(span.second, 1.0);

        dl->AddRectFilled(p0, p1, drag_colour);
    }
}

void waveform_view::process_selections(app_state &state)
{
    if(!state.has_active_file() || !ImPlot::IsPlotHovered())
        return;

    // Hit test:
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const auto pos = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);
        state.add_action(actions::select_labels{pos.x, pos.y});
    }
}

void waveform_view::render_resize_label_rect(
    app_state &state,
    const label &l,
    ImColor col,
    const char *name,
    double y_min,
    double y_max)
{
    // ImPlot edits the values in place, so it gets a copy and any change becomes an action. The
    // vertical extent is fixed by the label's lane.
    double start{l.m_start_s}, stop{l.m_stop_s};
    double top{y_max}, bottom{y_min};
    const bool was_resized =
        ImPlot::DragRect(static_cast<int>(l.m_id), &start, &bottom, &stop, &top, col);

    if(name && *name)
    {
        const ImVec2 corner = ImPlot::PlotToPixels(l.m_start_s, y_max);
        ImPlot::GetPlotDrawList()->AddText(
            ImVec2{corner.x + 3.f, corner.y + 1.f},
            IM_COL32(255, 255, 255, 200),
            name);
    }

    if(was_resized)
    {
        m_drag_arm_id = l.m_id;
        state.add_action(actions::move_label{l.m_id, start, stop, m_drag_seq});
    }

    if(l.m_id == m_drag_arm_id && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        m_drag_arm_id.reset();
        state.select_playback_region(l.m_start_s, l.m_stop_s);
    }
}
