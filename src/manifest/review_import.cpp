/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>
#include <optional>

#include "review_import.h"

namespace
{
constexpr double same_time_s = 1e-3; ///< Times this close are the same

/// The label of the class whose span holds the time, give or take the window: the one starting
/// nearest it
label *label_of_class_at(labels &ls, double t_s, double window_s, label_defn_id cls)
{
    label *best = nullptr;
    for(auto &l : ls)
    {
        if(l.m_defn_id != cls ||
           t_s < l.m_start_s - window_s ||
           t_s > std::max(l.m_stop_s, l.m_start_s) + window_s)
            continue;
        if(!best || std::abs(l.m_start_s - t_s) < std::abs(best->m_start_s - t_s))
            best = &l;
    }
    return best;
}

bool same_times(const label &l, time_span t)
{
    return std::abs(l.m_start_s - t.first) <= same_time_s &&
        std::abs(l.m_stop_s - t.second) <= same_time_s;
}
}

size_t review_import_plan::unapplied() const
{
    size_t n = 0U;
    for(const auto &[why, count] : m_unapplied)
        n += count;
    return n;
}

review_import_plan plan_review_import(
    const project_model &project,
    const clip_manifest &results,
    double window_s)
{
    review_import_plan plan;
    const auto &dict = project.m_label_dict;
    const auto unapplied = [&](const char *why) { ++plan.m_unapplied[why]; };
    const auto known = [&](const std::string &name) -> std::optional<label_defn_id>
    {
        auto id = class_named(dict, name);
        if(!id && !name.empty())
            plan.m_unknown_classes.insert(name);
        return id;
    };

    for(const auto &row : results.m_rows)
    {
        const auto &v = row.m_result;
        if(v.m_verdict.empty())
            continue; // Not heard
        if(!row.m_source)
        {
            unapplied("its recording isn't in the project");
            continue;
        }
        const auto &file = *row.m_source;
        auto it = plan.m_files.find(file);
        if(it == plan.m_files.end())
            it = plan.m_files.emplace(file, project.get_labels(file)).first;
        auto &ls = it->second;
        const bool has_stop = row.m_stop_s || v.m_new_stop_s;
        const auto span = row.reviewed_span(0.);

        // Change a label, counting what happened to it
        const auto change = [&](label &l, label_defn_id cls, label_state state)
        {
            const auto before = l;
            l.m_defn_id = cls;
            l.m_state = state;
            if(v.m_new_start_s)
                l.m_start_s = *v.m_new_start_s;
            if(v.m_new_stop_s)
                l.m_stop_s = std::max(l.m_start_s, *v.m_new_stop_s);
            if(l == before)
                ++plan.m_unchanged;
            else if(state == label_state::rejected)
                ++plan.m_rejected;
            else
                ++plan.m_changed;
        };
        const auto make = [&](label_defn_id cls)
        {
            if(std::ranges::any_of(
                   ls,
                   [&](const label &l) { return l.m_defn_id == cls && same_times(l, span); }))
            {
                ++plan.m_unchanged;
                return;
            }
            ls.add(label{get_next_label_id(), cls, span.first, span.second});
            ++plan.m_added;
        };

        if(v.m_verdict == "added")
        {
            if(const auto cls = known(row.reviewed_label()))
                make(*cls);
            else
                unapplied("its class isn't in the palette");
            continue;
        }
        if(row.m_label.empty())
        {
            unapplied("it has no class");
            continue;
        }
        const auto cls = known(row.m_label);
        auto *existing = cls ? label_of_class_at(ls, row.m_start_s, window_s, *cls) : nullptr;

        if(v.m_verdict == "not_event")
        {
            if(existing)
                change(*existing, existing->m_defn_id, label_state::rejected);
            else
                ++plan.m_unchanged; // Nothing there to reject
            continue;
        }

        const auto target = v.m_verdict == "relabel" ? known(row.reviewed_label()) : cls;
        if(!target)
        {
            unapplied("its class isn't in the palette");
            continue;
        }
        // Relabelled by an earlier import: the label is of the new class now
        if(!existing && v.m_verdict == "relabel")
            existing = label_of_class_at(ls, row.m_start_s, window_s, *target);
        if(existing)
            change(*existing, *target, label_state::confirmed);
        else if(has_stop)
            make(*target);
        else
            unapplied("no label of its class there, and no stop to make one");
    }

    // Only the recordings whose labels change
    std::erase_if(
        plan.m_files,
        [&](const auto &entry) { return entry.second == project.get_labels(entry.first); });
    return plan;
}
