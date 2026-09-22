#include "paint/ImageCache.h"

#include "core/AssetLoader.h"
#include "core/Log.h"

#include <include/codec/SkCodec.h>
#include <include/core/SkData.h>

namespace xgu::paint {

ImageCache& ImageCache::instance() {
    static ImageCache* cache = new ImageCache(); // outlives static destructors
    return *cache;
}

sk_sp<SkImage> ImageCache::get(const IAssetLoader& loader, std::string_view baseRelative,
                               std::string_view reference) {
    if (reference.empty()) {
        return nullptr;
    }
    const std::optional<std::string> resolved = loader.resolve(baseRelative, reference);
    if (!resolved) {
        XGU_LOG_ERROR("image: \"%.*s\" was rejected (outside the UI root)", static_cast<int>(reference.size()),
                      reference.data());
        return nullptr;
    }
    return getResolved(loader, *resolved);
}

sk_sp<SkImage> ImageCache::getResolved(const IAssetLoader& loader, const std::string& resolvedPath) {
    {
        std::lock_guard lock(mutex_);
        if (const auto it = images_.find(resolvedPath); it != images_.end()) {
            return it->second;
        }
    }

    sk_sp<SkImage> image;
    if (const std::optional<std::string> bytes = loader.read(resolvedPath)) {
        sk_sp<SkData> data = SkData::MakeWithCopy(bytes->data(), bytes->size());
        std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(std::move(data));
        if (!codec) {
            XGU_LOG_ERROR("image: \"%s\" is not a supported format (PNG, JPEG and WebP are)", resolvedPath.c_str());
        } else {
            auto [decoded, result] = codec->getImage();
            if (result != SkCodec::kSuccess || !decoded) {
                XGU_LOG_ERROR("image: cannot decode \"%s\" (codec result %d)", resolvedPath.c_str(),
                              static_cast<int>(result));
            } else {
                image = std::move(decoded);
            }
        }
    } else {
        XGU_LOG_ERROR("image: cannot read \"%s\"", resolvedPath.c_str());
    }

    std::lock_guard lock(mutex_);
    images_[resolvedPath] = image; // caches failures as null
    return image;
}

void ImageCache::clear() {
    std::lock_guard lock(mutex_);
    images_.clear();
}

size_t ImageCache::size() const {
    std::lock_guard lock(mutex_);
    return images_.size();
}

} // namespace xgu::paint
