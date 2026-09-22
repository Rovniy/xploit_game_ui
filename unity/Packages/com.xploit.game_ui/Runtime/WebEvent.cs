namespace Xploit.GameUI
{
    /// <summary>An event emitted from JavaScript via Unity.emit(name, payload). Filled in Stage 7.</summary>
    public sealed class WebEvent
    {
        public string Name { get; internal set; }
        public WebArguments Args { get; internal set; }
        public HtmlView View { get; internal set; }

        internal WebEvent(string name, WebArguments args, HtmlView view)
        {
            Name = name;
            Args = args;
            View = view;
        }
    }
}
