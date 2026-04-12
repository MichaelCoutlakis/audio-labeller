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
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AUDIOLAB_ENABLE_WARNINGS "Enable strict warnings" ON)


################################################################################
# Dependencies:
################################################################################


################################################################################
# Dependencies which must be available on the system, e.g. via a package
# manager like vcpkg or other
################################################################################
# OpenGL
find_package(OpenGL REQUIRED)

# Nlohmann::json
find_package(nlohmann_json CONFIG REQUIRED)

################################################################################
# Dependencies which are managed as submodules:
################################################################################
# SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(external/SDL EXCLUDE_FROM_ALL)


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