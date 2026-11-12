/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <filesystem>
#include <string>

/// Replace the target in one step, so that a failure part way through a write can never leave a
/// truncated file in place of the previous good one.
void write_file_atomically(const std::filesystem::path &target, const std::string &contents);

/// Quote a CSV field if needed
std::string csv_escape(const std::string &s);

/// The current time in UTC, ISO 8601, to the second
std::string utc_now_iso8601();
