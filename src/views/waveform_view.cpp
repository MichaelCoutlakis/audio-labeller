/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <implot.h>
#include <spdlog/fmt/fmt.h> // spdlog's fmt, bundled or installed

#include "../app/colours.h"
#include "../app/lanes.h"
#include "../dsp/spectrogram.h"
#include "waveform_view.h"

constexpr static auto as_ms = [](auto t)
{ return std::chrono::duration_cast<std::chrono::milliseconds>(t); };

namespace
{
constexpr float onset_hit_px = 6.f;  ///< How near a click must be to an onset to select it
constexpr double min_box_px = 6.;    ///< The narrowest a region's box is drawn or resized to
constexpr double min_band_px = 8.;   ///< A drag on the spectrogram shorter than this is time only
constexpr double edge_px = 3.;       ///< A band this near the top or bottom reaches the edge
constexpr size_t max_flag_rows = 4U; ///< Flags of more overlapping regions share the last row

/// Shift+click places the playhead, so selects and draws nothing
bool shift_click() { return ImGui::GetIO().KeyShift; }

/// A label's colour: its class's, faded until reviewed, grey if rejected, brighter if selected
ImColor label_colour(const label &l, const label_defn *defn, const app_state &state)
{
    ImColor col = defn ? ImColor(defn->m_color_rgba) : ImColor(colours::grey);
    if(l.m_state == label_state::suggested)
        col.Value.w *= 0.45f;
    if(l.m_state == label_state::rejected)
        col = ImColor(colours::clearer(colours::grey, -0.3f));
    if(state.is_label_selected(l.m_id))
        col = colours::brighter(col);
    return col;
}

ImColor opaque(ImColor c, float alpha) { return ImColor(c.Value.x, c.Value.y, c.Value.z, alpha); }

/// Label text: the class, with the score of suggestions and an "x:" for rejected ones. A review
/// list row's says it's the list's, with its class even if the palette lacks it.
std::string display_name(const label &l, const label_defn *defn, const app_state &state)
{
    if(app_state::row_of(l.m_id))
        return state.row_label_text(l.m_id);
    std::string name = defn ? defn->m_class : std::string{};
    if(l.m_state == label_state::rejected)
        name = "x:" + name;
    if(l.m_state == label_state::suggested)
        name = "? " + name + (l.m_score ? fmt::format(" {:.2f}", *l.m_score) : std::string{});
    return name;
}
}

double waveform_view::frequency_axis::y_of(double hz) const
{
    hz = std::clamp(hz, m_min_hz, m_max_hz);
    return frequency_position(hz, m_min_hz, m_max_hz, m_scale) * 2. - 1.;
}

double waveform_view::frequency_axis::hz_of(double y) const
{
    return row_frequency((std::clamp(y, -1., 1.) + 1.) / 2., 1U, m_min_hz, m_max_hz, m_scale);
}

frequency_band waveform_view::frequency_axis::band(double y0, double y1) const
{
    const double lo = std::min(y0, y1), hi = std::max(y0, y1);
    frequency_band b;
    if(lo > -1. + m_edge)
        b.m_low_hz = hz_of(lo);
    if(hi < 1. - m_edge)
        b.m_high_hz = hz_of(hi);
    return b;
}

frequency_band waveform_view::drag_band() const
{
    if(!m_drag_spectrogram ||
       std::abs(m_drag_y1 - m_drag_y0) / 2. * m_drag_plot_height < min_band_px)
        return {};
    return m_axis.band(m_drag_y0, m_drag_y1);
}

void waveform_view::release()
{
    for(auto &[key, t] : m_textures)
    {
        GLuint tex = t.m_texture;
        glDeleteTextures(1, &tex);
    }
    m_textures.clear();
}

void waveform_view::free_unused_textures()
{
    const auto generation = m_tiles.generation();
    for(auto it = m_textures.begin(); it != m_textures.end();)
    {
        if(it->second.m_tile.expired() || it->first.m_generation != generation)
        {
            GLuint tex = it->second.m_texture;
            glDeleteTextures(1, &tex);
            it = m_textures.erase(it);
        }
        else
            ++it;
    }
}

