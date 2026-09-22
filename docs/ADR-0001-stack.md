# ADR-0001. The xploit_game_ui technology stack

- Status: accepted (2026-09-22)
- Scope: the native runtime (C++), the Unity integration, and the choice of open-source components
- Related: [architecture.md](architecture.md), [threading.md](threading.md), [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md), [css-support.md](css-support.md)

## Context

The goal is a Unity plugin that uses HTML, CSS and JavaScript as game UI — conceptually
what Coherent Gameface does — without Gameface, Ultralight, Vuplex or any other
commercial WebView SDK. The guiding principle is **not to build a second browser**,
but to assemble a fast, embeddable and controllable game UI runtime out of
open-source parts. Every part has to stay replaceable behind `IJavaScriptRuntime`,
`IHtmlParser`, `ICssEngine`, `ILayoutEngine`, `IRenderer`, `IInputProvider`,
`IBridge` and `ITextureProvider`.

The MVP targets Unity 6 (6000.2–6000.5), Windows x64 and Direct3D 12 — Unity 6.1
and newer create Windows projects with D3D12 by default.

## Decision

| Concern | Component | Source / version | Licence |
|---|---|---|---|
| JavaScript | V8 13.0 | NuGet `v8-v143-x64` + `v8.redist-v143-x64` 13.0.245.25 (pmed); fallback is a `v8_monolith` build via depot_tools | BSD-3-Clause |
| HTML parsing | lexbor 3.0.1, `html` module | vcpkg | Apache-2.0 |
| CSS parsing | hand-written tokenizer and parser for the supported subset (`native/src/css`) | this project | — |
| CSS cascade, selectors, computed style | hand-written | `native/src/css` | — |
| Layout | Yoga 3.2.1 | vcpkg | MIT |
| Text shaping and line breaking | Skia `skparagraph`/`skshaper` (HarfBuzz + ICU) | vcpkg `skia[harfbuzz,icu,freetype,png,jpeg,webp,direct3d]` 148 | BSD-3-Clause (+ MIT, Unicode, FTL) |
| 2D painting and GPU | Skia Ganesh, Direct3D 12 backend | the same port | BSD-3-Clause |
| Images | libpng, libjpeg-turbo, libwebp through Skia's codecs | vcpkg | PNG / IJG+BSD / BSD-3-Clause |
| Unity integration | Unity Native Plugin API (`IUnityGraphicsD3D12v8`, `IUnityLog`) | headers read from a local Unity install, not redistributed | Unity Companion License |
| Tests | GoogleTest/gmock; Unity Test Framework | vcpkg / UPM | BSD-3-Clause / Unity |
| Build | CMake 4.3 + Ninja + the vcpkg bundled with Visual Studio 2026, triplet `x64-windows-static-md`, `builtin-baseline` pinned to microsoft/vcpkg commit `24a726719eee20d452535529d361ee593c680d6c` (2026-09-22) | | |

The DOM is our own; lexbor is used purely as a parser. No JSON library is needed
at runtime: script serialises through `v8::JSON` and C# through the bundled
`WebJson`. nlohmann/json is linked only into the tests and `xgu_cli`.

## Alternatives considered

| Candidate | Why it was rejected |
|---|---|
| **Servo** (Rust, MPL-2.0) | Bundles SpiderMonkey rather than V8; the embedding API is unstable; a heavy Windows build; a full browser engine |
| **LibWeb / Ladybird** (BSD-2-Clause) | Bundles LibJS; no embedding API; Windows is not officially supported |
| **Blink / Chromium Content / CEF** (BSD-3-Clause) | A second Chrome: multi-process, 100+ MB, exactly what the project set out not to do |
| **RmlUi 6.3** (MIT) | The closest ready-made "HTML/CSS game UI" library, with DX11/DX12 renderers, but RML and RCSS drift from real HTML and CSS and it is one monolithic dependency. Kept on record as the fast fallback path, roughly 6–8 weeks to an MVP |
| **Blitz** (Rust: Stylo + Taffy + Vello + Parley) | A real Firefox CSS engine, but beta, with no JS/DOM bindings, wgpu-only rendering and a Rust runtime. A candidate for a future `ICssEngine` |
| **Skia Graphite + Dawn** for D3D12 | The official successor to Ganesh D3D, but Dawn is a separate heavy dependency and driving Unity's device through it is harder. Deferred |
| **A hand-written 2D renderer** instead of Skia | Possible later as an `IRenderer`; for the MVP Skia brings anti-aliasing, shadows, rounded corners, transforms, codecs and text for free |
| **QuickJS-NG** instead of V8 | Does not meet the MVP acceptance criterion ("JS through V8"); possible as a second `IJavaScriptRuntime` backend for fast tests |
| **The vcpkg v8 port** | Stuck at version 9.1 (2021) and unmaintained |

