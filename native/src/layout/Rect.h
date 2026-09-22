#pragma once

#include <algorithm>

namespace xgu::layout {

// An axis-aligned rectangle in CSS pixels. Its own header because both the box
// tree and the inline text layer speak in rectangles, and they must not include
// each other.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    float right() const { return x + width; }
    float bottom() const { return y + height; }
    bool isEmpty() const { return width <= 0.0f || height <= 0.0f; }
    bool contains(float px, float py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
    Rect inset(float top, float right, float bottom, float left) const {
        return Rect{x + left, y + top, std::max(0.0f, width - left - right),
                    std::max(0.0f, height - top - bottom)};
    }
    bool operator==(const Rect& other) const {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }
};

} // namespace xgu::layout
