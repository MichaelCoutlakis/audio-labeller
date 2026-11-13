/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <exception>

#include <imgui_internal.h> // docking
#include <spdlog/spdlog.h>

#include "app_state.h"
#include "audio_labelling_app.h"

static constexpr auto autosave_interval = std::chrono::seconds(5);

audio_labeller_app::~audio_labeller_app()
{
    try
    {
        m_app_state.settings.save();
    }
    catch(const std::exception &e)
    {
        spdlog::error("Could not save settings: {}", e.what());
    }
}

project_model audio_labeller_app::open_project(app_state &state)
{
    if(!state.settings.m_load_error.empty())
        state.report_error(state.settings.m_load_error);

    try
    {
        return project_model(project_config::parse_from_file(state.settings.project_file));
    }
    catch(const std::exception &e)
    {
        state.report_error(
            "Could not open project " + state.settings.project_file + ":\n" + e.what());
        return project_model(project_config{});
    }
}

void audio_labeller_app::on_init()
{
    // Project sections read by the app. A mistake in one shouldn't stop labelling.
    try
    {
        m_app_state.m_review = review_settings::from_json(m_project.config().m_review);
        m_app_state.m_annotation_trees =
            annotation_tree_settings::list_from_json(m_project.config().m_annotations);
    }
    catch(const std::exception &e)
    {
        m_app_state.report_error(std::string{"Error in the project file: "} + e.what());
    }

    m_app_state.m_audio_devs = m_audio_engine.get_playback_devices();

    // Restore the previous device by name, or use the system default (always first):
    const auto &devs = m_app_state.m_audio_devs;
    auto it = std::ranges::find(devs, m_app_state.settings.m_device_name, &audio_dev::name);
    m_app_state.add_action(actions::select_playback_device{it != devs.end() ? *it : devs.front()});

    // Written up front so that on a first run there's a file for the user to point at a project
    m_app_state.settings.save();
}

void audio_labeller_app::on_frame()
{
    render_main_dockspace();

    m_app_state.m_playback_state = m_audio_engine.get_state();

    m_keyboard.handle(m_project, m_app_state);

    m_audio_control.render(m_project, m_app_state);
    m_file_list.render(m_project, m_app_state);

    m_waveform.render(m_project, m_app_state);
    m_applied_labels.render(m_project, m_app_state);
    m_label_controls.render(m_project, m_app_state);
    m_export_settings.render(m_project, m_app_state);
    m_clip_manifest.render(m_project, m_app_state);

    // Dispatch the actions (controller):
    for(auto &action : m_app_state.m_actions)
    {
        // A bad audio file or device shouldn't take the session down with it
        try
        {
            std::visit(m_handler, action);
        }
        catch(const std::exception &e)
        {
            m_app_state.report_error(e.what());
        }
    }
    m_app_state.m_actions.clear();

    try
    {
        m_handler.poll_background();
    }
    catch(const std::exception &e)
    {
        m_app_state.report_error(e.what());
    }

    autosave();

    render_error_popup();
}

void audio_labeller_app::on_shutdown()
{
    // An export in progress is stopped, so that it doesn't outlive the project it reports to
    try
    {
        m_handler.cancel_background();
    }
    catch(const std::exception &e)
    {
        spdlog::error("Could not stop the export: {}", e.what());
    }
    m_waveform.release();
    m_audio_engine.close();
}

void audio_labeller_app::autosave()
{
    const auto now = std::chrono::steady_clock::now();
    if(now - m_last_autosave < autosave_interval)
        return;
    m_last_autosave = now;

    try
    {
        m_project.save_if_dirty();
        m_autosave_failed = false;
    }
    catch(const std::exception &e)
    {
        if(!m_autosave_failed)
            m_app_state.report_error(std::string{"Could not save labels: "} + e.what());
        m_autosave_failed = true;
    }
}

void audio_labeller_app::render_error_popup()
{
    // Errors take priority over information. Each is shown until acknowledged.
    auto popup = [](const char *name, std::string &message)
    {
        if(!message.empty() && !ImGui::IsPopupOpen(name))
            ImGui::OpenPopup(name);

        if(ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushTextWrapPos(600.f);
            ImGui::TextUnformatted(message.c_str());
            ImGui::PopTextWrapPos();

            if(ImGui::Button("OK"))
            {
                message.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    };

    popup("Error", m_app_state.m_error_message);
    if(m_app_state.m_error_message.empty())
        popup("Information", m_app_state.m_info_message);
}

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
        ImGuiID bottom_left_id{};

        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.2f, &left_id, &centre_id);
        ImGui::DockBuilderSplitNode(centre_id, ImGuiDir_Right, 0.2f, &right_id, &centre_id);
        ImGui::DockBuilderSplitNode(centre_id, ImGuiDir_Up, 0.7f, &centre_id, &bottom_centre_id);
        ImGui::DockBuilderSplitNode(left_id, ImGuiDir_Up, 0.65f, &left_id, &bottom_left_id);

        ImGui::DockBuilderDockWindow(m_file_list.m_window_name.c_str(), left_id);
        ImGui::DockBuilderDockWindow(m_waveform.m_window_name.c_str(), centre_id);
        ImGui::DockBuilderDockWindow(m_label_controls.m_window_name.c_str(), bottom_centre_id);
        ImGui::DockBuilderDockWindow(m_applied_labels.m_window_name.c_str(), right_id);
        ImGui::DockBuilderDockWindow(m_clip_manifest.m_window_name.c_str(), right_id);
        ImGui::DockBuilderDockWindow(m_audio_control.m_window_name.c_str(), bottom_left_id);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    // Submit dockspace
    ImGui::DockSpaceOverViewport(dockspace_id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    // Submit windows: (ImGui::Begin("WindowName")...)
}
