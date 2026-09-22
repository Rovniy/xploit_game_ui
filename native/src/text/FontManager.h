#pragma once

#include <include/core/SkRefCnt.h>

#include <memory>
#include <string>

namespace skia::textlayout {
class FontCollection;
}
class SkUnicode;

namespace xgu::text {

// Fonts available to the engine: the system fonts plus any font files inside
// the view's UI root ("<uiRoot>/fonts"). One instance per process; tests can
// switch to a deterministic test-only collection.
class FontManager {
public:
    static FontManager& instance();

    // Adds a directory of font files (ttf/otf) to the asset font manager.
    // Call once per UI root; later calls with the same path are ignored.
    void addFontDirectory(const std::string& directory);

    // Uses only the fonts in `directory`, with fallback disabled, so text
    // measurements do not depend on the machine's installed fonts.
    void useTestFontsOnly(const std::string& directory);

    sk_sp<skia::textlayout::FontCollection> fontCollection();
    sk_sp<SkUnicode> unicode();

    // Family used when a document asks for something unavailable.
    const std::string& defaultFamily() const { return defaultFamily_; }

private:
    FontManager() = default;
    void ensureCollection();

    sk_sp<skia::textlayout::FontCollection> collection_;
    sk_sp<SkUnicode> unicode_;
    std::string defaultFamily_ = "Segoe UI";
    std::string assetDirectory_;
    bool testMode_ = false;
};

} // namespace xgu::text
