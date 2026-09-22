# Contributing to xploit_game_ui

Thanks for looking. This is a UI runtime for games, so the bar for a change is
usually "does it make a real game screen easier or cheaper", not "does a browser
do it this way". Both matter, but the first one decides.

## Before you start

- **Small fixes** — typos, a broken link, a wrong error message, a missing test:
  just open a pull request.
- **A new CSS property, selector or DOM API** — open an issue first with the
  markup you want to write. Some gaps are deliberate; the
  [support matrix](docs/css-support.md) says which, and why.
- **Anything touching threading, the D3D12 path or the V8 wrapper lifetime** —
  open an issue first. These are the three places where a plausible-looking
  change goes wrong in ways that only show up under a domain reload or a device
  reset. [threading.md](docs/threading.md) explains the invariants.

## Setting up a build

You need Windows 11 x64 and Visual Studio 2022 or 2026 with the **Desktop
development with C++** workload, which brings the CMake, Ninja and vcpkg the
build uses. Unity 6000.2+ is needed only for the plugin DLL and the PlayMode
tests.

```powershell
.\tools\build.ps1                 # configure, build, test, copy the DLL into the package
.\tools\build.ps1 -Config Debug
.\tools\build.ps1 -SkipTests -SkipCopy
```

The first run builds Skia from source through vcpkg and takes 30–60 minutes.
It is cached in `build/vcpkg-cache`, so the second run takes minutes. If a vcpkg
port fails, delete `native/out` and reconfigure before assuming the failure is
yours.

The Unity Native Plugin API headers are not in this repository — they belong to
Unity under the Unity Companion License. CMake finds them in any Unity Hub
install; `tools/fetch-unity-headers.ps1` copies them locally if you want to pin
one editor version. **Without them everything except `xploit_game_ui.dll`
still builds**, which is exactly what CI does, so a contribution to the engine
does not require Unity at all.

## The fast loop

Most engine work never opens Unity. `xgu_cli` runs the whole pipeline headless:

```powershell
$bin = ".\native\out\x64-windows-release\bin"
& $bin\xgu_cli.exe render page.html out.png --width 800 --height 500
& $bin\xgu_cli.exe layout page.html --width 800 --height 600
& $bin\xgu_cli.exe js  script.js
& $bin\xgu_cli.exe bench page.html --frames 300
```

`xgu_host.exe --debug` opens a window driving the real D3D12 path without an
editor, which is the right place to debug a rendering problem.

## Tests

```powershell
ctest --preset x64-windows-release --output-on-failure
```

Every change needs a test, and the project has three kinds:

- **Unit tests** (`native/tests/unit`) for parsing, the cascade, layout,
  hit testing, events and the bridge. Prefer these.
- **Golden image tests** (`native/tests/testdata/golden`) for anything that
  changes pixels. Set `XGU_UPDATE_GOLDEN=1` to regenerate the references, then
  **look at the new PNG before committing it** — a golden test that was updated
  without being looked at is worse than no test. On a mismatch the run writes
  `<name>.actual.png` and `<name>.diff.png` next to the reference.
- **Unity PlayMode tests** (`unity/Packages/com.xploit.game_ui/Tests`) for the
  C# API and the real GPU path. These need Unity and a D3D12 device, so CI does
  not run them; run them locally when you touch C# or the plugin:

  ```powershell
  & "<Unity>\Editor\Unity.exe" -batchmode -force-d3d12 -projectPath .\unity\Sandbox `
      -runTests -testPlatform PlayMode -testResults .\build\test-results.xml -logFile .\build\editor.log
  ```

  Close the editor first — it holds `xploit_game_ui.dll` open and the build
  cannot replace it.

When you find yourself fixing a bug, write the failing test first. Several of
the trickiest bugs in this engine's history were cases where the *test* was
wrong and the engine was right; a test that fails for the stated reason is the
only way to tell those apart.

## Style

- **C++20**, built `/W4 /permissive- /utf-8`. A pull request should not add a
  warning. Run
  `clang-format` over the code you touch; `.clang-format` is in the repository
  root. The tree is not uniformly formatted yet, so please do not reformat files
  your change does not touch — a formatting diff buried in a fix is unreviewable.
- **English** for identifiers, comments, commit messages and documentation.
- **Comments explain why, not what.** A comment that restates the line below it
  will be removed in review. A comment that records a Skia quirk, a Yoga
  limitation or a decision that looks wrong until you know the context is worth
  a paragraph.
- **Naming:** namespace `xgu`, files `PascalCase.h`/`.cpp`, C ABI functions
  `xgu_snake_case`, C# namespace `Xploit.GameUI`.
- **Errors** go through `Result<T>`, not exceptions, and a V8 exception never
  crosses the C ABI boundary.
- Match the surrounding code. Consistency beats personal preference.

## Commits and pull requests

Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(css): transitions and @keyframes animations
fix(layout): flex rows of inline children
perf: produce a frame only when something changed
docs: describe the damage-tracking rules
```

Scopes match the source directories: `core`, `dom`, `css`, `layout`, `text`,
`paint`, `js`, `bridge`, `input`, `render`, `unity`, `build`.

A pull request should:

1. Build clean and pass `ctest` on Windows.
2. Carry its tests in the same commit as the change.
3. Update [docs/css-support.md](docs/css-support.md) if it adds a feature or
   changes a deviation. **A new deviation from browser behaviour must be written
   down there.** That file is the project's contract with its users, and an
   undocumented deviation is a bug regardless of how reasonable it is.
4. Update [CHANGELOG.md](unity/Packages/com.xploit.game_ui/CHANGELOG.md) for a
   user-visible change.

Keep a pull request to one concern. A refactor bundled with a fix is two pull
requests.

## Licensing

By contributing you agree that your contribution is licensed under
[Apache-2.0](LICENSE), the project's licence, which includes its patent grant.

Do not add a dependency under a copyleft licence (GPL, LGPL, MPL) — the runtime
is deliberately free of them so that it can ship inside a commercial game. Any
new dependency goes into
[docs/THIRD-PARTY-NOTICES.md](docs/THIRD-PARTY-NOTICES.md) with its repository,
licence and redistribution terms, in the same pull request.

Do not commit Unity's headers, SDKs or any other file you did not write and are
not licensed to redistribute.

## Releases

Maintainers: [docs/RELEASING.md](docs/RELEASING.md) has the steps, and why part
of it cannot run in CI.

## Conduct

Participation is covered by the [Code of Conduct](CODE_OF_CONDUCT.md).
