/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <string>

#include "../app/app_state.h"
#include "../export/clip_export.h"
#include "../model/project_model.h"

/// Edits the project's "export" section, rather than editing the project file by hand
class export_settings_view
{
public:
    void render(const project_model &project, app_state &state);

    std::string m_window_name{"Export Settings"};

private:
    bool m_open{false};        ///< Whether m_edit holds the settings being edited
    export_settings m_edit;    ///< A copy, saved on request
    std::string m_directory;   ///< UTF-8, for the text box
    std::string m_error;
};