unsigned waveform_view::tile_texture_for(
    const spectrogram_tiles::tile_ptr &tile,
    const app_settings &settings)
{
    auto &entry = m_textures[tile->m_key];
    const bool current = entry.m_texture &&
        entry.m_gain_db == settings.m_spectrogram_gain_db &&
        entry.m_range_db == settings.m_spectrogram_range_db;
    if(current)
        return entry.m_texture;

    // A few uploads a frame keep it smooth; the rest follow on later frames
    constexpr int max_uploads_per_frame = 6;
    if(m_uploads_this_frame >= max_uploads_per_frame)
        return entry.m_texture; // Stale colours for a frame or two, or 0 if new
    ++m_uploads_this_frame;

    if(m_map_gain_db != settings.m_spectrogram_gain_db ||
       m_map_range_db != settings.m_spectrogram_range_db)
    {
        m_map_gain_db = settings.m_spectrogram_gain_db;
        m_map_range_db = settings.m_spectrogram_range_db;
        m_colour_map.resize(256);
        for(size_t i = 0U; i != 256U; ++i)
            m_colour_map[i] = i == 0U
                ? IM_COL32(0, 0, 0, 255)
                : ImGui::ColorConvertFloat4ToU32(
                      ImPlot::SampleColormap(float(i) / 255.f, ImPlotColormap_Viridis));
    }

    const auto &image = tile->m_image;
    std::vector<uint32_t> pixels(image.m_db.size());
    const float gain = settings.m_spectrogram_gain_db, range = settings.m_spectrogram_range_db;
    for(size_t r = 0U; r != image.m_rows; ++r)
    {
        for(size_t c = 0U; c != image.m_columns; ++c)
        {
            const float t = std::clamp(1.f + (image.at(r, c) + gain) / range, 0.f, 1.f);
            // Row 0 of the texture is the top, the highest frequency
            pixels[(image.m_rows - 1U - r) * image.m_columns + c] =
                m_colour_map[static_cast<size_t>(t * 255.f)];
        }
    }

    if(!entry.m_texture)
    {
        GLuint tex{};
        glGenTextures(1, &tex);
        entry.m_texture = tex;
    }
    entry.m_tile = tile;
    entry.m_gain_db = gain;
    entry.m_range_db = range;
    glBindTexture(GL_TEXTURE_2D, entry.m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
    return entry.m_texture;
}

void waveform_view::render(const project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    // Zoom to the new file once it's loaded. ImPlot's fit only covers what was drawn, which is
    // just the visible part of the waveform, so the limits are set explicitly.
    const auto &audio = state.get_audio_buffer();
    if(state.m_active_file != m_last_active_file && audio.num_frames() != 0U)
    {
        m_last_active_file = state.m_active_file;
        if(!state.m_view_request)
        {
            const double duration = audio.duration_s();
            const double initial = state.m_settings.m_initial_view_s;
            const double start = state.active_offset_s();
            state.m_view_request =
                time_span{start, start + (initial > 0. ? std::min(initial, duration) : duration)};
        }
    }
    if(!state.m_view_request)
        follow_playhead(state);
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
        LMB = select / drag labels, Ctrl to add to the selection, Shift to place the playhead
        MMB = pan, double click zoom to fit
        wheel = zoom
        RMB drag = zoom selection
    */
    // Don't use left click to pan - we want it to select label
    ImPlot::GetInputMap().Pan = ImGuiMouseButton_Middle;
    ImPlot::GetInputMap().Fit = ImGuiMouseButton_Middle; // Double click middle to zoom to fit

    state.m_hover_time_s.reset();
    auto &settings = state.m_settings;
    if(!settings.m_show_spectrogram)
        render_plot(project, state, false, true);
    else
    {
        // Waveform above, spectrogram below, sharing the time axis. The split can be dragged.
        float ratios[2] = {1.f - settings.m_spectrogram_share, settings.m_spectrogram_share};
        if(ImPlot::BeginSubplots(
               "##Views",
               2,
               1,
               ImVec2{-1.f, -1.f},
               ImPlotSubplotFlags_LinkAllX | ImPlotSubplotFlags_NoMenus,
               ratios))
        {
            render_plot(project, state, false, false);
            render_plot(project, state, true, true);
            ImPlot::EndSubplots();
            settings.m_spectrogram_share =
                std::clamp(ratios[1] / (ratios[0] + ratios[1]), 0.1f, 0.9f);
        }
    }
    ImGui::End();
}

void waveform_view::follow_playhead(app_state &state)
{
    const auto &playback = state.m_playback_state;
    const double t = playback.m_playhead_s, last = m_last_playhead_s;
    const bool started = playback.m_playing && !m_was_playing;
    const bool advancing = playback.m_playing && t != last;
    m_last_playhead_s = t;
    m_was_playing = playback.m_playing;

    // Not while the user pans or zooms, or before the view is known
    const double width = m_transform.view_duration();
    if(!state.m_settings.m_follow_playhead ||
       !advancing ||
       state.m_playing_clip ||
       width <= 0. ||
       ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
       ImGui::IsMouseDown(ImGuiMouseButton_Right))
        return;

    // Page on when the playhead nears the right edge or loops out of view. Only if it was in view
    // or has just started, so that scrolling away while it plays isn't undone.
    const double start = m_transform.m_view_start_s, end = m_transform.m_view_end_s;
    const bool was_in_view = last >= start && last <= end;
    if((t >= start && t <= start + width * 0.95) || !(was_in_view || started))
        return;
    const double new_start = t - width * 0.05;
    state.m_view_request = time_span{new_start, new_start + width};
}

void waveform_view::setup_frequency_ticks(const app_state &state)
{
    const auto rate = state.get_audio_buffer().m_sample_rate_hz;
    if(rate == 0U)
        return;
    spectrogram_settings s;
    s.m_scale = state.m_settings.m_spectrogram_scale;
    const double max_hz = rate / 2.;
    const double min_hz = spectrogram_min_hz(s, max_hz);

    std::vector<double> positions;
    std::vector<std::string> names;
    for(const double hz :
        {0., 100., 200., 500., 1000., 2000., 5000., 10000., 20000., 50000., 100000.})
    {
        if(hz < min_hz || hz > max_hz || (hz == 0. && s.m_scale != frequency_scale::linear))
            continue;
        const double pos = frequency_position(hz, min_hz, max_hz, s.m_scale);
        // Skip ticks crowded together, which the linear scale makes at the bottom
        if(!positions.empty() && pos * 2. - 1. - positions.back() < 0.08)
            continue;
        positions.push_back(pos * 2. - 1.);
        names.push_back(hz >= 1000. ? fmt::format("{:g}k", hz / 1000.) : fmt::format("{:g}", hz));
    }
    std::vector<const char *> labels;
    for(const auto &n : names)
        labels.push_back(n.c_str());
    ImPlot::SetupAxisTicks(
        ImAxis_Y1,
        positions.data(),
        static_cast<int>(positions.size()),
        labels.data());
}

void waveform_view::render_plot(
    const project_model &project,
    app_state &state,
    bool spectrogram,
    bool bottom)
{
    if(!ImPlot::BeginPlot(
           spectrogram ? "##Spectrogram" : "##Waveform",
           ImVec2{-1.f, -1.f},
           ImPlotFlags_NoLegend))
        return;

    ImPlot::SetupAxes(
        bottom ? "Time (s)" : nullptr,
        spectrogram ? "Hz" : nullptr,
        ImPlotAxisFlags_NoGridLines | (bottom ? 0 : ImPlotAxisFlags_NoTickLabels),
        ImPlotAxisFlags_NoGridLines | (spectrogram ? 0 : ImPlotAxisFlags_NoTickLabels));
    if(spectrogram)
        setup_frequency_ticks(state);
    // The spectrogram's y axis reaches past the top frequency by the height of the flags, so they
    // don't cover it. The rows are the whole file's, so the scale stays put while panning.
    double y_top = 1.;
    if(spectrogram)
    {
        size_t rows = 0U;
        for(const auto &[id, row] : flag_rows(state.visible_labels(project), max_flag_rows))
            rows = std::max(rows, row + 1U);
        m_flag_rows = rows;
        const double strip_px = static_cast<double>(rows) * flag_row_px();
        const double data_px = m_spectrogram_px - strip_px; // Last frame's size
        if(data_px > strip_px)
            y_top = 1. + 2. * strip_px / data_px;
    }
    ImPlot::SetupAxisLimits(ImAxis_Y1, -1, y_top, ImPlotCond_Always);
    if(spectrogram)
    {
        m_spectrogram_px = ImPlot::GetPlotSize().y;
        spectrogram_settings s;
        s.m_scale = state.m_settings.m_spectrogram_scale;
        const double max_hz = std::max(1., state.get_audio_buffer().m_sample_rate_hz / 2.);
        m_axis.m_min_hz = spectrogram_min_hz(s, max_hz);
        m_axis.m_max_hz = max_hz;
        m_axis.m_scale = s.m_scale;
        m_axis.m_edge = 2. * edge_px / spectrogram_data_px();
    }

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
    if(spectrogram)
    {
        render_spectrogram(state);
        layout_flags(project, state);
        update_flag_drag(project, state);
    }
    update_waveform_drag(project, state, spectrogram);
    process_selections(state, spectrogram);
    if(!spectrogram)
        render_waveform(state);
    render_cursor(state);
    // Labels are edited on either plot, but only faintly filled on the spectrogram so it shows
    render_annotations(project, state, spectrogram);
    if(spectrogram)
        render_flags(project, state);
    ImPlot::PopPlotClipRect();
    ImPlot::EndPlot();
}

void waveform_view::render_cursor(app_state &state)
{
    auto *dl = ImPlot::GetPlotDrawList();

    if(state.get_audio_buffer().m_sample_rate_hz == 0)
        return;

    // Shift+click places the playhead and plays from there
    if(ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && shift_click())
    {
        const double start_time = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
        state.add_action(
            actions::select_playback_region{
                {start_time, std::numeric_limits<double>::max()},
                state.m_loop,
                {},
                true
            });
    }

    double t = state.m_playback_state.m_playhead_s;
    const ImVec2 p0 = ImPlot::PlotToPixels(t, -1.0);
    const ImVec2 p1 = ImPlot::PlotToPixels(t, 1.0);

    dl->AddLine(p0, p1, IM_COL32(127, 0, 0, 255), 4.f);
}

void waveform_view::render_waveform(app_state &state)
{
    const auto &audio_buf = state.get_audio_buffer();
    if(audio_buf.num_frames() == 0)
        return;

    const float gain = std::pow(10.f, state.m_settings.m_waveform_gain_db / 20.f);
    const auto plot_width = std::max<size_t>(1U, static_cast<size_t>(ImPlot::GetPlotSize().x));
    const double rate = audio_buf.m_sample_rate_hz;
    const auto frames = audio_buf.num_frames();
    // A clip is drawn at its place in its recording
    const double offset = state.active_offset_s();
    if(offset < 0.)
    {
        // Padding before the recording's start, in a clip cut near it
        auto *dl = ImPlot::GetPlotDrawList();
        const auto p0 = ImPlot::PlotToPixels(offset, 1.);
        const auto p1 = ImPlot::PlotToPixels(0., -1.);
        dl->AddRectFilled(p0, p1, IM_COL32(128, 128, 128, 40));
    }

    // Only the visible range, a little wider so the edges are drawn
    const double margin = m_transform.view_duration() * 0.01;
    const auto first = static_cast<size_t>(
        std::clamp((m_transform.m_view_start_s - offset - margin) * rate, 0., double(frames)));
    const auto last = static_cast<size_t>(
        std::clamp((m_transform.m_view_end_s - offset + margin) * rate, 0., double(frames)));
    if(last <= first)
        return;
    const size_t frames_per_px = std::max<size_t>(1U, (last - first) / plot_width);

    ImPlotSpec spec;

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
            double(first) / rate + offset,
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
        m_x.push_back(level->m_bin_times[b] + offset);
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
    m_uploads_this_frame = 0;
    const auto &settings = state.m_settings;
    spectrogram_settings s;
    s.m_scale = settings.m_spectrogram_scale;
    s.m_max_frames_per_column = 16U; // Off the UI thread, so it can afford to miss less
    m_tiles.set_audio(state.get_audio_buffer_ptr(), s);
    free_unused_textures();

    if(state.get_audio_buffer().num_frames() == 0)
        return;

    // Tiles are in the audio's own time; a clip is drawn at its place in its recording
    const double offset = state.active_offset_s();
    const double start = m_transform.m_view_start_s - offset;
    const double stop = m_transform.m_view_end_s - offset;
    const double px_s = m_transform.view_duration() / std::max(1.f, ImPlot::GetPlotSize().x);
    const int level = m_tiles.level_for(px_s);

    // Coarser tiles first, as a stand-in where the finer ones aren't ready yet
    auto tiles = m_tiles.fallback(start, stop, level);
    for(auto &t : m_tiles.request(start, stop, level))
        tiles.push_back(std::move(t));

    for(const auto &tile : tiles)
    {
        const auto texture = tile_texture_for(tile, settings);
        if(!texture)
            continue;
        ImPlot::PlotImage(
            "##Spectrogram",
            static_cast<ImTextureID>(texture),
            ImPlotPoint{tile->m_image.m_start_s + offset, -1.},
            ImPlotPoint{tile->m_image.m_stop_s + offset, 1.});
    }
}

void waveform_view::render_annotations(
    const project_model &project,
    app_state &state,
    bool spectrogram)
{
    const bool faint = spectrogram;
    if(!state.m_active_file)
        return;

    // On the spectrogram the height is frequency, so a label is drawn at its band, or over the
    // full height; on the waveform it's in its lane
    const auto bands = spectrogram ? std::map<label_id, lane>{} : state.label_bands(project);
    // Only the selected boxes on the spectrogram can be resized, and not from under a flag
    const bool flag_in_way = (m_hovered_flag || m_flag_press) && !m_rect_held;
    bool rect_held = false;
    auto *dl = ImPlot::GetPlotDrawList();
    const float plot_top = ImPlot::GetPlotPos().y;
    const float plot_bottom = plot_top + ImPlot::GetPlotSize().y;
    for(const auto *label : state.visible_labels(project))
    {
        const auto *defn = project.m_label_dict.find_ptr(label->m_defn_id);
        const ImColor col = label_colour(*label, defn, state);
        const auto name = display_name(*label, defn, state);
        const bool banded = spectrogram && !label->m_band.is_full();
        const auto band = bands.contains(label->m_id) ? bands.at(label->m_id) : lane{};
        const double y_min = banded ? m_axis.y_of(label->m_band.m_low_hz) : band.m_y_min;
        const double y_max = banded ? m_axis.y_of(label->m_band.m_high_hz) : band.m_y_max;

        // Faint lines up from the box to the flags, or on the waveform over the full height, so
        // that a box in a low lane or band is seen against what's above and below it
        const float top = spectrogram ? m_flags_bottom_px : plot_top;
        const ImVec2 box_top = ImPlot::PlotToPixels(label->m_start_s, y_max);
        const ImVec2 box_bottom = ImPlot::PlotToPixels(label->m_stop_s, y_min);
        const ImU32 marker = opaque(col, 0.35f);
        for(const float x : {box_top.x, box_bottom.x})
        {
            if(label->is_onset() && x != box_top.x)
                continue;
            if(box_top.y > top + 1.f)
                dl->AddLine({x, top}, {x, box_top.y}, marker);
            if(!spectrogram && box_bottom.y < plot_bottom - 1.f)
                dl->AddLine({x, box_bottom.y}, {x, plot_bottom}, marker);
        }

        const bool editable =
            !spectrogram || (state.is_label_selected(label->m_id) && !flag_in_way);
        if(render_resize_label_rect(
               state,
               *label,
               col,
               name,
               y_min,
               y_max,
               faint,
               banded,
               editable) &&
           spectrogram)
            rect_held = true;
        if(spectrogram && m_hovered_flag == label->m_id)
            dl->AddRect(box_top, box_bottom, opaque(colours::brighter(col), 1.f), 0.f, 0, 2.f);
        // A review list row's start, over the full height: whether it sits on the sound's start
        // is part of what's judged
        if(app_state::row_of(label->m_id))
        {
            const ImU32 line = opaque(colours::brighter(col), 0.9f);
            dl->AddLine({box_top.x, plot_top}, {box_top.x, plot_bottom}, line, 2.f);
            dl->AddRect(box_top, box_bottom, opaque(colours::brighter(col), 0.8f), 0.f, 0, 1.f);
        }
    }
    if(spectrogram)
        m_rect_held = rect_held;
}

float waveform_view::flag_row_px() { return ImGui::GetTextLineHeight() + 4.f; }

double waveform_view::spectrogram_data_px() const
{
    return std::max(1., double(ImPlot::PlotToPixels(0., -1.).y - ImPlot::PlotToPixels(0., 1.).y));
}

void waveform_view::layout_flags(const project_model &project, const app_state &state)
{
    m_flags.clear();
    const auto labels = state.visible_labels(project);
    const auto rows = flag_rows(labels, max_flag_rows);
    // In the strip above the top frequency, which is a row high per row of flags
    const float top = ImPlot::GetPlotPos().y;
    m_flags_bottom_px = ImPlot::PlotToPixels(0., 1.).y;
    const float row_px =
        m_flag_rows ? (m_flags_bottom_px - top) / static_cast<float>(m_flag_rows) : 0.f;
    for(const auto *l : labels)
    {
        const auto row = rows.find(l->m_id);
        if(row == rows.end())
            continue;
        const float y = top + static_cast<float>(row->second) * row_px;
        const float x0 = ImPlot::PlotToPixels(l->m_start_s, 1.).x;
        const float x1 = std::max(ImPlot::PlotToPixels(l->m_stop_s, 1.).x, x0 + float(min_box_px));
        m_flags.push_back(label_flag{l->m_id, {x0, y}, {x1, y + row_px - 1.f}});
    }

    m_hovered_flag.reset();
    if(ImPlot::IsPlotHovered())
        if(const auto *f = flag_at(ImGui::GetMousePos()))
            m_hovered_flag = f->m_id;
}

const waveform_view::label_flag *waveform_view::flag_at(ImVec2 pos) const
{
    // The last drawn is on top
    for(auto it = m_flags.rbegin(); it != m_flags.rend(); ++it)
        if(pos.x >= it->m_min.x &&
           pos.x <= it->m_max.x &&
           pos.y >= it->m_min.y &&
           pos.y <= it->m_max.y)
            return &*it;
    return nullptr;
}

void waveform_view::update_flag_drag(const project_model &project, app_state &state)
{
    const auto find = [&](label_id id) -> const label *
    {
        for(const auto *l : state.visible_labels(project))
            if(l->m_id == id)
                return l;
        return nullptr;
    };
    const double mouse_s = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;

    if(!m_flag_press &&
       m_hovered_flag &&
       !m_rect_held &&
       ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
       !shift_click())
    {
        if(const auto *l = find(*m_hovered_flag))
        {
            const bool selected = state.is_label_selected(l->m_id);
            const bool ctrl = ImGui::GetIO().KeyCtrl;
            m_flag_press = flag_press{l->m_id, mouse_s - l->m_start_s, selected, false, ctrl};
            // Ctrl toggles it at once. A selected one is only reselected on release, if not moved.
            if(ctrl || !selected)
                state.add_action(
                    actions::select_labels{mouse_s, 0., 0., std::nullopt, false, l->m_id, ctrl});
        }
        return;
    }
    if(!m_flag_press)
        return;

    const auto *l = find(m_flag_press->m_id);
    if(l &&
       !m_flag_press->m_toggled &&
       ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
       ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const double start = mouse_s - m_flag_press->m_grab_s;
        if(start != l->m_start_s)
        {
            m_flag_press->m_moved = true;
            m_drag_arm_id = l->m_id; // Played once let go, as when a box is moved
            state.add_action(
                actions::move_label{
                    l->m_id,
                    start,
                    start + (l->m_stop_s - l->m_start_s),
                    m_drag_seq,
                    std::nullopt
                });
        }
    }
    if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if(l && !m_flag_press->m_moved && !m_flag_press->m_toggled && m_flag_press->m_was_selected)
            state.add_action(actions::select_labels{mouse_s, 0., 0., std::nullopt, false, l->m_id});
        m_flag_press.reset();
    }
}

