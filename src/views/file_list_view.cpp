/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <algorithm>
#include <array>
#include <optional>
#include <string>

#include <imgui.h>

#include "../model/palette.h"
#include "../model/paths.h"
#include "file_list_view.h"

namespace
{
const char *status_marker(file_status status)
{
    switch(status)
    {
    case file_status::done: return "[x] ";
    case file_status::skip: return "[-] ";
    case file_status::todo:
    default: return "[ ] ";
    }
}
}

void file_list_view::render_filters(const project_model &project, app_state &state)
{
    auto &list = state.m_settings.m_file_list;

    // Words the paths must contain
    std::array<char, 256> search{};
    list.m_search.copy(search.data(), search.size() - 1U);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::InputTextWithHint(
           "##Search",
           "Search paths (all words)",
           search.data(),
           search.size(),
           ImGuiInputTextFlags_EscapeClearsAll))
        list.m_search = search.data();

    // Status, then the filter by class (and external filters, later)
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.f;
    const char *statuses[] = {"All", "To do", "Done", "Skipped", "With suggestions"};
    int status = static_cast<int>(list.m_status);
    ImGui::SetNextItemWidth(half);
    if(ImGui::Combo("##Status", &status, statuses, IM_ARRAYSIZE(statuses)))
        list.m_status = static_cast<file_filter>(status);
    ImGui::SameLine();
    const char *finders[] = {"Any labels", "By class"};
    int finder = static_cast<int>(list.m_finder);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::Combo("##Finder", &finder, finders, IM_ARRAYSIZE(finders)))
        list.m_finder = static_cast<file_finder>(finder);
    ImGui::SetItemTooltip("Filter the files further, by the classes of their labels");

    if(list.m_finder == file_finder::by_class)
        render_class_chooser(project, state);
}

void file_list_view::render_class_chooser(const project_model &project, app_state &state)
{
    auto &chosen = state.m_settings.m_file_list.m_classes;

    // The chosen classes the palette has, each a button that removes it
    std::optional<std::string> remove;
    bool any = false;
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for(const auto &name : chosen.m_classes)
    {
        const auto *defn = [&]() -> const label_defn *
        {
            for(const auto &d : project.m_label_dict)
                if(d.m_class == name)
                    return &d;
            return nullptr;
        }();
        if(!defn)
            continue;
        const auto text = name + " x";
        if(any)
        {
            // On the same line as the last one, if it fits
            const float w = ImGui::CalcTextSize(text.c_str()).x +
                ImGui::GetStyle().FramePadding.x * 2.f;
            if(ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w <= right)
                ImGui::SameLine();
        }
        any = true;
        ImGui::PushID(name.c_str());
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(defn->m_color_rgba));
        if(ImGui::SmallButton(text.c_str()))
            remove = name;
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Remove from the filter");
        ImGui::PopID();
    }
    if(remove)
        chosen.toggle(*remove);

    // Add one by name. Enter adds the first match.
    const auto matches = filter_classes(project.m_label_dict, m_class_query.data());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::InputTextWithHint(
           "##Add class",
           any ? "Add a class" : "Choose classes (or right-click one in the palette)",
           m_class_query.data(),
           m_class_query.size(),
           ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll))
    {
        for(const auto *m : matches)
            if(!chosen.has(m->m_class))
            {
                chosen.toggle(m->m_class);
                break;
            }
        m_class_query[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1); // Stay in the box to add another
    }
    if(m_class_query[0] != '\0')
    {
        const float shown = std::min(static_cast<float>(std::max<size_t>(matches.size(), 1U)), 4.5f);
        if(ImGui::BeginChild("##Class matches", {-FLT_MIN, shown * ImGui::GetTextLineHeightWithSpacing()}))
        {
            if(matches.empty())
                ImGui::TextDisabled("No class matches");
            for(const auto *m : matches)
            {
                bool on = chosen.has(m->m_class);
                if(ImGui::Selectable(m->m_class.c_str(), &on))
                    chosen.toggle(m->m_class);
            }
        }
        ImGui::EndChild();
    }

    ImGui::Checkbox("Suggestions", &chosen.m_suggestions);
    ImGui::SetItemTooltip("Count suggestions of the classes as well as confirmed labels");
    ImGui::SameLine();
    ImGui::Checkbox("Rejected (x:)", &chosen.m_rejected);
    ImGui::SetItemTooltip("Count rejected candidates of the classes too");
    if(any)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Alt+N / Alt+P: their labels");
    }
}

