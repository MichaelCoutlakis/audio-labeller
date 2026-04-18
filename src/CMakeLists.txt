

add_subdirectory(imgui_app)

add_executable(AudioLabeller main.cpp
    "app/actions.h"
    "app/action_handler.cpp" "app/action_handler.h" 
    "app/app_state.h" "app/app_state.cpp" 
    "app/audio_labelling_app.h" "app/audio_labelling_app.cpp"
    "app/colours.h"

    "views/waveform_view.h" "views/waveform_view.cpp" 
    "views/labels_view.h" "views/labels_view.cpp"
    "views/label_controls_view.h" "views/label_controls_view.cpp"
    "views/file_list_view.h" "views/file_list_view.cpp" 
    
    "model/project_model.h" "model/project_model.cpp"
    "model/ordered_registry.h" 
    "model/label.h" 
    "model/types.h")


target_link_libraries(${PROJECT_NAME} PRIVATE 
    AudioLabeller::ImGuiApp 
    nlohmann_json::nlohmann_json 
    AudioFile::AudioFile)