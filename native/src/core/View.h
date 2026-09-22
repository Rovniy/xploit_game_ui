#pragma once

#include "core/interfaces/IJavaScriptRuntime.h"
#include "render/DisplayList.h"
#include "render/FrameMailbox.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace xgu {

enum class TextureFormat : uint8_t { BGRA8 = 0, RGBA8 = 1 };

enum class ProviderKind : uint8_t { Auto = 0, D3D12External = 1, D3D12Copy = 2, Cpu = 3, None = 4 };

// Lifecycle states mirrored to the C ABI (xgu_view_state).
enum class ViewState : int {
    Created = 0,
    Loading = 1,
    DomReady = 2,
    JsReady = 3,
    Interactive = 4,
    Paused = 5,
    Destroyed = 6,
};

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

// A view is one HTML document rendered to one texture. Rendering state is
// touched from the main and submission threads through thread-safe members;
// the JavaScript runtime (and later DOM/CSS/layout) belongs to the runtime thread.
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

    ViewState state() const { return static_cast<ViewState>(state_.load(std::memory_order_acquire)); }
    void setState(ViewState state) { state_.store(static_cast<int>(state), std::memory_order_release); }
    bool paused() const { return paused_.load(std::memory_order_acquire); }
    void setPaused(bool paused) { paused_.store(paused, std::memory_order_release); }

    uint64_t nextFrameId() { return ++frameCounter_; }

    // --- runtime thread only -------------------------------------------------
    // Factory used to create the JavaScript runtime lazily (installed by Runtime).
    static void setJavaScriptRuntimeFactory(JavaScriptRuntimeFactory factory);
    // Returns the runtime, creating it on first use; nullptr when unavailable.
    IJavaScriptRuntime* ensureJavaScript();
    IJavaScriptRuntime* javaScript() const { return js_.get(); }
    void disposeJavaScript();

private:
    ViewDesc desc_;
    ProviderKind provider_;
    std::atomic<uint32_t> width_;
    std::atomic<uint32_t> height_;
    std::atomic<float> dpr_;
    std::atomic<uint32_t> status_{0};
    std::atomic<int> state_{static_cast<int>(ViewState::Created)};
    std::atomic<bool> paused_{false};
    uint64_t frameCounter_ = 0;
    render::FrameMailbox mailbox_;
    std::unique_ptr<ViewSurface> surface_;
    std::unique_ptr<IJavaScriptRuntime> js_;
    bool jsFailed_ = false;
};

} // namespace xgu
