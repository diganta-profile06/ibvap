# =========================================================
# IBVAP executable
# =========================================================

# ---------------------------------------------------------
# QR Code library
# ---------------------------------------------------------

set_source_files_properties(
    "${CMAKE_SOURCE_DIR}/external/qrcode/qrcode.c"
    PROPERTIES
        LANGUAGE CXX
)

# =========================================================
# Executable sources
# =========================================================

add_executable(IBVAP

    main.cpp

    # -----------------------------------------------------
    # Application
    # -----------------------------------------------------

    app/Application.cpp
    app/Application.hpp

    # -----------------------------------------------------
    # Rendering
    # -----------------------------------------------------

    rendering/Renderer.hpp
    rendering/OpenGLRenderer.cpp
    rendering/OpenGLRenderer.hpp

    # -----------------------------------------------------
    # UI
    # -----------------------------------------------------

    ui/UI.cpp
    ui/UI.hpp

    # -----------------------------------------------------
    # Video base
    # -----------------------------------------------------

    video/Frame.hpp
    video/VideoSource.hpp

    # -----------------------------------------------------
    # AI
    # -----------------------------------------------------

    ai/AIFrame.hpp
    ai/AIFrameEnhancement.cpp
    ai/AIFrameEnhancement.hpp
    ai/AIResult.hpp
    ai/AIEngine.cpp
    ai/AIEngine.hpp

    # -----------------------------------------------------
    # Database
    # -----------------------------------------------------

    database/Database.cpp
    database/Database.hpp

    # -----------------------------------------------------
    # Object Detection
    # -----------------------------------------------------

    ai/detection/objectdetector.cpp
    ai/detection/objectdetector.hpp

    # -----------------------------------------------------
    # Face Detection
    # -----------------------------------------------------

    ai/face/FaceDetector.cpp
    ai/face/FaceDetector.hpp

    # -----------------------------------------------------
    # ANPR / License Plate Detection
    # -----------------------------------------------------

    ai/anpr/LicensePlateDetector.cpp
    ai/anpr/LicensePlateDetector.hpp

    # -----------------------------------------------------
    # QR Code
    # -----------------------------------------------------

    "${CMAKE_SOURCE_DIR}/external/qrcode/qrcode.c"

    # -----------------------------------------------------
    # Dear ImGui
    # -----------------------------------------------------

    "${CMAKE_SOURCE_DIR}/external/imgui-master/imgui.cpp"
    "${CMAKE_SOURCE_DIR}/external/imgui-master/imgui_draw.cpp"
    "${CMAKE_SOURCE_DIR}/external/imgui-master/imgui_tables.cpp"
    "${CMAKE_SOURCE_DIR}/external/imgui-master/imgui_widgets.cpp"

    "${CMAKE_SOURCE_DIR}/external/imgui-master/backends/imgui_impl_sdl3.cpp"
    "${CMAKE_SOURCE_DIR}/external/imgui-master/backends/imgui_impl_opengl3.cpp"
)

# =========================================================
# Platform-specific video implementation
# =========================================================

if(EMSCRIPTEN)

    # -----------------------------------------------------
    # WebAssembly video layer
    # -----------------------------------------------------

    target_sources(
        IBVAP
        PRIVATE

            "${CMAKE_SOURCE_DIR}/web/video/WebVideoSource.cpp"
            "${CMAKE_SOURCE_DIR}/web/video/WebVideoSource.hpp"

            "${CMAKE_SOURCE_DIR}/web/video/WebVideoSourceManager.cpp"
            "${CMAKE_SOURCE_DIR}/web/video/WebVideoSourceManager.hpp"
    )

