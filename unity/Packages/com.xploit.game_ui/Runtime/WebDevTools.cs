using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using System.Threading.Tasks;
using UnityEngine;

namespace Xploit.GameUI
{
    /// <summary>The result of <see cref="HtmlView.EvaluateAsync"/>.</summary>
    public readonly struct WebEvalResult
    {
        public WebEvalResult(bool ok, string text)
        {
            Ok = ok;
            Text = text;
        }

        /// <summary>False when the expression threw; <see cref="Text"/> is then the error.</summary>
        public bool Ok { get; }

        /// <summary>The value, formatted the way the DevTools console shows it.</summary>
        public string Text { get; }

        public override string ToString() => Text;
    }

    /// <summary>
    /// Chrome DevTools for the views. <see cref="Start"/> opens an endpoint on
    /// 127.0.0.1 (port 9222 by default), and every view with a document becomes
    /// a target in chrome://inspect with Console, Sources (breakpoints, stepping),
    /// Profiler and Memory. <see cref="HtmlView.DevToolsUrl"/> opens one view
    /// directly.
    ///
    /// The endpoint only listens on the loopback address and refuses requests
    /// whose Host is anything else. It runs in the editor and in Development
    /// Builds; in a release build <see cref="Start"/> refuses, so it cannot be
    /// left on by accident.
    ///
    /// While a script is stopped at a breakpoint its view does not update; the
    /// game keeps running, because JavaScript has a thread of its own.
    /// </summary>
    public static class WebDevTools
    {
        public const int DefaultPort = 9222;

        static int s_nextRequestId = 1;
        static readonly Dictionary<(ulong View, int Id), TaskCompletionSource<WebEvalResult>> s_pending =
            new Dictionary<(ulong, int), TaskCompletionSource<WebEvalResult>>();

        /// <summary>
        /// Start the endpoint as soon as the runtime initialises. The editor sets
        /// this from Window ▸ Xploit ▸ JS Console; a Development Build can set it
        /// before the first HtmlView is enabled.
        /// </summary>
        public static bool AutoStart { get; set; }

        /// <summary>Port <see cref="AutoStart"/> uses.</summary>
        public static int AutoStartPort { get; set; } = DefaultPort;

        public static bool IsRunning => Port != 0;

        /// <summary>The port in use, or 0 while the endpoint is not running.</summary>
        public static int Port
        {
            get
            {
                var manager = HtmlViewManager.Current;
                return manager != null && manager.IsNativeReady ? Native.xgu_devtools_port() : 0;
            }
        }

        /// <summary>Starts the endpoint. False when the port is taken or the build is not a development one.</summary>
        public static bool Start(int port = DefaultPort)
        {
            if (!Application.isEditor && !Debug.isDebugBuild)
            {
                Debug.LogWarning("[xploit_game_ui] DevTools only run in the editor and in Development Builds");
                return false;
            }
            var manager = HtmlViewManager.Instance;
            if (manager == null || !manager.IsNativeReady)
            {
                return false;
            }
            if (port < 0 || port > ushort.MaxValue)
            {
                throw new ArgumentOutOfRangeException(nameof(port));
            }
            return Native.xgu_devtools_start((ushort)port) == Native.Status.Ok;
        }

        public static void Stop()
        {
            var manager = HtmlViewManager.Current;
            if (manager != null && manager.IsNativeReady)
            {
                Native.xgu_devtools_stop();
            }
        }

        // ---- evaluation through the host session -------------------------------

        internal static Task<WebEvalResult> Evaluate(HtmlView view, string expression)
        {
            var id = s_nextRequestId++;
            var source = new TaskCompletionSource<WebEvalResult>();
            // replMode lets the console redeclare `let` and use top-level await,
            // the way the DevTools console does.
            var request = "{\"id\":" + id.ToString(CultureInfo.InvariantCulture) +
                          ",\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":" + WebJson.Serialize(expression) +
                          ",\"replMode\":true,\"awaitPromise\":true,\"generatePreview\":true,\"includeCommandLineAPI\":true," +
                          "\"objectGroup\":\"xgu-console\"}}";
            var status = Native.xgu_view_devtools_send(view.Handle, request);
            if (status != Native.Status.Ok)
            {
                source.SetResult(new WebEvalResult(false, $"the view cannot evaluate ({status})"));
                return source.Task;
            }
            s_pending[(view.Handle, id)] = source;
            return source.Task;
        }

        /// <summary>Settles the evaluations of a view that goes away.</summary>
        internal static void CancelFor(ulong handle)
        {
            if (s_pending.Count == 0)
            {
                return;
            }
            var gone = new List<(ulong, int)>();
            foreach (var key in s_pending.Keys)
            {
                if (key.View == handle)
                {
                    gone.Add(key);
                }
            }
            foreach (var key in gone)
            {
                var source = s_pending[key];
                s_pending.Remove(key);
                source.TrySetResult(new WebEvalResult(false, "the view was destroyed"));
            }
        }

