using System;
using UnityEngine;

namespace Xploit.GameUI
{
    /// <summary>What kind of input is being sent to a view.</summary>
    public enum WebInputType
    {
        MouseMove = 0,
        MouseDown = 1,
        MouseUp = 2,
        Wheel = 3,
        /// <summary>The pointer left the view; clears :hover.</summary>
        PointerLeave = 4,
        KeyDown = 5,
        KeyUp = 6,
        /// <summary>Text the host already composed, ready to insert.</summary>
        Text = 7,
        TouchBegin = 8,
        TouchMove = 9,
        TouchEnd = 10,
        /// <summary>The host window lost focus; clears hover, active and focus.</summary>
        WindowBlur = 11,
    }

    /// <summary>Mouse buttons, numbered as MouseEvent.button is in the DOM.</summary>
    public enum WebMouseButton
    {
        None = -1,
        Left = 0,
        Middle = 1,
        Right = 2,
    }

    /// <summary>Buttons currently held, as MouseEvent.buttons reports them.</summary>
    [Flags]
    public enum WebMouseButtons : uint
    {
        None = 0,
        Left = 1 << 0,
        Right = 1 << 1,
        Middle = 1 << 2,
    }

    [Flags]
    public enum WebModifiers : uint
    {
        None = 0,
        Alt = 1 << 0,
        Ctrl = 1 << 1,
        Shift = 1 << 2,
        Meta = 1 << 3,
    }

    /// <summary>
    /// One input event for an <see cref="HtmlView"/>. Positions are CSS pixels
    /// measured from the view's top-left corner; <see cref="HtmlView.ScreenToView"/>
    /// converts a screen point for you.
    /// </summary>
    public struct WebInputEvent
    {
        public WebInputType Type;
        public Vector2 Position;
        /// <summary>Wheel movement in CSS pixels, down-positive as in the DOM.</summary>
        public Vector2 Delta;
        public WebMouseButton Button;
        public WebMouseButtons Buttons;
        public WebModifiers Modifiers;
        /// <summary>Key events: the produced value, such as "a", "Enter" or "ArrowLeft".</summary>
        public string Key;
        /// <summary>Key events: the physical key, such as "KeyA".</summary>
        public string Code;
        /// <summary><see cref="WebInputType.Text"/>: the text to insert.</summary>
        public string Text;
        public int TouchId;
        /// <summary>Seconds; used for double-click detection. Defaults to the current time.</summary>
        public double Time;
        public bool Repeat;

        public static WebInputEvent Mouse(WebInputType type, Vector2 position,
            WebMouseButton button = WebMouseButton.Left)
        {
            return new WebInputEvent { Type = type, Position = position, Button = button };
        }

        public static WebInputEvent Typed(string text)
        {
            return new WebInputEvent { Type = WebInputType.Text, Text = text, Button = WebMouseButton.None };
        }

        public static WebInputEvent Keyboard(WebInputType type, string key, string code = null)
        {
            return new WebInputEvent { Type = type, Key = key, Code = code ?? key, Button = WebMouseButton.None };
        }
    }
}
