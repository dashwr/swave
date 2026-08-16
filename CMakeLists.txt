cmake_minimum_required(VERSION 3.25)
project(swave LANGUAGES CXX)

option(SWAVE_ENABLE_ONNXRUNTIME "Build ONNX Runtime CUDA/TensorRT session" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(GNUInstallDirs)

add_library(swave_core STATIC
    core/backend/video_backends.cpp
    core/inference/inference_session.cpp
    core/model/model_manifest.cpp
    core/pipeline/passthrough_pipeline.cpp
    core/pipeline/processing_pipeline.cpp
    core/source/synthetic_source.cpp
)
target_include_directories(swave_core PUBLIC .)

if (MSVC)
    target_compile_options(swave_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(swave_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (SWAVE_ENABLE_ONNXRUNTIME)
    set(SWAVE_ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime installation root")
    find_path(SWAVE_ORT_INCLUDE_DIR onnxruntime_cxx_api.h
        HINTS "${SWAVE_ONNXRUNTIME_ROOT}/include")
    find_library(SWAVE_ORT_LIBRARY onnxruntime
        HINTS "${SWAVE_ONNXRUNTIME_ROOT}/lib")
    if (NOT SWAVE_ORT_INCLUDE_DIR OR NOT SWAVE_ORT_LIBRARY)
        message(FATAL_ERROR "SWAVE_ENABLE_ONNXRUNTIME requires ONNX Runtime headers and library")
    endif()
    target_sources(swave_core PRIVATE core/inference/onnxruntime_session.cpp)
    target_include_directories(swave_core PRIVATE "${SWAVE_ORT_INCLUDE_DIR}")
    target_link_libraries(swave_core PRIVATE "${SWAVE_ORT_LIBRARY}")
    target_compile_definitions(swave_core PUBLIC SWAVE_HAS_ONNXRUNTIME=1)
endif()

add_executable(swave_type_tests tests/type_tests.cpp)
target_link_libraries(swave_type_tests PRIVATE swave_core)

add_executable(swave_cli apps/swave_cli.cpp)
target_link_libraries(swave_cli PRIVATE swave_core)

if (WIN32)
    set(SWAVE_CPPWINRT_ROOT "" CACHE PATH "Optional C++/WinRT include root")
    add_library(swave_windows STATIC platform/windows/window_capture.cpp)
    target_compile_features(swave_windows PRIVATE cxx_std_20)
    target_compile_definitions(swave_windows PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if (SWAVE_CPPWINRT_ROOT)
        target_include_directories(swave_windows PRIVATE "${SWAVE_CPPWINRT_ROOT}/include")
    endif()
    target_link_libraries(swave_windows PRIVATE swave_core d3d11 dxgi windowsapp)

    add_executable(swave_app WIN32 apps/swave_win32.cpp)
    target_compile_definitions(swave_app PRIVATE UNICODE _UNICODE)
    target_link_libraries(swave_app PRIVATE swave_windows swave_core user32 gdi32 shell32 windowsapp)
endif()

if (BUILD_TESTING)
    add_test(NAME swave_type_tests COMMAND swave_type_tests)
endif()

install(TARGETS swave_cli RUNTIME DESTINATION .)
install(FILES
    README.md
    THIRD_PARTY_NOTICES.txt
    DESTINATION .)
install(FILES
    docs/qa.md
    docs/README-WINDOWS.md
    docs/architecture.md
    docs/plan.md
    docs/flows.md
    docs/ui.md
    DESTINATION docs)
install(DIRECTORY models/ DESTINATION models OPTIONAL)
install(DIRECTORY cache/engines/ DESTINATION cache/engines OPTIONAL)

if (WIN32)
    install(TARGETS swave_app RUNTIME DESTINATION .)
    install(FILES apps/swave_app.manifest DESTINATION . RENAME swave_app.exe.manifest)
endif()
