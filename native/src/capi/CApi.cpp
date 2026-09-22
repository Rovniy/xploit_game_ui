// Implementation of the public C ABI declared in include/xploit_game_ui/xgu.h.

#include <xploit_game_ui/xgu.h>

#include "core/Log.h"
#include "core/Runtime.h"
#include "render/skia/TestFrame.h"

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace xgu;

namespace {

void XGU_RENDER_EVENT_CALLBACK(int eventId, void* data) { Runtime::instance().render().handleRenderEvent(eventId, data); }

bool withView(xgu_view_id id, const std::function<void(View&)>& fn) {
    return Runtime::instance().views().withView(static_cast<ViewId>(id), fn);
}

// True when `desc->struct_size` covers `field` (forward-compatible struct growth).
template <typename Struct, typename Field>
bool hasField(const Struct* desc, Field Struct::*field) {
    const auto offset = reinterpret_cast<const char*>(&(desc->*field)) - reinterpret_cast<const char*>(desc);
    return desc->struct_size >= static_cast<uint32_t>(offset) + sizeof(Field);
}

} // namespace

extern "C" {

XGU_API const char* xgu_version(void) { return XGU_VERSION_STRING; }

XGU_API int xgu_ping(void) { return 42; }

XGU_API void xgu_log_test(const char* message) {
    Log::write(LogLevel::Info, message ? message : "xgu_log_test");
}

XGU_API xgu_status xgu_initialize(const xgu_init_desc* desc) {
    RuntimeInitDesc init;
    if (desc) {
        if (desc->struct_size < sizeof(uint32_t)) {
            return XGU_ERR_INVALID_ARGUMENT;
        }
        if (hasField(desc, &xgu_init_desc::log_fn)) {
            init.logCallback = desc->log_fn;
        }
        if (hasField(desc, &xgu_init_desc::log_user)) {
            init.logUser = desc->log_user;
        }
        if (hasField(desc, &xgu_init_desc::data_dir) && desc->data_dir) {
            init.dataDir = desc->data_dir;
        }
        if (hasField(desc, &xgu_init_desc::flags)) {
            init.singleThreaded = (desc->flags & XGU_INIT_SINGLE_THREADED) != 0;
        }
    }
    return Runtime::instance().initialize(init) ? XGU_OK : XGU_ERR_INTERNAL;
}

XGU_API void xgu_shutdown(void) { Runtime::instance().shutdown(); }

XGU_API bool xgu_is_initialized(void) { return Runtime::instance().initialized(); }

XGU_API void xgu_set_log_callback(xgu_log_fn fn, void* user) { Log::setCallback(fn, user); }

XGU_API void xgu_tick(double time_seconds) { Runtime::instance().tick(time_seconds); }

XGU_API void xgu_log_queue_enable(bool enabled) { Log::setQueueEnabled(enabled); }

XGU_API bool xgu_log_poll(int* out_level, const char** out_message, xgu_view_id* out_view) {
    static thread_local std::string buffer;
    LogLevel level = LogLevel::Info;
    uint64_t viewId = 0;
    if (!Log::poll(level, buffer, viewId)) {
        return false;
    }
    if (out_level) {
        *out_level = static_cast<int>(level);
    }
    if (out_message) {
        *out_message = buffer.c_str();
    }
    if (out_view) {
        *out_view = static_cast<xgu_view_id>(viewId);
    }
    return true;
}

XGU_API uint32_t xgu_log_dropped_count(void) { return static_cast<uint32_t>(Log::takeDroppedCount()); }

XGU_API xgu_provider xgu_render_provider(void) {
    switch (Runtime::instance().render().activeProvider()) {
    case ProviderKind::D3D12External:
        return XGU_PROVIDER_D3D12_EXTERNAL;
    case ProviderKind::D3D12Copy:
        return XGU_PROVIDER_D3D12_COPY;
    case ProviderKind::Cpu:
        return XGU_PROVIDER_CPU;
    case ProviderKind::Auto:
    case ProviderKind::None:
        break;
    }
    return XGU_PROVIDER_NONE;
}

XGU_API int xgu_render_event_base(void) { return Runtime::instance().render().eventBase(); }

XGU_API xgu_render_event_fn xgu_get_render_event_func(void) { return &XGU_RENDER_EVENT_CALLBACK; }

XGU_API xgu_view_id xgu_view_create(const xgu_view_desc* desc) {
    if (!desc || desc->struct_size < sizeof(xgu_view_desc) || desc->width == 0 || desc->height == 0) {
        XGU_LOG_ERROR("xgu_view_create: invalid view description");
        return XGU_INVALID_VIEW;
    }
    ViewDesc view;
    view.width = desc->width;
    view.height = desc->height;
    view.devicePixelRatio = desc->device_pixel_ratio > 0.0f ? desc->device_pixel_ratio : 1.0f;
    view.format = desc->format == XGU_FORMAT_RGBA8 ? TextureFormat::RGBA8 : TextureFormat::BGRA8;
    switch (desc->provider) {
    case XGU_PROVIDER_D3D12_EXTERNAL:
        view.provider = ProviderKind::D3D12External;
        break;
    case XGU_PROVIDER_D3D12_COPY:
        view.provider = ProviderKind::D3D12Copy;
        break;
    case XGU_PROVIDER_CPU:
        view.provider = ProviderKind::Cpu;
        break;
    case XGU_PROVIDER_AUTO:
    case XGU_PROVIDER_NONE:
    default:
        view.provider = ProviderKind::Auto;
        break;
    }
    if (desc->ui_root) {
        view.uiRoot = desc->ui_root;
    }
    if (desc->name) {
        view.name = desc->name;
    }
    return static_cast<xgu_view_id>(Runtime::instance().createView(std::move(view)));
}

XGU_API xgu_status xgu_view_destroy(xgu_view_id view) {
    return Runtime::instance().destroyView(static_cast<ViewId>(view)) ? XGU_OK : XGU_ERR_INVALID_VIEW;
}

XGU_API xgu_status xgu_view_resize(xgu_view_id view, uint32_t width, uint32_t height, float dpr) {
    if (width == 0 || height == 0) {
        return XGU_ERR_INVALID_ARGUMENT;
    }
    const bool ok = withView(view, [&](View& v) {
        v.requestResize(width, height, dpr > 0.0f ? dpr : v.devicePixelRatio());
        Runtime::instance().render().prepareView(v);
    });
    return ok ? XGU_OK : XGU_ERR_INVALID_VIEW;
}

XGU_API void* xgu_view_get_native_texture(xgu_view_id view, uint32_t* out_width, uint32_t* out_height) {
    void* result = nullptr;
    withView(view, [&](View& v) { result = Runtime::instance().render().nativeTexture(v, out_width, out_height); });
    if (!result) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = 0;
    }
    return result;
}

