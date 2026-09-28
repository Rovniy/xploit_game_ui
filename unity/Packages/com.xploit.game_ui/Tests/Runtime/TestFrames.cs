using System.Collections;
using UnityEngine;

namespace Xploit.GameUI.Tests
{
    /// <summary>
    /// Waiting for the runtime thread. A number of frames alone is not enough:
    /// in batch mode frames come without vsync, a fraction of a millisecond
    /// apart, and a document load or a bridge round trip can take longer than
    /// a dozen of them. So both a frame count and some real time have to pass.
    /// </summary>
    static class TestFrames
    {
        const float MinimumSeconds = 0.2f;

        public static IEnumerator Settle(int frames)
        {
            var until = Time.realtimeSinceStartup + MinimumSeconds;
            for (int i = 0; i < frames || Time.realtimeSinceStartup < until; i++)
            {
                yield return null;
            }
        }
    }
}
