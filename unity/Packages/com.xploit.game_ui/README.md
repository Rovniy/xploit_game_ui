# xploit_game_ui — HTML/CSS/JS as Unity game UI

This package paints an HTML/CSS/JavaScript document into a texture and hands it
to an ordinary `RawImage`. The engine is our own: lexbor parses the HTML, the CSS
cascade and the Yoga layout are ours, Skia does text and rasterising, and V8 runs
the scripts. No Gameface, no Ultralight, no embedded browser.

Full documentation: <https://github.com/Rovniy/xploit_game_ui>

## Requirements

- Unity 6000.2 or newer, Windows x64.
- **Direct3D 12** for the hardware path. On D3D11 and under `-nographics` the
  package falls back to a software provider automatically.

## Five minutes

1. Put your page at `Assets/StreamingAssets/UI/MainMenu/index.html`. Everything
   it loads has to live inside `StreamingAssets`; the runtime rejects any path
   that leaves it.
2. On a `Canvas`, create an object with a `RawImage` and add `HtmlView` and
   `WebInput` to it.
3. In the `HtmlView` inspector set the path to `UI/MainMenu/index.html` and clear
   **Draw Test Frame On Enable**.
4. Press Play.

## On a mesh

A view can also be a screen in the world: a monitor on a desk, a panel on a
wall, a curved display.

1. Create a Quad (or any mesh with UVs) and add `HtmlView` to it. The view finds
   the `MeshRenderer` on its own (or set **Target Renderer**) and draws with the
   built-in `XploitGameUI/WorldPremultiplied` shader, an unlit transparent shader
   with *Cull* and *ZWrite* switches. Turn on **Keep Material** to use a material
   of your own instead; the view then only sets **Texture Property** on it, and
   the shader has to expect premultiplied alpha.
2. Turn off **Size From Rect Transform** and set **Size** in pixels, with the
   aspect of the mesh: 1280 × 720 on a quad scaled 16 × 9.
3. For input, add `WebInput` to the same object, keep a Collider on it and put a
   `PhysicsRaycaster` on the camera; the scene needs an `EventSystem` as usual.
   A non-convex `MeshCollider` maps the hit through the mesh UVs, so the pointer
   follows the texture on any shape. Any other collider is mapped as a quad in
   the local XY plane from -0.5 to 0.5, which is what Unity's Quad is.

`HtmlView.RayToView(ray, out point)` and `HitToView(hit, out point)` do the same
mapping for input of your own, such as a VR controller ray.

```csharp
public sealed class Menu : MonoBehaviour
{
    [SerializeField] HtmlView view;

    void OnEnable()
    {
        view.On("play", _ => StartGame());              // page: Unity.emit("play")
        view.RegisterFunction("getBestScore", _ => 42); // page: await Unity.call("getBestScore")
        view.JsReady += v => v.Send("healthChanged", 100); // page: Unity.on("healthChanged", ...)
    }
}
```

A complete worked example ships as **Samples → HUD** in the Package Manager.

## Debugging the page

**JS Console** (*Window ▸ Xploit ▸ JS Console*) shows the page's console output
per view and evaluates JavaScript in it the way the DevTools console does: the
value comes back formatted, a promise is awaited, and `let` can be declared again.
Up and Down walk the history; Shift+Enter starts a new line. From code, the same
thing is `await view.EvaluateAsync("document.title")`.

**Chrome DevTools.** With the *DevTools* toggle in that window on (the default),
Play Mode starts an endpoint on `127.0.0.1:9222`. Open `chrome://inspect` and the
views are listed under *Remote Target*, or press **DevTools** in the `HtmlView`
inspector. You get Console, Sources with breakpoints and stepping, Profiler and
Memory. A few things worth knowing:

- While a script is stopped at a breakpoint its view does not update, but the
  game keeps running: JavaScript has a thread of its own.
- Breakpoints survive `Reload`, so to stop in code that runs while the page
  loads, set the breakpoint and press **Reload** in the inspector.
- DevTools can only fetch source maps that are inline
  (`build.sourcemap: 'inline'` in Vite); the page's files are not served.
- The Elements and Styles panels are not there: the DevTools protocol is
  answered by V8, which knows JavaScript, not our DOM.
- The endpoint listens on the loopback address only and refuses a request whose
  `Host` is anything else. In a player it starts only in a Development Build,
  and only when asked: `WebDevTools.Start()`, or `WebDevTools.AutoStart = true`
  before the first view.

`xgu_cli serve page.html` runs a page outside Unity, with the same endpoint, for
working on the page alone. The whole guide, with the prompt's keys and what each
DevTools panel can do, is [docs/devtools.md](https://github.com/Rovniy/xploit_game_ui/blob/main/docs/devtools.md).

## The API

| Type | What it is for |
|---|---|
| `HtmlView` | one view: loading, state, the texture, the bridge, input |
| `HtmlViewManager` | one per process: runtime start-up, the frame, draining logs |
| `WebInput` | Unity input → the document, through uGUI events and the keyboard |
| `WebEvent`, `WebArguments`, `WebValue` | what arrived from the page |
| `WebJson` | serialising and parsing bridge payloads |
| `WebLogMessage` | a line from the runtime, with its level and the view that produced it |
| `WebTexture` | the view's texture and its recreation on resize |

### HtmlView

```csharp
view.Load("UI/MainMenu/index.html");   // relative to StreamingAssets
view.LoadHtml("<b>hello</b>");         // markup from memory
view.Reload();
view.ExecuteJS("console.log(document.title)");

view.Send("healthChanged", 75);                       // → Unity.on
view.On("play", e => Play(e.Args.GetString(0)));      // ← Unity.emit
view.RegisterFunction("getAmmo", _ => ammo);          // ← await Unity.call
view.RegisterFunctionAsync("loadSave", async _ => await LoadAsync());

view.SendInput(WebInputEvent.Mouse(WebInputType.MouseDown, point));
view.SetFocus("search");
view.Resize(1280, 720);

view.DomReady    += v => { };   // the DOM is built, no script has run yet
view.JsReady     += v => { };   // scripts have run
view.Interactive += v => { };   // the first frame has been painted
view.Log         += m => { };   // console.* and errors from this page
```

### Logs

Everything the page prints goes to the Unity Console by default. To route it into
your own console instead, subscribe to `HtmlViewManager.Log` and set
`HtmlViewManager.SuppressConsoleOutput`.

## Limits

HTML, CSS and DOM support is deliberately scoped to what game UI needs. The full
matrix, and every documented difference from a browser, is in
[docs/css-support.md](https://github.com/Rovniy/xploit_game_ui/blob/main/docs/css-support.md).
The short version:

- flexbox, block and inline are there; **CSS grid, `float`, `calc()`, custom
  properties and `@media` are not**;
- `transition`, `@keyframes` and `animation` work, as do gradients, shadows and
  transforms;
- there is no network — `fetch`, `XMLHttpRequest` and ES modules do not exist;
- JavaScript cannot read files, and cannot reach native code except through the
  bridge functions you register explicitly.
