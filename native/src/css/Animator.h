#pragma once

#include "core/RefCounted.h"
#include "css/Animation.h"
#include "css/ComputedStyle.h"

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xgu::dom {
class Element;
}

namespace xgu::css {

struct Declaration;
using DeclarationBlock = std::vector<Declaration>;

// Runs CSS transitions and @keyframes animations.
//
// The cascade produces a "base" style for an element; this turns it into the
// style actually used this frame, blending towards the base where a transition
// is running and applying keyframes where an animation is. Layout and paint
// never learn that anything is animating.
//
// Keyframes are resolved by asking the caller to run the cascade again with the
// keyframe's declarations appended, so units, `currentColor` and inheritance all
// behave exactly as they do anywhere else.
class Animator {
public:
    // Runs the cascade for the element with `extra` appended on top, or without
    // it when `extra` is null.
    using StyleResolver = std::function<RefPtr<ComputedStyle>(const DeclarationBlock* extra)>;

    // Replaces the keyframes animations can name.
    void setKeyframes(std::vector<KeyframesRule> keyframes);
    bool hasKeyframes() const { return !keyframes_.empty(); }

    // Advances the clock. Times come from the host, the same ones the timers use.
    void setTime(double seconds) { now_ = seconds; }
    double time() const { return now_; }

    // The style to use for `element` this frame; may be `base` itself.
    RefPtr<ComputedStyle> apply(dom::Element& element, RefPtr<ComputedStyle> base, const StyleResolver& resolve);

    // True when something is still moving, so the next frame has to recompute.
    bool hasRunning() const { return !running_.empty(); }
    bool isAnimating(const dom::Element& element) const { return running_.count(&element) != 0; }

    // Drops the state of an element that left the tree.
    void forget(const dom::Element& element);
    void clear();

private:
    struct RunningTransition {
        PropertyId property = PropertyId::Invalid;
        // Whole styles, because one property can live in several fields.
        StyleValues from;
        StyleValues to;
        double startTime = 0.0;
        double durationSeconds = 0.0;
        TimingFunction timing;
    };

    struct ElementState {
        // What the cascade said last time, to notice what moved.
        StyleValues previousBase;
        bool hasPreviousBase = false;
        std::vector<RunningTransition> transitions;
        AnimationSpec animation;
        double animationStart = 0.0;
        bool animationFinished = false;
    };

    const KeyframesRule* findKeyframes(const Atom& name) const;
    bool applyAnimation(ElementState& state, const StyleResolver& resolve, StyleValues& values);
    void startTransitions(ElementState& state, const StyleValues& base, const StyleValues& presentation);

    std::vector<KeyframesRule> keyframes_;
    std::unordered_map<const dom::Element*, ElementState> states_;
    std::unordered_set<const dom::Element*> running_;
    double now_ = 0.0;
};

} // namespace xgu::css