        /// <summary>
        /// Delivers what the host sessions answered. The manager calls it every
        /// frame; the JS Console window also calls it while the editor is paused.
        /// </summary>
        internal static void Pump()
        {
            var manager = HtmlViewManager.Current;
            if (manager == null || !manager.IsNativeReady)
            {
                return;
            }
            for (int i = 0; i < 256; i++)
            {
                if (!Native.xgu_devtools_poll(out var view, out var messagePtr) || messagePtr == IntPtr.Zero)
                {
                    break;
                }
                var text = System.Runtime.InteropServices.Marshal.PtrToStringUTF8(messagePtr);
                WebValue message;
                try
                {
                    message = WebJson.Parse(text);
                }
                catch (Exception)
                {
                    continue;
                }
                if (!message.Has("id"))
                {
                    continue; // a notification; the host session enables no domain
                }
                var key = (view, (int)message["id"].AsNumber);
                if (!s_pending.TryGetValue(key, out var source))
                {
                    continue;
                }
                s_pending.Remove(key);
                source.TrySetResult(ToResult(message));
            }
        }

        static WebEvalResult ToResult(WebValue message)
        {
            if (message.Has("error"))
            {
                return new WebEvalResult(false, message["error"]["message"].AsString);
            }
            var result = message["result"];
            if (result.Has("exceptionDetails"))
            {
                var details = result["exceptionDetails"];
                var exception = details["exception"];
                var description = exception.Has("description") ? exception["description"].AsString : null;
                return new WebEvalResult(false, !string.IsNullOrEmpty(description)
                    ? description
                    : exception.Has("value") ? Describe(exception) : details["text"].AsString);
            }
            return new WebEvalResult(true, Describe(result["result"]));
        }

        // ---- RemoteObject → text, roughly the way the DevTools console prints it --

        internal static string Describe(WebValue remote)
        {
            var type = remote["type"].AsString;
            var subtype = remote["subtype"].AsString;
            switch (type)
            {
                case "undefined":
                    return "undefined";
                case "string":
                    return Quote(remote["value"].AsString);
                case "number":
                case "boolean":
                case "bigint":
                    return remote.Has("unserializableValue")
                        ? remote["unserializableValue"].AsString
                        : remote.Has("description") ? remote["description"].AsString : Scalar(remote["value"]);
                case "symbol":
                case "function":
                    return remote["description"].AsString;
            }
            if (subtype == "null")
            {
                return "null";
            }
            if (remote.Has("preview") && subtype != "error" && subtype != "node")
            {
                return DescribePreview(remote["preview"], remote["description"].AsString);
            }
            return remote.Has("description") ? remote["description"].AsString : type;
        }

        static string DescribePreview(WebValue preview, string description)
        {
            var isArray = preview["subtype"].AsString == "array";
            var builder = new StringBuilder();
            // Plain objects show no class name; everything else keeps its own
            // ("Map(2)", "HTMLDivElement", "Point").
            if (!isArray && !string.IsNullOrEmpty(description) && description != "Object")
            {
                builder.Append(description).Append(' ');
            }
            builder.Append(isArray ? '[' : '{');
            var first = true;
            foreach (var property in preview["properties"].Items)
            {
                if (!first)
                {
                    builder.Append(", ");
                }
                first = false;
                if (!isArray)
                {
                    builder.Append(property["name"].AsString).Append(": ");
                }
                var value = property["value"].AsString;
                builder.Append(property["type"].AsString == "string" ? Quote(value) : value);
            }
            foreach (var entry in preview["entries"].Items)
            {
                if (!first)
                {
                    builder.Append(", ");
                }
                first = false;
                if (entry.Has("key"))
                {
                    builder.Append(entry["key"]["description"].AsString).Append(" => ");
                }
                builder.Append(entry["value"]["description"].AsString);
            }
            if (preview["overflow"].AsBool)
            {
                builder.Append(first ? "…" : ", …");
            }
            builder.Append(isArray ? ']' : '}');
            return builder.ToString();
        }

        static string Scalar(WebValue value) => value.Type switch
        {
            WebValue.Kind.Bool => value.AsBool ? "true" : "false",
            WebValue.Kind.Number => value.AsNumber.ToString("R", CultureInfo.InvariantCulture),
            WebValue.Kind.String => value.AsString,
            _ => "null",
        };

        static string Quote(string text) => "'" + (text ?? string.Empty).Replace("\\", "\\\\").Replace("'", "\\'") + "'";
    }
}
