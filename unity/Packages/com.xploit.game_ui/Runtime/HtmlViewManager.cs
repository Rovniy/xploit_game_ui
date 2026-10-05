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
        const string RuntimeDataDirectoryName = "XploitGameUI";

        // Runtime state only. [NonSerialized] because the editor serialises a
        // MonoBehaviour's private fields across a domain reload, and a manager
        // brought back with m_nativeReady but without its CommandBuffer is what
        // used to throw from IssueGc.
        [NonSerialized] readonly List<HtmlView> m_views = new List<HtmlView>();
        [NonSerialized] CommandBuffer m_commandBuffer;
        [NonSerialized] IntPtr m_renderEventFunc;
        [NonSerialized] int m_eventBase;
        [NonSerialized] bool m_nativeReady;

        // With domain reload off (Enter Play Mode Options) statics survive from
        // one Play Mode session to the next: without this, s_quitting stayed true
        // and the second session never got a manager.
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        static void ResetStatics()
        {
            s_instance = null;
            s_quitting = false;
        }

        // Only the current manager does anything. Any other one is a leftover and
        // removes itself.
        bool IsCurrent => s_instance == this;

        /// <summary>Gets (and lazily creates) the manager. Returns null while the application is quitting.</summary>
        public static HtmlViewManager Instance
        {
            get
            {
                if (s_instance == null && !s_quitting)
                {
                    // Not DontSave while playing: the editor keeps DontSave objects
                    // after Play Mode ends, and every session used to leave one
                    // manager behind. DontDestroyOnLoad (in Awake) goes with Play Mode.
                    var go = new GameObject("[xploit_game_ui]")
                    {
                        hideFlags = Application.isPlaying
                            ? HideFlags.HideInHierarchy | HideFlags.NotEditable
                            : HideFlags.HideAndDontSave,
                    };
                    s_instance = go.AddComponent<HtmlViewManager>();
                }
                return s_instance;
            }
        }

        /// <summary>The manager if one exists, without creating it (safe outside Play Mode).</summary>
        public static HtmlViewManager Current => s_instance;

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
            if (Application.isPlaying)
            {
                DontDestroyOnLoad(gameObject);
            }
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
                // StreamingAssets data ships with the player; persistentDataPath is empty on first launch.
                // Native V8 loading also falls back to the plugin and executable directories.
                data_dir = Native.Utf8(System.IO.Path.Combine(Application.streamingAssetsPath, RuntimeDataDirectoryName)),
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
            // Not in batch mode: a build machine or a test run has no one to use it.
            if (WebDevTools.AutoStart && !Application.isBatchMode && !WebDevTools.IsRunning)
            {
                WebDevTools.Start(WebDevTools.AutoStartPort);
            }
            if (HtmlView.EnableDebug)
            {
                Debug.Log($"[xploit_game_ui] native {NativeVersion}, provider {Provider}, device {SystemInfo.graphicsDeviceType}");
            }
        }

        /// <summary>
        /// Every message from the runtime, including the ones a view also raises
        /// through <see cref="HtmlView.Log"/>. Useful for an in-game console.
        /// </summary>
        public static event Action<WebLogMessage> Log;

        /// <summary>
        /// Stops the runtime's messages from reaching the Unity Console. The
        /// <see cref="Log"/> events still fire, so set this when routing output
        /// somewhere of your own.
        /// </summary>
        public static bool SuppressConsoleOutput { get; set; }

        const int RecentLogCapacity = 2000;
        static readonly List<WebLogMessage> s_recentLogs = new List<WebLogMessage>();

        /// <summary>
        /// The last messages from the runtime, oldest first, so a console opened
        /// late still shows what happened before it.
        /// </summary>
        public static IReadOnlyList<WebLogMessage> RecentLogs => s_recentLogs;

        /// <summary>Forgets <see cref="RecentLogs"/>.</summary>
        public static void ClearRecentLogs() => s_recentLogs.Clear();

        /// <summary>The views currently alive, in registration order.</summary>
        public static IReadOnlyList<HtmlView> Views =>
            s_instance != null ? s_instance.m_views : System.Array.Empty<HtmlView>();

        HtmlView FindView(ulong handle)
        {
            if (handle == Native.InvalidView)
            {
                return null;
            }
            for (int i = 0; i < m_views.Count; i++)
            {
                if (m_views[i] != null && m_views[i].Handle == handle)
                {
                    return m_views[i];
                }
            }
            return null;
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
            if (!IsCurrent)
            {
                Destroy(gameObject);
                return;
            }
            if (!m_nativeReady)
            {
                return;
            }
            // One runtime frame for every view (JS message loop; later timers, layout, paint).
            Native.xgu_tick(Time.unscaledTimeAsDouble);
            DrainLogs();
            WebDevTools.Pump();
            for (int i = 0; i < m_views.Count; i++)
            {
                var view = m_views[i];
                if (view != null && view.isActiveAndEnabled)
                {
                    view.Tick();
                    // Bridge messages run here so page handlers and Unity.call
                    // implementations always see the main thread.
                    view.PumpMessages();
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
                if (!Native.xgu_log_poll(out var level, out var messagePtr, out var handle) ||
                    messagePtr == IntPtr.Zero)
                {
                    break;
                }
                var text = System.Runtime.InteropServices.Marshal.PtrToStringUTF8(messagePtr);
                var message = new WebLogMessage((WebLogLevel)level, text, FindView(handle));
                if (s_recentLogs.Count >= RecentLogCapacity)
                {
                    s_recentLogs.RemoveRange(0, RecentLogCapacity / 10);
                }
                s_recentLogs.Add(message);
                message.View?.RaiseLog(message);
                Log?.Invoke(message);
                if (SuppressConsoleOutput)
                {
                    continue;
                }
                switch (message.Level)
                {
                    case WebLogLevel.Error:
                        Debug.LogError(text, message.View);
                        break;
                    case WebLogLevel.Warning:
                        Debug.LogWarning(text, message.View);
                        break;
                    default:
                        Debug.Log(text, message.View);
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
            if (!m_nativeReady || m_commandBuffer == null || handle == Native.InvalidView)
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
            if (!m_nativeReady || m_commandBuffer == null)
            {
                return;
            }
            m_commandBuffer.Clear();
            m_commandBuffer.IssuePluginEventAndData(m_renderEventFunc, m_eventBase + Native.EventGc, IntPtr.Zero);
            Graphics.ExecuteCommandBuffer(m_commandBuffer);
        }

        void DestroyAllViews()
        {
            if (!IsCurrent)
            {
                return; // a leftover: the views, and the runtime, are not its own
            }
            if (m_nativeReady)
            {
                // First: closing the DevTools sessions resumes a script stopped at
                // a breakpoint, which would otherwise hold the runtime thread and
                // with it the disposal of every view. The DLL also outlives Play
                // Mode in the editor, and the next session starts its own endpoint.
                Native.xgu_devtools_stop();
            }
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
            if (!IsCurrent)
            {
                return;
            }
            s_quitting = true;
            DestroyAllViews();
        }

        void OnDestroy()
        {
#if UNITY_EDITOR
            UnityEditor.AssemblyReloadEvents.beforeAssemblyReload -= OnBeforeAssemblyReload;
#endif
            if (!IsCurrent)
            {
                return;
            }
            DestroyAllViews();
            if (m_nativeReady)
            {
                DrainLogs();
                Native.xgu_log_queue_enable(false); // back to the direct IUnityLog sink
            }
            m_commandBuffer?.Release();
            m_commandBuffer = null;
            m_nativeReady = false;
            s_instance = null;
        }

#if UNITY_EDITOR
        // Removes managers left over from earlier Play Mode sessions by versions
        // that created them DontSave, on the way into Play Mode and out of it.
        [UnityEditor.InitializeOnLoadMethod]
        static void WatchPlayMode()
        {
            UnityEditor.EditorApplication.playModeStateChanged -= OnPlayModeStateChanged;
            UnityEditor.EditorApplication.playModeStateChanged += OnPlayModeStateChanged;
        }

        static void OnPlayModeStateChanged(UnityEditor.PlayModeStateChange change)
        {
            if (change != UnityEditor.PlayModeStateChange.ExitingEditMode &&
                change != UnityEditor.PlayModeStateChange.EnteredEditMode)
            {
                return;
            }
            foreach (var leftover in Resources.FindObjectsOfTypeAll<HtmlViewManager>())
            {
                if (leftover != null && leftover != s_instance && !UnityEditor.EditorUtility.IsPersistent(leftover))
                {
                    DestroyImmediate(leftover.gameObject);
                }
            }
        }
#endif
    }
}
