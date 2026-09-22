#pragma once

#include "core/Log.h"
#include "core/RuntimeThread.h"
#include "core/View.h"
#include "core/ViewRegistry.h"
#include "render/RenderSystem.h"

#include <memory>
#include <string>

namespace xgu {

struct RuntimeInitDesc {
    LogCallback logCallback = nullptr;
    void* logUser = nullptr;
    std::string dataDir;
    bool singleThreaded = false; // run the runtime inline on the calling thread (tests, CLI)
};

// Process-wide singleton. Exists from first use so the Unity plugin can
// configure the graphics device before C# calls xgu_initialize.
class Runtime {
public:
    static Runtime& instance();

    bool initialize(const RuntimeInitDesc& desc);
    void shutdown();
    bool initialized() const { return initialized_; }

    ViewRegistry& views() { return views_; }
    render::RenderSystem& render() { return *render_; }
    RuntimeThread& thread() { return thread_; }

    // --- main thread ---------------------------------------------------------
    ViewId createView(ViewDesc desc);
    bool destroyView(ViewId id);
    void destroyAllViews();
    // Runs the script on the runtime thread; returns false for unknown views.
    bool executeJavaScript(ViewId id, std::string source, std::string origin);
    // Document loading, also on the runtime thread.
    bool loadDocument(ViewId id, std::string relativePath);
    bool loadHtml(ViewId id, std::string html, std::string basePath);
    bool reloadDocument(ViewId id);
    // Forces one repaint; normally tick() does this for views that changed.
    bool repaintView(ViewId id);
    // Advances all views by one frame (JS message loop, later timers/rAF/layout).
    void tick(double timeSeconds);

private:
    Runtime();
    ~Runtime();

    void onTick(double timeSeconds);              // runtime thread
    void disposeView(std::unique_ptr<View> view); // runtime thread

    bool initialized_ = false;
    RuntimeInitDesc desc_;
    ViewRegistry views_;
    std::unique_ptr<render::RenderSystem> render_;
    RuntimeThread thread_;
};

} // namespace xgu
