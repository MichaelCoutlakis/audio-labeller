/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <algorithm>

#include <imgui.h>

namespace colours
{
constexpr auto grey IM_COL32(127, 127, 127, 127);

constexpr auto drag_grey IM_COL32(255, 255, 255, 40);

inline ImU32 clearer(ImU32 in, float rel_change)
{
    ImColor out(in);
    out.Value.w = std::clamp(out.Value.w + rel_change, 0.f, 1.f);
    return out;
}

inline ImU32 brighter(ImU32 in, float rel_change = 0.25f)
{
    ImColor c(in);
    c.Value.x  = std::clamp((c.Value.x + rel_change), 0.f, 1.f);
    c.Value.y  = std::clamp((c.Value.y + rel_change), 0.f, 1.f);
    c.Value.z  = std::clamp((c.Value.z + rel_change), 0.f, 1.f);
    return c;

}
}