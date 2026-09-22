using System.Collections;
using System.IO;
using System.Text.RegularExpressions;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace Xploit.GameUI.Tests
{
    /// <summary>
    /// Stage 6 acceptance: input sent from Unity reaches the document as DOM
    /// events, and the screen-to-view mapping that WebInput relies on is right.
    /// The tests call HtmlView.SendInput directly, which is the same entry point
    /// WebInput uses, because PlayMode tests cannot synthesise real device input.
    /// </summary>
    public class Stage6Tests
    {
        const int SettleFrames = 12;
        const string TestDirectory = "UI/__stage6__";

        string m_directory;
        GameObject m_canvas;

        [SetUp]
        public void SetUp()
        {
            m_directory = Path.Combine(Application.streamingAssetsPath, TestDirectory);
            Directory.CreateDirectory(m_directory);
        }

        [TearDown]
        public void TearDown()
        {
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

        HtmlView CreateView(int width = 200, int height = 200)
        {
            m_canvas = new GameObject("Canvas", typeof(Canvas), typeof(GraphicRaycaster));
            m_canvas.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;

            var go = new GameObject("HtmlView", typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
            go.SetActive(false);
            go.transform.SetParent(m_canvas.transform, false);
            go.GetComponent<RectTransform>().sizeDelta = new Vector2(width, height);
            var view = go.AddComponent<HtmlView>();
            view.SizeFromRectTransform = false;
            view.Size = new Vector2Int(width, height);
            view.DrawTestFrameOnEnable = false;
            go.SetActive(true);
            Assert.IsTrue(view.IsCreated, "native view was not created");
            return view;
        }

        static IEnumerator Settle()
        {
            for (int i = 0; i < SettleFrames; i++)
            {
                yield return null;
            }
        }

        static void SendMouse(HtmlView view, WebInputType type, float x, float y)
        {
            var evt = WebInputEvent.Mouse(type, new Vector2(x, y));
            evt.Buttons = type == WebInputType.MouseUp ? WebMouseButtons.None : WebMouseButtons.Left;
            view.SendInput(evt);
        }

        static void SendText(HtmlView view, string text)
        {
            view.SendInput(WebInputEvent.Typed(text));
        }

        [UnityTest]
        public IEnumerator Click_ReachesAListenerInTheDocument()
        {
            Write("index.html",
                "<html><body style=\"margin:0\">" +
                "<button id=\"button\" style=\"position:absolute;left:20px;top:20px;width:100px;height:40px\">CLICK</button>" +
                "<script src=\"./app.js\"></script></body></html>");
            Write("app.js",
                "document.getElementById('button').addEventListener('click', function (e) {" +
                "  console.log('clicked=' + e.target.id);" +
                "});");

            var view = CreateView();
            LogAssert.Expect(LogType.Log, new Regex("clicked=button"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            SendMouse(view, WebInputType.MouseDown, 70f, 40f);
            SendMouse(view, WebInputType.MouseUp, 70f, 40f);
            yield return Settle();
        }

        [UnityTest]
        public IEnumerator Hover_UpdatesTheHoverPseudoClass()
        {
            Write("index.html",
                "<html><body style=\"margin:0\">" +
                "<div id=\"box\" style=\"position:absolute;left:0;top:0;width:100px;height:100px\"></div>" +
                "<script src=\"./app.js\"></script></body></html>");
            Write("app.js",
                "const box = document.getElementById('box');" +
                "box.addEventListener('mouseenter', function () { console.log('hover=' + box.matches(':hover')); });");

            var view = CreateView();
            LogAssert.Expect(LogType.Log, new Regex("hover=true"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            SendMouse(view, WebInputType.MouseMove, 50f, 50f);
            yield return Settle();
        }

        [UnityTest]
        public IEnumerator Typing_UpdatesTheFieldValue()
        {
            Write("index.html",
                "<html><body style=\"margin:0\">" +
                "<input id=\"field\" style=\"position:absolute;left:0;top:0;width:150px;height:30px\">" +
                "<script src=\"./app.js\"></script></body></html>");
            Write("app.js",
                "const field = document.getElementById('field');" +
                "field.addEventListener('input', function () { console.log('typed=' + field.value); });");

            var view = CreateView();
            LogAssert.Expect(LogType.Log, new Regex("typed=hello"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            SendMouse(view, WebInputType.MouseDown, 50f, 15f);
            SendMouse(view, WebInputType.MouseUp, 50f, 15f);
            SendText(view, "hello");
            yield return Settle();
        }

        [UnityTest]
        public IEnumerator SetFocus_MovesFocusFromTheHost()
        {
            Write("index.html",
                "<html><body style=\"margin:0\">" +
                "<input id=\"field\" style=\"position:absolute;left:0;top:0;width:150px;height:30px\">" +
                "<script src=\"./app.js\"></script></body></html>");
            Write("app.js",
                "document.getElementById('field').addEventListener('focus', function () {" +
                "  console.log('focused=' + document.activeElement.id);" +
                "});");

            var view = CreateView();
            LogAssert.Expect(LogType.Log, new Regex("focused=field"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            view.SetFocus("field");
            yield return Settle();
        }

        [UnityTest]
        public IEnumerator ScreenToView_MapsTheRectTransform()
        {
            var view = CreateView(200, 100);
            yield return null;

            var rect = view.transform as RectTransform;
            Assert.IsNotNull(rect);
            var corners = new Vector3[4];
            rect.GetWorldCorners(corners); // bottom-left, top-left, top-right, bottom-right

            // The top-left corner of the rectangle is the origin of the view.
            Assert.IsTrue(view.ScreenToView(corners[1], null, out var topLeft));
            Assert.AreEqual(0f, topLeft.x, 0.5f);
            Assert.AreEqual(0f, topLeft.y, 0.5f);

            var centre = (corners[0] + corners[2]) * 0.5f;
            Assert.IsTrue(view.ScreenToView(centre, null, out var middle));
            Assert.AreEqual(100f, middle.x, 0.5f);
            Assert.AreEqual(50f, middle.y, 0.5f);

            // A point outside the rectangle is reported as outside.
            Assert.IsFalse(view.ScreenToView(corners[1] + new Vector3(-20f, 20f, 0f), null, out _));
        }

        [UnityTest]
        public IEnumerator WebInput_ForwardsPointerEvents()
        {
            Write("index.html",
                "<html><body style=\"margin:0\">" +
                "<div id=\"box\" style=\"position:absolute;left:0;top:0;width:200px;height:200px\"></div>" +
                "<script src=\"./app.js\"></script></body></html>");
            Write("app.js",
                "document.getElementById('box').addEventListener('mousedown', function (e) {" +
                "  console.log('down at ' + Math.round(e.clientX) + ',' + Math.round(e.clientY));" +
                "});");

            var view = CreateView();
            var input = view.gameObject.AddComponent<WebInput>();
            Assert.IsNotNull(input);
            LogAssert.Expect(LogType.Log, new Regex(@"down at 100,100"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            // Drive the handler the EventSystem would call, with the screen point
            // at the centre of the view.
            var rect = view.transform as RectTransform;
            var corners = new Vector3[4];
            rect.GetWorldCorners(corners);
            var centre = (corners[0] + corners[2]) * 0.5f;
            var pointer = new UnityEngine.EventSystems.PointerEventData(UnityEngine.EventSystems.EventSystem.current)
            {
                position = centre,
                button = UnityEngine.EventSystems.PointerEventData.InputButton.Left,
            };
            input.OnPointerDown(pointer);
            yield return Settle();
        }
    }
}
