# Архитектура xploit_game_ui

## Слои

```
Unity C# (com.xploit.game_ui)
  HtmlView · HtmlViewManager · WebEvent · WebArguments · WebTexture · WebInput · WebJson
        │  P/Invoke (C ABI xgu_*, UTF-8, хэндлы uint64 с поколением)
        ▼
xploit_game_ui.dll  (оболочка C ABI + точки входа Unity Native Plugin API)
        │
        ▼
xgu_runtime (статическая библиотека; не зависит от Unity)
  ├── core     Runtime, View, ViewRegistry, интерфейсы, очереди, Log, AssetLoader, TimerHeap
  ├── html     LexborHtmlParser → DOM, UA-стили (ua.css)
  ├── dom      Node/Element/Text/Document, IdMap, Serializer, EventTarget/Event/EventDispatcher
  ├── css      LexborCssParser + ValueParser → RuleSet; RuleIndex, SelectorMatcher, StyleEngine, ComputedStyle
  ├── layout   LayoutTreeBuilder, LayoutBox, StyleToYoga, YogaLayoutEngine, InlineFormattingContext, HitTester
  ├── text     FontManager, RunBuilder, WhiteSpace, TextControl
  ├── paint    SkiaPainter → DisplayList (SkPicture), ImageCache/ImageDecoder, FrameMailbox
  ├── js       V8Platform, V8Runtime, IsolateData, WrapperTemplates, bindings/*, ScriptLoader, ErrorReporter
  ├── bridge   BridgeMessage, MessageQueue, QueueBridge, RpcTable
  ├── input    InputEvent, InputQueue, InputRouter, FocusManager, KeyNames
  └── render   D3D12GrContext, D3D12TextureProvider, D3D12CopyProvider, CpuTextureProvider
```

Адаптеры поверх `xgu_runtime`: `xploit_game_ui.dll` (Unity), `xgu_host` (Win32-окно для отладки без Unity), `xgu_cli` (headless: HTML → PNG, дамп layout, прогон JS), `xgu_tests`/`xgu_golden_tests`.

## Пайплайн кадра

```
HTML ──lexbor──▶ DOM ──StyleEngine──▶ ComputedStyle ──LayoutTreeBuilder──▶ LayoutBox + Yoga
   ──YGNodeCalculateLayout──▶ рамки боксов ──SkiaPainter──▶ SkPicture (DisplayList)
   ──FrameMailbox──▶ submission-поток ──SkSurface(D3D12)──▶ ID3D12Resource ──CreateExternalTexture──▶ RawImage / Material
```

Инвалидация — dirty-биты на узлах: `StyleSelf`, `StyleChildren`, `LayoutTree`, `Layout`, `PaintSelf`, `PaintChildren`. В MVP перерисовывается весь viewport, если что-то dirty; dirty regions и кэш слоёв — Этап 10.

## Заменяемые компоненты (§20 ТЗ)

| Интерфейс | Реализация по умолчанию | Возможная замена |
|---|---|---|
| `IHtmlParser` | `html::LexborHtmlParser` (реализован) | собственный HTML5-токенизатор, Gumbo |
| `ICssEngine` (+ `ICssParser`) | `css::StyleEngine` + `css::LexborCssParser` | собственный токенизатор; Stylo |
| `ILayoutEngine` | `layout::YogaLayoutEngine` | Taffy (grid), собственный block/inline/flex |
| `IRenderer` | `paint::SkiaPainter` (display list) | собственный 2D-рендерер |
| `ITextureProvider` | `render::D3D12TextureProvider` | `D3D12CopyProvider`, `CpuTextureProvider`, `VulkanInteropProvider` |
| `IJavaScriptRuntime` | `js::V8Runtime` | QuickJS-NG, Hermes |
| `IInputProvider` | `input::QueuedInputProvider` | скриптованный ввод в тестах |
| `IBridge` | `bridge::QueueBridge` | lock-free очереди |

Слушатели DOM не зависят от JS-движка (`dom::ListenerCallback` виртуальный; `js::JsListenerCallback` держит `v8::Global<v8::Function>`).

## Ключевые структуры

- **DOM:** intrusive refcount (`RefPtr<Node>`); сильные ссылки вниз/вправо (`firstChild`, `nextSibling`), сырые вверх/влево. `Element` хранит `ComputedStyle`, `LayoutBox`, `TextControl`, `StateFlags` (hover/active/focus/…), `inlineStyle`, слот JS-обёртки.
- **CSS:** `RuleSet` → `RuleIndex` (корзины по правому compound: id > class > tag > universal). Ключ каскада `(layer, important, specificity, order)`. `ComputedStyle` неизменяем и refcounted; diff с предыдущим решает, что инвалидировать.
- **Layout:** `LayoutBox` ↔ `YGNodeRef`. Блок = column flex; инлайновое содержимое — анонимный IFC-бокс с measure/baseline функциями поверх `skia::textlayout::Paragraph`; атомарные инлайны — placeholders.
- **Paint:** stacking contexts (корень, positioned с z-index, opacity < 1, transform). Порядок — упрощённое приложение E CSS 2.1.
- **JS:** изолят на view; `FunctionTemplate` на интерфейс с цепочкой `Inherit`. Время жизни обёрток: обёртка держит **сильную** ссылку на узел, а слот узла — **слабый** handle на обёртку. Цикл рвётся слабой стороной: как только JS отпускает обёртку, ссылка на узел освобождается; узел, оставшийся в дереве, жив через родителя.
- **Мост:** `BridgeMessage{kind, id, level, ok, name, json}`; payload — JSON-строка, ядро её не разбирает.

## Ресурсы и безопасность

- Корень UI — каталог (`StreamingAssets/UI`), все пути (`<img src>`, `<script src>`, `<link href>`, `url()` в CSS, `@font-face`) разрешаются относительно документа и канонизируются; выход за корень, абсолютные пути, буквы дисков, UNC и URL-схемы отклоняются.
- Сети нет. `fetch`, `XMLHttpRequest`, `WebSocket`, `import()` отсутствуют; `--no-expose-wasm`.
- Единственная поверхность native из JS — объект `Unity` (`emit`, `on`, `off`, `call`); функции C# доступны только после `RegisterFunction`.

## Соглашения

- C++20, `/W4 /permissive- /utf-8`, исключения выключены в горячих путях не намеренно — используем `Result<T>` для ошибок парсинга/загрузки; исключения V8 не пересекают границу C ABI.
- Имена: namespace `xgu`, файлы `PascalCase.h/.cpp`, C ABI `xgu_snake_case`, C# `Xploit.GameUI`.
- Логи: `ILogSink` с уровнями Debug/Info/Warning/Error; в Unity через `IUnityLog`; префикс `[xploit_game_ui]`.
