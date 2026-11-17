/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cmath>
#include <exception>

#include <imgui.h>

#include "../model/new_project.h"
#include "../model/paths.h"
#include "project_settings_view.h"

namespace fs = std::filesystem;
using purpose = file_dialogs::purpose;

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
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}

void hint(const char *text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

/// Seconds, edited as milliseconds, which is the scale these are chosen at
void input_ms(const char *label, double &seconds, const char *tooltip)
{
    double ms = seconds * 1000.;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
    if(ImGui::InputDouble(label, &ms, 5., 50., "%.0f ms"))
        seconds = std::max(0., ms) / 1000.;
    help(tooltip);
}

float browse_width()
{
    return ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.f;
}
}

void project_settings_view::load(const project_model &project)
{
    m_original = project.settings();
    m_roots = m_original.m_dataset_roots;
    m_palette = m_original.m_palette;
    m_export_prefix = m_original.m_export_prefix;
    m_mix = !m_original.m_channel;
    m_channel = static_cast<int>(m_original.m_channel.value_or(0U)) + 1;
    m_error.clear();
    try
    {
        m_review = review_settings::from_json(m_original.m_review);
        // As it would be written, so that an unedited section doesn't count as a change
        m_original.m_review = m_review.to_json();
    }
    catch(const std::exception &e)
    {
        m_review = {};
        m_error = std::string{"The project's review section has an error: "} + e.what();
    }
    try
    {
        m_high_pass = high_pass_settings::from_json(m_original.m_high_pass);
        m_original.m_high_pass = m_high_pass.to_json();
    }
    catch(const std::exception &e)
    {
        m_high_pass = {};
        m_error = std::string{"The project's high-pass section has an error: "} + e.what();
    }
    m_browsing_root.reset();
}

project_settings project_settings_view::edited() const
{
    project_settings s;
    for(const auto &root : m_roots)
        if(!root.empty())
            s.m_dataset_roots.push_back(root);
    s.m_palette = m_palette;
    s.m_export_prefix = m_export_prefix;
    if(!m_mix)
        s.m_channel = static_cast<uint32_t>(std::max(1, m_channel) - 1);
    s.m_review = m_review.to_json();
    s.m_high_pass = m_high_pass.to_json();
    return s;
}

void project_settings_view::take_dialog_answers(const project_model &project)
{
    const auto dir = project.config().m_filename.parent_path();
    while(auto a = m_dialogs.take(purpose::settings_root))
    {
        if(!a->m_error.empty())
            m_error = "No file dialog could be shown (" + a->m_error + "). Type the path instead.";
        else if(a->m_path && m_browsing_root)
        {
            const auto text = path_for_project(*a->m_path, dir);
            if(*m_browsing_root < m_roots.size())
                m_roots[*m_browsing_root] = text;
            else
                m_roots.push_back(text);
        }
        m_browsing_root.reset();
    }
    while(auto a = m_dialogs.take(purpose::settings_palette))
    {
        if(!a->m_error.empty())
            m_error = "No file dialog could be shown (" + a->m_error + "). Type the path instead.";
        else if(a->m_path)
            m_palette = path_for_project(*a->m_path, dir);
    }
}

