#pragma once

#include "core/Log.h"
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

    ViewId createView(ViewDesc desc);
    bool destroyView(ViewId id);
    void destroyAllViews();

private:
    Runtime();
    ~Runtime();

    bool initialized_ = false;
    RuntimeInitDesc desc_;
    ViewRegistry views_;
    std::unique_ptr<render::RenderSystem> render_;
};

} // namespace xgu
