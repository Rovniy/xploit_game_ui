# xploit_game_ui architecture

## Layers

```
Unity C# (com.xploit.game_ui)
  HtmlView · HtmlViewManager · WebEvent · WebArguments · WebTexture · WebInput · WebJson
        │  P/Invoke (C ABI xgu_*, UTF-8, uint64 handles with a generation counter)
        ▼
xploit_game_ui.dll  (C ABI wrapper + Unity Native Plugin API entry points)
        │
        ▼
xgu_runtime (static library; knows nothing about Unity)
  ├── core     Runtime, View, ViewRegistry, interfaces, queues, Log, AssetLoader, TimerHeap
  ├── html     LexborHtmlParser → DOM, user-agent stylesheet (ua.css)
  ├── dom      Node/Element/Text/Document, IdMap, Serializer, EventTarget/Event/EventDispatcher
  ├── css      Tokenizer + Parser → RuleSet; RuleIndex, SelectorMatcher, StyleEngine, ComputedStyle, Animator
  ├── layout   LayoutTreeBuilder, LayoutBox, StyleToYoga, LayoutEngine, inline formatting context
  ├── text     FontManager, RunBuilder, WhiteSpace, TextControl
  ├── paint    Painter → DisplayList (SkPicture), BoxGeometry, ImageCache, FrameMailbox
  ├── js       V8Platform, V8Runtime, IsolateData, WrapperTemplates, DomBindings, UnityBindings, Timers
  ├── bridge   BridgeMessage, MessageQueue, QueueBridge, RpcTable
  ├── input    InputEvent, InputQueue, InputRouter, HitTester, FocusManager, KeyNames
  └── render   D3D12GrContext, D3D12TextureProvider, D3D12CopyProvider, CpuTextureProvider
```

Hosts on top of `xgu_runtime`: `xploit_game_ui.dll` (Unity), `xgu_host` (a Win32 window for debugging without Unity), `xgu_cli` (headless: HTML → PNG, layout dump, JS run, benchmarks), and `xgu_tests`.

## Frame pipeline

```
HTML ──lexbor──▶ DOM ──StyleEngine──▶ ComputedStyle ──LayoutTreeBuilder──▶ LayoutBox + Yoga
   ──YGNodeCalculateLayout──▶ box frames ──Painter──▶ SkPicture (DisplayList)
   ──FrameMailbox──▶ submission thread ──SkSurface(D3D12)──▶ ID3D12Resource ──CreateExternalTexture──▶ RawImage / Material
```

Invalidation runs on per-node dirty bits: `StyleSelf`, `StyleChildren`, `LayoutTree`, `Layout`, `PaintSelf`, `PaintChildren`. A frame is only produced when something is dirty, an animation is running or a timer fired; the painter records where every box landed in device pixels and publishes the union of the boxes that moved or asked for a repaint, so the provider rasterises that rectangle instead of the whole surface.

## Replaceable components

| Interface | Default implementation | Possible replacement |
|---|---|---|
| `IHtmlParser` | `html::LexborHtmlParser` | a hand-written HTML5 tokenizer, Gumbo |
| `ICssEngine` (+ `ICssParser`) | `css::StyleEngine` + `css::Parser` | Stylo |
| `ILayoutEngine` | `layout::LayoutEngine` (Yoga) | Taffy (for grid), a hand-written block/inline/flex engine |
| `IRenderer` | `paint::Painter` (display list) | a hand-written 2D renderer |
| `ITextureProvider` | `render::D3D12TextureProvider` | `D3D12CopyProvider`, `CpuTextureProvider`, `VulkanInteropProvider` |
| `IJavaScriptRuntime` | `js::V8Runtime` | QuickJS-NG, Hermes |
| `IInputProvider` | `input::QueuedInputProvider` | scripted input in tests |
| `IBridge` | `bridge::QueueBridge` | lock-free queues |

DOM listeners do not depend on the JavaScript engine: `dom::ListenerCallback` is virtual, and `js::JsListenerCallback` is the one implementation that holds a `v8::Global<v8::Function>`.

## Key structures

- **DOM:** intrusive reference counting (`RefPtr<Node>`); strong references point down and right (`firstChild`, `nextSibling`), raw ones up and left. `Element` owns its `ComputedStyle`, `LayoutBox`, `TextControl`, `StateFlags` (hover/active/focus/…), inline style and the slot holding its JavaScript wrapper.
- **CSS:** `RuleSet` → `RuleIndex`, bucketed by the rightmost compound selector (id > class > tag > universal). The cascade key is `(layer, important, specificity, order)`; animations sit above the author layer and below `!important`. `ComputedStyle` is immutable and reference counted, and a diff against the previous one decides what to invalidate.
- **Layout:** `LayoutBox` ↔ `YGNodeRef`. A block is a column flex container; inline content becomes an anonymous inline formatting context box with measure and baseline callbacks over `skia::textlayout::Paragraph`; atomic inlines are placeholders positioned from `getRectsForPlaceholders()`.
- **Paint:** stacking contexts (the root, positioned boxes with a z-index, `opacity < 1`, `transform`). The order is a simplified CSS 2.1 Appendix E, and hit testing walks the same geometry in reverse.
- **JavaScript:** one isolate per view; one `FunctionTemplate` per interface, chained with `Inherit`. Wrapper lifetime: the wrapper holds a **strong** reference to the node, and the node's slot holds a **weak** handle back to the wrapper. The weak side breaks the cycle — once script drops the wrapper, the reference to the node goes with it, and a node still in the tree stays alive through its parent.
- **Bridge:** `BridgeMessage{kind, id, level, ok, name, json}`. The payload is a JSON string that the core never parses.

## Assets and security

- The UI root is a directory (`StreamingAssets/UI`). Every path — `<img src>`, `<script src>`, `<link href>`, `url()` in CSS, `@font-face` — is resolved relative to the document and canonicalised; anything that escapes the root is rejected, including absolute paths, drive letters, UNC paths and URL schemes.
- There is no network. `fetch`, `XMLHttpRequest`, `WebSocket` and dynamic `import()` do not exist, and WebAssembly is off.
- The only native surface reachable from script is the `Unity` object (`emit`, `on`, `off`, `call`), and a C# function is callable only after `RegisterFunction`.

## Conventions

- C++20, `/W4 /permissive- /utf-8`. Parsing and loading report failures through `Result<T>` rather than exceptions, and V8 exceptions never cross the C ABI boundary.
- Naming: namespace `xgu`, files `PascalCase.h/.cpp`, C ABI `xgu_snake_case`, C# `Xploit.GameUI`.
- Logging: `ILogSink` with Debug/Info/Warning/Error levels, routed through `IUnityLog` inside Unity, prefixed `[xploit_game_ui]`.
