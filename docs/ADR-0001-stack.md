# ADR-0001. Технологический стек xploit_game_ui

- Статус: принято (2026-09-22)
- Область: native runtime (C++), интеграция с Unity, выбор open-source компонентов
- Связанные документы: [PLAN.md](PLAN.md), [architecture.md](architecture.md), [threading.md](threading.md), [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md), [css-support.md](css-support.md)

## Контекст

ТЗ требует Unity-плагин, использующий HTML/CSS/JavaScript как игровой UI (концептуально как Coherent Gameface), без Gameface, Ultralight, Vuplex и других коммерческих WebView SDK. Главный принцип — **не строить второй браузер**, а собрать быстрый, встраиваемый и контролируемый Game UI Runtime из open-source компонентов. Каждый компонент должен быть заменяем через интерфейсы `IJavaScriptRuntime`, `IHtmlParser`, `ICssEngine`, `ILayoutEngine`, `IRenderer`, `IInputProvider`, `IBridge`, `ITextureProvider`.

Целевая платформа MVP — Unity 6 (6000.2–6000.5), Windows x64, Direct3D 12 (Unity 6.1+ по умолчанию создаёт Windows-проекты с D3D12).

## Решение

| Задача | Компонент | Источник / версия | Лицензия |
|---|---|---|---|
| JavaScript | V8 13.0 | NuGet `v8-v143-x64` + `v8.redist-v143-x64` 13.0.245.25 (pmed); запасной путь — собственная сборка `v8_monolith` через depot_tools | BSD-3 |
| HTML-парсинг | lexbor 3.0.1, модуль `html` | vcpkg | Apache-2.0 |
| CSS-парсинг | lexbor, модуль `css` + собственный `ValueParser` для свойств, которые lexbor оставляет нетипизированными | vcpkg / собственный код | Apache-2.0 / — |
| CSS-каскад, селекторы, computed style | собственная реализация | `native/src/css` | — |
| Layout | Yoga 3.2.1 | vcpkg | MIT |
| Текст (shaping, перенос строк) | Skia `skparagraph`/`skshaper` (HarfBuzz + ICU) | vcpkg `skia[harfbuzz,icu,freetype,png,jpeg,webp,direct3d]` версии 148 | BSD-3 (+ MIT, Unicode, FTL) |
| 2D-рендер и GPU | Skia Ganesh, backend Direct3D 12 | тот же порт | BSD-3 |
| Изображения | libpng, libjpeg-turbo, libwebp через кодеки Skia | vcpkg | PNG / IJG+BSD / BSD-3 |
| Интеграция с Unity | Unity Native Plugin API (`IUnityGraphicsD3D12v8`, `IUnityLog`) | заголовки из Unity 6000.5.1f1 | Unity Companion License |
| Тесты | GoogleTest/gmock; Unity Test Framework | vcpkg / UPM | BSD-3 / Unity |
| Сборка | CMake 4.3 + Ninja + vcpkg из комплекта VS 2026, триплет `x64-windows-static-md`, `builtin-baseline` = коммит microsoft/vcpkg `24a726719eee20d452535529d361ee593c680d6c` (2026-09-22) | | |

DOM — собственный (lexbor используется только как парсер). JSON-библиотека в рантайме не нужна: JS сериализует через `v8::JSON`, C# — через встроенный `WebJson`; nlohmann-json подключается только в тестах и `xgu_cli`.

## Рассмотренные альтернативы

