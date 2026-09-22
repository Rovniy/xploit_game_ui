using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.UI;

namespace Xploit.GameUI
{
    /// <summary>
    /// One HTML document rendered to a texture. Stage 1 exposes the texture and
    /// the built-in test frame; Load/ExecuteJS/Send/On/RegisterFunction arrive
    /// with their stages (3, 2, 7, 7, 7).
    /// </summary>
    [AddComponentMenu("Xploit/Game UI/Html View")]
    [DisallowMultipleComponent]
    public sealed class HtmlView : MonoBehaviour
    {
        /// <summary>Prints native/provider diagnostics to the Unity Console.</summary>
        public static bool EnableDebug { get; set; }

        [Tooltip("Texture size in device pixels when no RectTransform drives it.")]
        [SerializeField] Vector2Int m_size = new Vector2Int(1280, 720);

        [Tooltip("Follow the size of the RectTransform (RawImage) this view is attached to.")]
        [SerializeField] bool m_sizeFromRectTransform = true;

        [Tooltip("CSS px → device px scale.")]
        [SerializeField] float m_devicePixelRatio = 1f;

        [Tooltip("RawImage that displays the view texture (optional; defaults to a RawImage on this GameObject).")]
        [SerializeField] RawImage m_targetImage;

        [Tooltip("Stage 1: paint the built-in Skia test frame right after the view is created.")]
        [SerializeField] bool m_drawTestFrameOnEnable = true;

        [Tooltip("Document to load on enable, relative to StreamingAssets (for example UI/MainMenu/index.html).")]
        [SerializeField] string m_documentPath = "";

        string m_loadedPath;
        ulong m_handle = Native.InvalidView;
        WebTexture m_webTexture;
        Material m_material;
        Vector2Int m_currentSize;
        HtmlViewManager m_manager;

        /// <summary>Texture with the rendered UI (premultiplied alpha, sRGB). Null until the first frame.</summary>
        public Texture Texture => m_webTexture?.Texture;

        public WebTexture WebTexture => m_webTexture;

        /// <summary>Native view handle; 0 when the view is not created.</summary>
        public ulong Handle => m_handle;

        public bool IsCreated => m_handle != Native.InvalidView;

        public RenderProvider Provider { get; private set; } = RenderProvider.None;

        public Vector2Int Size
        {
            get => m_size;
            set
            {
                m_size = value;
                if (!m_sizeFromRectTransform)
                {
                    ApplySize(value);
                }
            }
        }

        public bool SizeFromRectTransform
        {
            get => m_sizeFromRectTransform;
            set => m_sizeFromRectTransform = value;
        }

        public float DevicePixelRatio
        {
            get => m_devicePixelRatio;
            set => m_devicePixelRatio = Mathf.Max(0.1f, value);
        }

        public RawImage TargetImage
        {
            get => m_targetImage;
            set => m_targetImage = value;
        }

        public bool DrawTestFrameOnEnable
        {
            get => m_drawTestFrameOnEnable;
            set => m_drawTestFrameOnEnable = value;
        }

        /// <summary>Document loaded automatically when the component is enabled (may be empty).</summary>
        public string DocumentPath
        {
            get => m_documentPath;
            set => m_documentPath = value;
        }

        // ---- lifecycle -----------------------------------------------------

        void OnEnable()
        {
            m_manager = HtmlViewManager.Instance;
            if (m_manager == null || !m_manager.IsNativeReady)
            {
                Debug.LogError("[xploit_game_ui] native runtime is not available; HtmlView disabled");
                enabled = false;
                return;
            }
            if (m_targetImage == null)
            {
                m_targetImage = GetComponent<RawImage>();
            }
            CreateNativeView();
            m_manager.Register(this);
            if (m_drawTestFrameOnEnable && IsCreated)
            {
                DrawTestFrame();
            }
            if (!string.IsNullOrEmpty(m_documentPath) && IsCreated)
            {
                Load(m_documentPath);
            }
        }

