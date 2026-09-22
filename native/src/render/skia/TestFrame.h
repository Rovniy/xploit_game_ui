#pragma once

#include "render/DisplayList.h"

namespace xgu::render {

// Stage 1 spike: a built-in picture exercising the paths the HTML renderer will
// use (anti-aliased rounded rects, blur shadow, gradient, PNG encode/decode
// round trip, skparagraph text). Recorded in device pixels for `widthPx` x
// `heightPx`; content is laid out in CSS pixels scaled by `dpr`.
DisplayList recordTestFrame(int widthPx, int heightPx, float dpr, uint64_t frameId);

// Registers Skia's PNG/JPEG/WebP decoders. Idempotent.
void registerImageCodecs();

} // namespace xgu::render
