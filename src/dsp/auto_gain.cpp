/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>

#include "auto_gain.h"

namespace
{
float from_db(float db) { return std::pow(10.f, db / 20.f); }

/// Per-sample smoothing coefficient for a time constant
float coefficient(float time_s, uint32_t rate)
{
    return time_s > 0.f && rate ? std::exp(-1.f / (time_s * float(rate))) : 0.f;
}

constexpr float min_auto_gain = 0.1f; ///< -20 dB: loud recordings are turned down, but not far
}

void auto_gain::configure(const settings &s, uint32_t sample_rate_hz)
{
    m_settings = s;
    m_attack_coef = coefficient(s.m_attack_s, sample_rate_hz);
    m_release_coef = coefficient(s.m_release_s, sample_rate_hz);
    m_max_gain = from_db(std::max(0.f, s.m_max_gain_db));
    m_gain = s.m_automatic ? gain_for(m_envelope) : from_db(s.m_gain_db);
}

void auto_gain::prime(const float *samples, size_t n)
{
    float peak = 0.f;
    for(size_t i = 0U; i != n; ++i)
        peak = std::max(peak, std::abs(samples[i]));
    m_envelope = peak;
    if(m_settings.m_automatic)
        m_gain = gain_for(m_envelope);
}

float auto_gain::gain_for(float envelope) const
{
    if(envelope * m_max_gain <= m_settings.m_target)
        return m_max_gain;
    return std::max(min_auto_gain, m_settings.m_target / envelope);
}

void auto_gain::process(float *samples, size_t n)
{
    if(!m_settings.m_automatic)
    {
        m_gain = from_db(m_settings.m_gain_db);
        for(size_t i = 0U; i != n; ++i)
            samples[i] = std::clamp(samples[i] * m_gain, -1.f, 1.f);
        return;
    }

    for(size_t i = 0U; i != n; ++i)
    {
        const float level = std::abs(samples[i]);
        const float coef = level > m_envelope ? m_attack_coef : m_release_coef;
        m_envelope = level + coef * (m_envelope - level);
        m_gain = gain_for(m_envelope);
        samples[i] = std::clamp(samples[i] * m_gain, -1.f, 1.f);
    }
}

float auto_gain::current_gain_db() const { return 20.f * std::log10(std::max(m_gain, 1e-6f)); }
