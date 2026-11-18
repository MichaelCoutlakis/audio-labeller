/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "../model/label.h"

/// A horizontal band of the waveform plot, whose y axis runs from -1 to 1
struct lane
{
    double m_y_min{-1.};
    double m_y_max{1.};

    bool contains(double y) const { return m_y_min <= y && y <= m_y_max; }
    bool operator==(const lane &) const = default;
};

/// How labels share the height of the waveform
enum class label_layout
{
    stacked,     ///< Full height, shared only where labels overlap in time, like calendar events
    class_lanes, ///< A fixed band per palette class
    full_height  ///< Every label over the full height
};

/// With class lanes, each palette class gets its own band in palette order so that overlapping
/// sounds of different classes can be seen and clicked separately. Labels without a palette class
/// (unclassified selections) span the full height.
inline lane lane_of(const label_dict &dict, label_defn_id id, bool class_lanes)
{
    if(!class_lanes || dict.size() == 0)
        return {};

    size_t index = 0U;
    for(const auto &defn : dict)
    {
        if(defn.m_id == id)
        {
            const double height = 2. / static_cast<double>(dict.size());
            const double top = 1. - static_cast<double>(index) * height;
            return {top - height, top};
        }
        ++index;
    }
    return {};
}

/// A region's column in its stack: which one, and how many its group of overlapping regions needs
struct stack_column
{
    size_t m_column{};
    size_t m_columns{1U};
    bool operator==(const stack_column &) const = default;
};

/// Regions that overlap in time form a group, which is split into as many columns as it needs at
/// its most crowded, each region taking the first column free at its start. Zero-length labels
/// take no column.
inline std::map<label_id, stack_column> stack_columns(const std::vector<const label *> &labels)
{
    std::vector<const label *> regions;
    for(const auto *l : labels)
        if(!l->is_onset())
            regions.push_back(l);
    std::ranges::sort(
        regions,
        {},
        [](const label *l) { return std::tuple{l->m_start_s, -l->m_stop_s, l->m_id}; });

    std::map<label_id, stack_column> out;
    size_t group_start = 0U;
    while(group_start < regions.size())
    {
        // A group runs until a region starts after everything before it has stopped
        std::vector<double> column_stops;
        std::vector<size_t> columns;
        size_t end = group_start;
        double group_stop = regions[group_start]->m_stop_s;
        for(; end < regions.size() && (end == group_start || regions[end]->m_start_s < group_stop);
            ++end)
        {
            const auto *l = regions[end];
            group_stop = std::max(group_stop, l->m_stop_s);
            auto free =
                std::ranges::find_if(column_stops, [&](double s) { return s <= l->m_start_s; });
            if(free == column_stops.end())
            {
                column_stops.push_back(l->m_stop_s);
                columns.push_back(column_stops.size() - 1U);
            }
            else
            {
                *free = l->m_stop_s;
                columns.push_back(static_cast<size_t>(free - column_stops.begin()));
            }
        }
        for(size_t i = group_start; i != end; ++i)
            out[regions[i]->m_id] = stack_column{columns[i - group_start], column_stops.size()};
        group_start = end;
    }
    return out;
}

/// Stacking: each region gets the band of its column, like calendar events. Regions that overlap
/// nothing get the full height, so tens of classes don't make every label thin. Zero-length labels
/// are drawn as narrow boxes over the full height.
inline std::map<label_id, lane> stack_labels(const std::vector<const label *> &labels)
{
    std::map<label_id, lane> out;
    for(const auto *l : labels)
        out[l->m_id] = lane{};
    for(const auto &[id, c] : stack_columns(labels))
    {
        const double height = 2. / static_cast<double>(c.m_columns);
        const double top = 1. - static_cast<double>(c.m_column) * height;
        out[id] = lane{top - height, top};
    }
    return out;
}

/// The rows of flags along the top of the spectrogram, one flag per region, where regions that
/// overlap in time go in different rows. Rows past the last are put in it.
inline std::map<label_id, size_t> flag_rows(
    const std::vector<const label *> &labels,
    size_t max_rows)
{
    std::map<label_id, size_t> out;
    for(const auto &[id, c] : stack_columns(labels))
        out[id] = std::min(c.m_column, std::max<size_t>(max_rows, 1U) - 1U);
    return out;
}

/// The labels under a click on the spectrogram, most specific first: the smallest in time by
/// frequency, as a share of the band up to @p max_hz, so a band inside a region comes before it
inline void order_by_area(std::vector<const label *> &hits, double max_hz)
{
    const auto area = [&](const label *l)
    {
        const double low = std::clamp(l->m_band.m_low_hz, 0., max_hz);
        const double high = std::clamp(l->m_band.m_high_hz, 0., max_hz);
        const double share = max_hz > 0. ? (high - low) / max_hz : 1.;
        return (l->m_stop_s - l->m_start_s) * share;
    };
    std::ranges::stable_sort(hits, {}, area);
}

/// A click where labels are stacked selects one at a time: the first if none are selected, else
/// the one after the first selected (unselecting it), and after the last, none.
/// @return the label to unselect and the label to select, either of which may be absent
template <typename Selected>
std::pair<std::optional<label_id>, std::optional<label_id>> cycle_pick(
    const std::vector<const label *> &ordered,
    Selected &&is_selected)
{
    for(size_t i = 0U; i != ordered.size(); ++i)
    {
        if(!is_selected(ordered[i]->m_id))
            continue;
        std::optional<label_id> next;
        if(i + 1U < ordered.size())
            next = ordered[i + 1U]->m_id;
        return {ordered[i]->m_id, next};
    }
    if(ordered.empty())
        return {};
    return {std::nullopt, ordered.front()->m_id};
}

/// The band of each label, by the layout
inline std::map<label_id, lane> layout_labels(
    const std::vector<const label *> &labels,
    const label_dict &dict,
    label_layout layout)
{
    if(layout == label_layout::stacked)
        return stack_labels(labels);

    std::map<label_id, lane> out;
    for(const auto *l : labels)
        out[l->m_id] = l->is_onset()
            ? lane{}
            : lane_of(dict, l->m_defn_id, layout == label_layout::class_lanes);
    return out;
}
