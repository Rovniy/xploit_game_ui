# Security Policy

## Supported versions

The project is pre-1.0. Fixes go into the latest release; older releases are not
patched.

| Version | Supported |
|---|---|
| Latest release | yes |
| Anything older | no |

## Reporting a vulnerability

**Please do not open a public issue for a security problem.**

Report it through GitHub's private channel —
[Security → Report a vulnerability](https://github.com/Rovniy/xploit_game_ui/security/advisories/new) —
or by email to **contact@ravy.pro**.

Include enough to reproduce it: the HTML, CSS or JavaScript involved, the version
of the package, the Unity version, and what you expected to be prevented. A
proof of concept helps; a working exploit chain aimed at a third party does not.

You should get an acknowledgement within a week. If a report is confirmed, the
fix and an advisory go out together, and you are credited unless you ask
otherwise.

## What counts as a vulnerability here

The runtime's threat model is that **the page is not fully trusted, and the
machine is**. A game may ship UI built by a designer, patched over the air, or
modded. A page must therefore never be able to reach outside what it was given.

These are vulnerabilities and are taken seriously:

- **Escaping the UI root.** Any way for a page to read, write or probe a file
  outside its root directory — through `<img src>`, `<script src>`,
  `<link href>`, `url()` in CSS, `@font-face`, a symlink, a path-traversal
  sequence, a UNC path, a drive letter or a URL scheme.
- **Reaching native code that was not registered.** The page's only native
  surface is the `Unity` object, and a C# function is callable only after
  `RegisterFunction`. Any other route into native code is a vulnerability.
- **Getting the network.** The runtime has no `fetch`, `XMLHttpRequest`,
  `WebSocket` or dynamic `import()`, and WebAssembly is off. A way to make an
  outbound connection is a vulnerability.
- **Memory corruption** reachable from page content: a crafted document,
  stylesheet, font, image or script that causes a use-after-free, a buffer
  overflow or a type confusion in the parser, the layout engine, the painter or
  the DOM bindings.
- **Corrupting a wrapper's lifetime** so that script can reach a freed DOM node
  or event.

These are **not** vulnerabilities in this project:

- Anything a game's own C# code chooses to expose through `RegisterFunction`.
  That surface is yours to design; the runtime only guarantees that nothing
  crosses without it.
- `view.ExecuteJS(...)` running the string you gave it.
- A vulnerability in V8, Skia or another upstream dependency — report those
  upstream. Do tell us if this project pins a version with a known advisory, so
  the pin can move.
- A crash from a page that is simply enormous or pathological, unless it is
  reachable memory corruption rather than an out-of-memory condition.

## Hardening notes for games shipping this

- Keep the UI root as narrow as possible. Everything under it is readable by
  every page in that view.
- Treat `RegisterFunction` handlers as an untrusted boundary. Validate arguments
  there; the bridge guarantees their JSON shape, never their meaning.
- `ExecuteJS` executes whatever you hand it. Do not build a string out of player
  input and run it.
