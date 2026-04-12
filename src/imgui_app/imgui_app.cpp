/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include "imgui_app.h"

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "imgui.h"

#include <stdexcept>

#include <SDL3/SDL_opengl.h>

imgui_app::imgui_app(app_config config) :
    m_config(std::move(config))
{
}

void imgui_app::init_sdl()
{
    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        throw std::runtime_error(SDL_GetError());
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    m_window = SDL_CreateWindow(m_config.title.c_str(),
        m_config.m_width,
        m_config.m_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if(!m_window)
    {
        throw std::runtime_error(SDL_GetError());
    }

    m_gl_context = SDL_GL_CreateContext(m_window);
    if(!m_gl_context)
    {
        throw std::runtime_error(SDL_GetError());
    }

    SDL_GL_MakeCurrent(m_window, m_gl_context);
    SDL_GL_SetSwapInterval(m_config.m_vsync ? 1 : 0);
}

void imgui_app::init_imgui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if(m_config.m_docking)
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    if(m_config.m_viewports)
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(m_window, m_gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void imgui_app::begin_frame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void imgui_app::end_frame()
{
    ImGui::Render();

    glViewport(0, 0, m_config.m_width, m_config.m_height);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(m_window, m_gl_context);
    }

    SDL_GL_SwapWindow(m_window);
}

int imgui_app::run()
{
    init_sdl();
    init_imgui();

    on_init();

    while(m_running)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if(event.type == SDL_EVENT_QUIT)
                m_running = false;

            on_event(event);
        }

        begin_frame();

        on_menu_bar();
        on_frame();

        end_frame();
    }

    on_shutdown();
    shutdown();
    return 0;
}

void imgui_app::shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(m_gl_context);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
}
