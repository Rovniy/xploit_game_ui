#pragma once

#include "core/View.h"
#include "render/DisplayList.h"

#include <cstdint>

namespace xgu {

// Owns the per-view surface a DisplayList is painted into and exposes the
// result to the host: a native GPU texture (D3D12 providers) or a CPU pixel
// buffer (CPU provider).
//
// Threading: createSurface/paint/retire for GPU providers run on the graphics
// submission thread; the CPU provider runs them on the runtime (or calling)
// thread. nativeTexture()/acquirePixels() are safe from the main thread.
class ITextureProvider {
public:
    virtual ~ITextureProvider() = default;

    virtual ProviderKind kind() const = 0;

    // Creates (or recreates) the surface for the view's current size. Returns
    // false when the device is unavailable.
    virtual bool ensureSurface(View& view) = 0;

    // Paints one frame into the view's surface. Returns false on device loss.
    virtual bool paint(View& view, const render::DisplayList& frame) = 0;

    // Hands the surface back for deferred destruction (GPU) or immediate free (CPU).
    virtual void retire(std::unique_ptr<ViewSurface> surface) = 0;

    // Processes deferred releases whose GPU work has completed.
    virtual void collectGarbage() = 0;

    // ID3D12Resource* (or nullptr) plus its size.
    virtual void* nativeTexture(const View& view, uint32_t* outWidth, uint32_t* outHeight) const = 0;

    // CPU provider only; others return false.
    virtual bool acquirePixels(View& view, const void** outData, uint32_t* outSize, uint32_t* outWidth,
                               uint32_t* outHeight, uint64_t* outFrameId) = 0;
    virtual void releasePixels(View& view) = 0;

    // Device teardown: drop every GPU object (device reset, shutdown).
    virtual void releaseAll(bool waitIdle) = 0;
};

} // namespace xgu
