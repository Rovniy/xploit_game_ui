using System;
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

        // ---- public API (implemented in later stages) ----------------------

        /// <summary>Loads an HTML document relative to StreamingAssets (Stage 3).</summary>
        public void Load(string path) => throw new NotImplementedException("HtmlView.Load arrives in Stage 3 (HTML/DOM).");

        /// <summary>Reloads the current document (Stage 3).</summary>
        public void Reload() => throw new NotImplementedException("HtmlView.Reload arrives in Stage 3 (HTML/DOM).");

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

        /// <summary>Sends an event to JavaScript: Unity.on(eventName, ...) (Stage 7).</summary>
        public void Send(string eventName, params object[] args) => throw new NotImplementedException("HtmlView.Send arrives in Stage 7 (bridge).");

        /// <summary>Subscribes to Unity.emit(eventName, ...) from JavaScript (Stage 7).</summary>
        public void On(string eventName, Action<WebEvent> callback) => throw new NotImplementedException("HtmlView.On arrives in Stage 7 (bridge).");

        /// <summary>Registers a C# function callable through await Unity.call(name, ...) (Stage 7).</summary>
        public void RegisterFunction(string name, Func<WebArguments, object> callback) => throw new NotImplementedException("HtmlView.RegisterFunction arrives in Stage 7 (bridge).");
    }
}
