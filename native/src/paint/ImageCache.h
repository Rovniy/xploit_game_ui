#pragma once

#include <include/core/SkImage.h>
#include <include/core/SkRefCnt.h>

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace xgu {
class IAssetLoader;
}

namespace xgu::paint {

// Decoded images, keyed by the root-relative path they were loaded from.
//
// Decoding is synchronous on first use: UI images are small and the runtime
// thread owns both layout and paint, so a background decoder would only add
// ordering problems for the MVP (Stage 10 revisits this).
class ImageCache {
public:
    static ImageCache& instance();

    // Resolves `reference` against `baseRelative` through `loader`, decodes it
    // and caches the result. Returns nullptr when the path is refused, missing
    // or not a supported format; the failure is cached too, so a broken <img>
    // is not retried on every frame.
    sk_sp<SkImage> get(const IAssetLoader& loader, std::string_view baseRelative, std::string_view reference);

    // Already-resolved root-relative path.
    sk_sp<SkImage> getResolved(const IAssetLoader& loader, const std::string& resolvedPath);

    void clear();
    size_t size() const;

private:
    ImageCache() = default;

    mutable std::mutex mutex_;
    // A null entry means "tried and failed".
    std::unordered_map<std::string, sk_sp<SkImage>> images_;
};

} // namespace xgu::paint
