// xgu_host — standalone Win32/D3D12 host for developing and debugging the
// renderer without Unity. Mirrors the Unity integration: the runtime paints into
// a plugin-owned D3D12 texture through the same RenderSystem/event path, and the
// host copies that texture into the swap chain back buffer every frame.
//
// Usage: xgu_host [--debug] [--width W] [--height H] [--screenshot out.png [--frames N]]
//   --screenshot: after N frames (default 3) read the view texture back from the
//                 GPU, write it as PNG and exit. Used to validate the D3D12 path
//                 without Unity.

#include "core/Log.h"
#include "core/Runtime.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <include/core/SkImageInfo.h>
#include <include/core/SkPixmap.h>
#include <include/core/SkStream.h>
#include <include/encode/SkPngEncoder.h>

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kBackBufferCount = 2;

struct HostState {
    HWND hwnd = nullptr;
    UINT width = 1280;
    UINT height = 720;
    bool resized = false;
    bool running = true;
    bool debugLayer = false;
    std::string screenshotPath; // UTF-8; empty = interactive mode
    int framesBeforeScreenshot = 3;
    int framesRendered = 0;
    int exitCode = 0;

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12Resource> backBuffers[kBackBufferCount];
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    uint64_t fenceValue = 0;

    xgu_view_id view = XGU_INVALID_VIEW;
};

HostState g_host;

void fail(const char* what, HRESULT hr) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
    XGU_LOG_ERROR("%s", buffer);
    MessageBoxA(nullptr, buffer, "xgu_host", MB_ICONERROR);
    std::exit(1);
}

#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        const HRESULT hr_ = (expr);                                                                                    \
        if (FAILED(hr_)) fail(#expr, hr_);                                                                             \
    } while (0)

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            const UINT w = LOWORD(lParam);
            const UINT h = HIWORD(lParam);
            if (w > 0 && h > 0 && (w != g_host.width || h != g_host.height)) {
                g_host.width = w;
                g_host.height = h;
                g_host.resized = true;
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void waitForGpu() {
    const uint64_t value = ++g_host.fenceValue;
    CHECK(g_host.queue->Signal(g_host.fence.Get(), value));
    if (g_host.fence->GetCompletedValue() < value) {
        CHECK(g_host.fence->SetEventOnCompletion(value, g_host.fenceEvent));
        WaitForSingleObject(g_host.fenceEvent, INFINITE);
    }
}

void createDevice() {
    UINT factoryFlags = 0;
    if (g_host.debugLayer) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            XGU_LOG_INFO("D3D12 debug layer enabled");
        } else {
            XGU_LOG_WARNING("D3D12 debug layer unavailable (install Graphics Tools)");
        }
    }

    ComPtr<IDXGIFactory6> factory;
    CHECK(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)));

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; SUCCEEDED(factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                    IID_PPV_ARGS(&adapter)));
         ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_host.device)))) {
            char name[128];
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
            XGU_LOG_INFO("xgu_host: using adapter \"%s\"", name);
            break;
        }
    }
    if (!g_host.device) {
        fail("D3D12CreateDevice (no hardware adapter)", E_FAIL);
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CHECK(g_host.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_host.queue)));

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width = g_host.width;
    scDesc.Height = g_host.height;
    scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = kBackBufferCount;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swapChain1;
    CHECK(factory->CreateSwapChainForHwnd(g_host.queue.Get(), g_host.hwnd, &scDesc, nullptr, nullptr, &swapChain1));
    CHECK(swapChain1.As(&g_host.swapChain));
    factory->MakeWindowAssociation(g_host.hwnd, DXGI_MWA_NO_ALT_ENTER);
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        CHECK(g_host.swapChain->GetBuffer(i, IID_PPV_ARGS(&g_host.backBuffers[i])));
    }

    CHECK(g_host.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_host.allocator)));
    CHECK(g_host.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_host.allocator.Get(), nullptr,
                                           IID_PPV_ARGS(&g_host.commandList)));
    CHECK(g_host.commandList->Close());
    CHECK(g_host.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_host.fence)));
    g_host.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

