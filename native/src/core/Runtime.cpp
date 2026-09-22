#include "core/Runtime.h"

#include "js/v8/V8Platform.h"
#include "js/v8/V8Runtime.h"
#include "render/skia/TestFrame.h"

#include <vector>

namespace xgu {

Runtime::Runtime() : render_(std::make_unique<render::RenderSystem>(views_)) {
    thread_.setTickHandler([this](double time) { onTick(time); });
    View::setJavaScriptRuntimeFactory(&js::V8Runtime::create);
}

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
    thread_.start(desc.singleThreaded);
    // Initialise V8 eagerly on the runtime thread so failures show up at start-up.
    const std::string dataDir = desc.dataDir;
    thread_.post([dataDir] { js::V8Platform::instance().ensureInitialized(dataDir); });
    initialized_ = true;
    XGU_LOG_INFO("xploit_game_ui %s initialized (%s)", XGU_VERSION_STRING,
                 desc.singleThreaded ? "single-threaded" : "runtime thread");
    return true;
}

void Runtime::shutdown() {
    if (!initialized_) {
        return;
    }
    destroyAllViews();
    thread_.stop(); // drains the posted view disposals
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

void Runtime::disposeView(std::unique_ptr<View> view) {
    if (!view) {
        return;
    }
    view->setState(ViewState::Destroyed);
    view->disposeJavaScript();
    render_->destroyView(std::move(view));
}

bool Runtime::destroyView(ViewId id) {
    std::unique_ptr<View> view = views_.remove(id);
    if (!view) {
        return false;
    }
    // The JS isolate lives on the runtime thread; dispose it there. The view is
    // already unreachable from the registry, so no new work can target it.
    View* raw = view.release();
    thread_.post([this, raw] { disposeView(std::unique_ptr<View>(raw)); });
    return true;
}

void Runtime::destroyAllViews() {
    for (auto& view : views_.removeAll()) {
        View* raw = view.release();
        thread_.post([this, raw] { disposeView(std::unique_ptr<View>(raw)); });
    }
}

bool Runtime::executeJavaScript(ViewId id, std::string source, std::string origin) {
    if (!views_.resolve(id)) {
        return false;
    }
    thread_.post([this, id, source = std::move(source), origin = std::move(origin)] {
        // Resolved without holding the registry lock during evaluation: views are
        // only deleted by tasks on this same thread, so the pointer stays valid.
        View* view = views_.resolve(id);
        if (!view) {
            return;
        }
        if (IJavaScriptRuntime* js = view->ensureJavaScript()) {
            js->evaluate(source, origin);
        }
    });
    return true;
}

bool Runtime::loadDocument(ViewId id, std::string relativePath) {
    if (!views_.resolve(id)) {
        return false;
    }
    thread_.post([this, id, relativePath = std::move(relativePath)] {
        if (View* view = views_.resolve(id)) {
            view->loadDocument(relativePath);
            // Loading paints the first frame; the software provider
            // rasterises it here, GPU providers on the host's render event.
            render_->paintIfCpu(*view);
        }
    });
    return true;
}

bool Runtime::loadHtml(ViewId id, std::string html, std::string basePath) {
    if (!views_.resolve(id)) {
        return false;
    }
    thread_.post([this, id, html = std::move(html), basePath = std::move(basePath)] {
        if (View* view = views_.resolve(id)) {
            view->loadHtml(html, basePath);
            // Loading paints the first frame; the software provider
            // rasterises it here, GPU providers on the host's render event.
            render_->paintIfCpu(*view);
        }
    });
    return true;
}

bool Runtime::reloadDocument(ViewId id) {
    if (!views_.resolve(id)) {
        return false;
    }
    thread_.post([this, id] {
        if (View* view = views_.resolve(id)) {
            view->reload();
            // Loading paints the first frame; the software provider
            // rasterises it here, GPU providers on the host's render event.
            render_->paintIfCpu(*view);
        }
    });
    return true;
}

bool Runtime::repaintView(ViewId id) {
    if (!views_.resolve(id)) {
        return false;
    }
    thread_.post([this, id] {
        if (View* view = views_.resolve(id)) {
            if (view->updateAndPaint()) {
                // GPU providers wait for the host's render event; the software
                // provider rasterises right here.
                render_->paintIfCpu(*view);
            }
        }
    });
    return true;
}

void Runtime::tick(double timeSeconds) {
    if (!initialized_) {
        return;
    }
    thread_.requestTick(timeSeconds);
}

void Runtime::onTick(double timeSeconds) {
    std::vector<ViewId> ids;
    views_.forEach([&ids](ViewId id, View&) { ids.push_back(id); });
    for (ViewId id : ids) {
        View* view = views_.resolve(id);
        if (!view || view->paused()) {
            continue;
        }
        if (IJavaScriptRuntime* js = view->javaScript()) {
            js->tick(timeSeconds);
        }
        // Repaint when the document changed since the last frame. Style and
        // layout decide that by looking at the dirty bits they were given.
        if (view->updateAndPaint()) {
            render_->paintIfCpu(*view);
        }
    }
}

} // namespace xgu
