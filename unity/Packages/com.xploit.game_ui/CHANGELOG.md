# Changelog

All notable changes to this package are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/). While the project is below 1.0 a
minor release may contain a breaking change; those are called out explicitly.

## [0.9.0] — 2026-09-23

The first public release.

### Added

- Scrolling: `overflow: scroll` and `auto`, wheel scrolling that chains to the
  ancestor, a drawn scrollbar thumb, and `scrollTop`/`scrollLeft`/`scrollWidth`/
  `scrollHeight`/`clientWidth`/`clientHeight`/`offsetWidth`/`offsetHeight`,
  `scrollTo`, `scrollBy`, `scrollIntoView` and `getBoundingClientRect` in script.
- `linear-gradient` and `radial-gradient` in `background-image`.
- `transition`, `@keyframes` and `animation`, with the standard easing functions
  and `cubic-bezier()`, driven on the host frame clock.
- Apache-2.0 licence, third-party notices and contribution guidelines.

### Changed

- **A frame is produced only when something changed**, and only the region that
  changed is redrawn. An idle menu now costs 0.000 ms per frame and publishes 24
  frames out of 301 instead of 301 out of 301; a HUD with one animated element
  rasterises 0.115 ms instead of 15.9 ms, redrawing 22×22 pixels rather than
  800×600.
- The Unity Native Plugin API headers are no longer redistributed with the
  sources. They are read from a local Unity install at build time, which only
  affects building from source — the released package is unchanged.

### Fixed

- A comment inside a declaration block no longer swallows the property that
  follows it.
- A flex container whose children are all inline elements now lays them out as a
  row instead of collapsing them into one inline formatting context.

## [0.8.0]

- An `HtmlView` inspector: picking a document from `StreamingAssets`, live state
  in Play Mode, Reload and Pause buttons, and warnings for a missing `RawImage`,
  a missing `EventSystem` and a disabled Raycast Target.
- The `DomReady`, `JsReady` and `Interactive` lifecycle events, and a `Log` event
  on the view; `HtmlViewManager.Log` and `SuppressConsoleOutput` for routing
  output into your own console.
- Runtime messages now carry the view that produced them.
- `HtmlView.Resize`.
- The **HUD** sample.

## [0.7.0]

- The bridge: `Unity.emit`, `Unity.on`, `Unity.off` and `Unity.call` from the
  page; `Send`, `On`, `Off`, `RegisterFunction` and `RegisterFunctionAsync` from
  the game.
- `WebJson`, `WebValue` and `WebArguments` for payloads.

## [0.6.0]

- Input: `WebInput`, `WebInputEvent`, and
  `HtmlView.SendInput`/`SetFocus`/`ScreenToView`.
- DOM events with capture and bubble phases, `:hover`, `:active`, `:focus`, and
  text editing in `input` and `textarea`.

## [0.5.0]

- Document painting: backgrounds, borders, shadows, images, text, `opacity`,
  `transform` and `overflow` clipping.

## [0.3.0]

- Document loading: `Load`, `LoadHtml`, `Reload`, and the DOM in JavaScript.

## [0.2.0]

- JavaScript: `ExecuteJS`, `console.*` in the Unity Console, and view state.

## [0.1.0]

- The first slice: creating a view, a Skia texture on D3D12 plus the software
  provider, and `HtmlViewManager`.

[0.9.0]: https://github.com/Rovniy/xploit_game_ui/releases/tag/v0.9.0
