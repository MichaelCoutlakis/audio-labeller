/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

#include <SDL3/SDL_opengl.h>
#include <fmt/format.h>
#include <imgui.h>
#include <implot.h>

#include "../app/colours.h"
#include "../app/lanes.h"
#include "../dsp/spectrogram.h"
#include "waveform_view.h"

constexpr static auto as_ms = [](auto t)
{ return std::chrono::duration_cast<std::chrono::milliseconds>(t); };

namespace
{
constexpr float onset_hit_px = 6.f; ///< How near a click must be to an onset to select it

/// Label text: the class, with the score of suggestions and an "x:" for rejected ones
std::string display_name(const label &l, const label_defn *defn)
{
    std::string name = defn ? defn->m_class : std::string{};
    if(l.m_state == label_state::rejected)
        name = "x:" + name;
    if(l.m_state == label_state::suggested)
        name = "? " + name + (l.m_score ? fmt::format(" {:.2f}", *l.m_score) : std::string{});
    return name;
}
}

void waveform_view::release()
{
    if(m_texture)
    {
        GLuint tex = m_texture;
        glDeleteTextures(1, &tex);
        m_texture = 0;
    }
}

void waveform_view::render(const project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    // Rezoom the time axis if the active file changes
    const bool new_file = state.m_active_file != m_last_active_file;
    if(new_file)
    {
        m_last_active_file = state.m_active_file;
        if(!state.m_view_request)
            ImPlot::SetNextAxisToFit(ImAxis_X1);
    }
    if(state.m_view_request)
    {
        ImPlot::SetNextAxisLimits(
            ImAxis_X1,
            state.m_view_request->first,
            state.m_view_request->second,
            ImPlotCond_Always);
        state.m_view_request.reset();
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

    state.m_hover_time_s.reset();
    if(ImPlot::BeginPlot("Waveform", ImVec2{-1.f, -1.f}, ImPlotFlags_NoLegend))
    {
        ImPlot::SetupAxes(
            "Time",
            state.settings.m_show_spectrogram ? "Frequency" : "Amplitude",
            ImPlotAxisFlags_NoGridLines,
            ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickLabels);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -1, 1, ImPlotCond_Always);

        auto plot_limits = ImPlot::GetPlotLimits();

        m_transform.m_view_start_s = plot_limits.X.Min;
        m_transform.m_view_end_s = plot_limits.X.Max;

        if(ImPlot::IsPlotHovered())
        {
            state.m_hover_time_s = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                m_drag_seq = state.next_edit_seq();
        }

        ImPlot::PushPlotClipRect();
        if(state.settings.m_show_spectrogram)
            render_spectrogram(state);

        update_waveform_drag(project, state);

        process_selections(state);

        render_waveform(state);

        render_cursor(state);

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
    const auto &audio_buf = state.get_audio_buffer();
    if(audio_buf.num_frames() == 0)
        return;

    const float gain = state.settings.m_waveform_gain;
    const auto plot_width = std::max<size_t>(1U, static_cast<size_t>(ImPlot::GetPlotSize().x));
    const double rate = audio_buf.m_sample_rate_hz;
    const auto frames = audio_buf.num_frames();

    // Only the visible range, a little wider so the edges are drawn
    const double margin = m_transform.view_duration() * 0.01;
    const auto first = static_cast<size_t>(
        std::clamp((m_transform.m_view_start_s - margin) * rate, 0., double(frames)));
    const auto last = static_cast<size_t>(
        std::clamp((m_transform.m_view_end_s + margin) * rate, 0., double(frames)));
    if(last <= first)
        return;
    const size_t frames_per_px = std::max<size_t>(1U, (last - first) / plot_width);

    ImPlotSpec spec;
    if(state.settings.m_show_spectrogram)
        spec.FillAlpha = 0.35f;

    m_x.clear();
    m_lo.clear();
    m_hi.clear();
    const auto clip = [](float v) { return std::clamp(v, -1.f, 1.f); };

    if(frames_per_px < 64U)
    {
        // Zoomed in past the finest pyramid level: the samples themselves
        for(size_t n = first; n != last; ++n)
            m_hi.push_back(clip(audio_buf.m_samples[n] * gain));
        ImPlot::PlotLine(
            "Waveform",
            m_hi.data(),
            static_cast<int>(m_hi.size()),
            1. / rate,
            double(first) / rate,
            spec);
        return;
    }

    const auto *level = state.get_audio_min_max_level(frames_per_px);
    if(!level || level->m_bin_times.empty())
        return;
    const size_t bins = level->m_bin_times.size();
    const size_t b0 = std::min(bins - 1U, first / level->m_samples_per_bin);
    const size_t b1 = std::min(bins, last / level->m_samples_per_bin + 1U);
    for(size_t b = b0; b < b1; ++b)
    {
        m_x.push_back(level->m_bin_times[b]);
        m_lo.push_back(clip(level->m_min_vals[b] * gain));
        m_hi.push_back(clip(level->m_max_vals[b] * gain));
    }
    ImPlot::PlotShaded(
        "Waveform",
        m_x.data(),
        m_lo.data(),
        m_hi.data(),
        static_cast<int>(m_x.size()),
        spec);
}

void waveform_view::render_spectrogram(app_state &state)
{
    const auto &audio = state.get_audio_buffer();
    if(audio.num_frames() == 0)
        return;

    const auto size = ImPlot::GetPlotSize();
    const auto &settings = state.settings;
    const spectrogram_key key{
        &audio,
        std::max(0., m_transform.m_view_start_s),
        std::min(audio.duration_s(), m_transform.m_view_end_s),
        std::clamp(static_cast<int>(size.x), 16, 4096),
        std::clamp(static_cast<int>(size.y), 16, 1024),
        settings.m_spectrogram_scale,
        settings.m_spectrogram_gain_db,
        settings.m_spectrogram_range_db
    };
    if(key.m_stop_s <= key.m_start_s)
        return;

    // While zooming or scrolling, keep the last image rather than recompute every frame
    const auto now = std::chrono::steady_clock::now();
    const bool stale = !m_spectrogram_key || !(*m_spectrogram_key == key);
    if(stale && (!m_texture || now - m_spectrogram_time > ms(120) || m_last_audio != &audio))
    {
        m_spectrogram_time = now;
        m_spectrogram_key = key;
        m_last_audio = &audio;

        spectrogram_settings s;
        s.m_scale = key.m_scale;
        const auto image = compute_spectrogram(
            audio,
            key.m_start_s,
            key.m_stop_s,
            static_cast<size_t>(key.m_width),
            static_cast<size_t>(key.m_height),
            s);

        std::vector<uint32_t> pixels(image.m_db.size());
        for(size_t r = 0U; r != image.m_rows; ++r)
        {
            for(size_t c = 0U; c != image.m_columns; ++c)
            {
                const float db = image.at(r, c) + key.m_gain_db;
                const float t = std::clamp(1.f + db / key.m_range_db, 0.f, 1.f);
                const auto colour = ImPlot::SampleColormap(t, ImPlotColormap_Viridis);
                // Row 0 of the texture is the top, the highest frequency
                pixels[(image.m_rows - 1U - r) * image.m_columns + c] = ImGui::ColorConvertFloat4ToU32(
                    t > 0.f ? colour : ImVec4{0.f, 0.f, 0.f, 1.f});
            }
        }

        if(!m_texture)
        {
            GLuint tex{};
            glGenTextures(1, &tex);
            m_texture = tex;
        }
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            static_cast<GLsizei>(image.m_columns),
            static_cast<GLsizei>(image.m_rows),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixels.data());
    }

    if(m_texture && m_spectrogram_key)
        ImPlot::PlotImage(
            "##Spectrogram",
            static_cast<ImTextureID>(m_texture),
            ImPlotPoint{m_spectrogram_key->m_start_s, -1.},
            ImPlotPoint{m_spectrogram_key->m_stop_s, 1.});
}

