#include "render/RenderSystem.h"

#include "core/Log.h"
#include "render/skia/CpuTextureProvider.h"
#include "render/skia/D3D12GrContext.h"
#include "render/skia/D3D12TextureProvider.h"

#include <xploit_game_ui/xgu.h>

namespace xgu::render {

RenderSystem::RenderSystem(ViewRegistry& views)
    : views_(views), context_(std::make_unique<D3D12GrContext>()),
      d3d12_(std::make_unique<D3D12TextureProvider>(*context_)), cpu_(std::make_unique<CpuTextureProvider>()) {}

RenderSystem::~RenderSystem() { shutdownDevice(false); }

void RenderSystem::setD3D12Device(const D3D12Handles& handles) {
    std::lock_guard lock(mutex_);
    context_->setDevice(handles.device, handles.queue);
    d3d12_->setFrameFence(handles.frameFence, handles.nextFenceValue);
    active_ = ProviderKind::D3D12External;
    deviceLost_ = false;
    XGU_LOG_INFO("Render provider: D3D12 external texture");
}

void RenderSystem::setNoDevice() {
    std::lock_guard lock(mutex_);
    active_ = ProviderKind::Cpu;
    XGU_LOG_INFO("Render provider: CPU raster");
}

void RenderSystem::onDeviceLost() {
    std::lock_guard lock(mutex_);
    deviceLost_ = true;
    context_->abandonContext();
    d3d12_->releaseAll(false);
    pendingRetire_.clear();
    views_.forEach([](ViewId, View& view) {
        view.setStatus(kStatusDeviceLost);
        view.takeSurface().reset();
    });
}

void RenderSystem::shutdownDevice(bool waitIdle) {
    std::lock_guard lock(mutex_);
    views_.forEach([](ViewId, View& view) {
        if (view.provider() == ProviderKind::D3D12External || view.provider() == ProviderKind::D3D12Copy) {
            view.takeSurface().reset();
        }
    });
    pendingRetire_.clear();
    d3d12_->releaseAll(waitIdle);
    context_->reset();
    if (active_ == ProviderKind::D3D12External || active_ == ProviderKind::D3D12Copy) {
        active_ = ProviderKind::None;
    }
}

ProviderKind RenderSystem::activeProvider() const {
    std::lock_guard lock(mutex_);
    return active_;
}

ITextureProvider* RenderSystem::providerFor(View& view) {
    ProviderKind kind = view.provider();
    if (kind == ProviderKind::Auto) {
        kind = active_ == ProviderKind::None ? ProviderKind::Cpu : active_;
        view.setProvider(kind);
    }
    switch (kind) {
    case ProviderKind::D3D12External:
        return context_->hasDevice() ? d3d12_.get() : nullptr;
    case ProviderKind::D3D12Copy:
        XGU_LOG_WARNING("D3D12 copy provider is not implemented yet; using external texture provider");
        view.setProvider(ProviderKind::D3D12External);
        return context_->hasDevice() ? d3d12_.get() : nullptr;
    case ProviderKind::Cpu:
        return cpu_.get();
    case ProviderKind::Auto:
    case ProviderKind::None:
        break;
    }
    return nullptr;
}

void RenderSystem::prepareView(View& view) {
    std::lock_guard lock(mutex_);
    ITextureProvider* provider = providerFor(view);
    if (!provider) {
        return;
    }
    if (provider == d3d12_.get()) {
        // Resource creation is free-threaded; Skia wrapping happens on first paint.
        if (d3d12_->canCreateOnMainThread(view)) {
            d3d12_->ensureSurface(view);
        }
        return;
    }
    provider->ensureSurface(view);
}

void RenderSystem::destroyView(std::unique_ptr<View> view) {
    if (!view) {
        return;
    }
    std::lock_guard lock(mutex_);
    std::unique_ptr<ViewSurface> surface = view->takeSurface();
    view->mailbox().clear();
    if (!surface) {
        return;
    }
    if (view->provider() == ProviderKind::Cpu) {
        cpu_->retire(std::move(surface));
        return;
    }
    // GPU surfaces need a fence value that is only obtainable on the submission
    // thread; hand them over via the next PAINT/GC event.
    pendingRetire_.push_back(std::move(surface));
}

void* RenderSystem::nativeTexture(View& view, uint32_t* outWidth, uint32_t* outHeight) {
    std::lock_guard lock(mutex_);
    ITextureProvider* provider = providerFor(view);
    if (!provider) {
        if (outWidth) *outWidth = 0;
        if (outHeight) *outHeight = 0;
        return nullptr;
    }
    if (provider == d3d12_.get() && d3d12_->canCreateOnMainThread(view)) {
        d3d12_->ensureSurface(view);
    }
    return provider->nativeTexture(view, outWidth, outHeight);
}

bool RenderSystem::acquirePixels(View& view, const void** outData, uint32_t* outSize, uint32_t* outWidth,
                                 uint32_t* outHeight, uint64_t* outFrameId) {
    ITextureProvider* provider = providerFor(view);
    return provider && provider->acquirePixels(view, outData, outSize, outWidth, outHeight, outFrameId);
}

void RenderSystem::releasePixels(View& view) {
    if (ITextureProvider* provider = providerFor(view)) {
        provider->releasePixels(view);
    }
}

bool RenderSystem::paintIfCpu(View& view) {
    std::lock_guard lock(mutex_);
    ITextureProvider* provider = providerFor(view);
    if (provider != cpu_.get()) {
        return false;
    }
    std::optional<DisplayList> frame = view.mailbox().take();
    if (!frame || !frame->valid()) {
        return false;
    }
    return cpu_->paint(view, *frame);
}

void RenderSystem::handleRenderEvent(int eventId, void* data) {
    const int offset = eventId - eventBase_;
    switch (offset) {
    case XGU_EVT_PAINT: {
        const ViewId id = static_cast<ViewId>(reinterpret_cast<uintptr_t>(data));
        views_.withView(id, [this](View& view) { paintView(view); });
        flushPendingRetires();
        collectGarbage();
        break;
    }
    case XGU_EVT_GC:
        flushPendingRetires();
        collectGarbage();
        break;
    case XGU_EVT_BLIT:
        // Stage 5: D3D12 copy provider.
        break;
    default:
        XGU_LOG_WARNING("RenderSystem: unknown render event %d (base %d)", eventId, eventBase_);
        break;
    }
}

bool RenderSystem::paintView(View& view) {
    std::lock_guard lock(mutex_);
    if (deviceLost_) {
        view.setStatus(kStatusDeviceLost);
        return false;
    }
    ITextureProvider* provider = providerFor(view);
    if (!provider) {
        return false;
    }
    std::optional<DisplayList> frame = view.mailbox().take();
    if (!frame || !frame->valid()) {
        // Nothing new; still make sure the surface exists (resize without content).
        provider->ensureSurface(view);
        return true;
    }
    const bool ok = provider->paint(view, *frame);
    if (ok) {
        view.setStatus(kStatusTextureReady);
    } else if (context_->isDeviceLost()) {
        XGU_LOG_ERROR("RenderSystem: device lost while painting");
        onDeviceLost();
    }
    return ok;
}

void RenderSystem::flushPendingRetires() {
    std::lock_guard lock(mutex_);
    for (auto& surface : pendingRetire_) {
        d3d12_->retire(std::move(surface));
    }
    pendingRetire_.clear();
}

void RenderSystem::collectGarbage() {
    std::lock_guard lock(mutex_);
    d3d12_->collectGarbage();
    if (GrDirectContext* ctx = context_->context()) {
        ctx->checkAsyncWorkCompletion();
        ctx->performDeferredCleanup(std::chrono::seconds(2));
    }
}

} // namespace xgu::render
