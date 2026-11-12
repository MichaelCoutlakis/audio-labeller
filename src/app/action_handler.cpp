/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include <algorithm>

#include <spdlog/spdlog.h>

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
    m_proj.set_labels(file, s.m_labels);

    // Selections only exist for the active file:
    if(m_state.is_selected_file(file))
    {
        m_state.m_unlabelled = s.m_unlabelled;
        m_state.set_selection(s.m_selection);
    }
}

void action_handler::operator()(const actions::load_file &f)
{
    spdlog::info("loading file {}", generic_utf8(f.file_path));
    m_state.set_selected_file(f.file_path);

    m_audio_engine.set_audio_clip(m_state.get_audio_buffer_ptr());
    m_audio_engine.select_region(
        time_span{0, m_state.get_audio_buffer().duration_s()},
        m_state.m_loop);
}

void action_handler::operator()(const actions::add_label &r)
{
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
    auto hit = [&](const label &l)
    {
        return l.m_start_s <= s.m_x &&
            s.m_x <= l.m_stop_s &&
            lane_of(m_proj.m_label_dict, l.m_defn_id, m_state.m_class_lanes).contains(s.m_y);
    };
    std::vector<const label *> hits;

    for(const auto &l : m_state.m_unlabelled)
        if(hit(l))
        {
            if(!m_state.is_label_selected(l.m_id))
                m_state.m_active_label_defn.reset();

            if(m_state.toggle_label_selection(l.m_id))
                hits.push_back(&l);
        }

    if(!m_state.m_active_file)
        return;
    for(const auto &l : m_proj.get_labels(m_state.m_active_file.value()))
        if(hit(l))
        {
            // Prevent the selected label palette from going out of sync with the selections:
            if(!m_state.is_label_selected(l.m_id) && l.m_defn_id != m_state.m_active_label_defn)
                m_state.m_active_label_defn.reset();

            if(m_state.toggle_label_selection(l.m_id))
                hits.push_back(&l);
        }
    // Set the playback selection to the most recently selected region:
    if(!hits.empty())
    {
        time_span t{hits.back()->m_start_s, hits.back()->m_stop_s};
        m_audio_engine.select_region(t, m_state.m_loop);
        if(m_state.m_auto_start)
            m_audio_engine.play();
    }
}

void action_handler::operator()(const actions::toggle_label_selection &t)
{
    m_state.toggle_label_selection(t.m_id, t.m_clear_others);
}

void action_handler::operator()(const actions::clear_selection &c) { m_state.clear_selections(); }

void action_handler::assign_class(const std::set<label_id> &ids, label_defn_id defn_id)
{
    const auto &file = m_state.m_active_file.value();
    for(auto id : ids)
    {
        if(auto unlabelled = m_state.m_unlabelled.find_ptr(id))
        {
            auto l = *unlabelled;
            m_state.m_unlabelled.erase(unlabelled);
            l.m_defn_id = defn_id;
            m_proj.add_label(file, l);
        }
        else if(auto existing = m_proj.get_labels(file).find_ptr(id))
        {
            auto l = *existing;
            l.m_defn_id = defn_id;
            m_proj.update_label(file, l);
        }
    }
}

void action_handler::operator()(const actions::set_active_class &a)
{
    m_state.m_active_label_defn = a.m_defn_id;
    if(!a.m_defn_id || !m_state.has_active_file())
        return;

    undoable(
        m_state.m_active_file.value(),
        [&] { assign_class(m_state.selected_labels(), a.m_defn_id.value()); });
}

void action_handler::operator()(const actions::delete_labels &d)
{
    if(!m_state.has_active_file())
        return;

    const auto &file = m_state.m_active_file.value();
    undoable(
        file,
        [&]
        {
            for(auto id : d.m_ids)
            {
                m_state.m_unlabelled.erase(id);
                m_proj.erase_label(file, id);
                m_state.deselect(id);
            }
        });
}

void action_handler::operator()(const actions::move_label &m)
{
    if(!m_state.has_active_file())
        return;

    const auto &file = m_state.m_active_file.value();
    const double duration_s = m_state.get_audio_buffer().duration_s();
    const double start = std::clamp(std::min(m.m_start_s, m.m_stop_s), 0., duration_s);
    const double stop = std::clamp(std::max(m.m_start_s, m.m_stop_s), 0., duration_s);

    undoable(
        file,
        [&]
        {
            if(auto *l = m_state.m_unlabelled.find_ptr(m.m_id))
            {
                l->m_start_s = start;
                l->m_stop_s = stop;
            }
            else if(auto *existing = m_proj.get_labels(file).find_ptr(m.m_id))
            {
                auto l = *existing;
                l.m_start_s = start;
                l.m_stop_s = stop;
                m_proj.update_label(file, l);
            }
        },
        history::merge_key{m.m_drag_seq, 0U});
}

void action_handler::operator()(const actions::undo &u)
{
    if(auto entry = m_state.m_history.undo())
        restore(entry->m_file, entry->m_before);
}

void action_handler::operator()(const actions::redo &r)
{
    if(auto entry = m_state.m_history.redo())
        restore(entry->m_file, entry->m_after);
}

void action_handler::operator()(const actions::set_file_status &s)
{
    m_proj.set_status(s.m_file, s.m_status);
}

void action_handler::operator()(const actions::select_playback_device &dev)
{
    m_audio_engine.set_device(dev.dev);
    m_state.settings.m_device_name = dev.dev.name;
}

void action_handler::operator()(const actions::select_playback_region &r)
{
    m_audio_engine.select_region(r.t, r.loop);
    if(m_state.m_auto_start)
        m_audio_engine.play();
}

void action_handler::operator()(const actions::toggle_playback &a)
{
    if(m_audio_engine.get_state().playing)
        m_audio_engine.stop();
    else
        m_audio_engine.play();
}

void action_handler::operator()(const actions::playback_loop &p)
{
    m_audio_engine.set_playback_loop(p.loop);
}

void action_handler::operator()(const actions::rescan_files &r) { m_proj.rescan_files(); }

void action_handler::operator()(const actions::export_csv &e) { m_proj.export_csv(); }
