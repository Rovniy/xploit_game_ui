// Unity Native Plugin entry points for xploit_game_ui.dll.
//
// Wires IUnityLog into the runtime log, reserves render event ids, configures
// the D3D12 plugin events (queue access on the submission thread) and hands the
// Unity device/queue/frame fence to the RenderSystem.

#include "core/Log.h"
#include "core/Runtime.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <d3d12.h>
#include <dxgi.h> // IUnityGraphicsD3D12.h uses IDXGISwapChain without including it

#include <PluginAPI/IUnityGraphics.h>
#include <PluginAPI/IUnityGraphicsD3D12.h>
#include <PluginAPI/IUnityInterface.h>
#include <PluginAPI/IUnityLog.h>

namespace {

IUnityInterfaces* g_unity = nullptr;
IUnityGraphics* g_graphics = nullptr;
IUnityLog* g_log = nullptr;
IUnityGraphicsD3D12v8* g_d3d12v8 = nullptr;
IUnityGraphicsD3D12v7* g_d3d12v7 = nullptr;
int g_eventBase = 0;

void unityLogSink(void*, int level, const char* message) {
    if (!g_log) {
        return;
    }
    UnityLogType type = kUnityLogTypeLog;
    switch (static_cast<xgu::LogLevel>(level)) {
    case xgu::LogLevel::Error:
        type = kUnityLogTypeError;
        break;
    case xgu::LogLevel::Warning:
        type = kUnityLogTypeWarning;
        break;
    default:
        break;
    }
    g_log->Log(type, message, __FILE__, __LINE__);
}

const char* rendererName(UnityGfxRenderer renderer) {
    switch (renderer) {
    case kUnityGfxRendererD3D11:
        return "Direct3D 11";
    case kUnityGfxRendererD3D12:
        return "Direct3D 12";
    case kUnityGfxRendererVulkan:
        return "Vulkan";
    case kUnityGfxRendererNull:
        return "Null (batchmode/-nographics)";
    case kUnityGfxRendererOpenGLCore:
        return "OpenGL Core";
    default:
        return "other";
    }
}

uint64_t nextFrameFenceValue() {
    if (g_d3d12v8) {
        return g_d3d12v8->GetNextFrameFenceValue();
    }
    if (g_d3d12v7) {
        return g_d3d12v7->GetNextFrameFenceValue();
    }
    return 0;
}

void configureD3D12Events() {
    UnityD3D12PluginEventConfig config{};
    config.graphicsQueueAccess = kUnityD3D12GraphicsQueueAccess_Allow;
    config.flags = kUnityD3D12EventConfigFlag_FlushCommandBuffers | kUnityD3D12EventConfigFlag_SyncWorkerThreads;
    config.ensureActiveRenderTextureIsBound = false;
    for (int offset : {static_cast<int>(XGU_EVT_PAINT), static_cast<int>(XGU_EVT_GC)}) {
        if (g_d3d12v8) {
            g_d3d12v8->ConfigureEvent(g_eventBase + offset, &config);
        } else if (g_d3d12v7) {
            g_d3d12v7->ConfigureEvent(g_eventBase + offset, &config);
        }
    }
}

void initializeDevice() {
    xgu::render::RenderSystem& render = xgu::Runtime::instance().render();
    if (!g_graphics) {
        render.setNoDevice();
        return;
    }
    const UnityGfxRenderer renderer = g_graphics->GetRenderer();
    XGU_LOG_INFO("Unity graphics device: %s", rendererName(renderer));
    if (renderer != kUnityGfxRendererD3D12) {
        render.setNoDevice();
        return;
    }

    g_d3d12v8 = g_unity->Get<IUnityGraphicsD3D12v8>();
    if (!g_d3d12v8) {
        g_d3d12v7 = g_unity->Get<IUnityGraphicsD3D12v7>();
    }
    if (!g_d3d12v8 && !g_d3d12v7) {
        XGU_LOG_ERROR("IUnityGraphicsD3D12 v7/v8 not available; falling back to CPU provider");
        render.setNoDevice();
        return;
    }
    XGU_LOG_INFO("Using IUnityGraphicsD3D12%s", g_d3d12v8 ? "v8" : "v7");

    xgu::render::D3D12Handles handles;
    handles.device = g_d3d12v8 ? g_d3d12v8->GetDevice() : g_d3d12v7->GetDevice();
    handles.queue = g_d3d12v8 ? g_d3d12v8->GetCommandQueue() : g_d3d12v7->GetCommandQueue();
    handles.frameFence = g_d3d12v8 ? g_d3d12v8->GetFrameFence() : g_d3d12v7->GetFrameFence();
    handles.nextFenceValue = &nextFrameFenceValue;
    if (!handles.device || !handles.queue) {
        XGU_LOG_ERROR("IUnityGraphicsD3D12 returned a null device or queue; falling back to CPU provider");
        render.setNoDevice();
        return;
    }
    configureD3D12Events();
    render.setD3D12Device(handles);
}

void UNITY_INTERFACE_API onGraphicsDeviceEvent(UnityGfxDeviceEventType eventType) {
    xgu::render::RenderSystem& render = xgu::Runtime::instance().render();
    switch (eventType) {
    case kUnityGfxDeviceEventInitialize:
        initializeDevice();
        break;
    case kUnityGfxDeviceEventShutdown:
        render.shutdownDevice(true);
        g_d3d12v8 = nullptr;
        g_d3d12v7 = nullptr;
        break;
    case kUnityGfxDeviceEventBeforeReset:
        render.shutdownDevice(true);
        break;
    case kUnityGfxDeviceEventAfterReset:
        initializeDevice();
        break;
    }
}

} // namespace

extern "C" {

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces) {
    g_unity = unityInterfaces;
    g_log = g_unity->Get<IUnityLog>();
    g_graphics = g_unity->Get<IUnityGraphics>();
    xgu::Log::setCallback(&unityLogSink, nullptr);

    if (g_graphics) {
        g_eventBase = g_graphics->ReserveEventIDRange(XGU_EVT_COUNT);
        xgu::Runtime::instance().render().setEventBase(g_eventBase);
        g_graphics->RegisterDeviceEventCallback(onGraphicsDeviceEvent);
    }
    XGU_LOG_INFO("xploit_game_ui %s plugin loaded (render events %d..%d)", XGU_VERSION_STRING, g_eventBase,
                 g_eventBase + XGU_EVT_COUNT - 1);

    // The device may already exist when the plugin is loaded late; the manual
    // recommends running the Initialize path once by hand.
    onGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload() {
    if (g_graphics) {
        g_graphics->UnregisterDeviceEventCallback(onGraphicsDeviceEvent);
    }
    xgu::Runtime::instance().render().shutdownDevice(true);
    XGU_LOG_INFO("xploit_game_ui plugin unloaded");
    xgu::Log::setCallback(nullptr, nullptr);
    g_d3d12v8 = nullptr;
    g_d3d12v7 = nullptr;
    g_graphics = nullptr;
    g_log = nullptr;
    g_unity = nullptr;
}

} // extern "C"
