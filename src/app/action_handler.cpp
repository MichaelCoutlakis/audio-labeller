/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#include "action_handler.h"
#include "actions.h"

action_handler::action_handler(project_model &project, app_state &state) :
    m_proj(project),
    m_state(state)
{
}

void action_handler::operator()(const actions::add_label &r)
{
    label l{get_next_label_id(), r.m_defn_id.value_or(label_defn_id{}), r.m_t.first, r.m_t.second};
    // If a class was selected in the label palette, apply that
    if(r.m_defn_id)
        m_proj.get_labels(r.m_file).add(l);

    // Otherwise leave it as an unlabeled selection:
    else
        m_state.m_unlabelled.add(l);

    // In either case, addition of a label / selection clears previous selections and adds the new
    // selection to the selection set:
    m_state.clear_selections();
    m_state.toggle_label_selection(l.m_id);
}

void action_handler::operator()(const actions::select_labels &s)
{
    auto hit = [x = s.m_x](const label &l) { return l.m_start_s <= x && x <= l.m_stop_s; };

    for(auto &l : m_state.m_unlabelled)
        if(hit(l))
        {
            if(!m_state.is_label_selected(l.m_id))
                m_state.m_active_label_defn.reset();

            m_state.toggle_label_selection(l.m_id);
        }

    if(!m_state.m_active_file)
        return;
    for(auto &l : m_proj.get_labels(m_state.m_active_file.value()))
        if(hit(l))
        {
            // Prevent the selected label palette from going out of sync with the selections:
            if(!m_state.is_label_selected(l.m_id) && l.m_defn_id != m_state.m_active_label_defn)
                m_state.m_active_label_defn.reset();

            m_state.toggle_label_selection(l.m_id);
        }
}
void action_handler::operator()(const actions::delete_label &d)
{
    m_state.m_unlabelled.erase(d.m_id);

    if(m_state.has_active_file())
        m_proj.get_labels(m_state.m_active_file.value()).erase(d.m_id);
}

void action_handler::operator()(const actions::assign_label_class &a)
{
    if(!m_state.has_active_file())
        return;
    for(auto &id : a.m_ids)
    {
        if(auto unlabelled = m_state.m_unlabelled.find_ptr(id))
        {
            auto l = *unlabelled;
            m_state.m_unlabelled.erase(unlabelled);
            l.m_defn_id = a.m_defn_id;
            m_proj.get_labels(m_state.m_active_file.value()).add(l);
        }
        if(auto l = m_proj.get_labels(m_state.m_active_file.value()).find_ptr(id))
            l->m_defn_id = a.m_defn_id;
    }
}
