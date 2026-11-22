/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <tuple>

#include <spdlog/spdlog.h>

#include "../dsp/band_playback.h"
#include "../dsp/high_pass.h"
#include "../dsp/loudness.h"
#include "../dsp/onset_snap.h"
#include "../export/clip_export.h"
#include "../interchange/annotation_tree.h"
#include "../io/audio_source.h"
#include "../io/reveal.h"
#include "../manifest/clip_manifest.h"
#include "../model/add_classes.h"
#include "../model/copy_labels.h"
#include "../model/paths.h"
#include "action_handler.h"
#include "actions.h"
#include "lanes.h"

action_handler::action_handler(audio_engine &aud_eng, project_model &project, app_state &state) :
    m_audio_engine(aud_eng),
    m_proj(project),
    m_state(state)
{
}

label_snapshot action_handler::snapshot(const std::filesystem::path &file) const
{
    label_snapshot s{m_proj.get_labels(file)};
    if(m_state.is_selected_file(file))
    {
        s.m_unlabelled = m_state.m_unlabelled;
        s.m_selection = m_state.selected_labels();
    }
    return s;
}

void action_handler::restore(const std::filesystem::path &file, const label_snapshot &s)
{
    const auto replaced = snapshot(file);
    m_proj.set_labels(file, s.m_labels);

    // Selections only exist for the active file:
    if(m_state.is_selected_file(file))
    {
        release_playback_of_removed(replaced, s);
        m_state.m_unlabelled = s.m_unlabelled;
        m_state.set_selection(s.m_selection);
    }
}

void action_handler::release_playback_of_removed(
    const label_snapshot &before,
    const label_snapshot &after)
{
    // Drawing or moving a label plays just its span. If the label goes or moves, so does that
    // limit.
    const auto region = m_audio_engine.get_region();
    const double tolerance = 1e-3;
    const auto near = [&](time_span a)
    {
        return std::abs(a.first - region.first) < tolerance &&
            std::abs(a.second - region.second) < tolerance;
    };
    const auto changed_with_region = [&](const labels &was, const labels &now)
    {
        return std::ranges::any_of(
            was,
            [&](const label &l)
            {
                const auto *n = now.find_ptr(l.m_id);
                const bool gone = !n || n->m_start_s != l.m_start_s || n->m_stop_s != l.m_stop_s;
                return gone &&
                    (near({l.m_start_s, l.m_stop_s}) || near(m_state.playback_window(l)));
            });
    };
    if(changed_with_region(before.m_labels, after.m_labels) ||
       changed_with_region(before.m_unlabelled, after.m_unlabelled))
        m_audio_engine.release_region_end();
}

template <typename F>
void action_handler::edit_labels(const std::set<label_id> &ids, F &&edit)
{
    if(!m_state.has_active_file())
        return;
    const auto &file = m_state.m_active_file.value();
    for(auto id : ids)
    {
        if(auto *unlabelled = m_state.m_unlabelled.find_ptr(id))
            edit(*unlabelled);
        else if(const auto *existing = m_proj.get_labels(file).find_ptr(id))
        {
            auto l = *existing;
            edit(l);
            m_proj.update_label(file, l);
        }
    }
}

void action_handler::restore_active_audio()
{
    if(!m_playing_band)
        return;
    m_playing_band = false;
    m_audio_engine.set_audio_clip(m_state.get_audio_buffer_ptr(), m_state.active_offset_s());
}

void action_handler::select_region(time_span t, frequency_band band)
{
    const auto &audio = m_state.get_audio_buffer();
    const auto &settings = m_state.m_settings;
    const band_playback_settings bp{settings.m_band_shift, settings.m_band_shift_to_hz};
    // A clip is placed at an offset; bands are only drawn on the project's recordings
    if(m_state.active_is_clip() || !plan_band(band, audio.m_sample_rate_hz, bp).filters())
    {
        restore_active_audio();
        m_audio_engine.select_region(t, m_state.m_loop);
        return;
    }
    auto excerpt = render_band(audio, t, band, bp);
    m_audio_engine.set_audio_clip(
        std::make_shared<const audio_buffer>(std::move(excerpt.m_audio)),
        excerpt.m_start_s);
    m_playing_band = true;
    m_audio_engine.select_region(t, m_state.m_loop);
}

void action_handler::play_label(const label &l)
{
    restore_active_audio();
    select_region(m_state.playback_window(l), l.m_band);
    if(m_state.m_auto_start)
        m_audio_engine.play();
}

void action_handler::operator()(const actions::load_file &f)
{
    if(m_state.is_selected_file(f.m_file_path))
    {
        restore_active_audio();
        return;
    }
    spdlog::info("loading file {}", generic_utf8(f.m_file_path));
    m_state.set_selected_file(f.m_file_path, m_proj.get_channel(f.m_file_path));
    m_playing_band = false;

    m_audio_engine.set_audio_clip(m_state.get_audio_buffer_ptr());
    m_audio_engine.select_region(
        time_span{0, m_state.get_audio_buffer().duration_s()},
        m_state.m_loop);
}

void action_handler::operator()(const actions::add_label &r)
{
    // While a review list's row is shown, a region drawn is a sound the list didn't have
    if(m_state.list_mode() && m_state.is_selected_file(r.m_file))
    {
        const auto &shown = m_state.m_review_list->m_rows[*m_state.m_review_list_row];
        const auto *defn = r.m_defn_id ? m_proj.m_label_dict.find_ptr(*r.m_defn_id) : nullptr;
        add_list_row(r.m_t, defn ? defn->m_class : shown.reviewed_label());
        return;
    }
    if(m_state.active_is_clip())
        return; // A clip isn't a recording of the project, so it has no labels
    undoable(
        r.m_file,
        [&]
        {
            label l{
                get_next_label_id(),
                r.m_defn_id.value_or(label_defn_id{}),
                r.m_t.first,
                r.m_t.second
            };
            l.m_band = r.m_band;
            // If a class was selected in the label palette, apply that, otherwise leave it as an
            // unlabelled selection:
            if(r.m_defn_id)
                m_proj.add_label(r.m_file, l);
            else
                m_state.m_unlabelled.add(l);

            // Addition of a label / selection replaces the selection:
            m_state.set_selection({l.m_id});
        });
}

