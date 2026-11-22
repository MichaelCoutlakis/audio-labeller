/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <optional>
#include <vector>

#include <nlohmann/json.hpp>

#include "../model/audio_buffer.h"

/// Finds sounds that stand out from the background: frames whose level is some margin above the
/// noise floor (a low percentile of all frame levels), joined across short gaps
struct loudness_settings
{
    double m_frame_s{0.01};         ///< Level is measured over frames of this length
    double m_margin_db{12.};        ///< How far above the noise floor counts as a sound
    double m_min_level_db{-70.};    ///< Quieter than this (dBFS) never counts, eg. in silence
    double m_floor_percentile{0.2}; ///< The noise floor is this quantile of frame levels
    double m_min_length_s{0.03};    ///< Shorter sounds are dropped
    double m_merge_gap_s{0.1};      ///< Sounds closer than this are joined
    double m_padding_s{0.01};       ///< Added either side of each region
    /// Start each region at its sharpest rise rather than padding before it
    bool m_snap_starts{false};

    /// Missing keys keep their defaults
    static loudness_settings from_json(const nlohmann::json &j);
    nlohmann::json to_json() const;
};

struct loud_region
{
    double m_start_s{};
    double m_stop_s{};
    double m_peak_db{};      ///< Loudest frame, dBFS
    double m_above_db{};     ///< How far the peak is above the threshold
};

/// The loud regions of the first channel, in time order
std::vector<loud_region> find_loud_regions(const audio_buffer &audio, const loudness_settings &s);

/// How the end of a sound is found from where it starts, see estimate_sound_end
struct sound_end_settings
{
    double m_frame_s{0.01};       ///< Level is measured over frames of this length
    double m_peak_search_s{0.25}; ///< The sound's peak is looked for this soon after its start
    double m_context_s{5.};       ///< The noise floor is measured from this long before the start
    double m_floor_percentile{0.2};
    double m_margin_db{6.};     ///< The sound has ended once it's this close to the noise floor...
    double m_hold_s{0.05};      ///< ...for at least this long, so short dips don't end it
    double m_max_length_s{10.}; ///< A sound that doesn't fall back ends here
    double m_min_rise_db{3.};   ///< A peak less than this above the floor isn't a sound
};

/// Where a sound starting at start_s ends: once its level (of the first channel) has fallen back
/// to near the noise floor, and stayed there for the hold time. The threshold is the floor plus the
/// margin, or halfway from the floor to the sound's peak if that's lower, so quiet sounds end too.
/// nullopt if nothing rises above the floor after start_s.
std::optional<double> estimate_sound_end(
    const audio_buffer &audio,
    double start_s,
    const sound_end_settings &s = {});
