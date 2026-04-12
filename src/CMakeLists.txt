

add_subdirectory(imgui_app)

add_executable(AudioLabeller main.cpp
    "app/audio_labelling_app.h" "app/audio_labelling_app.cpp"

 "views/file_list_view.h" "views/waveform_view.h" "views/label_controls_view.h" "model/project_model.h")





target_link_libraries(${PROJECT_NAME} PRIVATE AudioLabeller::ImGuiApp)