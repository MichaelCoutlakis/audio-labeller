/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>
#include <set>

#include "spectrogram_tiles.h"

spectrogram_tiles::spectrogram_tiles(size_t threads, size_t max_tiles) :
    m_max_tiles(std::max<size_t>(max_tiles, 8U))
{
    for(size_t i = 0U; i != std::max<size_t>(threads, 1U); ++i)
        m_threads.emplace_back([this] { work(); });
}

spectrogram_tiles::~spectrogram_tiles()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
        m_queue.clear();
    }
    m_wake.notify_all();
    for(auto &t : m_threads)
        t.join();
}

void spectrogram_tiles::set_audio(
    std::shared_ptr<const audio_buffer> audio,
    const spectrogram_settings &settings)
{
    std::lock_guard lock(m_mutex);
    if(audio == m_audio &&
       settings.m_fft_size == m_settings.m_fft_size &&
       settings.m_scale == m_settings.m_scale &&
       settings.m_min_hz == m_settings.m_min_hz &&
       settings.m_max_frames_per_column == m_settings.m_max_frames_per_column)
        return;
    m_audio = std::move(audio);
    m_settings = settings;
    ++m_generation; // Work in progress for the old audio is thrown away when it finishes
    m_queue.clear();
    m_tiles.clear();
    m_last_used.clear();
}

int spectrogram_tiles::level_for(double seconds_per_pixel) const
{
    std::lock_guard lock(m_mutex);
    const double rate = m_audio ? m_audio->m_sample_rate_hz : 0.;
    const double samples = seconds_per_pixel * rate;
    if(!(samples > 1.))
        return min_level;
    // Round down, so that a column is never wider than a pixel
    return std::clamp(static_cast<int>(std::floor(std::log2(samples))), min_level, max_level);
}

double spectrogram_tiles::tile_seconds(int level) const
{
    std::lock_guard lock(m_mutex);
    const double rate = m_audio && m_audio->m_sample_rate_hz ? m_audio->m_sample_rate_hz : 1.;
    return std::ldexp(double(columns_per_tile), level) / rate;
}

std::pair<int64_t, int64_t> spectrogram_tiles::tile_range(double start_s, double stop_s, int level)
    const
{
    // Called with the lock held
    if(!m_audio || m_audio->m_sample_rate_hz == 0U || m_audio->num_frames() == 0U)
        return {0, -1};
    const double tile_s = std::ldexp(double(columns_per_tile), level) / m_audio->m_sample_rate_hz;
    const auto last_tile = static_cast<int64_t>(std::ceil(m_audio->duration_s() / tile_s)) - 1;
    const auto first = std::max<int64_t>(0, static_cast<int64_t>(std::floor(start_s / tile_s)));
    const auto last = std::min(last_tile, static_cast<int64_t>(std::floor(stop_s / tile_s)));
    return {first, last};
}

std::vector<spectrogram_tiles::tile_ptr> spectrogram_tiles::request(
    double start_s,
    double stop_s,
    int level)
{
    std::vector<tile_ptr> ready;
    {
        std::lock_guard lock(m_mutex);
        auto [first, last] = tile_range(start_s, stop_s, level);
        if(last < first)
            return ready;

        // Visible tiles from the middle outwards, then one either side for scrolling
        std::vector<int64_t> wanted;
        const int64_t middle = (first + last) / 2;
        for(int64_t d = 0; middle - d >= first || middle + d <= last; ++d)
        {
            if(middle + d <= last)
                wanted.push_back(middle + d);
            if(d && middle - d >= first)
                wanted.push_back(middle - d);
        }
        const auto [lo, hi] = tile_range(start_s - 1e9, stop_s + 1e9, level);
        if(last + 1 <= hi)
            wanted.push_back(last + 1);
        if(first - 1 >= lo)
            wanted.push_back(first - 1);

        m_queue.clear(); // What isn't visible any more isn't wanted
        for(const auto index : wanted)
        {
            const spectrogram_tile_key key{m_generation, level, index};
            if(auto it = m_tiles.find(key); it != m_tiles.end())
            {
                m_last_used[key] = ++m_use_counter;
                if(index >= first && index <= last)
                    ready.push_back(it->second);
            }
            else
                m_queue.push_back(key);
        }
    }
    m_wake.notify_all();
    return ready;
}

std::vector<spectrogram_tiles::tile_ptr> spectrogram_tiles::fallback(
    double start_s,
    double stop_s,
    int level) const
{
    std::vector<tile_ptr> out;
    std::lock_guard lock(m_mutex);
    for(const auto &[key, tile] : m_tiles)
    {
        if(key.m_generation != m_generation || key.m_level <= level)
            continue;
        if(tile->m_image.m_stop_s > start_s && tile->m_image.m_start_s < stop_s)
            out.push_back(tile);
    }
    std::ranges::sort(out, std::greater{}, [](const tile_ptr &t) { return t->m_key.m_level; });
    return out;
}

size_t spectrogram_tiles::pending() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.size() + m_busy;
}

uint64_t spectrogram_tiles::generation() const
{
    std::lock_guard lock(m_mutex);
    return m_generation;
}

void spectrogram_tiles::work()
{
    std::unique_lock lock(m_mutex);
    while(true)
    {
        m_wake.wait(lock, [&] { return m_stop || !m_queue.empty(); });
        if(m_stop)
            return;

        const auto key = m_queue.front();
        m_queue.pop_front();
        if(m_tiles.contains(key))
            continue; // Queued twice by requests on successive frames
        auto audio = m_audio;
        const auto settings = m_settings;
        ++m_busy;
        lock.unlock();

        // The FFTs, without the lock
        auto tile = std::make_shared<spectrogram_tile>();
        tile->m_key = key;
        const double tile_s =
            std::ldexp(double(columns_per_tile), key.m_level) / audio->m_sample_rate_hz;
        const double start = double(key.m_index) * tile_s;
        tile->m_image =
            compute_spectrogram(*audio, start, start + tile_s, columns_per_tile, rows, settings);

        lock.lock();
        --m_busy;
        if(key.m_generation != m_generation)
            continue; // The audio changed meanwhile
        m_tiles[key] = std::move(tile);
        m_last_used[key] = ++m_use_counter;

        // Evict the least recently used
        while(m_tiles.size() > m_max_tiles)
        {
            const auto oldest = std::ranges::min_element(
                m_last_used,
                {},
                [](const auto &entry) { return entry.second; });
            m_tiles.erase(oldest->first);
            m_last_used.erase(oldest);
        }
    }
}
