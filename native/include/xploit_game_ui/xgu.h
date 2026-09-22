// xploit_game_ui — public C ABI (v0, Stage 1).
//
// All strings are UTF-8. Unless stated otherwise every function must be called
// from the host application's main thread (the Unity main thread). Functions
// marked [submission thread] are invoked by the graphics backend.
//
// View handles are 64-bit ids composed of {slot index, generation}. A stale id
// (destroyed view) is rejected by every function; it never dereferences freed
// memory.

#ifndef XPLOIT_GAME_UI_XGU_H
#define XPLOIT_GAME_UI_XGU_H

#include <stdbool.h>
#include <stdint.h>

#if defined(XGU_STATIC)
#define XGU_API
#elif defined(_WIN32)
#if defined(XGU_BUILDING_DLL)
#define XGU_API __declspec(dllexport)
#else
#define XGU_API __declspec(dllimport)
#endif
#else
#define XGU_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t xgu_view_id;
#define XGU_INVALID_VIEW ((xgu_view_id)0)

typedef enum xgu_status {
    XGU_OK = 0,
    XGU_ERR_INVALID_ARGUMENT = 1,
    XGU_ERR_INVALID_VIEW = 2,
    XGU_ERR_NOT_INITIALIZED = 3,
    XGU_ERR_UNSUPPORTED = 4,
    XGU_ERR_INTERNAL = 5
} xgu_status;

typedef enum xgu_log_level {
    XGU_LOG_DEBUG = 0,
    XGU_LOG_INFO = 1,
    XGU_LOG_WARNING = 2,
    XGU_LOG_ERROR = 3
} xgu_log_level;

typedef void (*xgu_log_fn)(void* user, int level, const char* message);

typedef enum xgu_texture_format {
    XGU_FORMAT_BGRA8 = 0, /* D3D12 provider default; matches TextureFormat.BGRA32 */
    XGU_FORMAT_RGBA8 = 1  /* CPU provider default; matches TextureFormat.RGBA32 */
} xgu_texture_format;

typedef enum xgu_provider {
    XGU_PROVIDER_AUTO = 0,           /* D3D12 external if Unity runs D3D12, otherwise CPU */
    XGU_PROVIDER_D3D12_EXTERNAL = 1, /* zero-copy: plugin-owned ID3D12Resource, CreateExternalTexture */
    XGU_PROVIDER_D3D12_COPY = 2,     /* one GPU copy into a Unity-owned Texture2D (Stage 5+) */
    XGU_PROVIDER_CPU = 3,            /* Skia raster + LoadRawTextureData */
    XGU_PROVIDER_NONE = 4            /* no graphics device available */
} xgu_provider;

/* Offsets from xgu_render_event_base() for GL.IssuePluginEvent / CommandBuffer.IssuePluginEventAndData. */
typedef enum xgu_render_event {
    XGU_EVT_PAINT = 0,
    XGU_EVT_BLIT = 1,
    XGU_EVT_GC = 2,
    XGU_EVT_COUNT = 3
} xgu_render_event;

/* Bit flags returned by xgu_view_status(). */
enum xgu_view_status_flags {
    XGU_ST_TEXTURE_READY = 1 << 0,     /* native texture holds at least one painted frame */
    XGU_ST_TEXTURE_RECREATED = 1 << 1, /* native texture pointer changed (resize); cleared on read */
    XGU_ST_DEVICE_LOST = 1 << 2,       /* graphics device lost; recreate the view */
    XGU_ST_PIXELS_READY = 1 << 3       /* CPU provider: a new pixel buffer is available */
};

typedef enum xgu_init_flags {
    XGU_INIT_SINGLE_THREADED = 1 << 0 /* run the runtime inline on the calling thread (tests, tools) */
} xgu_init_flags;

typedef struct xgu_init_desc {
    uint32_t struct_size; /* sizeof(xgu_init_desc) */
    xgu_log_fn log_fn;    /* optional; may be NULL */
    void* log_user;
    const char* data_dir; /* optional; directory searched first for icudtl.dat */
    uint32_t flags;       /* xgu_init_flags */
} xgu_init_desc;

/* Lifecycle state of a view (see docs/threading.md). */
typedef enum xgu_view_state {
    XGU_STATE_CREATED = 0,
    XGU_STATE_LOADING = 1,
    XGU_STATE_DOM_READY = 2,
    XGU_STATE_JS_READY = 3,
    XGU_STATE_INTERACTIVE = 4,
    XGU_STATE_PAUSED = 5,
    XGU_STATE_DESTROYED = 6
} xgu_view_state;

typedef struct xgu_view_desc {
    uint32_t struct_size; /* sizeof(xgu_view_desc) */
    uint32_t width;       /* device pixels */
    uint32_t height;      /* device pixels */
    float device_pixel_ratio;
    xgu_texture_format format;
    xgu_provider provider;
    const char* ui_root; /* absolute directory that confines asset access; may be NULL until Stage 3 */
    const char* name;    /* optional debug name */
} xgu_view_desc;

typedef void (*xgu_render_event_fn)(int event_id, void* data);

/* -------------------------------------------------------------------------- */
/* Process                                                                     */
/* -------------------------------------------------------------------------- */

XGU_API const char* xgu_version(void);

