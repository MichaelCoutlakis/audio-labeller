/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <chrono>
#include <stdexcept>
#include <thread>

#include "../model/paths.h"
#include "process.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using clock_type = std::chrono::steady_clock;

namespace
{
bool expired(clock_type::time_point start, double timeout_s)
{
    return timeout_s > 0. &&
        std::chrono::duration<double>(clock_type::now() - start).count() > timeout_s;
}
}

#ifdef _WIN32
namespace
{
std::wstring widen(const std::string &s)
{
    if(s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

/// Quote an argument so that CommandLineToArgvW (and the C runtime) reads it back unchanged
std::wstring quote(const std::wstring &arg)
{
    if(!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0U;
    for(const wchar_t c : arg)
    {
        if(c == L'\\')
        {
            ++backslashes;
            continue;
        }
        if(c == L'"')
            out.append(backslashes * 2U + 1U, L'\\');
        else
            out.append(backslashes, L'\\');
        backslashes = 0U;
        out += c;
    }
    out.append(backslashes * 2U, L'\\');
    out += L'"';
    return out;
}

HANDLE open_output(const fs::path &p)
{
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE h = CreateFileW(
        p.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if(h == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Could not create " + generic_utf8(p));
    return h;
}
}

process_result run_process(
    const std::vector<std::string> &args,
    const fs::path &working_dir,
    const fs::path &stdout_file,
    const fs::path &stderr_file,
    const std::atomic<bool> *cancel,
    double timeout_s)
{
    if(args.empty())
        throw std::invalid_argument("No program to run");

    // cmd.exe reads what follows /c by its own rules rather than the C runtime's, so a shell
    // command line is passed as it is, in the quotes that /s strips
    std::wstring command_line;
    bool verbatim = false;
    for(const auto &arg : args)
    {
        const auto wide = widen(arg);
        command_line += (command_line.empty() ? L"" : L" ") + (verbatim ? L"\"" + wide + L"\"" : quote(wide));
        verbatim = args.front() == "cmd.exe" && (arg == "/c" || arg == "/C");
    }

    HANDLE out = open_output(stdout_file);
    HANDLE err = stderr_file == stdout_file ? out : open_output(stderr_file);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = out;
    si.hStdError = err;
    PROCESS_INFORMATION pi{};
    const auto dir = working_dir.empty() ? std::wstring{} : working_dir.wstring();
    const BOOL started = CreateProcessW(
        nullptr,
        command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        dir.empty() ? nullptr : dir.c_str(),
        &si,
        &pi);
    const DWORD start_error = GetLastError();
    CloseHandle(in);
    if(err != out)
        CloseHandle(err);
    CloseHandle(out);
    if(!started)
        throw std::runtime_error(
            "Could not run " + args.front() + " (error " + std::to_string(start_error) + ")");
    CloseHandle(pi.hThread);

    process_result result;
    const auto start = clock_type::now();
    while(WaitForSingleObject(pi.hProcess, 50) == WAIT_TIMEOUT)
    {
        result.m_cancelled = cancel && *cancel;
        result.m_timed_out = expired(start, timeout_s);
        if(result.m_cancelled || result.m_timed_out)
        {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, INFINITE);
            break;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    result.m_exit_code = static_cast<int>(code);
    return result;
}

std::vector<std::string> shell_command(const std::string &command_line)
{
    return {"cmd.exe", "/d", "/s", "/c", command_line};
}

#else

process_result run_process(
    const std::vector<std::string> &args,
    const fs::path &working_dir,
    const fs::path &stdout_file,
    const fs::path &stderr_file,
    const std::atomic<bool> *cancel,
    double timeout_s)
{
    if(args.empty())
        throw std::invalid_argument("No program to run");

    std::vector<char *> argv;
    for(const auto &a : args)
        argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);

    // Reports a failure to start through a pipe, closed on a successful exec
    int report[2];
    if(pipe(report) != 0)
        throw std::runtime_error("Could not create a pipe");
    fcntl(report[1], F_SETFD, FD_CLOEXEC);

    const pid_t pid = fork();
    if(pid < 0)
        throw std::runtime_error("Could not start " + args.front());
    if(pid == 0)
    {
        close(report[0]);
        if(!working_dir.empty() && chdir(working_dir.c_str()) != 0)
            _exit(126);
        const int out = open(stdout_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        const int err = stderr_file == stdout_file
            ? out
            : open(stderr_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        const int in = open("/dev/null", O_RDONLY);
        if(out < 0 || err < 0 || in < 0)
            _exit(126);
        dup2(in, 0);
        dup2(out, 1);
        dup2(err, 2);
        execvp(argv[0], argv.data());
        const int e = errno;
        [[maybe_unused]] auto n = write(report[1], &e, sizeof(e));
        _exit(127);
    }
    close(report[1]);
    int exec_error = 0;
    const bool failed = read(report[0], &exec_error, sizeof(exec_error)) == sizeof(exec_error);
    close(report[0]);
    if(failed)
    {
        waitpid(pid, nullptr, 0);
        throw std::runtime_error("Could not run " + args.front() + ": " + std::strerror(exec_error));
    }

    process_result result;
    const auto start = clock_type::now();
    int status = 0;
    while(true)
    {
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if(done == pid)
            break;
        result.m_cancelled = cancel && *cancel;
        result.m_timed_out = expired(start, timeout_s);
        if(result.m_cancelled || result.m_timed_out)
        {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    result.m_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

std::vector<std::string> shell_command(const std::string &command_line)
{
    return {"/bin/sh", "-c", command_line};
}

#endif
