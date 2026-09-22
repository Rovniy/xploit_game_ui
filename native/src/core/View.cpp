#include "core/View.h"

namespace xgu {

View::View(ViewDesc desc)
    : desc_(std::move(desc)), provider_(desc_.provider), width_(desc_.width), height_(desc_.height),
      dpr_(desc_.devicePixelRatio) {}

View::~View() = default;

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

} // namespace xgu
