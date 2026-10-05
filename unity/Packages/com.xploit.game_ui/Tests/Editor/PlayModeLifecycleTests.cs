using System.Collections;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace Xploit.GameUI.EditorTests
{
    /// <summary>
    /// Entering and leaving Play Mode, which PlayMode tests cannot do: the
    /// manager has to go away with Play Mode, and the next session has to work.
    /// Managers used to be created DontSave, survived Play Mode in the editor,
    /// and threw NullReferenceException from IssueGc when a later session quit.
    ///
    /// Needs a real editor: in batch mode the test runner does not enter Play Mode.
    /// </summary>
    public class PlayModeLifecycleTests
    {
        static int Managers() => Resources.FindObjectsOfTypeAll<HtmlViewManager>().Length;

        static void StartAView()
        {
            Assert.IsTrue(Application.isPlaying, "the editor entered Play Mode");
            var canvas = new GameObject("Canvas", typeof(Canvas));
            var go = new GameObject("lifecycle", typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
            go.transform.SetParent(canvas.transform, false);
            var view = go.AddComponent<HtmlView>();
            Assert.IsTrue(view.IsCreated, "the session has a working runtime");
            view.LoadHtml("<html><body><script>var ok = 1;</script></body></html>");
        }

        // The EnterPlayMode and ExitPlayMode instructions only work when the test
        // method itself yields them, so the two sessions are written out.
        [UnityTest]
        public IEnumerator TheManagerGoesAwayWithPlayModeAndTheNextSessionWorks()
        {
            yield return new EnterPlayMode();
            StartAView();
            for (int i = 0; i < 10; i++)
            {
                yield return null;
            }
            Assert.AreEqual(1, Managers(), "one manager while playing");
            yield return new ExitPlayMode();
            Assert.AreEqual(0, Managers(), "nothing left behind after the first session");

            yield return new EnterPlayMode();
            StartAView();
            for (int i = 0; i < 10; i++)
            {
                yield return null;
            }
            Assert.AreEqual(1, Managers(), "still one manager in the second session");
            yield return new ExitPlayMode();
            Assert.AreEqual(0, Managers(), "nothing left behind after the second session");
            // Any exception logged on the way (IssueGc and the like) fails the test.
        }
    }
}