void waveform_view::render_flags(const project_model &project, const app_state &state)
{
    auto *dl = ImPlot::GetPlotDrawList();
    std::map<label_id, const label *> by_id;
    for(const auto *l : state.visible_labels(project))
        by_id[l->m_id] = l;
    // Flags are in rows, by start, so a name can run on past a short flag to the next in its row
    std::ranges::sort(
        m_flags,
        {},
        [](const label_flag &f) { return std::pair{f.m_min.y, f.m_min.x}; });
    const float plot_right = ImPlot::GetPlotPos().x + ImPlot::GetPlotSize().x;
    for(size_t i = 0U; i != m_flags.size(); ++i)
    {
        const auto &f = m_flags[i];
        const bool next_in_row = i + 1U != m_flags.size() && m_flags[i + 1U].m_min.y == f.m_min.y;
        const float text_right =
            std::max(f.m_max.x, next_in_row ? m_flags[i + 1U].m_min.x : plot_right);
        const auto *l = by_id.at(f.m_id);
        const auto *defn = project.m_label_dict.find_ptr(l->m_defn_id);
        ImColor col = label_colour(*l, defn, state);
        const bool selected = state.is_label_selected(l->m_id);
        const bool hovered = m_hovered_flag == l->m_id;
        dl->AddRectFilled(f.m_min, f.m_max, opaque(col, selected || hovered ? 0.95f : 0.75f));
        if(selected || hovered)
            dl->AddRect(f.m_min, f.m_max, IM_COL32(255, 255, 255, selected ? 220 : 120));
        const auto name = display_name(*l, defn, state);
        if(!name.empty())
        {
            dl->PushClipRect(f.m_min, {text_right, f.m_max.y}, true);
            dl->AddText({f.m_min.x + 3.f, f.m_min.y}, IM_COL32(255, 255, 255, 230), name.c_str());
            dl->PopClipRect();
        }
    }
}

