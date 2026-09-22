#pragma once

#include "core/interfaces/ITextureProvider.h"

#include <include/core/SkSurface.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace xgu::render {

struct CpuViewSurface final : ViewSurface {
    sk_sp<SkSurface> raster;
    std::vector<uint8_t> buffers[2];
    int front = -1; // index of the buffer the host may read; -1 = none yet
    bool acquired = false;
    bool fresh = false;       // a frame newer than the last acquire is available
    bool pendingSwap = false; // a frame was painted while the host held `front`
    uint64_t frameId = 0;
    std::mutex mutex;
};

// Software provider: Skia raster surface + pixel buffer that the host uploads
// with Texture2D.LoadRawTextureData. Used when Unity runs without D3D12
// (D3D11, -nographics), for headless tools and for pixel-exact tests.
//
// Pixels are stored bottom-up (Unity convention) in the view's format.
class CpuTextureProvider final : public ITextureProvider {
public:
    ProviderKind kind() const override { return ProviderKind::Cpu; }

    bool ensureSurface(View& view) override;
    bool paint(View& view, const DisplayList& frame) override;
    void retire(std::unique_ptr<ViewSurface> surface) override { surface.reset(); }
    void collectGarbage() override {}
    void* nativeTexture(const View&, uint32_t* outWidth, uint32_t* outHeight) const override;
    bool acquirePixels(View& view, const void** outData, uint32_t* outSize, uint32_t* outWidth,
                       uint32_t* outHeight, uint64_t* outFrameId) override;
    void releasePixels(View& view) override;
    void releaseAll(bool) override {}
};

} // namespace xgu::render