else()

    # -----------------------------------------------------
    # Native video
    # -----------------------------------------------------

    target_sources(
        IBVAP
        PRIVATE

            video/CameraVideoSource.cpp
            video/CameraVideoSource.hpp

            video/LocalVideoSource.cpp
            video/LocalVideoSource.hpp

            video/RTSPVideoSource.cpp
            video/RTSPVideoSource.hpp

            video/VideoSourceManager.cpp
            video/VideoSourceManager.hpp

            # -------------------------------------------------
            # Mobile Stream
            # -------------------------------------------------

            mobile/MobileNetwork.cpp
            mobile/MobileNetwork.hpp

            mobile/MobileStreamServer.cpp
            mobile/MobileStreamServer.hpp

            mobile/MobileStreamSource.cpp
            mobile/MobileStreamSource.hpp
    )

endif()

# =========================================================
# Native-only dependencies
# =========================================================

if(NOT EMSCRIPTEN)

    # -------------------------------------------------------
    # Crow
    # -------------------------------------------------------

    set(
        CROW_ENABLE_SSL
        ON
        CACHE BOOL
        "Enable SSL support in Crow"
        FORCE
    )

    set(
        CROW_BUILD_EXAMPLES
        OFF
        CACHE BOOL
        "Disable Crow examples"
        FORCE
    )

    set(
        CROW_BUILD_TESTS
        OFF
        CACHE BOOL
        "Disable Crow tests"
        FORCE
    )

    add_subdirectory(
        "C:/Libraries/Crow-1.3.3"
        "${CMAKE_BINARY_DIR}/crow"
        EXCLUDE_FROM_ALL
    )

    # -------------------------------------------------------
    # FFmpeg
    # -------------------------------------------------------

    find_package(PkgConfig REQUIRED)

    pkg_check_modules(
        FFMPEG
        REQUIRED
        libavcodec
        libavutil
        libswscale
    )

    set(
        IBVAP_FFMPEG_ROOT
        "C:/msys64/ucrt64"
    )

    set(
        IBVAP_FFMPEG_INCLUDE_DIR
        "${IBVAP_FFMPEG_ROOT}/include"
    )

    set(
        IBVAP_FFMPEG_LIBRARY_DIR
        "${IBVAP_FFMPEG_ROOT}/lib"
    )

    set(
        IBVAP_FFMPEG_AVCODEC
        "${IBVAP_FFMPEG_LIBRARY_DIR}/libavcodec.dll.a"
    )

    set(
        IBVAP_FFMPEG_AVUTIL
        "${IBVAP_FFMPEG_LIBRARY_DIR}/libavutil.dll.a"
    )

    set(
        IBVAP_FFMPEG_SWSCALE
        "${IBVAP_FFMPEG_LIBRARY_DIR}/libswscale.dll.a"
    )

    # -------------------------------------------------------
    # FFmpeg include directories
    # -------------------------------------------------------

    target_include_directories(
        IBVAP
        PRIVATE

            ${FFMPEG_INCLUDE_DIRS}
            "${IBVAP_FFMPEG_INCLUDE_DIR}"
    )

    # -------------------------------------------------------
    # FFmpeg library directory
    # -------------------------------------------------------

    target_link_directories(
        IBVAP
        PRIVATE

            "${IBVAP_FFMPEG_LIBRARY_DIR}"
    )

endif()

# =========================================================
# ncnn AI inference runtime
# =========================================================

if(EMSCRIPTEN)

    # -------------------------------------------------------
    # WebAssembly ncnn
    # -------------------------------------------------------

    set(
        ncnn_DIR
        "C:/Users/Abhranil/Downloads/ncnn/webassembly/ncnn-20260526-webassembly/simd/lib/cmake/ncnn"
        CACHE PATH
        "Directory containing the WebAssembly ncnnConfig.cmake"
        FORCE
    )

else()

    # -------------------------------------------------------
    # Native Windows ncnn
    # -------------------------------------------------------

    set(
        ncnn_DIR
        "C:/Users/Abhranil/Downloads/ncnn/native-install/lib/cmake/ncnn"
        CACHE PATH
        "Directory containing the native ncnnConfig.cmake"
        FORCE
    )

endif()

find_package(
    ncnn
    CONFIG
    REQUIRED
)

