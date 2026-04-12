/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include "../model/project_model.h"
#include <string>

class waveform_view
{
public:
    void render(project_model &project)
    {
        ImGui::Begin(m_window_name.c_str());
        ImGui::Button("This is the waveform view");
        ImGui::End();
    }

    std::string m_window_name{"Waveform View"};
};