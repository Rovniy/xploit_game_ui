using UnityEngine;

namespace Xploit.GameUI
{
    /// <summary>How serious a message from the runtime is.</summary>
    public enum WebLogLevel
    {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3,
    }

    /// <summary>
    /// One line from the runtime: page console output, a CSS or HTML warning, or
    /// an uncaught script error.
    /// </summary>
    public readonly struct WebLogMessage
    {
        public WebLogMessage(WebLogLevel level, string text, HtmlView view)
        {
            Level = level;
            Text = text;
            View = view;
        }

        public WebLogLevel Level { get; }

        /// <summary>The message, already prefixed with "[xploit_game_ui]".</summary>
        public string Text { get; }

        /// <summary>
        /// The view whose document produced it, or null for messages from the
        /// runtime itself (startup, graphics device, bridge limits).
        /// </summary>
        public HtmlView View { get; }

        /// <summary>The Unity log type this maps to.</summary>
        public LogType UnityLogType => Level switch
        {
            WebLogLevel.Error => LogType.Error,
            WebLogLevel.Warning => LogType.Warning,
            _ => LogType.Log,
        };

        public override string ToString() => View != null ? $"{Text} (view \"{View.name}\")" : Text;
    }
}
