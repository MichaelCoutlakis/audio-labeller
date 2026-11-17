/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <string>

#include "../app/app_state.h"
#include "../app/file_dialogs.h"
#include "../model/project_model.h"

/// The File menu, and its dialogs to open a project or make one from a folder of audio
class project_view
{
public:
    explicit project_view(file_dialogs &dialogs) :
        m_dialogs(dialogs)
    {
    }

    /// Items of the File menu, inside BeginMenu
    void render_menu_items(const project_model &project, app_state &state);

    /// The dialogs, outside any window
    void render(const project_model &project, app_state &state);

private:
    file_dialogs &m_dialogs; ///< Shared with the project settings

    void take_dialog_answers(const project_model &project, app_state &state);
    void render_open_project(app_state &state);
    void render_new_project(app_state &state);

    // Open project, typed, if no system dialog could be shown:
    bool m_show_open_fallback{false};
    std::string m_open_path;
    std::string m_open_error;

    // New project:
    std::string m_data_root;
    std::string m_project_file;
    bool m_existing_palette{false};
    std::string m_palette;
    std::string m_classes;
    std::string m_new_error;
};
