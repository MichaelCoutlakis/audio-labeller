/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

#include "high_pass.h"

high_pass_settings high_pass_settings::from_json(const nlohmann::json &j)
{
    high_pass_settings s;
    if(!j.is_object())
        return s;
    s.m_enabled = j.value("enabled", s.m_enabled);
    s.m_cutoff_hz = j.value("cutoff_hz", s.m_cutoff_hz);
    s.m_order = j.value("order", s.m_order);
    if(s.m_order < 2 || s.m_order > 8 || s.m_order % 2 != 0)
        throw std::runtime_error(
            "The high-pass order must be 2, 4, 6 or 8, not " + std::to_string(s.m_order));
    if(!(s.m_cutoff_hz > 0.))
        throw std::runtime_error("The high-pass cutoff must be above 0 Hz");
    return s;
}

nlohmann::json high_pass_settings::to_json() const
{
    const high_pass_settings d;
    auto j = nlohmann::json::object();
    if(m_enabled != d.m_enabled)
        j["enabled"] = m_enabled;
    if(m_cutoff_hz != d.m_cutoff_hz)
        j["cutoff_hz"] = m_cutoff_hz;
    if(m_order != d.m_order)
        j["order"] = m_order;
    return j;
}

std::vector<biquad> butterworth_high_pass(double cutoff_hz, double sample_rate_hz, int order)
{
    // Bilinear transform of the analogue prototype, prewarped, one section per pole pair: the
    // RBJ cookbook high-pass with each pair's Q
    const double w0 = 2. * std::numbers::pi * cutoff_hz / sample_rate_hz;
    const double cos_w0 = std::cos(w0);
    std::vector<biquad> sections;
    for(int k = 1; k <= order / 2; ++k)
    {
        const double q = 1. / (2. * std::cos(std::numbers::pi * (2. * k - 1.) / (2. * order)));
        const double alpha = std::sin(w0) / (2. * q);
        const double a0 = 1. + alpha;
        sections.push_back(
            biquad{
                (1. + cos_w0) / 2. / a0,
                -(1. + cos_w0) / a0,
                (1. + cos_w0) / 2. / a0,
                -2. * cos_w0 / a0,
                (1. - alpha) / a0
            });
    }
    return sections;
}

std::vector<biquad> butterworth_low_pass(double cutoff_hz, double sample_rate_hz, int order)
{
    // As for the high-pass, with the cookbook's low-pass
    const double w0 = 2. * std::numbers::pi * cutoff_hz / sample_rate_hz;
    const double cos_w0 = std::cos(w0);
    std::vector<biquad> sections;
    for(int k = 1; k <= order / 2; ++k)
    {
        const double q = 1. / (2. * std::cos(std::numbers::pi * (2. * k - 1.) / (2. * order)));
        const double alpha = std::sin(w0) / (2. * q);
        const double a0 = 1. + alpha;
        sections.push_back(
            biquad{
                (1. - cos_w0) / 2. / a0,
                (1. - cos_w0) / a0,
                (1. - cos_w0) / 2. / a0,
                -2. * cos_w0 / a0,
                (1. - alpha) / a0
            });
    }
    return sections;
}

namespace
{
/// One pass through every section, in double precision: with low cutoffs the poles are close to
/// the unit circle, where float rounding would be audible. The sections run together, sample by
/// sample, so that the processor overlaps their dependency chains: about 1.6 times as fast as a
/// pass per section (2.5 s for an hour at 48 kHz, order 4, forwards and backwards).
template <typename It>
void filter_pass(It first, It last, const std::vector<biquad> &sections)
{
    if(first == last)
        return;
    constexpr size_t max_sections = 4U; // Order 8
    if(sections.size() > max_sections)
        throw std::invalid_argument("At most 4 filter sections (order 8)");
    const size_t n = sections.size();

    // Steady state for a constant input at the first sample's level
    std::array<double, max_sections> z1{}, z2{};
    double level = *first;
    for(size_t k = 0U; k != n; ++k)
    {
        const auto &s = sections[k];
        const double out = level * (s.m_b0 + s.m_b1 + s.m_b2) / (1. + s.m_a1 + s.m_a2);
        z2[k] = s.m_b2 * level - s.m_a2 * out;
        z1[k] = s.m_b1 * level - s.m_a1 * out + z2[k];
        level = out;
    }

    for(auto it = first; it != last; ++it)
    {
        double x = *it;
        for(size_t k = 0U; k != n; ++k)
        {
            const auto &s = sections[k];
            const double y = s.m_b0 * x + z1[k];
            z1[k] = s.m_b1 * x - s.m_a1 * y + z2[k];
            z2[k] = s.m_b2 * x - s.m_a2 * y;
            x = y;
        }
        *it = static_cast<float>(x);
    }
}
}

void filter_zero_phase(std::span<float> mono, const std::vector<biquad> &sections)
{
    filter_pass(mono.begin(), mono.end(), sections);
    filter_pass(mono.rbegin(), mono.rend(), sections);
}

void apply_high_pass(std::span<float> mono, uint32_t sample_rate_hz, const high_pass_settings &s)
{
    if(!s.m_enabled ||
       sample_rate_hz == 0U ||
       s.m_cutoff_hz <= 0. ||
       s.m_cutoff_hz >= 0.45 * sample_rate_hz)
        return;
    filter_zero_phase(mono, butterworth_high_pass(s.m_cutoff_hz, sample_rate_hz, s.m_order));
}
