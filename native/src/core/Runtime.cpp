#include "core/Runtime.h"

#include "render/skia/TestFrame.h"

namespace xgu {

Runtime::Runtime() : render_(std::make_unique<render::RenderSystem>(views_)) {}

Runtime::~Runtime() = default;

Runtime& Runtime::instance() {
    static Runtime* runtime = new Runtime(); // intentionally leaked: outlives static destructors
    return *runtime;
}

bool Runtime::initialize(const RuntimeInitDesc& desc) {
    if (desc.logCallback) {
        Log::setCallback(desc.logCallback, desc.logUser);
    }
    if (initialized_) {
        return true;
    }
    desc_ = desc;
    render::registerImageCodecs();
    initialized_ = true;
    XGU_LOG_INFO("xploit_game_ui %s initialized", XGU_VERSION_STRING);
    return true;
}

void Runtime::shutdown() {
    if (!initialized_) {
        return;
    }
    destroyAllViews();
    initialized_ = false;
    XGU_LOG_INFO("xploit_game_ui shut down");
}

ViewId Runtime::createView(ViewDesc desc) {
    auto view = std::make_unique<View>(std::move(desc));
    render_->prepareView(*view);
    const ViewId id = views_.add(std::move(view));
    XGU_LOG_DEBUG("view %llu created (slot %u, generation %u)", static_cast<unsigned long long>(id),
                  ViewRegistry::indexOf(id), ViewRegistry::generationOf(id));
    return id;
}

bool Runtime::destroyView(ViewId id) {
    std::unique_ptr<View> view = views_.remove(id);
    if (!view) {
        return false;
    }
    render_->destroyView(std::move(view));
    return true;
}

void Runtime::destroyAllViews() {
    for (auto& view : views_.removeAll()) {
        render_->destroyView(std::move(view));
    }
}

} // namespace xgu