        void OnDisable()
        {
            if (m_manager != null)
            {
                m_manager.Unregister(this);
            }
            ReleaseNativeView();
            if (m_manager != null)
            {
                m_manager.IssueGc();
            }
        }

        void CreateNativeView()
        {
            var size = ResolveSize();
            var provider = HtmlViewManager.Provider;
            var format = provider == RenderProvider.Cpu ? Native.TextureFormat.RGBA8 : Native.TextureFormat.BGRA8;
            var desc = new Native.ViewDesc
            {
                struct_size = (uint)System.Runtime.InteropServices.Marshal.SizeOf<Native.ViewDesc>(),
                width = (uint)Mathf.Max(1, size.x),
                height = (uint)Mathf.Max(1, size.y),
                device_pixel_ratio = m_devicePixelRatio,
                format = format,
                provider = RenderProvider.Auto,
                ui_root = Native.Utf8(Application.streamingAssetsPath),
                name = Native.Utf8(name),
            };
            try
            {
                m_handle = Native.xgu_view_create(ref desc);
            }
            finally
            {
                System.Runtime.InteropServices.Marshal.FreeCoTaskMem(desc.ui_root);
                System.Runtime.InteropServices.Marshal.FreeCoTaskMem(desc.name);
            }
            if (m_handle == Native.InvalidView)
            {
                Debug.LogError($"[xploit_game_ui] failed to create view \"{name}\"");
                return;
            }
            Provider = provider == RenderProvider.None ? RenderProvider.Cpu : provider;
            m_currentSize = size;
            m_webTexture = new WebTexture(m_handle, Provider, format);
            EnsureMaterial();
            if (EnableDebug)
            {
                Debug.Log($"[xploit_game_ui] view \"{name}\" created: {size.x}x{size.y}, provider {Provider}");
            }
        }

        internal void ReleaseNativeView()
        {
            if (m_targetImage != null)
            {
                m_targetImage.texture = null;
            }
            m_webTexture?.Dispose();
            m_webTexture = null;
            if (m_handle != Native.InvalidView)
            {
                Native.xgu_view_destroy(m_handle);
                m_handle = Native.InvalidView;
            }
        }

        // ---- per frame -----------------------------------------------------

        internal void Tick()
        {
            if (!IsCreated)
            {
                return;
            }
            var size = ResolveSize();
            if (size != m_currentSize && size.x > 0 && size.y > 0)
            {
                ApplySize(size);
            }
            if (Native.xgu_view_has_pending_frame(m_handle))
            {
                m_manager.IssuePaint(m_handle);
            }
            ReportStateChanges();
            var status = Native.xgu_view_status(m_handle);
            if ((status & Native.ViewStatus.DeviceLost) != 0)
            {
                Debug.LogWarning($"[xploit_game_ui] view \"{name}\": graphics device lost, recreating");
                ReleaseNativeView();
                CreateNativeView();
                if (m_drawTestFrameOnEnable)
                {
                    DrawTestFrame();
                }
                return;
            }
            m_webTexture.Update();
            if (m_targetImage != null && m_targetImage.texture != m_webTexture.Texture)
            {
                m_targetImage.texture = m_webTexture.Texture;
                m_targetImage.material = m_material;
            }
        }

        Vector2Int ResolveSize()
        {
            if (m_sizeFromRectTransform)
            {
                var rt = m_targetImage != null ? m_targetImage.rectTransform : transform as RectTransform;
                if (rt != null)
                {
                    var rect = rt.rect;
                    var scale = rt.lossyScale;
                    var canvas = rt.GetComponentInParent<Canvas>();
                    float pixelScale = canvas != null ? canvas.scaleFactor : 1f;
                    int w = Mathf.RoundToInt(rect.width * Mathf.Abs(scale.x) * pixelScale / Mathf.Max(0.0001f, canvas != null ? canvas.transform.lossyScale.x : 1f));
                    int h = Mathf.RoundToInt(rect.height * Mathf.Abs(scale.y) * pixelScale / Mathf.Max(0.0001f, canvas != null ? canvas.transform.lossyScale.y : 1f));
                    if (w > 0 && h > 0)
                    {
                        return new Vector2Int(w, h);
                    }
                }
            }
            return m_size;
        }