## Key architectural decisions

1. **Rendering on Unity's device.** Skia's `GrDirectContext` is built over Unity's
   `ID3D12Device` and `ID3D12CommandQueue` (through `IUnityGraphicsD3D12v8`, with a
   plugin event in `kUnityD3D12GraphicsQueueAccess_Allow` mode). The plugin creates
   the view's texture as a committed resource with
   `ALLOW_RENDER_TARGET | ALLOW_SIMULTANEOUS_ACCESS` and hands it to C# through
   `Texture2D.CreateExternalTexture`. There is no copy, and ordering within the single
   queue is the synchronisation. Skia's D3D backend ignores `MutableTextureState`, so
   after `flush(kPresent)` the resource stays in `COMMON`, which D3D12 implicitly
   promotes and decays for a simultaneous-access resource.
2. **A display list crosses the thread boundary.** The runtime thread (DOM, CSS,
   layout, JS, paint) publishes an immutable `SkPicture`; Unity's submission thread
   replays it into the surface. Every `GrDirectContext` call happens on one thread.
3. **One V8 isolate per view; one V8 platform per process**, never disposed, because
   Unity keeps native plugins loaded across domain reloads.
4. **Our own DOM and CSS cascade**, with lexbor only at the input. That keeps
   `IHtmlParser` and `ICssEngine` replaceable and lets style, layout and paint data
   live directly on the nodes. lexbor's `selectors` module is unused: it models
   `:hover`, `:active` and `:focus` as literal DOM attributes.
5. **Secure by default:** no network, no `fetch`/`XHR`/`WebSocket`/dynamic `import()`,
   no WebAssembly, file access confined to the UI root through `IAssetLoader`, and the
   only native surface is the `Unity` object plus `ExecuteJS` from C#.
6. **Generation-tagged handles.** A view in the C ABI is a `uint64_t {index:32,
   generation:32}`, and the same id is passed as `data` to `IssuePluginEventAndData`,
   so a plugin event that outlives its view is safely ignored.

## Risks and fallbacks

| # | Risk | Mitigation / fallback |
|---|---|---|
| 1 | Skia's Ganesh D3D backend is discussed for removal in favour of Graphite + Dawn | Skia 148 is pinned through the vcpkg baseline; `ITextureProvider` is abstract; the fallback is Skia Vulkan on its own device, importing the D3D12 shared resource and a shared fence |
| 2 | The V8 NuGet package has a single maintainer and was last updated in February 2025 | `IJavaScriptRuntime`; a documented `v8_monolith` build through depot_tools; local UI has no network, which bounds the exposure |
| 3 | Building Skia through vcpkg takes 30–60 minutes and the port has known Windows failures | A vcpkg binary cache (`build/vcpkg-cache`) and a CI cache; on a port failure, pin a patch in a `native/vcpkg-overlays` overlay |
| 4 | ICU data is tens of megabytes, and there are two copies of ICU (Skia's and V8's) | Accepted for the MVP; ICU data filters are later work |
| 5 | Premultiplied alpha and sRGB versus Unity's stock UI shader | The `XploitGameUI/RawImagePremultiplied` shader (`Blend One OneMinusSrcAlpha`) and an external texture created with `linear: false` |
| 6 | lexbor leaves `border-radius`, `background-*`, `box-shadow`, `transform` and `gap` untyped (`_custom`) | Resolved by writing our own parser; see the spike below |
| 7 | The V8 NuGet `.props` does not set the preprocessor defines for pointer compression and the sandbox | Take the flags from the shipped `v8config.h` / `v8_build_config.json` |
| 8 | Unity does not document the expected state of an external D3D12 texture | Simultaneous access plus `flush(kPresent)` leaves it in `COMMON`; the `D3D12Copy` provider, which uses `RequestResourceState`/`NotifyResourceState`, makes no assumption at all |
| 9 | The accuracy of our own inline layout | A golden corpus built from real HUD and menu mock-ups; every deviation is recorded in [css-support.md](css-support.md) |
| 10 | The `IUnityGraphicsD3D12v8` GUID is marked `// TODO: Get proper values` in Unity's header | Falls back to `IUnityGraphicsD3D12v7`; the `D3D12Copy` provider needs v8 and disables itself without it |

## Spike results