XGU_API bool xgu_view_has_pending_frame(xgu_view_id view) {
    bool pending = false;
    withView(view, [&](View& v) { pending = v.mailbox().hasPending(); });
    return pending;
}

XGU_API uint32_t xgu_view_status(xgu_view_id view) {
    uint32_t status = 0;
    withView(view, [&](View& v) { status = v.readStatus(kStatusTextureRecreated); });
    return status;
}

XGU_API bool xgu_view_acquire_pixels(xgu_view_id view, const void** out_data, uint32_t* out_size,
                                     uint32_t* out_width, uint32_t* out_height, uint64_t* out_frame_id) {
    bool ok = false;
    withView(view, [&](View& v) {
        ok = Runtime::instance().render().acquirePixels(v, out_data, out_size, out_width, out_height, out_frame_id);
    });
    return ok;
}

XGU_API void xgu_view_release_pixels(xgu_view_id view) {
    withView(view, [&](View& v) { Runtime::instance().render().releasePixels(v); });
}

XGU_API xgu_status xgu_view_load(xgu_view_id view, const char* path) {
    if (!path || path[0] == 0) {
        return XGU_ERR_INVALID_ARGUMENT;
    }
    if (!Runtime::instance().initialized()) {
        return XGU_ERR_NOT_INITIALIZED;
    }
    return Runtime::instance().loadDocument(static_cast<ViewId>(view), std::string(path)) ? XGU_OK
                                                                                          : XGU_ERR_INVALID_VIEW;
}

