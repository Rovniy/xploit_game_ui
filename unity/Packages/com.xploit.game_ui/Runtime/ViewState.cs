namespace Xploit.GameUI
{
    /// <summary>Lifecycle state of a native view. Mirrors xgu_view_state.</summary>
    public enum ViewState
    {
        /// <summary>The view exists but nothing has been loaded or executed yet.</summary>
        Created = 0,
        /// <summary>A document is being parsed (Stage 3).</summary>
        Loading = 1,
        /// <summary>The DOM is built; scripts have not run yet (Stage 3).</summary>
        DomReady = 2,
        /// <summary>The JavaScript engine is up and scripts have run.</summary>
        JsReady = 3,
        /// <summary>First frame laid out and painted; the view accepts input (Stage 5+).</summary>
        Interactive = 4,
        /// <summary>Ticks and input are suspended.</summary>
        Paused = 5,
        /// <summary>The native view is gone.</summary>
        Destroyed = 6,
    }
}