| Date | Spike | Result |
|---|---|---|
| 2026-09-22 | Building `skia[direct3d,freetype,harfbuzz,icu,png,jpeg,webp]:x64-windows-static-md` through vcpkg (baseline `24a72671…`) | **Succeeded.** Skia 148 built in 6.5 minutes, all 31 ports in 24, with a binary cache in `build/vcpkg-cache`. The CMake targets are `unofficial::skia::skia` and `unofficial::skia::modules::{skparagraph,skshaper,skunicode_core,skunicode_icu}`; headers are included as `<include/core/SkCanvas.h>` and `<modules/skparagraph/include/...>`. Two m148 API differences: gradients come from `SkShaders::LinearGradient(pts, SkGradient)` in `include/effects/SkGradient.h` (there is no `SkGradientShader.h`), and `kNormal_SkBlurStyle` lives in `include/core/SkBlurTypes.h`. `SK_GANESH` and `SK_DIRECT3D` are defined by our own `xgu_skia` target |
| 2026-09-22 | The D3D12 path outside Unity: `xgu_host --debug --screenshot` (our own device, Skia Ganesh D3D12, texture read back to PNG) | **Succeeded.** The frame — rounded panel, shadow, gradient, PNG image, skparagraph text — reads back from the GPU and matches `xgu_cli`'s CPU rendering |
| 2026-09-22 | The D3D12 path inside Unity 6000.5.1f1 (batch mode, `-force-d3d12 -force-d3d12-debug`) | **Succeeded.** PlayMode 6/6: the plugin loads, `IUnityGraphicsD3D12v8` is obtained (the TODO-marked GUID works), the Skia context is created on Unity's device and released cleanly, the Skia frame reads back from the external texture (`CreateExternalTexture` + `Graphics.Blit` into a render texture + `AsyncGPUReadback`; Unity cannot read a BGRA8 external texture back directly), and 10 create/destroy cycles leak no handles. The D3D12 debug layer reported three things: (a) id 614, a `SampleDesc.Quality` mismatch between the PSO and the RTV caused by the default `fSampleQualityPattern` in `GrD3DTextureResourceInfo` — **fixed** by setting it to 0 for the wrapped texture; (b) id 1315, `GetGPUDescriptorHandleForHeapStart` called on a CPU heap inside Skia's `GrD3DDescriptorHeap` — harmless and a known backend quirk; (c) id 1422, Skia's intermediate render targets are allocated `CREATE_NOT_ZEROED` by D3D12MA and never explicitly cleared — harmless, since the contents are fully overwritten, and avoidable with a custom `GrD3DMemoryAllocator` |
| 2026-09-22 | Binary size | `xploit_game_ui.dll` is roughly 58 MB in RelWithDebInfo: static Skia, about 30 MB of ICU data, plus FreeType, HarfBuzz and the image codecs. Trimming ICU and `/OPT:REF` are later work |
| 2026-09-22 | V8 NuGet `v8-v143-x64` / `v8.redist-v143-x64` 13.0.245.25 | **Succeeded.** The `.props` sets `V8_COMPRESS_POINTERS`, `V8_31BIT_SMIS_ON_64BIT_ARCH` and `V8_ENABLE_SANDBOX`, plus `V8_ENABLE_CHECKS` in Debug; `v8_build_config.json` confirms pointer compression and the sandbox are on and that i18n uses an external `icudtl.dat` (10 MB) in a component build (clang, MSVC STL). The runtime files are `v8.dll` (33 MB), `v8_libbase.dll`, `v8_libplatform.dll`, `icuuc.dll`, `third_party_icu_icui18n.dll`, `third_party_abseil-cpp_absl.dll`, `third_party_zlib.dll` and `icudtl.dat`. The debug package is built against the debug CRT, so a Debug configuration must link that one. There is no monolithic `v8_monolith` build for v143 on NuGet, so we use the component DLLs with `/DELAYLOAD` and preload them from the module directory (`V8Platform::preloadDlls`). One V8 quirk: its built-in no-op `console` shadows properties set on the global template, so our `console` is assigned to the global object after `Context::New`. The integration lives in `cmake/FetchV8Nuget.cmake` (download from nuget.org, `file(ARCHIVE_EXTRACT)`), covered by 13 GoogleTest cases and `xgu_cli js` |
| 2026-09-22 | CSS parser: lexbor's `css` module versus our own | **Our own parser was chosen** — the fallback named in risk 6. lexbor types only about 45 properties, and `border-radius`, `background-*`, `box-shadow`, `transform` and `gap` all arrive as `_custom`, so a value parser was needed regardless; two parsing paths would have meant double the work and inconsistent errors. A tokenizer of about 380 lines plus a value and shorthand parser of about 900 handle everything uniformly and give precise messages for unsupported properties. lexbor remains the HTML parser |
| 2026-09-22 | Layout on Yoga 3.2.1 | A block is a column flex container, inline content is an anonymous box with a measure function over skparagraph, and atomic inlines (`<img>`, inline-block) are placeholders inside the paragraph. Verified against a real menu page: flex centring, `gap`, percentages, `position: absolute` with `right`/`bottom`, and measured text. One quirk: a Yoga node with a measure function cannot have children, so atomic inlines are kept outside Yoga's child list and positioned from `getRectsForPlaceholders()` |
