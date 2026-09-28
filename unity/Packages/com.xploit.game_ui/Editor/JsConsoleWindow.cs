using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

namespace Xploit.GameUI.Editor
{
    /// <summary>
    /// Window ▸ Xploit ▸ JS Console: the page's console output and a prompt that
    /// evaluates JavaScript in the selected view, the way the DevTools console
    /// does. The toolbar also switches the Chrome DevTools endpoint on and off
    /// and opens DevTools on the view.
    /// </summary>
    public sealed class JsConsoleWindow : EditorWindow
    {
        enum EntryKind
        {
            Log,
            Input,
            Result,
            Error,
        }

        sealed class Entry
        {
            public EntryKind Kind;
            public WebLogLevel Level;
            public string Text;
            public HtmlView View;
            public float Height = -1f;
            public float HeightForWidth;
        }

        const int MaxEntries = 5000;
        const int MaxHistory = 100;
        const string InputControl = "XguJsConsoleInput";

        readonly List<Entry> m_entries = new List<Entry>();
        readonly List<Entry> m_visible = new List<Entry>();
        readonly List<string> m_history = new List<string>();
        int m_historyIndex = -1;
        string m_input = string.Empty;
        string m_search = string.Empty;
        bool m_showLog = true;
        bool m_showWarnings = true;
        bool m_showErrors = true;
        int m_viewIndex;
        Vector2 m_scroll;
        bool m_stickToBottom = true;
        bool m_filterDirty = true;

        GUIStyle m_logStyle;
        GUIStyle m_warningStyle;
        GUIStyle m_errorStyle;
        GUIStyle m_inputStyle;
        GUIStyle m_resultStyle;

        [MenuItem("Window/Xploit/JS Console")]
        public static void ShowWindow()
        {
            var window = GetWindow<JsConsoleWindow>();
            window.titleContent = new GUIContent("JS Console");
            window.Show();
        }

        void OnEnable()
        {
            HtmlViewManager.Log += OnLog;
            EditorApplication.update += OnUpdate;
            EditorApplication.playModeStateChanged += OnPlayModeChanged;
            m_entries.Clear();
            if (Application.isPlaying)
            {
                // Opened in the middle of a session: show what came before.
                foreach (var message in HtmlViewManager.RecentLogs)
                {
                    Add(FromLog(message));
                }
            }
        }

        void OnDisable()
        {
            HtmlViewManager.Log -= OnLog;
            EditorApplication.update -= OnUpdate;
            EditorApplication.playModeStateChanged -= OnPlayModeChanged;
        }

        void OnPlayModeChanged(PlayModeStateChange change)
        {
            if (change == PlayModeStateChange.EnteredPlayMode)
            {
                // A new session: nothing from the one before belongs here.
                m_entries.Clear();
                m_filterDirty = true;
                Repaint();
            }
        }

        void OnUpdate()
        {
            if (!Application.isPlaying)
            {
                return;
            }
            // Answers still arrive while the editor is paused, when the manager's
            // LateUpdate does not run.
            WebDevTools.Pump();
        }

        void OnLog(WebLogMessage message)
        {
            Add(FromLog(message));
            Repaint();
        }

        static Entry FromLog(WebLogMessage message) =>
            new Entry { Kind = EntryKind.Log, Level = message.Level, Text = Strip(message.Text), View = message.View };

        static string Strip(string text)
        {
            const string prefix = "[xploit_game_ui] ";
            return text != null && text.StartsWith(prefix) ? text.Substring(prefix.Length) : text ?? string.Empty;
        }

        void Add(Entry entry)
        {
            m_entries.Add(entry);
            if (m_entries.Count > MaxEntries)
            {
                m_entries.RemoveRange(0, MaxEntries / 10);
            }
            m_filterDirty = true;
        }

        // ---- GUI ---------------------------------------------------------------------

        void EnsureStyles()
        {
            if (m_logStyle != null)
            {
                return;
            }
            m_logStyle = new GUIStyle(EditorStyles.label) { wordWrap = true, richText = false, padding = new RectOffset(6, 6, 2, 2) };
            m_warningStyle = new GUIStyle(m_logStyle) { normal = { textColor = new Color(0.95f, 0.75f, 0.2f) } };
            m_errorStyle = new GUIStyle(m_logStyle) { normal = { textColor = new Color(1f, 0.42f, 0.42f) } };
            m_inputStyle = new GUIStyle(m_logStyle) { normal = { textColor = new Color(0.45f, 0.7f, 1f) } };
            m_resultStyle = new GUIStyle(m_logStyle) { normal = { textColor = EditorGUIUtility.isProSkin ? new Color(0.8f, 0.8f, 0.8f) : new Color(0.25f, 0.25f, 0.25f) } };
        }

