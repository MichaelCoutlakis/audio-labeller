/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <optional>

#include "../model/audio_buffer.h"
#include "../model/types.h"

/// How a label with a frequency band is played: filtered to the band, and optionally moved down
/// to where it's easier to hear
struct band_playback_settings
{
    /// Move a band that starts above m_shift_to_hz down to start there, keeping its width and
    /// timing (unlike slower playback). Many people hear little above 8 to 12 kHz.
    bool m_shift{false};
    double m_shift_to_hz{1000.};
};

/// What render_band() does to a band at a sample rate
struct band_plan
{
    std::optional<double> m_high_pass_hz; ///< At the band's bottom, unless it reaches 0 Hz
    std::optional<double> m_low_pass_hz;  ///< At the band's top, unless it reaches Nyquist
    double m_shift_hz{};                  ///< How far down the band is moved, 0 for not at all

    bool filters() const { return m_high_pass_hz || m_low_pass_hz; }
};

band_plan plan_band(frequency_band band, double sample_rate_hz, const band_playback_settings &s);

/// The order of the Butterworth filters, run forwards and backwards (so twice the roll-off)
inline constexpr int band_filter_order = 6;

struct band_excerpt
{
    audio_buffer m_audio;
    double m_start_s{}; ///< Where it starts in the recording, on a sample
};

/// A copy of the times t of the first channel, filtered to the band and shifted as planned, with
/// a few milliseconds of fade at each end. Audio either side lets the filters settle first, so
/// the edges don't ring. Only the selection is processed, not the whole recording.
band_excerpt render_band(
    const audio_buffer &audio,
    time_span t,
    frequency_band band,
    const band_playback_settings &s);
