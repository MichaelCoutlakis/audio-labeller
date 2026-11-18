/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

using time_span = std::pair<double, double>;

/// A label's frequency range, drawn and selected on the spectrogram. The default is every
/// frequency, which is what a label made on the waveform has.
struct frequency_band
{
    double m_low_hz{0.};
    double m_high_hz{std::numeric_limits<double>::infinity()};

    bool is_full() const { return m_low_hz <= 0. && !std::isfinite(m_high_hz); }
    bool contains(double hz) const { return m_low_hz <= hz && hz <= m_high_hz; }
    bool operator==(const frequency_band &) const = default;
};

struct playback_state
{
    bool m_playing{false};
    double m_playhead_s{};
    float m_gain_db{}; ///< Applied to what's played, which changes when it's automatic
};

struct audio_dev
{
    uint32_t m_id{}; ///< Only valid for this run. Devices are remembered between runs by name.
    std::string m_name;
    bool operator==(const audio_dev &other) const = default;
};
