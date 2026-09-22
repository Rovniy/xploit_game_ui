#include "core/ViewRegistry.h"

#include <gtest/gtest.h>

using namespace xgu;

namespace {

std::unique_ptr<View> makeView(uint32_t w = 16, uint32_t h = 16) {
    ViewDesc desc;
    desc.width = w;
    desc.height = h;
    desc.provider = ProviderKind::Cpu;
    return std::make_unique<View>(desc);
}

} // namespace

TEST(ViewRegistry, AddAndResolve) {
    ViewRegistry registry;
    const ViewId id = registry.add(makeView());
    ASSERT_NE(id, ViewRegistry::kInvalid);
    EXPECT_EQ(ViewRegistry::indexOf(id), 0u);
    EXPECT_EQ(ViewRegistry::generationOf(id), 1u);
    EXPECT_NE(registry.resolve(id), nullptr);
    EXPECT_EQ(registry.size(), 1u);
}

TEST(ViewRegistry, StaleIdAfterRemove) {
    ViewRegistry registry;
    const ViewId id = registry.add(makeView());
    std::unique_ptr<View> removed = registry.remove(id);
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(registry.resolve(id), nullptr);
    EXPECT_EQ(registry.remove(id), nullptr);
    EXPECT_FALSE(registry.withView(id, [](View&) {}));
    EXPECT_EQ(registry.size(), 0u);
}

TEST(ViewRegistry, SlotReuseBumpsGeneration) {
    ViewRegistry registry;
    const ViewId first = registry.add(makeView());
    registry.remove(first);
    const ViewId second = registry.add(makeView());
    EXPECT_EQ(ViewRegistry::indexOf(first), ViewRegistry::indexOf(second));
    EXPECT_NE(ViewRegistry::generationOf(first), ViewRegistry::generationOf(second));
    EXPECT_EQ(registry.resolve(first), nullptr);
    EXPECT_NE(registry.resolve(second), nullptr);
}

TEST(ViewRegistry, InvalidAndOutOfRangeIds) {
    ViewRegistry registry;
    EXPECT_EQ(registry.resolve(ViewRegistry::kInvalid), nullptr);
    EXPECT_EQ(registry.resolve(ViewRegistry::makeId(42, 1)), nullptr);
    registry.add(makeView());
    EXPECT_EQ(registry.resolve(ViewRegistry::makeId(0, 7)), nullptr);
}

TEST(ViewRegistry, RemoveAllInvalidatesEverything) {
    ViewRegistry registry;
    const ViewId a = registry.add(makeView());
    const ViewId b = registry.add(makeView());
    auto all = registry.removeAll();
    EXPECT_EQ(all.size(), 2u);
    EXPECT_EQ(registry.resolve(a), nullptr);
    EXPECT_EQ(registry.resolve(b), nullptr);
    EXPECT_EQ(registry.size(), 0u);
}

TEST(ViewRegistry, ForEachVisitsLiveViews) {
    ViewRegistry registry;
    registry.add(makeView(1, 1));
    const ViewId gone = registry.add(makeView(2, 2));
    registry.add(makeView(3, 3));
    registry.remove(gone);
    int visited = 0;
    registry.forEach([&](ViewId, View& view) {
        ++visited;
        EXPECT_NE(view.width(), 2u);
    });
    EXPECT_EQ(visited, 2);
}
