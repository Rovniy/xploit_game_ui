cmake_minimum_required(VERSION 3.28)

project(xploit_game_ui
    VERSION 0.1.0
    DESCRIPTION "HTML/CSS/JS game UI runtime for Unity (native core)"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# Unity and the V8 NuGet binaries use the dynamic CRT (/MD, /MDd).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(XGU_BUILD_TESTS "Build unit and golden tests" ON)
option(XGU_BUILD_TOOLS "Build xgu_cli and xgu_host" ON)
option(XGU_BUILD_UNITY_PLUGIN "Build the Unity plugin DLL (xploit_game_ui.dll)" ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# ---------------------------------------------------------------------------
# Dependencies (vcpkg manifest: native/vcpkg.json)
# ---------------------------------------------------------------------------
find_package(unofficial-skia CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Common compile settings
# ---------------------------------------------------------------------------
add_library(xgu_settings INTERFACE)
target_compile_features(xgu_settings INTERFACE cxx_std_20)
target_compile_definitions(xgu_settings INTERFACE
    UNICODE _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    XGU_VERSION_STRING="${PROJECT_VERSION}")
if(MSVC)
    target_compile_options(xgu_settings INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc /bigobj
        /wd4100 # unreferenced formal parameter
        /wd4201 # nameless struct/union (D3D12 headers)
    )
endif()
target_include_directories(xgu_settings INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# Skia wrapper target (adds the definitions the Ganesh/D3D headers expect)
# ---------------------------------------------------------------------------
add_library(xgu_skia INTERFACE)
target_link_libraries(xgu_skia INTERFACE
    unofficial::skia::modules::skparagraph
    unofficial::skia::modules::skshaper
    unofficial::skia::modules::skunicode_icu
    unofficial::skia::modules::skunicode_core
    unofficial::skia::skia)
target_compile_definitions(xgu_skia INTERFACE SK_GANESH SK_DIRECT3D)
target_link_libraries(xgu_skia INTERFACE d3d12 dxgi dxguid)

# ---------------------------------------------------------------------------
# Core: interfaces, view registry, logging, runtime singleton
# ---------------------------------------------------------------------------
add_library(xgu_core STATIC
    src/core/Log.cpp
    src/core/View.cpp
    src/core/ViewRegistry.cpp
    src/core/Runtime.cpp)
target_link_libraries(xgu_core PUBLIC xgu_settings xgu_skia)

# ---------------------------------------------------------------------------
# Render: Skia Ganesh D3D12 context, texture providers, test frame
# ---------------------------------------------------------------------------
add_library(xgu_render STATIC
    src/render/RenderSystem.cpp
    src/render/skia/D3D12GrContext.cpp
    src/render/skia/D3D12TextureProvider.cpp
    src/render/skia/CpuTextureProvider.cpp
    src/render/skia/TestFrame.cpp)
target_link_libraries(xgu_render PUBLIC xgu_core xgu_skia)

# Aggregate runtime (grows with each stage: html, dom, css, layout, js, bridge, input)
add_library(xgu_runtime INTERFACE)
target_link_libraries(xgu_runtime INTERFACE xgu_core xgu_render)

# The C ABI implementation is compiled into every host (DLL, tools, tests).
set(XGU_CAPI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/capi/CApi.cpp)

# ---------------------------------------------------------------------------
# Unity plugin DLL
# ---------------------------------------------------------------------------
if(XGU_BUILD_UNITY_PLUGIN)
    add_library(xploit_game_ui SHARED
        ${XGU_CAPI_SOURCES}
        src/platform/unity/UnityPlugin.cpp)
    target_compile_definitions(xploit_game_ui PRIVATE XGU_BUILDING_DLL)
    target_include_directories(xploit_game_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unity)
    target_link_libraries(xploit_game_ui PRIVATE xgu_runtime)
    set_target_properties(xploit_game_ui PROPERTIES
        OUTPUT_NAME xploit_game_ui
        PREFIX "")
endif()

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
if(XGU_BUILD_TOOLS)
    add_executable(xgu_cli
        ${XGU_CAPI_SOURCES}
        tools/xgu_cli/main.cpp)
    target_compile_definitions(xgu_cli PRIVATE XGU_STATIC)
    target_link_libraries(xgu_cli PRIVATE xgu_runtime)

    add_executable(xgu_host WIN32
        ${XGU_CAPI_SOURCES}
        src/platform/standalone/main.cpp)
    target_compile_definitions(xgu_host PRIVATE XGU_STATIC)
    target_link_libraries(xgu_host PRIVATE xgu_runtime)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(XGU_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    add_executable(xgu_tests
        ${XGU_CAPI_SOURCES}
        tests/unit/CApiTest.cpp
        tests/unit/ViewRegistryTest.cpp
        tests/unit/TestFrameTest.cpp)
    target_compile_definitions(xgu_tests PRIVATE
        XGU_STATIC
        XGU_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata")
    target_link_libraries(xgu_tests PRIVATE xgu_runtime GTest::gtest GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(xgu_tests)
endif()