        void ApplySize(Vector2Int size)
        {
            if (!IsCreated || size.x <= 0 || size.y <= 0)
            {
                return;
            }
            m_currentSize = size;
            Native.xgu_view_resize(m_handle, (uint)size.x, (uint)size.y, m_devicePixelRatio);
            if (m_drawTestFrameOnEnable)
            {
                DrawTestFrame();
            }
        }

        void EnsureMaterial()
        {
            if (m_material != null)
            {
                return;
            }
            var shader = Shader.Find("XploitGameUI/RawImagePremultiplied");
            if (shader == null)
            {
                Debug.LogWarning("[xploit_game_ui] shader XploitGameUI/RawImagePremultiplied not found; alpha will look wrong");
                return;
            }
            m_material = new Material(shader) { name = "xploit_game_ui premultiplied", hideFlags = HideFlags.HideAndDontSave };
        }

        void OnDestroy()
        {
            if (m_material != null)
            {
                if (Application.isPlaying) Destroy(m_material); else DestroyImmediate(m_material);
                m_material = null;
            }
        }

        // ---- Stage 1 API ---------------------------------------------------

        /// <summary>Stage 1 spike: paints the built-in Skia test frame (rounded panel, image, text).</summary>
        public void DrawTestFrame()
        {
            if (!IsCreated)
            {
                return;
            }
            var status = Native.xgu_view_draw_test_frame(m_handle);
            if (status != Native.Status.Ok)
            {
                Debug.LogError($"[xploit_game_ui] DrawTestFrame failed: {status}");
            }
        }

        // ---- input ---------------------------------------------------------

        /// <summary>
        /// Sends one input event to the native view. Positions are CSS pixels
        /// from the view's top-left. <see cref="WebInput"/> is the usual caller;
        /// call this directly to feed input from your own source.
        /// </summary>
        public void SendInput(WebInputEvent input)
        {
            if (!IsCreated)
            {
                return;
            }
            // The native side copies the strings, so the allocations only need to
            // survive this call.
            var keyPtr = Native.Utf8(input.Key);
            var codePtr = Native.Utf8(input.Code);
            var textPtr = Native.Utf8(input.Text);
            try
            {
                var evt = new Native.InputEvent
                {
                    StructSize = (uint)System.Runtime.InteropServices.Marshal.SizeOf<Native.InputEvent>(),
                    Type = (Native.InputType)input.Type,
                    X = input.Position.x,
                    Y = input.Position.y,
                    DeltaX = input.Delta.x,
                    DeltaY = input.Delta.y,
                    Button = (int)input.Button,
                    Buttons = (uint)input.Buttons,
                    Modifiers = (uint)input.Modifiers,
                    Key = keyPtr,
                    Code = codePtr,
                    Text = textPtr,
                    TouchId = input.TouchId,
                    Time = input.Time > 0.0 ? input.Time : Time.realtimeSinceStartupAsDouble,
                    Repeat = input.Repeat,
                };
                var status = Native.xgu_view_send_input(m_handle, ref evt);
                if (status != Native.Status.Ok)
                {
                    Debug.LogError($"[xploit_game_ui] SendInput({input.Type}) failed: {status}");
                }
            }
            finally
            {
                if (keyPtr != IntPtr.Zero) System.Runtime.InteropServices.Marshal.FreeCoTaskMem(keyPtr);
                if (codePtr != IntPtr.Zero) System.Runtime.InteropServices.Marshal.FreeCoTaskMem(codePtr);
                if (textPtr != IntPtr.Zero) System.Runtime.InteropServices.Marshal.FreeCoTaskMem(textPtr);
            }
        }

