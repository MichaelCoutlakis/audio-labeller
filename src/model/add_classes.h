/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "label.h"
#include "types.h"

/// How near two labels' edges must be for them to cover the same span
inline constexpr double same_span_tolerance_s = 1e-3;

inline bool same_span(const label &l, time_span t, double tolerance_s = same_span_tolerance_s)
{
    return std::abs(l.m_start_s - t.first) <= tolerance_s &&
        std::abs(l.m_stop_s - t.second) <= tolerance_s;
}

/// A label of the class over the span, ignoring rejected ones
inline std::optional<label_id> find_over(
    const labels &existing,
    label_defn_id defn_id,
    time_span t,
    double tolerance_s = same_span_tolerance_s)
{
    for(const auto &l : existing)
        if(l.m_defn_id == defn_id && l.m_state != label_state::rejected &&
           same_span(l, t, tolerance_s))
            return l.m_id;
    return std::nullopt;
}

/// New confirmed labels of a class over each of the given labels' spans (and bands). A span the
/// class already has a label over, or that an earlier one of them repeats, is skipped.
inline std::vector<label> labels_to_add(
    const labels &existing,
    const std::vector<label> &over,
    label_defn_id defn_id)
{
    std::vector<label> added;
    for(const auto &o : over)
    {
        const time_span t{o.m_start_s, o.m_stop_s};
        const bool repeat = std::ranges::any_of(added, [&](const label &a) { return same_span(a, t); });
        if(repeat || find_over(existing, defn_id, t))
            continue;
        label l{get_next_label_id(), defn_id, o.m_start_s, o.m_stop_s};
        l.m_band = o.m_band;
        added.push_back(l);
    }
    return added;
}

/// What adding a class over a selection does
struct add_class_plan
{
    /// Unclassified selections given the class, keeping their ids
    std::vector<label> m_classified;
    /// Unclassified selections dropped because the class already has a label over their span:
    /// (dropped, the label there), which takes its place in the selection
    std::vector<std::pair<label_id, label_id>> m_merged;
    /// New labels over the spans of the selected labels
    std::vector<label> m_added;
    size_t m_skipped{}; ///< Selected labels whose span the class already has

    bool empty() const { return m_classified.empty() && m_merged.empty() && m_added.empty(); }
};

/// Adding a class over a selection: unclassified selections (which have no class to keep) are
/// given it, and the selected labels get a new label of it over each span
inline add_class_plan plan_add_class(
    const labels &existing,
    const std::vector<label> &unclassified,
    const std::vector<label> &selected,
    label_defn_id defn_id)
{
    add_class_plan plan;
    labels with = existing; // Including those classified, so nothing is added twice
    for(const auto &u : unclassified)
    {
        if(const auto there = find_over(with, defn_id, {u.m_start_s, u.m_stop_s}))
        {
            plan.m_merged.emplace_back(u.m_id, *there);
            continue;
        }
        auto l = u;
        l.m_defn_id = defn_id;
        l.m_state = label_state::confirmed;
        plan.m_classified.push_back(l);
        with.add(l);
    }
    plan.m_added = labels_to_add(with, selected, defn_id);
    plan.m_skipped = selected.size() - plan.m_added.size();
    return plan;
}
