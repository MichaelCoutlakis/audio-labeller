/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <filesystem>
#include <string>

/// A file:// URI for an absolute path, percent-encoding everything but unreserved characters and
/// the separators. Commas are encoded too, as dbus-send splits arrays on them.
std::string file_uri(const std::filesystem::path &absolute);

/// Show a file in the system's file manager, selected if the file manager supports it: Explorer
/// on Windows, Finder on macOS, and elsewhere the freedesktop FileManager1 D-Bus service, else
/// the containing folder opened with xdg-open. Returns at once; a failure is only logged.
void reveal_in_file_manager(const std::filesystem::path &file);
