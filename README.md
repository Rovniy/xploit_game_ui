<div align="center">

# xploit_game_ui

**Build your game's UI with HTML, CSS and JavaScript. Render it on the GPU, inside Unity, at zero cost when nothing moves.**

[![CI](https://github.com/Rovniy/xploit_game_ui/actions/workflows/ci.yml/badge.svg)](https://github.com/Rovniy/xploit_game_ui/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![Unity](https://img.shields.io/badge/Unity-6000.2%2B-black.svg)](https://unity.com)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%C2%B7%20D3D12-lightgrey.svg)](#requirements)

[What it is](#what-it-is) · [Quick start](#quick-start) · [What works](#what-works) · [What it deliberately does not do](#what-it-deliberately-does-not-do) · [Building](#building-from-source) · [Docs](#documentation)

</div>

---

## What it is

Game UI is the part of a game that changes the most and is the most painful to
build. Menus, HUDs, inventories, settings screens, shops — all of it is layout,
text, state and animation, which is exactly what the web stack has spent thirty
years getting good at. Unity's own UI systems are fine, but iterating on them
means recompiling, reopening, reclicking.

`xploit_game_ui` lets you write that UI as an ordinary web page and drop it into
Unity as a texture:

```html
<div class="hud">
  <div class="health"><span id="bar" style="width: 75%"></span></div>
  <button id="inventory">Inventory</button>
</div>
```

```js
document.getElementById('inventory').addEventListener('click', () => Unity.emit('inventory'));
Unity.on('healthChanged', v => bar.style.width = v + '%');
const ammo = await Unity.call('getAmmo');
```

```csharp
view.Load("UI/HUD/index.html");
view.On("inventory", e => OpenInventory());
view.RegisterFunction("getAmmo", _ => ammo);
view.Send("healthChanged", 75f);
```

That is the whole integration. Edit the CSS, hit reload, see the change.

### It is not a browser

This is the part that matters. Embedding Chromium into a game means a second
process tree, a hundred megabytes, a networking stack you did not ask for and a
frame budget you do not control.

`xploit_game_ui` is a **game UI runtime** assembled out of open-source parts that
each do one job well:

| | |
|---|---|
| **[lexbor](https://github.com/lexbor/lexbor)** | HTML5 parsing |
| **[V8](https://github.com/v8/v8)** | JavaScript, the same engine as Chrome |
| **[Yoga](https://github.com/facebook/yoga)** | flexbox layout |
| **[Skia](https://github.com/google/skia)** | text shaping and GPU painting, the same engine as Chrome |

The DOM, the CSS cascade and the paint order are ours, about 20k lines of C++20,
so that every piece stays replaceable and every millisecond stays accountable.
It runs in-process, on Unity's own D3D12 device, with no copy between Skia and
the texture your `RawImage` samples.

No Gameface, no Ultralight, no Vuplex, no commercial WebView SDK. Apache-2.0,
and every dependency is permissively licensed — see
[THIRD-PARTY-NOTICES](docs/THIRD-PARTY-NOTICES.md).

### It costs nothing when nothing changes

A UI that is just sitting there should not burn a frame budget. The runtime
produces a frame only when something actually changed, and redraws only the
rectangle that changed — computed from where each box landed last frame.

Measured on an 800×600 view with the software rasteriser, so the numbers are a
floor rather than a best case:

| Scene | Before | After |
|---|---|---|
| Menu, idle | 22.7 ms/frame, 301 of 301 frames published | **0.000 ms**, 24 of 301 published |
| HUD with one animated element | 15.9 ms rasterising per frame | **0.115 ms**, redrawing 22×22 instead of 800×600 |

---

## Quick start

1. **Install the package.** Grab `com.xploit.game_ui-<version>.tgz` from the
   [latest release](https://github.com/Rovniy/xploit_game_ui/releases), then in Unity:
   *Window → Package Manager → + → Install package from tarball…*

   The tarball already contains the prebuilt native runtime, so there is nothing
   to compile.

2. **Put your page under StreamingAssets.** Everything the page can read lives
   under one root, and nothing outside it is reachable:

   ```
   Assets/StreamingAssets/UI/HUD/
     index.html
     style.css
     app.js
   ```

3. **Add the view.** Put a `RawImage` on a Canvas, add an `HtmlView` component
   next to it, and set *Path* to `UI/HUD/index.html`. Add `WebInput` if the page
   should respond to the mouse and the keyboard.

4. **Talk to it** from C#, with the four calls shown above: `Load`, `On`,
   `Send`, `RegisterFunction`.

A complete working HUD — page, game script and the bridge between them — ships as
a package sample: *Package Manager → xploit_game_ui → Samples → HUD → Import*.

---

## What works

A short version of the [full support matrix](docs/css-support.md), which is the
honest list and names every gap.

**Layout** — flexbox (`flex-direction`, `wrap`, `justify-*`, `align-*`, `gap`,
`flex-grow/shrink/basis`), block, inline, inline-block, `position` static /
relative / absolute / fixed, `z-index` with real CSS 2.1 stacking contexts,
`box-sizing`, percentages, `em`/`rem`/`vw`/`vh`.

**Painting** — backgrounds, images (PNG/JPEG/WebP), linear and radial gradients,
borders, `border-radius`, `box-shadow`, `opacity`, `transform`, `overflow` with
clipping and wheel scrolling.

**Text** — real shaping through HarfBuzz and ICU, so scripts that need it get it.
System fonts plus your own `@font-face` files, `line-height`, `text-align`,
`letter-spacing`, `text-decoration`, `text-transform`, `white-space`, and
`text-overflow: ellipsis`.

**Interaction** — the full mouse event set with `hover`/`active`/`focus` states,
keyboard events, focus handling, and editable `<input>` and `<textarea>` with
selection, caret placement and password masking.

**Animation** — `transition` and `@keyframes` with the standard easings and
`cubic-bezier()`, driven on the game's frame clock.

**Script** — V8, with the DOM API you would expect (`querySelector`,
`classList`, `style`, `addEventListener`, `getBoundingClientRect`, `scrollTop`),
`setTimeout`, `requestAnimationFrame`, and `console.*` wired into the Unity Console.

**The bridge** — `Unity.emit` / `Unity.on` from the page, `view.On` / `view.Send`
from C#, and `Unity.call` ↔ `RegisterFunction` for request/response, including
`async` handlers that return a `Task`.

## What it deliberately does not do

Being explicit about this is more useful than a longer feature list.

- **No network.** There is no `fetch`, no `XMLHttpRequest`, no `WebSocket` and
  no dynamic `import()`. UI ships with the game.
- **No filesystem access from script.** Every path resolves inside the UI root;
  absolute paths, drive letters, UNC paths, URL schemes and `..` escapes are
  rejected. The only native surface reachable from the page is the `Unity`
  object, and a C# function is callable only after you register it.
- **No CSS grid, float, `calc()`, custom properties or `@media`** — grid is the
  first candidate for the next round.
- **No `::before`/`::after` or `:nth-child()`.**
- **No `<table>`, `<select>`, `<canvas>`, `<video>` or `<iframe>`.**
- **No world-space UI yet.** The output is a texture, so putting it on a quad
  works; routing input through a raycast is not written.
- **Windows x64 with Direct3D 12 only** at present. On any other renderer the
  package falls back to a CPU rasteriser that uploads pixels, which works but
  costs more.

## Requirements

- Unity 6000.2 or newer, with the Windows graphics API set to Direct3D 12
- Windows 11 x64

To build from source you also need Visual Studio 2022 or 2026 with the
"Desktop development with C++" workload, which supplies the CMake, Ninja and
vcpkg the build uses.

---

## Building from source

```powershell
.\tools\fetch-unity-headers.ps1   # optional: pin the Unity plugin headers to one editor
.\tools\build.ps1                 # configure, build, run the tests, copy the DLL into the package
```

The first build compiles Skia through vcpkg, which takes 30–60 minutes. The
result is cached in `build/vcpkg-cache`, so later builds take minutes.

The Unity Native Plugin API headers ship with the Unity Editor under the Unity
Companion License and are **not** redistributed here. CMake finds them in any
Unity Hub installation by itself; without them the core, the tests and the
command-line tools still build, and only `xploit_game_ui.dll` is skipped.

### Working without Unity

The runtime has two hosts that need no editor at all, which is how most of the
engine gets developed:

```powershell
$bin = ".\native\out\x64-windows-release\bin"

& $bin\xgu_cli.exe render page.html out.png --width 800 --height 500  # page → PNG
& $bin\xgu_cli.exe layout page.html --width 800 --height 600          # box tree → JSON
& $bin\xgu_cli.exe js script.js                                       # run JS, console.* to stdout
& $bin\xgu_cli.exe bench page.html --frames 300                       # per-phase frame cost

& $bin\xgu_host.exe --debug                        # a window showing a live D3D12 frame
& $bin\xgu_host.exe --screenshot out.png --frames 3 # D3D12 without Unity, read back to PNG
```

### Tests

```powershell
ctest --preset x64-windows-release --output-on-failure
```

310 native tests, including golden image comparisons. Set `XGU_UPDATE_GOLDEN=1`
to regenerate the reference images; on a mismatch a test writes
`<name>.actual.png` and `<name>.diff.png` beside the reference.

The Unity PlayMode suite (46 tests) runs against a real D3D12 device:

```powershell
& "<Unity>\Editor\Unity.exe" -batchmode -force-d3d12 -projectPath .\unity\Sandbox `
    -runTests -testPlatform PlayMode -testResults .\build\test-results.xml -logFile .\build\editor.log
```

---

## Repository layout

```
native/   the C++20 runtime (CMake + vcpkg): core, Skia/D3D12 rendering, the C ABI,
          the Unity plugin, xgu_host, xgu_cli and the tests
unity/    Packages/com.xploit.game_ui — the UPM package (C# API + DLL)
          Sandbox — a Unity 6000.5 project used for the PlayMode tests
docs/     architecture, threading, the support matrix, licences
tools/    build.ps1, package.ps1, fetch-unity-headers.ps1
```

## Documentation

| | |
|---|---|
| [HTML / CSS / DOM support matrix](docs/css-support.md) | what works, and every documented deviation from a browser |
| [Architecture](docs/architecture.md) | layers, the frame pipeline, the replaceable interfaces |
| [Threading model](docs/threading.md) | which thread does what, and what crosses between them |
| [ADR-0001](docs/ADR-0001-stack.md) | why this stack, what was rejected, and the spike results |
| [Third-party notices](docs/THIRD-PARTY-NOTICES.md) | every dependency with its licence |

## Contributing

Issues and pull requests are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for
how to set up a build, and [SECURITY.md](SECURITY.md) for reporting a
vulnerability privately. Participation is covered by the
[Code of Conduct](CODE_OF_CONDUCT.md).

## Licence

[Apache-2.0](LICENSE). See [NOTICE](NOTICE) and
[docs/THIRD-PARTY-NOTICES.md](docs/THIRD-PARTY-NOTICES.md) for the dependencies.

> The goal was never to build a second Chrome. It was to build a fast, embeddable
> and controllable game UI runtime out of open-source parts.