        /// <summary>Moves keyboard focus to the element with this id; an empty id clears it.</summary>
        public void SetFocus(string elementId)
        {
            if (IsCreated)
            {
                Native.xgu_view_set_focus(m_handle, elementId ?? string.Empty);
            }
        }

        /// <summary>
        /// Converts a screen point to view coordinates in CSS pixels. Returns
        /// false when the point is outside the view's rectangle.
        /// </summary>
        public bool ScreenToView(Vector2 screenPoint, Camera camera, out Vector2 viewPoint)
        {
            viewPoint = Vector2.zero;
            var rect = transform as RectTransform;
            if (rect == null)
            {
                return false;
            }
            if (!RectTransformUtility.ScreenPointToLocalPointInRectangle(rect, screenPoint, camera, out var local))
            {
                return false;
            }
            var size = rect.rect.size;
            if (size.x <= 0f || size.y <= 0f)
            {
                return false;
            }
            // Local point is centred with y up; the view is top-left with y down.
            var normalized = new Vector2((local.x - rect.rect.x) / size.x, 1f - (local.y - rect.rect.y) / size.y);
            viewPoint = new Vector2(normalized.x * m_currentSize.x, normalized.y * m_currentSize.y) / DevicePixelRatio;
            return normalized.x >= 0f && normalized.x <= 1f && normalized.y >= 0f && normalized.y <= 1f;
        }

        // ---- public API (implemented in later stages) ----------------------

        /// <summary>
        /// Loads an HTML document. The path is relative to StreamingAssets, for
        /// example "UI/MainMenu/index.html". Parsing and script execution happen
        /// on the native runtime thread; watch <see cref="State"/> for progress.
        /// References that would leave StreamingAssets are refused.
        /// </summary>
        public void Load(string path)
        {
            if (!IsCreated)
            {
                Debug.LogWarning($"[xploit_game_ui] Load on a view that is not created (\"{name}\")");
                return;
            }
            if (string.IsNullOrEmpty(path))
            {
                Debug.LogError("[xploit_game_ui] Load: the path is empty");
                return;
            }
            m_loadedPath = path;
            m_reportedStage = 0;
            var status = Native.xgu_view_load(m_handle, path);
            if (status != Native.Status.Ok)
            {
                Debug.LogError($"[xploit_game_ui] Load(\"{path}\") failed: {status}");
            }
        }

        /// <summary>Loads HTML held in memory. <paramref name="basePath"/> anchors relative references.</summary>
        public void LoadHtml(string html, string basePath = null)
        {
            if (!IsCreated || html == null)
            {
                return;
            }
            m_loadedPath = null;
            var status = Native.xgu_view_load_html(m_handle, html, basePath ?? string.Empty);
            if (status != Native.Status.Ok)
            {
                Debug.LogError($"[xploit_game_ui] LoadHtml failed: {status}");
            }
        }

        /// <summary>Re-reads the document passed to <see cref="Load"/>, with a fresh JavaScript isolate.</summary>
        public void Reload()
        {
            if (!IsCreated)
            {
                return;
            }
            var status = Native.xgu_view_reload(m_handle);
            if (status != Native.Status.Ok)
            {
                Debug.LogError($"[xploit_game_ui] Reload failed: {status}");
            }
        }

        /// <summary>Document passed to <see cref="Load"/>, or null.</summary>
        public string LoadedPath => m_loadedPath;

        /// <summary>
        /// Compiles and runs JavaScript in this view's isolate (on the native runtime thread).
        /// console.* output and uncaught errors appear in the Unity Console.
        /// </summary>
        public void ExecuteJS(string javascript) => ExecuteJS(javascript, null);

