/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include "../audio/audio_engine.h"
#include "../model/project_model.h"
#include "actions.h"
#include "app_state.h"

struct action_handler
{
    action_handler(audio_engine &aud_eng, project_model &project, app_state &state);

    void operator()(const actions::load_file &f);

    void operator()(const actions::add_label &r);

    void operator()(const actions::select_labels &s);

    void operator()(const actions::delete_label &d);

    void operator()(const actions::assign_label_class &a);

    void operator()(const actions::select_playback_device &dev);

    void operator()(const actions::select_playback_region &r);

    void operator()(const actions::toggle_playback &a);

private:
    audio_engine &m_audio_engine;
    project_model &m_proj;
    app_state &m_state;
};
