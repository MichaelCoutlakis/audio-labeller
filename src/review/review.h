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
    double m_pre_roll_s{0.2}; ///< Played before a label
    /// Played after the start of a zero-length label, which has no stop to play to
    double m_point_post_s{0.5};
    double m_view_pre_s{0.5}; ///< Shown before a suggestion, and after its stop, in review mode
    double m_view_post_s{1.}; ///< Shown at least this long after its start in review mode
    double m_snap_window_s{0.005}; ///< How far an onset may move when snapped
    /// Whether snapping (A) also moves the starts of regions, not only zero-length labels. Off by
    /// default, since a region's start needn't be its sharpest onset.
    bool m_snap_regions{false};
    bool m_by_score{true}; ///< Review the lowest scores first, if the suggestions have scores

    /// Missing keys keep their defaults
    static review_settings from_json(const nlohmann::json &j);
    /// Only the keys that differ from the defaults, so that a project file stays short
    nlohmann::json to_json() const;
};

/// A label's times once snapped to the onset found near its start: a zero-length label moves
/// there, and a region's start does only if the settings ask. nullopt if it isn't to change.
std::optional<time_span> snapped(const label &l, double onset_s, const review_settings &settings);

/// What to play for a label: from the pre-roll before its start to its stop, or for a zero-length
/// label, to point_post_s after it
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
