/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>

#include "loudness.h"
#include "onset_snap.h"

namespace
{
/// RMS level of each frame of the first channel from frame `first` up to `last`, in dBFS (a full
/// scale sine reads -3 dB)
std::vector<double> frame_levels(
    const audio_buffer &audio,
    size_t frame_len,
    size_t first,
    size_t last)
{
    std::vector<double> level(last > first ? last - first : 0U);
    for(size_t f = 0U; f != level.size(); ++f)
    {
        double sum = 0.;
        const size_t begin = (first + f) * frame_len;
        for(size_t n = begin; n != begin + frame_len; ++n)
        {
            const double x = audio.m_samples[n * audio.m_channels];
            sum += x * x;
        }
        level[f] = 10. * std::log10(std::max(sum / double(frame_len), 1e-20));
    }
    return level;
}

/// The given quantile of the levels
double percentile(std::vector<double> levels, double q)
{
    const auto nth = static_cast<size_t>(q * double(levels.size() - 1U));
    std::ranges::nth_element(levels, levels.begin() + static_cast<ptrdiff_t>(nth));
    return levels[nth];
}
}

loudness_settings loudness_settings::from_json(const nlohmann::json &j)
{
    loudness_settings s;
    if(!j.is_object())
        return s;
    s.m_frame_s = std::max(0.001, j.value("frame_s", s.m_frame_s));
    s.m_margin_db = j.value("margin_db", s.m_margin_db);
    s.m_min_level_db = j.value("min_level_db", s.m_min_level_db);
    s.m_floor_percentile = std::clamp(j.value("floor_percentile", s.m_floor_percentile), 0., 1.);
    s.m_min_length_s = j.value("min_length_s", s.m_min_length_s);
    s.m_merge_gap_s = j.value("merge_gap_s", s.m_merge_gap_s);
    s.m_padding_s = j.value("padding_s", s.m_padding_s);
    s.m_onsets = j.value("onsets", s.m_onsets);
    return s;
}

nlohmann::json loudness_settings::to_json() const
{
    return {
        {"frame_s", m_frame_s},
        {"margin_db", m_margin_db},
        {"min_level_db", m_min_level_db},
        {"floor_percentile", m_floor_percentile},
        {"min_length_s", m_min_length_s},
        {"merge_gap_s", m_merge_gap_s},
        {"padding_s", m_padding_s},
        {"onsets", m_onsets}
    };
}

std::vector<loud_region> find_loud_regions(const audio_buffer &audio, const loudness_settings &s)
{
    std::vector<loud_region> out;
    const double rate = audio.m_sample_rate_hz;
    const size_t frames = audio.num_frames();
    const auto frame_len = static_cast<size_t>(std::max(1., std::round(s.m_frame_s * rate)));
    if(rate <= 0. || frames < frame_len)
        return out;

    const auto level = frame_levels(audio, frame_len, 0U, frames / frame_len);
    const double floor = percentile(level, s.m_floor_percentile);
    const double threshold = std::max(floor + s.m_margin_db, s.m_min_level_db);

    const double frame_s = double(frame_len) / rate;
    for(size_t f = 0U; f < level.size();)
    {
        if(level[f] < threshold)
        {
            ++f;
            continue;
        }
        loud_region r{double(f) * frame_s, 0., level[f], 0.};
        for(; f < level.size() && level[f] >= threshold; ++f)
            r.m_peak_db = std::max(r.m_peak_db, level[f]);
        r.m_stop_s = double(f) * frame_s;

        if(!out.empty() && r.m_start_s - out.back().m_stop_s < s.m_merge_gap_s)
        {
            out.back().m_stop_s = r.m_stop_s;
            out.back().m_peak_db = std::max(out.back().m_peak_db, r.m_peak_db);
        }
        else
            out.push_back(r);
    }

    const double duration = audio.duration_s();
    std::erase_if(out, [&](const loud_region &r) { return r.m_stop_s - r.m_start_s < s.m_min_length_s; });
    for(auto &r : out)
    {
        r.m_above_db = r.m_peak_db - threshold;
        if(s.m_onsets)
        {
            // The rise is within the first frame or so of the region
            r.m_start_s = snap_onset(audio, r.m_start_s + frame_s / 2., frame_s);
            r.m_stop_s = r.m_start_s;
        }
        else
        {
            r.m_start_s = std::max(0., r.m_start_s - s.m_padding_s);
            r.m_stop_s = std::min(duration, r.m_stop_s + s.m_padding_s);
        }
    }
    return out;
}

std::optional<double> estimate_sound_end(
    const audio_buffer &audio,
    double start_s,
    const sound_end_settings &s)
{
    const double rate = audio.m_sample_rate_hz;
    const auto frame_len = static_cast<size_t>(std::max(1., std::round(s.m_frame_s * rate)));
    const size_t total = rate > 0. ? audio.num_frames() / frame_len : 0U;
    const double frame_s = double(frame_len) / std::max(rate, 1.);
    const auto frame_at = [&](double t)
    { return std::min(total, static_cast<size_t>(std::max(0., t / frame_s))); };

    const size_t start = frame_at(start_s);
    const size_t end = frame_at(start_s + s.m_max_length_s);
    if(start >= end)
        return std::nullopt;
    const size_t context = frame_at(start_s - s.m_context_s);
    const auto level = frame_levels(audio, frame_len, context, end);
    const size_t first = start - context; // The sound's first frame, in level

    const double floor = percentile(level, s.m_floor_percentile);
    const size_t peak_end = std::min(level.size(), first + 1U + frame_at(s.m_peak_search_s));
    const double peak = *std::max_element(
        level.begin() + static_cast<ptrdiff_t>(first),
        level.begin() + static_cast<ptrdiff_t>(peak_end));
    if(peak - floor < s.m_min_rise_db)
        return std::nullopt;
    const double threshold = floor + std::min(s.m_margin_db, (peak - floor) / 2.);

    // From the peak on, the first run of frames below the threshold that lasts the hold time
    const auto peak_at = static_cast<size_t>(
        std::max_element(
            level.begin() + static_cast<ptrdiff_t>(first),
            level.begin() + static_cast<ptrdiff_t>(peak_end)) -
        level.begin());
    const auto hold = static_cast<size_t>(std::max(1., std::ceil(s.m_hold_s / frame_s)));
    size_t quiet = 0U;
    for(size_t f = peak_at; f != level.size(); ++f)
    {
        quiet = level[f] < threshold ? quiet + 1U : 0U;
        if(quiet == hold)
            return double(context + f + 1U - hold) * frame_s;
    }
    return std::min(start_s + s.m_max_length_s, audio.duration_s());
}
