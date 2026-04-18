/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include "../imgui_app/imgui_app.h"

#include <imgui.h>

#include "../views/file_list_view.h"
#include "../views/label_controls_view.h"
#include "../views/labels_view.h"
#include "../views/waveform_view.h"
#include "action_handler.h"



class audio_labeller_app : public imgui_app
{
public:
    using imgui_app::imgui_app;

private:
    project_model m_project{project_config{}};
    app_state m_app_state;

    file_list_view m_file_list;
    waveform_view m_waveform;
    label_control_view m_label_controls;
    labels_view m_applied_labels;
    action_handler m_handler{m_project, m_app_state};

    bool m_layout_initialised{false};

    void on_init() override;

    void on_frame() override;

    void on_shutdown() override;

    void render_main_dockspace();
};