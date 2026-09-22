#include "core/Atom.h"
#include "core/RefCounted.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

using namespace xgu;

TEST(Atom, EqualStringsShareOneEntry) {
    const Atom a("div");
    const Atom b(std::string("div"));
    const Atom c("span");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_EQ(a.string(), "div");
    EXPECT_EQ(a.hash(), b.hash());
}

TEST(Atom, DefaultIsEmpty) {
    const Atom empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty, Atom(""));
    EXPECT_FALSE(Atom("x").empty());
}

TEST(Atom, LoweredNormalisesAsciiOnly) {
    EXPECT_EQ(Atom::lowered("DIV"), Atom("div"));
    EXPECT_EQ(Atom::lowered("dIv"), Atom("div"));
    EXPECT_EQ(Atom::lowered("div"), Atom("div"));
    // Non-ASCII bytes pass through unchanged.
    EXPECT_EQ(Atom::lowered("\xD0\x9F"), Atom("\xD0\x9F"));
}

TEST(Atom, CaseInsensitiveCompare) {
    EXPECT_TRUE(Atom("Type").equalsIgnoringCase("type"));
    EXPECT_TRUE(Atom("type").equalsIgnoringCase("TYPE"));
    EXPECT_FALSE(Atom("type").equalsIgnoringCase("typed"));
}

TEST(Atom, WorksAsHashKey) {
    std::unordered_map<Atom, int> map;
    map[Atom("a")] = 1;
    map[Atom("b")] = 2;
    map[Atom("a")] = 3;
    EXPECT_EQ(map.size(), 2u);
    EXPECT_EQ(map[Atom("a")], 3);
}

namespace {

class Probe : public RefCounted {
public:
    explicit Probe(int* liveCount) : liveCount_(liveCount) { ++*liveCount_; }

protected:
    ~Probe() override { --*liveCount_; }

private:
    int* liveCount_;
};

} // namespace

TEST(RefCounted, MakeRefOwnsExactlyOneReference) {
    int live = 0;
    {
        RefPtr<Probe> probe = makeRef<Probe>(&live);
        EXPECT_EQ(live, 1);
        EXPECT_EQ(probe->refCount(), 1u);
    }
    EXPECT_EQ(live, 0);
}

TEST(RefCounted, CopyAndMoveTrackReferences) {
    int live = 0;
    RefPtr<Probe> first = makeRef<Probe>(&live);
    {
        RefPtr<Probe> second = first;
        EXPECT_EQ(first->refCount(), 2u);
        RefPtr<Probe> third = std::move(second);
        EXPECT_EQ(first->refCount(), 2u);
        EXPECT_FALSE(second);
        EXPECT_TRUE(third);
    }
    EXPECT_EQ(first->refCount(), 1u);
    EXPECT_EQ(live, 1);
    first.reset();
    EXPECT_EQ(live, 0);
}
