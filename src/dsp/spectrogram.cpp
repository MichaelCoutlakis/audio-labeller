/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>

#include "spectrogram.h"

namespace
{
double hz_to_mel(double hz) { return 2595. * std::log10(1. + hz / 700.); }
double mel_to_hz(double mel) { return 700. * (std::pow(10., mel / 2595.) - 1.); }
}

void fft(std::vector<std::complex<float>> &data)
{
    const size_t n = data.size();
    if(n == 0U || (n & (n - 1U)) != 0U)
        throw std::invalid_argument("FFT size must be a power of two");

    for(size_t i = 1U, j = 0U; i < n; ++i)
    {
        size_t bit = n >> 1U;
        for(; j & bit; bit >>= 1U)
            j ^= bit;
        j ^= bit;
        if(i < j)
            std::swap(data[i], data[j]);
    }
    for(size_t len = 2U; len <= n; len <<= 1U)
    {
        const double angle = -2. * std::numbers::pi / double(len);
        const std::complex<float> step(float(std::cos(angle)), float(std::sin(angle)));
        for(size_t i = 0U; i < n; i += len)
        {
            std::complex<float> w(1.f, 0.f);
            for(size_t k = 0U; k != len / 2U; ++k)
            {
                const auto u = data[i + k];
                const auto v = data[i + k + len / 2U] * w;
                data[i + k] = u + v;
                data[i + k + len / 2U] = u - v;
                w *= step;
            }
        }
    }
}

double row_frequency(double row, size_t rows, double min_hz, double max_hz, frequency_scale scale)
{
    const double f = rows ? row / double(rows) : 0.;
    switch(scale)
    {
    case frequency_scale::log:
        return min_hz * std::pow(max_hz / min_hz, f);
    case frequency_scale::mel:
    {
        const double lo = hz_to_mel(min_hz), hi = hz_to_mel(max_hz);
        return mel_to_hz(lo + f * (hi - lo));
    }
    case frequency_scale::linear:
    default:
        return f * max_hz;
    }
}

double frequency_position(double hz, double min_hz, double max_hz, frequency_scale scale)
{
    switch(scale)
    {
    case frequency_scale::log:
        return std::log(hz / min_hz) / std::log(max_hz / min_hz);
    case frequency_scale::mel:
        return (hz_to_mel(hz) - hz_to_mel(min_hz)) / (hz_to_mel(max_hz) - hz_to_mel(min_hz));
    case frequency_scale::linear:
    default:
        return hz / max_hz;
    }
}

double spectrogram_min_hz(const spectrogram_settings &settings, double max_hz)
{
    return settings.m_scale == frequency_scale::linear ? 0. : std::min(settings.m_min_hz, max_hz / 2.);
}

spectrogram_image compute_spectrogram(
    const audio_buffer &audio,
    double start_s,
    double stop_s,
    size_t columns,
    size_t rows,
    const spectrogram_settings &settings)
{
    spectrogram_image image;
    const double rate = audio.m_sample_rate_hz;
    const auto frames = static_cast<int64_t>(audio.num_frames());
    const size_t n = settings.m_fft_size;
    if(rate <= 0. || columns == 0U || rows == 0U || stop_s <= start_s || frames == 0)
        return image;

    image.m_columns = columns;
    image.m_rows = rows;
    image.m_start_s = start_s;
    image.m_stop_s = stop_s;
    image.m_max_hz = rate / 2.;
    image.m_min_hz = spectrogram_min_hz(settings, image.m_max_hz);
    image.m_db.assign(rows * columns, -200.f);

    // Hann window, scaled so that a full scale sine reads 0 dB
    std::vector<float> window(n);
    double window_sum = 0.;
    for(size_t i = 0U; i != n; ++i)
    {
        window[i] = float(0.5 - 0.5 * std::cos(2. * std::numbers::pi * double(i) / double(n)));
        window_sum += window[i];
    }
    const double scale = 2. / window_sum;

    // Bins covered by each row
    std::vector<std::pair<size_t, size_t>> row_bins(rows);
    const double bin_hz = rate / double(n);
    for(size_t r = 0U; r != rows; ++r)
    {
        const double lo =
            row_frequency(double(r), rows, image.m_min_hz, image.m_max_hz, settings.m_scale);
        const double hi =
            row_frequency(double(r + 1U), rows, image.m_min_hz, image.m_max_hz, settings.m_scale);
        auto a = static_cast<size_t>(std::floor(lo / bin_hz + 0.5));
        auto b = static_cast<size_t>(std::floor(hi / bin_hz + 0.5));
        a = std::min(a, n / 2U);
        b = std::clamp(b, a, n / 2U);
        row_bins[r] = {a, b};
    }

    const double column_s = (stop_s - start_s) / double(columns);
    const auto frames_per_column = std::clamp<size_t>(
        static_cast<size_t>(column_s * rate / double(n / 2U)),
        1U,
        std::max<size_t>(1U, settings.m_max_frames_per_column));

    std::vector<std::complex<float>> buffer(n);
    std::vector<float> power(n / 2U + 1U);
    for(size_t c = 0U; c != columns; ++c)
    {
        std::ranges::fill(power, 0.f);
        for(size_t k = 0U; k != frames_per_column; ++k)
        {
            const double t = start_s + column_s * (double(c) + (double(k) + 0.5) / frames_per_column);
            const auto centre = static_cast<int64_t>(std::llround(t * rate));
            const int64_t first = centre - static_cast<int64_t>(n / 2U);
            for(size_t i = 0U; i != n; ++i)
            {
                const int64_t s = first + static_cast<int64_t>(i);
                const float x = s >= 0 && s < frames
                    ? audio.m_samples[static_cast<size_t>(s) * audio.m_channels]
                    : 0.f;
                buffer[i] = {x * window[i], 0.f};
            }
            fft(buffer);
            for(size_t b = 0U; b <= n / 2U; ++b)
                power[b] = std::max(power[b], std::norm(buffer[b]));
        }

        for(size_t r = 0U; r != rows; ++r)
        {
            const auto [a, b] = row_bins[r];
            float p = 0.f;
            for(size_t bin = a; bin <= b; ++bin)
                p = std::max(p, power[bin]);
            const double amplitude = std::sqrt(double(p)) * scale;
            image.m_db[r * columns + c] = float(20. * std::log10(std::max(amplitude, 1e-10)));
        }
    }
    return image;
}
