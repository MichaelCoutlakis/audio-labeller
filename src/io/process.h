/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

struct process_result
{
    int m_exit_code{-1};
    bool m_timed_out{};
    bool m_cancelled{};

    bool ok() const { return m_exit_code == 0 && !m_timed_out && !m_cancelled; }
};

/// Run a program and wait for it, without a shell, so arguments need no quoting. The first
/// argument is the program, looked up on the PATH. Arguments and paths are UTF-8. Standard output
/// and error go to the given files (which may be the same). Polls cancel, and kills the process if
/// it's set or the timeout (0 for none) passes. Throws if the program can't be started.
process_result run_process(
    const std::vector<std::string> &args,
    const std::filesystem::path &working_dir,
    const std::filesystem::path &stdout_file,
    const std::filesystem::path &stderr_file,
    const std::atomic<bool> *cancel = nullptr,
    double timeout_s = 0.);

/// How a command line string is run: through cmd.exe on Windows (passed to it verbatim), /bin/sh
/// elsewhere
std::vector<std::string> shell_command(const std::string &command_line);
