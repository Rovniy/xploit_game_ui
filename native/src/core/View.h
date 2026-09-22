#pragma once

#include "render/DisplayList.h"
#include "render/FrameMailbox.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace xgu {

enum class TextureFormat : uint8_t { BGRA8 = 0, RGBA8 = 1 };

enum class ProviderKind : uint8_t { Auto = 0, D3D12External = 1, D3D12Copy = 2, Cpu = 3, None = 4 };

struct ViewDesc {
    uint32_t width = 0;  // device pixels
    uint32_t height = 0; // device pixels
    float devicePixelRatio = 1.0f;
    TextureFormat format = TextureFormat::BGRA8;
    ProviderKind provider = ProviderKind::Auto;
    std::string uiRoot;
    std::string name;
};

// Status flags mirrored to the C ABI (xgu_view_status_flags).
enum ViewStatusFlags : uint32_t {
    kStatusTextureReady = 1u << 0,
    kStatusTextureRecreated = 1u << 1,
    kStatusDeviceLost = 1u << 2,
    kStatusPixelsReady = 1u << 3,
};

// Provider-owned per-view GPU/CPU surface state. Concrete types live in render/.
struct ViewSurface {
    virtual ~ViewSurface() = default;
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::BGRA8;
};

// A view is one HTML document rendered to one texture. Stage 1 holds only the
// rendering state; DOM/CSS/JS state is added in later stages.
class View {
public:
    explicit View(ViewDesc desc);
    ~View();

    View(const View&) = delete;
    View& operator=(const View&) = delete;

    const ViewDesc& desc() const { return desc_; }
    ProviderKind provider() const { return provider_; }
    void setProvider(ProviderKind provider) { provider_ = provider; }

    // Requested size (main thread writes, submission thread reads).
    void requestResize(uint32_t width, uint32_t height, float dpr);
    uint32_t width() const { return width_.load(std::memory_order_acquire); }
    uint32_t height() const { return height_.load(std::memory_order_acquire); }
    float devicePixelRatio() const { return dpr_.load(std::memory_order_acquire); }

    render::FrameMailbox& mailbox() { return mailbox_; }

    // Surface is owned by the view but created/destroyed by the active provider.
    ViewSurface* surface() const { return surface_.get(); }
    std::unique_ptr<ViewSurface> takeSurface() { return std::move(surface_); }
    void setSurface(std::unique_ptr<ViewSurface> surface) { surface_ = std::move(surface); }

    uint32_t status() const { return status_.load(std::memory_order_acquire); }
    void setStatus(uint32_t flags) { status_.fetch_or(flags, std::memory_order_acq_rel); }
    void clearStatus(uint32_t flags) { status_.fetch_and(~flags, std::memory_order_acq_rel); }
    // Returns the flags and clears the ones in `clearMask`.
    uint32_t readStatus(uint32_t clearMask);

    uint64_t nextFrameId() { return ++frameCounter_; }

private:
    ViewDesc desc_;
    ProviderKind provider_;
    std::atomic<uint32_t> width_;
    std::atomic<uint32_t> height_;
    std::atomic<float> dpr_;
    std::atomic<uint32_t> status_{0};
    uint64_t frameCounter_ = 0;
    render::FrameMailbox mailbox_;
    std::unique_ptr<ViewSurface> surface_;
};

} // namespace xgu
