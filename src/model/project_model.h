/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <optional>
#include <string>
#include <vector>

class project_model
{
    std::vector<std::string> m_filenames;
    std::optional<size_t> m_active_file;
};
