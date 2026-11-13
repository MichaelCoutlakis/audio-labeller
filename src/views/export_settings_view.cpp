/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>

#include <imgui.h>

#include "../model/paths.h"
#include "export_settings_view.h"

namespace
{
/// A text box for a std::string, growing it as needed
bool input_text(const char *label, std::string &s)
{
    return ImGui::InputText(
        label,
        s.data(),
        s.capacity() + 1U,
        ImGuiInputTextFlags_CallbackResize,
        [](ImGuiInputTextCallbackData *data)
        {
            if(data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                auto *str = static_cast<std::string *>(data->UserData);
                str->resize(static_cast<size_t>(data->BufTextLen));
                data->Buf = str->data();
            }
            return 0;
        },
        &s);
}

void help(const char *text)
{
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}
}

void export_settings_view::render(const project_model &project, app_state &state)
{
    if(!state.m_show_export_settings)
    {
        m_open = false;
        return;
    }
    if(!m_open)
    {
        try
        {
            m_edit = export_settings::from_json(project.config().m_export);
            m_error.clear();
        }
        catch(const std::exception &e)
        {
            m_edit = export_settings{};
            m_error = std::string{"The project's export section has an error: "} + e.what();
        }
        m_directory = generic_utf8(m_edit.m_directory);
        m_open = true;
    }

    ImGui::SetNextWindowSize(ImVec2{460.f, 0.f}, ImGuiCond_FirstUseEver);
    if(!ImGui::Begin(m_window_name.c_str(), &state.m_show_export_settings))
    {
        ImGui::End();
        return;
    }
    if(!project.has_project())
    {
        ImGui::TextUnformatted("No project loaded.");
        ImGui::End();
        return;
    }

    input_text("Directory", m_directory);
    help("Where the dataset is written, relative to the project file. ${ENV_VARS} work.");

    int mode = static_cast<int>(m_edit.m_mode);
    ImGui::RadioButton("Padded", &mode, static_cast<int>(export_settings::clip_mode::padded));
    help("Each label plus padding, merging labels whose clips would overlap");
    ImGui::SameLine();
    ImGui::RadioButton("Fixed length", &mode, static_cast<int>(export_settings::clip_mode::fixed));
    help("A window of fixed length centred on each label");
    m_edit.m_mode = static_cast<export_settings::clip_mode>(mode);

    if(m_edit.m_mode == export_settings::clip_mode::padded)
    {
        ImGui::InputDouble("Padding (s)", &m_edit.m_padding_s, 0.1, 0.5, "%.3f");
        ImGui::InputDouble("Longest merged clip (s)", &m_edit.m_max_clip_s, 1., 5., "%.1f");
    }
    else
        ImGui::InputDouble("Clip length (s)", &m_edit.m_fixed_length_s, 0.5, 1., "%.3f");

    int background = static_cast<int>(m_edit.m_background_per_file);
    ImGui::InputInt("Background clips per file", &background);
    help("Clips with no labels, from files marked done");
    m_edit.m_background_per_file = static_cast<unsigned>(std::max(0, background));
    if(m_edit.m_background_per_file)
        ImGui::InputDouble("Background length (s)", &m_edit.m_background_length_s, 0.5, 1., "%.2f");

    m_edit.m_padding_s = std::max(0., m_edit.m_padding_s);
    m_edit.m_max_clip_s = std::max(0.1, m_edit.m_max_clip_s);
    m_edit.m_fixed_length_s = std::max(0.01, m_edit.m_fixed_length_s);
    m_edit.m_background_length_s = std::max(0.01, m_edit.m_background_length_s);

    if(!m_error.empty())
        ImGui::TextColored(ImVec4{1.f, 0.75f, 0.3f, 1.f}, "%s", m_error.c_str());

    ImGui::BeginDisabled(state.is_exporting());
    if(ImGui::Button("Save to project"))
    {
        m_edit.m_directory = path_from_utf8(m_directory);
        auto section = m_edit.to_json();
        section["directory"] = m_directory;
        state.add_action(actions::set_export_settings{section});
        state.m_show_export_settings = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Cancel"))
        state.m_show_export_settings = false;

    ImGui::End();
}