void waveform_view::render_annotations(const project_model &project, app_state &state)
{
    if(!state.m_active_file)
        return;

    const auto bands = state.label_bands(project);
    for(const auto *label : state.visible_labels(project))
    {
        const auto *defn = project.m_label_dict.find_ptr(label->m_defn_id);
        ImColor col = defn ? ImColor(defn->m_color_rgba) : ImColor(colours::grey);
        if(label->m_state == label_state::suggested)
            col.Value.w *= 0.45f; // Faded until reviewed
        if(label->m_state == label_state::rejected)
            col = ImColor(colours::clearer(colours::grey, -0.3f));
        if(state.is_label_selected(label->m_id))
            col = colours::brighter(col);

        const auto name = display_name(*label, defn);
        if(label->is_onset())
            render_onset(state, *label, col, name);
        else
        {
            const auto band = bands.contains(label->m_id) ? bands.at(label->m_id) : lane{};
            render_resize_label_rect(state, *label, col, name, band.m_y_min, band.m_y_max);
        }
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

    const label_defn *active_defn = state.m_active_label_defn
        ? proj.m_label_dict.find_ptr(state.m_active_label_defn.value())
        : nullptr;
    const bool onset_class = active_defn && active_defn->m_kind == class_kind::onset;

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
            // Classes marked as onsets are defined by their start alone
            if(onset_class)
                drag_range.second = drag_range.first;
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

        if(active_defn)
            drag_colour = active_defn->m_color_rgba;

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
        const double px_s = m_transform.view_duration() / std::max(1.f, ImPlot::GetPlotSize().x);
        state.add_action(actions::select_labels{pos.x, pos.y, onset_hit_px * px_s});
    }
}

