/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include "../imgui_app/imgui_app.h"

#include <imgui.h>

#include "../views/file_list_view.h"
#include "../views/label_controls_view.h"
#include "../views/waveform_view.h"

class label_control_view
{
public:
    void render(project_model &project)
    {
        ImGui::Begin(m_window_name.c_str());
        ImGui::Button("This is the label controls view");
        ImGui::End();
    }

    std::string m_window_name{"Label Controls"};
};

class applied_labels_view
{
public:
    void render(project_model &project)
    {
        ImGui::Begin(m_window_name.c_str());
        ImGui::Button("This is the applied labels view");
        ImGui::End();
    }

    std::string m_window_name{"Applied Labels"};
};

class audio_labeller_app : public imgui_app
{
public:
    using imgui_app::imgui_app;

private:
    project_model m_project;

    file_list_view m_file_list;
    waveform_view m_waveform;
    label_control_view m_label_controls;
    applied_labels_view m_applied_labels;

    bool m_layout_initialised{false};

    void on_init() override;

    void on_frame() override;

    void on_shutdown() override;

    void render_main_dockspace();
};