# ---------------------------------------------------------
# ncnn include directory
# ---------------------------------------------------------

target_include_directories(
    IBVAP
    PRIVATE

        "${ncnn_DIR}/../../../include"
)

message(
    STATUS
    "IBVAP: ncnn found"
)

# =========================================================
# Include directories
# =========================================================

target_include_directories(
    IBVAP
    PRIVATE

        "${CMAKE_CURRENT_SOURCE_DIR}"

        "${CMAKE_SOURCE_DIR}/external/imgui-master"
        "${CMAKE_SOURCE_DIR}/external/imgui-master/backends"

        "${CMAKE_SOURCE_DIR}/external/qrcode"
)

# ---------------------------------------------------------
# Native-only include directories
# ---------------------------------------------------------

if(NOT EMSCRIPTEN)

    target_include_directories(
        IBVAP
        PRIVATE

            "C:/Libraries/Crow-1.3.3/include"

            "C:/opencv-4.5.5/build/install/x64/mingw/include"
    )

endif()

# =========================================================
# SQLite
# =========================================================

if(EMSCRIPTEN)

    # Emscripten provides the SQLite3 port.  Keep the same
    # sqlite3 C API and schema as the native build.
    target_compile_options(
        IBVAP
        PRIVATE
            "-sUSE_SQLITE3=1"
            "-sFORCE_FILESYSTEM=1"
            "-sASYNCIFY=1"
    )

    target_link_options(
        IBVAP
        PRIVATE
            "-sUSE_SQLITE3=1"
            "-sFORCE_FILESYSTEM=1"
            "-sASYNCIFY=1"
            "-lidbfs.js"
    )

else()

    find_package(PkgConfig REQUIRED)

    pkg_check_modules(
        SQLITE3
        REQUIRED
        sqlite3
    )

    target_include_directories(
        IBVAP
        PRIVATE
            ${SQLITE3_INCLUDE_DIRS}
    )

    target_link_directories(
        IBVAP
        PRIVATE
            ${SQLITE3_LIBRARY_DIRS}
    )

endif()

# =========================================================
# Libraries
# =========================================================

if(EMSCRIPTEN)

    # -------------------------------------------------------
    # WebAssembly
    # -------------------------------------------------------

    target_link_libraries(
        IBVAP
        PRIVATE

            SDL3::SDL3
            OpenGL::GL

            ncnn
    )

else()

    # -------------------------------------------------------
    # Native
    # -------------------------------------------------------

    target_link_libraries(
        IBVAP
        PRIVATE

            SDL3::SDL3
            OpenGL::GL
            ${OpenCV_LIBS}
            Crow::Crow

            ncnn
            ${SQLITE3_LIBRARIES}

            "-Wl,--start-group"

            "${IBVAP_FFMPEG_AVCODEC}"
            "${IBVAP_FFMPEG_SWSCALE}"
            "${IBVAP_FFMPEG_AVUTIL}"

            "-Wl,--end-group"
    )

endif()

# =========================================================
# Windows networking
# =========================================================

if(WIN32 AND NOT EMSCRIPTEN)

    target_link_libraries(
        IBVAP
        PRIVATE

            ws2_32
            mswsock
            iphlpapi
    )

endif()

# =========================================================
# OpenSSL
# =========================================================

if(NOT EMSCRIPTEN)

    find_package(OpenSSL REQUIRED)

    target_link_libraries(
        IBVAP
        PRIVATE

            OpenSSL::SSL
            OpenSSL::Crypto
    )

endif()

# =========================================================
# OpenCV runtime DLLs
# =========================================================

if(WIN32 AND NOT EMSCRIPTEN)

    set(
        IBVAP_OPENCV_BIN_DIR
        "C:/opencv-4.5.5/build/install/x64/mingw/bin"
    )

    file(
        GLOB
        IBVAP_OPENCV_DLLS
        "${IBVAP_OPENCV_BIN_DIR}/*.dll"
    )

    add_custom_command(
        TARGET IBVAP
        POST_BUILD

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${IBVAP_OPENCV_DLLS}
            "$<TARGET_FILE_DIR:IBVAP>"

        COMMENT
            "Copying OpenCV runtime DLLs beside IBVAP.exe"
    )

