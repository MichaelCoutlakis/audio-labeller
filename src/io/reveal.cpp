/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <exception>
#include <thread>

#include <spdlog/spdlog.h>

#include "../model/paths.h"
#include "reveal.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // First, as the others need it

#include <objbase.h>
#include <shlobj.h>
#else
#include "process.h"
#endif

namespace fs = std::filesystem;

std::string file_uri(const fs::path &absolute)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out = "file://";
    auto path = generic_utf8(absolute);
    if(!path.starts_with('/'))
        out += '/'; // C:/x -> file:///C:/x
    for(const char ch : path)
    {
        const auto c = static_cast<unsigned char>(ch);
        const bool keep = (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '.' ||
            c == '_' ||
            c == '~' ||
            c == '/' ||
            c == ':';
        if(keep)
            out += ch;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xFU];
        }
    }
    return out;
}

namespace
{
#ifdef _WIN32
void reveal_blocking(const fs::path &file)
{
    // explorer /select, is fussy about quoting, and its exit code says nothing about success
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(file.c_str());
    HRESULT hr = E_FAIL;
    if(item)
    {
        hr = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
        ILFree(item);
    }
    if(SUCCEEDED(init))
        CoUninitialize();
    if(FAILED(hr))
        spdlog::warn("Could not show {} in Explorer (error {:#x})", generic_utf8(file), hr);
}
#else
bool run_quietly(const std::vector<std::string> &args, double timeout_s)
{
    try
    {
        return run_process(args, {}, "/dev/null", "/dev/null", nullptr, timeout_s).ok();
    }
    catch(const std::exception &e)
    {
        spdlog::debug("{}", e.what());
        return false;
    }
}

void reveal_blocking(const fs::path &file)
{
#ifdef __APPLE__
    if(run_quietly({"open", "-R", native_utf8(file)}, 0.))
        return;
#else
    // Supported by Nautilus, Dolphin, Nemo, Caja and Thunar. --print-reply waits for the answer,
    // so a missing service is a failure rather than silence.
    if(run_quietly(
           {"dbus-send",
            "--session",
            "--print-reply",
            "--dest=org.freedesktop.FileManager1",
            "--type=method_call",
            "/org/freedesktop/FileManager1",
            "org.freedesktop.FileManager1.ShowItems",
            "array:string:" + file_uri(file),
            "string:"},
           10.))
        return;
    // The folder, without the file selected
    if(run_quietly({"xdg-open", native_utf8(file.parent_path())}, 0.))
        return;
#endif
    spdlog::warn("Could not show {} in a file manager", generic_utf8(file));
}
#endif
}

void reveal_in_file_manager(const fs::path &file)
{
    const auto absolute = fs::absolute(file).lexically_normal();
    spdlog::info("Showing {} in the file manager", generic_utf8(absolute));
    // Off the UI thread, as a file manager may take a while to answer, or not return at all
    std::thread([absolute] { reveal_blocking(absolute); }).detach();
}