void action_handler::operator()(const actions::select_labels &s)
{
    const auto bands = m_state.label_bands(m_proj);
    auto hit = [&](const label &l)
    {
        const auto band = bands.find(l.m_id);
        // On the spectrogram, a label with a band is drawn at its frequencies
        if(s.m_hz && !l.m_band.is_full())
        {
            if(!l.m_band.contains(*s.m_hz))
                return false;
        }
        else if(band == bands.end() || !band->second.contains(s.m_y))
            return false;
        if(l.is_onset())
            return std::abs(s.m_x - l.m_start_s) <= s.m_tolerance_s;
        return l.m_start_s <= s.m_x && s.m_x <= l.m_stop_s;
    };

    std::vector<const label *> under;
    for(const auto *l : m_state.visible_labels(m_proj))
        if(s.m_flag ? l->m_id == *s.m_flag : hit(*l))
            under.push_back(l);

    // Which are hit: all of them, or one at a time
    const bool cycle = s.m_one_at_a_time && !s.m_flag;
    if(cycle)
        order_by_area(under, m_state.get_audio_buffer().m_sample_rate_hz / 2.);
    const auto is_selected = [&](label_id id) { return m_state.is_label_selected(id); };
    const auto label_of = [&](label_id id) { return *std::ranges::find(under, id, &label::m_id); };
    // Prevent the selected label palette from going out of sync with the selections:
    const auto sync_palette = [&](const label *l)
    {
        if(!m_state.is_label_selected(l->m_id) && l->m_defn_id != m_state.m_active_label_defn)
            m_state.m_active_label_defn.reset();
    };

    std::vector<const label *> hits;
    if(s.m_toggle)
    {
        std::vector<label_id> toggle;
        if(cycle)
        {
            const auto [off, on] = cycle_pick(under, is_selected);
            if(off)
                toggle.push_back(*off);
            if(on)
                toggle.push_back(*on);
        }
        else
            for(const auto *l : under)
                toggle.push_back(l->m_id);
        for(const auto id : toggle)
        {
            const auto *l = label_of(id);
            sync_palette(l);
            if(m_state.toggle_label_selection(l->m_id))
                hits.push_back(l);
        }
    }
    else
    {
        // Select only what's hit. Nested boxes: the next one each click, back round to the first.
        std::vector<const label *> pick = under;
        if(cycle && !under.empty())
        {
            const auto on = cycle_pick(under, is_selected).second;
            pick = {label_of(on.value_or(under.front()->m_id))};
        }
        std::set<label_id> ids;
        for(const auto *l : pick)
        {
            if(l->m_defn_id != m_state.m_active_label_defn)
                m_state.m_active_label_defn.reset();
            ids.insert(l->m_id);
        }
        m_state.set_selection(std::move(ids));
        hits = std::move(pick);
    }

    // Set the playback selection to the most recently selected region:
    if(!hits.empty())
    {
        const auto *last = hits.back();
        if(last->m_state == label_state::suggested)
            m_state.m_review_pos = review_item{
                m_state.m_active_file.value(),
                last->m_id,
                last->m_start_s,
                last->m_score
            };
        select_row_of(*last);
        play_label(*last);
    }
}

void action_handler::select_row_of(const label &l)
{
    if(!m_state.m_review_list || !m_state.m_active_file)
        return;
    if(const auto row = app_state::row_of(l.m_id))
    {
        m_state.m_review_list_row = *row;
        return;
    }
    // A project label: the row in this recording whose start it holds, the nearest its start
    const auto &rows = m_state.m_review_list->m_rows;
    const double window = m_state.m_review_list_settings.m_match_s;
    std::optional<size_t> best;
    for(size_t r = 0U; r != rows.size(); ++r)
    {
        const auto &row = rows[r];
        if(!row.present() || row.m_source != m_state.m_active_file || m_state.active_is_clip() ||
           row.m_start_s < l.m_start_s - window || row.m_start_s > std::max(l.m_stop_s, l.m_start_s) + window)
            continue;
        if(!best || std::abs(row.m_start_s - l.m_start_s) < std::abs(rows[*best].m_start_s - l.m_start_s))
            best = r;
    }
    if(best)
        m_state.m_review_list_row = *best;
}

void action_handler::operator()(const actions::toggle_label_selection &t)
{
    if(!m_state.toggle_label_selection(t.m_id, t.m_clear_others))
        return;
    // A newly selected label becomes the playback region, so Space plays it
    const auto labels = m_state.visible_labels(m_proj);
    if(auto it = std::ranges::find(labels, t.m_id, &label::m_id); it != labels.end())
    {
        select_row_of(**it);
        play_label(**it);
    }
}

void action_handler::operator()(const actions::clear_selection &c) { m_state.clear_selections(); }

void action_handler::operator()(const actions::select_all_labels &)
{
    std::set<label_id> ids;
    for(const auto *l : m_state.visible_labels(m_proj))
        ids.insert(l->m_id);
    m_state.set_selection(std::move(ids));
}

void action_handler::assign_class(const std::set<label_id> &ids, label_defn_id defn_id)
{
    const auto &file = m_state.m_active_file.value();
    const auto relabel = [&](label &l) { l.m_defn_id = defn_id; };
    for(auto id : ids)
    {
        if(auto unlabelled = m_state.m_unlabelled.find_ptr(id))
        {
            auto l = *unlabelled;
            m_state.m_unlabelled.erase(unlabelled);
            relabel(l);
            m_proj.add_label(file, l);
        }
        else if(auto existing = m_proj.get_labels(file).find_ptr(id))
        {
            // Relabelling a suggestion (or a rejected one) confirms it
            auto l = *existing;
            relabel(l);
            l.m_state = label_state::confirmed;
            m_proj.update_label(file, l);
        }
    }
}

