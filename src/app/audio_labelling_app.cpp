/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <imgui_internal.h> // docking

#include "audio_labelling_app.h"

void audio_labeller_app::on_init() { }

void audio_labeller_app::on_frame()
{
    render_main_dockspace();

    m_file_list.render(m_project, m_app_state);

    m_waveform.render(m_project);
    m_applied_labels.render(m_project);
    m_label_controls.render(m_project);
}

void audio_labeller_app::on_shutdown() { }

void audio_labeller_app::render_main_dockspace()
{
    ImGuiID dockspace_id = ImGui::GetID("My Dockspace");
    ImGuiViewport *viewport = ImGui::GetMainViewport();

    // Set up dockspace with default layout:
    if(ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
    {
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);
        ImGuiID left_id{};
        ImGuiID centre_id{};
        ImGuiID right_id{};
        ImGuiID bottom_centre_id{};

        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.2f, &left_id, &centre_id);
        ImGui::DockBuilderSplitNode(centre_id, ImGuiDir_Right, 0.2f, &right_id, &centre_id);
        ImGui::DockBuilderSplitNode(centre_id, ImGuiDir_Up, 0.5f, &centre_id, &bottom_centre_id);

        ImGui::DockBuilderDockWindow(m_file_list.m_window_name.c_str(), left_id);
        ImGui::DockBuilderDockWindow(m_waveform.m_window_name.c_str(), centre_id);
        ImGui::DockBuilderDockWindow(m_label_controls.m_window_name.c_str(), bottom_centre_id);
        ImGui::DockBuilderDockWindow(m_applied_labels.m_window_name.c_str(), right_id);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    // Submit dockspace
    ImGui::DockSpaceOverViewport(dockspace_id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    // Submit windows: (ImGui::Begin("WindowName")...)
}
