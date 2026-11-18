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
#include "../dsp/spectrogram.h"
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
    /// Scroll the view on when playback reaches its edge
    void follow_playhead(app_state &state);
    void render_waveform(app_state &state);
    void render_spectrogram(app_state &state);
    /// One plot of the stack: the waveform, or the spectrogram. Labels can be edited on either.
    void render_plot(const project_model &project, app_state &state, bool spectrogram, bool bottom);
    void setup_frequency_ticks(const app_state &state);
    void render_annotations(const project_model &project, app_state &state, bool spectrogram);
    void update_waveform_drag(const project_model &proj, app_state &state, bool spectrogram);

    void process_selections(app_state &state, bool spectrogram);

    void render_resize_label_rect(
        app_state &state,
        const label &l,
        ImColor col,
        const std::string &name,
        double y_min,
        double y_max,
        bool faint,
        bool banded);

    coord_transform m_transform;

    /// The spectrogram's frequency axis, which runs from -1 at the bottom to 1 at the top
    struct frequency_axis
    {
        double m_min_hz{};
        double m_max_hz{1.};
        frequency_scale m_scale{frequency_scale::linear};
        double m_edge{}; ///< Within this of the top or bottom counts as the edge

        double y_of(double hz) const;
        double hz_of(double y) const;
        /// The band between two heights, reaching 0 Hz or Nyquist at the edges
        frequency_band band(double y0, double y1) const;
    };
    frequency_axis m_axis; ///< Of the spectrogram, set as it's drawn

    /// A new label's band, drawn by dragging on the spectrogram
    bool m_drag_spectrogram{false};
    double m_drag_y0{}, m_drag_y1{};
    float m_drag_plot_height{1.f};
    /// The band of the drag so far, full if it isn't tall enough to be meant as one
    frequency_band drag_band() const;

    using tp = std::chrono::steady_clock::time_point;
    using ms = std::chrono::milliseconds;
    tp m_drag_start;
    std::optional<label_id> m_drag_arm_id;
    uint64_t m_drag_seq{}; ///< New for each mouse press, so each drag is one undo step
    std::optional<std::filesystem::path> m_last_active_file;
    double m_last_playhead_s{};
    bool m_was_playing{false};

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
