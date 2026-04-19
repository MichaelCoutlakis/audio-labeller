/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>

#include <imgui_internal.h> // docking

#include "app_state.h"
#include "audio_labelling_app.h"

audio_labeller_app::~audio_labeller_app() { m_app_state.settings.save(); }

void audio_labeller_app::on_init()
{
    m_app_state.settings = app_settings::load();
    m_app_state.m_audio_devs = m_audio_engine.get_playback_devices(&m_app_state.audio_default_dev);

    if(!m_app_state.m_audio_devs.empty())
    {
        // Restore or select default audio device
        if(!m_app_state.settings.dev ||
           !std::ranges::contains(m_app_state.m_audio_devs, m_app_state.settings.dev.value()))
        {
            m_app_state.settings.dev = m_app_state.audio_default_dev;
        }
        actions::select_playback_device a{m_app_state.settings.dev.value()};
        m_app_state.m_actions.push_back(a);
    }
}

void audio_labeller_app::on_frame()
{
    render_main_dockspace();

    m_app_state.m_playback_state = m_audio_engine.get_state();

    m_audio_control.render(m_project, m_app_state);
    m_file_list.render(m_project, m_app_state);

    m_waveform.render(m_project, m_app_state);
    m_applied_labels.render(m_project, m_app_state);
    m_label_controls.render(m_project, m_app_state);

    // Dispatch the actions (controller):
    for(auto &action : m_app_state.m_actions)
        std::visit(m_handler, action);
    m_app_state.m_actions.clear();
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
