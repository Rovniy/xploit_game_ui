# Locates the Unity Native Plugin API headers (IUnityInterface.h, IUnityGraphics.h,
# IUnityGraphicsD3D12.h, IUnityLog.h).
#
# Those headers ship with the Unity Editor under the Unity Companion License and
# are therefore not redistributed in this repository. They are found in a local
# Unity installation instead, or copied next to the sources by
# tools/fetch-unity-headers.ps1.
#
# Sets:
#   XGU_UNITY_PLUGIN_API_FOUND    TRUE when a usable set of headers was found
#   XGU_UNITY_PLUGIN_API_INCLUDE  directory to add to the include path, such that
#                                 #include <PluginAPI/IUnityGraphics.h> resolves
#
# Override the search with -DXGU_UNITY_PLUGIN_API_DIR=<path to the PluginAPI folder>
# or by setting the environment variable of the same name.

set(XGU_UNITY_PLUGIN_API_DIR "" CACHE PATH
    "Directory holding the Unity Native Plugin API headers (Editor/Data/PluginAPI)")

function(_xgu_unity_api_is_usable directory out_var)
    set(${out_var} FALSE PARENT_SCOPE)
    foreach(header IUnityInterface.h IUnityGraphics.h IUnityGraphicsD3D12.h IUnityLog.h)
        if(NOT EXISTS "${directory}/${header}")
            return()
        endif()
    endforeach()
    set(${out_var} TRUE PARENT_SCOPE)
endfunction()

set(_xgu_unity_candidates "")

if(XGU_UNITY_PLUGIN_API_DIR)
    list(APPEND _xgu_unity_candidates "${XGU_UNITY_PLUGIN_API_DIR}")
endif()
if(DEFINED ENV{XGU_UNITY_PLUGIN_API_DIR})
    list(APPEND _xgu_unity_candidates "$ENV{XGU_UNITY_PLUGIN_API_DIR}")
endif()

# What tools/fetch-unity-headers.ps1 writes. Kept out of version control.
list(APPEND _xgu_unity_candidates "${CMAKE_CURRENT_SOURCE_DIR}/third_party/unity/PluginAPI")

# A single editor pointed at by the usual CI environment variable.
if(DEFINED ENV{UNITY_EDITOR_PATH})
    list(APPEND _xgu_unity_candidates "$ENV{UNITY_EDITOR_PATH}/Data/PluginAPI")
    list(APPEND _xgu_unity_candidates "$ENV{UNITY_EDITOR_PATH}/Unity.app/Contents/PluginAPI")
endif()

# Every editor installed through Unity Hub, newest version first.
set(_xgu_unity_hub_globs
    "$ENV{ProgramFiles}/Unity/Hub/Editor/*/Editor/Data/PluginAPI"
    "$ENV{ProgramW6432}/Unity/Hub/Editor/*/Editor/Data/PluginAPI"
    "C:/Program Files/Unity/Hub/Editor/*/Editor/Data/PluginAPI"
    "/Applications/Unity/Hub/Editor/*/Unity.app/Contents/PluginAPI"
    "$ENV{HOME}/Unity/Hub/Editor/*/Editor/Data/PluginAPI")
set(_xgu_unity_hub "")
foreach(pattern IN LISTS _xgu_unity_hub_globs)
    file(GLOB _xgu_matches "${pattern}")
    list(APPEND _xgu_unity_hub ${_xgu_matches})
endforeach()
if(_xgu_unity_hub)
    list(SORT _xgu_unity_hub COMPARE NATURAL ORDER DESCENDING)
    list(APPEND _xgu_unity_candidates ${_xgu_unity_hub})
endif()

set(XGU_UNITY_PLUGIN_API_FOUND FALSE)
foreach(candidate IN LISTS _xgu_unity_candidates)
    _xgu_unity_api_is_usable("${candidate}" _usable)
    if(_usable)
        get_filename_component(_parent "${candidate}" DIRECTORY)
        get_filename_component(_leaf "${candidate}" NAME)
        if(NOT _leaf STREQUAL "PluginAPI")
            # The include directive spells the folder out, so it has to be named
            # PluginAPI. Mirror the headers into the build tree when it is not.
            set(_mirror "${CMAKE_BINARY_DIR}/unity-plugin-api/PluginAPI")
            file(MAKE_DIRECTORY "${_mirror}")
            file(GLOB _xgu_unity_headers "${candidate}/*.h")
            file(COPY ${_xgu_unity_headers} DESTINATION "${_mirror}")
            set(_parent "${CMAKE_BINARY_DIR}/unity-plugin-api")
        endif()
        set(XGU_UNITY_PLUGIN_API_FOUND TRUE)
        set(XGU_UNITY_PLUGIN_API_INCLUDE "${_parent}")
        message(STATUS "Unity Native Plugin API headers: ${candidate}")
        # The plugin prefers IUnityGraphicsD3D12v8 and falls back to v7, so an
        # older copy still builds; say so rather than failing.
        file(STRINGS "${candidate}/IUnityGraphicsD3D12.h" _v8_line
             REGEX "IUnityGraphicsD3D12v8" LIMIT_COUNT 1)
        if(NOT _v8_line)
            message(STATUS "  Predates IUnityGraphicsD3D12v8; the plugin falls back to v7.")
        endif()
        break()
    endif()
endforeach()

if(NOT XGU_UNITY_PLUGIN_API_FOUND)
    message(STATUS
        "Unity Native Plugin API headers not found. The native core, tests and tools still "
        "build; xploit_game_ui.dll does not. Run tools/fetch-unity-headers.ps1, or configure "
        "with -DXGU_UNITY_PLUGIN_API_DIR=<Unity>/Editor/Data/PluginAPI.")
endif()