void waveform_view::render_resize_label_rect(
    app_state &state,
    const label &l,
    ImColor col,
    const std::string &name,
    double y_min,
    double y_max)
{
    // ImPlot edits the values in place, so it gets a copy and any change becomes an action. The
    // vertical extent is fixed by the label's lane.
    double start{l.m_start_s}, stop{l.m_stop_s};
    double top{y_max}, bottom{y_min};
    const bool was_resized =
        ImPlot::DragRect(static_cast<int>(l.m_id), &start, &bottom, &stop, &top, col);

    if(l.m_state == label_state::suggested)
    {
        // Dashed outline, so suggestions stand out from labels at a glance
        auto *dl = ImPlot::GetPlotDrawList();
        const ImVec2 a = ImPlot::PlotToPixels(l.m_start_s, y_max);
        const ImVec2 b = ImPlot::PlotToPixels(l.m_stop_s, y_min);
        const ImU32 outline = colours::brighter(ImColor(col.Value.x, col.Value.y, col.Value.z, 1.f));
        for(float x = a.x; x < b.x; x += 8.f)
        {
            dl->AddLine({x, a.y}, {std::min(x + 4.f, b.x), a.y}, outline);
            dl->AddLine({x, b.y}, {std::min(x + 4.f, b.x), b.y}, outline);
        }
    }

    if(!name.empty())
    {
        const ImVec2 corner = ImPlot::PlotToPixels(l.m_start_s, y_max);
        ImPlot::GetPlotDrawList()->AddText(
            ImVec2{corner.x + 3.f, corner.y + 1.f},
            IM_COL32(255, 255, 255, 200),
            name.c_str());
    }

    if(was_resized)
    {
        m_drag_arm_id = l.m_id;
        state.add_action(actions::move_label{l.m_id, start, stop, m_drag_seq});
    }

    if(l.m_id == m_drag_arm_id && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        m_drag_arm_id.reset();
        const auto window = state.playback_window(l);
        state.select_playback_region(window.first, window.second);
    }
}

void waveform_view::render_onset(
    app_state &state,
    const label &l,
    ImColor col,
    const std::string &name)
{
    double t = l.m_start_s;
    ImVec4 line = col.Value;
    line.w = std::max(line.w, l.m_state == label_state::confirmed ? 0.9f : 0.5f);
    const float thickness = state.is_label_selected(l.m_id) ? 4.f : 2.f;
    const bool moved = ImPlot::DragLineX(static_cast<int>(l.m_id), &t, line, thickness);

    // A flag at the top, with the name beside it
    auto *dl = ImPlot::GetPlotDrawList();
    const ImVec2 top = ImPlot::PlotToPixels(l.m_start_s, 1.);
    const ImU32 flag = ImGui::ColorConvertFloat4ToU32(line);
    dl->AddTriangleFilled(top, {top.x + 9.f, top.y + 5.f}, {top.x, top.y + 10.f}, flag);
    if(!name.empty())
        dl->AddText({top.x + 11.f, top.y}, IM_COL32(255, 255, 255, 220), name.c_str());

    if(moved)
    {
        m_drag_arm_id = l.m_id;
        state.add_action(actions::move_label{l.m_id, t, t, m_drag_seq});
    }
    if(l.m_id == m_drag_arm_id && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        m_drag_arm_id.reset();
        const auto window = state.playback_window(l);
        state.select_playback_region(window.first, window.second);
    }
}
