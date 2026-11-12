add_executable(audio_labeller_tests
    "test_main.cpp"
    "test_utils.h"
    "test_audio_engine.cpp"
    "test_audio_source.cpp"
    "test_history.cpp"
    "test_lanes.cpp"
    "test_playback_cursor.cpp"
    "test_project_model.cpp"

    # Playback is tested against SDL's dummy audio driver
    "${PROJECT_SOURCE_DIR}/src/audio/audio_engine.cpp"
)

target_include_directories(audio_labeller_tests PRIVATE ${DRLIBS_INCLUDE_DIRS})
target_link_libraries(audio_labeller_tests PRIVATE audio_labeller_core SDL3::SDL3 doctest::doctest)

add_test(NAME audio_labeller_tests COMMAND audio_labeller_tests)