void file_list_view::render(const project_model &project, app_state &state)
{
    ImGui::Begin(m_window_name.c_str());

    if(!project.has_project())
    {
        ImGui::TextWrapped("No project loaded. Open one, or make one from a folder of audio:");
        if(ImGui::Button("Open project..."))
            state.m_browse_for_project = true;
        ImGui::SameLine();
        if(ImGui::Button("New project..."))
            state.m_show_new_project = true;
    }

    render_filters(project, state);

    const auto rows = state.filtered_files(project);
    size_t num_finished = 0U;
    for(const auto &file : project.get_files())
        num_finished += project.get_status(file.m_path) != file_status::todo;
    if(rows.size() == project.get_files().size())
        ImGui::Text("%zu of %zu files done or skipped", num_finished, project.get_files().size());
    else
        ImGui::Text(
            "%zu of %zu files shown, %zu done or skipped",
            rows.size(),
            project.get_files().size(),
            num_finished);

    auto &list = state.m_settings.m_file_list;
    const bool by_class = list.m_finder == file_finder::by_class &&
        !list.m_classes.ids(project.m_label_dict).empty();
    const auto trees = state.m_annotation_trees.size();
    const float list_height = ImGui::GetContentRegionAvail().y -
        (5.f + static_cast<float>(trees)) * ImGui::GetFrameHeightWithSpacing();
    const auto flags = ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_Sortable |
        ImGuiTableFlags_NoSavedSettings | // The order is in the app's settings
        ImGuiTableFlags_SizingStretchProp;
    if(ImGui::BeginTable(
           "##Audio Files", by_class ? 2 : 1, flags, ImVec2{-FLT_MIN, std::max(list_height, 100.f)}))
    {
        // Starting in the remembered order
        const bool by_count = by_class && list.m_sort == file_sort::class_count;
        const auto direction = list.m_descending ? ImGuiTableColumnFlags_PreferSortDescending
                                                 : ImGuiTableColumnFlags_PreferSortAscending;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(
            "Recording",
            ImGuiTableColumnFlags_WidthStretch |
                (by_count ? ImGuiTableColumnFlags_None
                          : ImGuiTableColumnFlags_DefaultSort | direction));
        if(by_class)
            ImGui::TableSetupColumn(
                "Of the classes",
                ImGuiTableColumnFlags_WidthFixed |
                    (by_count ? ImGuiTableColumnFlags_DefaultSort | direction
                              : ImGuiTableColumnFlags_PreferSortDescending));
        ImGui::TableHeadersRow();

        // The order is kept in the settings, so that N and P follow it and it's remembered
        if(auto *specs = ImGui::TableGetSortSpecs())
        {
            if(specs->SpecsDirty && specs->SpecsCount > 0)
            {
                const auto &spec = specs->Specs[0];
                list.m_sort = spec.ColumnIndex == 1 ? file_sort::class_count : file_sort::path;
                list.m_descending = spec.SortDirection == ImGuiSortDirection_Descending;
            }
            specs->SpecsDirty = false;
        }

        int id = 0;
        for(const auto &row : rows)
        {
            const auto *file = row.m_file;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            size_t num_labels = 0U, num_suggested = 0U;
            for(const auto &l : project.get_labels(file->m_path))
            {
                num_labels += l.is_confirmed();
                num_suggested += l.m_state == label_state::suggested;
            }
            std::string counts;
            if(num_labels || num_suggested)
                counts = " (" + std::to_string(num_labels) +
                    (num_suggested ? ", " + std::to_string(num_suggested) + "?" : "") + ")";
            const auto *file_state = project.find_file_state(file->m_path);
            const bool exported =
                file_state && file_state->m_export && file_state->m_export->m_verified;
            const std::string text = status_marker(project.get_status(file->m_path)) +
                file->m_display_name +
                counts +
                (exported ? " - exported" : "");

            bool is_selected = state.is_selected_file(file->m_path);
            ImGui::PushID(id++); // Names are only unique within their dataset root
            if(ImGui::Selectable(text.c_str(), &is_selected, ImGuiSelectableFlags_SpanAllColumns) &&
               is_selected)
                state.add_action(actions::load_file{file->m_path});
            if(ImGui::BeginPopupContextItem("##File menu"))
            {
#if defined(_WIN32)
                const char *reveal = "Show in Explorer";
#elif defined(__APPLE__)
                const char *reveal = "Show in Finder";
#else
                const char *reveal = "Show in file manager";
#endif
                if(ImGui::MenuItem(reveal))
                    state.add_action(actions::reveal_file{file->m_path});
                if(ImGui::MenuItem("Copy path"))
                    ImGui::SetClipboardText(native_utf8(file->m_path).c_str());
                ImGui::EndPopup();
            }
            if(exported && ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Exported %s and verified: the original is safe to delete",
                    file_state->m_export->m_time.c_str());
            if(by_class)
            {
                ImGui::TableNextColumn();
                ImGui::Text("%zu", row.m_class_count);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Status of the active file
    if(state.has_active_file())
    {
        const auto &active = state.m_active_file.value();
        int status = static_cast<int>(project.get_status(active));
        bool changed = ImGui::RadioButton("To do", &status, static_cast<int>(file_status::todo));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Done (D)", &status, static_cast<int>(file_status::done));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Skip (S)", &status, static_cast<int>(file_status::skip));
        if(changed)
            state.add_action(actions::set_file_status{active, static_cast<file_status>(status)});
    }

    if(project.num_unresolved() > 0U)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 80, 255));
        ImGui::TextWrapped(
            "%zu labels refer to audio files that weren't found. They're kept in the label file.",
            project.num_unresolved());
        ImGui::PopStyleColor();
    }

    if(ImGui::Button("Rescan files"))
        state.add_action(actions::rescan_files{});
    ImGui::SameLine();
    if(ImGui::Button("Export CSV"))
        state.add_action(actions::export_csv{});

    if(state.is_exporting())
    {
        const auto &progress = *state.m_export_progress;
        const auto total = std::max<size_t>(1U, progress.m_total);
        const auto done = progress.m_done.load();
        const auto text = "Exporting " + std::to_string(done) + " of " + std::to_string(total);
        ImGui::ProgressBar(
            static_cast<float>(done) / static_cast<float>(total),
            ImVec2{-ImGui::GetFrameHeightWithSpacing() * 3.f, 0.f},
            text.c_str());
        ImGui::SameLine();
        if(ImGui::Button("Cancel"))
            state.add_action(actions::cancel_export{});
    }
    else
    {
        if(ImGui::Button("Export clips"))
            state.add_action(actions::export_dataset{});
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("Cut clips around the labels of files marked done, for training");
        ImGui::SameLine();
        if(ImGui::Button("Settings..."))
            state.m_show_export_settings = true;
    }

    // Annotation trees from the project, for exchanging label files with other tools
    for(size_t i = 0U; i != state.m_annotation_trees.size(); ++i)
    {
        const auto &tree = state.m_annotation_trees[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tree.m_name.c_str());
        if(tree.m_import != annotation_tree_settings::import_mode::none)
        {
            ImGui::SameLine();
            if(ImGui::Button("Import"))
                state.add_action(actions::import_annotations{i});
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    tree.m_import == annotation_tree_settings::import_mode::suggestions
                        ? "Read the label files as suggestions to review"
                        : "Read the label files as labels");
        }
        if(tree.m_export)
        {
            ImGui::SameLine();
            if(ImGui::Button("Export"))
                state.add_action(actions::export_annotations{i});
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Write a label file for each file marked done (empty if it has no events)");
        }
        ImGui::PopID();
    }
    ImGui::End();
}