void resizeSwapChain() {
    waitForGpu();
    for (auto& buffer : g_host.backBuffers) {
        buffer.Reset();
    }
    CHECK(g_host.swapChain->ResizeBuffers(kBackBufferCount, g_host.width, g_host.height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        CHECK(g_host.swapChain->GetBuffer(i, IID_PPV_ARGS(&g_host.backBuffers[i])));
    }
}

void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
}

// Reads the view texture back through a READBACK buffer and writes a top-down PNG.
bool saveViewTexturePng(ID3D12Resource* texture, uint32_t width, uint32_t height, const std::string& path) {
    D3D12_RESOURCE_DESC desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes = 0;
    g_host.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    CHECK(g_host.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));

    CHECK(g_host.allocator->Reset());
    CHECK(g_host.commandList->Reset(g_host.allocator.Get(), nullptr));
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
    g_host.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr); // COMMON promotes to COPY_SOURCE
    CHECK(g_host.commandList->Close());
    ID3D12CommandList* lists[] = {g_host.commandList.Get()};
    g_host.queue->ExecuteCommandLists(1, lists);
    waitForGpu();

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, static_cast<SIZE_T>(totalBytes)};
    CHECK(readback->Map(0, &readRange, &mapped));
    // Skia rendered with kBottomLeft origin, so texture row 0 is the bottom of the image: flip.
    const size_t tightRow = static_cast<size_t>(width) * 4u;
    std::vector<uint8_t> pixels(tightRow * height);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(mapped) + footprint.Offset +
                                static_cast<size_t>(height - 1 - y) * footprint.Footprint.RowPitch;
        std::memcpy(pixels.data() + static_cast<size_t>(y) * tightRow, srcRow, tightRow);
    }
    D3D12_RANGE noWrite{0, 0};
    readback->Unmap(0, &noWrite);

    const SkImageInfo info = SkImageInfo::Make(static_cast<int>(width), static_cast<int>(height),
                                               kBGRA_8888_SkColorType, kPremul_SkAlphaType);
    SkPixmap pixmap(info, pixels.data(), tightRow);
    SkFILEWStream stream(path.c_str());
    if (!stream.isValid()) {
        XGU_LOG_ERROR("xgu_host: cannot open %s for writing", path.c_str());
        return false;
    }
    if (!SkPngEncoder::Encode(&stream, pixmap, SkPngEncoder::Options{})) {
        XGU_LOG_ERROR("xgu_host: PNG encode failed");
        return false;
    }
    XGU_LOG_INFO("xgu_host: wrote %s (%ux%u)", path.c_str(), width, height);
    return true;
}

