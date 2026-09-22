using System.Collections;
using System.Text.RegularExpressions;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace Xploit.GameUI.Tests
{
    /// <summary>
    /// Stage 1 acceptance: Unity -> C++ (ping), C++ -> Unity (log), provider
    /// selection, and the Skia test frame arriving in a texture Unity can read.
    /// </summary>
    public class Stage1Tests
    {
        [Test]
        public void Ping_ReturnsFortyTwo()
        {
            Assert.AreEqual(42, Native.xgu_ping());
        }

        [Test]
        public void Version_IsReported()
        {
            Assert.IsNotEmpty(Native.Version());
        }

        [UnityTest]
        public IEnumerator NativeLog_ReachesUnityConsole()
        {
            var manager = HtmlViewManager.Instance;
            Assert.IsTrue(manager.IsNativeReady, "native runtime failed to initialise");
            LogAssert.Expect(LogType.Log, new Regex("native log OK"));
            Native.xgu_log_test("[test] native log OK");
            yield return null;
        }

        [Test]
        public void Provider_MatchesGraphicsDevice()
        {
            Assert.IsTrue(HtmlViewManager.Instance.IsNativeReady);
            var provider = HtmlViewManager.Provider;
            if (SystemInfo.graphicsDeviceType == GraphicsDeviceType.Direct3D12)
            {
                Assert.AreEqual(RenderProvider.D3D12External, provider);
            }
            else
            {
                Assert.AreEqual(RenderProvider.Cpu, provider);
            }
        }

        [UnityTest]
        public IEnumerator TestFrame_PaintsIntoReadableTexture()
        {
            const int width = 256;
            const int height = 192;

            var canvasGo = new GameObject("Canvas", typeof(Canvas));
            canvasGo.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;

            var go = new GameObject("HtmlView", typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
            go.SetActive(false);
            go.transform.SetParent(canvasGo.transform, false);
            var rt = go.GetComponent<RectTransform>();
            rt.sizeDelta = new Vector2(width, height);
            var view = go.AddComponent<HtmlView>();
            view.SizeFromRectTransform = false;
            view.Size = new Vector2Int(width, height);
            view.DrawTestFrameOnEnable = true;
            go.SetActive(true);

            Assert.IsTrue(view.IsCreated, "native view was not created");

            // Give the render thread a few frames to paint and Unity to pick up the texture.
            Texture texture = null;
            for (int i = 0; i < 10 && texture == null; i++)
            {
                yield return null;
                texture = view.Texture;
            }
            Assert.IsNotNull(texture, "view texture never appeared");
            Assert.AreEqual(width, texture.width);
            Assert.AreEqual(height, texture.height);
            yield return null;
            yield return null;

            Color32[] pixels = null;
            if (view.WebTexture.IsGpu)
            {
                // BGRA8 external textures do not support readback directly; blit into an RGBA render texture first.
                var readbackRt = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Default);
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
                    pixels = request.GetData<Color32>().ToArray();
                }
                else
                {
                    var previous = RenderTexture.active;
                    RenderTexture.active = readbackRt;
                    var copy = new Texture2D(width, height, TextureFormat.RGBA32, false);
                    copy.ReadPixels(new Rect(0, 0, width, height), 0, 0);
                    copy.Apply();
                    RenderTexture.active = previous;
                    pixels = copy.GetPixels32();
                    Object.Destroy(copy);
                }
                readbackRt.Release();
                Object.Destroy(readbackRt);
            }
            else
            {
                pixels = ((Texture2D)texture).GetPixels32();
            }
            Assert.AreEqual(width * height, pixels.Length);

            // Bottom-up rows. Corner (1,1) is inside the transparent 8% margin; the centre is the opaque panel.
            Color32 corner = pixels[1 * width + 1];
            Color32 centre = pixels[(height / 2) * width + width / 2];
            Assert.AreEqual(0, corner.a, "margin should be transparent");
            Assert.Greater(centre.a, 200, "panel should be nearly opaque");
            Assert.Greater(centre.b, centre.r, "panel is dark blue");

            Object.Destroy(go);
            Object.Destroy(canvasGo);
            yield return null;
        }

        [UnityTest]
        public IEnumerator CreateDestroy_Loop_DoesNotCrash()
        {
            for (int i = 0; i < 10; i++)
            {
                var go = new GameObject("HtmlView", typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
                go.SetActive(false);
                var view = go.AddComponent<HtmlView>();
                view.SizeFromRectTransform = false;
                view.Size = new Vector2Int(64 + i * 8, 64);
                go.SetActive(true);
                Assert.IsTrue(view.IsCreated);
                yield return null;
                Object.Destroy(go);
                yield return null;
            }
            Assert.AreEqual(0, HtmlViewManager.Instance.ViewCount);
        }
    }
}