endif()

# =========================================================
# FFmpeg runtime DLLs
# =========================================================

if(WIN32 AND NOT EMSCRIPTEN)

    set(
        FFMPEG_PREFIX
        "${IBVAP_FFMPEG_ROOT}"
    )

    if(
        EXISTS
            "${FFMPEG_PREFIX}/bin"
    )

        file(
            GLOB
            IBVAP_FFMPEG_DLLS

            "${FFMPEG_PREFIX}/bin/av*.dll"
            "${FFMPEG_PREFIX}/bin/swscale-*.dll"
            "${FFMPEG_PREFIX}/bin/swresample-*.dll"
        )

        add_custom_command(
            TARGET IBVAP
            POST_BUILD

            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${IBVAP_FFMPEG_DLLS}
                "$<TARGET_FILE_DIR:IBVAP>"

            COMMENT
                "Copying FFmpeg runtime DLLs beside IBVAP.exe"
        )

    endif()

endif()

# =========================================================
# Mobile web application
# =========================================================

if(WIN32 AND NOT EMSCRIPTEN)

    add_custom_command(
        TARGET IBVAP
        POST_BUILD

        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:IBVAP>/mobile"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/mobile/index.html"
            "$<TARGET_FILE_DIR:IBVAP>/mobile/index.html"

        COMMENT
            "Copying IBVAP mobile web application"
    )

endif()

# =========================================================
# Native AI model assets
# =========================================================
#
# YOLOv8s:
#   Person / vehicle / object detection
#
# SCRFD:
#   Face detection
#
# PP-OCRv5:
#   Text / license-plate candidate detection
#
# Native Windows packages all three model groups.
#
# WebAssembly model packaging remains separate and is
# intentionally not handled here.
#
# =========================================================