void renderFrame() {
    // 1. Let the runtime paint into the view texture (same path as Unity's PAINT event).
    if (xgu_view_has_pending_frame(g_host.view)) {
        xgu_get_render_event_func()(xgu_render_event_base() + XGU_EVT_PAINT, reinterpret_cast<void*>(g_host.view));
    }

    // 2. Copy the view texture into the back buffer.
    uint32_t texWidth = 0;
    uint32_t texHeight = 0;
    auto* viewTexture = static_cast<ID3D12Resource*>(xgu_view_get_native_texture(g_host.view, &texWidth, &texHeight));

    const UINT backIndex = g_host.swapChain->GetCurrentBackBufferIndex();
    ID3D12Resource* backBuffer = g_host.backBuffers[backIndex].Get();

    CHECK(g_host.allocator->Reset());
    CHECK(g_host.commandList->Reset(g_host.allocator.Get(), nullptr));
    transition(g_host.commandList.Get(), backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    if (viewTexture && (xgu_view_status(g_host.view) & XGU_ST_TEXTURE_READY)) {
        // The view texture sits in COMMON (simultaneous access) and promotes to COPY_SOURCE implicitly.
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = backBuffer;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = viewTexture;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_BOX box{};
        box.right = std::min<UINT>(texWidth, g_host.width);
        box.bottom = std::min<UINT>(texHeight, g_host.height);
        box.back = 1;
        g_host.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    }
    transition(g_host.commandList.Get(), backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    CHECK(g_host.commandList->Close());
    ID3D12CommandList* lists[] = {g_host.commandList.Get()};
    g_host.queue->ExecuteCommandLists(1, lists);
    CHECK(g_host.swapChain->Present(1, 0));

    // 3. Signal the frame fence (the value retire() asked for) and keep it simple: wait.
    waitForGpu();
    xgu_get_render_event_func()(xgu_render_event_base() + XGU_EVT_GC, nullptr);

    // 4. Screenshot mode: read the view texture back and exit.
    g_host.framesRendered++;
    if (!g_host.screenshotPath.empty() && g_host.framesRendered >= g_host.framesBeforeScreenshot) {
        const bool ready = viewTexture && (xgu_view_status(g_host.view) & XGU_ST_TEXTURE_READY);
        if (!ready || !saveViewTexturePng(viewTexture, texWidth, texHeight, g_host.screenshotPath)) {
            XGU_LOG_ERROR("xgu_host: screenshot failed (texture ready: %d)", ready ? 1 : 0);
            g_host.exitCode = 2;
        }
        PostQuitMessage(0);
    }
}

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<size_t>(size) - 1);
    return out;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--debug") {
            g_host.debugLayer = true;
        } else if (arg == L"--width" && i + 1 < argc) {
            g_host.width = static_cast<UINT>(_wtoi(argv[++i]));
        } else if (arg == L"--height" && i + 1 < argc) {
            g_host.height = static_cast<UINT>(_wtoi(argv[++i]));
        } else if (arg == L"--screenshot" && i + 1 < argc) {
            g_host.screenshotPath = toUtf8(argv[++i]);
        } else if (arg == L"--frames" && i + 1 < argc) {
            g_host.framesBeforeScreenshot = std::max(1, _wtoi(argv[++i]));
        }
    }
    LocalFree(argv);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"XguHostWindow";
    RegisterClassExW(&wc);

    RECT rect{0, 0, static_cast<LONG>(g_host.width), static_cast<LONG>(g_host.height)};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_host.hwnd = CreateWindowExW(0, wc.lpszClassName, L"xploit_game_ui host — Skia Ganesh D3D12",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                                  rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);

    xgu_init_desc init{};
    init.struct_size = sizeof(init);
    xgu_initialize(&init);

    createDevice();

    xgu::render::D3D12Handles handles;
    handles.device = g_host.device.Get();
    handles.queue = g_host.queue.Get();
    handles.frameFence = g_host.fence.Get();
    handles.nextFenceValue = [] { return g_host.fenceValue + 1; };
    xgu::Runtime::instance().render().setEventBase(0);
    xgu::Runtime::instance().render().setD3D12Device(handles);

    xgu_view_desc viewDesc{};
    viewDesc.struct_size = sizeof(viewDesc);
    viewDesc.width = g_host.width;
    viewDesc.height = g_host.height;
    viewDesc.device_pixel_ratio = static_cast<float>(GetDpiForWindow(g_host.hwnd)) / 96.0f;
    viewDesc.format = XGU_FORMAT_BGRA8;
    viewDesc.provider = XGU_PROVIDER_D3D12_EXTERNAL;
    viewDesc.name = "host";
    g_host.view = xgu_view_create(&viewDesc);
    xgu_view_draw_test_frame(g_host.view);

    MSG msg{};
    while (g_host.running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_host.running = false;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_host.running) {
            break;
        }
        if (g_host.resized) {
            g_host.resized = false;
            resizeSwapChain();
            xgu_view_resize(g_host.view, g_host.width, g_host.height, viewDesc.device_pixel_ratio);
            xgu_view_draw_test_frame(g_host.view);
        }
        renderFrame();
    }

    xgu_view_destroy(g_host.view);
    xgu_get_render_event_func()(xgu_render_event_base() + XGU_EVT_GC, nullptr);
    waitForGpu();
    xgu::Runtime::instance().render().shutdownDevice(true);
    xgu_shutdown();
    if (g_host.fenceEvent) {
        CloseHandle(g_host.fenceEvent);
    }
    return g_host.exitCode;
}
