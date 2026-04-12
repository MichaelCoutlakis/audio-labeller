/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/

#pragma once

#include <SDL3/SDL.h>
#include <string>

/// App configuration, \sa imgui_app
struct app_config
{
    std::string title{"ImGui Ap"};
    int m_width = 1600;
    int m_height = 900;
    bool m_docking = true;
    bool m_viewports = false;
    bool m_vsync = true;
};

/// A generic ImGui app to hide the boiler-plate
class imgui_app
{
public:
    explicit imgui_app(app_config config);
    virtual ~imgui_app() { }

    int run();
    void request_quit() { m_running = false; }

protected:
    virtual void on_init() { }
    virtual void on_frame() = 0;
    virtual void on_shutdown() { }

    virtual void on_event(const SDL_Event &) { }
    virtual void on_menu_bar() { }

private:
    void init_sdl();
    void init_opengl();
    void init_imgui();

    void begin_frame();
    void end_frame();
    void shutdown();

private:
    app_config m_config;
    bool m_running = true;

    SDL_Window *m_window{nullptr};
    SDL_GLContext m_gl_context{nullptr};
};
