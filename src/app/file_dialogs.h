/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

/// The system's file dialogs, through SDL. They're asynchronous and may answer on another
/// thread, so answers wait here until the UI takes them on the main thread.
class file_dialogs
{
public:
    /// What a dialog was opened for, so that its answer goes to the right place
    enum class purpose
    {
        open_project,
        data_root,
        project_file,
        palette,
        settings_root, ///< A dataset root in the project settings
        settings_palette
    };

    struct answer
    {
        purpose m_purpose;
        std::optional<std::filesystem::path> m_path; ///< Empty if cancelled
        std::string m_error; ///< Set if no dialog could be shown, eg. no portal or zenity on Linux
    };

    void set_window(SDL_Window *window) { m_window = window; }

    /// Choose an existing JSON file
    void open_json(purpose p, const std::filesystem::path &start = {});
    void save_json(purpose p, const std::filesystem::path &start = {});
    void open_folder(purpose p, const std::filesystem::path &start = {});

    /// Whether a dialog is showing, so that another isn't opened over it
    bool is_open() const;

    /// The answer of a dialog for this purpose that has closed, once
    std::optional<answer> take(purpose p);

private:
    struct mailbox
    {
        mutable std::mutex m_mutex;
        bool m_open{false};
        std::vector<answer> m_answers;
    };
    struct request
    {
        std::shared_ptr<mailbox> m_mailbox;
        purpose m_purpose;
    };
    static void callback(void *userdata, const char *const *files, int filter);
    request *begin(purpose p);

    SDL_Window *m_window{nullptr};
    std::shared_ptr<mailbox> m_mailbox{std::make_shared<mailbox>()};
    std::string m_start; ///< Kept until the dialog has read it
};
