/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include <nlohmann/json.hpp>

#include "../model/label.h"
#include "../model/project_model.h"
#include "../model/types.h"

/// The project's "review" section: how labels are played and shown while checking them
struct review_settings
{
    double m_onset_pre_s{0.1};  ///< Played before an onset
    double m_onset_post_s{0.5}; ///< Played after an onset
    double m_pre_roll_s{0.};    ///< Played before a region
    double m_view_pre_s{0.1};   ///< Shown before a suggestion in review mode
    double m_view_post_s{0.65}; ///< Shown after its start in review mode
    double m_snap_window_s{0.005}; ///< How far an onset may move when snapped
    bool m_by_score{true}; ///< Review the lowest scores first, if the suggestions have scores

    /// Missing keys keep their defaults
    static review_settings from_json(const nlohmann::json &j);
};

/// What to play for a label: an onset with its context either side, a region with any pre-roll
time_span playback_window(const label &l, const review_settings &settings, double duration_s);

/// What to show of a suggestion in review mode, like the training window around an event
time_span review_view(const label &l, const review_settings &settings);

struct review_item
{
    std::filesystem::path m_file;
    label_id m_id{};
    double m_start_s{};
    std::optional<float> m_score;
    size_t m_file_index{}; ///< In the project's file list
};

/// Every suggestion waiting for review, in the files that aren't skipped. In file order, or by
/// score (lowest first, then those without one) if any suggestion has one and settings ask.
std::vector<review_item> review_queue(const project_model &project, const review_settings &settings);

/// The item after (or before) the given position in the queue, wrapping around. The position need
/// not be in the queue any more, eg. once it's been accepted.
std::optional<review_item> step_review(
    const std::vector<review_item> &queue,
    const std::optional<review_item> &from,
    bool forward,
    const review_settings &settings);
