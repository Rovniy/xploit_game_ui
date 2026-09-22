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
