# Changelog

All notable changes to this package are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/). While the project is below 1.0 a
minor release may contain a breaking change; those are called out explicitly.

## [Unreleased]

### Added

- `text-shadow`: several shadows separated by commas, offsets, blur and colour
  (`currentColor` when omitted), inherited.
- Chrome DevTools for the page's JavaScript: `WebDevTools.Start()` (automatic in
  Play Mode, off in release players) opens an endpoint on `127.0.0.1:9222`, and
  every view is a target in `chrome://inspect` with Console, Sources (breakpoints,
  stepping), Profiler and Memory. Sessions survive a reload, breakpoints
  included. The endpoint refuses requests whose `Host` is not the loopback address.
- *Window ▸ Xploit ▸ JS Console*: console output per view and a prompt that
  evaluates JavaScript in it; `HtmlView.EvaluateAsync` does the same from code,
  and `HtmlView.DevToolsUrl` gives the URL that opens DevTools on the view. The
  guide is `docs/devtools.md`.
- The whole built-in `console` in pages: `table`, `group`, `time`, `count`,
  `assert`, `dir` and the rest now work (they used to be missing).
- `xgu_cli serve page.html` runs a page with DevTools outside Unity.
- Views on a mesh: `HtmlView.TargetRenderer` shows the view on any `Renderer`
  with the new `XploitGameUI/WorldPremultiplied` shader (or only sets the texture
  on your own material with `KeepRendererMaterial`), and gives the Renderer its
  material back when the view goes. `WebInput` works on such a mesh through a
  `PhysicsRaycaster`, mapping the hit through the mesh UVs (`MeshCollider`) or as
  a quad (any other collider). `HtmlView.RayToView` and `HitToView` expose the
  same mapping.

### Fixed

- Stopping Play Mode could log a burst of `NullReferenceException` from
  `HtmlViewManager.IssueGc`. The manager was created `DontSave`, which the editor
  keeps after Play Mode ends, so every session left one behind; the leftovers
  also ran `LateUpdate` in later sessions. It now goes with Play Mode, leftovers
  from earlier versions are removed, and a manager that is not the current one
  does nothing.
- With domain reload off (*Enter Play Mode Options*), the second Play Mode
  session got no manager at all and every `HtmlView` disabled itself.
- "Unhandled promise rejection" is reported once the microtasks have run, as in
  a browser, not the moment a promise rejects: a handler attached later in the
  same turn (an awaiting caller, the DevTools console) no longer produces a
  false error.
- The `HtmlView` inspector refreshed its live state every frame in Play Mode,
  which cost the editor several milliseconds a frame while the object was
  selected. It now refreshes four times a second.

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
