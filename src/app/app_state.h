/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include "../model/project_model.h"

audio_buffer load_audio(const std::filesystem::path &path);

class coord_transform
{
public:
    double m_view_start_s{};
    double m_view_end_s{};

    double time_to_px(double t_s, float px_min, float px_max) const
    {
        double norm = (t_s - m_view_start_s) / (m_view_end_s - m_view_start_s);
        return px_min + norm * (px_max - px_min);
    }

    double px_to_time(float px, float px_min, float px_max) const
    {
        double norm = (px - px_min) / (px_max - px_min);
        return m_view_start_s + norm * (m_view_end_s - m_view_start_s);
    }

    double view_duration() const { return m_view_end_s - m_view_start_s; }
};

/// Represent the audio buffer at multiple zoom levels
struct audio_min_max_level
{
    size_t m_samples_per_bin{};
    std::vector<float> m_min_vals;
    std::vector<float> m_max_vals;
    std::vector<float> m_bin_times;
};

/// Container for audio buffer representations at the different zoom levels
class audio_min_max_pyramid_map
{
public:
    audio_min_max_pyramid_map() = default;
    audio_min_max_pyramid_map(const audio_buffer &buffer);

    const audio_min_max_level *get_level(size_t samples_per_px) const;

private:
    std::vector<audio_min_max_level> m_levels;
};

struct waveform_interaction
{
    void begin_drag(double time_s)
    {
        m_dragging = true;
        m_drag_start = time_s;
        m_pending_selection.reset();
    }
    void update_drag(double time_s) { m_drag_current = time_s; }
    void end_drag(bool commit)
    {
        m_dragging = false;
        if(commit)
        m_pending_selection = get_drag_range();
    }

    bool is_dragging() const { return m_dragging; }

    time_span get_drag_range() const
    {
        return {std::min(m_drag_start, m_drag_current), std::max(m_drag_start, m_drag_current)};
    }
    std::optional<time_span> pending_selection() const { return m_pending_selection; }

private:
    bool m_dragging{false};
    double m_drag_start{};
    double m_drag_current{};
    std::optional<time_span> m_pending_selection;
};

class app_state
{
public:
    bool is_label_selected(label_id id) { return m_selected_labels.count(id); }

    void toggle_label_selection(label_id id)
    {
        if(!m_selected_labels.erase(id))
            m_selected_labels.insert(id);
    }

    std::optional<std::filesystem::path> m_active_file;

    bool is_selected_file(const std::filesystem::path &path);

    void set_selected_file(const std::filesystem::path &path);

    const audio_buffer &get_audio_buffer() { return m_active_audio; }

    const audio_min_max_level *get_audio_min_max_level(size_t samples_per_px);

    waveform_interaction m_drag;

    size_t m_cursor_sample{0U};

private:
    std::set<label_id> m_selected_labels;

    audio_buffer m_active_audio;

    audio_min_max_pyramid_map m_audio_min_max_map;
};
