/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

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
    bool m_onsets{false};           ///< Suggest onsets (snapped to the rise) rather than regions

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
