/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <compare>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../model/audio_buffer.h"
#include "spectrogram.h"

/// Identifies a tile: a fixed number of columns at a zoom level. Level k has 2^k samples per
/// column, so tiles of one level line up whatever the view, and are reused while scrolling.
struct spectrogram_tile_key
{
    uint64_t m_generation{}; ///< Of the audio and settings, see spectrogram_tiles::set_audio
    int m_level{};
    int64_t m_index{};

    auto operator<=>(const spectrogram_tile_key &) const = default;
};

struct spectrogram_tile
{
    spectrogram_tile_key m_key;
    spectrogram_image m_image; ///< Its times are the tile's
};

/// Spectrogram tiles computed on worker threads and cached, so that the view never waits for an
/// FFT. The view asks for the tiles covering what it shows; those ready are returned, the rest are
/// queued (replacing earlier requests, which are no longer visible) and appear on later frames.
/// Thread safe; the audio is shared, never copied.
class spectrogram_tiles
{
public:
    static constexpr size_t columns_per_tile = 256U;
    static constexpr size_t rows = 256U; ///< Scaled to the plot's height when drawn
    static constexpr int min_level = 4;  ///< 16 samples per column at the finest
    static constexpr int max_level = 30;

    using tile_ptr = std::shared_ptr<const spectrogram_tile>;

    explicit spectrogram_tiles(size_t threads = 2U, size_t max_tiles = 160U);
    ~spectrogram_tiles();
    spectrogram_tiles(const spectrogram_tiles &) = delete;
    spectrogram_tiles &operator=(const spectrogram_tiles &) = delete;

    /// Start over with new audio or settings, dropping the cache and any queued work.
    /// Does nothing if they're the same as before.
    void set_audio(std::shared_ptr<const audio_buffer> audio, const spectrogram_settings &settings);

    /// The coarsest level with at most the given seconds per column, so it's at least as sharp as
    /// the screen
    int level_for(double seconds_per_pixel) const;

    /// Seconds covered by one tile at a level
    double tile_seconds(int level) const;

    /// The tiles covering [start_s, stop_s] at a level that are ready. Missing ones (and a tile
    /// either side, for scrolling) are queued, the middle of the view first.
    std::vector<tile_ptr> request(double start_s, double stop_s, int level);

    /// Ready tiles at coarser levels covering the range, coarsest first, to show until the
    /// requested level is ready
    std::vector<tile_ptr> fallback(double start_s, double stop_s, int level) const;

    /// Tiles queued or being computed
    size_t pending() const;

    uint64_t generation() const;

private:
    void work();
    /// Index range of tiles covering a time range at a level, clipped to the audio
    std::pair<int64_t, int64_t> tile_range(double start_s, double stop_s, int level) const;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop{false};

    std::shared_ptr<const audio_buffer> m_audio;
    spectrogram_settings m_settings;
    uint64_t m_generation{};

    std::deque<spectrogram_tile_key> m_queue;
    size_t m_busy{}; ///< Tiles being computed now
    std::map<spectrogram_tile_key, tile_ptr> m_tiles;
    std::map<spectrogram_tile_key, uint64_t> m_last_used; ///< For evicting the least recently used
    uint64_t m_use_counter{};
    size_t m_max_tiles;

    std::vector<std::thread> m_threads;
};
