/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <utility>

#include <imgui.h>

#include "../model/paths.h"
#include "clip_manifest_view.h"

namespace
{
std::string lower(std::string s)
{
    std::ranges::transform(
        s,
        s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Numbers sort as numbers, everything else as text
bool less_field(const std::string &a, const std::string &b)
{
    char *end_a{}, *end_b{};
    const double x = std::strtod(a.c_str(), &end_a);
    const double y = std::strtod(b.c_str(), &end_b);
    const bool num_a = !a.empty() && *end_a == '\0';
    const bool num_b = !b.empty() && *end_b == '\0';
    if(num_a && num_b)
        return x < y;
    if(num_a != num_b)
        return num_a;
    return lower(a) < lower(b);
}

/// A key pressed with exactly the given modifiers (and none others)
bool pressed(ImGuiKey key, ImGuiKeyChord mods = ImGuiMod_None)
{
    return ImGui::GetIO().KeyMods == mods && ImGui::IsKeyPressed(key, false);
}

/// Columns after the list's own
constexpr const char *extra_columns[] = {"Verdict", "Note", "Now in the recording"};
constexpr size_t extra_count = std::size(extra_columns);
}

size_t clip_manifest_view::column_count(const app_state &state) const
{
    return (state.m_review_list ? state.m_review_list->m_columns.size() : 0U) + extra_count;
}

std::string clip_manifest_view::column_name(const app_state &state, size_t column) const
{
    const auto &own = state.m_review_list->m_columns;
    return column < own.size() ? own[column] : extra_columns[column - own.size()];
}

std::string clip_manifest_view::current_label(
    const project_model &project,
    const app_state &state,
    const clip_manifest::row &row) const
{
    if(!row.m_source)
        return row.m_clip ? "(recording not found: clip only)" : "(recording not found)";
    const auto *l = find_label_at(
        project.get_labels(*row.m_source),
        row.m_onset_s,
        state.m_review_list_settings.m_match_s,
        class_named(project.m_label_dict, row.m_label));
    if(!l)
        return "-";
    const auto *defn = project.m_label_dict.find_ptr(l->m_defn_id);
    const std::string name = defn ? defn->m_class : "?";
    switch(l->m_state)
    {
    case label_state::confirmed: return name + " (checked)";
    case label_state::rejected: return "not an event";
    case label_state::suggested:
    default: return name + " (suggested)";
    }
}

std::string clip_manifest_view::cell(
    const project_model &project,
    const app_state &state,
    size_t row,
    size_t column) const
{
    const auto &list = *state.m_review_list;
    const auto &r = list.m_rows[row];
    if(column < list.m_columns.size())
        return r.m_fields[column];
    switch(column - list.m_columns.size())
    {
    case 0:
        return r.m_result.m_new_label.empty() ? r.m_result.m_verdict
                                              : r.m_result.m_verdict + ": " + r.m_result.m_new_label;
    case 1: return r.m_result.m_note;
    default: return current_label(project, state, r);
    }
}

void clip_manifest_view::take_list(const project_model &project, app_state &state)
{
    m_generation = state.m_review_list_generation;
    m_order.clear();
    m_position.reset();
    m_note_row.reset();
    m_filter.clear();
    m_sort_column = -1;
    m_sort_descending = false;
    m_dirty = true;
    if(!state.m_review_list)
        return;

    const auto &views = state.m_settings.m_list_views;
    if(auto it = views.find(generic_utf8(state.m_review_list->m_path)); it != views.end())
    {
        m_filter = it->second.m_filter;
        m_sort_descending = it->second.m_descending;
        for(size_t c = 0U; c != column_count(state); ++c)
            if(!it->second.m_sort_column.empty() && column_name(state, c) == it->second.m_sort_column)
                m_sort_column = static_cast<int>(c);
    }
    apply_filter_and_sort(project, state);

    // Carry on from the first row not heard yet, without playing it until asked
    const auto &rows = state.m_review_list->m_rows;
    const auto next = std::ranges::find_if(
        m_order,
        [&](size_t r) { return rows[r].m_result.m_verdict.empty(); });
    if(next != m_order.end())
    {
        m_position = static_cast<size_t>(next - m_order.begin());
        m_scroll_to_selection = true;
    }
}

void clip_manifest_view::remember_view(app_state &state) const
{
    if(!state.m_review_list)
        return;
    state.m_settings.m_list_views[generic_utf8(state.m_review_list->m_path)] = review_list_view{
        m_filter,
        m_sort_column >= 0 ? column_name(state, static_cast<size_t>(m_sort_column)) : std::string{},
        m_sort_descending};
}

void clip_manifest_view::apply_filter_and_sort(const project_model &project, const app_state &state)
{
    m_dirty = false;
    if(!state.m_review_list)
        return;
    const auto columns = column_count(state);

    // Terms separated by spaces must all match: column=value, or text anywhere in the row.
    // "verdict=" keeps the rows not heard yet.
    std::vector<std::pair<std::optional<size_t>, std::string>> terms;
    std::istringstream words(m_filter);
    for(std::string word; words >> word;)
    {
        const auto eq = word.find('=');
        std::optional<size_t> column;
        if(eq != std::string::npos)
            for(size_t c = 0U; c != columns; ++c)
                if(lower(column_name(state, c)) == lower(word.substr(0, eq)))
                    column = c;
        terms.emplace_back(column, lower(column ? word.substr(eq + 1U) : word));
    }

    std::optional<size_t> selected_row;
    if(m_position && *m_position < m_order.size())
        selected_row = m_order[*m_position];

    m_order.clear();
    for(size_t r = 0U; r != state.m_review_list->m_rows.size(); ++r)
    {
        const bool pass = std::ranges::all_of(
            terms,
            [&](const auto &term)
            {
                if(term.first)
                {
                    const auto text = lower(cell(project, state, r, *term.first));
                    // A verdict with a new label matches its verdict alone too
                    return text == term.second || text.starts_with(term.second + ":");
                }
                for(size_t c = 0U; c != columns; ++c)
                    if(lower(cell(project, state, r, c)).contains(term.second))
                        return true;
                return false;
            });
        if(pass)
            m_order.push_back(r);
    }

    if(m_sort_column >= 0 && static_cast<size_t>(m_sort_column) < columns)
    {
        const auto c = static_cast<size_t>(m_sort_column);
        std::vector<std::string> keys(state.m_review_list->m_rows.size());
        for(const auto r : m_order)
            keys[r] = cell(project, state, r, c);
        std::ranges::stable_sort(
            m_order,
            [&](size_t a, size_t b)
            { return m_sort_descending ? less_field(keys[b], keys[a]) : less_field(keys[a], keys[b]); });
    }

    // Keep the same row selected
    m_position.reset();
    if(selected_row)
        if(auto it = std::ranges::find(m_order, *selected_row); it != m_order.end())
            m_position = static_cast<size_t>(it - m_order.begin());
}

void clip_manifest_view::select(app_state &state, size_t position, bool play)
{
    if(m_order.empty())
        return;
    m_position = std::min(position, m_order.size() - 1U);
    m_scroll_to_selection = true;
    state.add_action(actions::show_list_row{m_order[*m_position], play});
}

void clip_manifest_view::verdict(
    const project_model &project,
    app_state &state,
    clip_verdict v,
    label_defn_id cls)
{
    if(!m_position)
        return;
    state.add_action(actions::judge_clip{m_order[*m_position], v, cls});
    if(state.m_settings.m_auto_advance && *m_position + 1U < m_order.size())
        select(state, *m_position + 1U);
}

void clip_manifest_view::handle_keys(const project_model &project, app_state &state)
{
    if(ImGui::GetIO().WantTextInput)
        return;

    // The list has the keys while it has focus, so undo is here too
    if(pressed(ImGuiKey_Z, ImGuiMod_Ctrl))
        state.add_action(actions::undo{});
    if(pressed(ImGuiKey_Y, ImGuiMod_Ctrl) || pressed(ImGuiKey_Z, ImGuiMod_Ctrl | ImGuiMod_Shift))
        state.add_action(actions::redo{});

    if(m_order.empty())
        return;
    if(pressed(ImGuiKey_DownArrow))
        select(state, m_position ? *m_position + 1U : 0U);
    if(pressed(ImGuiKey_UpArrow))
        select(state, m_position && *m_position > 0U ? *m_position - 1U : 0U);
    if(!m_position)
        return;
    const auto &row = state.m_review_list->m_rows[m_order[*m_position]];

    if(pressed(ImGuiKey_Space))
        select(state, *m_position);
    if(pressed(ImGuiKey_N))
        m_focus_note = true;

    // The list's class, else the class of the label at the event, for a verdict of correct
    auto named = class_named(project.m_label_dict, row.m_label);
    if(!named && row.m_source)
        if(const auto *l = find_label_at(
               project.get_labels(*row.m_source),
               row.m_onset_s,
               state.m_review_list_settings.m_match_s))
            named = l->m_defn_id;
    if(pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter))
    {
        if(named)
            verdict(project, state, clip_verdict::correct, *named);
        else
            state.report_error(
                row.m_label.empty() ? std::string{"The row has no class: relabel it with 1-9."}
                                    : "\"" + row.m_label + "\" isn't in the palette: relabel it with 1-9.");
    }
    if(pressed(ImGuiKey_X))
        verdict(project, state, clip_verdict::not_event, named.value_or(label_defn_id{}));

    const auto numbered = state.numbered_classes(project);
    for(size_t i = 0U; i != std::min<size_t>(numbered.size(), 9U); ++i)
        if(pressed(static_cast<ImGuiKey>(ImGuiKey_1 + i)) ||
           pressed(static_cast<ImGuiKey>(ImGuiKey_Keypad1 + i)))
            verdict(project, state, clip_verdict::relabel, numbered[i]);

    if(pressed(ImGuiKey_J) && row.m_source)
        state.add_action(actions::jump_to{*row.m_source, row.m_onset_s});
}

void clip_manifest_view::render_note(app_state &state)
{
    const auto row = m_position ? std::optional{m_order[*m_position]} : std::nullopt;
    if(row != m_note_row)
    {
        m_note_row = row;
        m_note = row ? state.m_review_list->m_rows[*row].m_result.m_note : std::string{};
    }
    ImGui::BeginDisabled(!row);
    if(m_focus_note)
    {
        ImGui::SetKeyboardFocusHere();
        m_focus_note = false;
    }
    char buffer[512]{};
    m_note.copy(buffer, sizeof(buffer) - 1U);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::InputTextWithHint(
           "##Note",
           "Note on this row (N), written with its verdict",
           buffer,
           sizeof(buffer),
           ImGuiInputTextFlags_EnterReturnsTrue))
        ImGui::SetWindowFocus(); // Enter is done: back to the list
    m_note = buffer;
    if(ImGui::IsItemDeactivated() && row &&
       m_note != state.m_review_list->m_rows[*row].m_result.m_note)
        state.add_action(actions::set_row_note{*row, m_note});
    ImGui::EndDisabled();
}

