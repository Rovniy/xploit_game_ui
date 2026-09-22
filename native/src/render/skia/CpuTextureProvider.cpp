#include "render/skia/CpuTextureProvider.h"

#include "core/Log.h"

#include <include/core/SkCanvas.h>
#include <include/core/SkImageInfo.h>

namespace xgu::render {
namespace {

SkColorType toSkColorType(TextureFormat format) {
    return format == TextureFormat::BGRA8 ? kBGRA_8888_SkColorType : kRGBA_8888_SkColorType;
}

} // namespace

bool CpuTextureProvider::ensureSurface(View& view) {
    const uint32_t width = view.width();
    const uint32_t height = view.height();
    auto* current = static_cast<CpuViewSurface*>(view.surface());
    if (current && current->width == width && current->height == height) {
        return true;
    }
    if (width == 0 || height == 0) {
        return false;
    }
    auto fresh = std::make_unique<CpuViewSurface>();
    const SkImageInfo info =
        SkImageInfo::Make(static_cast<int>(width), static_cast<int>(height), toSkColorType(view.desc().format),
                          kPremul_SkAlphaType);
    fresh->raster = SkSurfaces::Raster(info);
    if (!fresh->raster) {
        XGU_LOG_ERROR("CpuTextureProvider: SkSurfaces::Raster(%ux%u) failed", width, height);
        return false;
    }
    fresh->width = width;
    fresh->height = height;
    fresh->format = view.desc().format;
    const size_t bytes = static_cast<size_t>(width) * height * 4u;
    fresh->buffers[0].resize(bytes);
    fresh->buffers[1].resize(bytes);
    if (current) {
        view.setStatus(kStatusTextureRecreated);
    }
    view.setSurface(std::move(fresh));
    return true;
}

bool CpuTextureProvider::paint(View& view, const DisplayList& frame) {
    if (!ensureSurface(view)) {
        return false;
    }
    auto* surface = static_cast<CpuViewSurface*>(view.surface());
    SkCanvas* canvas = surface->raster->getCanvas();
    canvas->save();
    // Flip vertically: Unity's LoadRawTextureData expects the bottom row first.
    canvas->translate(0, static_cast<SkScalar>(surface->height));
    canvas->scale(1, -1);
    if (!frame.dirtyPx.isEmpty()) {
        canvas->clipIRect(frame.dirtyPx);
    }
    canvas->clear(SK_ColorTRANSPARENT);
    if (frame.picture) {
        if (frame.sizePx.width() != static_cast<int>(surface->width) ||
            frame.sizePx.height() != static_cast<int>(surface->height)) {
            canvas->scale(static_cast<float>(surface->width) / static_cast<float>(frame.sizePx.width()),
                          static_cast<float>(surface->height) / static_cast<float>(frame.sizePx.height()));
        }
        canvas->drawPicture(frame.picture);
    }
    canvas->restore();

    std::lock_guard lock(surface->mutex);
    // Write into the buffer the host is not holding.
    const int back = surface->front < 0 ? 0 : 1 - surface->front;
    const SkImageInfo info = surface->raster->imageInfo();
    const size_t rowBytes = static_cast<size_t>(surface->width) * 4u;
    if (!surface->raster->readPixels(info, surface->buffers[back].data(), rowBytes, 0, 0)) {
        XGU_LOG_ERROR("CpuTextureProvider: readPixels failed");
        return false;
    }
    surface->frameId = frame.frameId;
    if (surface->acquired) {
        // Host still reads `front`; publish the new frame when it releases.
        surface->pendingSwap = true;
    } else {
        surface->front = back;
        surface->pendingSwap = false;
        surface->fresh = true;
        view.setStatus(kStatusPixelsReady);
    }
    view.setStatus(kStatusTextureReady);
    return true;
}

void* CpuTextureProvider::nativeTexture(const View&, uint32_t* outWidth, uint32_t* outHeight) const {
    if (outWidth) *outWidth = 0;
    if (outHeight) *outHeight = 0;
    return nullptr;
}

bool CpuTextureProvider::acquirePixels(View& view, const void** outData, uint32_t* outSize, uint32_t* outWidth,
                                       uint32_t* outHeight, uint64_t* outFrameId) {
    auto* surface = static_cast<CpuViewSurface*>(view.surface());
    if (!surface) {
        return false;
    }
    std::lock_guard lock(surface->mutex);
    if (surface->front < 0 || !surface->fresh || surface->acquired) {
        return false;
    }
    surface->acquired = true;
    surface->fresh = false;
    const std::vector<uint8_t>& buffer = surface->buffers[surface->front];
    if (outData) *outData = buffer.data();
    if (outSize) *outSize = static_cast<uint32_t>(buffer.size());
    if (outWidth) *outWidth = surface->width;
    if (outHeight) *outHeight = surface->height;
    if (outFrameId) *outFrameId = surface->frameId;
    view.clearStatus(kStatusPixelsReady);
    return true;
}

void CpuTextureProvider::releasePixels(View& view) {
    auto* surface = static_cast<CpuViewSurface*>(view.surface());
    if (!surface) {
        return;
    }
    std::lock_guard lock(surface->mutex);
    surface->acquired = false;
    if (surface->pendingSwap) {
        surface->front = 1 - surface->front;
        surface->pendingSwap = false;
        surface->fresh = true;
        view.setStatus(kStatusPixelsReady);
    }
}

} // namespace xgu::render