| Кандидат | Почему отклонён |
|---|---|
| **Servo** (Rust, MPL-2.0) | Встроенный SpiderMonkey вместо V8; embedding API нестабилен; тяжёлая сборка под Windows; полноценный браузерный движок |
| **LibWeb / Ladybird** (BSD-2) | Встроенный LibJS; нет embedding API; Windows официально не поддерживается |
| **Blink / Chromium Content / CEF** (BSD-3) | «Второй Chrome»: мультипроцесс, 100+ МБ, противоречит принципу ТЗ |
| **RmlUi 6.3** (MIT) | Ближайшая готовая «HTML/CSS game UI» библиотека с DX11/DX12-рендерерами, но RML/RCSS отклоняются от настоящего HTML/CSS и это одна монолитная зависимость. Остаётся задокументированным запасным «быстрым» путём (оценка 6–8 недель до MVP) |
| **Blitz** (Rust: Stylo + Taffy + Vello + Parley) | Настоящий CSS-движок Firefox, но beta, без JS/DOM-биндингов, рендер только через wgpu, рантайм на Rust. Кандидат на будущую замену `ICssEngine` |
| **Skia Graphite + Dawn** для D3D12 | Официальный преемник Ganesh D3D, но Dawn — отдельная тяжёлая зависимость, а работа поверх устройства Unity через Dawn сложнее. Отложено |
| **Собственный 2D-рендерер** вместо Skia | Возможен позже как реализация `IRenderer`; для MVP Skia даёт сглаживание, тени, скругления, трансформы, кодеки и текст «из коробки» |
| **QuickJS-NG** вместо V8 | Не соответствует критерию приёмки MVP («JS через V8»); возможен как второй backend `IJavaScriptRuntime` для быстрых тестов |
| **vcpkg-порт v8** | Версия 9.1 (2021), не обновляется |

## Ключевые архитектурные решения

1. **Рендер на устройстве Unity.** `GrDirectContext` Skia создаётся поверх `ID3D12Device`/`ID3D12CommandQueue` Unity (интерфейс `IUnityGraphicsD3D12v8`, плагинное событие с `kUnityD3D12GraphicsQueueAccess_Allow`); текстура view создаётся плагином как committed-ресурс с `ALLOW_RENDER_TARGET | ALLOW_SIMULTANEOUS_ACCESS` и передаётся в C# через `Texture2D.CreateExternalTexture`. Нулевое копирование, синхронизация — порядок отправки в одну очередь. Skia-backend D3D игнорирует `MutableTextureState`, поэтому после `flush(kPresent)` ресурс остаётся в `COMMON`, а D3D12 неявно повышает и деградирует состояние simultaneous-access ресурса.
2. **Display list между потоками.** Runtime-поток (DOM/CSS/layout/JS/paint) публикует неизменяемый `SkPicture`; submission-поток Unity проигрывает его в surface. Все обращения к `GrDirectContext` — в одном потоке.
3. **Изолят V8 на view, платформа V8 — на процесс**, никогда не уничтожается (Unity держит native-плагины загруженными через domain reload).
4. **Собственный DOM и каскад CSS**, парсеры lexbor только на входе. Это сохраняет заменяемость `IHtmlParser`/`ICssEngine` и позволяет держать style/layout/paint-данные прямо в узлах. Модуль `selectors` lexbor не используется: он моделирует `:hover/:active/:focus` как литеральные атрибуты DOM.
5. **Безопасность по умолчанию:** нет сети, нет `fetch/XHR/WebSocket/import()`, `--no-expose-wasm`, доступ к файлам только внутри корня UI через `IAssetLoader`, единственная поверхность native — объект `Unity` и `ExecuteJS` из C#.
6. **Хэндлы с поколениями.** View в C ABI — `uint64_t {index:32, generation:32}`; тот же id передаётся как `data` в `IssuePluginEventAndData`, поэтому устаревшие события после уничтожения view безопасны.

## Риски и запасные варианты

| # | Риск | Митигация / запасной вариант |
|---|---|---|
| 1 | Ganesh D3D backend Skia обсуждается к удалению (преемник Graphite+Dawn) | Закрепить Skia 148 через baseline vcpkg; `ITextureProvider` абстрактен; запасной вариант — Skia Vulkan на собственном устройстве с импортом D3D12 shared resource + shared fence |
| 2 | NuGet V8 — пакет одного мейнтейнера, последнее обновление 02.2025 | `IJavaScriptRuntime`; задокументированная сборка `v8_monolith` через depot_tools; локальный UI без сети |
| 3 | Сборка Skia через vcpkg 30–60 минут; известны сбои порта на Windows | Бинарный кэш vcpkg (`build/vcpkg-cache`); артефакт CI; при сбое — фиксировать патч порта в overlay `native/vcpkg-overlays` |
| 4 | Данные ICU (десятки МБ), две копии ICU (Skia и V8) | Принято для MVP; фильтры данных ICU на Этапе 10 |
| 5 | Premultiplied alpha + sRGB против стандартного UI-шейдера Unity | Шейдер `XploitGameUI/RawImagePremultiplied` (`Blend One OneMinusSrcAlpha`), внешняя текстура с `linear:false` |
| 6 | lexbor оставляет border-radius, background-*, box-shadow, transform, gap нетипизированными (`_custom`) | Собственный `ValueParser`; при потере данных — собственный токенизатор за `ICssParser` (спайк Этапа 4) |
| 7 | `.props` NuGet V8 не задаёт preprocessor defines (pointer compression/sandbox) | Спайк Этапа 2: взять флаги из `v8config.h`/`v8_build_config.json` |
| 8 | Unity не документирует ожидаемое состояние внешней D3D12-текстуры | Simultaneous access + flush `kPresent` → `COMMON`; запасной провайдер `D3D12Copy` через `RequestResourceState/NotifyResourceState` |
| 9 | Точность собственного инлайнового layout | Golden-корпус из реальных макетов HUD/меню; отклонения фиксируются в [css-support.md](css-support.md) |
| 10 | GUID `IUnityGraphicsD3D12v8` помечен в заголовке `// TODO: Get proper values` | Fallback на `IUnityGraphicsD3D12v7`; `D3D12Copy`-провайдер требует v8 и отключается при его отсутствии |