XGU_API xgu_status xgu_view_load_html(xgu_view_id view, const char* html, const char* base_path) {
    if (!html) {
        return XGU_ERR_INVALID_ARGUMENT;
    }
    if (!Runtime::instance().initialized()) {
        return XGU_ERR_NOT_INITIALIZED;
    }
    const bool ok = Runtime::instance().loadHtml(static_cast<ViewId>(view), std::string(html),
                                                 base_path ? std::string(base_path) : std::string());
    return ok ? XGU_OK : XGU_ERR_INVALID_VIEW;
}

XGU_API xgu_status xgu_view_reload(xgu_view_id view) {
    if (!Runtime::instance().initialized()) {
        return XGU_ERR_NOT_INITIALIZED;
    }
    return Runtime::instance().reloadDocument(static_cast<ViewId>(view)) ? XGU_OK : XGU_ERR_INVALID_VIEW;
}

namespace {

// Copies the host's event into the engine's own struct. Strings are copied here
// because the caller's buffers do not outlive the call.
input::InputEvent toInputEvent(const xgu_input_event& source) {
    input::InputEvent event;
    event.type = static_cast<input::InputEventType>(source.type);
    event.x = source.x;
    event.y = source.y;
    event.deltaX = source.delta_x;
    event.deltaY = source.delta_y;
    event.button = source.button;
    event.buttons = source.buttons;
    event.modifiers.alt = (source.modifiers & XGU_MOD_ALT) != 0;
    event.modifiers.ctrl = (source.modifiers & XGU_MOD_CTRL) != 0;
    event.modifiers.shift = (source.modifiers & XGU_MOD_SHIFT) != 0;
    event.modifiers.meta = (source.modifiers & XGU_MOD_META) != 0;
    event.key = source.key ? source.key : "";
    event.code = source.code ? source.code : "";
    event.text = source.text ? source.text : "";
    event.touchId = source.touch_id;
    event.time = source.time;
    event.repeat = source.repeat;
    return event;
}

} // namespace

XGU_API xgu_status xgu_view_send_input(xgu_view_id view, const xgu_input_event* event) {
    if (!Runtime::instance().initialized()) {
        return XGU_ERR_NOT_INITIALIZED;
    }
    if (!event || event->struct_size < sizeof(xgu_input_event)) {
        return XGU_ERR_INVALID_ARGUMENT;
    }
    if (event->type < XGU_INPUT_MOUSE_MOVE || event->type > XGU_INPUT_WINDOW_BLUR) {
        return XGU_ERR_INVALID_ARGUMENT;
    }
    return Runtime::instance().sendInput(static_cast<ViewId>(view), toInputEvent(*event)) ? XGU_OK
                                                                                         : XGU_ERR_INVALID_VIEW;
}

XGU_API xgu_status xgu_view_set_focus(xgu_view_id view, const char* element_id) {
    if (!Runtime::instance().initialized()) {
        return XGU_ERR_NOT_INITIALIZED;
    }
    return Runtime::instance().setFocus(static_cast<ViewId>(view), element_id ? element_id : "") ? XGU_OK
                                                                                                 : XGU_ERR_INVALID_VIEW;
}

namespace {

// The strings a poll hands out have to outlive the call, so the last message is
// kept here until the next poll on the same view.
std::mutex g_polledMutex;
std::unordered_map<xgu_view_id, BridgeMessage> g_polled;

} // namespace

XGU_API bool xgu_view_poll_message(xgu_view_id view, xgu_message* out_message) {
    if (!Runtime::instance().initialized() || !out_message ||
        out_message->struct_size < sizeof(xgu_message)) {
        return false;
    }
    BridgeMessage message;
    if (!Runtime::instance().pollMessage(static_cast<ViewId>(view), message)) {
        return false;
    }
    std::lock_guard lock(g_polledMutex);
    BridgeMessage& held = g_polled[view];
    held = std::move(message);
    out_message->kind = held.kind == BridgeMessageKind::Call ? XGU_MSG_CALL : XGU_MSG_EMIT;
    out_message->id = held.id;
    out_message->name = held.name.c_str();
    out_message->json = held.json.empty() ? nullptr : held.json.c_str();
    return true;
}

XGU_API xgu_status xgu_view_send_event(xgu_view_id view, const char* name, const char* json) {
    if (!Runtime::instance().initialized()) {
        return XGU_ERR_NOT_INITIALIZED;
    }
    if (!name || name[0] == 0) {
        return XGU_ERR_INVALID_ARGUMENT;
    }
    return Runtime::instance().sendEvent(static_cast<ViewId>(view), name, json ? json : "") ? XGU_OK
                                                                                           : XGU_ERR_INVALID_VIEW;
}

