/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "../app/app_state.h"
#include "../model/project_model.h"

class waveform_view
{
public:
    void render(const project_model &project, app_state &state);

    /// Free the GPU texture. Must be called while the GL context exists.
    void release();

    std::string m_window_name{"Waveform View"};

private:
    void render_cursor(app_state &state);
    void render_waveform(app_state &state);
    void render_spectrogram(app_state &state);
    void render_annotations(const project_model &project, app_state &state);
    void update_waveform_drag(const project_model &proj, app_state &state);

    void process_selections(app_state &state);

    void render_resize_label_rect(
        app_state &state,
        const label &l,
        ImColor col,
        const std::string &name,
        double y_min,
        double y_max);
    void render_onset(app_state &state, const label &l, ImColor col, const std::string &name);

    coord_transform m_transform;

    using tp = std::chrono::steady_clock::time_point;
    using ms = std::chrono::milliseconds;
    tp m_drag_start;
    std::optional<label_id> m_drag_arm_id;
    uint64_t m_drag_seq{}; ///< New for each mouse press, so each drag is one undo step
    std::optional<std::filesystem::path> m_last_active_file;
    const audio_buffer *m_last_audio{nullptr};

    // Scratch for drawing the visible part of the waveform at the chosen gain
    std::vector<float> m_x, m_lo, m_hi;

    /* Spectrogram: recomputed when the view settles on a new range */
    struct spectrogram_key
    {
        const audio_buffer *m_audio{};
        double m_start_s{}, m_stop_s{};
        int m_width{}, m_height{};
        frequency_scale m_scale{};
        float m_gain_db{}, m_range_db{};
        bool operator==(const spectrogram_key &) const = default;
    };
    std::optional<spectrogram_key> m_spectrogram_key;
    tp m_spectrogram_time{};
    unsigned m_texture{}; ///< OpenGL texture name, 0 if none
};
