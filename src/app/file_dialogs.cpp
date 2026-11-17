/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

#include "../model/paths.h"
#include "file_dialogs.h"

namespace
{
// SDL needs the filters until the dialog closes
constexpr SDL_DialogFileFilter json_filters[] = {{"JSON files", "json"}, {"All files", "*"}};
}

void file_dialogs::callback(void *userdata, const char *const *files, int)
{
    const std::unique_ptr<request> req{static_cast<request *>(userdata)};
    answer a{req->m_purpose, std::nullopt, {}};
    if(!files)
        a.m_error = SDL_GetError();
    else if(*files)
        a.m_path = path_from_utf8(*files);

    std::lock_guard lock{req->m_mailbox->m_mutex};
    req->m_mailbox->m_open = false;
    req->m_mailbox->m_answers.push_back(std::move(a));
}

file_dialogs::request *file_dialogs::begin(purpose p)
{
    {
        std::lock_guard lock{m_mailbox->m_mutex};
        m_mailbox->m_open = true;
    }
    return new request{m_mailbox, p};
}

void file_dialogs::open_json(purpose p, const std::filesystem::path &start)
{
    m_start = generic_utf8(start);
    SDL_ShowOpenFileDialog(
        &callback,
        begin(p),
        m_window,
        json_filters,
        SDL_arraysize(json_filters),
        m_start.empty() ? nullptr : m_start.c_str(),
        false);
}

void file_dialogs::save_json(purpose p, const std::filesystem::path &start)
{
    m_start = generic_utf8(start);
    SDL_ShowSaveFileDialog(
        &callback,
        begin(p),
        m_window,
        json_filters,
        SDL_arraysize(json_filters),
        m_start.empty() ? nullptr : m_start.c_str());
}

void file_dialogs::open_folder(purpose p, const std::filesystem::path &start)
{
    m_start = generic_utf8(start);
    SDL_ShowOpenFolderDialog(
        &callback,
        begin(p),
        m_window,
        m_start.empty() ? nullptr : m_start.c_str(),
        false);
}

bool file_dialogs::is_open() const
{
    std::lock_guard lock{m_mailbox->m_mutex};
    return m_mailbox->m_open;
}

std::optional<file_dialogs::answer> file_dialogs::take(purpose p)
{
    std::lock_guard lock{m_mailbox->m_mutex};
    auto &answers = m_mailbox->m_answers;
    const auto it = std::ranges::find(answers, p, &answer::m_purpose);
    if(it == answers.end())
        return std::nullopt;
    auto a = std::move(*it);
    answers.erase(it);
    return a;
}