void waveform_view::update_waveform_drag(
    const project_model &proj,
    app_state &state,
    bool spectrogram)
{
    if(!state.has_active_file() || !ImPlot::IsPlotHovered())
        return;

    auto &drag = state.m_drag;
    const auto mouse = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);
    const double start_time = mouse.x;
    const double mouse_y = std::clamp(mouse.y, -1., 1.);
    const tp t_now = std::chrono::steady_clock::now();

    // Start drag initial timer: Prevent drag on double clicks, single clicks to set the cursor etc
    if(!drag.is_dragging() &&
       ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
       !m_flag_press &&
       !shift_click())
    {
        m_drag_start = t_now;
        drag.begin_drag(start_time);
        // On the spectrogram, the drag's height is the label's frequency band
        m_drag_spectrogram = spectrogram;
        m_drag_y0 = m_drag_y1 = mouse_y;
    }

    // Update drag
    if(drag.is_dragging() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        drag.update_drag(start_time);
        if(spectrogram && m_drag_spectrogram)
        {
            m_drag_y1 = mouse_y;
            m_drag_plot_height = static_cast<float>(spectrogram_data_px());
        }
    }

    const label_defn *active_defn = state.m_active_label_defn
        ? proj.m_label_dict.find_ptr(state.m_active_label_defn.value())
        : nullptr;

    // End drag
    if(drag.is_dragging() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        auto drag_range = drag.get_drag_range();
        const auto extent = state.active_extent();
        drag_range.first = std::clamp(drag_range.first, extent.first, extent.second);
        drag_range.second = std::clamp(drag_range.second, extent.first, extent.second);

        // Only commit the drag selection if the drag duration and range exceeded some amount,
        // otherwise likely a cursor click:
        const bool commit = as_ms(t_now - m_drag_start) > ms(100) &&
            std::abs(drag_range.second - drag_range.first) > 0.02;
        drag.end_drag();
        auto band = drag_band();

        if(commit)
        {
            state.add_action(
                actions::add_label{
                    drag_range,
                    state.m_active_file.value(),
                    state.m_active_label_defn,
                    band
                });
            state.add_action(actions::select_playback_region{drag_range, state.m_loop, band});
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

        // The band only on the spectrogram it's drawn on
        double y0 = -1., y1 = 1.;
        if(spectrogram)
            if(const auto band = drag_band(); !band.is_full())
            {
                y0 = m_axis.y_of(band.m_low_hz);
                y1 = m_axis.y_of(band.m_high_hz);
            }
        const ImVec2 p0 = ImPlot::PlotToPixels(span.first, y0);
        const ImVec2 p1 = ImPlot::PlotToPixels(span.second, y1);

        dl->AddRectFilled(p0, p1, drag_colour);
    }
}