void action_handler::operator()(const actions::set_active_class &a)
{
    if(a.m_toggle && a.m_defn_id && a.m_defn_id == m_state.m_active_label_defn)
    {
        const auto labels = m_state.visible_labels(m_proj);
        const bool lacking = std::ranges::any_of(
            labels,
            [&](const label *l)
            { return m_state.is_label_selected(l->m_id) && l->m_defn_id != *a.m_defn_id; });
        if(!lacking)
        {
            m_state.m_active_label_defn.reset();
            return;
        }
    }
    m_state.m_active_label_defn = a.m_defn_id;
    if(!a.m_defn_id || !m_state.has_active_file())
        return;

    // Selected review list rows are relabelled in the list, not the label file
    const auto *defn = m_proj.m_label_dict.find_ptr(*a.m_defn_id);
    const auto ids = rows_judged(
        m_state.selected_labels(),
        clip_verdict::relabel,
        defn ? defn->m_class : std::string{});
    if(ids.empty() || m_state.active_is_clip())
        return;
    undoable(m_state.m_active_file.value(), [&] { assign_class(ids, a.m_defn_id.value()); });
}

void action_handler::operator()(const actions::add_class &a)
{
    if(m_state.active_is_clip())
        return; // A clip has no labels of the project
    if(!m_state.has_active_file() || m_state.selected_labels().empty())
        return;
    const auto &file = m_state.m_active_file.value();

    std::vector<label> unclassified;
    std::vector<label> selected;
    // Not over review list rows: a review doesn't make labels in the label file
    for(const auto *l : m_state.visible_labels(m_proj))
        if(m_state.is_label_selected(l->m_id) && !app_state::row_of(l->m_id))
            (m_state.m_unlabelled.find_ptr(l->m_id) ? unclassified : selected).push_back(*l);

    const auto plan = plan_add_class(m_proj.get_labels(file), unclassified, selected, a.m_defn_id);
    const auto *defn = m_proj.m_label_dict.find_ptr(a.m_defn_id);
    const auto name = defn ? defn->m_class : std::to_string(a.m_defn_id.m_id);
    const auto skipped = plan.m_skipped + plan.m_merged.size();
    if(skipped)
        spdlog::warn(
            "{} already has a label over {} of the {} selected spans, skipped",
            name,
            skipped,
            unclassified.size() + selected.size());
    if(plan.empty())
        return;

    // The selection and active class stay, so more classes can be added over the same spans
    undoable(
        file,
        [&]
        {
            for(const auto &l : plan.m_classified)
            {
                m_state.m_unlabelled.erase(l.m_id);
                m_proj.add_label(file, l);
            }
            auto selection = m_state.selected_labels();
            for(const auto &[dropped, there] : plan.m_merged)
            {
                m_state.m_unlabelled.erase(dropped);
                selection.erase(dropped);
                selection.insert(there);
            }
            m_state.set_selection(std::move(selection));
            for(const auto &l : plan.m_added)
                m_proj.add_label(file, l);
        });
}

void action_handler::operator()(const actions::select_whole_file &)
{
    if(m_state.active_is_clip())
        return; // A clip has no labels of the project
    if(!m_state.has_active_file())
        return;
    const auto &file = m_state.m_active_file.value();
    const time_span whole{0., m_state.get_audio_buffer().duration_s()};

    const auto &defn = m_state.m_active_label_defn;
    const auto there = defn ? find_over(m_proj.get_labels(file), *defn, whole)
                            : find_over(m_state.m_unlabelled, label_defn_id{}, whole);
    if(there)
    {
        m_state.set_selection({*there});
        return;
    }
    (*this)(actions::add_label{whole, file, defn});
}

void action_handler::operator()(const actions::assign_class &a)
{
    if(!m_state.has_active_file())
        return;
    const auto *defn = m_proj.m_label_dict.find_ptr(a.m_defn_id);
    const auto ids =
        rows_judged(a.m_ids, clip_verdict::relabel, defn ? defn->m_class : std::string{});
    if(ids.empty() || m_state.active_is_clip())
        return;
    undoable(m_state.m_active_file.value(), [&] { assign_class(ids, a.m_defn_id); });
}

void action_handler::operator()(const actions::accept_labels &a)
{
    if(!m_state.has_active_file())
        return;
    const auto ids = rows_judged(a.m_ids, clip_verdict::correct);
    if(ids.empty() || m_state.active_is_clip())
        return;
    undoable(
        m_state.m_active_file.value(),
        [&] { edit_labels(ids, [](label &l) { l.m_state = label_state::confirmed; }); });
}

void action_handler::operator()(const actions::reject_labels &rejected)
{
    if(!m_state.has_active_file())
        return;
    const actions::reject_labels r{rows_judged(rejected.m_ids, clip_verdict::not_event)};
    if(r.m_ids.empty() || m_state.active_is_clip())
        return;
    undoable(
        m_state.m_active_file.value(),
        [&]
        {
            // Unclassified selections aren't anything yet, so there's nothing to keep
            for(auto id : r.m_ids)
                if(m_state.m_unlabelled.erase(id))
                    m_state.deselect(id);
            edit_labels(r.m_ids, [](label &l) { l.m_state = label_state::rejected; });
            if(!m_state.m_settings.m_show_rejected)
                for(auto id : r.m_ids)
                    m_state.deselect(id);
        });
}

void action_handler::operator()(const actions::delete_labels &d)
{
    if(!m_state.has_active_file())
        return;

    // Rows the reviewer added can be taken back; the list's own rows stay
    std::set<label_id> ids;
    for(const auto id : d.m_ids)
    {
        const auto row = app_state::row_of(id);
        if(!row)
            ids.insert(id);
        else if(m_state.m_review_list && *row < m_state.m_review_list->m_rows.size() &&
                m_state.m_review_list->m_rows[*row].m_added)
        {
            change_row(*row, [](row_verdict &v) { v = {}; });
            m_state.deselect(id);
        }
    }
    if(ids.empty() || m_state.active_is_clip())
        return;
    const auto &file = m_state.m_active_file.value();
    undoable(
        file,
        [&]
        {
            for(auto id : ids)
            {
                m_state.m_unlabelled.erase(id);
                m_proj.erase_label(file, id);
                m_state.deselect(id);
            }
        });
}

