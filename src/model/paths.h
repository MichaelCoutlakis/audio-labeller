/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
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

/// Replace each ${NAME} with the environment variable NAME, so that one project can work on
/// machines with different drive letters and mount points. Throws if a variable isn't set.
inline std::string expand_env_vars(const std::string &text)
{
    std::string out;
    size_t pos = 0U;
    while(pos < text.size())
    {
        const auto open = text.find("${", pos);
        if(open == std::string::npos)
            break;
        const auto close = text.find('}', open + 2U);
        if(close == std::string::npos)
            throw std::runtime_error("Unterminated ${ in path " + text);
        out.append(text, pos, open - pos);

        const auto name = text.substr(open + 2U, close - open - 2U);
        const char *value = std::getenv(name.c_str());
        if(!value)
            throw std::runtime_error(
                "Environment variable " + name + " is not set, but is used in " + text);
        out += value;
        pos = close + 1U;
    }
    out.append(text, pos);
    return out;
}

inline std::filesystem::path expand_env_vars(const std::filesystem::path &p)
{
    return path_from_utf8(expand_env_vars(generic_utf8(p)));
}