void project_settings_view::render(const project_model &project, app_state &state)
{
    if(!state.m_show_project_settings || !project.has_project())
    {
        m_open = false;
        return;
    }
    if(!m_open)
    {
        load(project);
        m_open = true;
    }
    take_dialog_answers(project);

    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 36.f, 0.f}, ImGuiCond_FirstUseEver);
    if(!ImGui::Begin(m_window_name.c_str(), &state.m_show_project_settings))
    {
        ImGui::End();
        return;
    }

    const auto &cfg = project.config();
    const auto dir = cfg.m_filename.parent_path();
    const bool can_browse = !m_dialogs.is_open();
    const float browse = browse_width();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    hint(("Saved to " +
          generic_utf8(cfg.m_filename) +
          ". Relative paths are relative to its "
          "folder, and ${ENV_VARS} work.")
             .c_str());

    // Audio
    ImGui::SeparatorText("Audio folders");
    std::optional<size_t> remove;
    for(size_t i = 0U; i != m_roots.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        const float remove_w = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(-(browse + remove_w + 2.f * spacing));
        input_text("##Root", m_roots[i]);
        ImGui::SameLine();
        ImGui::BeginDisabled(!can_browse);
        if(ImGui::Button("Browse..."))
        {
            m_browsing_root = i;
            m_dialogs.open_folder(
                purpose::settings_root,
                cfg.resolve_root(path_from_utf8(m_roots[i])));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_roots.size() == 1U);
        if(ImGui::Button("x", ImVec2{remove_w, 0.f}))
            remove = i;
        help(
            "Remove this folder. Its labels are kept in the label file, and come back if it's "
            "added again.");
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if(remove)
        m_roots.erase(m_roots.begin() + static_cast<std::ptrdiff_t>(*remove));
    ImGui::BeginDisabled(!can_browse);
    if(ImGui::Button("Add folder..."))
    {
        m_browsing_root = m_roots.size();
        m_dialogs.open_folder(purpose::settings_root, dir);
    }
    ImGui::EndDisabled();
    hint(
        "Each is searched for audio in all its subfolders, and only read. Labels refer to files "
        "by folder as written here; if just one folder changes, its labels follow it.");

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Default channel");
    ImGui::SameLine();
    if(ImGui::RadioButton("Mix of all", m_mix))
        m_mix = true;
    ImGui::SameLine();
    if(ImGui::RadioButton("Channel", !m_mix))
        m_mix = false;
    if(!m_mix)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
        ImGui::InputInt("##Channel", &m_channel);
        m_channel = std::clamp(m_channel, 1, 64);
    }
    hint(
        "Shown and played for files of more than one channel. A file given its own channel in "
        "Options keeps it.");

    ImGui::Spacing();
    ImGui::Checkbox("High-pass filter", &m_high_pass.m_enabled);
    help("For wind and handling noise in field recordings");
    ImGui::BeginDisabled(!m_high_pass.m_enabled);
    ImGui::SameLine();
    float cutoff = static_cast<float>(m_high_pass.m_cutoff_hz);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
    if(ImGui::SliderFloat("Cutoff", &cutoff, 10.f, 2000.f, "%.0f Hz", ImGuiSliderFlags_Logarithmic))
        m_high_pass.m_cutoff_hz = std::round(cutoff);
    ImGui::SameLine();
    const char *orders[] = {"Gentle", "Medium", "Steep", "Steepest"};
    int order = std::clamp(m_high_pass.m_order / 2 - 1, 0, 3);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.f);
    if(ImGui::Combo("Slope", &order, orders, IM_ARRAYSIZE(orders)))
        m_high_pass.m_order = (order + 1) * 2;
    help(
        "Butterworth order 2, 4, 6 or 8, applied forwards and backwards so that sounds don't "
        "move in time: 24, 48, 72 or 96 dB per octave below the cutoff, -6 dB at it");
    ImGui::EndDisabled();
    hint(
        "Applied to the waveform, spectrogram, playback and the built-in loudness labeller, but "
        "not to exported clips or the files given to external labellers. Options can turn it off "
        "for a session.");

    // Classes
    ImGui::SeparatorText("Classes");
    ImGui::SetNextItemWidth(-(browse + spacing));
    input_text("##Palette", m_palette);
    ImGui::SameLine();
    ImGui::BeginDisabled(!can_browse);
    if(ImGui::Button("Browse...##Palette"))
        m_dialogs.open_json(purpose::settings_palette, cfg.resolve_proj(path_from_utf8(m_palette)));
    ImGui::EndDisabled();
    hint(
        "The palette file. Edit it to rename classes, change colours or add classes, then "
        "reopen the project.");

    // Review
    ImGui::SeparatorText("Playback and review");
    input_ms("Before an onset", m_review.m_onset_pre_s, "Played before an onset");
    ImGui::SameLine();
    input_ms("After an onset", m_review.m_onset_post_s, "Played after an onset");
    input_ms("Pre-roll", m_review.m_pre_roll_s, "Played before a region");
    ImGui::SameLine();
    input_ms("Snap window", m_review.m_snap_window_s, "How far snapping may move an onset");
    input_ms("Shown before", m_review.m_view_pre_s, "Shown before a suggestion in review mode");
    ImGui::SameLine();
    input_ms(
        "Shown after",
        m_review.m_view_post_s,
        "Shown after a suggestion's start in review mode");
    ImGui::Checkbox("Review lowest scores first", &m_review.m_by_score);
    help("If the suggestions have scores. Otherwise in file order.");

    // Export
    ImGui::SeparatorText("Export");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.f);
    input_text("CSV prefix", m_export_prefix);
    help("Export CSV writes <prefix>.csv: a path without the extension");
    ImGui::SameLine();
    if(ImGui::Button("Clip export..."))
        state.m_show_export_settings = true;

    ImGui::Spacing();
    hint(("Labels are saved to " +
          generic_utf8(cfg.resolve_proj(cfg.m_labels)) +
          ". Annotation trees, assisted labellers and clip manifests are set in the project file.")
             .c_str());

    if(!m_error.empty())
        ImGui::TextColored(ImVec4{1.f, 0.45f, 0.45f, 1.f}, "%s", m_error.c_str());

    // Save
    const auto s = edited();
    const bool changed = s != m_original;
    const bool reopens =
        s.m_dataset_roots != m_original.m_dataset_roots || s.m_palette != m_original.m_palette;
    ImGui::Separator();
    ImGui::BeginDisabled(!changed || state.is_exporting() || state.is_assisting());
    if(ImGui::Button(reopens ? "Save and reopen" : "Save"))
    {
        if(s.m_dataset_roots.empty())
            m_error = "Add at least one folder of audio";
        else if(s.m_export_prefix.empty())
            m_error = "The CSV prefix is empty";
        else if(!fs::is_regular_file(cfg.resolve_proj(path_from_utf8(s.m_palette))))
            m_error = "The palette " +
                generic_utf8(cfg.resolve_proj(path_from_utf8(s.m_palette))) +
                " doesn't exist";
        else
        {
            state.add_action(actions::set_project_settings{s});
            state.m_show_project_settings = false;
        }
    }
    ImGui::EndDisabled();
    if(reopens)
        help(
            "New folders or a new palette are read by opening the project again, which closes the "
            "open file. Labels are saved first.");
    ImGui::SameLine();
    if(ImGui::Button("Cancel"))
        state.m_show_project_settings = false;
    ImGui::SameLine();
    if(ImGui::Button("Show project file"))
        state.add_action(actions::reveal_file{cfg.m_filename});

    ImGui::End();
}