        /// <param name="origin">Name shown in error stack traces; defaults to "&lt;view name&gt;.ExecuteJS".</param>
        public void ExecuteJS(string javascript, string origin)
        {
            if (!IsCreated)
            {
                Debug.LogWarning($"[xploit_game_ui] ExecuteJS on a view that is not created (\"{name}\")");
                return;
            }
            if (string.IsNullOrEmpty(javascript))
            {
                return;
            }
            var status = Native.xgu_view_execute_js(m_handle, javascript, origin ?? $"{name}.ExecuteJS");
            if (status != Native.Status.Ok)
            {
                Debug.LogError($"[xploit_game_ui] ExecuteJS failed: {status}");
            }
        }

        /// <summary>Pauses JavaScript timers and frame processing for this view.</summary>
        public bool Paused
        {
            get => IsCreated && Native.xgu_view_get_state(m_handle) == ViewState.Paused;
            set
            {
                if (IsCreated)
                {
                    Native.xgu_view_set_paused(m_handle, value);
                }
            }
        }

        /// <summary>Lifecycle state of the native view.</summary>
        public ViewState State => IsCreated ? Native.xgu_view_get_state(m_handle) : ViewState.Destroyed;

        /// <summary>
        /// Raised once the document is parsed and the DOM can be queried, before
        /// its scripts have run.
        /// </summary>
        public event Action<HtmlView> DomReady;

        /// <summary>Raised once the document's scripts have run.</summary>
        public event Action<HtmlView> JsReady;

        /// <summary>Raised once a frame has been painted and the view accepts input.</summary>
        public event Action<HtmlView> Interactive;

        /// <summary>
        /// Raised for every runtime message this view's document produced:
        /// console output, CSS and HTML warnings, uncaught script errors.
        /// </summary>
        public event Action<WebLogMessage> Log;

        internal void RaiseLog(in WebLogMessage message)
        {
            try
            {
                Log?.Invoke(message);
            }
            catch (Exception error)
            {
                Debug.LogException(error, this);
            }
        }

        // How far the lifecycle has been reported: 0 nothing, 1 DomReady,
        // 2 JsReady, 3 Interactive. Counted rather than compared against the
        // state value, because Paused and Destroyed are not later stages.
        int m_reportedStage;

        // Turns the native state into the events above. Called once a frame.
        void ReportStateChanges()
        {
            var reached = State switch
            {
                ViewState.DomReady => 1,
                ViewState.JsReady => 2,
                ViewState.Interactive => 3,
                _ => m_reportedStage,
            };
            // A load can pass several stages between two frames, so each one it
            // went through is reported, in order.
            while (m_reportedStage < reached)
            {
                m_reportedStage++;
                switch (m_reportedStage)
                {
                    case 1: RaiseLifecycle(DomReady); break;
                    case 2: RaiseLifecycle(JsReady); break;
                    case 3: RaiseLifecycle(Interactive); break;
                }
            }
        }

        void RaiseLifecycle(Action<HtmlView> handler)
        {
            try
            {
                handler?.Invoke(this);
            }
            catch (Exception error)
            {
                Debug.LogException(error, this);
            }
        }

        /// <summary>
        /// Resizes the view's texture. Normally the RectTransform drives this;
        /// call it when <see cref="SizeFromRectTransform"/> is off.
        /// </summary>
        public void Resize(int width, int height, float devicePixelRatio = 0f)
        {
            if (devicePixelRatio > 0f)
            {
                DevicePixelRatio = devicePixelRatio;
            }
            SizeFromRectTransform = false;
            Size = new Vector2Int(Mathf.Max(1, width), Mathf.Max(1, height));
        }

        // ---- bridge --------------------------------------------------------

        readonly Dictionary<string, List<Action<WebEvent>>> m_eventHandlers =
            new Dictionary<string, List<Action<WebEvent>>>();
        readonly Dictionary<string, Func<WebArguments, object>> m_functions =
            new Dictionary<string, Func<WebArguments, object>>();
        readonly Dictionary<string, Func<WebArguments, Task<object>>> m_asyncFunctions =
            new Dictionary<string, Func<WebArguments, Task<object>>>();

