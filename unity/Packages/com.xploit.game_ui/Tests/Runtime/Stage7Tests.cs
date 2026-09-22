using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace Xploit.GameUI.Tests
{
    /// <summary>
    /// Stage 7 acceptance: the bridge in both directions, including the scenario
    /// from the specification — PLAY reaches C#, and C# pushes health back into
    /// the document.
    /// </summary>
    public class Stage7Tests
    {
        const int SettleFrames = 12;
        const string TestDirectory = "UI/__stage7__";

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
            if (m_object != null)
            {
                UnityEngine.Object.Destroy(m_object);
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
            m_object = new GameObject("HtmlView", typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
            m_object.SetActive(false);
            var view = m_object.AddComponent<HtmlView>();
            view.SizeFromRectTransform = false;
            view.Size = new Vector2Int(128, 128);
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
        public IEnumerator Emit_ReachesAnOnHandler()
        {
            Write("index.html", "<script src=\"./app.js\"></script>");
            Write("app.js", "Unity.emit('play', 42, 'hard', { seed: 7 });");

            var view = CreateView();
            WebEvent received = null;
            view.On("play", e => received = e);
            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            Assert.IsNotNull(received, "the emit never arrived");
            Assert.AreEqual("play", received.Name);
            Assert.AreSame(view, received.View);
            Assert.AreEqual(3, received.Args.Count);
            Assert.AreEqual(42, received.Args.GetInt(0));
            Assert.AreEqual("hard", received.Args.GetString(1));
            Assert.AreEqual(7, received.Args[2]["seed"].Get<int>());
        }

        [UnityTest]
        public IEnumerator Send_ReachesUnityOnInThePage()
        {
            Write("index.html", "<script src=\"./app.js\"></script>");
            Write("app.js",
                "Unity.on('healthChanged', function (value, cause) {" +
                "  console.log('health=' + value + ' cause=' + cause);" +
                "});");

            var view = CreateView();
            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            LogAssert.Expect(LogType.Log, new Regex("health=75 cause=fall"));
            view.Send("healthChanged", 75, "fall");
            yield return Settle();
        }

        [UnityTest]
        public IEnumerator Call_IsAnsweredByARegisteredFunction()
        {
            Write("index.html", "<script src=\"./app.js\"></script>");
            Write("app.js",
                "Unity.call('add', 2, 3).then(function (total) { console.log('total=' + total); });");

            var view = CreateView();
            view.RegisterFunction("add", args => args.GetNumber(0) + args.GetNumber(1));
            LogAssert.Expect(LogType.Log, new Regex("total=5"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();
        }

        [UnityTest]
        public IEnumerator Call_CanReturnAnObject()
        {
            Write("index.html", "<script src=\"./app.js\"></script>");
            Write("app.js",
                "Unity.call('profile').then(function (p) { console.log('who=' + p.name + '/' + p.level); });");

            var view = CreateView();
            view.RegisterFunction("profile", _ => new Dictionary<string, object>
            {
                { "name", "Ada" },
                { "level", 12 },
            });
            LogAssert.Expect(LogType.Log, new Regex("who=Ada/12"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();
        }

        [UnityTest]
        public IEnumerator CallAsync_SettlesWhenTheTaskDoes()
        {
            Write("index.html", "<script src=\"./app.js\"></script>");
            Write("app.js",
                "(async function () { console.log('loaded=' + await Unity.call('loadSave')); })();");

            var view = CreateView();
            view.RegisterFunctionAsync("loadSave", async _ =>
            {
                await Task.Yield();
                return "slot-3";
            });
            LogAssert.Expect(LogType.Log, new Regex("loaded=slot-3"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();
        }

        [UnityTest]
        public IEnumerator Call_RejectsWhenTheHandlerThrows()
        {
            Write("index.html", "<script src=\"./app.js\"></script>");
            Write("app.js",
                "Unity.call('boom').catch(function (e) { console.log('rejected ' + e.name + ': ' + e.message); });");

            var view = CreateView();
            view.RegisterFunction("boom", _ => throw new InvalidOperationException("no save slot"));
            LogAssert.Expect(LogType.Exception, new Regex("InvalidOperationException"));
            LogAssert.Expect(LogType.Log, new Regex("rejected InvalidOperationException: no save slot"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();
        }

        [UnityTest]
        public IEnumerator Call_RejectsWhenNothingIsRegistered()
        {
            Write("index.html", "<script src=\"./app.js\"></script>");
            Write("app.js",
                "Unity.call('missing').catch(function (e) { console.log('rejected: ' + e.message); });");

            var view = CreateView();
            LogAssert.Expect(LogType.Log, new Regex("rejected: no C# function is registered"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();
        }

        [UnityTest]
        public IEnumerator Off_StopsDelivery()
        {
            Write("index.html", "<script src=\"./app.js\"></script>");
            Write("app.js", "globalThis.fire = function () { Unity.emit('ping'); };");

            var view = CreateView();
            var count = 0;
            Action<WebEvent> handler = _ => count++;
            view.On("ping", handler);
            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            view.ExecuteJS("fire();");
            yield return Settle();
            Assert.AreEqual(1, count);

            view.Off("ping", handler);
            view.ExecuteJS("fire();");
            yield return Settle();
            Assert.AreEqual(1, count, "the handler was removed");
        }

        /// <summary>The scenario the specification names: PLAY, then health back.</summary>
        [UnityTest]
        public IEnumerator MvpScenario_PlayThenHealthChanged()
        {
            Write("index.html",
                "<html><body style=\"margin:0\">" +
                "<button id=\"play\">PLAY</button>" +
                "<span id=\"health-value\">100</span>" +
                "<script src=\"./app.js\"></script></body></html>");
            Write("app.js",
                "document.getElementById('play').addEventListener('click', function () { Unity.emit('play'); });" +
                "Unity.on('healthChanged', function (value) {" +
                "  document.getElementById('health-value').textContent = String(value);" +
                "  console.log('dom now shows ' + document.getElementById('health-value').textContent);" +
                "});");

            var view = CreateView();
            var played = false;
            view.On("play", _ =>
            {
                played = true;
                view.Send("healthChanged", 75);
            });
            LogAssert.Expect(LogType.Log, new Regex("dom now shows 75"));
            view.Load(TestDirectory + "/index.html");
            yield return Settle();

            // Click the button the way the host would.
            view.SendInput(WebInputEvent.Mouse(WebInputType.MouseDown, new Vector2(24f, 28f)));
            view.SendInput(WebInputEvent.Mouse(WebInputType.MouseUp, new Vector2(24f, 28f)));
            yield return Settle();

            Assert.IsTrue(played, "the click never reached C#");
        }

        // ---- the JSON layer on its own ---------------------------------------

        [Test]
        public void WebJson_SerialisesTheTypesTheBridgeCarries()
        {
            Assert.AreEqual("null", WebJson.Serialize(null));
            Assert.AreEqual("true", WebJson.Serialize(true));
            Assert.AreEqual("42", WebJson.Serialize(42));
            Assert.AreEqual("\"hi\"", WebJson.Serialize("hi"));
            Assert.AreEqual("\"a\\\"b\\n\"", WebJson.Serialize("a\"b\n"));
            Assert.AreEqual("[1,2]", WebJson.Serialize(new[] { 1, 2 }));
            Assert.AreEqual("{\"k\":1}", WebJson.Serialize(new Dictionary<string, object> { { "k", 1 } }));
            // NaN and infinity have no JSON form and become null, as JavaScript does.
            Assert.AreEqual("null", WebJson.Serialize(float.NaN));
            // Enums cross by name, which is what page code compares against.
            Assert.AreEqual("\"Warning\"", WebJson.Serialize(LogType.Warning));
        }

        [Test]
        public void WebJson_ParsesBackIntoAValueTree()
        {
            var value = WebJson.Parse("{\"a\":[1,\"two\",null,true],\"b\":{\"c\":1.5}}");
            Assert.IsNotNull(value);
            Assert.AreEqual(WebValue.Kind.Object, value.Type);
            Assert.AreEqual(4, value["a"].Count);
            Assert.AreEqual(1, value["a"][0].Get<int>());
            Assert.AreEqual("two", value["a"][1].AsString);
            Assert.IsTrue(value["a"][2].IsNull);
            Assert.IsTrue(value["a"][3].AsBool);
            Assert.AreEqual(1.5, value["b"]["c"].AsNumber, 0.0001);

            // A missing member is Null rather than an exception.
            Assert.IsTrue(value["nope"].IsNull);
            Assert.IsTrue(value["a"][99].IsNull);
        }

        [Test]
        public void WebJson_RejectsInvalidText()
        {
            Assert.IsNull(WebJson.Parse("{"));
            Assert.IsNull(WebJson.Parse("[1,]x"));
            Assert.IsNull(WebJson.Parse("\"unterminated"));
            Assert.IsNull(WebJson.Parse("tru"));
        }

        [Test]
        public void WebJson_HandlesEscapesAndUnicode()
        {
            var value = WebJson.Parse("\"line\\nbreak \\u0416 \\ud83c\\udfae\"");
            Assert.IsNotNull(value);
            Assert.AreEqual("line\nbreak Ж 🎮", value.AsString);
        }
    }
}
