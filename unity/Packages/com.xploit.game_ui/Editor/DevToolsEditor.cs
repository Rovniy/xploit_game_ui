using System.Diagnostics;
using System.IO;
using UnityEditor;
using UnityEngine;
using Debug = UnityEngine.Debug;

namespace Xploit.GameUI.Editor
{
    /// <summary>
    /// Editor side of <see cref="WebDevTools"/>: whether the endpoint starts with
    /// Play Mode (a per-user preference, on by default), and opening Chrome
    /// DevTools on a view.
    /// </summary>
    [InitializeOnLoad]
    public static class DevToolsEditor
    {
        const string EnabledKey = "Xploit.GameUI.DevTools.Enabled";
        const string PortKey = "Xploit.GameUI.DevTools.Port";

        static DevToolsEditor()
        {
            Apply();
        }

        /// <summary>Start the endpoint when Play Mode starts.</summary>
        public static bool Enabled
        {
            get => EditorPrefs.GetBool(EnabledKey, true);
            set
            {
                EditorPrefs.SetBool(EnabledKey, value);
                Apply();
                if (!Application.isPlaying)
                {
                    return;
                }
                if (value && !WebDevTools.IsRunning)
                {
                    WebDevTools.Start(Port);
                }
                else if (!value && WebDevTools.IsRunning)
                {
                    WebDevTools.Stop();
                }
            }
        }

        public static int Port
        {
            get => EditorPrefs.GetInt(PortKey, WebDevTools.DefaultPort);
            set
            {
                EditorPrefs.SetInt(PortKey, Mathf.Clamp(value, 1, 65535));
                Apply();
            }
        }

        static void Apply()
        {
            WebDevTools.AutoStart = Enabled;
            WebDevTools.AutoStartPort = Port;
        }

        /// <summary>
        /// Opens Chrome (or Edge) DevTools on the view, and puts the URL on the
        /// clipboard for when no browser is found or it refuses the URL.
        /// </summary>
        public static void Open(HtmlView view)
        {
            var url = view != null ? view.DevToolsUrl : null;
            if (string.IsNullOrEmpty(url))
            {
                Debug.LogWarning("[xploit_game_ui] DevTools are not running; turn them on in Window ▸ Xploit ▸ JS Console");
                return;
            }
            EditorGUIUtility.systemCopyBuffer = url;
            var browser = FindBrowser();
            if (browser != null)
            {
                try
                {
                    Process.Start(new ProcessStartInfo(browser, url) { UseShellExecute = false });
                    Debug.Log($"[xploit_game_ui] opening DevTools for \"{view.name}\". If nothing opens, paste the " +
                              "copied URL into the address bar, or use chrome://inspect.");
                    return;
                }
                catch (System.Exception error)
                {
                    Debug.LogWarning($"[xploit_game_ui] cannot start {browser}: {error.Message}");
                }
            }
            Debug.Log($"[xploit_game_ui] DevTools URL copied to the clipboard; paste it into Chrome's address bar: {url}");
        }

        static string FindBrowser()
        {
            var programFiles = System.Environment.GetFolderPath(System.Environment.SpecialFolder.ProgramFiles);
            var programFilesX86 = System.Environment.GetFolderPath(System.Environment.SpecialFolder.ProgramFilesX86);
            var localAppData = System.Environment.GetFolderPath(System.Environment.SpecialFolder.LocalApplicationData);
            string[] candidates =
            {
                Path.Combine(programFiles, "Google", "Chrome", "Application", "chrome.exe"),
                Path.Combine(programFilesX86, "Google", "Chrome", "Application", "chrome.exe"),
                Path.Combine(localAppData, "Google", "Chrome", "Application", "chrome.exe"),
                Path.Combine(programFilesX86, "Microsoft", "Edge", "Application", "msedge.exe"),
                Path.Combine(programFiles, "Microsoft", "Edge", "Application", "msedge.exe"),
            };
            foreach (var candidate in candidates)
            {
                if (File.Exists(candidate))
                {
                    return candidate;
                }
            }
            return null;
        }
    }
}
