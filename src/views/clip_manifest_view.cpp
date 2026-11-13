/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

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

bool pressed(ImGuiKey key) { return ImGui::GetIO().KeyMods == 0 && ImGui::IsKeyPressed(key, false); }
}

void clip_manifest_view::load(const project_model &project)
{
    m_loaded = true;
    m_error.clear();
    m_manifest = {};
    m_dirty = true;
    m_position.reset();
    try
    {
        m_settings = clip_manifest_settings::from_json(project.config().m_clip_manifest);
        if(m_settings)
            m_manifest = load_clip_manifest(project, *m_settings);
    }
    catch(const std::exception &e)
    {
        m_error = e.what();
    }
}

std::string clip_manifest_view::current_label(
    const project_model &project,
    const clip_manifest::row &row) const
{
    if(!row.m_source)
        return "(recording not found)";
    const auto *l = find_label_at(
        project.get_labels(*row.m_source),
        row.m_onset_s,
        m_settings->m_match_s,
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

void clip_manifest_view::apply_filter_and_sort(const project_model &project)
{
    // Terms separated by spaces must all match: column=value, or text anywhere in the row
    std::vector<std::pair<std::optional<size_t>, std::string>> terms;
    std::istringstream words(m_filter);
    for(std::string word; words >> word;)
    {
        const auto eq = word.find('=');
        std::optional<size_t> column;
        if(eq != std::string::npos)
            for(size_t c = 0U; c != m_manifest.m_columns.size(); ++c)
                if(lower(m_manifest.m_columns[c]) == lower(word.substr(0, eq)))
                    column = c;
        terms.emplace_back(column, lower(column ? word.substr(eq + 1U) : word));
    }

    std::optional<size_t> selected_row;
    if(m_position && *m_position < m_order.size())
        selected_row = m_order[*m_position];

    m_order.clear();
    for(size_t r = 0U; r != m_manifest.m_rows.size(); ++r)
    {
        const auto &fields = m_manifest.m_rows[r].m_fields;
        const bool pass = std::ranges::all_of(
            terms,
            [&](const auto &term)
            {
                if(term.first)
                    return lower(fields[*term.first]) == term.second;
                return std::ranges::any_of(
                    fields,
                    [&](const std::string &f) { return lower(f).contains(term.second); });
            });
        if(pass)
            m_order.push_back(r);
    }

    const auto columns = static_cast<int>(m_manifest.m_columns.size());
    if(m_sort_column >= 0)
    {
        std::ranges::stable_sort(
            m_order,
            [&](size_t a, size_t b)
            {
                const auto &ra = m_manifest.m_rows[a];
                const auto &rb = m_manifest.m_rows[b];
                const auto fa = m_sort_column < columns
                    ? ra.m_fields[static_cast<size_t>(m_sort_column)]
                    : current_label(project, ra);
                const auto fb = m_sort_column < columns
                    ? rb.m_fields[static_cast<size_t>(m_sort_column)]
                    : current_label(project, rb);
                return m_sort_descending ? less_field(fb, fa) : less_field(fa, fb);
            });
    }

    // Keep the same row selected
    m_position.reset();
    if(selected_row)
        if(auto it = std::ranges::find(m_order, *selected_row); it != m_order.end())
            m_position = static_cast<size_t>(it - m_order.begin());
    m_dirty = false;
}

void clip_manifest_view::select(app_state &state, size_t position)
{
    if(m_order.empty())
        return;
    m_position = std::min(position, m_order.size() - 1U);
    m_scroll_to_selection = true;
    state.add_action(actions::play_clip{m_manifest.m_rows[m_order[*m_position]].m_clip});
}

void clip_manifest_view::verdict(
    const project_model &project,
    app_state &state,
    clip_verdict v,
    label_defn_id cls)
{
    if(!m_position)
        return;
    const auto &row = m_manifest.m_rows[m_order[*m_position]];
    if(!row.m_source)
    {
        state.report_error("The clip's recording isn't in the project, so there's no label to change.");
        return;
    }
    state.add_action(actions::judge_clip{*row.m_source, row.m_onset_s, m_settings->m_match_s, v, cls});
    if(*m_position + 1U < m_order.size())
        select(state, *m_position + 1U);
}

void clip_manifest_view::handle_keys(const project_model &project, app_state &state)
{
    if(ImGui::GetIO().WantTextInput || m_order.empty())
        return;

    if(pressed(ImGuiKey_DownArrow))
        select(state, m_position ? *m_position + 1U : 0U);
    if(pressed(ImGuiKey_UpArrow))
        select(state, m_position && *m_position > 0U ? *m_position - 1U : 0U);
    if(!m_position)
        return;
    const auto &row = m_manifest.m_rows[m_order[*m_position]];

    if(pressed(ImGuiKey_Space))
        select(state, *m_position);

    const auto named = class_named(project.m_label_dict, row.m_label);
    if(pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter))
    {
        if(named)
            verdict(project, state, clip_verdict::correct, *named);
        else
            state.report_error("\"" + row.m_label + "\" isn't in the palette: relabel it with 1-9.");
    }
    if(pressed(ImGuiKey_X))
        verdict(project, state, clip_verdict::not_event, named.value_or(label_defn_id{}));

    size_t index = 0U;
    for(const auto &defn : project.m_label_dict)
    {
        if(index == 9U)
            break;
        if(pressed(static_cast<ImGuiKey>(ImGuiKey_1 + index)) ||
           pressed(static_cast<ImGuiKey>(ImGuiKey_Keypad1 + index)))
            verdict(project, state, clip_verdict::relabel, defn.m_id);
        ++index;
    }

    if(pressed(ImGuiKey_J) && row.m_source)
        state.add_action(actions::jump_to{*row.m_source, row.m_onset_s});
}

void clip_manifest_view::render(const project_model &project, app_state &state)
{
    if(!m_loaded)
        load(project);

    state.m_keys_captured = false;
    if(!ImGui::Begin(m_window_name.c_str()))
    {
        ImGui::End();
        return;
    }

    if(!m_settings && m_error.empty())
    {
        ImGui::TextWrapped(
            "No clip manifest in the project. Add a \"clip-manifest\" section to check clips cut by "
            "another tool; see ARCHITECTURE.md.");
        ImGui::End();
        return;
    }

    if(ImGui::Button("Reload"))
        load(project);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    char buffer[256]{};
    m_filter.copy(buffer, sizeof(buffer) - 1U);
    if(ImGui::InputTextWithHint("##Filter", "Filter: words, or column=value", buffer, sizeof(buffer)))
    {
        m_filter = buffer;
        m_dirty = true;
    }
    if(!m_error.empty())
    {
        ImGui::TextColored(ImVec4{1.f, 0.6f, 0.3f, 1.f}, "%s", m_error.c_str());
        ImGui::End();
        return;
    }

    if(m_dirty)
        apply_filter_and_sort(project);
    ImGui::Text(
        "%zu of %zu clips. Up/Down play, Enter correct, X not an event, 1-9 relabel, J show in the "
        "recording.",
        m_order.size(),
        m_manifest.m_rows.size());
    if(m_manifest.m_unresolved)
        ImGui::TextColored(
            ImVec4{1.f, 0.8f, 0.3f, 1.f},
            "%zu clips' recordings aren't in the project",
            m_manifest.m_unresolved);

    // Keys act on the list while it has focus, rather than on the waveform
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        state.m_keys_captured = true;
        handle_keys(project, state);
    }

    const auto columns = static_cast<int>(m_manifest.m_columns.size());
    const auto flags = ImGuiTableFlags_Sortable |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_BordersInnerV;
    if(columns > 0 && ImGui::BeginTable("##Clips", columns + 1, flags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        for(const auto &name : m_manifest.m_columns)
            ImGui::TableSetupColumn(name.c_str());
        ImGui::TableSetupColumn("Now in the recording");
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
            apply_filter_and_sort(project);
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
                const auto &row = m_manifest.m_rows[m_order[position]];
                ImGui::TableNextRow();
                ImGui::PushID(i);
                for(int c = 0; c != columns; ++c)
                {
                    ImGui::TableSetColumnIndex(c);
                    const auto &text = row.m_fields[static_cast<size_t>(c)];
                    if(c == 0)
                    {
                        const bool selected = m_position == position;
                        if(ImGui::Selectable(
                               text.c_str(),
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
                    else
                        ImGui::TextUnformatted(text.c_str());
                }
                ImGui::TableSetColumnIndex(columns);
                ImGui::TextUnformatted(current_label(project, row).c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
