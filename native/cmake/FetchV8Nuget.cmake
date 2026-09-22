# Fetches the prebuilt V8 NuGet packages (pmed/v8-nuget) and exposes them as the
# INTERFACE target `xgu_v8` plus the list XGU_V8_RUNTIME_FILES (DLLs, ICU data,
# snapshot blobs) that must sit next to every executable/plugin using V8.
#
# Packages:
#   v8-v143-x64        headers + import libraries (lib/{Debug,Release}/*.dll.lib)
#   v8.redist-v143-x64 runtime DLLs + icudtl.dat   (lib/{Debug,Release}/)
#
# The Debug package is a debug V8 build linked against the debug CRT (/MDd), so
# the Debug configuration must use it; every other configuration uses Release.

set(XGU_V8_VERSION "13.0.245.25" CACHE STRING "V8 NuGet package version (pmed/v8-nuget)")
set(XGU_V8_NUGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/v8/${XGU_V8_VERSION}" CACHE PATH
    "Directory where the V8 NuGet packages are downloaded and extracted")
set(XGU_V8_NUGET_BASE_URL "https://www.nuget.org/api/v2/package" CACHE STRING "NuGet v2 download endpoint")

function(_xgu_fetch_nupkg id)
    set(dest "${XGU_V8_NUGET_DIR}/${id}")
    if(EXISTS "${dest}/.extracted")
        return()
    endif()
    set(nupkg "${XGU_V8_NUGET_DIR}/${id}.${XGU_V8_VERSION}.nupkg")
    if(NOT EXISTS "${nupkg}")
        message(STATUS "Downloading ${id} ${XGU_V8_VERSION} from nuget.org ...")
        file(MAKE_DIRECTORY "${XGU_V8_NUGET_DIR}")
        file(DOWNLOAD "${XGU_V8_NUGET_BASE_URL}/${id}/${XGU_V8_VERSION}" "${nupkg}" STATUS status SHOW_PROGRESS)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            list(GET status 1 reason)
            file(REMOVE "${nupkg}")
            message(FATAL_ERROR "Failed to download ${id} ${XGU_V8_VERSION}: ${reason}")
        endif()
    endif()
    message(STATUS "Extracting ${id} ${XGU_V8_VERSION} ...")
    file(ARCHIVE_EXTRACT INPUT "${nupkg}" DESTINATION "${dest}")
    file(TOUCH "${dest}/.extracted")
endfunction()

_xgu_fetch_nupkg(v8-v143-x64)
_xgu_fetch_nupkg(v8.redist-v143-x64)

set(_xgu_v8_dev "${XGU_V8_NUGET_DIR}/v8-v143-x64")
set(_xgu_v8_redist "${XGU_V8_NUGET_DIR}/v8.redist-v143-x64")

if(CMAKE_CONFIGURATION_TYPES)
    message(WARNING "Multi-config generators use the Release V8 binaries for every configuration")
    set(_xgu_v8_cfg "Release")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_xgu_v8_cfg "Debug")
else()
    set(_xgu_v8_cfg "Release")
endif()

if(NOT EXISTS "${_xgu_v8_dev}/lib/${_xgu_v8_cfg}/v8.dll.lib")
    message(FATAL_ERROR "V8 import library not found: ${_xgu_v8_dev}/lib/${_xgu_v8_cfg}/v8.dll.lib")
endif()

add_library(xgu_v8 INTERFACE)
target_include_directories(xgu_v8 SYSTEM INTERFACE "${_xgu_v8_dev}/include")
# Must match v8_build_config.json of the package (pointer compression + sandbox).
target_compile_definitions(xgu_v8 INTERFACE
    V8_COMPRESS_POINTERS
    V8_31BIT_SMIS_ON_64BIT_ARCH
    V8_ENABLE_SANDBOX
    $<$<CONFIG:Debug>:V8_ENABLE_CHECKS>)
target_link_libraries(xgu_v8 INTERFACE
    "${_xgu_v8_dev}/lib/${_xgu_v8_cfg}/v8.dll.lib"
    "${_xgu_v8_dev}/lib/${_xgu_v8_cfg}/v8_libbase.dll.lib"
    "${_xgu_v8_dev}/lib/${_xgu_v8_cfg}/v8_libplatform.dll.lib"
    dbghelp shlwapi winmm
    delayimp)
# Delay-load so xploit_game_ui.dll itself loads even when the V8 DLLs are not on
# the default search path; V8Platform pre-loads them from the module directory.
target_link_options(xgu_v8 INTERFACE
    /DELAYLOAD:v8.dll
    /DELAYLOAD:v8_libbase.dll
    /DELAYLOAD:v8_libplatform.dll)

file(GLOB XGU_V8_RUNTIME_FILES
    "${_xgu_v8_redist}/lib/${_xgu_v8_cfg}/*.dll"
    "${_xgu_v8_redist}/lib/${_xgu_v8_cfg}/*.dat"
    "${_xgu_v8_redist}/lib/${_xgu_v8_cfg}/*.bin")
if(NOT XGU_V8_RUNTIME_FILES)
    message(FATAL_ERROR "No V8 runtime files found in ${_xgu_v8_redist}/lib/${_xgu_v8_cfg}")
endif()
message(STATUS "V8 ${XGU_V8_VERSION} (${_xgu_v8_cfg}) from ${XGU_V8_NUGET_DIR}")

# Copies the V8 runtime files into `dir` as part of the build.
function(xgu_add_v8_runtime_copy target dir)
    add_custom_target(${target} ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XGU_V8_RUNTIME_FILES} "${dir}"
        COMMENT "Copying V8 runtime files to ${dir}"
        VERBATIM)
endfunction()
