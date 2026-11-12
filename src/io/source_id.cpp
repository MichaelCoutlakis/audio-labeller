/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <spdlog/spdlog.h>

#include "../model/paths.h"
#include "source_id.h"

std::string compute_source_id(const std::filesystem::path &path)
{
    std::ifstream f(path, std::ios::binary);
    if(!f)
        throw std::runtime_error("Could not open " + generic_utf8(path));

    uint64_t hash = 14695981039346656037ULL;
    std::vector<char> buffer(1U << 20);
    while(f)
    {
        f.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto n = static_cast<size_t>(f.gcount());
        for(size_t i = 0U; i != n; ++i)
        {
            hash ^= static_cast<uint8_t>(buffer[i]);
            hash *= 1099511628211ULL;
        }
    }
    if(f.bad())
        throw std::runtime_error("Could not read " + generic_utf8(path));

    return fmt::format("{:012x}", hash >> 16);
}
