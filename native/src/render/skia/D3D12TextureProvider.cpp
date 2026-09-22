#include "render/skia/D3D12TextureProvider.h"

#include "core/Log.h"

#include <include/core/SkCanvas.h>
#include <include/core/SkColorSpace.h>
#include <include/core/SkSurfaceProps.h>
#include <include/gpu/ganesh/GrDirectContext.h>
#include <include/gpu/ganesh/GrTypes.h>
#include <include/gpu/ganesh/SkSurfaceGanesh.h>
#include <include/gpu/ganesh/d3d/GrD3DBackendSurface.h>
#include <include/gpu/ganesh/d3d/GrD3DTypes.h>

namespace xgu::render {
namespace {

DXGI_FORMAT toDxgiFormat(TextureFormat format) {
    switch (format) {
    case TextureFormat::BGRA8:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::RGBA8:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    return DXGI_FORMAT_B8G8R8A8_UNORM;
}

SkColorType toSkColorType(TextureFormat format) {
    switch (format) {
    case TextureFormat::BGRA8:
        return kBGRA_8888_SkColorType;
    case TextureFormat::RGBA8:
        return kRGBA_8888_SkColorType;
    }
    return kBGRA_8888_SkColorType;
}

} // namespace

D3D12TextureProvider::D3D12TextureProvider(D3D12GrContext& context) : context_(context) {}

D3D12TextureProvider::~D3D12TextureProvider() { releaseAll(false); }

void D3D12TextureProvider::setFrameFence(ID3D12Fence* fence, FenceValueFn nextValue) {
    fence_ = fence;
    nextFenceValue_ = std::move(nextValue);
}

std::unique_ptr<D3D12ViewSurface> D3D12TextureProvider::createSurface(uint32_t width, uint32_t height,
                                                                      TextureFormat format) {
    ID3D12Device* device = context_.device();
    if (!device || width == 0 || height == 0) {
        return nullptr;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = toDxgiFormat(format);
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    auto surface = std::make_unique<D3D12ViewSurface>();
    const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                       D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                       IID_PPV_ARGS(&surface->resource));
    if (FAILED(hr)) {
        XGU_LOG_ERROR("D3D12TextureProvider: CreateCommittedResource(%ux%u) failed: 0x%08lX", width, height,
                      static_cast<unsigned long>(hr));
        return nullptr;
    }
    surface->resource->SetName(L"xploit_game_ui view texture");
    surface->width = width;
    surface->height = height;
    surface->format = format;
    return surface;
}

bool D3D12TextureProvider::canCreateOnMainThread(const View& view) const {
    return view.surface() == nullptr && context_.hasDevice();
}

bool D3D12TextureProvider::ensureSurface(View& view) {
    const uint32_t width = view.width();
    const uint32_t height = view.height();
    auto* current = static_cast<D3D12ViewSurface*>(view.surface());
    if (current && current->width == width && current->height == height) {
        return true;
    }
    if (!context_.hasDevice()) {
        return false;
    }
    std::unique_ptr<D3D12ViewSurface> fresh = createSurface(width, height, view.desc().format);
    if (!fresh) {
        return false;
    }
    if (current) {
        retire(view.takeSurface());
        view.setStatus(kStatusTextureRecreated);
        view.clearStatus(kStatusTextureReady);
    }
    view.setSurface(std::move(fresh));
    return true;
}

bool D3D12TextureProvider::wrapSurface(D3D12ViewSurface& surface) {
    if (surface.wrapped) {
        return true;
    }
    GrDirectContext* ctx = context_.context();
    if (!ctx) {
        return false;
    }

    GrD3DTextureResourceInfo info;
    surface.resource->AddRef();
    info.fResource.reset(surface.resource.Get());
    info.fResourceState = D3D12_RESOURCE_STATE_COMMON;
    info.fFormat = toDxgiFormat(surface.format);
    info.fSampleCount = 1;
    info.fLevelCount = 1;
    // Our resource is created with SampleDesc.Quality = 0. Skia's default here is
    // DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN, which the D3D12 debug layer flags as a
    // PSO/RTV sample-desc mismatch (id 614) for single-sampled targets.
    info.fSampleQualityPattern = 0;

    surface.backendTexture = GrBackendTextures::MakeD3D(static_cast<int>(surface.width),
                                                        static_cast<int>(surface.height), info, "xgu-view");

    // No LCD text on a transparent UI layer.
    SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
    // kBottomLeft: Skia renders bottom-up so Unity (bottom-up UVs) shows it upright.
    surface.surface = SkSurfaces::WrapBackendTexture(ctx, surface.backendTexture, kBottomLeft_GrSurfaceOrigin, 1,
                                                     toSkColorType(surface.format), nullptr, &props);
    if (!surface.surface) {
        XGU_LOG_ERROR("D3D12TextureProvider: SkSurfaces::WrapBackendTexture failed (%ux%u)", surface.width,
                      surface.height);
        return false;
    }
    surface.wrapped = true;
    return true;
}

bool D3D12TextureProvider::paint(View& view, const DisplayList& frame) {
    if (!context_.ensureContext()) {
        return false;
    }
    if (!ensureSurface(view)) {
        return false;
    }
    auto* surface = static_cast<D3D12ViewSurface*>(view.surface());
    if (!surface || !wrapSurface(*surface)) {
        return false;
    }

    GrDirectContext* ctx = context_.context();
    SkCanvas* canvas = surface->surface->getCanvas();
    canvas->save();
    if (!frame.dirtyPx.isEmpty()) {
        canvas->clipIRect(frame.dirtyPx);
    }
    canvas->clear(SK_ColorTRANSPARENT);
    if (frame.picture) {
        // The picture is recorded in device pixels for the frame's size; scale if the
        // surface was resized between recording and painting.
        if (frame.sizePx.width() != static_cast<int>(surface->width) ||
            frame.sizePx.height() != static_cast<int>(surface->height)) {
            const float sx = static_cast<float>(surface->width) / static_cast<float>(frame.sizePx.width());
            const float sy = static_cast<float>(surface->height) / static_cast<float>(frame.sizePx.height());
            canvas->scale(sx, sy);
        }
        canvas->drawPicture(frame.picture);
    }
    canvas->restore();

    GrFlushInfo flushInfo{};
    ctx->flush(surface->surface.get(), SkSurfaces::BackendSurfaceAccess::kPresent, flushInfo);
    ctx->submit(GrSyncCpu::kNo);
    ctx->checkAsyncWorkCompletion();

    if (context_.isDeviceLost()) {
        return false;
    }
    return true;
}

void D3D12TextureProvider::retire(std::unique_ptr<ViewSurface> surface) {
    if (!surface) {
        return;
    }
    if (fence_ && nextFenceValue_) {
        retired_.push_back(Retired{std::move(surface), nextFenceValue_()});
        return;
    }
    // No fence available: make sure the GPU is idle before freeing.
    if (GrDirectContext* ctx = context_.context()) {
        ctx->flush();
        ctx->submit(GrSyncCpu::kYes);
    }
    surface.reset();
}

void D3D12TextureProvider::collectGarbage() {
    if (retired_.empty()) {
        return;
    }
    if (!fence_) {
        retired_.clear();
        return;
    }
    const uint64_t completed = fence_->GetCompletedValue();
    std::erase_if(retired_, [completed](const Retired& r) { return r.fenceValue <= completed; });
}

void* D3D12TextureProvider::nativeTexture(const View& view, uint32_t* outWidth, uint32_t* outHeight) const {
    const auto* surface = static_cast<const D3D12ViewSurface*>(view.surface());
    if (!surface || !surface->resource) {
        if (outWidth) *outWidth = 0;
        if (outHeight) *outHeight = 0;
        return nullptr;
    }
    if (outWidth) *outWidth = surface->width;
    if (outHeight) *outHeight = surface->height;
    return surface->resource.Get();
}

void D3D12TextureProvider::releaseAll(bool waitIdle) {
    if (waitIdle) {
        if (GrDirectContext* ctx = context_.context()) {
            ctx->flush();
            ctx->submit(GrSyncCpu::kYes);
        }
    }
    retired_.clear();
}

} // namespace xgu::render
