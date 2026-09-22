using System.Collections;
using System.IO;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace Xploit.GameUI.Tests
{
    /// <summary>
    /// Stage 5 acceptance: a styled document is painted and the pixels reach a
    /// Unity texture, on the D3D12 provider as well as on the software one.
    /// The assertions look at colour dominance and alpha rather than exact
    /// values, because the readback path goes through a blit on the GPU.
    /// </summary>
    public class Stage5Tests
    {
        const int Width = 128;
        const int Height = 128;
        const string TestDirectory = "UI/__stage5__";

        string m_directory;
        GameObject m_canvas;
        GameObject m_viewObject;

        [SetUp]
        public void SetUp()
        {
            m_directory = Path.Combine(Application.streamingAssetsPath, TestDirectory);
            Directory.CreateDirectory(m_directory);
        }

        [TearDown]
        public void TearDown()
        {
            if (m_viewObject != null)
            {
                Object.Destroy(m_viewObject);
            }
            if (m_canvas != null)
            {
                Object.Destroy(m_canvas);
            }
            if (Directory.Exists(m_directory))
            {
                Directory.Delete(m_directory, true);
            }
            var meta = m_directory + ".meta";
            if (File.Exists(meta))
            {
                File.Delete(meta);
            }
        }

        void Write(string fileName, string contents)
        {
            File.WriteAllText(Path.Combine(m_directory, fileName), contents);
        }

        HtmlView CreateView()
        {
            m_canvas = new GameObject("Canvas", typeof(Canvas));
            m_canvas.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;

            m_viewObject = new GameObject("HtmlView", typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
            m_viewObject.SetActive(false);
            m_viewObject.transform.SetParent(m_canvas.transform, false);
            m_viewObject.GetComponent<RectTransform>().sizeDelta = new Vector2(Width, Height);
            var view = m_viewObject.AddComponent<HtmlView>();
            view.SizeFromRectTransform = false;
            view.Size = new Vector2Int(Width, Height);
            view.DrawTestFrameOnEnable = false;
            m_viewObject.SetActive(true);
            Assert.IsTrue(view.IsCreated, "native view was not created");
            return view;
        }

        /// <summary>
        /// Reads the view texture back. The rows are bottom-up, so
        /// <see cref="PixelAt"/> is what callers should use.
        /// </summary>
        static IEnumerator ReadBack(HtmlView view, int width, int height, System.Action<Color32[]> onDone)
        {
            // The texture resource exists from view creation, so waiting on it alone
            // can read back a frame that predates the document. Interactive means
            // a painted frame has been published.
            Texture texture = null;
            for (int i = 0; i < 60; i++)
            {
                yield return null;
                texture = view.Texture;
                if (texture != null && view.State == ViewState.Interactive)
                {
                    break;
                }
            }
            Assert.IsNotNull(texture, "view texture never appeared");
            Assert.AreEqual(ViewState.Interactive, view.State, "no frame was painted");
            // Let the plugin render event run against the published frame.
            yield return null;
            Assert.AreEqual(width, texture.width);
            Assert.AreEqual(height, texture.height);
            yield return null;

            if (view.WebTexture.IsGpu)
            {
                // BGRA8 external textures cannot be read back directly.
                var readbackRt = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32,
                    RenderTextureReadWrite.Default);
                readbackRt.Create();
                Graphics.Blit(texture, readbackRt);
                if (SystemInfo.supportsAsyncGPUReadback)
                {
                    var request = AsyncGPUReadback.Request(readbackRt, 0, TextureFormat.RGBA32);
                    while (!request.done)
                    {
                        yield return null;
                    }
                    Assert.IsFalse(request.hasError, "AsyncGPUReadback failed");
                    onDone(request.GetData<Color32>().ToArray());
                }
                else
                {
                    var previous = RenderTexture.active;
                    RenderTexture.active = readbackRt;
                    var copy = new Texture2D(width, height, TextureFormat.RGBA32, false);
                    copy.ReadPixels(new Rect(0, 0, width, height), 0, 0);
                    copy.Apply();
                    RenderTexture.active = previous;
                    onDone(copy.GetPixels32());
                    Object.Destroy(copy);
                }
                readbackRt.Release();
                Object.Destroy(readbackRt);
            }
            else
            {
                onDone(((Texture2D)texture).GetPixels32());
            }
        }

        // Top-down CSS coordinates over a bottom-up buffer.
        static Color32 PixelAt(Color32[] pixels, int width, int height, int x, int y)
        {
            return pixels[(height - 1 - y) * width + x];
        }

        [UnityTest]
        public IEnumerator Load_PaintsTheStyledDocumentIntoTheTexture()
        {
            Write("index.html",
                "<html><head><style>" +
                "html,body{margin:0;width:100%;height:100%;background:transparent}" +
                "#box{position:absolute;left:16px;top:16px;width:64px;height:48px;background:#ff2020}" +
                "</style></head><body><div id=\"box\"></div></body></html>");

            var view = CreateView();
            view.Load(TestDirectory + "/index.html");

            Color32[] pixels = null;
            yield return ReadBack(view, Width, Height, result => pixels = result);
            Assert.IsNotNull(pixels);

            var inside = PixelAt(pixels, Width, Height, 48, 40);
            Assert.Greater(inside.a, 200, "the box is opaque");
            Assert.Greater(inside.r, 150, "the box is red");
            Assert.Less(inside.g, 100);
            Assert.Less(inside.b, 100);

            var outside = PixelAt(pixels, Width, Height, 110, 110);
            Assert.Less(outside.a, 16, "the page background stays transparent");
        }

        [UnityTest]
        public IEnumerator JavaScriptStyleChange_RepaintsTheTexture()
        {
            Write("index.html",
                "<html><head><style>" +
                "html,body{margin:0;width:100%;height:100%;background:transparent}" +
                "#box{position:absolute;left:0;top:0;width:128px;height:128px;background:#ff2020}" +
                "</style></head><body><div id=\"box\"></div></body></html>");

            var view = CreateView();
            view.Load(TestDirectory + "/index.html");

            Color32[] pixels = null;
            yield return ReadBack(view, Width, Height, result => pixels = result);
            Assert.Greater(PixelAt(pixels, Width, Height, 64, 64).r, 150, "starts red");

            view.ExecuteJS("document.getElementById('box').style.background = '#2020ff';");
            for (int i = 0; i < 6; i++)
            {
                yield return null;
            }

            pixels = null;
            yield return ReadBack(view, Width, Height, result => pixels = result);
            var repainted = PixelAt(pixels, Width, Height, 64, 64);
            Assert.Greater(repainted.b, 150, "the style change was repainted");
            Assert.Less(repainted.r, 100);
        }

        [UnityTest]
        public IEnumerator Resize_RepaintsAtTheNewSize()
        {
            Write("index.html",
                "<html><head><style>" +
                "html,body{margin:0;width:100%;height:100%;background:transparent}" +
                "#box{width:100%;height:100%;background:#20c020}" +
                "</style></head><body><div id=\"box\"></div></body></html>");

            var view = CreateView();
            view.Load(TestDirectory + "/index.html");

            Color32[] pixels = null;
            yield return ReadBack(view, Width, Height, result => pixels = result);
            Assert.Greater(PixelAt(pixels, Width, Height, 120, 120).a, 200, "the box fills the view");

            const int wideWidth = 200;
            const int wideHeight = 96;
            view.Size = new Vector2Int(wideWidth, wideHeight);
            for (int i = 0; i < 8; i++)
            {
                yield return null;
            }

            pixels = null;
            yield return ReadBack(view, wideWidth, wideHeight, result => pixels = result);
            var farRight = PixelAt(pixels, wideWidth, wideHeight, wideWidth - 4, wideHeight - 4);
            Assert.Greater(farRight.a, 200, "a 100% wide box follows the new viewport");
            Assert.Greater(farRight.g, farRight.r, "and it is still green");
        }
    }
}
