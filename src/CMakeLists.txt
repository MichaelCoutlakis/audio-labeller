add_subdirectory(imgui_app)

# Model, file I/O and export: no UI or audio device dependencies, so it can be unit tested
add_library(audio_labeller_core STATIC
    "model/audio_buffer.h"
    "model/colour.h"
    "model/history.h"
    "model/label.h"
    "model/ordered_registry.h"
    "model/paths.h"
    "model/project_model.h" "model/project_model.cpp"
    "model/types.h"

    "io/audio_source.h" "io/audio_source.cpp"
    "io/source_id.h" "io/source_id.cpp"
)
target_include_directories(audio_labeller_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${DRLIBS_INCLUDE_DIRS}
)
target_link_libraries(audio_labeller_core PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_executable(AudioLabeller main.cpp
    "app/actions.h"
    "app/action_handler.cpp" "app/action_handler.h"
    "app/app_state.h" "app/app_state.cpp"
    "app/audio_labelling_app.h" "app/audio_labelling_app.cpp"
    "app/colours.h"

    "audio/audio_engine.h" "audio/audio_engine.cpp"
    "audio/playback_cursor.h"

    "views/audio_view.h" "views/audio_view.cpp"
    "views/waveform_view.h" "views/waveform_view.cpp"
    "views/labels_view.h" "views/labels_view.cpp"
    "views/label_controls_view.h" "views/label_controls_view.cpp"
    "views/file_list_view.h" "views/file_list_view.cpp"
    "views/shortcuts.h" "views/shortcuts.cpp"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    audio_labeller_core
    AudioLabeller::ImGuiApp
)
