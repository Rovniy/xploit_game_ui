# xploit_game_ui — HTML/CSS/JS Game UI Runtime для Unity: план реализации

## Статус выполнения

| Этап | Состояние | Примечания |
|---|---|---|
| 0 Feasibility / ADR | **готово** (2026-09-22) | `docs/ADR-0001-stack.md`, `THIRD-PARTY-NOTICES.md`, `threading.md`, `architecture.md`, `css-support.md`, этот план |
| 1 Native Hello World + спайк D3D12 | **готово** (2026-09-22) | `xploit_game_ui.dll`, `xgu_host`, `xgu_cli`, `xgu_tests`; UPM-пакет `com.xploit.game_ui`; Sandbox (D3D12) со сценой `Stage1_HelloWorld`. Проверено: native GoogleTest 17/17; `xgu_cli --test-frame` (CPU) и `xgu_host --screenshot` (D3D12, readback с GPU) дают одинаковый кадр; Unity PlayMode 6/6 в batchmode под `-force-d3d12 -force-d3d12-debug` (ping=42, native-лог в Console, провайдер D3D12External, кадр Skia считан из внешней текстуры, 10 циклов create/destroy). Известные сообщения debug-слоя D3D12 — см. ADR (безвредные, внутри Skia) |
| 2 V8 | **готово** (2026-09-22) | `IJavaScriptRuntime` + `js::V8Runtime` (изолят на view, `console.*`, `Uncaught …` со стеком, необработанные rejection'ы, microtask checkpoint), `js::V8Platform` (процесс-синглтон, ICU, предзагрузка DLL, `--no-expose-wasm`), `core::RuntimeThread` (runtime-поток + inline-режим), очередь логов с вычиткой в главном потоке Unity, C ABI (`xgu_tick`, `xgu_view_execute_js`, `xgu_view_set_paused`, `xgu_view_get_state`, `xgu_log_*`), `HtmlView.ExecuteJS/Paused/State`, `xgu_cli js`. Проверено: native 33/33, Unity PlayMode 12/12 на D3D12 |
| 3 HTML/DOM | в работе | |
| 4–10 | не начаты | |

Что осталось вне автоматических проверок Этапа 1: ручной запуск сцены `Stage1_HelloWorld` в редакторе (RawImage с кадром Skia) и проверка `-force-d3d11` → CPU-провайдер; оба сценария покрыты кодом, но не прогонялись интерактивно.

## Контекст

ТЗ требует Unity-плагин, который рендерит HTML/CSS/JS как игровой UI (аналог Coherent Gameface), собранный только из open-source компонентов, без Gameface/Ultralight/Vuplex и коммерческих WebView SDK. Репозиторий `D:\xploit_game_ui` пуст — проект с нуля.

Решения, подтверждённые пользователем (2026-09-22):

| Решение | Выбор |
|---|---|
| Ядро движка | Собственный движок из компонентов (lexbor + собственный CSS-каскад + Yoga + Skia/skparagraph + V8), C++20 |
| GPU-путь в MVP | Только D3D12 (Skia Ganesh D3D12 на устройстве Unity). Для D3D11-проектов — CPU-провайдер (загрузка пикселей) |
| Структура репозитория | Монорепозиторий: `native/` (C++ runtime) + `unity/` (UPM-пакет + тестовый Unity-проект) |
| Первый срез реализации | Этап 0 (ADR, таблица лицензий, скелет) + Этап 1 (Hello World DLL, Unity↔C++, спайк текстуры Skia D3D12 на RawImage) |
| Именование | Библиотека **xploit_game_ui**: `xploit_game_ui.dll`, C ABI с префиксом `xgu_`, C++ namespace `xgu`, UPM-пакет `com.xploit.game_ui`, C# namespace `Xploit.GameUI` |
| Язык | Общение и проектная документация — на русском; идентификаторы кода, комментарии в коде и коммиты — на английском |
| Документация | План фиксируется в репозитории как `docs/PLAN.md` (этот документ) и поддерживается по этапам |

## Факты об окружении (проверено на этой машине)

- Visual Studio 2026 Community 18.9, MSVC 14.51 (C++20), Windows SDK 10.0.26100. VS 2022 17.14 установлена без C++ toolset.
- В комплекте VS 2026: CMake 4.3.1, Ninja 1.13.2, vcpkg 2026-05-27 (`C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg\vcpkg.exe`). В PATH их нет — использовать Developer PowerShell или абсолютные пути в CMakePresets.
- Unity: 6000.2.6f2, 6000.3.2f1, 6000.5.1f1 (`C:\Program Files\Unity\Hub\Editor\`). Заголовки плагинов в `Editor\Data\PluginAPI\`, включая `IUnityGraphicsD3D12v8`.
- Unity 6.1+ по умолчанию создаёт Windows-проекты с D3D12.
- GPU: NVIDIA RTX 4070 SUPER + AMD Radeon iGPU (полезно для тестов с несколькими адаптерами). Vulkan runtime 1.4 есть, Vulkan SDK нет.
- Также есть: Python 3.13, Rust 1.97, Node 22, .NET SDK 10. depot_tools нет (vcpkg сам скачивает Python/GN для сборки Skia).

## Этап 0 — Architecture Decision Document (содержимое `docs/ADR-0001-stack.md`)

### Рассмотренные альтернативы и причины отказа
- **Servo** (Rust, MPL-2.0): встроенный SpiderMonkey (ТЗ требует V8), нестабильный embedding API, тяжёлая сборка под Windows, полноценный браузерный движок. Отклонён.
- **LibWeb/Ladybird** (BSD-2): встроенный LibJS, нет embedding API, Windows не поддерживается. Отклонён.
- **Blink / Chromium Content / CEF** (BSD-3): «второй Chrome», мультипроцесс, 100+ МБ. Отклонён по принципу ТЗ.
- **RmlUi 6.3** (MIT): ближайшая готовая «HTML/CSS game UI» библиотека с DX11/DX12-рендерерами, но RML/RCSS отклоняются от настоящего HTML/CSS и это одна монолитная зависимость. Зафиксирован как запасной «быстрый» путь, если собственный движок сильно сдвинется по срокам.
- **Blitz** (Rust: Stylo + Taffy + Vello + Parley, MIT/Apache + MPL для stylo): настоящий CSS-движок, но beta, нет JS/DOM-биндингов, рендер только через wgpu, Rust-рантайм. Отклонён; кандидат на будущую замену `ICssEngine`.

### Выбранный стек

| Задача | Компонент | Версия / источник | Лицензия |
|---|---|---|---|
| JavaScript | V8 | NuGet `v8-v143-x64` + `v8.redist-v143-x64` 13.0.245.25 (pmed, февраль 2025; собран clang, MSVC STL, DLL: v8.dll, v8_libbase.dll, v8_libplatform.dll, zlib.dll). Запасной путь: собственная сборка `v8_monolith` через depot_tools | BSD-3 |
| HTML-парсинг | lexbor 3.0.1 | vcpkg `lexbor` (HTML5-совместимый парсер; только для документов и фрагментов, DOM собственный) | Apache-2.0 |
| CSS-парсинг | модуль `css` lexbor + собственный `ValueParser` для свойств, которые lexbor оставляет нетипизированными | `native/src/css`; собственный токенизатор как fallback за `ICssParser` | Apache-2.0 / — |
| CSS-каскад, селекторы, computed style | Собственная реализация (подмножество MVP) | `native/src/css` (модуль `selectors` lexbor не используется: он моделирует `:hover` и т.п. как литеральные атрибуты) | — |
| Layout | Yoga 3.2.1 | vcpkg `yoga` (flexbox; block эмулируется column flex) | MIT |
| Инлайновый текст / перенос строк | Skia `skparagraph` + `skshaper` (HarfBuzz + ICU) | vcpkg `skia[harfbuzz,icu,freetype,png,jpeg,webp,direct3d]` m148 (`unofficial::skia::modules::skparagraph`) | BSD-3 (HarfBuzz MIT, ICU Unicode license, FreeType FTL) |
| 2D paint + GPU | Skia Ganesh, backend D3D12 | тот же порт vcpkg; display list как `SkPicture` | BSD-3 |
| Изображения | libpng, libjpeg-turbo, libwebp через кодеки Skia | транзитивно через vcpkg | PNG / BSD-IJG / BSD-3 |
| Мост с Unity | заголовки Unity Native Plugin API (`IUnityGraphicsD3D12v8`, `IUnityLog`) | копия из `Editor\Data\PluginAPI` | Unity Companion License |
| Тесты | GoogleTest + gmock (vcpkg `gtest`), Unity Test Framework | | BSD-3 |
| JSON | в рантайме не нужен (JS: `v8::JSON`, C#: встроенный `WebJson`); nlohmann-json только для тестов и `xgu_cli` | vcpkg | MIT |
| Сборка | CMake 4.3 + Ninja + vcpkg (из комплекта VS 2026), триплет `x64-windows-static-md`, закреплённый baseline, бинарный кэш | | — |

Все рантайм-компоненты разрешают коммерческое распространение и модификацию, требуют указания авторства (файл `THIRD-PARTY-NOTICES.md` в пакете), ни один не требует раскрытия исходников (FreeType FTL требует упоминания; ICU — текста лицензии). Заголовки Unity можно использовать только в Unity-зависимых проектах — наш случай.

### Риски, фиксируемые в ADR
1. Ganesh D3D backend в Skia обсуждается к deprecation (преемник — Graphite+Dawn). Митигация: закрепить Skia m148 через baseline vcpkg; `IRenderer`/`ITextureProvider` абстрактны; запасной вариант — Skia Vulkan на собственном устройстве с импортом D3D12 shared resource + shared fence.
2. NuGet V8 — пакет одного мейнтейнера, последнее обновление февраль 2025 (V8 13.0). Митигация: абстракция `IJavaScriptRuntime`; задокументировать сборку `v8_monolith` через depot_tools как долгосрочный источник; у локального UI нет сети, так что риск безопасности старого V8 ограничен.
3. Skia через vcpkg — GN-сборка 30–60 минут. Митигация: бинарный кэш vcpkg в общем месте; артефакт CI.
4. Размер данных ICU (десятки МБ). Митигация: принять для MVP, урезать фильтрами данных ICU на Этапе 10.
5. Premultiplied alpha + sRGB: Skia выдаёт premultiplied sRGB, стандартный UI-шейдер Unity ждёт straight alpha. Митигация: шейдер `XploitGameUI/RawImagePremultiplied` (`Blend One OneMinusSrcAlpha`) и внешняя текстура с `linear: false`.
6. Модуль CSS lexbor оставляет border-radius, background-*, box-shadow, transform, gap и др. нетипизированными (`_custom`); если значения не сохраняются дословно — парсер заменяется на собственный токенизатор за `ICssParser` (спайк Этапа 4).
7. `.props` NuGet V8 не задаёт preprocessor defines; флаги pointer compression/sandbox нужно взять из поставляемых `v8config.h`/`v8_build_config.json`, иначе падение на старте (спайк Этапа 2).
8. Предполагаемое Unity состояние ресурса для внешней D3D12-текстуры не документировано. Митигация: ресурс с simultaneous access + flush `kPresent` оставляет `COMMON`, который неявно повышается/деградирует; провайдер `D3D12Copy` (одно GPU-копирование через `RequestResourceState/NotifyResourceState`) не требует допущений.
9. Точность собственного инлайнового layout. Митигация: корпус golden-тестов из реальных макетов HUD/меню в начале Этапа 4; отклонения фиксируются в `docs/css-support.md`.

## Структура репозитория

```
D:\xploit_game_ui
├── native/
│   ├── CMakeLists.txt, CMakePresets.json, vcpkg.json, vcpkg-configuration.json (закреплённый baseline)
│   ├── cmake/ (FetchV8Nuget.cmake, CopyRuntime.cmake)
│   ├── third_party/unity/PluginAPI/ (заголовки из Unity 6000.5.1f1)
│   ├── include/xploit_game_ui/ (публичный C ABI: xgu.h)
│   ├── src/
│   │   ├── core/      интерфейсы (§20 ТЗ), Runtime, View, потоки (MessageQueue, Mailbox), Log
│   │   ├── html/      адаптер lexbor → DOM, ua.css
│   │   ├── dom/       Node/Element/Text/Document, мутации, id map, сериализатор, события
│   │   ├── css/       LexborCssParser + ValueParser, RuleSet, Selector/RuleIndex/SelectorMatcher, StyleEngine (каскад), ComputedStyle
│   │   ├── layout/    адаптер Yoga, LayoutBox, inline formatting context (skparagraph), HitTester
│   │   ├── text/      FontManager, RunBuilder, WhiteSpace, TextControl
│   │   ├── paint/     render tree, stacking contexts, Painter → SkPicture, ImageCache
│   │   ├── render/skia/  владелец GrDirectContext D3D12, провайдеры текстур (D3D12 external, D3D12 copy, CPU raster)
│   │   ├── js/v8/     V8Runtime, DOM-биндинги, таймеры, console, объект Unity
│   │   ├── bridge/    протокол событий/RPC, очереди
│   │   ├── input/     маршрутизация ввода, фокус, диспетчер событий
│   │   └── platform/unity/ (UnityPluginLoad, render event, IUnityLog), platform/standalone/ (Win32-хост)
│   ├── tools/xgu_cli/ (рендер HTML → PNG, дамп layout, headless-прогон JS)
│   └── tests/ (GoogleTest, golden-тесты, testdata/{html,css,js,golden,fonts})
├── unity/
│   ├── Packages/com.xploit.game_ui/  package.json, Runtime/ (HtmlView.cs, HtmlViewManager.cs, WebEvent.cs, WebArguments.cs, WebTexture.cs, WebInput.cs, WebJson.cs, Native.cs), Plugins/x86_64/xploit_game_ui.dll + DLL V8, Shaders/, Editor/, Tests/
│   └── Sandbox/  проект Unity 6000.5.1f1 (D3D12), Assets/StreamingAssets/UI/MainMenu/{index.html,style.css,app.js,images/,fonts/}, PlayMode-тесты
├── docs/  PLAN.md (этот план), ADR-0001-stack.md, THIRD-PARTY-NOTICES.md, architecture.md, threading.md, css-support.md
└── tools/ build.ps1 (конфигурация + сборка native, копирование DLL в UPM-пакет)
```

## Модель потоков (`docs/threading.md`)

- **Главный поток Unity**: C# API `HtmlView`; подача ввода; в `Update()` вычитывает очередь native→C# (события, логи консоли, RPC-запросы); только здесь вызывается Unity API.
- **Runtime-поток** (один на процесс, принадлежит `xploit_game_ui.dll`): за кадр для каждого view: ввод → DOM-события → таймеры/rAF → microtask checkpoint V8 → style → layout → paint → публикация `SkPicture` + dirty rect в слот «последний выигрывает». Изолят V8 (один на view) входит только в этом потоке, т.е. «JavaScript thread» из ТЗ — это runtime-поток (DOM API должны быть синхронными из JS). Поток пробуждается по `xgu_view_tick` из `Update()` Unity или по входящим сообщениям, так что кадры UI идут в темпе кадров Unity.
- **Submission-поток Unity** (плагинное событие с `kUnityD3D12GraphicsQueueAccess_Allow`): колбэк `IssuePluginEventAndData` берёт последний picture, проигрывает его в `SkSurface` поверх D3D12-текстуры view, делает flush с `BackendSurfaceAccess::kPresent` (ресурс остаётся в `COMMON`) и submit в очередь Unity. `GrDirectContext` создаётся лениво в первом колбэке и используется только в этом потоке.
- Межпоточные контракты: очереди под мьютексом с ограничением; слот picture на view; уничтожение/resize view откладывают GPU-ресурсы в список, освобождаемый по frame fence Unity; domain reload уничтожает все view; device reset/loss бросает контекст.

## Путь рендера (Unity D3D12 ↔ Skia Ganesh) — проверено по локальным заголовкам Unity и Skia main

**Жизненный цикл плагина** (`native/src/platform/unity/UnityPlugin.cpp`)
- `UnityPluginLoad`: взять `IUnityLog`, `IUnityGraphics`; `ReserveEventIDRange(XGU_EVT_COUNT)`; зарегистрировать device-event колбэк и один раз вызвать его с `Initialize` (документированный обход пропуска события).
- На `Initialize` при `GetRenderer()==kUnityGfxRendererD3D12`: получить `IUnityGraphicsD3D12v8` (GUID в заголовке помечен `// TODO`, поэтому fallback на `v7`); `ConfigureEvent(PAINT, {Allow, FlushCommandBuffers|SyncWorkerThreads})`. Skia пишет собственные command list'ы, а `submit()` вызывает `ExecuteCommandLists`+`Signal` на `GrD3DBackendContext::fQueue`, поэтому нужен `GetCommandQueue` (легален только в режиме `Allow`), а `CommandRecordingState` не подходит. Любой другой renderer (D3D11, Null/`-nographics`) выбирает CPU-провайдер.
- `GrDirectContext` создаётся в первом колбэке PAINT: `fDevice=GetDevice()`, `fQueue=GetCommandQueue()`, адаптер ищется по `GetAdapterLuid()` (никогда не адаптер 0: на машине есть AMD iGPU), `fMemoryAllocator=nullptr` (Skia создаёт свой D3D12MA), `GrContextOptions.fPersistentCache` на диске для компиляции шейдеров SkSL→SPIR-V→HLSL.
- `BeforeReset`/`Shutdown`: `submit(GrSyncCpu::kYes)`, сброс surfaces, `releaseResourcesAndAbandonContext()`, освобождение ресурсов. `UnityPluginUnload` снимает регистрацию и повторяет shutdown, если тот был пропущен.

**Текстура на view** (`native/src/render/skia/D3D12TextureProvider.cpp`)
- Ресурс создаёт плагин: committed `B8G8R8A8_UNORM`, `ALLOW_RENDER_TARGET | ALLOW_SIMULTANEOUS_ACCESS`, начальное состояние `COMMON`; оборачивается `GrBackendTextures::MakeD3D` + `SkSurfaces::WrapBackendTexture(kBottomLeft_GrSurfaceOrigin, kBGRA_8888, premul)`.
- Состояния: D3D-backend Skia игнорирует `MutableTextureState`; `flush(surface, kPresent)` переводит в `PRESENT == COMMON`. Из `COMMON` D3D12 неявно повышает до `PIXEL_SHADER_RESOURCE` при сэмплировании Unity, а simultaneous-access ресурсы деградируют обратно в `COMMON` после каждого `ExecuteCommandLists`, поэтому отслеживаемое Skia состояние остаётся верным. Спайк Этапа 1 проверяет это под `-force-d3d12-debug`.
- Передача в C#: `Texture2D.CreateExternalTexture(w,h,TextureFormat.BGRA32, mipChain:false, linear:false, ptr)` (Skia пишет sRGB-кодированные premultiplied пиксели) и материал `Blend One OneMinusSrcAlpha` на RawImage.
- Запасной провайдер `D3D12Copy` (нужен v8): Skia рисует в приватную текстуру; второе событие (режим `DontCare`) через `CommandRecordingState` + `RequestResourceState(unityTex, COPY_DEST)` + `CopyResource` + `NotifyResourceState` копирует в `Texture2D`, созданную в C#. Одно GPU-копирование, никаких допущений о состояниях.
- Resize: запоминается в главном потоке, применяется в следующем PAINT (новый ресурс, старый откладывается с тегом `GetNextFrameFenceValue()`); флаг статуса `TEXTURE_RECREATED` заставляет C# вызвать `UpdateExternalTexture(newPtr)`.

**Протокол кадра**
- Runtime-поток: `SkPictureRecorder` → `{picture, dirtyRect, frameId}` в один слот «последний выигрывает» (picture неизменяем и refcounted, тройной буфер не нужен). Изображения — растровые `SkImage`, Skia загружает их на GPU лениво в submission-потоке, так что вся GPU-работа в одном потоке.
- C# `Update()`: если `xgu_view_has_pending_frame(id)` → `cb.IssuePluginEventAndData(fn, base+PAINT, (IntPtr)id)`; `Graphics.ExecuteCommandBuffer`. `data` — 64-битный id view `{index, generation}`, не указатель на кучу, поэтому устаревшие события после destroy безопасны.
- Колбэк: найти view → обеспечить контекст/surface → `clipIRect(dirty)`, `clear`, `drawPicture` → `flush(kPresent, GrFlushInfo{fFinishedProc})` → `submit(kNo)` → `checkAsyncWorkCompletion`, периодически `performDeferredCleanup`, обработка отложенных освобождений по `GetFrameFence()->GetCompletedValue()`. Frame fence Unity никогда не ждётся покадрово.

**Другие провайдеры за `ITextureProvider`**
- `CpuRaster`: `SkSurfaces::Raster(RGBA8 premul)` в runtime-потоке, с переворотом по Y; C# `xgu_view_acquire_pixels` → `LoadRawTextureData` + `Apply` → `xgu_view_release_pixels`. Для D3D11-пользователей, CI с `-nographics` и попиксельных golden-тестов.
- `VulkanInterop` (на случай проблем Ganesh D3D): собственный VkDevice на адаптере с тем же LUID; D3D12-текстура с `HEAP_FLAG_SHARED`, импорт через `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT`; shared `ID3D12Fence` как timeline semaphore; Unity ждёт `GetCommandQueue()->Wait(fence, v)` в событии `Allow`.

**C ABI рендера** (`native/include/xploit_game_ui/xgu.h`, главный поток, если не указано иное)
`xgu_render_provider`, `xgu_render_event_base`, `xgu_get_render_event_func`, `xgu_view_create(w,h,format,provider_pref)`, `xgu_view_destroy` (отложенно), `xgu_view_resize`, `xgu_view_get_native_texture(id,&w,&h)` (валидно после первого PAINT), `xgu_view_set_unity_texture` (copy-провайдер), `xgu_view_has_pending_frame`, `xgu_view_status` (флаги `TEXTURE_READY|TEXTURE_RECREATED|DEVICE_LOST|PIXELS_READY`), `xgu_view_acquire_pixels/release_pixels`, `xgu_views_destroy_all` (domain reload).

**Критерии выхода спайка Этапа 1**: standalone Win32-приложение и Unity Sandbox показывают кадр Skia (скруглённый прямоугольник, PNG, текст skparagraph); `GetD3DTextureResourceInfo(bt).fResourceState == COMMON` после flush; ноль строк `D3D12 ERROR` в `Editor.log` под `-force-d3d12 -force-d3d12-debug`; без утечек за 10 циклов Play/Stop, перекомпиляция скриптов в Play, resize, `-force-device-index 1` (второй GPU), а `-force-d3d11` откатывается на CPU-провайдер.

## Дизайн ядра (проверено по заголовкам lexbor 3.x, Yoga 3.2.1, skparagraph m148)

**Базовые правила**
- Один runtime-поток на процесс; каждый view внутри него однопоточный. Между потоками ходят только неизменяемые `DisplayList` (`SkPicture`) и `BridgeMessage`.
- Триплет vcpkg `x64-windows-static-md`: все статические зависимости используют /MD CRT, как DLL V8 и Unity. `xploit_game_ui.dll` = оболочка C ABI + точки входа плагина Unity; всё остальное — статические библиотеки (`xgu_core, xgu_dom, xgu_html, xgu_css, xgu_layout, xgu_text, xgu_paint, xgu_js, xgu_bridge, xgu_input, xgu_runtime`). V8 поставляется компонентными DLL (`v8.dll, v8_libbase.dll, v8_libplatform.dll`, zlib/abseil, `icudtl.dat`, `*_blob.bin`), копируемыми рядом; `v8.monolith` — опция CMake позже. Статический ICU Skia и ICU V8 — две копии, принято для MVP.
- Все 8 интерфейсов — в `native/src/core/interfaces/`. Реализации по умолчанию: `LexborHtmlParser`, `css::StyleEngine` (поверх `ICssParser` → `LexborCssParser`), `YogaLayoutEngine`, `SkiaPainter` (записывает display list; GPU-растеризация — забота провайдера текстур), `D3D12TextureProvider`/`CpuTextureProvider`, `V8Runtime`, `QueuedInputProvider`, `QueueBridge`. Слушатели DOM независимы от движка (`dom::ListenerCallback` виртуальный; `js::JsListenerCallback` оборачивает `v8::Global<Function>`), поэтому JS-рантайм действительно заменяем.

**DOM (`native/src/dom/`)** — собственный C++ DOM; lexbor используется только для парсинга документов и фрагментов (`lxb_html_document_parse_fragment`), после чего его дерево конвертируется и уничтожается. Причины: intrusive refcount-узлы, разделяемые деревом, JS-обёртками и событиями в полёте; `ComputedStyle`/`LayoutBox`/dirty-биты/слушатели/слот обёртки как обычные поля; API мутаций, спроектированный под нашу инвалидацию; модуль `selectors` lexbor реализует `:hover/:active/:focus` через литеральные атрибуты, так что свой matcher нужен в любом случае. Модель: `Node{type, parent(raw), firstChild/nextSibling(strong), lastChild/prevSibling(raw), dirty StyleSelf|StyleChildren|LayoutTree|Layout|PaintSelf|PaintChildren, EventTargetData, WrapperSlot}`, `Element{tag atom, knownTag enum, attrs, id, ClassList, StateFlags Hover|Active|Focus|FocusWithin|Disabled|Checked, inlineStyle, RefPtr<ComputedStyle>, unique_ptr<LayoutBox>, unique_ptr<TextControl>}`, `Text{utf8 data}`, `Document{documentElement/head/body, IdMap, hovered/active/focused, MutationSink*}`. Собственный `Serializer` для геттера `innerHTML`. Каждая мутация идёт через `Document::sink->onMutation`, который ставит dirty-биты и уведомляет style engine и JS-модуль.

**CSS (`native/src/css/`)** — `LexborCssParser` использует lexbor для токенизации, структуры правил, селекторов и ~45 типизированных свойств (display, box model, position/insets, z-index, flex-*, justify/align-*, background-color, color, opacity, font-*, line-height, text-align, text-decoration, white-space, overflow-x/y, box-sizing…). Свойства, которые lexbor оставляет как `lxb_css_property__custom_t` (border-radius, background-image/size/position/repeat, box-shadow, transform, transform-origin, gap/row-gap/column-gap, cursor, pointer-events), идут через наш `ValueParser` (~400 строк). Результат — собственный `RuleSet{StyleRule{Selector, decls, order, origin}}`; шортхенды (margin, padding, border*, border-radius, flex, flex-flow, gap, background, text-decoration, overflow, inset) раскрываются в longhand при парсинге. Fallback, если lexbor не сохраняет custom-значения дословно: собственный токенизатор ~1.5k строк за тем же `ICssParser` (спайк в начале Этапа 4). `RuleIndex` группирует правила по правому compound (id > class > tag > universal); `SelectorMatcher` идёт справа налево (descendant, child, `+`, `~`, атрибутные селекторы, `:hover/:active/:focus/:focus-within/:disabled/:checked/:first-child/:last-child/:not()`); ключ каскада `(layer UA<author<inline, important, specificity, order)`; `ComputedStyle` разрешает наследование, `initial`, `em/rem/vw/vh`, `currentColor`; проценты box-свойств остаются процентами для Yoga. Diff старого/нового `ComputedStyle` с предвычисленной маской решает `Layout` vs `PaintSelf` vs пересчёт поддерева. `StateUsage` запоминает, какие псевдо-состояния встречаются (и в неправых compound'ах ли), чтобы hover инвалидировал только то, что может измениться. UA-стили `native/src/html/ua.css` встраиваются ресурсом (block по умолчанию, `button/input/textarea` как inline-block с border-box, `[hidden],head,script,style{display:none}`).

**Layout (`native/src/layout/`, `native/src/text/`)** — `LayoutTreeBuilder` строит `LayoutBox`: блочные контейнеры, все дети которых inline-level, получают один анонимный inline formatting context (IFC); смешанные дети — анонимные IFC-прогоны; flex-элементы блокифицируются; `display:none` — без бокса; `display:contents` → `YGDisplayContents`. `StyleToYoga`: block = column flex, `AlignItems(Stretch)`, дети grow/shrink 0, `margin:auto` через `YGNodeStyleSetMarginAuto`; flex = прямое отображение, включая `Gap(YGGutter)`; `position` → `YGPositionType` (static/relative/absolute), insets по сторонам; `overflow` → `YGOverflow`; `box-sizing` задаётся явно; `YGConfig` на view с `UseWebDefaults(true)`, `PointScaleFactor(dpr)`. IFC-бокс — `YGNodeTypeText` с measure + baseline функциями: `RunBuilder` превращает DOM-диапазон в UTF-16 текст + прогоны `TextStyle` (семейства, `SkFontStyle`, размер, цвет, decoration, letter-spacing, инлайновый фон, `setHeight/HeightOverride` для `line-height`, `text-transform`), `WhiteSpace::collapse` по `white-space`, атомарные инлайны (`img/button/input/textarea/inline-block`) как `addPlaceholder(PlaceholderStyle)` с размером из предварительного layout их Yoga-поддерева; `getRectsForPlaceholders()` позиционирует их после `layout(width)`. `nowrap + overflow:hidden + text-overflow:ellipsis` → `setMaxLines(1)+setEllipsis`. Measure кэширует две последние ширины; `FontCollection::getParagraphCache` кэширует shaping. Шрифты: `setDefaultFontManager(SkFontMgr_New_DirectWrite(), "Segoe UI")`, `setAssetFontManager(SkFontMgr_New_Custom_Directory(<UIRoot>/fonts))`, `SkUnicodes::ICU::Make()` один раз на процесс; тесты используют `setTestFontManager` со шрифтами из `testdata/fonts` для детерминизма. `img` — `ReplacedBox` с measure по intrinsic size; завершение декодирования помечает его dirty и диспатчит `load`. После `YGNodeCalculateLayout` боксы с `HasNewLayout` получают border-box в координатах документа; layout в CSS px, painter применяет один `canvas->scale(dpr)`. Задокументированные отклонения MVP: без margin collapsing, без float, инлайновый элемент с блочными детьми ведёт себя как inline-block, инлайновые границы/паддинги игнорируются, `overflow:hidden` обрезает и out-of-flow потомков, без маркеров списков.

**Paint (`native/src/paint/`)** — `SkiaPainter::paint` → `DisplayList{SkPicture, dirtyDevice, frameId, viewportPx, dpr}`; dirty rect в MVP — весь viewport; на чистых кадрах ничего не публикуется. Stacking contexts: корень, positioned с `z-index != auto`, `opacity < 1`, `transform != none`. Порядок в контексте: transform (`T(origin)·M·T(-origin)`) и `saveLayerAlphaf` при необходимости → box-shadow (RRect + `SkMaskFilter::MakeBlur`, вырезанный из border box) → цвет фона (`drawRRect`) → фоновое изображение (`drawImageRect` или repeat-шейдер) → граница (`drawDRRect`) → `clipRRect` для `overflow:hidden` → контексты с отрицательным z-index → in-flow боксы в порядке дерева (IFC — `paragraph->paint`, оверлеи каретки/выделения) → positioned `z-index:auto/0` → положительные z-index → restore. `ImageCache` по разрешённому пути, декодирование в пуле воркеров (`SkCodec` PNG/JPEG/WebP → растровый `SkImage`); загрузка на GPU лениво в submission-потоке при проигрывании picture. Не входит в MVP: кэш picture на бокс, dirty regions, inset shadows, градиенты, `border-image`.

**Ввод и события (`native/src/input/`)** — `InputEvent{type Mouse*/Wheel/Key*/TextInput/Touch*/WindowBlur, x,y,dx,dy, button(s), modifiers, XguKey, text[16], touchId, time}`. `HitTester` идёт в обратном порядке отрисовки, инвертирует трансформы, отсекает по клипам `overflow:hidden`, учитывает `pointer-events:none`, текстовые попадания через `getGlyphPositionAtCoordinate`. `InputRouter`: diff цепочки hover → `mouseout/leave`, `mouseover/enter`, `mousemove`, флаги `Hover`; `mousedown` ставит `Active` и фокус (`FocusManager`: фокусируемые `input/textarea/button/[tabindex]`, перенаправление `label`, цикл Tab, `WindowBlur` сбрасывает); `mouseup` снимает и синтезирует `click` на общем предке целей down/up, `dblclick` в 500 мс; клавиши — сфокусированному элементу (иначе `body`) со всплытием до `document/window`; `TextInput` → `beforeinput`+`input`; касания зеркалятся в мышь для первого пальца. `EventDispatcher`: capture → target → bubble с `stopPropagation`/`stopImmediatePropagation`/`once`/`preventDefault`; действия по умолчанию после диспатча. `TextControl` для `input/textarea`: UTF-16 value, каретка/anchor, вставка/Backspace/Delete/стрелки/Home/End/Enter/Ctrl+A, прямоугольники каретки и выделения из `getRectsForRange`, установка мышью через `getGlyphPositionAtCoordinate`, мигание 530 мс только с `PaintSelf`, маскирование `type=password`, placeholder; Unity передаёт уже составленные IME-строки, поэтому UI композиции нет.

**JavaScript (`native/src/js/`)** — `V8Platform` — синглтон процесса, создаётся в `xgu_initialize` (`NewDefaultPlatform`, `InitializeICUDefaultLocation`, `InitializeExternalStartupDataFromFile`, если пакет поставляет blob'ы, `Initialize`) и никогда не уничтожается (Unity держит native-плагины загруженными через domain reload). На view: `Isolate` со стандартным аллокатором и лимитом old-gen 128 МБ, `MicrotasksPolicy::kExplicit`, message listener + promise-reject колбэк → `ErrorReporter` (формат `Uncaught <name>: <msg>\n    at fn (file:line:col)`), динамический `import()` отклоняется, `IsolateData` в слоте 0 владеет `WrapperTemplates` (FunctionTemplate на интерфейс с цепочками `Inherit`: `EventTarget < Node < Element/Text/Document`, `Event < MouseEvent/KeyboardEvent/InputEvent/FocusEvent/WheelEvent`, `CSSStyleDeclaration`, `DOMTokenList`, `NodeList`, `HTMLCollection`, `Console`, `Unity`; 2 internal fields: указатель + тег типа). Глобалы: `window/globalThis`, `document`, `console`, `setTimeout/setInterval/clear*`, `requestAnimationFrame`, `performance.now`, минимальный `getComputedStyle`, `Unity`. Время жизни обёрток: обёртка держит `ref()` узла; **strong пока узел подключён, weak когда отсоединён** (`ClearWeak` при подключении, `SetWeak(kParameter)` при отсоединении; second-pass колбэк делает unref). Известная форма утечки (замыкание слушателя, захватившее отсоединённый узел) задокументирована; `CppHeap`/`TracedReference` — путь апгрейда. Таймеры в `TimerHeap`, выбираются за кадр; rAF один раз за кадр; microtask checkpoint после каждого колбэка. `console.*` → `ILogSink` → сообщение `Log` через мост → `Debug.Log/LogWarning/LogError`. `ScriptLoader` исполняет `<script>` (inline или `src` через `IAssetLoader`) в порядке документа с `ScriptOrigin(относительный путь)`; `type="module"` предупреждает и пропускается; затем `DOMContentLoaded`, `load`, состояние → JsReady. Безопасность: нет `fetch/XMLHttpRequest/WebSocket/import()`, `--no-expose-wasm`, `FileAssetLoader` канонизирует и отклоняет всё, что выходит за корень UI (абсолютные пути, буквы дисков, UNC, `..`, URL-схемы). Порядок dispose: остановить ввод/мост → сбросить все Global → освободить контекст → `Isolate::Dispose` → освободить DOM/layout/paint → очистить mailbox.

**Мост (`native/src/bridge/`)** — `BridgeMessage{kind Emit|Send|Call|Reply|Log|Lifecycle, id, level, ok, name, json}`. Ядро не интерпретирует payload: JS использует `v8::JSON::Stringify/Parse`, C# — встроенный `WebJson` (~400 строк), поэтому C++ JSON-библиотека в рантайме не нужна (nlohmann-json только в тестах/`xgu_cli`). `QueueBridge` = две очереди mutex+deque; runtime→host вычитывается C# в `Update` через `xgu_view_poll_message`; host→runtime — в начале каждого кадра runtime. RPC id на view; C# выполняет обработчики `RegisterFunction` (sync или `Task`) в главном потоке и вызывает `xgu_view_reply(id, ok, json)`; исключения отвечают `ok=false` с `{name,message,stack}` → rejected promise. Лимиты: payload > 4 МБ отклоняется с ошибкой в консоль; исходящая очередь до 10k с выбросом самых старых `Emit`/`Log` (никогда `Call`/`Reply`); `MouseMove` коалесцируется. C# `object[]` ↔ JSON: примитивы/массивы/`IList`/`IDictionary<string,object>`/`WebArguments` нативно, прочие `[Serializable]` типы через `JsonUtility` как листовые значения; входящие значения — дерево `WebValue` с `Get<T>(i)`/`GetObject<T>(i)`.

**C ABI (`native/include/xploit_game_ui/xgu.h`, extern "C", UTF-8)** — хэндлы `uint64_t xgu_view_id {index:32, generation:32}` (устаревшие id отклоняются; тот же id — `data` для render-событий). Процесс: `xgu_initialize(XguInitDesc*)`, `xgu_shutdown`, `xgu_set_log_callback`, `xgu_version`. View: `xgu_view_create(XguViewDesc{ui_root, w, h, dpr, format, provider_pref})`, `xgu_view_destroy`, `xgu_view_load(rel_path)`, `xgu_view_load_html(html, base_dir)`, `xgu_view_reload`, `xgu_view_resize(w,h,dpr)`, `xgu_view_set_paused`, `xgu_view_tick(time)`, `xgu_view_execute_js(src, origin)`, `xgu_view_send_event(name, json)`, `xgu_view_reply(id, ok, json)`, `xgu_view_send_input(XguInputEvent*)`, `xgu_view_poll_message(XguMessage*)`, `xgu_view_get_state`, плюс функции рендера из раздела выше.

**C# API (`unity/Packages/com.xploit.game_ui/Runtime/`, namespace `Xploit.GameUI`)** — `Native.cs` (P/Invoke `[DllImport("xploit_game_ui")]`); `HtmlView : MonoBehaviour` с `Load/Reload/ExecuteJS/Send(name, params object[])/On/Off/RegisterFunction/RegisterFunctionAsync/Resize/Paused/State/Texture/EnableDebug` и событиями `DomReady/JsReady/Interactive/Log`; `HtmlViewManager` (синглтон, `DontDestroyOnLoad`, ленивый `xgu_initialize`, тикает view и вычитывает сообщения в `Update`, уничтожает все view на `AssemblyReloadEvents.beforeAssemblyReload`/`OnApplicationQuit`); `WebEvent{Name, Args, View}`; `WebArguments`; `WebJson`; `WebInput` (legacy Input или Input System → `XguInputEvent`, экран → координаты view через целевой `RectTransform`, `KeyCode` → `XguKey`); `WebTexture` (внешняя текстура, обработка resize, premultiplied-материал).

**Тестирование (`native/tests/`, `native/tools/xgu_cli/`)** — GoogleTest + gmock через vcpkg. Unit: таблица атомов, операции DOM/id map/сериализатор, CSS-парсер → `RuleSet`, matcher селекторов + специфичность, каскад (наследование, единицы, `!important`, inline), отображение в Yoga, measure IFC, порядок диспатча событий, hit testing с трансформами/клипами, редактирование текста, backpressure моста. Golden: `testdata/golden/*.html` рендерятся через `SkSurfaces::Raster` тестовыми шрифтами, PNG сравниваются по каналам (допуск 8/255, ≤ 0.1 % пикселей), `--update` перегенерирует, при падении пишутся actual/diff PNG. JS smoke: страницы отчитываются через `Unity.emit("test", {...})`; `xgu_cli run page.html --frames N --input events.json --replies replies.json` работает headless. `xgu_cli render in.html out.png --width --height --dpr` и `xgu_cli layout in.html` (JSON боксов) для CI. PlayMode-тесты Unity по этапам из таблицы.

**Спайки в начале своего этапа** (каждый ≤ 1 дня, результаты в ADR)
- Этап 1: собрать `skia[direct3d,harfbuzz,icu,freetype,png,jpeg,webp]:x64-windows-static-md`, слинковать hello-paragraph exe, зафиксировать точные имена CMake-таргетов skparagraph/skshaper/skunicode; включить бинарный кэш vcpkg; затем спайк D3D12/Unity.
- Этап 2: smoke exe с NuGet V8; подтвердить defines pointer-compression/sandbox из поставляемых `v8config.h`/`v8_build_config.json` (`.props` их не задаёт); решить компонентные DLL vs `v8.monolith`; рано проверить domain reload в редакторе.
- Этап 4: прогнать полный список свойств MVP через lexbor и сдампить декларации; проверить, что `_custom`-значения сохраняются дословно, иначе переключить `ICssParser` на собственный токенизатор.

## План по этапам (этапы 0–10 ТЗ с критериями приёмки)

| Этап | Результат | Критерий приёмки | Оценка |
|---|---|---|---|
| 0 Feasibility / ADR | `docs/PLAN.md`, `docs/ADR-0001-stack.md`, `docs/THIRD-PARTY-NOTICES.md`, `docs/threading.md`, `docs/architecture.md`, `docs/css-support.md` | Проверено; у каждого компонента заполнены repo/license/redistribution/modification/attribution/source disclosure | 2 д |
| 1 Native Hello World | `xploit_game_ui.dll` + UPM-пакет + Sandbox; Unity→C++ (`xgu_ping`), C++→Unity (`IUnityLog`); **спайк Skia D3D12**: статический кадр (скруглённый прямоугольник, картинка, текст) на RawImage | PlayMode-тест: ping возвращает 42, native-лог виден, readback внешней текстуры даёт ненулевую альфу в центре прямоугольника | 5–7 д |
| 2 V8 | `V8Runtime : IJavaScriptRuntime`, изолят на view, `console.*` → Unity Console, runtime-поток + очереди, `ExecuteJS` | `view.ExecuteJS("console.log('Hello')")` печатает в Unity Console; JS-исключение логирует сообщение + стек | 4–6 д |
| 3 HTML/DOM | lexbor → собственный DOM, `Load(path)` в пределах корня UI, `<script src>`, `document.getElementById/querySelector(All)`, `textContent`, API мутаций, жизненный цикл DOM Ready/JS Ready | `<div id="a">Hello World</div>` → JS читает/пишет `textContent`; тесты на форму дерева, парсинг фрагментов, стабильность обёрток, loader отклоняет `../x.js` | 5–7 д |
| 4 CSS/Layout | `LexborCssParser` + `ValueParser` (подмножество MVP), matcher селекторов, каскад/наследование, `ComputedStyle`, UA-стили, отображение в Yoga, инлайновый текст через skparagraph, `xgu_cli layout` | Golden-тесты layout (боксы как JSON) для ~20 фикстур flex/block/text в пределах ±1 px от измерений Chrome | 12–15 д |
| 5 GPU renderer | Render tree → `SkPicture`; фоны, границы+radius, box-shadow, изображения, текст, opacity, transform, overflow clip; провайдеры D3D12 + CPU raster; `xgu_cli render` | `index.html` + `style.css` с «Hello World» отображаются на RawImage; PNG golden-тесты с допуском | 8–10 д |
| 6 Input | Мышь/клавиатура/текст/касания Unity → C ABI → hit test → DOM-события (capture/bubble), hover/active/focus, редактирование текста в `input`/`textarea` | Клик мышью Unity по `<button id="button">CLICK</button>` вызывает JS-слушатель; PlayMode-тест управляет `WebInput` | 6–8 д |
| 7 JS bridge | `Unity.emit/on/call` ↔ `view.On/Send/RegisterFunction`, JSON payload, promise RPC, маршалинг в главный поток | Сценарий MVP из ТЗ: PLAY → `On("play")`; `Send("healthChanged",75)` → JS обновляет DOM | 4–5 д |
| 8 Unity API | `HtmlView`, `HtmlViewManager`, `WebEvent`, `WebArguments`, `WebTexture`, `WebInput`, инспектор, `EnableDebug`, примеры | Unity-разработчик реализует пример `HUD` из §21 ТЗ без единой строки C++ | 4–5 д |
| 9 World Space | Текстура на материале/квадe; UV точки raycast → координаты view для ввода | HTML-кнопка на 3D-квадe кликабельна | 2–3 д |
| 10 Оптимизация | Dirty rects, кэш слоёв, атласы глифов/изображений, кэш шрифтов, кэш стилей, батчинг, урезание ICU, D3D11 через shared texture (опционально) | Профилировочные цели на пункт; без функциональных регрессий в golden-тестах | постоянно |

Итого до MVP (этапы 0–8): примерно 50–65 человеко-дней для одного разработчика.

## Первый срез: Этап 0 + Этап 1 — конкретные задачи

### Этап 0 — документы
1. `docs/PLAN.md`: этот план (обновляется по этапам).
2. `docs/ADR-0001-stack.md`: решения, таблица альтернатив (Servo, LibWeb, Blink/CEF, RmlUi, Blitz), выбранный стек, риски и запасные варианты.
3. `docs/THIRD-PARTY-NOTICES.md`: строка на компонент с Repository / License / Commercial redistribution / Modification / Attribution / Source disclosure, плюс полные тексты лицензий для поставки в пакете.
4. `docs/threading.md` и `docs/architecture.md` (диаграмма пайплайна из ТЗ, восемь интерфейсов, машина состояний жизненного цикла).
5. `docs/css-support.md`: матрица поддержки HTML/CSS/DOM для MVP (обновляется по этапам).

### Этап 1 — скелет native и спайк D3D12
6. `native/CMakeLists.txt`, `native/CMakePresets.json` (пресеты `x64-windows-debug|release`, триплет `x64-windows-static-md`, `VCPKG_ROOT` = vcpkg из VS 2026, `VCPKG_BINARY_SOURCES` файловый кэш), `native/vcpkg.json` (`skia[direct3d,freetype,harfbuzz,icu,png,jpeg,webp]`, `lexbor`, `yoga`, `gtest`, `nlohmann-json`), `native/vcpkg-configuration.json` (закреплённый baseline со skia 148 / lexbor 3.0.1 / yoga 3.2.1), `.gitignore`, `.clang-format`, `.editorconfig`. Первое действие этапа — спайк сборки Skia (зафиксировать имена таргетов skparagraph/skshaper/skunicode).
7. `native/third_party/unity/PluginAPI/*.h` — копия из `C:\Program Files\Unity\Hub\Editor\6000.5.1f1\Editor\Data\PluginAPI\` вместе с `LICENSE.md`.
8. `native/include/xploit_game_ui/xgu.h` — C ABI v0 (хэндлы `uint64_t xgu_view_id {index, generation}`): `xgu_version`, `xgu_initialize`, `xgu_shutdown`, `xgu_set_log_callback`, `xgu_ping`, `xgu_render_provider`, `xgu_render_event_base`, `xgu_get_render_event_func`, `xgu_view_create(w,h,format,provider_pref)`, `xgu_view_destroy`, `xgu_view_resize`, `xgu_view_get_native_texture`, `xgu_view_has_pending_frame`, `xgu_view_status`, `xgu_view_acquire_pixels/release_pixels`, `xgu_views_destroy_all`, `xgu_view_draw_test_frame` (только для спайка, удаляется на Этапе 5).
9. `native/src/core/` — `Log.h/.cpp` (абстракция sink), `Runtime.h/.cpp` (синглтон процесса; runtime-поток появляется на Этапе 2), `View.h/.cpp`, `ViewRegistry` (id с поколениями).
10. `native/src/render/skia/` — `D3D12GrContext.cpp` (GrDirectContext из `ID3D12Device`+`ID3D12CommandQueue`), `D3D12TextureProvider.cpp`, `CpuTextureProvider.cpp`, `TestFrame.cpp` (скруглённый прямоугольник, декодированный PNG, текст skparagraph).
11. `native/src/platform/unity/UnityPlugin.cpp` — `UnityPluginLoad/Unload`, device-события `IUnityGraphics`, получение `IUnityGraphicsD3D12v8` (fallback v7), sink `IUnityLog`, колбэк render-события (детали в «Пути рендера»).
12. `native/src/platform/standalone/main.cpp` — Win32-окно + D3D12 swapchain + Skia с тем же тестовым кадром (проверка Skia D3D12 без Unity).
13. `native/tests/` — GoogleTest: `xgu_ping`, проверка поколений id view, CPU raster golden-тест тестового кадра (`tests/testdata/golden/test_frame.png`).
14. `native/tools/xgu_cli/` — версия Этапа 1 рендерит тестовый кадр в PNG (вырастает в инструмент HTML→PNG).
15. `tools/build.ps1` — конфигурация, сборка, ctest, копирование `xploit_game_ui.dll` (позже и DLL V8) в `unity/Packages/com.xploit.game_ui/Plugins/x86_64/`.
16. `unity/Packages/com.xploit.game_ui/` — `package.json` (`"unity": "6000.2"`), `Runtime/Native.cs` (P/Invoke), `Runtime/HtmlView.cs` (Этап 1: создание view, `CreateExternalTexture`, свойство `Texture`; `Load/ExecuteJS/Send/On/RegisterFunction` бросают `NotImplementedException` до своего этапа), `Runtime/Xploit.GameUI.Runtime.asmdef`, `Shaders/RawImagePremultiplied.shader` + материал, `Editor/` (настройки импорта DLL), `Tests/Runtime/` PlayMode-тесты + asmdef.
17. `unity/Sandbox/` — проект Unity 6000.5.1f1, графический API Windows принудительно D3D12 (`m_Automatic: 0`, только D3D12), `Packages/manifest.json` со ссылкой на пакет по относительному `file:` пути, сцена `Stage1_HelloWorld` (Canvas → RawImage с premultiplied-материалом + `HtmlView`), `Assets/StreamingAssets/UI/MainMenu/` заготовки для следующих этапов.
18. `README.md` в корне: требования к сборке и запуск в две команды.

## Проверка (end-to-end)

Native:
```powershell
# Developer PowerShell for VS 2026, из D:\xploit_game_ui
$env:VCPKG_ROOT = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg"
cmake --preset x64-windows-release -S native
cmake --build --preset x64-windows-release
ctest --preset x64-windows-release --output-on-failure
.\native\out\x64-windows-release\bin\xgu_cli.exe --test-frame out.png                 # Этап 1 (CPU-провайдер); позже: xgu_cli render index.html out.png
.\native\out\x64-windows-release\bin\xgu_host.exe --debug                              # окно с тестовым кадром Skia D3D12 (Esc — выход)
.\native\out\x64-windows-release\bin\xgu_host.exe --debug --screenshot out.png --frames 3   # D3D12-путь без Unity: readback текстуры в PNG и выход
```
Unity (batchmode, без UI редактора):
```powershell
& "C:\Program Files\Unity\Hub\Editor\6000.5.1f1\Editor\Unity.exe" -batchmode -force-d3d12 -force-d3d12-debug -projectPath D:\xploit_game_ui\unity\Sandbox -runTests -testPlatform PlayMode -testResults D:\xploit_game_ui\build\test-results.xml -logFile D:\xploit_game_ui\build\editor.log
# Прогон с CPU-провайдером для headless CI (Null device): добавить -nographics; GPU-тесты НЕ должны использовать -nographics.
```
CI-скрипт падает, если в `editor.log` есть `D3D12 ERROR` или `CORRUPTION`.
Вручную: открыть Sandbox в Unity 6000.5.1f1, запустить `Stage1_HelloWorld`, убедиться, что RawImage показывает кадр Skia, а в Console есть `[xploit_game_ui] native log OK` (C++→Unity) и `ping=42` (Unity→C++). Далее ворота каждого этапа — критерии из таблицы; финальный PlayMode-тест — сценарий MVP из ТЗ (кнопка PLAY → `On("play")` → `Send("healthChanged", 75)` → обновление DOM).
