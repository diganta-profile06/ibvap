cmake_minimum_required(VERSION 3.25)

project(IBVAP_Web
    VERSION 0.1.0
    DESCRIPTION "IBVAP WebAssembly Build"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "This CMake project must be configured with Emscripten."
    )
endif()

# ---------------------------------------------------------
# SDL3
# ---------------------------------------------------------

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/SDL3"
    "${CMAKE_BINARY_DIR}/SDL3"
    EXCLUDE_FROM_ALL
)

# ---------------------------------------------------------
# IBVAP WebAssembly
# ---------------------------------------------------------

add_executable(IBVAP_Web

    # -----------------------------------------------------
    # Main
    # -----------------------------------------------------

    "${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp"

    # -----------------------------------------------------
    # Application
    # -----------------------------------------------------

    "${CMAKE_CURRENT_SOURCE_DIR}/../src/app/Application.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/app/Application.hpp"

    # -----------------------------------------------------
    # Rendering
    # -----------------------------------------------------

    "${CMAKE_CURRENT_SOURCE_DIR}/../src/rendering/Renderer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/rendering/OpenGLRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/rendering/OpenGLRenderer.hpp"

    # -----------------------------------------------------
    # UI
    # -----------------------------------------------------

    "${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/UI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/UI.hpp"

    # -----------------------------------------------------
    # Core video types
    # -----------------------------------------------------

    "${CMAKE_CURRENT_SOURCE_DIR}/../src/video/Frame.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/video/VideoSource.hpp"

    # -----------------------------------------------------
    # Web video implementation
    # -----------------------------------------------------

    "${CMAKE_CURRENT_SOURCE_DIR}/video/WebVideoSource.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/video/WebVideoSource.hpp"

    "${CMAKE_CURRENT_SOURCE_DIR}/video/WebVideoSourceManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/video/WebVideoSourceManager.hpp"

    # -----------------------------------------------------
    # Dear ImGui
    # -----------------------------------------------------

    "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui-master/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui-master/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui-master/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui-master/imgui_widgets.cpp"

    "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui-master/backends/imgui_impl_sdl3.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui-master/backends/imgui_impl_opengl3.cpp"
)

# ---------------------------------------------------------
# Include directories
# ---------------------------------------------------------

target_include_directories(
    IBVAP_Web
    PRIVATE

        "${CMAKE_CURRENT_SOURCE_DIR}/../src"

        "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui-master"

        "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui-master/backends"

        "${CMAKE_CURRENT_SOURCE_DIR}/video"
)

# ---------------------------------------------------------
# SDL3
# ---------------------------------------------------------

target_link_libraries(
    IBVAP_Web
    PRIVATE
        SDL3::SDL3
)

# ---------------------------------------------------------
# WebAssembly / WebGL
# ---------------------------------------------------------

target_link_options(
    IBVAP_Web
    PRIVATE

        "-sUSE_WEBGL2=1"

        "-sALLOW_MEMORY_GROWTH=1"

        "-sASSERTIONS=1"

        "-sNO_EXIT_RUNTIME=1"

        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
)

# ---------------------------------------------------------
# Output
# ---------------------------------------------------------

set_target_properties(
    IBVAP_Web
    PROPERTIES

        OUTPUT_NAME "ibvap"

        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/bin"
)