void clip_manifest_view::render(const project_model &project, app_state &state)
{
    if(m_generation != state.m_review_list_generation)
        take_list(project, state);

    state.m_keys_captured = false;
    if(std::exchange(state.m_focus_review_list, false))
        ImGui::SetNextWindowFocus();
    if(!ImGui::Begin(m_window_name.c_str()))
    {
        ImGui::End();
        return;
    }

    if(!state.m_review_list && state.m_review_list_error.empty())
    {
        ImGui::TextWrapped(
            "No review list open. Open one from File > Open review list, or name one in the "
            "project's \"clip-manifest\" section; see README.md for the format.");
        ImGui::End();
        return;
    }
    if(!state.m_review_list)
    {
        ImGui::TextColored(ImVec4{1.f, 0.6f, 0.3f, 1.f}, "%s", state.m_review_list_error.c_str());
        ImGui::End();
        return;
    }
    auto &list = *state.m_review_list;

    if(ImGui::Button("Reload"))
        state.add_action(actions::open_review_list{list.m_path});
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    char buffer[256]{};
    m_filter.copy(buffer, sizeof(buffer) - 1U);
    if(ImGui::InputTextWithHint(
           "##Filter",
           "Filter: words, or column=value (verdict= for the rows not heard)",
           buffer,
           sizeof(buffer)))
    {
        m_filter = buffer;
        m_dirty = true;
        remember_view(state);
    }

    if(m_dirty)
        apply_filter_and_sort(project, state);
    ImGui::TextUnformatted(generic_utf8(list.m_path).c_str());
    ImGui::Text(
        "%zu of %zu rows shown, %zu heard. Up/Down show and play, Space again, Enter correct, X not "
        "an event, 1-9 relabel, N note, J jump.",
        m_order.size(),
        list.m_rows.size(),
        list.heard());
    if(list.m_unresolved)
        ImGui::TextColored(
            ImVec4{1.f, 0.8f, 0.3f, 1.f},
            "%zu rows' recordings aren't in the project",
            list.m_unresolved);

    // Keys act on the list while it has focus, rather than on the waveform
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        state.m_keys_captured = true;
        handle_keys(project, state);
    }

    render_note(state);

    const auto columns = static_cast<int>(column_count(state));
    const auto flags = ImGuiTableFlags_Sortable |
        ImGuiTableFlags_SortTristate |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_NoSavedSettings; // The sort is remembered per list instead
    // A table per list, so that one's widths and sort don't carry over to another's columns
    ImGui::PushID(static_cast<int>(m_generation));
    if(ImGui::BeginTable("##Rows", columns, flags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        for(int c = 0; c != columns; ++c)
        {
            ImGuiTableColumnFlags column_flags = ImGuiTableColumnFlags_None;
            if(c == m_sort_column)
                column_flags |= ImGuiTableColumnFlags_DefaultSort |
                    (m_sort_descending ? ImGuiTableColumnFlags_PreferSortDescending
                                       : ImGuiTableColumnFlags_PreferSortAscending);
            ImGui::TableSetupColumn(column_name(state, static_cast<size_t>(c)).c_str(), column_flags);
        }
        ImGui::TableHeadersRow();

        if(auto *specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty)
        {
            if(specs->SpecsCount > 0)
            {
                m_sort_column = specs->Specs[0].ColumnIndex;
                m_sort_descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            }
            else
                m_sort_column = -1;
            specs->SpecsDirty = false;
            apply_filter_and_sort(project, state);
            remember_view(state);
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_order.size()));
        if(m_scroll_to_selection && m_position)
            clipper.IncludeItemByIndex(static_cast<int>(*m_position));
        while(clipper.Step())
        {
            for(int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto position = static_cast<size_t>(i);
                const auto r = m_order[position];
                const auto &row = list.m_rows[r];
                ImGui::TableNextRow();
                ImGui::PushID(i);
                for(int c = 0; c != columns; ++c)
                {
                    ImGui::TableSetColumnIndex(c);
                    const auto text = cell(project, state, r, static_cast<size_t>(c));
                    if(c != 0)
                    {
                        ImGui::TextUnformatted(text.c_str());
                        continue;
                    }
                    const bool selected = m_position == position;
                    if(ImGui::Selectable(
                           text.empty() ? "##row" : text.c_str(),
                           selected,
                           ImGuiSelectableFlags_SpanAllColumns |
                               ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        select(state, position);
                        if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && row.m_source)
                            state.add_action(actions::jump_to{*row.m_source, row.m_onset_s});
                    }
                    if(selected && m_scroll_to_selection)
                    {
                        ImGui::SetScrollHereY();
                        m_scroll_to_selection = false;
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
    ImGui::End();
}
