/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <chrono>
#include <ctime>
#include <fstream>
#include <stdexcept>

#include "../model/paths.h"
#include "files.h"

void write_file_atomically(const std::filesystem::path &target, const std::string &contents)
{
    auto tmp = target;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if(!f)
            throw std::runtime_error("Could not create " + generic_utf8(tmp));
        f << contents;
        f.flush();
        if(!f)
            throw std::runtime_error("Could not write " + generic_utf8(tmp));
    }
    std::filesystem::rename(tmp, target);
}

std::string csv_escape(const std::string &s)
{
    if(s.find_first_of(", \"\r\n") == std::string::npos)
        return s;

    std::string out = "\"";
    for(char c : s)
    {
        if(c == '"')
            out += "\"\"";
        else
            out += c;
    }
    out += "\"";
    return out;
}

std::string utc_now_iso8601()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}
