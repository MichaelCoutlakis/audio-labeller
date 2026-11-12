/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include "../model/label.h"

/// A horizontal band of the waveform plot, whose y axis runs from -1 to 1
struct lane
{
    double m_y_min{-1.};
    double m_y_max{1.};

    bool contains(double y) const { return m_y_min <= y && y <= m_y_max; }
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
