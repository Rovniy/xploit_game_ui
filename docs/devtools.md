# Debugging a page: the JS Console and Chrome DevTools

Two tools look inside the JavaScript of a running `HtmlView`:

- the **JS Console** window in the Unity editor, for reading the page's output
  and trying a line of code without leaving Unity;
- **Chrome DevTools**, the real thing: console, breakpoints and stepping,
  profiler, heap snapshots.

Both talk to the same place, the V8 inspector of the view, so what one shows the
other shows too.

## JS Console

Open it from *Window ▸ Xploit ▸ JS Console*, or with **JS Console** in the
`HtmlView` inspector during Play Mode.

```
┌ Clear │ MainMenu ▾ │ 🔍 search │ Log Warnings Errors │      DevTools :9222 │ Open DevTools ┐
│ page ready                                                                               │
│ health 100                                                                               │
│ > document.querySelectorAll('.line').length                                              │
│ ← 3                                                                                      │
│ > player                                                                                 │
│ ← {name: 'Ada', level: 12}                                                               │
│ > missing.value                                                                          │
│ ✕ ReferenceError: missing is not defined                                                 │
└ > _                                                                                     ┘
```

**The log** is what the page sent to the console (`console.log`, `warn`, `error`,
`info`, `debug`, and `table`, `count`, `time`, `assert` and the rest), uncaught
errors, and the engine's own warnings for the document (CSS it could not parse,
missing files). The view menu picks whose output you see; messages from the
runtime itself belong to no view and always show. *Log*, *Warnings* and *Errors*
filter by level, the search field by text, and *Clear* empties the window. It
keeps the last 5000 lines. Opened in the middle of a session, it first shows the
last 2000 messages from before.

**The prompt** at the bottom runs JavaScript in the selected view:

| Key | Does |
|---|---|
| Enter | run the text |
| Shift+Enter | new line, for more than one statement |
| Up / Down | walk through what you ran before |

It behaves like the DevTools console, not like `ExecuteJS`:

- the value of the last expression comes back, formatted the way DevTools prints
  it: `'text'`, `42`, `[1, 2, 3]`, `{x: 1, label: 'a'}`, `Map(2) {…}`,
  `HTMLDivElement`;
- a promise is awaited, and `await` works at the top level:
  `await Unity.call('getBuildInfo')`;
- `let` and `const` may be declared again, so running a line twice does not fail;
- an error comes back as the answer (✕), not as an "Uncaught" in the Unity Console;
- the console helpers are there: `$_` (the last result), `keys(obj)`, `values(obj)`.

Everything runs in the page's global scope. Changes you make (to the DOM, to
variables) are real, and the view repaints as usual.

**From code** the same evaluation is `HtmlView.EvaluateAsync`:

```csharp
WebEvalResult result = await view.EvaluateAsync("document.title");
if (result.Ok)
{
    Debug.Log(result.Text); // 'Main Menu'
}
```

It works in players too, Development Build or not. It never goes through the
network, so it does not need the DevTools endpoint.

## Chrome DevTools

### Turning it on

In the editor it is on by default: entering Play Mode starts an endpoint on
`127.0.0.1:9222`. The **DevTools** toggle in the JS Console toolbar switches it
off and on; it is a per-user preference and applies at once. Next to it the
window shows the port in use, or *port busy?* when it could not start (another
program, such as Chrome started with `--remote-debugging-port=9222`, holds the
port).

### Opening it

Either way:

- **chrome://inspect** in Chrome or Edge. The views appear under *Remote Target*
  with their GameObject names; click **inspect**. Nothing to configure:
  `localhost:9222` is one of the addresses Chrome looks at by default.
- **Open DevTools** in the JS Console window, or **DevTools** in the `HtmlView`
  inspector. They start Chrome (or Edge) on the view and put the address on the
  clipboard as well; if no window appears, paste it into the address bar. The
  address is also `HtmlView.DevToolsUrl`.

### What it can do

| Panel | Works | Notes |
|---|---|---|
| Console | yes | output with expandable objects, the prompt, `console.table`, preserved messages from before DevTools connected |
| Sources | yes | every script of the page, breakpoints (line, conditional, logpoints), `debugger;`, stepping, watch, call stack, scope |
| Performance / Profiler | yes | CPU profiles of the page's JavaScript |
| Memory | yes | heap snapshots and allocation sampling, for finding leaks |
| Elements, Styles, Network, Application | no | the protocol is answered by V8, which knows JavaScript; our DOM, CSS and asset loading are not exposed to it |

### Breakpoints

When a script stops, **its view stops updating**: no frames, no timers, no input
reach the page until you resume. **The game keeps running.** JavaScript runs on a
thread of its own, and the Unity main thread never waits for it. Other views are
stopped too, since they share that thread.

Code that runs while the page loads runs before DevTools can connect. To stop in
it, set the breakpoint once and press **Reload** in the `HtmlView` inspector
(or call `view.Reload()`): the DevTools session stays attached across the
reload, breakpoints included, and the new document stops on it.

*Pause on caught exceptions* works. *Pause on uncaught exceptions* may not: the
engine catches what an event handler or a timer throws so that one handler
cannot break the frame, and V8 then sees those errors as caught.

### Source maps

DevTools can only use source maps that are **inline**, because the endpoint does
not serve the page's files. With Vite:

```js
// vite.config.js
export default defineConfig({
  build: { sourcemap: 'inline' }, // for debug builds of the UI only
})
```

Sources then shows the original `.vue` and `.ts` files, and breakpoints set in
them work.

### In a player

Development Builds can use DevTools as well; release builds cannot, and
`WebDevTools.Start` returns false there. It is never on by itself in a player:

```csharp
// Before the first HtmlView is enabled, e.g. in a bootstrap script:
WebDevTools.AutoStart = true; // uses port 9222

// or at any time:
WebDevTools.Start(9222);
WebDevTools.Stop();
```

Nothing starts in batch mode (build machines, command-line test runs).

### Safety

The endpoint listens on the loopback address only, so other machines cannot
reach it, and it refuses every request whose `Host` header is not
`127.0.0.1`, `localhost` or `[::1]`, so a web page in a browser cannot reach it
through DNS rebinding either. Anyone on the same machine who can open a socket
can still run JavaScript in the page while it is on, which is why players only
get it in Development Builds and only when asked.

## Without Unity

`xgu_cli serve` runs a page on its own, with the same endpoint:

```powershell
.\native\out\x64-windows-release\bin\xgu_cli.exe serve .\unity\Sandbox\Assets\StreamingAssets\UI\MainMenu\index.html --port 9222
```

It prints the DevTools address and runs until Ctrl+C. There is no game on the
other side, so `Unity.call` rejects and `Unity.on` never fires, but everything
else about the page can be debugged there.

## For the curious

`js::Inspector` is the V8 inspector of one view. `devtools::DevToolsHub` connects
it to clients: Chrome over the WebSocket server (`devtools::DevToolsServer`, one
thread, HTTP and WebSocket on Winsock), and one session per view for the host,
which is what the JS Console and `EvaluateAsync` use through
`xgu_view_devtools_send` and `xgu_devtools_poll`. Messages are handled on the
runtime thread; see [threading](threading.md#a-script-stopped-at-a-breakpoint)
for what happens while a script is paused.
