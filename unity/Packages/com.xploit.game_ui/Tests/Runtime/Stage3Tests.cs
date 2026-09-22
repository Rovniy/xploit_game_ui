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
    /// Stage 3 acceptance: an HTML document is loaded from StreamingAssets, its
    /// scripts run, and JavaScript can read and mutate the DOM.
    /// </summary>
    public class Stage3Tests
    {
        const int SettleFrames = 12;
        const string TestDirectory = "UI/__tests__";

        string m_directory;

        [SetUp]
        public void SetUp()
        {
            m_directory = Path.Combine(Application.streamingAssetsPath, TestDirectory);
            Directory.CreateDirectory(m_directory);
        }

        [TearDown]
        public void TearDown()
        {
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

        static HtmlView CreateView(string name)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
            go.SetActive(false);
            var view = go.AddComponent<HtmlView>();
            view.SizeFromRectTransform = false;
            view.Size = new Vector2Int(128, 128);
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

        [UnityTest]
        public IEnumerator Load_RunsScriptsAndExposesTheDom()
        {
            Write("index.html",
                "<html><head><title>Test Menu</title></head><body>" +
                "<div id=\"greeting\" class=\"box\">Hello</div>" +
                "<script src=\"./app.js\"></script></body></html>");
            Write("app.js",
                "console.log('title=' + document.title);" +
                "console.log('text=' + document.getElementById('greeting').textContent);" +
                "console.log('matches=' + document.querySelector('.box').matches('div#greeting'));");

            var view = CreateView("stage3-load");
            LogAssert.Expect(LogType.Log, new Regex("title=Test Menu"));
            LogAssert.Expect(LogType.Log, new Regex("text=Hello"));
            LogAssert.Expect(LogType.Log, new Regex("matches=true"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            Assert.AreEqual(ViewState.JsReady, view.State);
            Object.Destroy(view.gameObject);
            yield return null;
        }

        [UnityTest]
        public IEnumerator JavaScript_MutatesTheDom()
        {
            Write("mutate.html",
                "<div id=\"list\"></div><script>" +
                "const list = document.getElementById('list');" +
                "for (let i = 0; i < 3; i++) {" +
                "  const item = document.createElement('span');" +
                "  item.className = 'item';" +
                "  item.textContent = 'item' + i;" +
                "  list.appendChild(item);" +
                "}" +
                "console.log('count=' + document.querySelectorAll('.item').length);" +
                "console.log('text=' + list.textContent);" +
                "</script>");

            var view = CreateView("stage3-mutate");
            LogAssert.Expect(LogType.Log, new Regex("count=3"));
            LogAssert.Expect(LogType.Log, new Regex("text=item0item1item2"));
            view.Load(TestDirectory + "/mutate.html");
            yield return Settle();
            Object.Destroy(view.gameObject);
            yield return null;
        }

        [UnityTest]
        public IEnumerator LoadHtml_WorksWithoutAFile()
        {
            var view = CreateView("stage3-memory");
            LogAssert.Expect(LogType.Log, new Regex("memory=works"));
            view.LoadHtml("<b id=\"b\">works</b><script>console.log('memory=' + document.getElementById('b').textContent);</script>");
            yield return Settle();
            Assert.AreEqual(ViewState.JsReady, view.State);
            Object.Destroy(view.gameObject);
            yield return null;
        }

        [UnityTest]
        public IEnumerator Load_RefusesPathsOutsideStreamingAssets()
        {
            var view = CreateView("stage3-escape");
            LogAssert.Expect(LogType.Error, new Regex("cannot read|escapes the UI root|rejected"));
            view.Load("../../secret.html");
            yield return Settle();
            Assert.AreNotEqual(ViewState.JsReady, view.State);
            Object.Destroy(view.gameObject);
            yield return null;
        }

        [UnityTest]
        public IEnumerator Reload_StartsFromACleanIsolate()
        {
            Write("counter.html",
                "<script>globalThis.n = (globalThis.n || 0) + 1; console.log('n=' + globalThis.n);</script>");

            var view = CreateView("stage3-reload");
            LogAssert.Expect(LogType.Log, new Regex("n=1"));
            view.Load(TestDirectory + "/counter.html");
            yield return Settle();

            LogAssert.Expect(LogType.Log, new Regex("n=1"));
            view.Reload();
            yield return Settle();

            Object.Destroy(view.gameObject);
            yield return null;
        }
    }
}
