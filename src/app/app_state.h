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

private:
    std::set<label_id> m_selected_labels;

    audio_buffer m_active_audio;
};
