/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <algorithm>
#include <map>
#include <tuple>
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

/// Stacking: labels that overlap in time form a group, which is split into as many columns as it
/// needs at its most crowded, each label taking the first column free at its start. Labels that
/// overlap nothing get the full height, so tens of classes don't make every label thin.
/// Onsets have no length, so they're drawn as lines over the full height and take no column.
inline std::map<label_id, lane> stack_labels(std::vector<const label *> labels)
{
    std::map<label_id, lane> out;
    std::vector<const label *> regions;
    for(const auto *l : labels)
    {
        if(l->is_onset())
            out[l->m_id] = lane{};
        else
            regions.push_back(l);
    }
    std::ranges::sort(
        regions,
        {},
        [](const label *l) { return std::tuple{l->m_start_s, -l->m_stop_s, l->m_id}; });

    size_t group_start = 0U;
    while(group_start < regions.size())
    {
        // A group runs until a label starts after everything before it has stopped
        std::vector<double> column_stops;
        std::vector<size_t> columns;
        size_t end = group_start;
        double group_stop = regions[group_start]->m_stop_s;
        for(; end < regions.size() && (end == group_start || regions[end]->m_start_s < group_stop);
            ++end)
        {
            const auto *l = regions[end];
            group_stop = std::max(group_stop, l->m_stop_s);
            auto free = std::ranges::find_if(column_stops, [&](double s) { return s <= l->m_start_s; });
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
        const double height = 2. / static_cast<double>(column_stops.size());
        for(size_t i = group_start; i != end; ++i)
        {
            const double top = 1. - static_cast<double>(columns[i - group_start]) * height;
            out[regions[i]->m_id] = lane{top - height, top};
        }
        group_start = end;
    }
    return out;
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
