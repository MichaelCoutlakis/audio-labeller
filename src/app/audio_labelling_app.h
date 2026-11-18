/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include "../imgui_app/imgui_app.h"

#include <chrono>

#include <imgui.h>

#include "../audio/audio_engine.h"
#include "../views/assist_view.h"
#include "../views/audio_view.h"
#include "../views/clip_manifest_view.h"
#include "../views/export_settings_view.h"
#include "../views/file_list_view.h"
#include "../views/label_controls_view.h"
#include "../views/labels_view.h"
#include "../views/paste_dialog_view.h"
#include "../views/project_settings_view.h"
#include "../views/project_view.h"
#include "../views/shortcuts.h"
#include "../views/waveform_view.h"

#include "action_handler.h"

class audio_labeller_app : public imgui_app
{
public:
    using imgui_app::imgui_app;
    ~audio_labeller_app();

private:
    app_state m_app_state;
    project_model m_project = open_project(m_app_state);

    audio_view m_audio_control;
    file_list_view m_file_list;
    waveform_view m_waveform;
    label_control_view m_label_controls;
    labels_view m_applied_labels;
    keyboard_controller m_keyboard;
    export_settings_view m_export_settings;
    clip_manifest_view m_clip_manifest;
    assist_view m_assist;
    file_dialogs m_dialogs;
    project_view m_project_view{m_dialogs};
    paste_dialog_view m_paste_dialog;
    project_settings_view m_project_settings{m_dialogs};

    audio_engine m_audio_engine;

    action_handler m_handler{m_audio_engine, m_project, m_app_state};

    std::chrono::steady_clock::time_point m_last_autosave{std::chrono::steady_clock::now()};
    bool m_autosave_failed{false}; ///< To report a persistent failure once rather than every time

    /// Opens the project from the settings. On failure, reports the error and returns an empty
    /// model so that the app still starts and can't overwrite whatever it failed to read.
    static project_model open_project(app_state &state);

    /// Read the project's sections that the app keeps in its state (review, annotation trees,
    /// assisted labellers). A mistake in one is reported rather than stopping labelling.
    void load_project_sections();

    /// Close the project, saving its labels, and open another. If either fails, the current
    /// project stays open and the error is reported.
    void switch_project(const std::filesystem::path &file);

    void autosave();

    void render_error_popup();

    void on_init() override;

    void on_frame() override;

    void on_shutdown() override;

    void on_menu_bar() override;

    void render_main_dockspace();
};