#pragma once

#include "core/interfaces/ITextureProvider.h"
#include "render/skia/D3D12GrContext.h"

#include <include/core/SkSurface.h>
#include <include/gpu/ganesh/GrBackendSurface.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <functional>
#include <vector>

namespace xgu::render {

// Per-view D3D12 surface: a plugin-owned committed resource that Unity wraps
// with Texture2D.CreateExternalTexture, plus the Skia surface around it.
struct D3D12ViewSurface final : ViewSurface {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    GrBackendTexture backendTexture;
    sk_sp<SkSurface> surface; // created on the submission thread on first paint
    bool wrapped = false;
};

// Zero-copy provider (XGU_PROVIDER_D3D12_EXTERNAL).
//
// Resources are created in D3D12_RESOURCE_STATE_COMMON with
// ALLOW_RENDER_TARGET | ALLOW_SIMULTANEOUS_ACCESS. Skia's flush with
// BackendSurfaceAccess::kPresent leaves them in PRESENT (== COMMON); D3D12
// then promotes implicitly when Unity samples and decays back to COMMON after
// each ExecuteCommandLists, so Skia's tracked state stays correct.
class D3D12TextureProvider final : public ITextureProvider {
public:
    using FenceValueFn = std::function<uint64_t()>;

    explicit D3D12TextureProvider(D3D12GrContext& context);
    ~D3D12TextureProvider() override;

    // Fence used to defer destruction until the GPU finished using a resource.
    // `nextValue` must be callable on the submission thread. Both may be null;
    // retire() then waits for the GPU synchronously.
    void setFrameFence(ID3D12Fence* fence, FenceValueFn nextValue);

    ProviderKind kind() const override { return ProviderKind::D3D12External; }

    // Main thread (initial creation) or submission thread (resize).
    bool ensureSurface(View& view) override;
    // Submission thread.
    bool paint(View& view, const DisplayList& frame) override;
    void retire(std::unique_ptr<ViewSurface> surface) override;
    void collectGarbage() override;
    void* nativeTexture(const View& view, uint32_t* outWidth, uint32_t* outHeight) const override;
    bool acquirePixels(View&, const void**, uint32_t*, uint32_t*, uint32_t*, uint64_t*) override { return false; }
    void releasePixels(View&) override {}
    void releaseAll(bool waitIdle) override;

    // Creates only the D3D12 resource (device methods are free-threaded), no Skia objects.
    std::unique_ptr<D3D12ViewSurface> createSurface(uint32_t width, uint32_t height, TextureFormat format);

    // True when the surface can be (re)created right now without a fence (no old surface).
    bool canCreateOnMainThread(const View& view) const;

private:
    struct Retired {
        std::unique_ptr<ViewSurface> surface;
        uint64_t fenceValue = 0;
    };

    bool wrapSurface(D3D12ViewSurface& surface);

    D3D12GrContext& context_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    FenceValueFn nextFenceValue_;
    std::vector<Retired> retired_;
};

} // namespace xgu::render
