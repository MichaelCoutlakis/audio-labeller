/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstdint>

/// Colours are packed RGBA with red in the lowest byte. This is the same layout as ImGui's
/// IM_COL32, so the views can use them directly (checked in app/colours.h).
constexpr uint32_t pack_rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    return (r & 0xFFU) | (g & 0xFFU) << 8 | (b & 0xFFU) << 16 | (a & 0xFFU) << 24;
}

constexpr uint8_t colour_channel(uint32_t rgba, int index)
{
    return static_cast<uint8_t>(rgba >> (8 * index));
}

inline constexpr uint32_t unknown_class_colour = pack_rgba(127, 127, 127, 127);
