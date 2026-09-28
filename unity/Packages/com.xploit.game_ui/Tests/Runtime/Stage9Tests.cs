using System.Collections;
using System.IO;
using System.Text.RegularExpressions;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.TestTools;

namespace Xploit.GameUI.Tests
{
    /// <summary>
    /// Stage 9 acceptance: a view shown on a mesh. The texture reaches the
    /// Renderer, the Renderer gets its own material back when the view goes,
    /// and a ray that hits the mesh lands on the right point of the document.
    /// </summary>
    public class Stage9Tests
    {
        const int SettleFrames = 12;
        const string TestDirectory = "UI/__stage9__";

        string m_directory;
        GameObject m_quad;
        GameObject m_camera;

        [SetUp]
        public void SetUp()
        {
            m_directory = Path.Combine(Application.streamingAssetsPath, TestDirectory);
            Directory.CreateDirectory(m_directory);
        }

        [TearDown]
        public void TearDown()
        {
            if (m_quad != null)
            {
                Object.Destroy(m_quad);
            }
            if (m_camera != null)
            {
                Object.Destroy(m_camera);
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

        // A 2 x 1 quad at the origin facing a camera one unit in front of it, with
        // a 200 x 100 view: one world unit is 100 view pixels.
        HtmlView CreateView(bool boxCollider = false)
        {
            m_quad = GameObject.CreatePrimitive(PrimitiveType.Quad);
            m_quad.name = "HtmlView quad";
            m_quad.SetActive(false);
            m_quad.transform.localScale = new Vector3(2f, 1f, 1f);
            if (boxCollider)
            {
                Object.DestroyImmediate(m_quad.GetComponent<MeshCollider>());
                m_quad.AddComponent<BoxCollider>();
            }
            var view = m_quad.AddComponent<HtmlView>();
            view.SizeFromRectTransform = false;
            view.Size = new Vector2Int(200, 100);
            view.DrawTestFrameOnEnable = true;
            m_quad.SetActive(true);
            Assert.IsTrue(view.IsCreated, "native view was not created");

            m_camera = new GameObject("Camera", typeof(Camera)) { tag = "MainCamera" };
            m_camera.transform.position = new Vector3(0f, 0f, -1f);
            Physics.SyncTransforms();
            return view;
        }

        static IEnumerator Settle()
        {
            return TestFrames.Settle(SettleFrames);
        }

        static Ray RayAt(float x, float y) => new Ray(new Vector3(x, y, -1f), Vector3.forward);

        // What the camera sees, as pixels. Asking the Renderer is no use here:
        // MaterialPropertyBlock.GetTexture returns null for a texture made with
        // Texture2D.CreateExternalTexture, although the block does hold it and
        // the mesh draws with it.
        int CountPixels(System.Func<Color, bool> predicate)
        {
            var camera = m_camera.GetComponent<Camera>();
            var target = new RenderTexture(64, 64, 24);
            camera.targetTexture = target;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            camera.Render();
            var previous = RenderTexture.active;
            RenderTexture.active = target;
            var read = new Texture2D(64, 64, TextureFormat.RGBA32, false);
            read.ReadPixels(new Rect(0, 0, 64, 64), 0, 0);
            read.Apply();
            RenderTexture.active = previous;
            camera.targetTexture = null;
            var count = 0;
            foreach (var pixel in read.GetPixels())
            {
                if (predicate(pixel))
                {
                    count++;
                }
            }
            Object.Destroy(read);
            target.Release();
            Object.Destroy(target);
            return count;
        }

        string Describe()
        {
            var sum = Color.clear;
            var lit = CountPixels(pixel =>
            {
                sum += pixel;
                return pixel.r + pixel.g + pixel.b > 0.05f;
            });
            return $"{lit} of 4096 pixels not black, average {sum / 4096f}";
        }

        // The middle of the test frame is its blue-grey panel: neither the black
        // of the built-in shader without a texture nor the white of an unlit
        // material's default texture.
        static bool IsTestFrame(Color pixel) => pixel.b > pixel.r + 0.08f;

        [UnityTest]
        public IEnumerator Renderer_ShowsTheViewTexture()
        {
            var view = CreateView();
            var meshRenderer = m_quad.GetComponent<MeshRenderer>();
            Assert.AreSame(meshRenderer, view.TargetRenderer, "the Renderer on the GameObject is picked up");
            yield return Settle();

            Assert.IsNotNull(view.Texture, "no frame reached Unity");
            Assert.AreEqual("XploitGameUI/WorldPremultiplied", meshRenderer.sharedMaterial.shader.name);
            Assert.Greater(CountPixels(IsTestFrame), 1000, "the mesh shows the view's frame; " + Describe());
        }

        [UnityTest]
        public IEnumerator Renderer_GetsItsMaterialBackWhenTheViewIsDisabled()
        {
            var view = CreateView();
            var meshRenderer = m_quad.GetComponent<MeshRenderer>();
            yield return Settle();
            Assert.AreEqual("XploitGameUI/WorldPremultiplied", meshRenderer.sharedMaterial.shader.name);

            view.enabled = false;
            Assert.AreNotEqual("XploitGameUI/WorldPremultiplied", meshRenderer.sharedMaterial.shader.name,
                "the primitive's own material is restored");
            var block = new MaterialPropertyBlock();
            meshRenderer.GetPropertyBlock(block);
            Assert.IsTrue(block.isEmpty, "the view texture is taken off the Renderer");
        }

        [UnityTest]
        public IEnumerator KeepMaterial_OnlySetsTheTextureProperty()
        {
            var view = CreateView();
            var meshRenderer = m_quad.GetComponent<MeshRenderer>();
            // A material of our own whose texture is white until the view sets it.
            var own = new Material(Shader.Find("Unlit/Transparent"));
            meshRenderer.sharedMaterial = own;
            view.KeepRendererMaterial = true;
            view.RendererTextureProperty = "_MainTex";
            yield return Settle();

            Assert.AreSame(own, meshRenderer.sharedMaterial, "the material is left alone");
            Assert.Greater(CountPixels(IsTestFrame), 1000, "its texture is the view's frame; " + Describe());
            Object.Destroy(own);
        }

        [UnityTest]
        public IEnumerator RayToView_MapsTheMeshUVs()
        {
            var view = CreateView();
            yield return null;
            AssertMapping(view);
        }

        [UnityTest]
        public IEnumerator RayToView_MapsAnyOtherColliderAsAQuad()
        {
            var view = CreateView(boxCollider: true);
            yield return null;
            AssertMapping(view);
        }

        static void AssertMapping(HtmlView view)
        {
            Assert.IsTrue(view.RayToView(RayAt(0f, 0f), out var centre));
            Assert.AreEqual(100f, centre.x, 0.5f);
            Assert.AreEqual(50f, centre.y, 0.5f);

            // Up and to the left of the centre is towards the view's origin.
            Assert.IsTrue(view.RayToView(RayAt(-0.8f, 0.4f), out var nearTopLeft));
            Assert.AreEqual(20f, nearTopLeft.x, 0.5f);
            Assert.AreEqual(10f, nearTopLeft.y, 0.5f);

            // A miss is reported, and the point stays where the last hit was.
            Assert.IsFalse(view.RayToView(RayAt(3f, 0f), out var missed));
            Assert.AreEqual(nearTopLeft, missed);
        }

        [UnityTest]
        public IEnumerator WebInput_ClickOnTheMeshReachesTheDocument()
        {
            Write("index.html",
                "<html><body style=\"margin:0\">" +
                "<div id=\"box\" style=\"position:absolute;left:0;top:0;width:200px;height:100px\"></div>" +
                "<script src=\"./app.js\"></script></body></html>");
            Write("app.js",
                "document.getElementById('box').addEventListener('mousedown', function (e) {" +
                "  console.log('down at ' + Math.round(e.clientX) + ',' + Math.round(e.clientY));" +
                "});");

            var view = CreateView();
            var input = m_quad.AddComponent<WebInput>();
            LogAssert.Expect(LogType.Log, new Regex(@"down at 150,25"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            // World (0.5, 0.25) is UV (0.75, 0.75): three quarters across, a quarter down.
            var camera = m_camera.GetComponent<Camera>();
            var pointer = new PointerEventData(EventSystem.current)
            {
                position = camera.WorldToScreenPoint(new Vector3(0.5f, 0.25f, 0f)),
                button = PointerEventData.InputButton.Left,
            };
            input.OnPointerDown(pointer);
            yield return Settle();
        }
    }
}
