# Third-party components and licences

Every runtime component permits commercial redistribution and modification, and
none of them requires the source of your game to be disclosed. The attribution
requirements are met by shipping this file and the full licence texts with the
package (`unity/Packages/com.xploit.game_ui/Third Party Notices.md`, assembled by
`tools/build.ps1` from `native/vcpkg_installed/x64-windows-static-md/share/*/copyright`
and from the V8 NuGet packages).

xploit_game_ui itself is licensed under [Apache-2.0](../LICENSE).

| Component | Repository | Licence | Commercial redistribution | Modification | Attribution | Source disclosure |
|---|---|---|---|---|---|---|
| V8 | https://github.com/v8/v8 | BSD-3-Clause | yes | yes | yes (licence text) | no |
| V8 NuGet build (pmed) | https://github.com/pmed/v8-nuget | BSD-3-Clause for the scripts; the binaries are under V8's licence | yes | yes | yes | no |
| Skia (including skparagraph, skshaper, skunicode) | https://github.com/google/skia | BSD-3-Clause | yes | yes | yes | no |
| lexbor | https://github.com/lexbor/lexbor | Apache-2.0 | yes | yes | yes (NOTICE, where present) | no |
| Yoga | https://github.com/facebook/yoga | MIT | yes | yes | yes | no |
| HarfBuzz | https://github.com/harfbuzz/harfbuzz | MIT ("Old MIT") | yes | yes | yes | no |
| ICU | https://github.com/unicode-org/icu | Unicode License v3 | yes | yes | yes (licence text) | no |
| FreeType | https://gitlab.freedesktop.org/freetype/freetype | FTL (dual with GPLv2; FTL is the one used) | yes | yes | yes ("This software is built with FreeType") | no |
| libpng | https://github.com/pnggroup/libpng | PNG Reference Library License v2 | yes | yes | yes | no |
| libjpeg-turbo | https://github.com/libjpeg-turbo/libjpeg-turbo | IJG + BSD-3-Clause + zlib | yes | yes | yes | no |
| libwebp | https://chromium.googlesource.com/webm/libwebp | BSD-3-Clause | yes | yes | yes | no |
| zlib | https://github.com/madler/zlib | zlib | yes | yes | not required (appreciated) | no |
| D3D12 Memory Allocator | https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator | MIT | yes | yes | yes | no |
| SPIRV-Cross | https://github.com/KhronosGroup/SPIRV-Cross | Apache-2.0 | yes | yes | yes | no |
| Abseil (inside V8) | https://github.com/abseil/abseil-cpp | Apache-2.0 | yes | yes | yes | no |
| GoogleTest (tests only) | https://github.com/google/googletest | BSD-3-Clause | yes | yes | yes | no |
| nlohmann/json (tests and CLI only) | https://github.com/nlohmann/json | MIT | yes | yes | yes | no |

No copyleft licence (GPL, LGPL, MPL) is present in the shipped runtime.

## Unity Native Plugin API headers

`IUnityInterface.h`, `IUnityGraphics.h`, `IUnityGraphicsD3D12.h` and `IUnityLog.h`
ship with the Unity Editor under the [Unity Companion License](https://unity.com/legal/licenses/unity-companion-license),
which allows their use in Unity-dependent projects. **They are not redistributed
in this repository.** The build reads them from a local Unity installation:
CMake looks in `XGU_UNITY_PLUGIN_API_DIR`, in `native/third_party/unity/PluginAPI`,
and in every editor under Unity Hub, and `tools/fetch-unity-headers.ps1` copies
them there on request.

Without them the native core, the tests and `xgu_cli` still build; only
`xploit_game_ui.dll` is skipped.

## Considered and rejected

See [ADR-0001](ADR-0001-stack.md): Servo (MPL-2.0), LibWeb (BSD-2-Clause),
CEF (BSD-3-Clause), RmlUi (MIT), Blitz (MIT/Apache-2.0, with stylo under MPL-2.0).