if(WIN32 AND NOT EMSCRIPTEN)

    # -------------------------------------------------------
    # YOLOv8s model
    # -------------------------------------------------------

    set(
        IBVAP_YOLO_PARAM
        "${CMAKE_SOURCE_DIR}/models/detection/yolov8s.ncnn.param"
    )

    set(
        IBVAP_YOLO_BIN
        "${CMAKE_SOURCE_DIR}/models/detection/yolov8s.ncnn.bin"
    )

    # -------------------------------------------------------
    # Verify YOLO model
    # -------------------------------------------------------

    if(NOT EXISTS "${IBVAP_YOLO_PARAM}")

        message(
            FATAL_ERROR
            "IBVAP: missing YOLO parameter file: ${IBVAP_YOLO_PARAM}"
        )

    endif()

    if(NOT EXISTS "${IBVAP_YOLO_BIN}")

        message(
            FATAL_ERROR
            "IBVAP: missing YOLO model file: ${IBVAP_YOLO_BIN}"
        )

    endif()

    # -------------------------------------------------------
    # SCRFD model paths
    # -------------------------------------------------------

    set(
        IBVAP_SCRFD_PARAM
        "${CMAKE_SOURCE_DIR}/models/face/scrfd_500m-opt2.param"
    )

    set(
        IBVAP_SCRFD_BIN
        "${CMAKE_SOURCE_DIR}/models/face/scrfd_500m-opt2.bin"
    )

    # -------------------------------------------------------
    # Verify SCRFD model
    # -------------------------------------------------------

    if(NOT EXISTS "${IBVAP_SCRFD_PARAM}")

        message(
            FATAL_ERROR
            "IBVAP: missing SCRFD parameter file: ${IBVAP_SCRFD_PARAM}"
        )

    endif()

    if(NOT EXISTS "${IBVAP_SCRFD_BIN}")

        message(
            FATAL_ERROR
            "IBVAP: missing SCRFD model file: ${IBVAP_SCRFD_BIN}"
        )

    endif()

    # -------------------------------------------------------
    # ANPR / PP-OCRv5 model paths
    # -------------------------------------------------------

    set(
        IBVAP_ANPR_PARAM
        "${CMAKE_SOURCE_DIR}/models/anpr/pp_OCRv5_mobile_det.ncnn.param"
    )

    set(
        IBVAP_ANPR_BIN
        "${CMAKE_SOURCE_DIR}/models/anpr/pp_OCRv5_mobile_det.ncnn.bin"
    )

    # -------------------------------------------------------
    # Verify ANPR model
    # -------------------------------------------------------

    if(NOT EXISTS "${IBVAP_ANPR_PARAM}")

        message(
            FATAL_ERROR
            "IBVAP: missing ANPR parameter file: ${IBVAP_ANPR_PARAM}"
        )

    endif()

    if(NOT EXISTS "${IBVAP_ANPR_BIN}")

        message(
            FATAL_ERROR
            "IBVAP: missing ANPR model file: ${IBVAP_ANPR_BIN}"
        )

    endif()

    # -------------------------------------------------------
    # Model status
    # -------------------------------------------------------

    message(
        STATUS
        "IBVAP: YOLOv8s model found"
    )

    message(
        STATUS
        "IBVAP: SCRFD face detector model found"
    )

    message(
        STATUS
        "IBVAP: PP-OCRv5 ANPR detector model found"
    )

    # =====================================================
    # Native AI model packaging
    # =====================================================

    add_custom_command(
        TARGET IBVAP
        POST_BUILD

        # ---------------------------------------------------
        # Create complete model directory hierarchy
        # ---------------------------------------------------

        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:IBVAP>/models"

        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:IBVAP>/models/detection"

        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:IBVAP>/models/face"

        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:IBVAP>/models/anpr"

        # ---------------------------------------------------
        # YOLOv8s
        # ---------------------------------------------------

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${IBVAP_YOLO_PARAM}"
            "$<TARGET_FILE_DIR:IBVAP>/models/detection/yolov8s.ncnn.param"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${IBVAP_YOLO_BIN}"
            "$<TARGET_FILE_DIR:IBVAP>/models/detection/yolov8s.ncnn.bin"

        # ---------------------------------------------------
        # SCRFD 500M
        # ---------------------------------------------------

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${IBVAP_SCRFD_PARAM}"
            "$<TARGET_FILE_DIR:IBVAP>/models/face/scrfd_500m-opt2.param"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${IBVAP_SCRFD_BIN}"
            "$<TARGET_FILE_DIR:IBVAP>/models/face/scrfd_500m-opt2.bin"

        # ---------------------------------------------------
        # PP-OCRv5 mobile detector
        # ---------------------------------------------------

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${IBVAP_ANPR_PARAM}"
            "$<TARGET_FILE_DIR:IBVAP>/models/anpr/pp_OCRv5_mobile_det.ncnn.param"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${IBVAP_ANPR_BIN}"
            "$<TARGET_FILE_DIR:IBVAP>/models/anpr/pp_OCRv5_mobile_det.ncnn.bin"

        COMMENT
            "Copying native AI model assets"
    )

endif()

# =========================================================
# Emscripten WebGL 2
# =========================================================

if(EMSCRIPTEN)

    target_link_options(
        IBVAP
        PRIVATE

            "-sMIN_WEBGL_VERSION=2"
            "-sMAX_WEBGL_VERSION=2"

            "-sALLOW_MEMORY_GROWTH=1"

            "-sASSERTIONS=1"

            "-sNO_EXIT_RUNTIME=1"

            "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    )

endif()

# =========================================================
# Emscripten HTML launcher
# =========================================================

if(EMSCRIPTEN)

    set_target_properties(
        IBVAP
        PROPERTIES
            SUFFIX ".html"
    )

endif()