XGU_API xgu_status xgu_view_reply(xgu_view_id view, uint64_t id, bool ok, const char* json) {
    if (!Runtime::instance().initialized()) {
        return XGU_ERR_NOT_INITIALIZED;
    }
    if (id == 0) {
        return XGU_ERR_INVALID_ARGUMENT;
    }
    return Runtime::instance().reply(static_cast<ViewId>(view), id, ok, json ? json : "") ? XGU_OK
                                                                                         : XGU_ERR_INVALID_VIEW;
}

XGU_API uint64_t xgu_view_bridge_dropped(xgu_view_id view) {
    if (!Runtime::instance().initialized()) {
        return 0;
    }
    View* resolved = Runtime::instance().views().resolve(static_cast<ViewId>(view));
    return resolved ? static_cast<uint64_t>(resolved->bridge().droppedCount()) : 0;
}

XGU_API bool xgu_view_get_stats(xgu_view_id view, xgu_frame_stats* out_stats) {
    if (!Runtime::instance().initialized() || !out_stats || out_stats->struct_size < sizeof(xgu_frame_stats)) {
        return false;
    }
    View* resolved = Runtime::instance().views().resolve(static_cast<ViewId>(view));
    if (!resolved) {
        return false;
    }
    const View::FrameStats& stats = resolved->frameStats();
    out_stats->frames_published = stats.published;
    out_stats->frames_skipped = stats.skipped;
    out_stats->style_ms = stats.styleMs;
    out_stats->layout_ms = stats.layoutMs;
    out_stats->paint_ms = stats.paintMs;
    out_stats->raster_ms = stats.rasterMs;
    out_stats->damage_x = stats.damageX;
    out_stats->damage_y = stats.damageY;
    out_stats->damage_width = stats.damageWidth;
    out_stats->damage_height = stats.damageHeight;
    return true;
}

XGU_API xgu_status xgu_view_repaint(xgu_view_id view) {
    if (!Runtime::instance().initialized()) {
        return XGU_ERR_NOT_INITIALIZED;
    }
    return Runtime::instance().repaintView(static_cast<ViewId>(view)) ? XGU_OK : XGU_ERR_INVALID_VIEW;
}

XGU_API xgu_status xgu_view_execute_js(xgu_view_id view, const char* source, const char* origin) {
    if (!source) {
        return XGU_ERR_INVALID_ARGUMENT;
    }
    if (!Runtime::instance().initialized()) {
        return XGU_ERR_NOT_INITIALIZED;
    }
    const bool ok = Runtime::instance().executeJavaScript(static_cast<ViewId>(view), std::string(source),
                                                          origin ? std::string(origin) : std::string("<execute_js>"));
    return ok ? XGU_OK : XGU_ERR_INVALID_VIEW;
}

XGU_API xgu_status xgu_view_set_paused(xgu_view_id view, bool paused) {
    const bool ok = withView(view, [&](View& v) { v.setPaused(paused); });
    return ok ? XGU_OK : XGU_ERR_INVALID_VIEW;
}

XGU_API xgu_view_state xgu_view_get_state(xgu_view_id view) {
    xgu_view_state state = XGU_STATE_DESTROYED;
    withView(view, [&](View& v) {
        state = v.paused() ? XGU_STATE_PAUSED : static_cast<xgu_view_state>(static_cast<int>(v.state()));
    });
    return state;
}

XGU_API void xgu_views_destroy_all(void) { Runtime::instance().destroyAllViews(); }

XGU_API xgu_status xgu_view_draw_test_frame(xgu_view_id view) {
    xgu_status status = XGU_ERR_INVALID_VIEW;
    withView(view, [&](View& v) {
        render::DisplayList frame = render::recordTestFrame(static_cast<int>(v.width()), static_cast<int>(v.height()),
                                                            v.devicePixelRatio(), v.nextFrameId());
        if (!frame.valid()) {
            status = XGU_ERR_INTERNAL;
            return;
        }
        v.mailbox().publish(std::move(frame));
        Runtime::instance().render().paintIfCpu(v);
        status = XGU_OK;
    });
    return status;
}

} // extern "C"