        GUIStyle StyleFor(Entry entry)
        {
            switch (entry.Kind)
            {
                case EntryKind.Input: return m_inputStyle;
                case EntryKind.Result: return m_resultStyle;
                case EntryKind.Error: return m_errorStyle;
            }
            return entry.Level == WebLogLevel.Error ? m_errorStyle
                : entry.Level == WebLogLevel.Warning ? m_warningStyle
                : m_logStyle;
        }

        static string Label(Entry entry)
        {
            switch (entry.Kind)
            {
                case EntryKind.Input: return "> " + entry.Text;
                case EntryKind.Result: return "← " + entry.Text;
                case EntryKind.Error: return "✕ " + entry.Text;
            }
            return entry.Text;
        }

        HtmlView SelectedView(IReadOnlyList<HtmlView> views)
        {
            if (views.Count == 0)
            {
                return null;
            }
            m_viewIndex = Mathf.Clamp(m_viewIndex, 0, views.Count - 1);
            return views[m_viewIndex];
        }

        void OnGUI()
        {
            EnsureStyles();
            var views = HtmlViewManager.Views;
            var view = SelectedView(views);
            DrawToolbar(views, view);
            DrawEntries(view);
            DrawPrompt(view);
        }

        void DrawToolbar(IReadOnlyList<HtmlView> views, HtmlView view)
        {
            using (new EditorGUILayout.HorizontalScope(EditorStyles.toolbar))
            {
                if (GUILayout.Button("Clear", EditorStyles.toolbarButton, GUILayout.Width(44)))
                {
                    m_entries.Clear();
                    HtmlViewManager.ClearRecentLogs();
                    m_filterDirty = true;
                }

                var names = new string[views.Count];
                for (int i = 0; i < views.Count; i++)
                {
                    names[i] = views[i] != null ? views[i].name : "(destroyed)";
                }
                using (new EditorGUI.DisabledScope(views.Count == 0))
                {
                    var index = EditorGUILayout.Popup(m_viewIndex, views.Count == 0 ? new[] { "No views" } : names,
                        EditorStyles.toolbarPopup, GUILayout.Width(140));
                    if (index != m_viewIndex)
                    {
                        m_viewIndex = index;
                        m_filterDirty = true;
                    }
                }

                var search = GUILayout.TextField(m_search, EditorStyles.toolbarSearchField, GUILayout.MinWidth(80));
                if (search != m_search)
                {
                    m_search = search;
                    m_filterDirty = true;
                }

                m_filterDirty |= Toggle(ref m_showLog, "Log");
                m_filterDirty |= Toggle(ref m_showWarnings, "Warnings");
                m_filterDirty |= Toggle(ref m_showErrors, "Errors");

                GUILayout.FlexibleSpace();

                var enabled = GUILayout.Toggle(DevToolsEditor.Enabled, new GUIContent("DevTools",
                        $"Chrome DevTools endpoint on 127.0.0.1:{DevToolsEditor.Port}, started with Play Mode."),
                    EditorStyles.toolbarButton, GUILayout.Width(64));
                if (enabled != DevToolsEditor.Enabled)
                {
                    DevToolsEditor.Enabled = enabled;
                }
                var status = !Application.isPlaying ? "starts with Play Mode"
                    : WebDevTools.IsRunning ? $":{WebDevTools.Port}"
                    : DevToolsEditor.Enabled ? "port busy?" : "off";
                GUILayout.Label(status, EditorStyles.miniLabel, GUILayout.ExpandWidth(false));
                using (new EditorGUI.DisabledScope(view == null || !WebDevTools.IsRunning))
                {
                    if (GUILayout.Button("Open DevTools", EditorStyles.toolbarButton, GUILayout.Width(92)))
                    {
                        DevToolsEditor.Open(view);
                    }
                }
            }
        }

        static bool Toggle(ref bool value, string label)
        {
            var next = GUILayout.Toggle(value, label, EditorStyles.toolbarButton, GUILayout.ExpandWidth(false));
            var changed = next != value;
            value = next;
            return changed;
        }

        bool Passes(Entry entry, HtmlView view)
        {
            // Messages of the runtime itself (no view) show for every view.
            if (entry.View != null && view != null && entry.View != view)
            {
                return false;
            }
            if (entry.Kind == EntryKind.Log)
            {
                var shown = entry.Level == WebLogLevel.Error ? m_showErrors
                    : entry.Level == WebLogLevel.Warning ? m_showWarnings
                    : m_showLog;
                if (!shown)
                {
                    return false;
                }
            }
            return string.IsNullOrEmpty(m_search) ||
                   entry.Text.IndexOf(m_search, System.StringComparison.OrdinalIgnoreCase) >= 0;
        }