## Результаты спайков

| Дата | Спайк | Результат |
|---|---|---|
| 2026-09-22 | Сборка `skia[direct3d,freetype,harfbuzz,icu,png,jpeg,webp]:x64-windows-static-md` через vcpkg (baseline `24a72671…`) | **успех**. Skia 148 собралась за 6,5 мин (все 31 порт — 24 мин), бинарный кэш в `build/vcpkg-cache`. CMake-таргеты: `unofficial::skia::skia`, `unofficial::skia::modules::{skparagraph,skshaper,skunicode_core,skunicode_icu}`; заголовки подключаются как `<include/core/SkCanvas.h>`, `<modules/skparagraph/include/...>`. Отличия API m148: градиенты — `SkShaders::LinearGradient(pts, SkGradient)` из `include/effects/SkGradient.h` (файла `SkGradientShader.h` нет); `kNormal_SkBlurStyle` — в `include/core/SkBlurTypes.h`. `SK_GANESH`/`SK_DIRECT3D` задаём сами через `xgu_skia` |
| 2026-09-22 | Путь D3D12 вне Unity: `xgu_host --debug --screenshot` (собственное устройство, Skia Ganesh D3D12, readback текстуры в PNG) | **успех**: кадр (скруглённая панель, тень, градиент, PNG-картинка, текст skparagraph) читается с GPU и совпадает с CPU-рендером `xgu_cli` |
| 2026-09-22 | Путь D3D12 в Unity 6000.5.1f1 (batchmode, `-force-d3d12 -force-d3d12-debug`) | **успех**: PlayMode 6/6 — плагин загружается, `IUnityGraphicsD3D12v8` получен (GUID с пометкой TODO работает), контекст Skia создан на устройстве Unity (RTX 4070 SUPER) и корректно освобождён, кадр Skia считан из внешней текстуры (`CreateExternalTexture` + `Graphics.Blit` в RT + `AsyncGPUReadback`; напрямую readback BGRA8-внешней текстуры Unity не поддерживает), 10 циклов create/destroy без утечек хэндлов. Debug-слой D3D12 показал: (а) id 614 — несовпадение `SampleDesc.Quality` PSO/RTV из-за значения по умолчанию `fSampleQualityPattern` в `GrD3DTextureResourceInfo` — **исправлено** (`= 0` для обёрнутой текстуры); (б) id 1315 — `GetGPUDescriptorHandleForHeapStart` на CPU-heap внутри `GrD3DDescriptorHeap` Skia — безвредно, известная особенность backend'а; (в) id 1422 — промежуточные RT Skia из D3D12MA создаются `CREATE_NOT_ZEROED` и не очищаются явно — безвредно (содержимое перезаписывается полностью); при желании устраняется собственным `GrD3DMemoryAllocator` (Этап 10) |
| 2026-09-22 | Размер бинарников | `xploit_game_ui.dll` ≈ 58 МБ (RelWithDebInfo; статический Skia + данные ICU ≈ 30 МБ + FreeType/HarfBuzz/кодеки). Урезание ICU и `/OPT:REF` — Этап 10 |
| | V8 NuGet: defines, состав DLL | Этап 2 |
| | lexbor CSS: сохранение `_custom`-значений | Этап 4 |
