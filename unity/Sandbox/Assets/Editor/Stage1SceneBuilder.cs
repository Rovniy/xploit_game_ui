using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;
using Xploit.GameUI;

namespace Xploit.GameUI.Sandbox.Editor
{
    /// <summary>
    /// Builds Assets/Scenes/Stage1_HelloWorld.unity: a Canvas with a RawImage
    /// driven by an HtmlView (Stage 1 test frame). Runs from the menu or in
    /// batch mode: -executeMethod Xploit.GameUI.Sandbox.Editor.Stage1SceneBuilder.Create
    /// </summary>
    public static class Stage1SceneBuilder
    {
        const string ScenePath = "Assets/Scenes/Stage1_HelloWorld.unity";

        [MenuItem("Xploit/Game UI/Create Stage 1 scene")]
        public static void Create()
        {
            var scene = EditorSceneManager.NewScene(NewSceneSetup.DefaultGameObjects, NewSceneMode.Single);

            var canvasGo = new GameObject("Canvas", typeof(Canvas), typeof(CanvasScaler), typeof(GraphicRaycaster));
            var canvas = canvasGo.GetComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            var scaler = canvasGo.GetComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ConstantPixelSize;

            var viewGo = new GameObject("HtmlView", typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
            viewGo.transform.SetParent(canvasGo.transform, false);
            var rt = viewGo.GetComponent<RectTransform>();
            rt.anchorMin = Vector2.zero;
            rt.anchorMax = Vector2.one;
            rt.offsetMin = new Vector2(40, 40);
            rt.offsetMax = new Vector2(-40, -40);
            var image = viewGo.GetComponent<RawImage>();
            image.color = Color.white;
            image.raycastTarget = true;

            var view = viewGo.AddComponent<HtmlView>();
            view.TargetImage = image;
            view.SizeFromRectTransform = true;
            view.DrawTestFrameOnEnable = true;

            var eventSystemGo = new GameObject("EventSystem", typeof(EventSystem), typeof(StandaloneInputModule));
            eventSystemGo.transform.SetSiblingIndex(canvasGo.transform.GetSiblingIndex() + 1);

            var camera = Camera.main;
            if (camera != null)
            {
                camera.clearFlags = CameraClearFlags.SolidColor;
                camera.backgroundColor = new Color(0.10f, 0.11f, 0.14f);
            }

            Directory.CreateDirectory(Path.GetDirectoryName(ScenePath));
            EditorSceneManager.SaveScene(scene, ScenePath);

            var scenes = new System.Collections.Generic.List<EditorBuildSettingsScene>(EditorBuildSettings.scenes);
            if (!scenes.Exists(s => s.path == ScenePath))
            {
                scenes.Add(new EditorBuildSettingsScene(ScenePath, true));
                EditorBuildSettings.scenes = scenes.ToArray();
            }
            AssetDatabase.SaveAssets();
            Debug.Log($"[xploit_game_ui] created {ScenePath}");
        }
    }
}
