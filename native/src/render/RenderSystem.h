#pragma once

#include "core/View.h"
#include "core/ViewRegistry.h"
#include "core/interfaces/ITextureProvider.h"

#include <d3d12.h>

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace xgu::render {

class D3D12GrContext;
class D3D12TextureProvider;
class CpuTextureProvider;

struct D3D12Handles {
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    // Optional fence signalled by the host when a frame completes. `nextFenceValue`
    // is called on the submission thread when a resource is retired.
    ID3D12Fence* frameFence = nullptr;
    std::function<uint64_t()> nextFenceValue;
};

// Chooses the texture provider, owns the Skia D3D12 context and dispatches
// render events. One instance per process (owned by Runtime).
class RenderSystem {
public:
    explicit RenderSystem(ViewRegistry& views);
    ~RenderSystem();

    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;

    // --- device lifecycle (render/submission thread, or host main thread) ---
    void setD3D12Device(const D3D12Handles& handles);
    void setNoDevice();
    void onDeviceLost();
    void shutdownDevice(bool waitIdle);

    ProviderKind activeProvider() const;
    void setEventBase(int base) { eventBase_ = base; }
    int eventBase() const { return eventBase_; }

    // --- main thread ---
    // Resolves Auto to the active provider; may return nullptr when no device.
    ITextureProvider* providerFor(View& view);
    // Creates whatever can be created without the submission thread.
    void prepareView(View& view);
    // Takes ownership of a removed view; its surface is retired on the submission thread.
    void destroyView(std::unique_ptr<View> view);
    void* nativeTexture(View& view, uint32_t* outWidth, uint32_t* outHeight);
    bool acquirePixels(View& view, const void** outData, uint32_t* outSize, uint32_t* outWidth, uint32_t* outHeight,
                       uint64_t* outFrameId);
    void releasePixels(View& view);

    // Paints immediately on the calling thread (CPU provider only). Returns false
    // for GPU providers, whose frames wait for XGU_EVT_PAINT.
    bool paintIfCpu(View& view);

    // --- submission thread ---
    void handleRenderEvent(int eventId, void* data);
    bool paintView(View& view);
    void collectGarbage();

    D3D12GrContext* grContext() const { return context_.get(); }

private:
    void flushPendingRetires();

    ViewRegistry& views_;
    mutable std::recursive_mutex mutex_;
    std::unique_ptr<D3D12GrContext> context_;
    std::unique_ptr<D3D12TextureProvider> d3d12_;
    std::unique_ptr<CpuTextureProvider> cpu_;
    std::vector<std::unique_ptr<ViewSurface>> pendingRetire_;
    ProviderKind active_ = ProviderKind::None;
    int eventBase_ = 0;
    bool deviceLost_ = false;
};

} // namespace xgu::render
