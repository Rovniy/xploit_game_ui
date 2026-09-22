using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace Xploit.GameUI
{
    /// <summary>
    /// Process-wide owner of the native runtime: initialises it on first use,
    /// ticks every <see cref="HtmlView"/> once per frame, issues the render
    /// events and tears everything down on quit / domain reload.
    /// </summary>
    [DefaultExecutionOrder(-1000)]
    [AddComponentMenu("")]
    public sealed class HtmlViewManager : MonoBehaviour
    {
        static HtmlViewManager s_instance;
        static bool s_quitting;

        readonly List<HtmlView> m_views = new List<HtmlView>();
        CommandBuffer m_commandBuffer;
        IntPtr m_renderEventFunc;
        int m_eventBase;
        bool m_nativeReady;

        /// <summary>Gets (and lazily creates) the manager. Returns null while the application is quitting.</summary>
        public static HtmlViewManager Instance
        {
            get
            {
                if (s_instance == null && !s_quitting)
                {
                    var go = new GameObject("[xploit_game_ui]") { hideFlags = HideFlags.HideAndDontSave };
                    s_instance = go.AddComponent<HtmlViewManager>();
                }
                return s_instance;
            }
        }

        /// <summary>Texture provider chosen by the native runtime for this process.</summary>
        public static RenderProvider Provider => Native.xgu_render_provider();

        /// <summary>Native runtime version string.</summary>
        public static string NativeVersion => Native.Version();

        public bool IsNativeReady => m_nativeReady;
        public int ViewCount => m_views.Count;

        void Awake()
        {
            if (s_instance != null && s_instance != this)
            {
                Destroy(gameObject);
                return;
            }
            s_instance = this;
            DontDestroyOnLoad(gameObject);
            InitializeNative();
#if UNITY_EDITOR
            UnityEditor.AssemblyReloadEvents.beforeAssemblyReload += OnBeforeAssemblyReload;
#endif
        }

        void InitializeNative()
        {
            if (m_nativeReady)
            {
                return;
            }
            var desc = new Native.InitDesc
            {
                struct_size = (uint)System.Runtime.InteropServices.Marshal.SizeOf<Native.InitDesc>(),
                data_dir = Native.Utf8(Application.persistentDataPath),
            };
            try
            {
                var status = Native.xgu_initialize(ref desc);
                if (status != Native.Status.Ok)
                {
                    Debug.LogError($"[xploit_game_ui] xgu_initialize failed: {status}");
                    return;
                }
            }
            finally
            {
                if (desc.data_dir != IntPtr.Zero)
                {
                    System.Runtime.InteropServices.Marshal.FreeCoTaskMem(desc.data_dir);
                }
            }
            m_renderEventFunc = Native.xgu_get_render_event_func();
            m_eventBase = Native.xgu_render_event_base();
            m_commandBuffer = new CommandBuffer { name = "xploit_game_ui" };
            // From here on native log messages (including console.* from the runtime
            // thread) are queued and reported from DrainLogs on the main thread.
            Native.xgu_log_queue_enable(true);
            m_nativeReady = true;
            if (HtmlView.EnableDebug)
            {
                Debug.Log($"[xploit_game_ui] native {NativeVersion}, provider {Provider}, device {SystemInfo.graphicsDeviceType}");
            }
        }

        internal void Register(HtmlView view)
        {
            if (!m_views.Contains(view))
            {
                m_views.Add(view);
            }
        }

        internal void Unregister(HtmlView view)
        {
            m_views.Remove(view);
        }

        void LateUpdate()
        {
            if (!m_nativeReady)
            {
                return;
            }
            // One runtime frame for every view (JS message loop; later timers, layout, paint).
            Native.xgu_tick(Time.unscaledTimeAsDouble);
            DrainLogs();
            for (int i = 0; i < m_views.Count; i++)
            {
                var view = m_views[i];
                if (view != null && view.isActiveAndEnabled)
                {
                    view.Tick();
                }
            }
        }

        /// <summary>
        /// Reports queued native log messages through Debug.Log on the main thread.
        /// Bounded per frame so a runaway script cannot stall the editor.
        /// </summary>
        void DrainLogs()
        {
            const int maxPerFrame = 256;
            for (int i = 0; i < maxPerFrame; i++)
            {
                if (!Native.xgu_log_poll(out var level, out var messagePtr) || messagePtr == IntPtr.Zero)
                {
                    break;
                }
                var message = System.Runtime.InteropServices.Marshal.PtrToStringUTF8(messagePtr);
                switch ((Native.LogLevel)level)
                {
                    case Native.LogLevel.Error:
                        Debug.LogError(message);
                        break;
                    case Native.LogLevel.Warning:
                        Debug.LogWarning(message);
                        break;
                    default:
                        Debug.Log(message);
                        break;
                }
            }
            var dropped = Native.xgu_log_dropped_count();
            if (dropped > 0)
            {
                Debug.LogWarning($"[xploit_game_ui] {dropped} log message(s) dropped (queue overflow)");
            }
        }

        /// <summary>Asks the graphics backend to paint the view's pending frame (submission thread).</summary>
        internal void IssuePaint(ulong handle)
        {
            if (!m_nativeReady || handle == Native.InvalidView)
            {
                return;
            }
            m_commandBuffer.Clear();
            m_commandBuffer.IssuePluginEventAndData(m_renderEventFunc, m_eventBase + Native.EventPaint, (IntPtr)handle);
            Graphics.ExecuteCommandBuffer(m_commandBuffer);
        }

        /// <summary>Lets the native side release GPU resources of destroyed views once the GPU is done with them.</summary>
        internal void IssueGc()
        {
            if (!m_nativeReady)
            {
                return;
            }
            m_commandBuffer.Clear();
            m_commandBuffer.IssuePluginEventAndData(m_renderEventFunc, m_eventBase + Native.EventGc, IntPtr.Zero);
            Graphics.ExecuteCommandBuffer(m_commandBuffer);
        }

        void DestroyAllViews()
        {
            for (int i = m_views.Count - 1; i >= 0; i--)
            {
                var view = m_views[i];
                if (view != null)
                {
                    view.ReleaseNativeView();
                }
            }
            m_views.Clear();
            if (m_nativeReady)
            {
                Native.xgu_views_destroy_all();
                IssueGc();
            }
        }

#if UNITY_EDITOR
        void OnBeforeAssemblyReload()
        {
            DestroyAllViews();
        }
#endif

        void OnApplicationQuit()
        {
            s_quitting = true;
            DestroyAllViews();
        }

        void OnDestroy()
        {
#if UNITY_EDITOR
            UnityEditor.AssemblyReloadEvents.beforeAssemblyReload -= OnBeforeAssemblyReload;
#endif
            DestroyAllViews();
            if (m_nativeReady)
            {
                DrainLogs();
                Native.xgu_log_queue_enable(false); // back to the direct IUnityLog sink
            }
            m_commandBuffer?.Release();
            m_commandBuffer = null;
            if (s_instance == this)
            {
                s_instance = null;
            }
        }
    }
}
