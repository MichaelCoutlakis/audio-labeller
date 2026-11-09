/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <exception>

#include <spdlog/spdlog.h>

#include "app/audio_labelling_app.h"

int main()
{
    // Catching here guarantees the stack unwinds, so the app's destructors get to save the labels
    try
    {
        app_config cfg;
        cfg.title = "Audio Labeller Tool";
        cfg.m_docking = true;
        audio_labeller_app app(cfg);
        return app.run();
    }
    catch(const std::exception &e)
    {
        spdlog::critical("Fatal error: {}", e.what());
        return 1;
    }
}
