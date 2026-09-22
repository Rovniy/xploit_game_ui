#pragma once

#include <include/core/SkPicture.h>
#include <include/core/SkRect.h>
#include <include/core/SkRefCnt.h>
#include <include/core/SkSize.h>

#include <cstdint>

namespace xgu::render {

// Output of the paint stage for one frame. Immutable once published; safe to
// hand across threads because SkPicture is immutable and ref-counted.
struct DisplayList {
    sk_sp<SkPicture> picture;
    SkIRect dirtyPx = SkIRect::MakeEmpty(); // device pixels; full viewport in the MVP
    SkISize sizePx = SkISize::MakeEmpty(); // viewport in device pixels
    float devicePixelRatio = 1.0f;
    uint64_t frameId = 0;

    bool valid() const { return picture != nullptr && !sizePx.isEmpty(); }
};

} // namespace xgu::render
