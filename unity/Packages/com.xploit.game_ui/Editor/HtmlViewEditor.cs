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
        SerializedProperty m_targetRenderer;
        SerializedProperty m_keepRendererMaterial;
        SerializedProperty m_rendererTextureProperty;

        // The live state is refreshed a few times a second. Repainting the
        // inspector every frame (RequiresConstantRepaint) costs the editor
        // several milliseconds a frame and made the UI look expensive.
        const double RefreshInterval = 0.25;
        double m_nextRefresh;

        void OnEnable()
        {
            m_size = serializedObject.FindProperty("m_size");
            m_sizeFromRectTransform = serializedObject.FindProperty("m_sizeFromRectTransform");
            m_devicePixelRatio = serializedObject.FindProperty("m_devicePixelRatio");
            m_targetImage = serializedObject.FindProperty("m_targetImage");
            m_drawTestFrameOnEnable = serializedObject.FindProperty("m_drawTestFrameOnEnable");
            m_documentPath = serializedObject.FindProperty("m_documentPath");
            m_targetRenderer = serializedObject.FindProperty("m_targetRenderer");
            m_keepRendererMaterial = serializedObject.FindProperty("m_keepRendererMaterial");
            m_rendererTextureProperty = serializedObject.FindProperty("m_rendererTextureProperty");
            EditorApplication.update += RefreshWhilePlaying;
        }

        void OnDisable()
        {
            EditorApplication.update -= RefreshWhilePlaying;
        }

        void RefreshWhilePlaying()
        {
            if (!Application.isPlaying)
            {
                return;
            }
            var now = EditorApplication.timeSinceStartup;
            if (now < m_nextRefresh)
            {
                return;
            }
            m_nextRefresh = now + RefreshInterval;
            Repaint();
        }

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
            EditorGUILayout.PropertyField(m_targetRenderer, new GUIContent("Target Renderer"));
            if (m_targetRenderer.objectReferenceValue != null || ((HtmlView)target).GetComponent<Renderer>() != null)
            {
                using (new EditorGUI.IndentLevelScope())
                {
                    EditorGUILayout.PropertyField(m_keepRendererMaterial, new GUIContent("Keep Material"));
                    using (new EditorGUI.DisabledScope(!m_keepRendererMaterial.boolValue))
                    {
                        EditorGUILayout.PropertyField(m_rendererTextureProperty, new GUIContent("Texture Property"));
                    }
                }
            }
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

            var rawImage = view.TargetImage != null ? view.TargetImage : view.GetComponent<RawImage>();
            var meshRenderer = view.TargetRenderer != null
                ? view.TargetRenderer
                : rawImage == null ? view.GetComponent<Renderer>() : null;
            if (rawImage == null && meshRenderer == null)
            {
                EditorGUILayout.HelpBox(
                    "Nothing shows the view. Add a RawImage or a MeshRenderer to this GameObject, or assign " +
                    "Target Image or Target Renderer.", MessageType.Warning);
            }
            if (meshRenderer != null && rawImage == null)
            {
                DrawMeshWarnings(view, meshRenderer);
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

        void DrawMeshWarnings(HtmlView view, Renderer meshRenderer)
        {
            if (m_sizeFromRectTransform.boolValue)
            {
                EditorGUILayout.HelpBox(
                    "A mesh has no size in pixels: the view uses Size. Match its aspect to the mesh.",
                    MessageType.Info);
            }
            if (view.GetComponent<WebInput>() == null)
            {
                return;
            }
            var collider = view.GetComponent<Collider>();
            if (collider == null)
            {
                EditorGUILayout.HelpBox(
                    "Web Input on a mesh needs a Collider on this GameObject to know where the pointer is.",
                    MessageType.Warning);
            }
            else if (!(collider is MeshCollider meshCollider) || meshCollider.convex)
            {
                EditorGUILayout.HelpBox(
                    "This collider is mapped as a quad in the local XY plane (-0.5..0.5). Use a non-convex " +
                    "MeshCollider to map any other mesh through its UVs.", MessageType.Info);
            }
            else if (meshCollider.sharedMesh != null && !meshCollider.sharedMesh.isReadable)
            {
                EditorGUILayout.HelpBox(
                    "The collider's mesh is not readable, so the hit has no texture coordinate. Turn on " +
                    "Read/Write in the model's import settings.", MessageType.Warning);
            }
            if (meshRenderer.gameObject != view.gameObject)
            {
                EditorGUILayout.HelpBox(
                    "Pointer events reach the GameObject with the collider. Keep the Target Renderer, the " +
                    "Collider and Web Input on the same GameObject as the Html View.", MessageType.Warning);
            }
            if (Object.FindFirstObjectByType<PhysicsRaycaster>() == null)
            {
                EditorGUILayout.HelpBox(
                    "Web Input on a mesh needs a PhysicsRaycaster on the camera, otherwise the EventSystem " +
                    "never sends pointer events to 3D objects.", MessageType.Warning);
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
                using (new EditorGUI.DisabledScope(!view.IsCreated || !WebDevTools.IsRunning))
                {
                    if (GUILayout.Button(new GUIContent("DevTools", WebDevTools.IsRunning
                            ? "Open Chrome DevTools on this view (the URL also goes to the clipboard)."
                            : "Turn DevTools on in Window > Xploit > JS Console.")))
                    {
                        DevToolsEditor.Open(view);
                    }
                }
                if (GUILayout.Button(new GUIContent("JS Console", "Window > Xploit > JS Console")))
                {
                    JsConsoleWindow.ShowWindow();
                }
            }
        }
    }
}
