#include "css/Animator.h"

#include "css/StyleSheet.h"
#include "dom/Element.h"

#include <algorithm>
#include <cmath>

namespace xgu::css {

void Animator::setKeyframes(std::vector<KeyframesRule> keyframes) { keyframes_ = std::move(keyframes); }

void Animator::forget(const dom::Element& element) {
    states_.erase(&element);
    running_.erase(&element);
}

void Animator::clear() {
    states_.clear();
    running_.clear();
}

const KeyframesRule* Animator::findKeyframes(const Atom& name) const {
    // A later rule with the same name wins, so search backwards.
    for (auto it = keyframes_.rbegin(); it != keyframes_.rend(); ++it) {
        if (it->name == name) {
            return &*it;
        }
    }
    return nullptr;
}

void Animator::startTransitions(ElementState& state, const StyleValues& base, const StyleValues& presentation) {
    if (base.transitions.empty() || !state.hasPreviousBase) {
        return;
    }
    std::vector<PropertyId> single(1);
    for (const TransitionSpec& spec : base.transitions) {
        const std::vector<PropertyId>* properties = &animatableProperties();
        if (!spec.all) {
            single[0] = spec.property;
            properties = &single;
        }
        for (const PropertyId property : *properties) {
            if (!isAnimatable(property) || propertyEquals(property, state.previousBase, base)) {
                continue;
            }
            // Replace whatever is running on this property and start from where
            // the element is showing right now, so the move stays smooth even
            // when the target changes mid-flight.
            state.transitions.erase(std::remove_if(state.transitions.begin(), state.transitions.end(),
                                                   [property](const RunningTransition& running) {
                                                       return running.property == property;
                                                   }),
                                    state.transitions.end());
            RunningTransition running;
            running.property = property;
            running.from = presentation;
            running.to = base;
            running.startTime = now_ + spec.delaySeconds;
            running.durationSeconds = spec.durationSeconds;
            running.timing = spec.timing;
            state.transitions.push_back(std::move(running));
        }
    }
}

bool Animator::applyAnimation(ElementState& state, const StyleResolver& resolve, StyleValues& values) {
    const AnimationSpec& spec = state.animation;
    if (spec.empty()) {
        return false;
    }
    const KeyframesRule* rule = findKeyframes(spec.name);
    if (!rule || rule->steps.empty()) {
        return false;
    }

    const double elapsed = now_ - state.animationStart - spec.delaySeconds;
    const bool beforeStart = elapsed < 0.0;
    if (beforeStart && spec.fill != AnimationFillMode::Backwards && spec.fill != AnimationFillMode::Both) {
        return false;
    }

    double iteration = std::max(0.0, elapsed) / spec.durationSeconds;
    const bool infinite = spec.iterations < 0.0f;
    bool finished = false;
    if (!infinite && iteration >= spec.iterations) {
        iteration = spec.iterations;
        finished = true;
    }
    state.animationFinished = finished;
    if (finished && spec.fill != AnimationFillMode::Forwards && spec.fill != AnimationFillMode::Both) {
        return false; // over, and it leaves nothing behind
    }

    // Where inside one run we are, and which way that run goes.
    const auto wholeRuns = static_cast<long long>(std::floor(iteration));
    double progress = iteration - static_cast<double>(wholeRuns);
    if (finished && iteration > 0.0 && progress == 0.0) {
        progress = 1.0; // the end of the last run, not the start of the next
    }
    bool reversed = spec.direction == AnimationDirection::Reverse;
    if (spec.direction == AnimationDirection::Alternate) {
        reversed = (wholeRuns % 2) != 0;
    } else if (spec.direction == AnimationDirection::AlternateReverse) {
        reversed = (wholeRuns % 2) == 0;
    }
    if (reversed) {
        progress = 1.0 - progress;
    }
    const auto offset = static_cast<float>(std::clamp(progress, 0.0, 1.0));

    // The pair of keyframes around this offset.
    const std::vector<KeyframeStep>& steps = rule->steps;
    size_t upper = 0;
    while (upper < steps.size() && steps[upper].offset < offset) {
        ++upper;
    }
    const KeyframeStep* before = upper == 0 ? nullptr : &steps[upper - 1];
    const KeyframeStep* after = upper < steps.size() ? &steps[upper] : nullptr;
    if (!before && !after) {
        return false;
    }
    if (!before) {
        before = after;
    }
    if (!after) {
        after = before;
    }

    // Each keyframe goes through the cascade on top of the element's own rules,
    // so `50% { width: 50% }` resolves exactly as a normal declaration would.
    RefPtr<ComputedStyle> fromStyle = resolve(before->declarations.get());
    RefPtr<ComputedStyle> toStyle = resolve(after->declarations.get());
    if (!fromStyle || !toStyle) {
        return false;
    }

    const float span = after->offset - before->offset;
    const float local = span > 0.0f ? std::clamp((offset - before->offset) / span, 0.0f, 1.0f) : 0.0f;
    const float eased = spec.timing.evaluate(local);

    std::vector<PropertyId> touched;
    const auto collect = [&touched](const DeclarationBlock& declarations) {
        for (const Declaration& declaration : declarations) {
            if (isAnimatable(declaration.property) &&
                std::find(touched.begin(), touched.end(), declaration.property) == touched.end()) {
                touched.push_back(declaration.property);
            }
        }
    };
    collect(*before->declarations);
    collect(*after->declarations);

    for (const PropertyId property : touched) {
        blendProperty(property, *fromStyle, *toStyle, eased, values);
    }
    return !touched.empty();
}

RefPtr<ComputedStyle> Animator::apply(dom::Element& element, RefPtr<ComputedStyle> base,
                                      const StyleResolver& resolve) {
    const bool wants = !base->transitions.empty() || !base->animation.empty();
    auto it = states_.find(&element);
    if (!wants && it == states_.end()) {
        return base; // the common case: nothing animates here
    }
    if (it == states_.end()) {
        it = states_.emplace(&element, ElementState{}).first;
    }
    ElementState& state = it->second;

    // A different description restarts the animation.
    if (!(state.animation == base->animation)) {
        state.animation = base->animation;
        state.animationStart = now_;
        state.animationFinished = false;
    }

    // What the element is showing right now, before this frame's work.
    const StyleValues presentation = element.computedStyle() ? *element.computedStyle() : *base;
    startTransitions(state, *base, presentation);
    state.previousBase = *base;
    state.hasPreviousBase = true;

    StyleValues values = *base;
    bool changed = applyAnimation(state, resolve, values);

    // Transitions sit on top of the animation and of the cascade.
    for (auto running = state.transitions.begin(); running != state.transitions.end();) {
        const double elapsed = now_ - running->startTime;
        if (elapsed < 0.0) {
            // Still inside the delay: hold the value it started from.
            blendProperty(running->property, running->from, running->from, 0.0f, values);
            changed = true;
            ++running;
            continue;
        }
        const float linear = running->durationSeconds > 0.0
                                 ? static_cast<float>(std::min(1.0, elapsed / running->durationSeconds))
                                 : 1.0f;
        blendProperty(running->property, running->from, running->to, running->timing.evaluate(linear), values);
        if (linear >= 1.0f) {
            running = state.transitions.erase(running);
            continue;
        }
        changed = true;
        ++running;
    }

    const bool stillRunning =
        !state.transitions.empty() || (!state.animation.empty() && !state.animationFinished);
    if (stillRunning) {
        running_.insert(&element);
    } else {
        running_.erase(&element);
        // The state holds the previous base style, which is what tells a
        // transition that something moved. Keep it for as long as the element
        // declares one, and only forget elements that animate nothing.
        if (base->transitions.empty() && base->animation.empty()) {
            states_.erase(it);
        }
    }

    if (!changed) {
        return base;
    }
    auto result = makeRef<ComputedStyle>();
    result->setValues(values);
    return result;
}

} // namespace xgu::css
