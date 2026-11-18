/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <nlohmann/json.hpp>

/// A high-pass filter for wind and handling noise in field recordings, applied to a recording
/// once when it's read, so that the waveform, spectrogram and playback all have it
struct high_pass_settings
{
    bool m_enabled{false};
    double m_cutoff_hz{100.};
    int m_order{4}; ///< Butterworth order, even: 2, 4, 6 or 8

    bool operator==(const high_pass_settings &) const = default;

    /// Missing keys keep their defaults. Throws on an order or cutoff out of range.
    static high_pass_settings from_json(const nlohmann::json &j);
    /// Only the keys that differ from the defaults
    nlohmann::json to_json() const;
};

/// One second order section, in transposed direct form II
struct biquad
{
    double m_b0{1.}, m_b1{}, m_b2{}, m_a1{}, m_a2{};
};

/// A Butterworth high-pass of the given (even) order as cascaded biquads
std::vector<biquad> butterworth_high_pass(double cutoff_hz, double sample_rate_hz, int order);

/// A Butterworth low-pass of the given (even) order as cascaded biquads
std::vector<biquad> butterworth_low_pass(double cutoff_hz, double sample_rate_hz, int order);

/// Filter forwards and then backwards, which cancels the phase shift: sounds don't move in time,
/// which matters for labels and onsets. The response is the filter's squared, so -6 dB at the
/// cutoff and twice the roll-off. Each pass starts from the steady state of its first sample,
/// so a DC offset doesn't ring at the start or end.
void filter_zero_phase(std::span<float> mono, const std::vector<biquad> &sections);

/// Apply the settings to mono samples, doing nothing if disabled or the cutoff is out of range
void apply_high_pass(std::span<float> mono, uint32_t sample_rate_hz, const high_pass_settings &s);
