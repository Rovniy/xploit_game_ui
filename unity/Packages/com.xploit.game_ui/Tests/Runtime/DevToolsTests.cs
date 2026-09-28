using System.Collections;
using System.Threading.Tasks;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace Xploit.GameUI.Tests
{
    /// <summary>
    /// DevTools from Unity: HtmlView.EvaluateAsync (what the JS Console window
    /// runs) and the Chrome DevTools endpoint. The native tests cover the
    /// protocol, breakpoints and the WebSocket server itself.
    /// </summary>
    public class DevToolsTests
    {
        const int SettleFrames = 12;

        GameObject m_canvas;

        [TearDown]
        public void TearDown()
        {
            WebDevTools.Stop();
            if (m_canvas != null)
            {
                Object.Destroy(m_canvas);
            }
        }

        HtmlView CreateView()
        {
            m_canvas = new GameObject("Canvas", typeof(Canvas));
            m_canvas.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;
            var go = new GameObject("devtools-view", typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
            go.SetActive(false);
            go.transform.SetParent(m_canvas.transform, false);
            var view = go.AddComponent<HtmlView>();
            view.SizeFromRectTransform = false;
            view.Size = new Vector2Int(64, 64);
            view.DrawTestFrameOnEnable = false;
            go.SetActive(true);
            Assert.IsTrue(view.IsCreated, "native view was not created");
            view.LoadHtml("<html><body><script>var answer = 42; var point = { x: 1, label: 'a' };</script></body></html>");
            return view;
        }

        static IEnumerator Settle()
        {
            return TestFrames.Settle(SettleFrames);
        }

        static IEnumerator Await(Task<WebEvalResult> task)
        {
            for (int i = 0; i < 300 && !task.IsCompleted; i++)
            {
                yield return null;
            }
            Assert.IsTrue(task.IsCompleted, "no answer from the view");
        }

        [UnityTest]
        public IEnumerator EvaluateAsync_FormatsValuesLikeTheDevToolsConsole()
        {
            var view = CreateView();
            yield return Settle();

            var number = view.EvaluateAsync("answer + 1");
            yield return Await(number);
            Assert.IsTrue(number.Result.Ok);
            Assert.AreEqual("43", number.Result.Text);

            var text = view.EvaluateAsync("'hi'");
            yield return Await(text);
            Assert.AreEqual("'hi'", text.Result.Text);

            var obj = view.EvaluateAsync("point");
            yield return Await(obj);
            Assert.AreEqual("{x: 1, label: 'a'}", obj.Result.Text);

            var array = view.EvaluateAsync("[1, 2, 3]");
            yield return Await(array);
            Assert.AreEqual("[1, 2, 3]", array.Result.Text);

            // replMode: `let` may be declared again, as in the DevTools console.
            var first = view.EvaluateAsync("let n = 1; n");
            yield return Await(first);
            var again = view.EvaluateAsync("let n = 2; n");
            yield return Await(again);
            Assert.AreEqual("2", again.Result.Text);

            var awaited = view.EvaluateAsync("await Promise.resolve(5)");
            yield return Await(awaited);
            Assert.AreEqual("5", awaited.Result.Text);
        }

        [UnityTest]
        public IEnumerator EvaluateAsync_ReportsAThrownError()
        {
            var view = CreateView();
            yield return Settle();

            var failed = view.EvaluateAsync("missingName.value");
            yield return Await(failed);
            Assert.IsFalse(failed.Result.Ok);
            StringAssert.Contains("ReferenceError", failed.Result.Text);
        }

        [UnityTest]
        public IEnumerator Endpoint_StartsGivesAUrlAndStops()
        {
            var view = CreateView();
            yield return Settle();

            WebDevTools.Stop(); // the editor may have started it with Play Mode
            Assert.IsNull(view.DevToolsUrl, "no URL while the endpoint is off");
            Assert.IsTrue(WebDevTools.Start(0), "port 0 picks a free port");
            Assert.IsTrue(WebDevTools.IsRunning);
            var url = view.DevToolsUrl;
            Assert.IsNotNull(url);
            StringAssert.StartsWith("devtools://devtools/bundled/js_app.html?", url);
            StringAssert.Contains($"ws=127.0.0.1:{WebDevTools.Port}/devtools/page/{view.Handle}", url);

            WebDevTools.Stop();
            Assert.IsFalse(WebDevTools.IsRunning);
            Assert.IsNull(view.DevToolsUrl);
        }
    }
}