        void DrawEntries(HtmlView view)
        {
            if (m_filterDirty)
            {
                m_visible.Clear();
                foreach (var entry in m_entries)
                {
                    if (Passes(entry, view))
                    {
                        m_visible.Add(entry);
                    }
                }
                m_filterDirty = false;
            }

            var width = position.width - 16f;
            float total = 0f;
            foreach (var entry in m_visible)
            {
                if (entry.Height < 0f || !Mathf.Approximately(entry.HeightForWidth, width))
                {
                    entry.Height = StyleFor(entry).CalcHeight(new GUIContent(Label(entry)), width);
                    entry.HeightForWidth = width;
                }
                total += entry.Height;
            }

            var area = GUILayoutUtility.GetRect(0f, 100000f, 0f, 100000f);
            if (Event.current.type == EventType.Repaint && m_stickToBottom)
            {
                m_scroll.y = Mathf.Max(0f, total - area.height);
            }
            var previous = m_scroll.y;
            m_scroll = GUI.BeginScrollView(area, m_scroll, new Rect(0f, 0f, width, total));
            float y = 0f;
            foreach (var entry in m_visible)
            {
                // Only what is on screen is drawn.
                if (y + entry.Height >= m_scroll.y && y <= m_scroll.y + area.height)
                {
                    EditorGUI.SelectableLabel(new Rect(0f, y, width, entry.Height), Label(entry), StyleFor(entry));
                }
                y += entry.Height;
            }
            GUI.EndScrollView();
            if (!Mathf.Approximately(previous, m_scroll.y))
            {
                // Scrolling up stops following new lines; back at the end resumes it.
                m_stickToBottom = m_scroll.y >= total - area.height - 4f;
            }
        }

        void DrawPrompt(HtmlView view)
        {
            var enabled = Application.isPlaying && view != null && view.IsCreated;
            var current = Event.current;
            if (current.type == EventType.KeyDown && GUI.GetNameOfFocusedControl() == InputControl)
            {
                // Enter runs the expression; Shift+Enter starts a new line.
                if (current.keyCode == KeyCode.Return || current.keyCode == KeyCode.KeypadEnter)
                {
                    if (!current.shift && enabled)
                    {
                        Submit(view);
                        current.Use();
                    }
                }
                else if (current.keyCode == KeyCode.UpArrow && m_history.Count > 0 && !m_input.Contains("\n"))
                {
                    m_historyIndex = m_historyIndex < 0 ? m_history.Count - 1 : Mathf.Max(0, m_historyIndex - 1);
                    SetInput(m_history[m_historyIndex]);
                    current.Use();
                }
                else if (current.keyCode == KeyCode.DownArrow && m_historyIndex >= 0 && !m_input.Contains("\n"))
                {
                    m_historyIndex++;
                    SetInput(m_historyIndex < m_history.Count ? m_history[m_historyIndex] : string.Empty);
                    if (m_historyIndex >= m_history.Count)
                    {
                        m_historyIndex = -1;
                    }
                    current.Use();
                }
            }

            using (new EditorGUILayout.HorizontalScope())
            {
                GUILayout.Label(">", GUILayout.Width(12));
                using (new EditorGUI.DisabledScope(!enabled))
                {
                    GUI.SetNextControlName(InputControl);
                    m_input = EditorGUILayout.TextArea(m_input, GUILayout.MinHeight(20));
                }
            }
            if (!Application.isPlaying)
            {
                EditorGUILayout.HelpBox("Enter Play Mode to evaluate JavaScript in a view.", MessageType.None);
            }
        }

        // The focused text area edits its own copy of the text, so a change from
        // here has to go into that copy as well.
        void SetInput(string text)
        {
            m_input = text;
            if (GUIUtility.keyboardControl != 0 &&
                GUIUtility.GetStateObject(typeof(TextEditor), GUIUtility.keyboardControl) is TextEditor editor)
            {
                editor.text = text;
                editor.MoveTextEnd();
            }
        }

        async void Submit(HtmlView view)
        {
            var expression = m_input.Trim();
            if (expression.Length == 0)
            {
                return;
            }
            SetInput(string.Empty);
            m_historyIndex = -1;
            m_history.Remove(expression);
            m_history.Add(expression);
            if (m_history.Count > MaxHistory)
            {
                m_history.RemoveAt(0);
            }
            m_stickToBottom = true;
            Add(new Entry { Kind = EntryKind.Input, Text = expression, View = view });
            Repaint();

            var result = await view.EvaluateAsync(expression);
            Add(new Entry { Kind = result.Ok ? EntryKind.Result : EntryKind.Error, Text = result.Text, View = view });
            Repaint();
        }
    }
}
