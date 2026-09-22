#pragma once

#include "core/Atom.h"
#include "css/Properties.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace xgu::css {

struct StyleValues;
struct Declaration;

// --- timing ------------------------------------------------------------------

enum class TimingKind : uint8_t { Linear, Ease, EaseIn, EaseOut, EaseInOut, CubicBezier };

// A CSS easing curve. Everything but `linear` is a cubic Bézier, so the named
// ones just carry their control points.
struct TimingFunction {
    TimingKind kind = TimingKind::Ease;
    float x1 = 0.25f;
    float y1 = 0.1f;
    float x2 = 0.25f;
    float y2 = 1.0f;

    // Maps progress 0..1 through the curve.
    float evaluate(float t) const;

    bool operator==(const TimingFunction& other) const {
        return kind == other.kind && x1 == other.x1 && y1 == other.y1 && x2 == other.x2 && y2 == other.y2;
    }
};

// --- transitions -------------------------------------------------------------

struct TransitionSpec {
    // Which property to animate; `all` covers every animatable one.
    PropertyId property = PropertyId::Invalid;
    bool all = false;
    float durationSeconds = 0.0f;
    float delaySeconds = 0.0f;
    TimingFunction timing;

    bool operator==(const TransitionSpec& other) const {
        return property == other.property && all == other.all && durationSeconds == other.durationSeconds &&
               delaySeconds == other.delaySeconds && timing == other.timing;
    }
};

// --- animations --------------------------------------------------------------

enum class AnimationDirection : uint8_t { Normal, Reverse, Alternate, AlternateReverse };
enum class AnimationFillMode : uint8_t { None, Forwards, Backwards, Both };

struct AnimationSpec {
    // Empty name means no animation.
    Atom name;
    float durationSeconds = 0.0f;
    float delaySeconds = 0.0f;
    // Negative means infinite.
    float iterations = 1.0f;
    AnimationDirection direction = AnimationDirection::Normal;
    AnimationFillMode fill = AnimationFillMode::None;
    TimingFunction timing;

    bool empty() const { return name.view().empty() || durationSeconds <= 0.0f; }

    bool operator==(const AnimationSpec& other) const {
        return name == other.name && durationSeconds == other.durationSeconds &&
               delaySeconds == other.delaySeconds && iterations == other.iterations &&
               direction == other.direction && fill == other.fill && timing == other.timing;
    }
};

// --- @keyframes --------------------------------------------------------------

struct KeyframeStep {
    // 0..1, from "0%"/"from" to "100%"/"to".
    float offset = 0.0f;
    std::shared_ptr<const std::vector<Declaration>> declarations;
};

struct KeyframesRule {
    Atom name;
    // Sorted by offset.
    std::vector<KeyframeStep> steps;
};

// --- interpolation -----------------------------------------------------------

// True when the engine can animate this property. Everything else jumps to its
// new value at the end of the duration instead of sliding.
bool isAnimatable(PropertyId property);

// Writes `from` blended towards `to` by `t` (0..1) into `out` for one property.
// Returns false when the property is not animatable, leaving `out` untouched.
bool blendProperty(PropertyId property, const StyleValues& from, const StyleValues& to, float t, StyleValues& out);

// Copies one property's value across, used to apply a keyframe that the
// animation holds at its start or end.
void copyProperty(PropertyId property, const StyleValues& from, StyleValues& out);

// True when the two styles hold the same value for this property. Comparing the
// whole StyleValues would be far too coarse for deciding what to transition.
bool propertyEquals(PropertyId property, const StyleValues& a, const StyleValues& b);

// Every property the engine can animate, for `transition: all`.
const std::vector<PropertyId>& animatableProperties();

} // namespace xgu::css
