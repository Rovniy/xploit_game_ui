#include "core/View.h"

#include "core/Log.h"
#include "css/StyleEngine.h"
#include "dom/Document.h"
#include "dom/Element.h"
#include "html/LexborHtmlParser.h"
#include "js/v8/V8Runtime.h"
#include "input/InputRouter.h"

#include <chrono>
#include "layout/LayoutEngine.h"
#include "paint/Painter.h"
#include "text/FontManager.h"

#include <vector>

namespace xgu {
namespace {

JavaScriptRuntimeFactory& jsFactory() {
    static JavaScriptRuntimeFactory factory;
    return factory;
}

} // namespace

View::View(ViewDesc desc)
    : desc_(std::move(desc)), provider_(desc_.provider), width_(desc_.width), height_(desc_.height),
      dpr_(desc_.devicePixelRatio) {
    if (!desc_.uiRoot.empty()) {
        assetLoader_ = std::make_unique<FileAssetLoader>(desc_.uiRoot);
        // Fonts shipped with the UI live next to it.
        text::FontManager::instance().addFontDirectory(desc_.uiRoot + "/fonts");
    }
}

View::~View() {
    disposeJavaScript();
    inputRouter_.reset();
    painter_.reset();
    layoutEngine_.reset();
    styleEngine_.reset();
    document_.reset();
}

void View::requestResize(uint32_t width, uint32_t height, float dpr) {
    const bool changed = width_.load(std::memory_order_acquire) != width ||
                         height_.load(std::memory_order_acquire) != height ||
                         dpr_.load(std::memory_order_acquire) != dpr;
    width_.store(width, std::memory_order_release);
    height_.store(height, std::memory_order_release);
    dpr_.store(dpr, std::memory_order_release);
    if (changed) {
        // The document has not changed, but the surface has, so the next frame
        // must be produced even though nothing is dirty.
        frameInvalid_ = true;
    }
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
    // Every isolate starts with `document` bound to this view's document.
    if (auto* v8Runtime = dynamic_cast<js::V8Runtime*>(js_.get())) {
        v8Runtime->installDom(ensureDocument());
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

dom::Document& View::ensureDocument() {
    if (!document_) {
        document_ = makeRef<dom::Document>();
        document_->setAssetLoader(assetLoader_.get());
        styleEngine_ = std::make_unique<css::StyleEngine>(*document_);
        layoutEngine_ = std::make_unique<layout::LayoutEngine>(*document_);
        painter_ = std::make_unique<paint::Painter>(*document_, *layoutEngine_);
        inputRouter_ = std::make_unique<input::InputRouter>(*document_, *layoutEngine_);
        document_->setElementStateProvider(inputRouter_.get());
        document_->setFocusController(inputRouter_.get());
        document_->setBoxProvider(layoutEngine_.get());
    }
    return *document_;
}

bool View::updateStyleAndLayout() {
    if (!document_ || !styleEngine_ || !layoutEngine_ || !document_->documentElement()) {
        return false;
    }
    const float dpr = devicePixelRatio() > 0.0f ? devicePixelRatio() : 1.0f;
    // Layout works in CSS pixels; the painter scales to device pixels.
    const float cssWidth = static_cast<float>(width()) / dpr;
    const float cssHeight = static_cast<float>(height()) / dpr;

    const auto styleStart = std::chrono::steady_clock::now();
    styleEngine_->recalcStyles(cssWidth, cssHeight, frameTime_);
    const auto layoutStart = std::chrono::steady_clock::now();
    layoutEngine_->layout(cssWidth, cssHeight, dpr);
    const auto layoutEnd = std::chrono::steady_clock::now();

    stats_.styleMs = std::chrono::duration<double, std::milli>(layoutStart - styleStart).count();
    stats_.layoutMs = std::chrono::duration<double, std::milli>(layoutEnd - layoutStart).count();
    return true;
}

bool View::loadDocument(std::string_view relativePath) {
    const LogViewScope logScope(id_);
    if (!assetLoader_) {
        XGU_LOG_ERROR("view \"%s\": no UI root configured; cannot load \"%.*s\"", desc_.name.c_str(),
                      static_cast<int>(relativePath.size()), relativePath.data());
        return false;
    }
    // The path is relative to the UI root, so resolve it against the root itself.
    const std::optional<std::string> resolved = assetLoader_->resolve({}, relativePath);
    if (!resolved) {
        XGU_LOG_ERROR("view \"%s\": load of \"%.*s\" was rejected (outside the UI root)", desc_.name.c_str(),
                      static_cast<int>(relativePath.size()), relativePath.data());
        return false;
    }
    const std::optional<std::string> html = assetLoader_->read(*resolved);
    if (!html) {
        XGU_LOG_ERROR("view \"%s\": cannot read \"%s\" under %s", desc_.name.c_str(), resolved->c_str(),
                      assetLoader_->root().c_str());
        return false;
    }
    loadedPath_ = *resolved;
    return loadHtml(*html, *resolved);
}

bool View::loadHtml(std::string_view html, std::string_view baseRelative) {
    const LogViewScope logScope(id_);
    setState(ViewState::Loading);
    resetDocument();
    dom::Document& document = ensureDocument();
    document.setUrl(std::string(baseRelative));

    html::LexborHtmlParser parser;
    if (!parser.parseDocument(html, document)) {
        XGU_LOG_ERROR("view \"%s\": failed to parse \"%.*s\"", desc_.name.c_str(),
                      static_cast<int>(baseRelative.size()), baseRelative.data());
        return false;
    }
    setState(ViewState::DomReady);
    XGU_LOG_DEBUG("view \"%s\": DOM ready (%.*s)", desc_.name.c_str(), static_cast<int>(baseRelative.size()),
                  baseRelative.data());

    // <style> blocks and <link rel=stylesheet> are collected before scripts run,
    // so scripts already see the styled tree.
    styleEngine_->reloadStyleSheets();
    updateStyleAndLayout();

    // Creating the runtime also binds `document`.
    if (ensureJavaScript()) {
        runDocumentScripts();
    }
    setState(ViewState::JsReady);
    // Scripts may have changed the DOM; restyle, lay out and record a frame.
    updateAndPaint();
    return true;
}

void View::pumpBridge() {
    IJavaScriptRuntime* js = javaScript();
    if (!js || !js->ready()) {
        return; // the messages stay queued until a document is running
    }
    std::vector<BridgeMessage> messages;
    bridge_.drainToPage(messages);
    for (const BridgeMessage& message : messages) {
        js->deliverBridgeMessage(message);
    }
}

bool View::sendInput(const input::InputEvent& event) {
    if (!inputRouter_) {
        return false;
    }
    return inputRouter_->handle(event);
}

bool View::updateAndPaint() {
    if (!document_ || !painter_) {
        ++stats_.skipped;
        return false;
    }
    // An idle document costs nothing: no restyle, no layout, no recording and
    // no rasterising. Animations keep asking for frames on their own.
    const bool animating = styleEngine_ && styleEngine_->hasRunningAnimations();
    if (!frameInvalid_ && !document_->dirty() && !animating) {
        ++stats_.skipped;
        return false;
    }
    if (!updateStyleAndLayout()) {
        ++stats_.skipped;
        return false;
    }
    const float dpr = devicePixelRatio() > 0.0f ? devicePixelRatio() : 1.0f;
    const auto paintStart = std::chrono::steady_clock::now();
    render::DisplayList frame = painter_->paint(static_cast<int>(width()), static_cast<int>(height()), dpr,
                                                nextFrameId(), frameInvalid_);
    stats_.paintMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - paintStart).count();
    if (!frame.valid()) {
        ++stats_.skipped;
        return false;
    }
    ++stats_.published;
    stats_.damageX = frame.dirtyPx.left();
    stats_.damageY = frame.dirtyPx.top();
    stats_.damageWidth = frame.dirtyPx.width();
    stats_.damageHeight = frame.dirtyPx.height();
    frameInvalid_ = false;
    document_->clearDirtyFlag();
    mailbox_.publish(std::move(frame));
    if (state() == ViewState::JsReady) {
        setState(ViewState::Interactive);
    }
    return true;
}

void View::resetDocument() {
    // A new document must not inherit the previous one's globals, timers, event
    // listeners or DOM, so everything that belongs to a document goes at once.
    // The bridge survives: the host registers its handlers on the view, not on
    // whatever page happens to be loaded.
    disposeJavaScript();
    jsFailed_ = false;
    inputRouter_.reset();
    painter_.reset();
    layoutEngine_.reset();
    styleEngine_.reset();
    document_.reset();
}

bool View::reload() {
    const LogViewScope logScope(id_);
    if (loadedPath_.empty()) {
        return false;
    }
    const std::string path = loadedPath_;
    return loadDocument(path);
}

void View::runDocumentScripts() {
    dom::Document* document = document_.get();
    if (!document || !js_) {
        return;
    }
    // Collect first: running a script may mutate the tree.
    struct PendingScript {
        std::string source;
        std::string origin;
    };
    std::vector<PendingScript> scripts;
    const Atom srcAttribute("src");
    const Atom typeAttribute("type");

    for (dom::Node* node = document; node; node = dom::nextInTreeOrder(node, document)) {
        if (!node->isElement()) {
            continue;
        }
        auto& element = static_cast<dom::Element&>(*node);
        if (element.knownTag() != html::HtmlTag::Script) {
            continue;
        }
        if (const std::string* type = element.getAttribute(typeAttribute)) {
            const Atom typeAtom(*type);
            if (!type->empty() && !typeAtom.equalsIgnoringCase("text/javascript") &&
                !typeAtom.equalsIgnoringCase("application/javascript")) {
                if (typeAtom.equalsIgnoringCase("module")) {
                    XGU_LOG_WARNING("view \"%s\": <script type=\"module\"> is not supported yet; skipped",
                                    desc_.name.c_str());
                }
                continue;
            }
        }
        if (const std::string* src = element.getAttribute(srcAttribute)) {
            if (!assetLoader_) {
                XGU_LOG_ERROR("view \"%s\": <script src> needs a UI root", desc_.name.c_str());
                continue;
            }
            const std::optional<std::string> resolved = assetLoader_->resolve(document->url(), *src);
            if (!resolved) {
                XGU_LOG_ERROR("view \"%s\": <script src=\"%s\"> was rejected", desc_.name.c_str(), src->c_str());
                continue;
            }
            const std::optional<std::string> source = assetLoader_->read(*resolved);
            if (!source) {
                XGU_LOG_ERROR("view \"%s\": cannot read script \"%s\"", desc_.name.c_str(), resolved->c_str());
                continue;
            }
            scripts.push_back(PendingScript{*source, *resolved});
        } else {
            std::string source = element.textContent();
            if (!source.empty()) {
                scripts.push_back(PendingScript{std::move(source), document->url() + " (inline)"});
            }
        }
    }

    for (const PendingScript& script : scripts) {
        js_->evaluate(script.source, script.origin);
    }
}

} // namespace xgu
