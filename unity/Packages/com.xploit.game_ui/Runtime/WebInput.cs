using System.Collections.Generic;
using UnityEngine;
using UnityEngine.EventSystems;

namespace Xploit.GameUI
{
    /// <summary>
    /// Feeds Unity input into an <see cref="HtmlView"/>.
    ///
    /// Pointer input arrives through the uGUI event interfaces, so it already
    /// respects the Canvas, the RectTransform and any scaling; that is also what
    /// makes a view on a world-space canvas clickable. Keyboard input is polled
    /// in Update from the legacy Input Manager.
    ///
    /// The GameObject needs a Graphic with Raycast Target on (the RawImage the
    /// view draws into), and the scene needs an EventSystem.
    /// </summary>
    [AddComponentMenu("Xploit/Game UI/Web Input")]
    [RequireComponent(typeof(HtmlView))]
    [DisallowMultipleComponent]
    public sealed class WebInput : MonoBehaviour,
        IPointerMoveHandler,
        IPointerDownHandler,
        IPointerUpHandler,
        IPointerExitHandler,
        IDragHandler,
        IScrollHandler
    {
        [Tooltip("Forward keyboard input to the document while this component is enabled.")]
        [SerializeField] bool m_captureKeyboard = true;

        [Tooltip("Wheel notches to CSS pixels. Browsers scroll about 100 px per notch.")]
        [SerializeField] float m_scrollScale = 100f;

        HtmlView m_view;
        readonly List<KeyCode> m_heldKeys = new List<KeyCode>();

        /// <summary>Keys that carry no printable character and have to be polled.</summary>
        static readonly (KeyCode Code, string Key, string Physical)[] NavigationKeys =
        {
            (KeyCode.Backspace, "Backspace", "Backspace"),
            (KeyCode.Delete, "Delete", "Delete"),
            (KeyCode.LeftArrow, "ArrowLeft", "ArrowLeft"),
            (KeyCode.RightArrow, "ArrowRight", "ArrowRight"),
            (KeyCode.UpArrow, "ArrowUp", "ArrowUp"),
            (KeyCode.DownArrow, "ArrowDown", "ArrowDown"),
            (KeyCode.Home, "Home", "Home"),
            (KeyCode.End, "End", "End"),
            (KeyCode.PageUp, "PageUp", "PageUp"),
            (KeyCode.PageDown, "PageDown", "PageDown"),
            (KeyCode.Return, "Enter", "Enter"),
            (KeyCode.KeypadEnter, "Enter", "NumpadEnter"),
            (KeyCode.Tab, "Tab", "Tab"),
            (KeyCode.Escape, "Escape", "Escape"),
            (KeyCode.Space, " ", "Space"),
        };

        public bool CaptureKeyboard
        {
            get => m_captureKeyboard;
            set => m_captureKeyboard = value;
        }

        public float ScrollScale
        {
            get => m_scrollScale;
            set => m_scrollScale = value;
        }

        void Awake()
        {
            m_view = GetComponent<HtmlView>();
        }

        void OnDisable()
        {
            // Release whatever the document thinks is held.
            SendSimple(WebInputType.WindowBlur);
            m_heldKeys.Clear();
        }

        void OnApplicationFocus(bool focused)
        {
            if (!focused)
            {
                SendSimple(WebInputType.WindowBlur);
                m_heldKeys.Clear();
            }
        }

        // ---- pointer -------------------------------------------------------

        public void OnPointerMove(PointerEventData eventData) => SendPointer(WebInputType.MouseMove, eventData);

        public void OnDrag(PointerEventData eventData) => SendPointer(WebInputType.MouseMove, eventData);

        public void OnPointerDown(PointerEventData eventData) => SendPointer(WebInputType.MouseDown, eventData);

        public void OnPointerUp(PointerEventData eventData) => SendPointer(WebInputType.MouseUp, eventData);

        public void OnPointerExit(PointerEventData eventData) => SendSimple(WebInputType.PointerLeave);

        public void OnScroll(PointerEventData eventData)
        {
            if (m_view == null || !m_view.IsCreated)
            {
                return;
            }
            if (!m_view.ScreenToView(eventData.position, eventData.pressEventCamera, out var point))
            {
                return;
            }
            var evt = new WebInputEvent { Type = WebInputType.Wheel, Button = WebMouseButton.None };
            evt.Position = point;
            // Unity scrolls up-positive; the DOM scrolls down-positive.
            evt.Delta = new Vector2(eventData.scrollDelta.x, -eventData.scrollDelta.y) * m_scrollScale;
            evt.Modifiers = CurrentModifiers();
            evt.Time = Time.realtimeSinceStartupAsDouble;
            m_view.SendInput(evt);
        }

        void SendPointer(WebInputType type, PointerEventData eventData)
        {
            if (m_view == null || !m_view.IsCreated)
            {
                return;
            }
            var camera = eventData.pressEventCamera != null ? eventData.pressEventCamera : eventData.enterEventCamera;
            if (!m_view.ScreenToView(eventData.position, camera, out var point) && type != WebInputType.MouseMove)
            {
                return;
            }
            var evt = WebInputEvent.Mouse(type, point, ToButton(eventData.button));
            evt.Buttons = HeldButtons();
            evt.Modifiers = CurrentModifiers();
            evt.Time = Time.realtimeSinceStartupAsDouble;
            m_view.SendInput(evt);
        }

        void SendSimple(WebInputType type)
        {
            if (m_view == null || !m_view.IsCreated)
            {
                return;
            }
            var evt = new WebInputEvent { Type = type, Button = WebMouseButton.None };
            evt.Time = Time.realtimeSinceStartupAsDouble;
            m_view.SendInput(evt);
        }

        static WebMouseButton ToButton(PointerEventData.InputButton button)
        {
            switch (button)
            {
                case PointerEventData.InputButton.Right: return WebMouseButton.Right;
                case PointerEventData.InputButton.Middle: return WebMouseButton.Middle;
                default: return WebMouseButton.Left;
            }
        }

        static WebMouseButtons HeldButtons()
        {
            var buttons = WebMouseButtons.None;
            if (Input.GetMouseButton(0)) buttons |= WebMouseButtons.Left;
            if (Input.GetMouseButton(1)) buttons |= WebMouseButtons.Right;
            if (Input.GetMouseButton(2)) buttons |= WebMouseButtons.Middle;
            return buttons;
        }

        static WebModifiers CurrentModifiers()
        {
            var modifiers = WebModifiers.None;
            if (Input.GetKey(KeyCode.LeftAlt) || Input.GetKey(KeyCode.RightAlt)) modifiers |= WebModifiers.Alt;
            if (Input.GetKey(KeyCode.LeftControl) || Input.GetKey(KeyCode.RightControl)) modifiers |= WebModifiers.Ctrl;
            if (Input.GetKey(KeyCode.LeftShift) || Input.GetKey(KeyCode.RightShift)) modifiers |= WebModifiers.Shift;
            if (Input.GetKey(KeyCode.LeftCommand) || Input.GetKey(KeyCode.RightCommand)) modifiers |= WebModifiers.Meta;
            return modifiers;
        }

        // ---- keyboard ------------------------------------------------------

        void Update()
        {
            if (!m_captureKeyboard || m_view == null || !m_view.IsCreated)
            {
                return;
            }
            SendNavigationKeys();
            SendTypedText();
        }

        void SendNavigationKeys()
        {
            var modifiers = CurrentModifiers();
            var now = Time.realtimeSinceStartupAsDouble;
            foreach (var entry in NavigationKeys)
            {
                if (Input.GetKeyDown(entry.Code))
                {
                    SendKey(WebInputType.KeyDown, entry.Key, entry.Physical, modifiers, now, false);
                    m_heldKeys.Add(entry.Code);
                }
                else if (Input.GetKeyUp(entry.Code))
                {
                    SendKey(WebInputType.KeyUp, entry.Key, entry.Physical, modifiers, now, false);
                    m_heldKeys.Remove(entry.Code);
                }
            }

            // Ctrl shortcuts the document may want (select all, copy, ...) carry
            // no printable character while Ctrl is down.
            if ((modifiers & WebModifiers.Ctrl) == 0)
            {
                return;
            }
            for (var code = KeyCode.A; code <= KeyCode.Z; code++)
            {
                if (Input.GetKeyDown(code))
                {
                    var letter = ((char)('a' + (code - KeyCode.A))).ToString();
                    SendKey(WebInputType.KeyDown, letter, "Key" + letter.ToUpperInvariant(), modifiers, now, false);
                }
            }
        }

        void SendTypedText()
        {
            var typed = Input.inputString;
            if (string.IsNullOrEmpty(typed))
            {
                return;
            }
            // Backspace and Return already went out as key events above.
            var text = typed.Replace("\b", string.Empty).Replace("\n", string.Empty).Replace("\r", string.Empty);
            if (text.Length == 0)
            {
                return;
            }
            var evt = WebInputEvent.Typed(text);
            evt.Modifiers = CurrentModifiers();
            evt.Time = Time.realtimeSinceStartupAsDouble;
            m_view.SendInput(evt);
        }

        void SendKey(WebInputType type, string key, string code, WebModifiers modifiers, double time, bool repeat)
        {
            var evt = WebInputEvent.Keyboard(type, key, code);
            evt.Modifiers = modifiers;
            evt.Time = time;
            evt.Repeat = repeat;
            m_view.SendInput(evt);
        }
    }
}
