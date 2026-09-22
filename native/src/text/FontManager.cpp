#include "text/FontManager.h"

#include "core/Log.h"

#include <include/core/SkFontMgr.h>
#include <include/ports/SkFontMgr_data.h>
#include <include/ports/SkFontMgr_directory.h>
#include <include/ports/SkTypeface_win.h>
#include <modules/skparagraph/include/FontCollection.h>
#include <modules/skunicode/include/SkUnicode_icu.h>

namespace xgu::text {

FontManager& FontManager::instance() {
    static FontManager* manager = new FontManager(); // outlives static destructors
    return *manager;
}

void FontManager::ensureCollection() {
    if (collection_) {
        return;
    }
    collection_ = sk_make_sp<skia::textlayout::FontCollection>();

    if (!testMode_) {
        sk_sp<SkFontMgr> systemFonts = SkFontMgr_New_DirectWrite();
        if (!systemFonts) {
            XGU_LOG_WARNING("text: DirectWrite font manager unavailable; only asset fonts will be used");
        }
        collection_->setDefaultFontManager(systemFonts, defaultFamily_.c_str());
        collection_->enableFontFallback();
    }
    if (!assetDirectory_.empty()) {
        sk_sp<SkFontMgr> assetFonts = SkFontMgr_New_Custom_Directory(assetDirectory_.c_str());
        if (assetFonts) {
            if (testMode_) {
                collection_->setDefaultFontManager(assetFonts, defaultFamily_.c_str());
                collection_->setTestFontManager(assetFonts);
                collection_->disableFontFallback();
            } else {
                collection_->setAssetFontManager(assetFonts);
            }
        } else {
            XGU_LOG_WARNING("text: cannot read fonts from %s", assetDirectory_.c_str());
        }
    }
}

void FontManager::addFontDirectory(const std::string& directory) {
    if (directory.empty() || directory == assetDirectory_) {
        return;
    }
    assetDirectory_ = directory;
    collection_.reset(); // rebuilt on next use
}

void FontManager::useTestFontsOnly(const std::string& directory) {
    testMode_ = true;
    assetDirectory_ = directory;
    collection_.reset();
}

sk_sp<skia::textlayout::FontCollection> FontManager::fontCollection() {
    ensureCollection();
    return collection_;
}

sk_sp<SkUnicode> FontManager::unicode() {
    if (!unicode_) {
        unicode_ = SkUnicodes::ICU::Make();
        if (!unicode_) {
            XGU_LOG_ERROR("text: SkUnicodes::ICU::Make failed; text layout will not work");
        }
    }
    return unicode_;
}

} // namespace xgu::text
