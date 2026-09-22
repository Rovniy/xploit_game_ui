#pragma once

#include "css/ComputedStyle.h"
#include "css/SelectorMatcher.h"
#include "css/StyleSheet.h"
#include "dom/Document.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace xgu::css {

// Buckets the rules of all active stylesheets by the rightmost compound so a
// candidate lookup touches a handful of rules instead of all of them.
class RuleIndex {
public:
    void add(const StyleRule& rule);
    void clear();

    // Appends the rules that could match `element` (still needs full matching).
    void collect(const dom::Element& element, std::vector<const StyleRule*>& out) const;

    size_t ruleCount() const { return ruleCount_; }
    // True when any selector uses the given dynamic pseudo-class.
    bool usesHover() const { return usesHover_; }
    bool usesActive() const { return usesActive_; }
    bool usesFocus() const { return usesFocus_; }

private:
    void note(const Selector& selector);

    std::unordered_map<Atom, std::vector<const StyleRule*>> byId_;
    std::unordered_map<Atom, std::vector<const StyleRule*>> byClass_;
    std::unordered_map<Atom, std::vector<const StyleRule*>> byTag_;
    std::vector<const StyleRule*> universal_;
    size_t ruleCount_ = 0;
    bool usesHover_ = false;
    bool usesActive_ = false;
    bool usesFocus_ = false;
};

// Owns the stylesheets of one document and resolves computed styles.
//
// Runtime thread only. Attaches itself as the document's MutationSink so tree
// and attribute changes invalidate the right subtrees.
class StyleEngine final : public dom::MutationSink {
public:
    explicit StyleEngine(dom::Document& document);
    ~StyleEngine() override;

    // Replaces the author stylesheets with the ones found in the document
    // (<style> elements and <link rel=stylesheet>), reading files through the
    // document's asset loader. Safe to call after every load.
    void reloadStyleSheets();

    // Adds an author stylesheet directly (tests, C# API).
    void addStyleSheet(std::string_view css, Origin origin = Origin::Author);
    void clearAuthorStyleSheets();

    // Recomputes styles for every element whose subtree is marked dirty.
    // `viewport` is in CSS pixels and resolves vw/vh.
    void recalcStyles(float viewportWidth, float viewportHeight);
    // Forces a full recalculation on the next recalcStyles().
    void invalidateAll();

    const ComputedStyle* styleFor(const dom::Element& element) const;
    const std::vector<std::string>& warnings() const { return warnings_; }
    size_t ruleCount() const { return index_.ruleCount(); }

    // Element state used by :hover/:active/:focus. Owned by the input router
    // from Stage 6; setting it invalidates only what can change.
    void setStateProvider(const ElementStateProvider* provider) { stateProvider_ = provider; }

    // dom::MutationSink
    void onNodeInserted(dom::Node& node) override;
    void onNodeRemoved(dom::Node& node, dom::Node& formerParent) override;
    void onAttributeChanged(dom::Element& element, const Atom& name) override;
    void onTextChanged(dom::CharacterData& node) override;

private:
    void rebuildIndex();
    void recalcSubtree(dom::Element& element, const ComputedStyle& parentStyle, bool force);
    RefPtr<ComputedStyle> computeStyle(dom::Element& element, const ComputedStyle& parentStyle);

    dom::Document& document_;
    StyleSheet userAgentSheet_;
    std::vector<StyleSheet> authorSheets_;
    RuleIndex index_;
    SelectorMatcher matcher_;
    const ElementStateProvider* stateProvider_ = nullptr;
    std::vector<std::string> warnings_;
    LengthContext lengthContext_;
    bool indexDirty_ = true;
    bool forceFullRecalc_ = true;
    uint32_t nextRuleOrder_ = 0;
};

// The built-in user-agent stylesheet (game-UI flavoured defaults).
std::string_view userAgentStyleSheet();

} // namespace xgu::css
