/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <limits>
#include <tuple>

#include "review.h"

namespace
{
bool uses_scores(const std::vector<review_item> &queue, const review_settings &settings)
{
    return settings.m_by_score &&
        std::ranges::any_of(queue, [](const review_item &i) { return i.m_score.has_value(); });
}

auto order_key(const review_item &i, bool by_score)
{
    const float score = by_score ? i.m_score.value_or(std::numeric_limits<float>::infinity()) : 0.f;
    return std::tuple{score, i.m_file_index, i.m_start_s, i.m_id};
}
}

review_settings review_settings::from_json(const nlohmann::json &j)
{
    review_settings s;
    if(!j.is_object())
        return s;
    s.m_onset_pre_s = j.value("onset_pre_s", s.m_onset_pre_s);
    s.m_onset_post_s = j.value("onset_post_s", s.m_onset_post_s);
    s.m_pre_roll_s = j.value("pre_roll_s", s.m_pre_roll_s);
    s.m_view_pre_s = j.value("view_pre_s", s.m_view_pre_s);
    s.m_view_post_s = j.value("view_post_s", s.m_view_post_s);
    s.m_snap_window_s = j.value("snap_window_s", s.m_snap_window_s);
    s.m_by_score = j.value("by_score", s.m_by_score);
    return s;
}

time_span playback_window(const label &l, const review_settings &settings, double duration_s)
{
    double start{}, stop{};
    if(l.is_onset())
    {
        start = l.m_start_s - settings.m_onset_pre_s;
        stop = l.m_start_s + settings.m_onset_post_s;
    }
    else
    {
        start = l.m_start_s - settings.m_pre_roll_s;
        stop = l.m_stop_s;
    }
    start = std::clamp(start, 0., duration_s);
    stop = std::clamp(stop, start, duration_s);
    return {start, stop};
}

time_span review_view(const label &l, const review_settings &settings)
{
    return {
        l.m_start_s - settings.m_view_pre_s,
        std::max(l.m_stop_s + settings.m_view_pre_s, l.m_start_s + settings.m_view_post_s)
    };
}

std::vector<review_item> review_queue(const project_model &project, const review_settings &settings)
{
    std::vector<review_item> queue;
    const auto &files = project.get_files();
    for(size_t i = 0U; i != files.size(); ++i)
    {
        if(project.get_status(files[i].m_path) == file_status::skip)
            continue;
        for(const auto &l : project.get_labels(files[i].m_path))
            if(l.m_state == label_state::suggested)
                queue.push_back(review_item{files[i].m_path, l.m_id, l.m_start_s, l.m_score, i});
    }
    const bool by_score = uses_scores(queue, settings);
    std::ranges::sort(queue, {}, [&](const review_item &i) { return order_key(i, by_score); });
    return queue;
}

std::optional<review_item> step_review(
    const std::vector<review_item> &queue,
    const std::optional<review_item> &from,
    bool forward,
    const review_settings &settings)
{
    if(queue.empty())
        return std::nullopt;
    if(!from)
        return forward ? queue.front() : queue.back();

    const bool by_score = uses_scores(queue, settings);
    const auto key = order_key(*from, by_score);
    if(forward)
    {
        for(const auto &item : queue)
            if(order_key(item, by_score) > key)
                return item;
        return queue.front();
    }
    for(auto it = queue.rbegin(); it != queue.rend(); ++it)
        if(order_key(*it, by_score) < key)
            return *it;
    return queue.back();
}
