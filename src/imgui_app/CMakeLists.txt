add_library(ImGuiApp imgui_app.cpp imgui_app.h)
add_library(AudioLabeller::ImGuiApp ALIAS ImGuiApp)

target_link_libraries(ImGuiApp PUBLIC imgui::imgui implot::implot)