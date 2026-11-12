/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <filesystem>
#include <string>

/// A short ID for an audio file, from its contents: 12 hex digits of a 64-bit FNV-1a hash. Stable
/// across renames and moves, so it names exported clips and links them back to their source.
std::string compute_source_id(const std::filesystem::path &path);
