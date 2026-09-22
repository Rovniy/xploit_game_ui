using System.Collections;
using System.Text.RegularExpressions;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace Xploit.GameUI.Tests
{
    /// <summary>
    /// Stage 2 acceptance: V8 runs inside the view, console.* reaches the Unity
    /// Console, uncaught errors are reported, and views have isolated globals.
    /// </summary>
    public class Stage2Tests
    {
        static HtmlView CreateView(string name)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
            go.SetActive(false);
            var view = go.AddComponent<HtmlView>();
            view.SizeFromRectTransform = false;
            view.Size = new Vector2Int(64, 64);
            view.DrawTestFrameOnEnable = false;
            go.SetActive(true);
            Assert.IsTrue(view.IsCreated, "native view was not created");
            return view;
        }

        // Frames the runtime needs to pick up posted scripts and drain their logs.
        const int SettleFrames = 10;

        [UnityTest]
        public IEnumerator ExecuteJS_ConsoleLog_ReachesUnityConsole()
        {
            var view = CreateView("js-log");
            LogAssert.Expect(LogType.Log, new Regex("Hello from V8 42 \\{\"ok\":true\\}"));
            view.ExecuteJS("console.log('Hello from V8', 6 * 7, { ok: true })");
            for (int i = 0; i < SettleFrames; i++)
            {
                yield return null;
            }
            Assert.AreEqual(ViewState.JsReady, view.State);
            Object.Destroy(view.gameObject);
            yield return null;
        }

        [UnityTest]
        public IEnumerator ExecuteJS_ConsoleWarnAndError_MapToUnityLogTypes()
        {
            var view = CreateView("js-levels");
            LogAssert.Expect(LogType.Warning, new Regex("careful now"));
            LogAssert.Expect(LogType.Error, new Regex("this is bad"));
            view.ExecuteJS("console.warn('careful now'); console.error('this is bad');");
            for (int i = 0; i < SettleFrames; i++)
            {
                yield return null;
            }
            Object.Destroy(view.gameObject);
            yield return null;
        }

        [UnityTest]
        public IEnumerator ExecuteJS_UncaughtError_IsReportedWithStack()
        {
            var view = CreateView("js-error");
            LogAssert.Expect(LogType.Error, new Regex("Uncaught Error: boom[\\s\\S]*at explode[\\s\\S]*app\\.js:1"));
            view.ExecuteJS("function explode() { throw new Error('boom'); }\nexplode();", "app.js");
            for (int i = 0; i < SettleFrames; i++)
            {
                yield return null;
            }
            Object.Destroy(view.gameObject);
            yield return null;
        }

        [UnityTest]
        public IEnumerator Views_HaveIsolatedGlobals()
        {
            var first = CreateView("js-first");
            var second = CreateView("js-second");
            first.ExecuteJS("globalThis.marker = 'first';");
            LogAssert.Expect(LogType.Log, new Regex("second sees undefined"));
            second.ExecuteJS("console.log('second sees ' + typeof marker)");
            LogAssert.Expect(LogType.Log, new Regex("first sees first"));
            first.ExecuteJS("console.log('first sees ' + marker)");
            for (int i = 0; i < SettleFrames; i++)
            {
                yield return null;
            }
            Object.Destroy(first.gameObject);
            Object.Destroy(second.gameObject);
            yield return null;
        }

        [UnityTest]
        public IEnumerator Microtasks_RunEveryFrame()
        {
            var view = CreateView("js-micro");
            LogAssert.Expect(LogType.Log, new Regex("microtask done"));
            view.ExecuteJS("Promise.resolve().then(() => console.log('microtask done'))");
            for (int i = 0; i < SettleFrames; i++)
            {
                yield return null;
            }
            Object.Destroy(view.gameObject);
            yield return null;
        }

        [UnityTest]
        public IEnumerator Destroying_A_View_With_Isolate_DoesNotCrash()
        {
            for (int i = 0; i < 5; i++)
            {
                var view = CreateView("js-cycle");
                view.ExecuteJS("var big = new Array(1000).fill('x'.repeat(100));");
                yield return null;
                Object.Destroy(view.gameObject);
                yield return null;
            }
            Assert.AreEqual(0, HtmlViewManager.Instance.ViewCount);
        }
    }
}
