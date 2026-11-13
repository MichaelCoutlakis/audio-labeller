/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "../model/audio_buffer.h"

enum class frequency_scale
{
    linear,
    log,
    mel
};

struct spectrogram_settings
{
    size_t m_fft_size{1024}; ///< A power of two
    frequency_scale m_scale{frequency_scale::mel};
    double m_min_hz{20.}; ///< Bottom of the log and mel scales
    /// FFT frames per column at most, when zoomed out. Each column shows their maximum, so short
    /// events between frames aren't missed.
    size_t m_max_frames_per_column{4};
};

/// Levels in dB (relative to a full scale sine) for a grid of time columns by frequency rows.
/// Row 0 is the lowest frequency.
struct spectrogram_image
{
    size_t m_columns{};
    size_t m_rows{};
    std::vector<float> m_db; ///< m_rows * m_columns, row by row
    double m_start_s{};
    double m_stop_s{};
    double m_min_hz{};
    double m_max_hz{};

    float at(size_t row, size_t column) const { return m_db[row * m_columns + column]; }
};

/// In-place radix-2 FFT. The size must be a power of two.
void fft(std::vector<std::complex<float>> &data);

/// The frequency at the bottom edge of row (0 to rows, so rows gives the top) on the scale
double row_frequency(double row, size_t rows, double min_hz, double max_hz, frequency_scale scale);

/// Spectrogram of the first channel between two times, for drawing at the given size
spectrogram_image compute_spectrogram(
    const audio_buffer &audio,
    double start_s,
    double stop_s,
    size_t columns,
    size_t rows,
    const spectrogram_settings &settings);
