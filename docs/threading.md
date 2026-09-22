# xploit_game_ui threading model

## Threads

| Thread | Owner | What it does | What it must not do |
|---|---|---|---|
| Unity main thread | Unity | The C# `HtmlView` API; feeding input (`WebInput`); `HtmlViewManager.Update()` ticks each view (`xgu_view_tick`), drains the native→C# queue (`xgu_view_poll_message`) and calls user handlers; creates external textures | Anything heavy — no layout and no JavaScript here |
| Runtime thread | `xploit_game_ui.dll`, one per process | Per view, per frame: incoming messages → input → DOM events → timers and `requestAnimationFrame` → V8 microtask checkpoint → style → layout → paint → publish a `DisplayList` | Calling the Unity API; touching `GrDirectContext` |
| Unity submission thread | Unity (plugin event `XGU_EVT_PAINT`, mode `kUnityD3D12GraphicsQueueAccess_Allow`) | Creates `GrDirectContext` lazily, replays the `SkPicture` into the surface, `flush(kPresent)`, `submit`, and releases deferred GPU resources once the frame fence passes | Anything other than Skia GPU work and D3D12 |
| Worker pool | the runtime | Image decoding (`SkCodec` → raster `SkImage`) | Touching the DOM |

The "JavaScript thread" is the runtime thread: the DOM API has to be synchronous from script, so the V8 isolate — one per view — is only ever entered there.

## What crosses a thread boundary

- **Main → runtime:** `BridgeMessage` (Send/Reply/ExecuteJS/Load/Resize), `XguInputEvent`, `tick`. Mutex-guarded `std::deque` queues, with `MouseMove` coalesced.
- **Runtime → main:** `BridgeMessage` (Emit/Call/Log/Lifecycle). The queue is capped at 10k messages; on overflow the oldest `Emit` and `Log` messages are dropped, never `Call` or `Reply`.
- **Runtime → submission:** `DisplayList{ sk_sp<SkPicture>, dirtyRect, frameId }`, an immutable object in a last-one-wins slot, one slot per view. `SkPicture` is reference counted, so a frame that is superseded before it is shown costs nothing to drop.
- **Submission → main:** status flags only (`xgu_view_status`: `TEXTURE_READY`, `TEXTURE_RECREATED`, `DEVICE_LOST`, `PIXELS_READY`) plus the native texture pointer, which C# polls in `Update()`.

Nothing else crosses: DOM nodes, `ComputedStyle`, `LayoutBox` and every V8 object live on the runtime thread alone.

## Frame pacing

The runtime thread sleeps on a condition variable and wakes on `xgu_view_tick(view, time)` from Unity's `Update()`, or when a message arrives. One tick is one runtime frame for that view, and several ticks that arrive before the thread runs are coalesced. On a clean frame — no dirty bits, no timers, no running animation — no `DisplayList` is published and C# issues no plugin event, because `xgu_view_has_pending_frame` returns 0.

## Lifecycle

```
Create → Initialize → Load → DOM Ready → JS Ready → Interactive ⇄ Pause/Resume → Reload → Dispose
```

- `Create`/`Initialize`: `xgu_view_create` registers the view under a generation-tagged id and allocates the V8 isolate on the first `Load`.
- `Load`: read the HTML through `IAssetLoader`, parse it, apply the user-agent and author stylesheets, fire `DOMContentLoaded` (**DOM Ready**), run `<script>` elements in document order, fire `load` (**JS Ready**), then the first layout and paint reach **Interactive**.
- `Pause`: input and ticks are ignored and timers freeze; `Resume` continues from the stored time.
- `Reload`: disposes the view's internal state, isolate included, and loads again. The view id survives.
- `Dispose` (`xgu_view_destroy`): the main thread marks the view for destruction; the runtime thread finishes the frame in flight, drops every `v8::Global`, releases the context and isolate, then the DOM, layout and paint state; at the next plugin event the submission thread parks the GPU resources in a list tagged with `GetNextFrameFenceValue()` and frees them once `GetFrameFence()->GetCompletedValue()` catches up. The id's generation is bumped, so a plugin event still carrying the old id is ignored.
- **Editor domain reload:** `HtmlViewManager` calls `xgu_views_destroy_all()` from `AssemblyReloadEvents.beforeAssemblyReload`. The DLL and the V8 platform stay loaded, and views are recreated afterwards.
- **Device loss** (`isDeviceLost`, `DXGI_ERROR_DEVICE_REMOVED`) and `kUnityGfxDeviceEventBeforeReset`: the Skia context is abandoned (`releaseResourcesAndAbandonContext`), every view reports `DEVICE_LOST`, and C# recreates them.

## Rules for contributors

1. Every C ABI function documents which thread may call it. The default is the Unity main thread.
2. C# callbacks (`Action<WebEvent>`, `Func<WebArguments, object>`) only ever run from `HtmlViewManager.Update()`.
3. The runtime thread never blocks on the GPU, and the submission thread never waits on the runtime thread.
4. All Skia GPU work — `GrDirectContext`, an `SkSurface` over a D3D12 resource — happens on the submission thread. Raster `SkImage`s and `SkPicture`s may be created on the runtime thread.
5. `xgu_tests` and `xgu_cli` run the runtime in single-threaded mode, where a tick executes synchronously on the calling thread. It is the same code, without the separate thread.