void action_handler::operator()(const actions::copy_labels &c)
{
    if(m_state.active_is_clip())
        return; // A clip has no labels of the project
    if(!m_state.has_active_file())
        return;
    // Rejected candidates aren't sounds to look for, and unclassified selections have no class
    std::vector<label> copied;
    for(const auto &l : m_proj.get_labels(m_state.m_active_file.value()))
        if(c.m_ids.contains(l.m_id) && l.m_state != label_state::rejected)
            copied.push_back(l);
    if(copied.empty())
        return;
    m_state.m_label_clipboard = label_clipboard{m_state.m_active_file.value(), std::move(copied)};
    spdlog::info(
        "Copied {} labels from {}",
        m_state.m_label_clipboard.m_labels.size(),
        generic_utf8(m_state.m_label_clipboard.m_source.filename()));
}

void action_handler::operator()(const actions::paste_labels &p)
{
    if(m_state.active_is_clip())
        return; // A clip has no labels of the project
    const auto &clipboard = m_state.m_label_clipboard;
    if(!m_state.has_active_file() || clipboard.m_labels.empty())
        return;
    const auto &file = m_state.m_active_file.value();

    const auto pasted = labels_to_paste(
        clipboard.m_labels,
        m_proj.get_labels(file),
        p.m_offset_s,
        m_state.get_audio_buffer().duration_s(),
        p.m_state);

    spdlog::info(
        "Pasted {} labels from {}, shifted by {:+.3f} s",
        pasted.m_added.size(),
        generic_utf8(clipboard.m_source.filename()),
        p.m_offset_s);
    // Only worth interrupting for when some were left out
    if(pasted.m_outside || pasted.m_present)
    {
        auto message = fmt::format(
            "Pasted {} of {} labels.",
            pasted.m_added.size(),
            clipboard.m_labels.size());
        if(pasted.m_outside)
            message +=
                fmt::format("\n{} fell outside this recording once shifted.", pasted.m_outside);
        if(pasted.m_present)
            message += fmt::format("\n{} were here already.", pasted.m_present);
        m_state.report_info(message);
    }
    if(pasted.m_added.empty())
        return;

    undoable(
        file,
        [&]
        {
            for(const auto &l : pasted.m_added)
                m_proj.add_label(file, l);
        });
    std::set<label_id> ids;
    for(const auto &l : pasted.m_added)
        ids.insert(l.m_id);
    m_state.set_selection(std::move(ids));
}

void action_handler::operator()(const actions::move_label &m)
{
    if(!m_state.has_active_file())
        return;

    const auto &file = m_state.m_active_file.value();
    const double duration_s = m_state.get_audio_buffer().duration_s();
    const double wanted_start = std::min(m.m_start_s, m.m_stop_s);
    const double wanted_stop = std::max(m.m_start_s, m.m_stop_s);

    // A review list row: its new times, in the recording's time, go to the results
    if(const auto row = app_state::row_of(m.m_id))
    {
        if(!m_state.m_review_list || *row >= m_state.m_review_list->m_rows.size())
            return;
        const auto extent = m_state.active_extent();
        // An edge left where the list put it isn't a change
        const auto listed =
            m_state.m_review_list->m_rows[*row].span(m_state.m_review_list_settings.m_region_s);
        const auto moved_to = [](double t, double was) -> std::optional<double>
        {
            if(std::abs(t - was) < 1e-9)
                return std::nullopt;
            return t;
        };
        change_row(
            *row,
            [&](row_verdict &v)
            {
                v.m_new_start_s = moved_to(
                    std::clamp(wanted_start, extent.first, extent.second),
                    listed.first);
                v.m_new_stop_s = moved_to(
                    std::clamp(wanted_stop, extent.first, extent.second),
                    listed.second);
            },
            history::merge_key{m.m_drag_seq, m.m_id});
        return;
    }
    if(m_state.active_is_clip())
        return;

    undoable(
        file,
        [&]
        {
            edit_labels(
                {m.m_id},
                [&](label &l)
                {
                    // Moved whole (not resized), it keeps its length at the ends of the file
                    // rather than being squashed
                    const double length = wanted_stop - wanted_start;
                    const bool moved = std::abs(length - (l.m_stop_s - l.m_start_s)) < 1e-9;
                    double start = std::clamp(wanted_start, 0., duration_s);
                    double stop = std::clamp(wanted_stop, 0., duration_s);
                    if(moved && length <= duration_s)
                    {
                        start = std::clamp(wanted_start, 0., duration_s - length);
                        stop = start + length;
                    }
                    l.m_start_s = start;
                    l.m_stop_s = stop;
                    if(m.m_band)
                        l.m_band = *m.m_band;
                });
        },
        history::merge_key{m.m_drag_seq, 0U});
}

void action_handler::operator()(const actions::snap_labels &s)
{
    if(m_state.active_is_clip())
        return; // A clip has no labels of the project
    if(!m_state.has_active_file())
        return;
    const auto &audio = m_state.get_audio_buffer();
    const auto &review = m_state.m_review;
    undoable(
        m_state.m_active_file.value(),
        [&]
        {
            edit_labels(
                s.m_ids,
                [&](label &l)
                {
                    const double onset = snap_onset(audio, l.m_start_s, review.m_snap_window_s);
                    if(const auto times = snapped(l, onset, review))
                        std::tie(l.m_start_s, l.m_stop_s) = *times;
                });
        });
}

namespace
{
/// The end of the sound starting at a time, or a short default where nothing stands out
double sound_end(const audio_buffer &audio, double start_s)
{
    constexpr double fallback_s = 0.5;
    const auto end = estimate_sound_end(audio, start_s).value_or(start_s + fallback_s);
    return std::max(std::min(end, audio.duration_s()), start_s);
}
}

