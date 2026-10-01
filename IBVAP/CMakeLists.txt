cmake_minimum_required(VERSION 3.25)

project(IBVAP
    VERSION 0.1.0
    DESCRIPTION "AI Border Surveillance System"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------
# OpenGL
# ---------------------------------------------------------

find_package(OpenGL REQUIRED)

# ---------------------------------------------------------
# OpenCV
# ---------------------------------------------------------
#
# OpenCV is currently used by the native ANPR pipeline.
# The WASM build does not depend on it, so do not force a
# Windows OpenCV installation into the Emscripten configure.

if(NOT EMSCRIPTEN)

    set(
        OpenCV_DIR
        "C:/opencv-4.5.5/build/install/x64/mingw/lib"
    )

    find_package(
        OpenCV 4.5.5
        EXACT
        CONFIG
        REQUIRED
    )

    message(
        STATUS
        "IBVAP: OpenCV version = ${OpenCV_VERSION}"
    )

endif()

# ---------------------------------------------------------
# Output directories
# ---------------------------------------------------------

set(
    CMAKE_RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
)

set(
    CMAKE_LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
)

set(
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/lib"
)

# ---------------------------------------------------------
# SDL3
# ---------------------------------------------------------

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL3"
    EXCLUDE_FROM_ALL
)

# ---------------------------------------------------------
# IBVAP
# ---------------------------------------------------------

add_subdirectory(src)