/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "../model/audio_buffer.h"
#include "../model/types.h"

/// Where playback is within a mono clip. It has no locking of its own: audio_engine guards it with
/// the audio stream's lock.
struct playback_cursor
{
    uint64_t m_playhead{};
    uint64_t m_region_start{};
    uint64_t m_region_end{};
    bool m_loop{false};
    bool m_playing{false};

    void reset(const audio_buffer &clip)
    {
        m_playhead = m_region_start = 0U;
        m_region_end = clip.num_frames();
        m_playing = false;
    }

    void select_region(const audio_buffer &clip, time_span t, bool loop)
    {
        const double duration = clip.duration_s();
        const double t0 = std::clamp(t.first, 0., duration);
        const double t1 = std::clamp(t.second, t0, duration);
        m_region_start = static_cast<uint64_t>(t0 * clip.m_sample_rate_hz);
        m_region_end = std::clamp<uint64_t>(
            static_cast<uint64_t>(t1 * clip.m_sample_rate_hz),
            m_region_start,
            clip.num_frames());
        m_playhead = m_region_start;
        m_loop = loop;
    }

    /// Start from the beginning of the region. @return whether there's anything to play
    bool play()
    {
        m_playing = m_region_end > m_region_start;
        m_playhead = m_region_start;
        return m_playing;
    }

    /// Copy the next frames into out, following the region and looping.
    /// @return the number of frames written, fewer than requested once playback ends
    size_t read(const audio_buffer &clip, float *out, size_t num_frames)
    {
        size_t written = 0U;
        while(m_playing && written < num_frames)
        {
            if(m_playhead >= m_region_end)
            {
                if(!m_loop)
                {
                    m_playing = false;
                    break;
                }
                m_playhead = m_region_start;
            }
            const auto n = std::min<uint64_t>(num_frames - written, m_region_end - m_playhead);
            std::copy_n(clip.m_samples.data() + m_playhead, n, out + written);
            m_playhead += n;
            written += n;
        }
        return written;
    }
};
