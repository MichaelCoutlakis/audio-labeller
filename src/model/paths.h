/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <filesystem>
#include <string>

/// UTF-8 text of a path with forward slashes, for files, logs and display
inline std::string generic_utf8(const std::filesystem::path &p)
{
    const auto s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

inline std::filesystem::path path_from_utf8(const std::string &s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
