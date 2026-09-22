#include "core/View.h"

#include "core/Log.h"

namespace xgu {
namespace {

JavaScriptRuntimeFactory& jsFactory() {
    static JavaScriptRuntimeFactory factory;
    return factory;
}

} // namespace

View::View(ViewDesc desc)
    : desc_(std::move(desc)), provider_(desc_.provider), width_(desc_.width), height_(desc_.height),
      dpr_(desc_.devicePixelRatio) {}

View::~View() { disposeJavaScript(); }

void View::requestResize(uint32_t width, uint32_t height, float dpr) {
    width_.store(width, std::memory_order_release);
    height_.store(height, std::memory_order_release);
    dpr_.store(dpr, std::memory_order_release);
}

uint32_t View::readStatus(uint32_t clearMask) {
    uint32_t current = status_.load(std::memory_order_acquire);
    if (clearMask != 0) {
        status_.fetch_and(~clearMask, std::memory_order_acq_rel);
    }
    return current;
}

void View::setJavaScriptRuntimeFactory(JavaScriptRuntimeFactory factory) { jsFactory() = std::move(factory); }

IJavaScriptRuntime* View::ensureJavaScript() {
    if (js_) {
        return js_->ready() ? js_.get() : nullptr;
    }
    if (jsFailed_) {
        return nullptr;
    }
    JavaScriptRuntimeFactory& factory = jsFactory();
    if (!factory) {
        XGU_LOG_ERROR("view \"%s\": no JavaScript runtime factory installed", desc_.name.c_str());
        jsFailed_ = true;
        return nullptr;
    }
    js_ = factory(*this);
    if (!js_ || !js_->initialize()) {
        XGU_LOG_ERROR("view \"%s\": JavaScript runtime initialisation failed", desc_.name.c_str());
        js_.reset();
        jsFailed_ = true;
        return nullptr;
    }
    if (state() == ViewState::Created) {
        setState(ViewState::JsReady);
    }
    return js_.get();
}

void View::disposeJavaScript() {
    if (js_) {
        js_->dispose();
        js_.reset();
    }
}

} // namespace xgu
