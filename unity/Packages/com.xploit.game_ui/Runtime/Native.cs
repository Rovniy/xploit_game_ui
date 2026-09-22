using System;
using System.Runtime.InteropServices;

namespace Xploit.GameUI
{
    /// <summary>
    /// P/Invoke bindings for the native runtime (xploit_game_ui.dll).
    /// Mirrors native/include/xploit_game_ui/xgu.h. All strings are UTF-8.
    /// </summary>
    internal static class Native
    {
        public const string Lib = "xploit_game_ui";

        public enum Status
        {
            Ok = 0,
            InvalidArgument = 1,
            InvalidView = 2,
            NotInitialized = 3,
            Unsupported = 4,
            Internal = 5,
        }

        public enum LogLevel
        {
            Debug = 0,
            Info = 1,
            Warning = 2,
            Error = 3,
        }

        public enum TextureFormat
        {
            BGRA8 = 0,
            RGBA8 = 1,
        }


        public const int EventPaint = 0;
        public const int EventBlit = 1;
        public const int EventGc = 2;
        public const int EventCount = 3;

        [Flags]
        public enum ViewStatus : uint
        {
            None = 0,
            TextureReady = 1 << 0,
            TextureRecreated = 1 << 1,
            DeviceLost = 1 << 2,
            PixelsReady = 1 << 3,
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LogFn(IntPtr user, int level, IntPtr message);

        [Flags]
        public enum InitFlags : uint
        {
            None = 0,
            SingleThreaded = 1 << 0,
        }


        [StructLayout(LayoutKind.Sequential)]
        public struct InitDesc
        {
            public uint struct_size;
            public IntPtr log_fn;
            public IntPtr log_user;
            public IntPtr data_dir;
            public InitFlags flags;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ViewDesc
        {
            public uint struct_size;
            public uint width;
            public uint height;
            public float device_pixel_ratio;
            public TextureFormat format;
            public RenderProvider provider;
            public IntPtr ui_root;
            public IntPtr name;
        }

        public const ulong InvalidView = 0;

        // ---- process -------------------------------------------------------

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr xgu_version();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int xgu_ping();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void xgu_log_test([MarshalAs(UnmanagedType.LPUTF8Str)] string message);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_initialize(ref InitDesc desc);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void xgu_shutdown();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool xgu_is_initialized();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void xgu_set_log_callback(IntPtr fn, IntPtr user);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void xgu_tick(double timeSeconds);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void xgu_log_queue_enable([MarshalAs(UnmanagedType.I1)] bool enabled);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool xgu_log_poll(out int level, out IntPtr message, out ulong view);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint xgu_log_dropped_count();

        // ---- rendering -----------------------------------------------------

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern RenderProvider xgu_render_provider();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int xgu_render_event_base();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr xgu_get_render_event_func();

        // ---- views ---------------------------------------------------------

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong xgu_view_create(ref ViewDesc desc);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_destroy(ulong view);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_resize(ulong view, uint width, uint height, float devicePixelRatio);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr xgu_view_get_native_texture(ulong view, out uint width, out uint height);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool xgu_view_has_pending_frame(ulong view);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern ViewStatus xgu_view_status(ulong view);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool xgu_view_acquire_pixels(ulong view, out IntPtr data, out uint size, out uint width,
            out uint height, out ulong frameId);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void xgu_view_release_pixels(ulong view);

        // ---- documents ------------------------------------------------------

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_load(ulong view, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_load_html(ulong view,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string html,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string basePath);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_reload(ulong view);

        // ---- JavaScript -----------------------------------------------------

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_execute_js(ulong view,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string source,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string origin);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_set_paused(ulong view, [MarshalAs(UnmanagedType.I1)] bool paused);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern ViewState xgu_view_get_state(ulong view);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void xgu_views_destroy_all();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_draw_test_frame(ulong view);

        // ---- input ---------------------------------------------------------

        public enum InputType
        {
            MouseMove = 0,
            MouseDown = 1,
            MouseUp = 2,
            Wheel = 3,
            PointerLeave = 4,
            KeyDown = 5,
            KeyUp = 6,
            Text = 7,
            TouchBegin = 8,
            TouchMove = 9,
            TouchEnd = 10,
            WindowBlur = 11,
        }

        public enum MouseButton
        {
            None = -1,
            Left = 0,
            Middle = 1,
            Right = 2,
        }

        [Flags]
        public enum MouseButtons : uint
        {
            None = 0,
            Left = 1 << 0,
            Right = 1 << 1,
            Middle = 1 << 2,
        }

        [Flags]
        public enum Modifiers : uint
        {
            None = 0,
            Alt = 1 << 0,
            Ctrl = 1 << 1,
            Shift = 1 << 2,
            Meta = 1 << 3,
        }

        /// <summary>
        /// Mirrors xgu_input_event. The string fields are UTF-8 pointers the
        /// caller owns for the duration of the call, so
        /// <see cref="HtmlView"/> pins them around xgu_view_send_input.
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct InputEvent
        {
            public uint StructSize;
            public InputType Type;
            public float X;
            public float Y;
            public float DeltaX;
            public float DeltaY;
            public int Button;
            public uint Buttons;
            public uint Modifiers;
            public IntPtr Key;
            public IntPtr Code;
            public IntPtr Text;
            public int TouchId;
            public double Time;
            [MarshalAs(UnmanagedType.I1)] public bool Repeat;

            public static InputEvent Create(InputType type)
            {
                return new InputEvent
                {
                    StructSize = (uint)Marshal.SizeOf<InputEvent>(),
                    Type = type,
                    Button = (int)MouseButton.None,
                };
            }
        }

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_send_input(ulong view, ref InputEvent evt);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_set_focus(ulong view,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string elementId);

        // ---- bridge --------------------------------------------------------

        public enum MessageKind
        {
            Emit = 0,
            Call = 1,
        }

        /// <summary>
        /// Mirrors xgu_message. The two string pointers belong to the runtime and
        /// stay valid only until the next poll on the same view, so the managed
        /// side copies them straight away.
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct Message
        {
            public uint StructSize;
            public MessageKind Kind;
            public ulong Id;
            public IntPtr Name;
            public IntPtr Json;
        }

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool xgu_view_poll_message(ulong view, ref Message message);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_send_event(ulong view,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string json);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern Status xgu_view_reply(ulong view, ulong id, [MarshalAs(UnmanagedType.I1)] bool ok,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string json);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong xgu_view_bridge_dropped(ulong view);

        // ---- helpers -------------------------------------------------------

        public static string Version()
        {
            var ptr = xgu_version();
            return ptr == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUTF8(ptr);
        }

        /// <summary>Allocates a UTF-8 copy of <paramref name="value"/>; free with <see cref="Marshal.FreeCoTaskMem"/>.</summary>
        public static IntPtr Utf8(string value)
        {
            return string.IsNullOrEmpty(value) ? IntPtr.Zero : Marshal.StringToCoTaskMemUTF8(value);
        }
    }
}
