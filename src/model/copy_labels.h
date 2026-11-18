/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "add_classes.h"
#include "label.h"

/// What pasting labels does
struct pasted_labels
{
    std::vector<label> m_added;
    size_t m_outside{}; ///< Fell outside the recording once shifted
    size_t m_present{}; ///< Already there, in any state
};

/// Copied labels as new ones in the given state (suggestions, for labels from another recording
/// of the same scene, to check that this one heard each sound), with offset_s added to their
/// times: negative if this recording started later. Those starting outside the recording once
/// shifted are left out, and stops past its end cut short. A label of the same class over the
/// same span, in any state, isn't added again.
inline pasted_labels labels_to_paste(
    const std::vector<label> &copied,
    const labels &existing,
    double offset_s,
    double duration_s,
    label_state state)
{
    pasted_labels out;
    for(const auto &c : copied)
    {
        const double start = c.m_start_s + offset_s;
        if(start < 0. || start >= duration_s)
        {
            ++out.m_outside;
            continue;
        }
        const double stop = std::min(c.m_stop_s + offset_s, duration_s);
        const time_span t{start, stop};
        const auto same = [&](const label &l)
        { return l.m_defn_id == c.m_defn_id && same_span(l, t); };
        if(std::ranges::any_of(existing, same) || std::ranges::any_of(out.m_added, same))
        {
            ++out.m_present;
            continue;
        }
        label l{get_next_label_id(), c.m_defn_id, start, stop, state};
        l.m_band = c.m_band;
        out.m_added.push_back(l);
    }
    return out;
}