/* Round-trip smoke test used by the Unity Hello World stage. Always returns 42. */
XGU_API int xgu_ping(void);

/* Writes a message through the active log sink (IUnityLog in Unity). Any thread. */
XGU_API void xgu_log_test(const char* message);

XGU_API xgu_status xgu_initialize(const xgu_init_desc* desc);
XGU_API void xgu_shutdown(void);
XGU_API bool xgu_is_initialized(void);

/* Any thread. Replaces the log callback given in xgu_initialize. */
XGU_API void xgu_set_log_callback(xgu_log_fn fn, void* user);

/* Advances every view by one frame on the runtime thread (JS message loop and
   microtasks; timers, requestAnimationFrame and layout in later stages).
   Call once per host frame with a monotonic time in seconds. */
XGU_API void xgu_tick(double time_seconds);

/* Log queueing. With the queue enabled, log messages (including console.* from
   JavaScript, which runs on the runtime thread) are buffered instead of being
   passed to the log callback, so the host can report them on its main thread.
   Disabling flushes whatever is still queued through the callback. */
XGU_API void xgu_log_queue_enable(bool enabled);

/* Pops the oldest queued message. Returns false when the queue is empty.
   `out_message` stays valid until the next xgu_log_poll call on this thread. */
XGU_API bool xgu_log_poll(int* out_level, const char** out_message);

/* Number of messages dropped because the queue overflowed; resets the counter. */
XGU_API uint32_t xgu_log_dropped_count(void);

/* -------------------------------------------------------------------------- */
/* Rendering                                                                   */
/* -------------------------------------------------------------------------- */

/* The provider chosen for this process (after the graphics device initialised). */
XGU_API xgu_provider xgu_render_provider(void);

/* Base event id reserved from IUnityGraphics; add xgu_render_event offsets. -1 outside Unity. */
XGU_API int xgu_render_event_base(void);

/* Callback for CommandBuffer.IssuePluginEventAndData; pass the view id as `data`. */
XGU_API xgu_render_event_fn xgu_get_render_event_func(void);

/* -------------------------------------------------------------------------- */
/* Views                                                                       */
/* -------------------------------------------------------------------------- */

XGU_API xgu_view_id xgu_view_create(const xgu_view_desc* desc);
XGU_API xgu_status xgu_view_destroy(xgu_view_id view);
XGU_API xgu_status xgu_view_resize(xgu_view_id view, uint32_t width, uint32_t height, float device_pixel_ratio);

/* D3D12 providers: ID3D12Resource* for Texture2D.CreateExternalTexture. NULL for the CPU provider. */
XGU_API void* xgu_view_get_native_texture(xgu_view_id view, uint32_t* out_width, uint32_t* out_height);

/* True when a recorded frame is waiting to be painted; issue XGU_EVT_PAINT for this view. */
XGU_API bool xgu_view_has_pending_frame(xgu_view_id view);

/* Status flags; XGU_ST_TEXTURE_RECREATED is cleared by this call. */
XGU_API uint32_t xgu_view_status(xgu_view_id view);

/* CPU provider: borrow the latest pixel buffer (bottom-up rows, format from the view desc).
   Returns false when no new frame is available. Release before the next acquire. */
XGU_API bool xgu_view_acquire_pixels(xgu_view_id view, const void** out_data, uint32_t* out_size,
                                     uint32_t* out_width, uint32_t* out_height, uint64_t* out_frame_id);
XGU_API void xgu_view_release_pixels(xgu_view_id view);

/* -------------------------------------------------------------------------- */
/* Documents                                                                   */
/* -------------------------------------------------------------------------- */

/* Loads an HTML document. `path` is relative to the view's UI root
   ("UI/MainMenu/index.html"); references that escape the root are rejected.
   Runs asynchronously on the runtime thread; watch xgu_view_get_state. */
XGU_API xgu_status xgu_view_load(xgu_view_id view, const char* path);

/* Loads HTML held in memory. `base_path` (may be NULL) anchors relative
   references such as <script src> and <img src>. */
XGU_API xgu_status xgu_view_load_html(xgu_view_id view, const char* html, const char* base_path);

/* Re-reads and re-runs the document last passed to xgu_view_load, with a fresh
   JavaScript isolate. */
XGU_API xgu_status xgu_view_reload(xgu_view_id view);

/* -------------------------------------------------------------------------- */
/* JavaScript                                                                  */
/* -------------------------------------------------------------------------- */

/* Compiles and runs `source` in the view's isolate on the runtime thread.
   `origin` names the script in error messages (may be NULL). Uncaught errors
   are logged as XGU_LOG_ERROR ("Uncaught ..."). */
XGU_API xgu_status xgu_view_execute_js(xgu_view_id view, const char* source, const char* origin);

XGU_API xgu_status xgu_view_set_paused(xgu_view_id view, bool paused);
XGU_API xgu_view_state xgu_view_get_state(xgu_view_id view);

/* Destroys every view; call from AssemblyReloadEvents.beforeAssemblyReload. */
XGU_API void xgu_views_destroy_all(void);

/* Stage 1 spike only: records a built-in test picture (rounded rect, image, text) as the next frame.
   Removed when the HTML pipeline lands in Stage 5. */
XGU_API xgu_status xgu_view_draw_test_frame(xgu_view_id view);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* XPLOIT_GAME_UI_XGU_H */
