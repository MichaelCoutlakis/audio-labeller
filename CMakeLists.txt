################################################################################
# Audio Labelling Tool
# Top level dependency and build configuration
################################################################################

cmake_minimum_required(VERSION 3.22)

project(AudioLabeller
    VERSION 0.1.0
    LANGUAGES CXX C
)

################################################################################
# Global settings
################################################################################
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AUDIOLAB_ENABLE_WARNINGS "Enable strict warnings for the app's own code" ON)
option(AUDIOLAB_BUILD_TESTS "Build the unit tests" ON)
option(AUDIOLAB_SYSTEM_DEPS
    "Find installed dependencies (eg. from vcpkg or the system's packages) rather than building them from external/"
    OFF)
# SDL alone can be chosen separately, following AUDIOLAB_SYSTEM_DEPS by default
if(AUDIOLAB_SYSTEM_DEPS)
    set(audiolab_sdl_default OFF)
else()
    set(audiolab_sdl_default ON)
endif()
option(AUDIOLAB_SDL_SUBMODULE "Build SDL3 from external/SDL, rather than find an installed one"
    ${audiolab_sdl_default})

# Warnings for the app's own targets only, not the dependencies
function(audiolab_warnings target)
    if(NOT AUDIOLAB_ENABLE_WARNINGS)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    endif()
endfunction()


################################################################################
# Dependencies: built from the submodules in external/ by default, so that a
# fresh clone builds without a package manager, or all found installed with
# AUDIOLAB_SYSTEM_DEPS. Dear ImGui and ImPlot always come from external/, as
# they're built with the SDL3 and OpenGL backends.
################################################################################
# OpenGL: always the system's
find_package(OpenGL REQUIRED)

function(audiolab_require_submodule dir)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/${dir}/.git)
        message(FATAL_ERROR
            "external/${dir} is missing: run git submodule update --init --recursive, "
            "or configure with -DAUDIOLAB_SYSTEM_DEPS=ON to use installed dependencies")
    endif()
endfunction()

if(AUDIOLAB_SYSTEM_DEPS)
    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_path(DRLIBS_INCLUDE_DIRS "dr_wav.h" REQUIRED)
    if(AUDIOLAB_BUILD_TESTS)
        find_package(doctest CONFIG REQUIRED)
    endif()
else()
    # nlohmann/json: header only
    audiolab_require_submodule(json)
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    add_subdirectory(external/json EXCLUDE_FROM_ALL)

    # spdlog, compiled, with the copy of fmt it bundles
    audiolab_require_submodule(spdlog)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE INTERNAL "")
    set(SPDLOG_BUILD_TESTS OFF CACHE INTERNAL "")
    set(SPDLOG_INSTALL OFF CACHE INTERNAL "")
    add_subdirectory(external/spdlog EXCLUDE_FROM_ALL)

    # dr_libs: single headers (dr_wav, dr_flac, dr_mp3)
    audiolab_require_submodule(dr_libs)
    set(DRLIBS_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/external/dr_libs)

    # doctest: header only, for the tests
    if(AUDIOLAB_BUILD_TESTS)
        audiolab_require_submodule(doctest)
        set(DOCTEST_NO_INSTALL ON CACHE INTERNAL "")
        set(DOCTEST_WITH_TESTS OFF CACHE INTERNAL "")
        set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE INTERNAL "")
        add_subdirectory(external/doctest EXCLUDE_FROM_ALL)
    endif()
endif()

# SDL3: from the submodule, or installed (eg. vcpkg's sdl3)
if(AUDIOLAB_SDL_SUBMODULE)
    audiolab_require_submodule(SDL)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    add_subdirectory(external/SDL EXCLUDE_FROM_ALL)
else()
    find_package(SDL3 CONFIG REQUIRED)
endif()


### ImGUI
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
add_library(imgui::imgui ALIAS imgui)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(imgui PUBLIC
    SDL3::SDL3
    OpenGL::GL
)

### Implot
set(IMPLOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/implot)

add_library(implot STATIC
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)
add_library(implot::implot ALIAS implot)

target_include_directories(implot PUBLIC
    ${IMPLOT_DIR}
)

target_link_libraries(implot PUBLIC imgui)

################################################################################
## Application source
################################################################################
add_subdirectory(src)

if(AUDIOLAB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
