add_executable(audio_labeller_tests
    "test_main.cpp"
    "test_utils.h"
    "test_assist.cpp"
    "test_auto_gain.cpp"
    "test_audio_engine.cpp"
    "test_audio_source.cpp"
    "test_export.cpp"
    "test_history.cpp"
    "test_interchange.cpp"
    "test_lanes.cpp"
    "test_manifest.cpp"
    "test_playback_cursor.cpp"
    "test_project_model.cpp"
    "test_review.cpp"

    # Playback is tested against SDL's dummy audio driver
    "${PROJECT_SOURCE_DIR}/src/audio/audio_engine.cpp"
)

target_include_directories(audio_labeller_tests PRIVATE ${DRLIBS_INCLUDE_DIRS})
target_link_libraries(audio_labeller_tests PRIVATE audio_labeller_core SDL3::SDL3 doctest::doctest)

audiolab_warnings(audio_labeller_tests)

add_test(NAME audio_labeller_tests COMMAND audio_labeller_tests)
