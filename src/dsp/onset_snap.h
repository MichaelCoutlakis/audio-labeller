/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include "../model/audio_buffer.h"

/// The time of the sharpest rise in energy within window_s of t_s, for placing onsets
/// consistently. Energy is the Teager-Kaiser operator x[n]^2 - x[n-1] x[n+1], smoothed over
/// half a millisecond, which follows the amplitude and frequency of a transient and ignores slow
/// level changes. Returns t_s if there's no rise, eg. in silence. Reads the first channel.
double snap_onset(const audio_buffer &audio, double t_s, double window_s);
