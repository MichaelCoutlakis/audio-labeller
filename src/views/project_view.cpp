/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <exception>

#include <imgui.h>

#include "../model/new_project.h"
#include "../model/paths.h"
#include "project_view.h"

namespace fs = std::filesystem;
using purpose = file_dialogs::purpose;

namespace
{
constexpr const char *open_popup = "Open project";
constexpr const char *new_popup = "New project";

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

/// A path text box filling the line, less room for a Browse button after it
bool path_input(const char *label, std::string &s)
{
    const float button =
        ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.f;
    ImGui::SetNextItemWidth(-(button + ImGui::GetStyle().ItemSpacing.x));
    const bool changed = input_text(label, s);
    ImGui::SameLine();
    return changed;
}

void hint(const char *text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void error_text(const std::string &text)
{
    if(text.empty())
        return;
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 110, 110, 255));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

/// Where a dialog for a path in a text box starts: the path's folder, else the fallback's
fs::path start_for(const std::string &typed, const fs::path &fallback)
{
    if(!typed.empty())
        return path_from_utf8(typed);
    return fallback;
}
}

void project_view::render_menu_items(const project_model &project, app_state &state)
{
    if(ImGui::MenuItem("Open project...", nullptr, false, !m_dialogs.is_open()))
        state.m_browse_for_project = true;
    if(ImGui::MenuItem("New project from folder...", nullptr, false, !m_dialogs.is_open()))
        state.m_show_new_project = true;
    if(project.has_project())
    {
        ImGui::Separator();
        if(ImGui::MenuItem("Open review list...", nullptr, false, !m_dialogs.is_open()))
        {
            const auto &open = state.m_review_list;
            m_dialogs.open_csv(
                purpose::review_list,
                open ? open->m_path.parent_path() : project.config().m_filename.parent_path());
        }
        const auto &recent = state.m_settings.m_recent_lists;
        if(ImGui::BeginMenu("Recent review lists", !recent.empty()))
        {
            for(const auto &list : recent)
                if(ImGui::MenuItem(list.m_list.c_str()))
                    state.add_action(actions::open_review_list{path_from_utf8(list.m_list)});
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Import review edits...", nullptr, false, !m_dialogs.is_open()))
        {
            const auto &open = state.m_review_list;
            m_dialogs.open_csv(
                purpose::review_results,
                open ? open->m_path.parent_path() : project.config().m_filename.parent_path());
        }
        ImGui::Separator();
        if(ImGui::MenuItem("Project settings..."))
            state.m_show_project_settings = true;
        if(ImGui::MenuItem("Show project file in file manager"))
            state.add_action(actions::reveal_file{project.config().m_filename});
    }
}

void project_view::take_dialog_answers(const project_model &project, app_state &state)
{
    const auto purposes = {
        purpose::open_project,
        purpose::data_root,
        purpose::project_file,
        purpose::palette,
        purpose::review_list,
        purpose::review_results};
    for(const auto p : purposes)
    {
        while(auto a = m_dialogs.take(p))
        {
            // With no dialog available (eg. Linux without a portal or zenity), the path is typed
            if(!a->m_error.empty())
            {
                const auto message =
                    "No file dialog could be shown (" + a->m_error + "). Type the path instead.";
                if(a->m_purpose == purpose::review_list || a->m_purpose == purpose::review_results)
                    state.report_error(
                        "No file dialog could be shown (" + a->m_error + "). Name the list in the "
                        "project's \"clip-manifest\" section instead.");
                else if(a->m_purpose == purpose::open_project)
                {
                    m_show_open_fallback = true;
                    m_open_error = message;
                }
                else
                    m_new_error = message;
                continue;
            }
            if(!a->m_path)
                continue; // Cancelled

            const auto path = native_utf8(*a->m_path);
            switch(a->m_purpose)
            {
            case purpose::open_project: state.add_action(actions::open_project{*a->m_path}); break;
            case purpose::data_root: m_data_root = path; break;
            case purpose::project_file: m_project_file = path; break;
            case purpose::palette: m_palette = path; break;
            case purpose::review_list:
                state.add_action(actions::open_review_list{*a->m_path});
                break;
            case purpose::review_results:
                state.add_action(actions::plan_review_import{*a->m_path});
                break;
            default: break;
            }
        }
    }

    if(state.m_browse_for_project)
    {
        state.m_browse_for_project = false;
        if(!m_dialogs.is_open())
        {
            const auto &current = project.config().m_filename;
            m_dialogs.open_json(
                purpose::open_project,
                current.empty() ? fs::path{} : current.parent_path());
        }
    }
}

void project_view::render(const project_model &project, app_state &state)
{
    take_dialog_answers(project, state);
    render_open_project(state);
    render_new_project(state);
    render_review_import(state);
}

void project_view::render_review_import(app_state &state)
{
    constexpr const char *popup = "Import review edits";
    if(!state.m_review_import)
        return;
    if(!ImGui::IsPopupOpen(popup))
        ImGui::OpenPopup(popup);
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 36.f, 0.f}, ImGuiCond_Appearing);
    if(!ImGui::BeginPopupModal(popup))
        return;
    auto &request = *state.m_review_import;
    ImGui::TextWrapped("%s", generic_utf8(request.m_results).c_str());
    ImGui::Separator();
    bool apply = false;
    if(!request.m_error.empty())
        error_text(request.m_error);
    else if(request.m_plan)
    {
        const auto &plan = *request.m_plan;
        ImGui::TextWrapped(
            "Applying these results to the project's labels would change %zu labels, add %zu and "
            "mark %zu not an event, in %zu recordings. %zu rows are already as the review says.",
            plan.m_changed,
            plan.m_added,
            plan.m_rejected,
            plan.m_files.size(),
            plan.m_unchanged);
        if(plan.unapplied())
        {
            ImGui::Text("%zu rows can't be applied:", plan.unapplied());
            for(const auto &[why, count] : plan.m_unapplied)
                ImGui::BulletText("%zu: %s", count, why.c_str());
        }
        if(!plan.m_unknown_classes.empty())
        {
            std::string names;
            for(const auto &name : plan.m_unknown_classes)
                names += (names.empty() ? "" : ", ") + name;
            ImGui::TextWrapped("Classes the palette doesn't have: %s", names.c_str());
            ImGui::Checkbox("Add them to the palette, and apply their rows too", &request.m_add_classes);
        }
        ImGui::Checkbox("Take the done mark off the recordings it changes", &request.m_clear_done);
        hint("So they're checked again. Ctrl+Z takes the labels back in one step, not the marks.");
        ImGui::BeginDisabled(plan.m_files.empty() && !(request.m_add_classes && !plan.m_unknown_classes.empty()));
        apply = ImGui::Button("Import");
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    const bool cancel = ImGui::Button("Cancel");
    if(apply)
        state.add_action(actions::apply_review_import{
            request.m_results,
            request.m_add_classes,
            request.m_clear_done});
    if(apply || cancel)
    {
        state.m_review_import.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void project_view::render_open_project(app_state &state)
{
    if(m_show_open_fallback)
    {
        m_show_open_fallback = false;
        ImGui::OpenPopup(open_popup);
    }
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 36.f, 0.f}, ImGuiCond_Appearing);
    if(!ImGui::BeginPopupModal(open_popup))
        return;

    error_text(m_open_error);
    ImGui::TextUnformatted("Project file (.json)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    input_text("##Project", m_open_path);
    if(ImGui::Button("Open") && !m_open_path.empty())
    {
        state.add_action(actions::open_project{path_from_utf8(m_open_path)});
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void project_view::render_new_project(app_state &state)
{
    if(state.m_show_new_project)
    {
        state.m_show_new_project = false;
        m_new_error.clear();
        ImGui::OpenPopup(new_popup);
    }
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 40.f, 0.f}, ImGuiCond_Appearing);
    if(!ImGui::BeginPopupModal(new_popup))
        return;

    const bool can_browse = !m_dialogs.is_open();

    ImGui::SeparatorText("Audio");
    ImGui::TextUnformatted("Folder of recordings");
    path_input("##Data root", m_data_root);
    ImGui::BeginDisabled(!can_browse);
    if(ImGui::Button("Browse...##Data root"))
        m_dialogs.open_folder(purpose::data_root, start_for(m_data_root, {}));
    ImGui::EndDisabled();
    hint("Searched for audio files in all its subfolders. It's only read, never written to.");

    ImGui::SeparatorText("Project");
    ImGui::TextUnformatted("Project file");
    path_input("##Project file", m_project_file);
    ImGui::BeginDisabled(!can_browse);
    if(ImGui::Button("Browse...##Project file"))
    {
        // By default beside the audio folder, which is often where a project belongs
        const auto root = path_from_utf8(m_data_root);
        m_dialogs.save_json(
            purpose::project_file,
            start_for(m_project_file, root.empty() ? fs::path{} : root.parent_path()));
    }
    ImGui::EndDisabled();
    hint(
        "Can be anywhere, eg. under version control away from the data. The labels and palette "
        "are saved beside it.");

    ImGui::SeparatorText("Classes");
    if(ImGui::RadioButton("New palette", !m_existing_palette))
        m_existing_palette = false;
    ImGui::SameLine();
    if(ImGui::RadioButton("Existing palette", m_existing_palette))
        m_existing_palette = true;
    if(m_existing_palette)
    {
        path_input("##Palette", m_palette);
        ImGui::BeginDisabled(!can_browse);
        if(ImGui::Button("Browse...##Palette"))
            m_dialogs.open_json(purpose::palette, start_for(m_palette, {}));
        ImGui::EndDisabled();
        hint("Shared, not copied, so that projects can label with the same classes.");
    }
    else
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        input_text("##Classes", m_classes);
        hint(
            "Class names, separated by commas, eg. Dog, Cat, Bird. Colours are chosen for them; "
            "edit the palette file to change them or add classes.");
    }

    // What will be written, so that nothing appears unexpectedly
    if(!m_project_file.empty())
    {
        new_project_spec spec{path_from_utf8(m_project_file), {}, {}, {}};
        const auto project = new_project_file(spec);
        const auto files = new_project_files_for(project);
        std::string writes = "Writes " + generic_utf8(project);
        if(!m_existing_palette)
            writes += "\nand " + generic_utf8(files.m_palette);
        writes += "\nLabels are saved to " + generic_utf8(files.m_labels);
        ImGui::Spacing();
        hint(writes.c_str());
    }

    ImGui::Spacing();
    error_text(m_new_error);

    if(ImGui::Button("Create and open"))
    {
        try
        {
            new_project_spec spec;
            spec.m_project_file = path_from_utf8(m_project_file);
            spec.m_data_root = path_from_utf8(m_data_root);
            if(m_existing_palette)
            {
                if(m_palette.empty())
                    throw std::runtime_error("Choose the palette file");
                spec.m_palette = path_from_utf8(m_palette);
            }
            else
                spec.m_classes = split_class_names(m_classes);

            const auto written = create_project(spec);
            state.add_action(actions::open_project{written});
            // To review the rest, such as the default channel, once it's open
            state.m_show_project_settings = true;
            m_new_error.clear();
            ImGui::CloseCurrentPopup();
        }
        catch(const std::exception &e)
        {
            m_new_error = e.what();
        }
    }
    ImGui::SameLine();
    if(ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