        /// <summary>
        /// Sends an event to the page: every handler registered with
        /// Unity.on(eventName, ...) runs with these arguments. Values are
        /// serialised as JSON, so primitives, strings, arrays, dictionaries and
        /// [Serializable] types all cross.
        /// </summary>
        public void Send(string eventName, params object[] args)
        {
            if (!IsCreated)
            {
                Debug.LogWarning($"[xploit_game_ui] Send on a view that is not created (\"{name}\")");
                return;
            }
            if (string.IsNullOrEmpty(eventName))
            {
                Debug.LogError("[xploit_game_ui] Send: the event name is empty");
                return;
            }
            var status = Native.xgu_view_send_event(m_handle, eventName, WebJson.SerializeArguments(args));
            if (status != Native.Status.Ok)
            {
                Debug.LogError($"[xploit_game_ui] Send(\"{eventName}\") failed: {status}");
            }
        }

        /// <summary>Subscribes to Unity.emit(eventName, ...) from page script.</summary>
        public void On(string eventName, Action<WebEvent> callback)
        {
            if (string.IsNullOrEmpty(eventName) || callback == null)
            {
                return;
            }
            if (!m_eventHandlers.TryGetValue(eventName, out var handlers))
            {
                handlers = new List<Action<WebEvent>>();
                m_eventHandlers[eventName] = handlers;
            }
            handlers.Add(callback);
        }

        /// <summary>
        /// Removes one handler, or every handler for the event when
        /// <paramref name="callback"/> is null.
        /// </summary>
        public void Off(string eventName, Action<WebEvent> callback = null)
        {
            if (string.IsNullOrEmpty(eventName) || !m_eventHandlers.TryGetValue(eventName, out var handlers))
            {
                return;
            }
            if (callback == null)
            {
                m_eventHandlers.Remove(eventName);
                return;
            }
            handlers.Remove(callback);
            if (handlers.Count == 0)
            {
                m_eventHandlers.Remove(eventName);
            }
        }

        /// <summary>
        /// Registers a function the page can await through Unity.call(name, ...).
        /// The handler runs on Unity's main thread; whatever it returns becomes
        /// the resolved value, and an exception rejects the page's promise.
        /// </summary>
        public void RegisterFunction(string name, Func<WebArguments, object> callback)
        {
            if (string.IsNullOrEmpty(name) || callback == null)
            {
                return;
            }
            m_asyncFunctions.Remove(name);
            m_functions[name] = callback;
        }

        /// <summary>
        /// Same as <see cref="RegisterFunction"/> for work that takes more than a
        /// frame. The page's promise settles when the task does.
        /// </summary>
        public void RegisterFunctionAsync(string name, Func<WebArguments, Task<object>> callback)
        {
            if (string.IsNullOrEmpty(name) || callback == null)
            {
                return;
            }
            m_functions.Remove(name);
            m_asyncFunctions[name] = callback;
        }

        public void UnregisterFunction(string name)
        {
            if (string.IsNullOrEmpty(name))
            {
                return;
            }
            m_functions.Remove(name);
            m_asyncFunctions.Remove(name);
        }

        /// <summary>
        /// What the view's last frame cost, and how many frames it has produced.
        /// An idle document produces none at all.
        /// </summary>
        public bool TryGetFrameStats(out double styleMs, out double layoutMs, out double paintMs, out double rasterMs,
            out ulong published, out ulong skipped, out RectInt damage)
        {
            styleMs = layoutMs = paintMs = rasterMs = 0;
            published = skipped = 0;
            damage = default;
            if (!IsCreated)
            {
                return false;
            }
            var stats = new Native.FrameStats
            {
                StructSize = (uint)System.Runtime.InteropServices.Marshal.SizeOf<Native.FrameStats>(),
            };
            if (!Native.xgu_view_get_stats(m_handle, ref stats))
            {
                return false;
            }
            styleMs = stats.StyleMs;
            layoutMs = stats.LayoutMs;
            paintMs = stats.PaintMs;
            rasterMs = stats.RasterMs;
            published = stats.FramesPublished;
            skipped = stats.FramesSkipped;
            damage = new RectInt(stats.DamageX, stats.DamageY, stats.DamageWidth, stats.DamageHeight);
            return true;
        }

