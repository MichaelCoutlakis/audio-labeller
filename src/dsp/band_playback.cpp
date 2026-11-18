/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <vector>

#include "band_playback.h"
#include "high_pass.h"

namespace
{
constexpr double min_cutoff_hz = 1.;  ///< A band starting below this reaches 0 Hz
constexpr double max_cutoff = 0.45;   ///< Of the sample rate: above this, a band reaches Nyquist
constexpr double fade_s = 0.005;
constexpr double max_settle_s = 1.;
}

band_plan plan_band(frequency_band band, double sample_rate_hz, const band_playback_settings &s)
{
    band_plan plan;
    if(sample_rate_hz <= 0. || band.is_full())
        return plan;
    const double top = max_cutoff * sample_rate_hz;
    const double lo = band.m_low_hz, hi = std::min(band.m_high_hz, sample_rate_hz / 2.);
    if(hi <= lo)
        return plan;
    if(lo >= min_cutoff_hz && lo < top)
        plan.m_high_pass_hz = lo;
    if(hi < top)
        plan.m_low_pass_hz = hi;
    if(s.m_shift && plan.m_high_pass_hz && lo > s.m_shift_to_hz && s.m_shift_to_hz >= 0.)
        plan.m_shift_hz = lo - s.m_shift_to_hz;
    return plan;
}

band_excerpt render_band(
    const audio_buffer &audio,
    time_span t,
    frequency_band band,
    const band_playback_settings &s)
{
    band_excerpt excerpt;
    auto &out = excerpt.m_audio;
    out.m_sample_rate_hz = audio.m_sample_rate_hz;
    out.m_channels = 1U;
    out.m_source_channels = audio.m_source_channels;
    const double rate = audio.m_sample_rate_hz;
    const auto frames = audio.num_frames();
    if(rate <= 0. || frames == 0U)
        return excerpt;

    const auto frame_at = [&](double seconds)
    { return static_cast<size_t>(std::clamp(seconds * rate, 0., double(frames))); };
    const size_t first = frame_at(t.first), last = std::max(first, frame_at(t.second));
    excerpt.m_start_s = double(first) / rate;

    const auto plan = plan_band(band, rate, s);
    const double lo = plan.m_high_pass_hz.value_or(0.);
    const double hi = plan.m_low_pass_hz.value_or(max_cutoff * rate);

    // A narrow band (under a factor of 3) is filtered as a low-pass moved up to it, by mixing
    // (below), as its edges are too close for a high-pass and low-pass not to cut into its middle.
    // That needs the band's bottom to be at least half its width above 0 Hz, so wider bands get
    // the high-pass and low-pass, before any shift.
    const bool narrow = plan.m_high_pass_hz && hi <= 3. * lo;
    const bool mix = narrow || plan.m_shift_hz > 0.;

    // Settling time grows as the narrowest feature of the filters shrinks
    double narrowest = std::numeric_limits<double>::infinity();
    for(const auto hz : {plan.m_high_pass_hz, plan.m_low_pass_hz})
        if(hz)
            narrowest = std::min(narrowest, *hz);
    if(mix)
        narrowest = std::min(narrowest, (hi - lo) / 2.);
    const double settle_s =
        plan.filters() ? std::min(max_settle_s, 0.05 + 8. / narrowest) : 0.;
    const size_t pad = static_cast<size_t>(settle_s * rate);
    const size_t a = first > pad ? first - pad : 0U, b = std::min(frames, last + pad);

    std::vector<float> x(b - a);
    for(size_t i = 0U; i != x.size(); ++i)
        x[i] = audio.m_samples[(a + i) * audio.m_channels];

    if(!narrow)
    {
        if(plan.m_high_pass_hz)
            filter_zero_phase(x, butterworth_high_pass(lo, rate, band_filter_order));
        if(plan.m_low_pass_hz)
            filter_zero_phase(x, butterworth_low_pass(hi, rate, band_filter_order));
    }
    if(mix)
    {
        // Weaver's single sideband method: mix the band's centre down to 0 Hz, low-pass to half
        // its width (which selects the band), and mix up to its new centre, the same one if it
        // isn't shifted. The band moves down without being mirrored.
        const double centre = (lo + hi) / 2., half = (hi - lo) / 2.;
        const double w1 = 2. * std::numbers::pi * centre / rate;
        const double w2 = 2. * std::numbers::pi * (centre - plan.m_shift_hz) / rate;
        std::vector<float> in_phase(x.size()), quadrature(x.size());
        for(size_t n = 0U; n != x.size(); ++n)
        {
            in_phase[n] = static_cast<float>(x[n] * std::cos(w1 * double(n)));
            quadrature[n] = static_cast<float>(x[n] * std::sin(w1 * double(n)));
        }
        const auto sections = butterworth_low_pass(half, rate, band_filter_order);
        filter_zero_phase(in_phase, sections);
        filter_zero_phase(quadrature, sections);
        for(size_t n = 0U; n != x.size(); ++n)
            x[n] = static_cast<float>(
                2. * (in_phase[n] * std::cos(w2 * double(n)) +
                      quadrature[n] * std::sin(w2 * double(n))));
    }

    out.m_samples.assign(x.begin() + static_cast<ptrdiff_t>(first - a),
                         x.begin() + static_cast<ptrdiff_t>(last - a));

    // Fade in and out, so cutting into the filtered audio doesn't click
    const size_t n = out.m_samples.size();
    const size_t fade = std::min(n / 2U, static_cast<size_t>(fade_s * rate));
    for(size_t i = 0U; i < fade; ++i)
    {
        const auto g = static_cast<float>(
            0.5 - 0.5 * std::cos(std::numbers::pi * double(i) / double(fade)));
        out.m_samples[i] *= g;
        out.m_samples[n - 1U - i] *= g;
    }
    return excerpt;
}
