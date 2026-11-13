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
#include "../dsp/spectrogram_tiles.h"
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
    /// One plot of the stack: the waveform, or the spectrogram. Labels can be edited on either.
    void render_plot(const project_model &project, app_state &state, bool spectrogram, bool bottom);
    void setup_frequency_ticks(const app_state &state);
    void render_annotations(const project_model &project, app_state &state, bool faint);
    void update_waveform_drag(const project_model &proj, app_state &state);

    void process_selections(app_state &state);

    void render_resize_label_rect(
        app_state &state,
        const label &l,
        ImColor col,
        const std::string &name,
        double y_min,
        double y_max,
        bool faint);
    void render_onset(app_state &state, const label &l, ImColor col, const std::string &name);

    coord_transform m_transform;

    using tp = std::chrono::steady_clock::time_point;
    using ms = std::chrono::milliseconds;
    tp m_drag_start;
    std::optional<label_id> m_drag_arm_id;
    uint64_t m_drag_seq{}; ///< New for each mouse press, so each drag is one undo step
    std::optional<std::filesystem::path> m_last_active_file;

    // Scratch for drawing the visible part of the waveform at the chosen gain
    std::vector<float> m_x, m_lo, m_hi;

    /* Spectrogram: tiles computed in the background, each drawn from its own texture */
    struct tile_texture
    {
        std::weak_ptr<const spectrogram_tile> m_tile; ///< Freed with it once evicted
        unsigned m_texture{};                         ///< OpenGL texture name
        float m_gain_db{}, m_range_db{};              ///< The colours it was made with
    };
    /// The texture for a tile, made or recoloured if needed (up to a few per frame), else 0
    unsigned tile_texture_for(const spectrogram_tiles::tile_ptr &tile, const app_settings &settings);
    void free_unused_textures();

    spectrogram_tiles m_tiles;
    std::map<spectrogram_tile_key, tile_texture> m_textures;
    std::vector<uint32_t> m_colour_map; ///< 256 colours, for the gain and range below
    float m_map_gain_db{}, m_map_range_db{-1.f};
    int m_uploads_this_frame{};
};
