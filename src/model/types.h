/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <string>
#include <utility>

using time_span = std::pair<double, double>;

struct playback_state
{
    bool playing{false};
    double playhead_s{};
};


struct audio_dev
{
    int index;
    std::string name;
    bool operator==(const audio_dev& other) const = default;
};
