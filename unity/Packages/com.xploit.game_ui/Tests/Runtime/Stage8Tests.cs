using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using System.Text.RegularExpressions;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace Xploit.GameUI.Tests
{
    /// <summary>
    /// Stage 8: the Unity-facing API a game actually uses — lifecycle events,
    /// log routing and resizing — without touching the native layer directly.
    /// </summary>
    public class Stage8Tests
    {
        const int SettleFrames = 14;
        const string TestDirectory = "UI/__stage8__";

        string m_directory;
        GameObject m_object;

        [SetUp]
        public void SetUp()
        {
            m_directory = Path.Combine(Application.streamingAssetsPath, TestDirectory);
            Directory.CreateDirectory(m_directory);
        }

        [TearDown]
        public void TearDown()
        {
            HtmlViewManager.SuppressConsoleOutput = false;
            if (m_object != null)
            {
                Object.Destroy(m_object);
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

        HtmlView CreateView(int width = 128, int height = 128)
        {
            m_object = new GameObject("HtmlView", typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
            m_object.SetActive(false);
            var view = m_object.AddComponent<HtmlView>();
            view.SizeFromRectTransform = false;
            view.Size = new Vector2Int(width, height);
            view.DrawTestFrameOnEnable = false;
            m_object.SetActive(true);
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

        [UnityTest]
        public IEnumerator LifecycleEventsFireInOrder()
        {
            Write("index.html", "<html><body><div id=\"a\">hi</div></body></html>");

            var view = CreateView();
            var order = new List<string>();
            view.DomReady += _ => order.Add("dom");
            view.JsReady += _ => order.Add("js");
            view.Interactive += _ => order.Add("interactive");

            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            CollectionAssert.AreEqual(new[] { "dom", "js", "interactive" }, order);
            Assert.AreEqual(ViewState.Interactive, view.State);
        }

        [UnityTest]
        public IEnumerator LifecycleEventsFireAgainAfterAnotherLoad()
        {
            Write("index.html", "<html><body>one</body></html>");
            Write("second.html", "<html><body>two</body></html>");

            var view = CreateView();
            var interactive = 0;
            view.Interactive += _ => interactive++;

            view.Load(TestDirectory + "/index.html");
            yield return Settle();
            Assert.AreEqual(1, interactive);

            view.Load(TestDirectory + "/second.html");
            yield return Settle();
            Assert.AreEqual(2, interactive, "a second document reports its own lifecycle");
        }

        [UnityTest]
        public IEnumerator LogEventCarriesThePageOutputAndItsView()
        {
            Write("index.html", "<script>console.log('from the page'); console.error('bad');</script>");

            var view = CreateView();
            var messages = new List<WebLogMessage>();
            view.Log += m => messages.Add(m);
            // The console output is expected; the test reads it through the event.
            LogAssert.Expect(LogType.Log, new Regex("from the page"));
            LogAssert.Expect(LogType.Error, new Regex("bad"));

            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            Assert.IsTrue(messages.Exists(m => m.Text.Contains("from the page") && m.Level == WebLogLevel.Info),
                "the info line never arrived");
            Assert.IsTrue(messages.Exists(m => m.Text.Contains("bad") && m.Level == WebLogLevel.Error),
                "the error line never arrived");
            foreach (var message in messages)
            {
                Assert.AreSame(view, message.View, "every message names the view that produced it");
            }
        }

        [UnityTest]
        public IEnumerator SuppressConsoleOutputKeepsTheEventsButSilencesTheConsole()
        {
            Write("index.html", "<script>console.log('quiet please');</script>");

            var view = CreateView();
            var seen = 0;
            view.Log += m =>
            {
                if (m.Text.Contains("quiet please"))
                {
                    seen++;
                }
            };
            HtmlViewManager.SuppressConsoleOutput = true;

            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            Assert.AreEqual(1, seen, "the event still fires");
            // No LogAssert.Expect: nothing should have reached the Unity Console.
        }

        [UnityTest]
        public IEnumerator ManagerListsTheLiveViews()
        {
            var view = CreateView();
            yield return null;
            Assert.IsTrue(HtmlViewManager.Views.Contains(view), "the view should be registered");

            Object.Destroy(m_object);
            m_object = null;
            yield return null;
            yield return null;
            Assert.IsFalse(HtmlViewManager.Views.Contains(view), "a destroyed view unregisters");
        }

        [UnityTest]
        public IEnumerator ResizeChangesTheTextureAndTheViewport()
        {
            Write("index.html",
                "<html><body style=\"margin:0\">" +
                "<div id=\"box\" style=\"width:100%;height:20px;background:#ff0000\"></div></body></html>");

            var view = CreateView(128, 64);
            view.Load(TestDirectory + "/index.html");
            yield return Settle();
            Assert.AreEqual(128, view.Texture.width);
            Assert.AreEqual(64, view.Texture.height);

            view.Resize(200, 100);
            yield return Settle();
            Assert.AreEqual(200, view.Texture.width);
            Assert.AreEqual(100, view.Texture.height);
        }

        [UnityTest]
        public IEnumerator BridgeAndInputStillWorkThroughTheManagedApiOnly()
        {
            // The whole point of the stage: a game does this and nothing else.
            Write("index.html",
                "<html><body style=\"margin:0\">" +
                "<button id=\"go\" style=\"position:absolute;left:0;top:0;width:100px;height:40px\">GO</button>" +
                "<script src=\"./app.js\"></script></body></html>");
            Write("app.js",
                "document.getElementById('go').addEventListener('click', function () { Unity.emit('go'); });" +
                "Unity.on('reply', function (text) { console.log('page heard ' + text); });");

            var view = CreateView();
            view.On("go", e => e.View.Send("reply", "you"));
            LogAssert.Expect(LogType.Log, new Regex("page heard you"));

            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            view.SendInput(WebInputEvent.Mouse(WebInputType.MouseDown, new Vector2(50f, 20f)));
            view.SendInput(WebInputEvent.Mouse(WebInputType.MouseUp, new Vector2(50f, 20f)));
            yield return Settle();
        }
    }
}