        /// <summary>Messages dropped because a bridge queue filled up.</summary>
        public ulong BridgeDroppedCount => IsCreated ? Native.xgu_view_bridge_dropped(m_handle) : 0;

        /// <summary>
        /// Delivers everything the page queued. Called once a frame by
        /// <see cref="HtmlViewManager"/>, on the main thread.
        /// </summary>
        internal void PumpMessages()
        {
            if (!IsCreated)
            {
                return;
            }
            var message = new Native.Message
            {
                StructSize = (uint)System.Runtime.InteropServices.Marshal.SizeOf<Native.Message>(),
            };
            // A handler may call Send or even Load, so the loop re-reads the
            // handle state every time.
            while (IsCreated && Native.xgu_view_poll_message(m_handle, ref message))
            {
                var eventName = message.Name == IntPtr.Zero
                    ? string.Empty
                    : System.Runtime.InteropServices.Marshal.PtrToStringUTF8(message.Name);
                var json = message.Json == IntPtr.Zero
                    ? null
                    : System.Runtime.InteropServices.Marshal.PtrToStringUTF8(message.Json);

                if (message.Kind == Native.MessageKind.Emit)
                {
                    DispatchEmit(eventName, json);
                }
                else
                {
                    DispatchCall(message.Id, eventName, json);
                }
            }
        }

        void DispatchEmit(string eventName, string json)
        {
            if (!m_eventHandlers.TryGetValue(eventName, out var handlers) || handlers.Count == 0)
            {
                if (EnableDebug)
                {
                    Debug.Log($"[xploit_game_ui] no handler for Unity.emit(\"{eventName}\")");
                }
                return;
            }
            var webEvent = new WebEvent(eventName, WebArguments.FromJson(json), this);
            // Copy: a handler may call Off while it runs.
            foreach (var handler in handlers.ToArray())
            {
                try
                {
                    handler(webEvent);
                }
                catch (Exception error)
                {
                    // One bad handler must not stop the others or the frame.
                    Debug.LogException(error, this);
                }
            }
        }

        void DispatchCall(ulong id, string functionName, string json)
        {
            var args = WebArguments.FromJson(json);
            if (m_functions.TryGetValue(functionName, out var sync))
            {
                try
                {
                    Reply(id, true, WebJson.Serialize(sync(args)));
                }
                catch (Exception error)
                {
                    Reply(id, false, DescribeError(error));
                    Debug.LogException(error, this);
                }
                return;
            }
            if (m_asyncFunctions.TryGetValue(functionName, out var async))
            {
                RunAsyncCall(id, functionName, async, args);
                return;
            }
            Reply(id, false, WebJson.Serialize(new Dictionary<string, object>
            {
                { "name", "ReferenceError" },
                { "message", $"no C# function is registered as \"{functionName}\"" },
            }));
        }

        async void RunAsyncCall(ulong id, string functionName, Func<WebArguments, Task<object>> handler,
            WebArguments args)
        {
            try
            {
                var result = await handler(args);
                Reply(id, true, WebJson.Serialize(result));
            }
            catch (Exception error)
            {
                Reply(id, false, DescribeError(error));
                Debug.LogError($"[xploit_game_ui] Unity.call(\"{functionName}\") failed: {error.Message}");
            }
        }

        void Reply(ulong id, bool ok, string json)
        {
            if (!IsCreated)
            {
                return; // the view went away while the call was running
            }
            Native.xgu_view_reply(m_handle, id, ok, json);
        }

        static string DescribeError(Exception error)
        {
            return WebJson.Serialize(new Dictionary<string, object>
            {
                { "name", error.GetType().Name },
                { "message", error.Message },
                { "stack", error.StackTrace ?? string.Empty },
            });
        }
    }
}
