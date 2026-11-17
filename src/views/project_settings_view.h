/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <optional>
#include <string>

#include "../app/app_state.h"
#include "../app/file_dialogs.h"
#include "../dsp/high_pass.h"
#include "../model/project_model.h"
#include "../review/review.h"

/// Edits the common settings of the project file: its audio folders, palette, default channel,
/// high-pass filter, review timings and export prefix. Shown after making a project, and from the File menu.
class project_settings_view
{
public:
    explicit project_settings_view(file_dialogs &dialogs) :
        m_dialogs(dialogs)
    {
    }

    void render(const project_model &project, app_state &state);

    /// Forget the copy being edited, eg. when another project opens
    void close() { m_open = false; }

    std::string m_window_name{"Project Settings"};

private:
    file_dialogs &m_dialogs;

    bool m_open{false}; ///< Whether the fields below hold the settings being edited
    project_settings m_original;
    std::vector<std::string> m_roots;
    std::string m_palette;
    std::string m_export_prefix;
    bool m_mix{true};
    int m_channel{1}; ///< Numbered from 1, as shown
    review_settings m_review;
    high_pass_settings m_high_pass;
    std::string m_error;
    std::optional<size_t> m_browsing_root; ///< The row a folder dialog is for, size() for a new one

    void load(const project_model &project);
    void take_dialog_answers(const project_model &project);
    project_settings edited() const;
};
