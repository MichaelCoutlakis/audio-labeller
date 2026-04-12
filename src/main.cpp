/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include "app/audio_labelling_app.h"

int main()
{
    app_config cfg;
    cfg.title = "Audio Labeller Tool";
    cfg.m_docking = true;
    audio_labeller_app app(cfg);
    return app.run();
}