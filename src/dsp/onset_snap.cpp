/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "onset_snap.h"

double snap_onset(const audio_buffer &audio, double t_s, double window_s)
{
    const auto rate = static_cast<double>(audio.m_sample_rate_hz);
    const auto frames = static_cast<int64_t>(audio.num_frames());
    if(rate <= 0. || frames < 8)
        return t_s;

    const auto sample = [&](int64_t n)
    {
        n = std::clamp<int64_t>(n, 0, frames - 1);
        return static_cast<double>(audio.m_samples[static_cast<size_t>(n) * audio.m_channels]);
    };

    const int64_t half = std::max<int64_t>(1, std::llround(0.00025 * rate)); // Smoothing
    const int64_t centre = std::llround(t_s * rate);
    const int64_t reach = std::max<int64_t>(1, std::llround(window_s * rate));
    const int64_t lo = std::max<int64_t>(0, centre - reach);
    const int64_t hi = std::min<int64_t>(frames - 1, centre + reach);
    if(hi <= lo)
        return t_s;

    // Teager-Kaiser energy over the window and the smoothing either side
    const int64_t first = lo - 2 * half - 1;
    const int64_t count = hi - first + 2 * half + 2;
    std::vector<double> energy(static_cast<size_t>(count));
    for(int64_t i = 0; i != count; ++i)
    {
        const int64_t n = first + i;
        const double x = sample(n);
        energy[static_cast<size_t>(i)] = std::max(0., x * x - sample(n - 1) * sample(n + 1));
    }

    // Centred moving average, then its rise across the smoothing width
    std::vector<double> prefix(energy.size() + 1U);
    for(size_t i = 0U; i != energy.size(); ++i)
        prefix[i + 1U] = prefix[i] + energy[i];
    const auto smoothed = [&](int64_t n)
    {
        const auto i = n - first;
        const auto a = static_cast<size_t>(std::clamp<int64_t>(i - half, 0, count));
        const auto b = static_cast<size_t>(std::clamp<int64_t>(i + half + 1, 0, count));
        return b > a ? (prefix[b] - prefix[a]) / double(b - a) : 0.;
    };

    std::vector<double> rise(static_cast<size_t>(hi - lo + 1));
    double best = 0., loudest = 0.;
    size_t best_i = 0U;
    for(int64_t n = lo; n <= hi; ++n)
    {
        loudest = std::max(loudest, smoothed(n + half));
        const double r = smoothed(n + half) - smoothed(n - half);
        rise[static_cast<size_t>(n - lo)] = r;
        if(r > best)
        {
            best = r;
            best_i = static_cast<size_t>(n - lo);
        }
    }
    if(best <= 0.1 * loudest || best <= 0.)
        return t_s; // No clear rise, eg. silence or a steady sound

    // The middle of the steepest stretch, as a steady onset gives a plateau rather than a peak
    size_t a = best_i, b = best_i;
    while(a > 0U && rise[a - 1U] >= 0.9 * best)
        --a;
    while(b + 1U < rise.size() && rise[b + 1U] >= 0.9 * best)
        ++b;
    double weight = 0., sum = 0.;
    for(size_t i = a; i <= b; ++i)
    {
        weight += rise[i];
        sum += rise[i] * double(i);
    }
    return (double(lo) + sum / weight) / rate;
}
