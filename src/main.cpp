/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

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
        cfg.m_nav_keyboard = false; // The app's own shortcuts use the arrows, Tab and Space

        // System fonts with wide coverage, for file names in any language
#ifdef _WIN32
        const char *windir = std::getenv("WINDIR");
        const std::string fonts = std::string{windir ? windir : "C:/Windows"} + "/Fonts/";
        cfg.m_fonts = {fonts + "segoeui.ttf", fonts + "arial.ttf"};
#elif defined(__APPLE__)
        cfg.m_fonts = {
            "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
            "/Library/Fonts/Arial Unicode.ttf"
        };
#else
        cfg.m_fonts = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf"
        };
#endif
        audio_labeller_app app(cfg);
        return app.run();
    }
    catch(const std::exception &e)
    {
        spdlog::critical("Fatal error: {}", e.what());
        return 1;
    }
}
