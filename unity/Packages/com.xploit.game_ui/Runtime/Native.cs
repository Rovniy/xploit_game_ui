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
        public static extern bool xgu_log_poll(out int level, out IntPtr message);

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
