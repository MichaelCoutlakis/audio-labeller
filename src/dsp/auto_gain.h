/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>

/// Playback gain: fixed, or automatic so that quiet recordings are audible. The automatic gain
/// follows the peak level (fast attack, slow release) and raises it towards a target, up to a
/// maximum gain. The output is clipped to full scale so a sudden loud sound can't blast.
class auto_gain
{
public:
    struct settings
    {
        bool m_automatic{false};
        float m_gain_db{0.f};      ///< Fixed gain, when not automatic
        float m_max_gain_db{40.f}; ///< Automatic gain is at most this
        float m_target{0.5f};      ///< Peak level the automatic gain aims for (-6 dBFS)
        float m_attack_s{0.002f};
        float m_release_s{0.5f};
    };

    void configure(const settings &s, uint32_t sample_rate_hz);
    const settings &get_settings() const { return m_settings; }

    /// Start the level from the loudest of these samples, eg. the start of what's about to play,
    /// so that playback doesn't begin too loud or too quiet
    void prime(const float *samples, size_t n);

    /// Apply the gain in place
    void process(float *samples, size_t n);

    /// The gain applied to the last sample, in dB
    float current_gain_db() const;

private:
    float gain_for(float envelope) const;

    settings m_settings;
    float m_attack_coef{};
    float m_release_coef{};
    float m_max_gain{100.f};
    float m_envelope{};
    float m_gain{1.f};
};
