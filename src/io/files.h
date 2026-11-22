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

/// 16 hex digits of a 64-bit FNV-1a hash, to tell whether a file's contents changed
std::string content_hash(const std::string &contents);

/// A file's contents, empty if it can't be read
std::string read_whole_file(const std::filesystem::path &path);

/// The current time in UTC, ISO 8601, to the second
std::string utc_now_iso8601();