void waveform_view::process_selections(app_state &state, bool spectrogram)
{
    if(!state.has_active_file() || !ImPlot::IsPlotHovered())
        return;

    // Hit test:
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
       !m_flag_press &&
       !shift_click() &&
       !(spectrogram && m_rect_held))
    {
        const auto pos = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);
        const double px_s = m_transform.view_duration() / std::max(1.f, ImPlot::GetPlotSize().x);
        state.add_action(
            actions::select_labels{
                pos.x,
                pos.y,
                onset_hit_px * px_s,
                spectrogram ? std::optional<double>{m_axis.hz_of(pos.y)} : std::nullopt,
                spectrogram,
                std::nullopt,
                ImGui::GetIO().KeyCtrl
            });
    }
}

bool waveform_view::render_resize_label_rect(
    app_state &state,
    const label &l,
    ImColor col,
    const std::string &name,
    double y_min,
    double y_max,
    bool faint,
    bool banded,
    bool editable)
{
    // ImPlot edits the values in place, so it gets a copy and any change becomes an action. The
    // vertical extent is fixed by the label's lane, unless it's a band on the spectrogram. A box is
    // at least a few pixels wide, so that a short one can still be grabbed, and resizing doesn't
    // make it narrower than that.
    const double min_width =
        min_box_px * m_transform.view_duration() / std::max(1.f, ImPlot::GetPlotSize().x);
    const double length = l.m_stop_s - l.m_start_s;
    const double shown_stop = std::max(l.m_stop_s, l.m_start_s + min_width);
    double start{l.m_start_s}, stop{shown_stop};
    double top{y_max}, bottom{y_min};
    bool held = false;
    ImColor fill = col;
    if(faint)
        fill.Value.w *= 0.3f;
    const bool was_resized = ImPlot::DragRect(
        static_cast<int>(l.m_id),
        &start,
        &bottom,
        &stop,
        &top,
        fill,
        editable ? ImPlotDragToolFlags_None : ImPlotDragToolFlags_NoInputs,
        nullptr,
        nullptr,
        &held);
    if(faint)
    {
        // An outline, as the fill is faint
        const ImVec2 a = ImPlot::PlotToPixels(l.m_start_s, y_max);
        const ImVec2 b = ImPlot::PlotToPixels(shown_stop, y_min);
        ImPlot::GetPlotDrawList()->AddRect(
            a,
            b,
            ImColor(col.Value.x, col.Value.y, col.Value.z, 0.9f));
    }

    if(l.m_state == label_state::suggested)
    {
        // Dashed outline, so suggestions stand out from labels at a glance
        auto *dl = ImPlot::GetPlotDrawList();
        const ImVec2 a = ImPlot::PlotToPixels(l.m_start_s, y_max);
        const ImVec2 b = ImPlot::PlotToPixels(shown_stop, y_min);
        const ImU32 outline =
            colours::brighter(ImColor(col.Value.x, col.Value.y, col.Value.z, 1.f));
        for(float x = a.x; x < b.x; x += 8.f)
        {
            dl->AddLine({x, a.y}, {std::min(x + 4.f, b.x), a.y}, outline);
            dl->AddLine({x, b.y}, {std::min(x + 4.f, b.x), b.y}, outline);
        }
    }

    float name_x = 3.f;
    if(l.is_onset())
    {
        // A zero-length label (a point, from an old file or an import) is a narrow box with a flag,
        // to be widened into a region by dragging an edge or with E
        const ImVec2 top = ImPlot::PlotToPixels(l.m_start_s, y_max);
        const ImU32 flag = ImColor(col.Value.x, col.Value.y, col.Value.z, 0.9f);
        ImPlot::GetPlotDrawList()
            ->AddTriangleFilled(top, {top.x + 9.f, top.y + 5.f}, {top.x, top.y + 10.f}, flag);
        name_x = 11.f;
    }

    if(!name.empty())
    {
        ImVec2 corner = ImPlot::PlotToPixels(l.m_start_s, y_max);
        if(faint)
            corner.y = std::max(corner.y, m_flags_bottom_px); // Not under the flags
        ImPlot::GetPlotDrawList()->AddText(
            ImVec2{corner.x + name_x, corner.y + 1.f},
            IM_COL32(255, 255, 255, 200),
            name.c_str());
    }

    if(was_resized)
    {
        if(start > stop)
            std::swap(start, stop); // Dragged one edge past the other
        const double shown = shown_stop - l.m_start_s;
        if(length < min_width && std::abs((stop - start) - shown) < 1e-9)
            stop = start + length; // Moved, not resized: keep its length
        else
            stop = std::max(stop, start + min_width);
        std::optional<frequency_band> band;
        if(banded && (top != y_max || bottom != y_min))
            band = m_axis.band(bottom, top);
        m_drag_arm_id = l.m_id;
        state.add_action(actions::move_label{l.m_id, start, stop, m_drag_seq, band});
    }

    if(l.m_id == m_drag_arm_id && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        m_drag_arm_id.reset();
        const auto window = state.playback_window(l);
        state.select_playback_region(window.first, window.second, l.m_band);
    }
    return held;
}