void action_handler::operator()(const actions::extend_labels &e)
{
    if(m_state.active_is_clip())
        return; // A clip has no labels of the project
    if(!m_state.has_active_file())
        return;
    const auto &audio = m_state.get_audio_buffer();
    undoable(
        m_state.m_active_file.value(),
        [&]
        {
            edit_labels(
                e.m_ids,
                [&](label &l)
                {
                    if(l.is_onset())
                        l.m_stop_s = sound_end(audio, l.m_start_s);
                });
        });
}

void action_handler::operator()(const actions::mark_sound &m)
{
    if(m_state.active_is_clip())
        return; // A clip has no labels of the project
    if(!m_state.has_active_file())
        return;
    const double start = std::clamp(m.m_t_s, 0., m_state.get_audio_buffer().duration_s());
    (*this)(actions::add_label{
        {start, sound_end(m_state.get_audio_buffer(), start)},
        m.m_file,
        m.m_defn_id
    });
}

void action_handler::operator()(const actions::review_step &r)
{
    const auto queue = review_queue(m_proj, m_state.m_review);
    auto from = m_state.m_review_pos;
    const auto next = step_review(queue, from, r.m_forward, m_state.m_review);
    if(!next)
    {
        m_state.report_info("No suggestions left to review.");
        m_state.m_review_pos.reset();
        return;
    }
    m_state.m_review_pos = next;

    (*this)(actions::load_file{next->m_file});
    const auto *l = m_proj.get_labels(next->m_file).find_ptr(next->m_id);
    if(!l)
        return;
    m_state.set_selection({l->m_id});
    m_state.m_view_request = review_view(*l, m_state.m_review);
    select_region(m_state.playback_window(*l), l->m_band);
    m_audio_engine.play(); // Reviewing means listening, whatever auto play says
}

void action_handler::restore_step(const history::entry &e, bool undo)
{
    if(!e.m_file.empty())
        restore(e.m_file, undo ? e.m_before : e.m_after);
    for(const auto &more : e.m_more)
        restore(more.m_file, undo ? more.m_before : more.m_after);
    if(const auto &change = e.m_list_change)
        set_row_verdict(change->m_list, change->m_row, undo ? change->m_before : change->m_after);
}

void action_handler::operator()(const actions::undo &u)
{
    if(auto entry = m_state.m_history.undo())
        restore_step(*entry, true);
}

void action_handler::operator()(const actions::redo &r)
{
    if(auto entry = m_state.m_history.redo())
        restore_step(*entry, false);
}

void action_handler::operator()(const actions::set_file_status &s)
{
    if(m_state.active_is_clip() && m_state.is_selected_file(s.m_file))
        return; // A clip isn't one of the project's recordings
    m_proj.set_status(s.m_file, s.m_status);
}

void action_handler::operator()(const actions::set_channel &c)
{
    m_proj.set_channel(c.m_file, c.m_channel);
    if(m_state.is_selected_file(c.m_file))
        reload_active_audio();
}

void action_handler::reload_active_audio()
{
    if(!m_state.m_active_file)
        return;
    const auto &file = *m_state.m_active_file;
    m_state.load_audio(file, m_state.active_is_clip() ? std::nullopt : m_proj.get_channel(file));
    m_playing_band = false;
    const auto state = m_audio_engine.get_state();
    m_audio_engine.set_audio_clip(m_state.get_audio_buffer_ptr(), m_state.active_offset_s());
    m_audio_engine.select_region(
        time_span{state.m_playhead_s, m_state.get_audio_buffer().duration_s()},
        m_state.m_loop);
}

void action_handler::operator()(const actions::select_playback_device &dev)
{
    m_audio_engine.set_device(dev.m_dev);
    m_audio_engine.set_speed(m_state.m_settings.m_playback_speed);
    (*this)(actions::set_playback_gain{});
    m_state.m_settings.m_device_name = dev.m_dev.m_name;
}

void action_handler::operator()(const actions::select_playback_region &r)
{
    restore_active_audio();
    select_region(r.m_t, r.m_band);
    m_audio_engine.set_playback_loop(r.m_loop);
    if(m_state.m_auto_start || r.m_play)
        m_audio_engine.play();
}

void action_handler::operator()(const actions::toggle_playback &a)
{
    if(m_audio_engine.get_state().m_playing)
        m_audio_engine.stop();
    else
        m_audio_engine.play();
}

void action_handler::operator()(const actions::playback_loop &p)
{
    m_audio_engine.set_playback_loop(p.m_loop);
}

void action_handler::operator()(const actions::set_playback_speed &s)
{
    m_state.m_settings.m_playback_speed = std::clamp(s.m_speed, 0.1f, 2.f);
    m_audio_engine.set_speed(m_state.m_settings.m_playback_speed);
}

void action_handler::operator()(const actions::set_playback_gain &)
{
    const auto &s = m_state.m_settings;
    auto_gain::settings g;
    g.m_automatic = s.m_auto_gain;
    g.m_gain_db = s.m_playback_gain_db;
    g.m_max_gain_db = s.m_auto_gain_max_db;
    m_audio_engine.set_gain(g);
}

void action_handler::operator()(const actions::rescan_files &r) { m_proj.rescan_files(); }

void action_handler::operator()(const actions::export_csv &e) { m_proj.export_csv(); }

void action_handler::operator()(const actions::export_dataset &e)
{
    if(m_state.is_exporting())
        return;
    const auto settings = export_settings::from_json(m_proj.config().m_export);

    // The job is a copy of what the export needs, so labelling can carry on meanwhile
    auto job = std::make_shared<export_job>(prepare_export(m_proj, settings));
    auto progress = std::make_shared<export_progress>();
    m_state.m_export_progress = progress;
    m_state.m_export_result = std::async(
        std::launch::async,
        [job, progress] { return run_export(*job, progress.get()); });
}

void action_handler::operator()(const actions::cancel_export &c)
{
    if(m_state.m_export_progress)
        m_state.m_export_progress->m_cancel = true;
}

