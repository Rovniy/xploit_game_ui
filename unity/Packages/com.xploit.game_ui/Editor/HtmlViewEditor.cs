using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace Xploit.GameUI.Editor
{
    /// <summary>
    /// Inspector for <see cref="HtmlView"/>: the settings, live state while
    /// playing, and the checks that catch the three setups that silently do
    /// nothing (no RawImage, a document outside StreamingAssets, input with no
    /// EventSystem).
    /// </summary>
    [CustomEditor(typeof(HtmlView))]
    [CanEditMultipleObjects]
    public sealed class HtmlViewEditor : UnityEditor.Editor
    {
        SerializedProperty m_size;
        SerializedProperty m_sizeFromRectTransform;
        SerializedProperty m_devicePixelRatio;
        SerializedProperty m_targetImage;
        SerializedProperty m_drawTestFrameOnEnable;
        SerializedProperty m_documentPath;

        void OnEnable()
        {
            m_size = serializedObject.FindProperty("m_size");
            m_sizeFromRectTransform = serializedObject.FindProperty("m_sizeFromRectTransform");
            m_devicePixelRatio = serializedObject.FindProperty("m_devicePixelRatio");
            m_targetImage = serializedObject.FindProperty("m_targetImage");
            m_drawTestFrameOnEnable = serializedObject.FindProperty("m_drawTestFrameOnEnable");
            m_documentPath = serializedObject.FindProperty("m_documentPath");
        }

        public override bool RequiresConstantRepaint() => Application.isPlaying;

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            EditorGUILayout.LabelField("Document", EditorStyles.boldLabel);
            DrawDocumentPath();

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Surface", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(m_sizeFromRectTransform, new GUIContent("Size From Rect Transform"));
            using (new EditorGUI.DisabledScope(m_sizeFromRectTransform.boolValue))
            {
                EditorGUILayout.PropertyField(m_size, new GUIContent("Size (device px)"));
            }
            EditorGUILayout.PropertyField(m_devicePixelRatio, new GUIContent("Device Pixel Ratio"));
            EditorGUILayout.PropertyField(m_targetImage, new GUIContent("Target Image"));
            EditorGUILayout.PropertyField(m_drawTestFrameOnEnable, new GUIContent("Draw Test Frame On Enable"));

            serializedObject.ApplyModifiedProperties();

            DrawWarnings();
            DrawRuntimeState();
        }

        void DrawDocumentPath()
        {
            using (new EditorGUILayout.HorizontalScope())
            {
                EditorGUILayout.PropertyField(m_documentPath, new GUIContent("Path (in StreamingAssets)"));
                if (GUILayout.Button("...", GUILayout.Width(28f)))
                {
                    PickDocument();
                }
            }
        }

        void PickDocument()
        {
            var root = Application.streamingAssetsPath;
            var chosen = EditorUtility.OpenFilePanel("Pick an HTML document", root, "html");
            if (string.IsNullOrEmpty(chosen))
            {
                return;
            }
            var full = Path.GetFullPath(chosen).Replace('\\', '/');
            var rootFull = Path.GetFullPath(root).Replace('\\', '/').TrimEnd('/') + "/";
            if (!full.StartsWith(rootFull, System.StringComparison.OrdinalIgnoreCase))
            {
                EditorUtility.DisplayDialog("Outside StreamingAssets",
                    "The runtime only reads documents under StreamingAssets, so that a page cannot reach the rest " +
                    "of the disk. Move the file there and pick it again.", "OK");
                return;
            }
            m_documentPath.stringValue = full.Substring(rootFull.Length);
        }

        void DrawWarnings()
        {
            var view = (HtmlView)target;

            if (view.TargetImage == null && view.GetComponent<RawImage>() == null)
            {
                EditorGUILayout.HelpBox(
                    "No RawImage to draw into. Add one to this GameObject or assign Target Image, " +
                    "otherwise the view renders but nothing shows it.", MessageType.Warning);
            }

            var path = m_documentPath.stringValue;
            if (!string.IsNullOrEmpty(path) && !Application.isPlaying)
            {
                var full = Path.Combine(Application.streamingAssetsPath, path);
                if (!File.Exists(full))
                {
                    EditorGUILayout.HelpBox($"StreamingAssets/{path} does not exist.", MessageType.Warning);
                }
            }

            if (view.GetComponent<WebInput>() != null && EventSystem.current == null &&
                Object.FindFirstObjectByType<EventSystem>() == null)
            {
                EditorGUILayout.HelpBox(
                    "Web Input needs an EventSystem in the scene to receive pointer events.", MessageType.Warning);
            }

            var graphic = view.TargetImage != null ? view.TargetImage : view.GetComponent<RawImage>();
            if (graphic != null && view.GetComponent<WebInput>() != null && !graphic.raycastTarget)
            {
                EditorGUILayout.HelpBox(
                    "Raycast Target is off on the RawImage, so pointer events never reach the document.",
                    MessageType.Warning);
            }
        }

        void DrawRuntimeState()
        {
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Runtime", EditorStyles.boldLabel);

            if (!Application.isPlaying)
            {
                EditorGUILayout.HelpBox("Enter Play Mode to see the live state.", MessageType.None);
                HtmlView.EnableDebug = EditorGUILayout.Toggle(
                    new GUIContent("Verbose Logging", "Prints provider and view diagnostics to the Console."),
                    HtmlView.EnableDebug);
                return;
            }

            var view = (HtmlView)target;
            using (new EditorGUI.DisabledScope(true))
            {
                EditorGUILayout.EnumPopup("State", view.State);
                EditorGUILayout.EnumPopup("Provider", view.Provider);
                EditorGUILayout.TextField("Loaded", string.IsNullOrEmpty(view.LoadedPath) ? "(in memory)" : view.LoadedPath);
                var texture = view.Texture;
                EditorGUILayout.TextField("Texture", texture != null ? $"{texture.width} x {texture.height}" : "none");
                EditorGUILayout.TextField("Native Handle", view.Handle.ToString());
                if (view.BridgeDroppedCount > 0)
                {
                    EditorGUILayout.TextField("Bridge Dropped", view.BridgeDroppedCount.ToString());
                }
                if (view.TryGetFrameStats(out var style, out var layout, out var paint, out var raster,
                        out var published, out var skipped, out var damage))
                {
                    EditorGUILayout.TextField("Frames", $"{published} drawn, {skipped} skipped");
                    EditorGUILayout.TextField("Last frame",
                        $"style {style:0.00} ms, layout {layout:0.00} ms, record {paint:0.00} ms");
                    EditorGUILayout.TextField("Redrawn", $"{damage.width}x{damage.height} at {damage.x},{damage.y}");
                }
            }

            HtmlView.EnableDebug = EditorGUILayout.Toggle(
                new GUIContent("Verbose Logging", "Prints provider and view diagnostics to the Console."),
                HtmlView.EnableDebug);

            using (new EditorGUILayout.HorizontalScope())
            {
                using (new EditorGUI.DisabledScope(!view.IsCreated))
                {
                    if (GUILayout.Button("Reload"))
                    {
                        view.Reload();
                    }
                    if (GUILayout.Button(view.Paused ? "Resume" : "Pause"))
                    {
                        view.Paused = !view.Paused;
                    }
                }
            }
        }
    }
}
