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
file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

option(XGU_BUILD_TESTS "Build unit and golden tests" ON)
option(XGU_BUILD_TOOLS "Build xgu_cli and xgu_host" ON)
option(XGU_BUILD_UNITY_PLUGIN "Build the Unity plugin DLL (xploit_game_ui.dll)" ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# ---------------------------------------------------------------------------
# Dependencies (vcpkg manifest: native/vcpkg.json; V8 via NuGet)
# ---------------------------------------------------------------------------
find_package(unofficial-skia CONFIG REQUIRED)
find_package(lexbor CONFIG REQUIRED)
find_package(yoga CONFIG REQUIRED)
include(FetchV8Nuget) # -> xgu_v8, XGU_V8_RUNTIME_FILES, xgu_add_v8_runtime_copy()

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
        /external:W0 # silence warnings from SYSTEM includes (V8, Skia)
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
# Core: interfaces, view registry, logging, runtime thread
# ---------------------------------------------------------------------------
add_library(xgu_core STATIC
    src/bridge/QueueBridge.cpp
    src/core/Atom.cpp
    src/core/AssetLoader.cpp
    src/core/Log.cpp
    src/core/ViewRegistry.cpp
    src/core/RuntimeThread.cpp)
target_link_libraries(xgu_core PUBLIC xgu_settings xgu_skia)

# ---------------------------------------------------------------------------
# DOM: own node tree, HTML parsing through lexbor, selector matching
# ---------------------------------------------------------------------------
# dom/, html/ and css/Selector* are mutually dependent (innerHTML parses, the
# parser builds nodes, selectors inspect them), so they form one target.
add_library(xgu_dom STATIC
    src/dom/Node.cpp
    src/dom/Element.cpp
    src/dom/Document.cpp
    src/dom/Event.cpp
    src/dom/EventTarget.cpp
    src/dom/TextControl.cpp
    src/dom/Serializer.cpp
    src/html/HtmlTags.cpp
    src/html/LexborHtmlParser.cpp
    src/css/Selector.cpp
    src/css/SelectorMatcher.cpp
    src/css/Properties.cpp
    src/css/CssValue.cpp
    src/css/Tokenizer.cpp
    src/css/Parser.cpp
    src/css/ComputedStyle.cpp
    src/css/StyleEngine.cpp)
target_link_libraries(xgu_dom PUBLIC xgu_core PRIVATE lexbor::lexbor_static)

# ---------------------------------------------------------------------------
# Text and layout: skparagraph for inline content, Yoga for boxes
# ---------------------------------------------------------------------------
# Paint lives here too: layout needs intrinsic image sizes and paint needs the
# box tree, so splitting them would only create a dependency cycle.
add_library(xgu_layout STATIC
    src/text/FontManager.cpp
    src/text/InlineContent.cpp
    src/layout/LayoutBox.cpp
    src/layout/LayoutEngine.cpp
    src/paint/BoxGeometry.cpp
    src/paint/ImageCache.cpp
    src/paint/Painter.cpp
    src/input/HitTester.cpp
    src/input/InputRouter.cpp)
# LayoutBox.h exposes YGNodeRef, so Yoga is part of the public interface.
target_link_libraries(xgu_layout PUBLIC xgu_dom xgu_skia yoga::yogacore)

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

# ---------------------------------------------------------------------------
# JavaScript: V8 platform + per-view runtime
# ---------------------------------------------------------------------------
add_library(xgu_js STATIC
    src/js/v8/V8Platform.cpp
    src/js/v8/V8Runtime.cpp
    src/js/v8/DomBindings.cpp
    src/js/v8/Timers.cpp
    src/js/v8/UnityBindings.cpp)
# The DOM bindings read box geometry for getBoundingClientRect and the scroll
# properties, so they need the layout tree.
target_link_libraries(xgu_js PUBLIC xgu_core xgu_dom xgu_layout xgu_v8)

# ---------------------------------------------------------------------------
# Runtime aggregate (process singleton wiring core + render + js)
# ---------------------------------------------------------------------------
add_library(xgu_runtime STATIC
    src/core/Runtime.cpp
    src/core/View.cpp)
target_link_libraries(xgu_runtime PUBLIC xgu_core xgu_dom xgu_layout xgu_render xgu_js)

# The C ABI implementation is compiled into every host (DLL, tools, tests).
set(XGU_CAPI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/capi/CApi.cpp)

# V8 runtime files (DLLs, icudtl.dat) next to every binary in bin/.
xgu_add_v8_runtime_copy(xgu_v8_runtime "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

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
    add_dependencies(xploit_game_ui xgu_v8_runtime)
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
    add_dependencies(xgu_cli xgu_v8_runtime)

    add_executable(xgu_host WIN32
        ${XGU_CAPI_SOURCES}
        src/platform/standalone/main.cpp)
    target_compile_definitions(xgu_host PRIVATE XGU_STATIC)
    target_link_libraries(xgu_host PRIVATE xgu_runtime)
    add_dependencies(xgu_host xgu_v8_runtime)
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
        tests/unit/TestFrameTest.cpp
        tests/unit/V8RuntimeTest.cpp
        tests/unit/AtomTest.cpp
        tests/unit/AssetLoaderTest.cpp
        tests/unit/DomTest.cpp
        tests/unit/HtmlParserTest.cpp
        tests/unit/SelectorTest.cpp
        tests/unit/DocumentLoadTest.cpp
        tests/unit/CssParserTest.cpp
        tests/unit/StyleEngineTest.cpp
        tests/unit/LayoutTest.cpp
        tests/unit/StyleLayoutPipelineTest.cpp
        tests/unit/PaintTest.cpp
        tests/unit/GoldenImageTest.cpp
        tests/unit/InputTest.cpp
        tests/unit/TextControlTest.cpp
        tests/unit/BridgeTest.cpp
        tests/unit/TimerTest.cpp
        tests/unit/ScrollTest.cpp
        tests/unit/GradientTest.cpp)
    target_compile_definitions(xgu_tests PRIVATE
        XGU_STATIC
        XGU_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata")
    target_link_libraries(xgu_tests PRIVATE xgu_runtime GTest::gtest GTest::gtest_main)
    add_dependencies(xgu_tests xgu_v8_runtime)
    include(GoogleTest)
    gtest_discover_tests(xgu_tests)
endif()