void action_handler::operator()(const actions::run_assist &r)
{
    if(m_state.is_assisting())
        return;
    auto settings = m_state.m_assisted_labellers.at(r.m_labeller);
    if(r.m_loudness)
        settings.m_loudness = *r.m_loudness;
    auto job = std::make_shared<assist_job>(
        prepare_assist(m_proj, settings, r.m_scope, m_state.m_active_file, r.m_class));
    // Built-ins see the audio as it's shown and played
    for(auto &input : job->m_inputs)
        input.m_high_pass = m_state.m_high_pass;
    if(job->m_inputs.empty())
    {
        m_state.m_assist_report = "No files to run on.";
        return;
    }
    auto progress = std::make_shared<assist_progress>();
    m_state.m_assist_progress = progress;
    m_state.m_assist_report.clear();
    m_state.m_assist_result = std::async(
        std::launch::async,
        [job, progress] { return run_assist(*job, progress.get()); });
}

void action_handler::operator()(const actions::cancel_assist &c)
{
    if(m_state.m_assist_progress)
        m_state.m_assist_progress->m_cancel = true;
}

void action_handler::poll_assist()
{
    auto &result = m_state.m_assist_result;
    if(!result.valid() || result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    m_state.m_assist_progress.reset();

    const auto outcome = result.get();
    const auto summary = apply_assist(m_proj, outcome);
    if(summary.m_added)
        m_state.m_history = history{}; // As for imports: older snapshots would drop them

    auto &report = m_state.m_assist_report;
    report = fmt::format(
        "{}{}: {} suggestions added in {} files{}.",
        outcome.m_name,
        outcome.m_cancelled ? " (cancelled)" : "",
        summary.m_added,
        summary.m_files,
        summary.m_duplicates ? fmt::format(", {} already there", summary.m_duplicates) : "");
    if(!summary.m_new_classes.empty())
    {
        report += "\nNot in the palette, added for this session:";
        for(const auto &name : summary.m_new_classes)
            report += " " + name;
    }
    if(!outcome.m_errors.empty())
    {
        report += fmt::format("\n{} files failed.", outcome.m_errors.size());
        if(!outcome.m_log_dir.empty())
            report += " Logs are in " + generic_utf8(outcome.m_log_dir);
        std::string errors;
        for(size_t i = 0U; i != std::min<size_t>(outcome.m_errors.size(), 5U); ++i)
            errors += (i ? "\n\n" : "") + outcome.m_errors[i];
        if(outcome.m_errors.size() > 5U)
            errors += fmt::format("\n\n...and {} more", outcome.m_errors.size() - 5U);
        m_state.report_error(outcome.m_name + " failed:\n" + errors);
    }
    spdlog::info("{}", report);
}

void action_handler::poll_background()
{
    poll_assist();

    auto &result = m_state.m_export_result;
    if(!result.valid() || result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    try
    {
        const auto outcome = result.get();
        apply_export(m_proj, outcome);
        if(outcome.m_cancelled)
            m_state.report_info("Export cancelled. Nothing was recorded.");
        else
            report_export(outcome.m_summary);
    }
    catch(const std::exception &e)
    {
        m_state.report_error(std::string{"Export failed: "} + e.what());
    }
    m_state.m_export_progress.reset();
}

void action_handler::operator()(const actions::reveal_file &r) { reveal_in_file_manager(r.m_file); }

void action_handler::operator()(const actions::set_high_pass &h)
{
    if(h.m_settings == m_state.m_high_pass)
        return;
    m_state.m_high_pass = h.m_settings;
    reload_active_audio();
}

void action_handler::operator()(const actions::set_project_settings &s)
{
    const auto channel_before =
        m_state.m_active_file ? m_proj.get_channel(*m_state.m_active_file) : std::nullopt;
    const auto high_pass_before = m_proj.config().m_high_pass;
    if(m_proj.set_settings(s.m_settings))
    {
        // New roots or a new palette: read the project again, as at the start
        m_state.m_project_request = m_proj.config().m_filename;
        return;
    }
    m_state.m_review = review_settings::from_json(m_proj.config().m_review);
    bool reload =
        m_state.m_active_file && m_proj.get_channel(*m_state.m_active_file) != channel_before;
    // A new project high-pass replaces any change made in Options for the session
    if(m_proj.config().m_high_pass != high_pass_before)
    {
        m_state.m_high_pass = high_pass_settings::from_json(m_proj.config().m_high_pass);
        reload = true;
    }
    // The active file follows a new default channel, unless it has its own
    if(reload)
        reload_active_audio();
}

void action_handler::operator()(const actions::open_project &o)
{
    // The app owns the project, so it makes the switch once the frame's actions are done
    m_state.m_project_request = o.m_file;
}

void action_handler::cancel_background()
{
    if(m_state.is_assisting())
    {
        (*this)(actions::cancel_assist{});
        m_state.m_assist_result.wait();
    }
    if(m_state.is_exporting())
    {
        (*this)(actions::cancel_export{});
        m_state.m_export_result.wait();
    }
    poll_background();
}

void action_handler::report_export(const export_summary &summary)
{
    std::string message = fmt::format(
        "Exported {} clips from {} files to\n{}",
        summary.m_clips,
        summary.m_files,
        generic_utf8(summary.m_directory));
    if(summary.m_unverified_clips)
        message += fmt::format(
            "\n\n{} clips didn't read back correctly. Keep their originals.",
            summary.m_unverified_clips);
    if(summary.m_todo_with_labels)
        message += fmt::format(
            "\n\n{} files with labels weren't exported because they aren't marked done.",
            summary.m_todo_with_labels);
    if(summary.m_withdrawn_files)
        message += fmt::format(
            "\n\nRemoved {} clips of {} files exported earlier but no longer marked done.",
            summary.m_withdrawn_clips,
            summary.m_withdrawn_files);
    m_state.report_info(message);

    for(const auto &error : summary.m_errors)
        m_state.report_error("Export failed for " + error);
}

void action_handler::operator()(const actions::set_export_settings &s)
{
    m_proj.set_export_section(s.m_section);
    m_state.report_info("Saved the export settings to " + generic_utf8(m_proj.config().m_filename));
}

void action_handler::operator()(const actions::import_annotations &i)
{
    const annotation_tree tree(m_proj.config(), m_state.m_annotation_trees.at(i.m_tree));
    const auto summary = import_annotations(m_proj, tree);

    // An undo step from before the import would put back labels without what it added
    m_state.m_history = history{};

    std::string message = fmt::format(
        "Read {} label files from {}:\n{} added, {} already present.",
        summary.m_files,
        generic_utf8(tree.root()),
        summary.m_added,
        summary.m_duplicates);
    if(summary.m_marked_done)
        message += fmt::format(
            "\n{} empty files: their recordings were marked done (no events).",
            summary.m_marked_done);
    if(summary.m_unmatched)
        message +=
            fmt::format("\n{} label files have no recording in the project.", summary.m_unmatched);
    if(!summary.m_new_classes.empty())
    {
        message += "\n\nNot in the palette, so added for this session (grey):";
        for(const auto &name : summary.m_new_classes)
            message += "\n  " + name;
    }
    m_state.report_info(message);
    for(const auto &error : summary.m_errors)
        m_state.report_error("Could not import " + error);
}

void action_handler::operator()(const actions::export_annotations &e)
{
    const annotation_tree tree(m_proj.config(), m_state.m_annotation_trees.at(e.m_tree));
    const auto summary = export_annotations(m_proj, tree);
    auto message = fmt::format(
        "Wrote {} label files ({} labels) to\n{}\n{} were already up to date.",
        summary.m_files - summary.m_unchanged,
        summary.m_labels,
        generic_utf8(tree.root()),
        summary.m_unchanged);
    if(summary.m_removed)
        message += fmt::format(
            "\nRemoved {} written earlier for files no longer done.",
            summary.m_removed);
    if(!summary.m_changed.empty())
    {
        message += "\n\nWritten earlier for files no longer done, but changed since, so left in "
                   "place (a reader may take them as reviewed):";
        for(const auto &path : summary.m_changed)
            message += "\n  " + path;
    }
    m_state.report_info(message);
    for(const auto &error : summary.m_errors)
        m_state.report_error("Could not export " + error);
}

void action_handler::operator()(const actions::open_review_list &o)
{
    const auto &section = m_proj.config().m_clip_manifest;
    try
    {
        clip_manifest_settings settings;
        if(o.m_file.empty())
        {
            const auto own = clip_manifest_settings::from_json(section);
            if(!own)
            {
                if(!o.m_quiet)
                    m_state.report_info("The project names no review list.");
                return;
            }
            settings = *own;
        }
        else
            settings = clip_manifest_settings::for_list(section, o.m_file);

        auto list = load_clip_manifest(m_proj, settings);
        // A project that doesn't scan its roots lists the recordings the list names
        if(!m_proj.scans_roots())
        {
            std::vector<std::filesystem::path> named;
            for(const auto &row : list.m_rows)
                if(row.m_source)
                    named.push_back(*row.m_source);
            m_proj.add_files(named);
        }
        m_state.m_settings.add_recent_list(
            {generic_utf8(list.m_path), generic_utf8(m_proj.config().m_filename)});
        m_state.m_review_list = std::move(list);
        m_state.m_review_list_settings = std::move(settings);
        m_state.m_review_list_error.clear();
        m_state.m_review_list_row.reset();
        m_state.m_focus_review_list = !o.m_quiet;
        ++m_state.m_review_list_generation;
    }
    catch(const std::exception &e)
    {
        m_state.m_review_list.reset();
        m_state.m_review_list_row.reset();
        m_state.m_review_list_error = e.what();
        ++m_state.m_review_list_generation;
        if(!o.m_quiet)
            m_state.report_error(e.what());
    }
}

void action_handler::operator()(const actions::show_list_row &s)
{
    if(!m_state.m_review_list || s.m_row >= m_state.m_review_list->m_rows.size())
        return;
    const auto &settings = m_state.m_review_list_settings;
    const auto &row = m_state.m_review_list->m_rows[s.m_row];
    m_state.m_review_list_row = s.m_row;
    const auto span = row.reviewed_span(settings.m_region_s);
    const label event{0U, label_defn_id{}, span.first, span.second};

    if(!row.m_source)
    {
        if(!row.m_clip)
        {
            m_state.report_info("This row's recording isn't in the project, and it has no clip.");
            return;
        }
        // The clip, placed at its offset, so its times are the recording's
        m_state.set_selected_clip(*row.m_clip, row.m_clip_offset_s);
            m_playing_band = false;
        m_audio_engine.set_audio_clip(m_state.get_audio_buffer_ptr(), m_state.active_offset_s());
    }
    else
    {
        const auto &file = *row.m_source;
        // The row's channel, for this row only: the file's own choice stays as it is
        const auto channel = row.m_channel ? row.m_channel : m_proj.get_channel(file);
        if(!m_state.is_selected_file(file) || m_state.active_is_clip())
        {
            // Play the event's stretch first, so it's heard without waiting for the whole
            // recording to be read; a second either side lets the high-pass settle
            const label at_file{0U, label_defn_id{}, span.first, span.second};
            const auto window = ::playback_window(at_file, m_state.m_review, 1e12);
            if(s.m_play)
            {
                double start_s{};
                auto excerpt = load_mono_range(
                    file,
                    channel,
                    {window.first - 1., window.second + 1.},
                    start_s);
                apply_high_pass(excerpt.m_samples, excerpt.m_sample_rate_hz, m_state.m_high_pass);
                m_audio_engine.set_audio_clip(
                    std::make_shared<const audio_buffer>(std::move(excerpt)),
                    start_s);
                m_audio_engine.select_region(window, false);
                m_audio_engine.play();
            }
            // Then the whole recording for the view; the excerpt plays on until something else
            // is played
            m_state.set_selected_file(file, channel);
                    m_playing_band = s.m_play;
            if(!s.m_play)
                m_audio_engine.set_audio_clip(m_state.get_audio_buffer_ptr());
        }
        else
        {
            restore_active_audio();
            if(channel != m_state.active_channel())
            {
                m_state.load_audio(file, channel);
                m_audio_engine.set_audio_clip(m_state.get_audio_buffer_ptr());
            }
        }
    }

    m_state.refresh_row_labels(m_proj);
    m_state.set_selection({app_state::row_label_base + s.m_row});
    m_state.m_view_request = review_view(event, m_state.m_review);
    if(m_playing_band)
        return; // Playing the excerpt already
    select_region(m_state.playback_window(event), frequency_band{});
    if(s.m_play)
        m_audio_engine.play(); // Hearing the list means listening, whatever auto play says
}

void action_handler::set_row_verdict(const std::filesystem::path &list, size_t row, const row_verdict &v)
{
    auto &open = m_state.m_review_list;
    if(!open || open->m_path != list || row >= open->m_rows.size())
    {
        m_state.report_info(
            "The review list " + generic_utf8(list) + " isn't open any more, so its results are "
            "left as they are.");
        return;
    }
    open->m_rows[row].m_result = v;
    ++m_state.m_review_list_revision;
    try
    {
        write_results(*open, m_state.m_settings.m_review_changes_file);
    }
    catch(const std::exception &e)
    {
        m_state.report_error(
            "Could not write the results " + generic_utf8(open->m_results_path) + ": " + e.what());
    }
}

template <typename F>
void action_handler::change_row(size_t row, F &&change, std::optional<history::merge_key> key)
{
    if(!m_state.m_review_list || row >= m_state.m_review_list->m_rows.size())
        return;
    const auto &list = *m_state.m_review_list;
    const auto before = list.m_rows[row].m_result;
    auto after = before;
    change(after);
    if(after == before)
        return;
    history::entry step;
    step.m_merge_key = key;
    step.m_list_change = row_verdict_change{list.m_path, row, before, after};
    set_row_verdict(list.m_path, row, after);
    m_state.m_history.push(std::move(step));
}

void action_handler::add_list_row(time_span span, const std::string &label)
{
    if(!m_state.list_mode())
        return;
    auto &list = *m_state.m_review_list;
    const auto row = list.add_row(*m_state.m_review_list_row, span, label);
    const auto added = list.m_rows[row].m_result;
    list.m_rows[row].m_result = {}; // So the step records it appearing
    history::entry step;
    step.m_list_change = row_verdict_change{list.m_path, row, {}, added};
    set_row_verdict(list.m_path, row, added);
    m_state.m_history.push(std::move(step));
    m_state.refresh_row_labels(m_proj);
    m_state.set_selection({app_state::row_label_base + row});
}

std::set<label_id> action_handler::rows_judged(
    const std::set<label_id> &ids,
    clip_verdict v,
    const std::string &new_label)
{
    std::set<label_id> others;
    for(const auto id : ids)
        if(const auto row = app_state::row_of(id))
            (*this)(actions::judge_clip{*row, v, new_label});
        else
            others.insert(id);
    return others;
}

void action_handler::operator()(const actions::judge_clip &j)
{
    change_row(
        j.m_row,
        [&](row_verdict &v)
        {
            // An added row stays added; relabelling it changes its class
            if(v.m_verdict == "added")
            {
                if(j.m_verdict == clip_verdict::relabel)
                    v.m_new_label = j.m_new_label;
                return;
            }
            v.m_verdict = verdict_name(j.m_verdict);
            v.m_new_label = j.m_verdict == clip_verdict::relabel ? j.m_new_label : std::string{};
        });
}

void action_handler::operator()(const actions::set_row_note &n)
{
    change_row(n.m_row, [&](row_verdict &v) { v.m_note = n.m_note; });
}

void action_handler::operator()(const actions::plan_review_import &p)
{
    auto &request = m_state.m_review_import.emplace();
    request.m_results = p.m_results;
    try
    {
        const auto settings =
            clip_manifest_settings::for_list(m_proj.config().m_clip_manifest, p.m_results);
        const auto results = load_results_file(m_proj, settings, p.m_results);
        request.m_plan = plan_review_import(m_proj, results, settings.m_match_s);
    }
    catch(const std::exception &e)
    {
        request.m_error = e.what();
    }
}

void action_handler::operator()(const actions::apply_review_import &a)
{
    m_state.m_review_import.reset();
    const auto settings =
        clip_manifest_settings::for_list(m_proj.config().m_clip_manifest, a.m_results);
    const auto results = load_results_file(m_proj, settings, a.m_results);
    auto plan = plan_review_import(m_proj, results, settings.m_match_s);
    if(a.m_add_classes && !plan.m_unknown_classes.empty())
    {
        m_proj.add_palette_classes({plan.m_unknown_classes.begin(), plan.m_unknown_classes.end()});
        plan = plan_review_import(m_proj, results, settings.m_match_s);
    }

    // One undo step over every recording it changes
    history::entry step;
    for(const auto &[file, after] : plan.m_files)
    {
        history::entry e{file, snapshot(file), {}, std::nullopt};
        m_proj.set_labels(file, after);
        e.m_after = snapshot(file);
        if(a.m_clear_done && m_proj.get_status(file) == file_status::done)
            m_proj.set_status(file, file_status::todo);
        if(step.m_file.empty())
            step = std::move(e);
        else
            step.m_more.push_back(std::move(e));
    }
    if(!step.m_file.empty())
        m_state.m_history.push(std::move(step));
    m_state.report_info(fmt::format(
        "Imported the review: {} labels changed, {} added, {} marked not an event, {} already as "
        "the review says; {} rows not applied.",
        plan.m_changed,
        plan.m_added,
        plan.m_rejected,
        plan.m_unchanged,
        plan.unapplied()));
}

void action_handler::operator()(const actions::jump_to &j)
{
    (*this)(actions::load_file{j.m_file});
    restore_active_audio();
    m_state.m_view_request = time_span{j.m_time_s - 2., j.m_time_s + 2.};
    if(const auto *l = find_label_at(m_proj.get_labels(j.m_file), j.m_time_s, 0.05))
    {
        m_state.set_selection({l->m_id});
        play_label(*l);
    }
    else
    {
        const label at{0U, label_defn_id{}, j.m_time_s, j.m_time_s};
        m_audio_engine.select_region(m_state.playback_window(at), m_state.m_loop);
        m_audio_engine.play();
    }
}
