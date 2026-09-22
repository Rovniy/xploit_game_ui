// CSS transitions and @keyframes animations.
//
// The clock is driven by xgu_tick, so nothing here depends on wall-clock timing.

#include "core/Runtime.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

struct Pixel {
    uint8_t r = 0, g = 0, b = 0, a = 0;
};

class AnimationTest : public ::testing::Test {
protected:
    void SetUp() override {
        xgu_init_desc init{};
        init.struct_size = sizeof(init);
        init.flags = XGU_INIT_SINGLE_THREADED;
        ASSERT_EQ(xgu_initialize(&init), XGU_OK);
        xgu::Runtime::instance().render().setNoDevice();
    }

    void TearDown() override { xgu_views_destroy_all(); }

    void load(const std::string& body) {
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = 100;
        desc.height = 100;
        desc.device_pixel_ratio = 1.0f;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.name = "animation-test";
        view_ = xgu_view_create(&desc);
        ASSERT_NE(view_, XGU_INVALID_VIEW);
        const std::string page =
            "<style>html,body{margin:0;width:100%;height:100%;background:transparent}</style>" + body;
        ASSERT_EQ(xgu_view_load_html(view_, page.c_str(), nullptr), XGU_OK);
        capture();
    }

    // Advances the clock and keeps the frame it produced.
    void tick(double seconds) {
        xgu_tick(seconds);
        capture();
    }

    Pixel at(uint32_t x, uint32_t y) const {
        if (pixels_.empty() || x >= width_ || y >= height_) {
            return Pixel{};
        }
        const size_t offset = (static_cast<size_t>(height_ - 1 - y) * width_ + x) * 4;
        return Pixel{pixels_[offset], pixels_[offset + 1], pixels_[offset + 2], pixels_[offset + 3]};
    }

    void run(const std::string& javascript) {
        ASSERT_EQ(xgu_view_execute_js(view_, javascript.c_str(), nullptr), XGU_OK);
    }

    xgu_view_id view_ = XGU_INVALID_VIEW;

private:
    void capture() {
        const void* data = nullptr;
        uint32_t size = 0;
        if (!xgu_view_acquire_pixels(view_, &data, &size, &width_, &height_, nullptr)) {
            return; // nothing new this frame; the previous capture still stands
        }
        const auto* bytes = static_cast<const uint8_t*>(data);
        pixels_.assign(bytes, bytes + size);
        xgu_view_release_pixels(view_);
    }

    std::vector<uint8_t> pixels_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace

TEST_F(AnimationTest, ATransitionMovesTheColourOverItsDuration) {
    load(R"html(
      <style>
        #box { position: absolute; left: 0; top: 0; width: 100px; height: 100px;
               background: #ff0000; transition: background-color 1s linear; }
        #box.blue { background: #0000ff }
      </style>
      <div id="box"></div>)html");

    EXPECT_GT(at(50, 50).r, 240) << "starts red";

    tick(0.0);
    run("document.getElementById('box').classList.add('blue');");
    tick(0.0); // the class change starts the transition

    tick(0.5);
    const Pixel half = at(50, 50);
    EXPECT_NEAR(half.r, 128, 24) << "half way between the two colours";
    EXPECT_NEAR(half.b, 128, 24);

    tick(1.0);
    EXPECT_GT(at(50, 50).b, 240) << "arrived";
    EXPECT_LT(at(50, 50).r, 16);
}

TEST_F(AnimationTest, ATransitionWithADelayWaitsBeforeMoving) {
    load(R"html(
      <style>
        #box { position: absolute; left: 0; top: 0; width: 100px; height: 100px;
               background: #ff0000; transition: background-color 1s linear 1s; }
        #box.blue { background: #0000ff }
      </style>
      <div id="box"></div>)html");

    tick(0.0);
    run("document.getElementById('box').classList.add('blue');");
    tick(0.0);

    tick(0.5);
    EXPECT_GT(at(50, 50).r, 240) << "still waiting out the delay";

    tick(1.5);
    EXPECT_NEAR(at(50, 50).r, 128, 24) << "half way, one second after the delay ended";
}

TEST_F(AnimationTest, ATransitionOnAllCoversEveryAnimatableProperty) {
    load(R"html(
      <style>
        #box { position: absolute; left: 0; top: 0; width: 20px; height: 100px;
               background: #ff0000; transition: all 1s linear; }
        #box.wide { width: 100px }
      </style>
      <div id="box"></div>)html");

    tick(0.0);
    run("document.getElementById('box').classList.add('wide');");
    tick(0.0);

    tick(0.5);
    // Half way the box is about 60px wide, so x=50 is inside and x=70 is not.
    EXPECT_GT(at(50, 50).a, 200);
    EXPECT_EQ(at(70, 50).a, 0);

    tick(1.0);
    EXPECT_GT(at(90, 50).a, 200) << "fully wide at the end";
}

TEST_F(AnimationTest, NoTransitionMeansTheValueJumps) {
    load(R"html(
      <style>
        #box { position: absolute; left: 0; top: 0; width: 100px; height: 100px; background: #ff0000 }
        #box.blue { background: #0000ff }
      </style>
      <div id="box"></div>)html");

    tick(0.0);
    run("document.getElementById('box').classList.add('blue');");
    tick(0.0);
    EXPECT_GT(at(50, 50).b, 240) << "straight to the new colour";
}

TEST_F(AnimationTest, KeyframesAnimateOverTheDuration) {
    load(R"html(
      <style>
        @keyframes fade { from { background: #ff0000 } to { background: #0000ff } }
        #box { position: absolute; left: 0; top: 0; width: 100px; height: 100px;
               background: #00ff00; animation: fade 1s linear; }
      </style>
      <div id="box"></div>)html");

    tick(0.0);
    EXPECT_GT(at(50, 50).r, 240) << "the first keyframe wins over the element's own background";

    tick(0.5);
    EXPECT_NEAR(at(50, 50).r, 128, 24);
    EXPECT_NEAR(at(50, 50).b, 128, 24);

    tick(1.0);
    // Without a fill mode the animation leaves nothing behind, so the element's
    // own background comes back.
    EXPECT_GT(at(50, 50).g, 240);
}

TEST_F(AnimationTest, ForwardsFillKeepsTheLastKeyframe) {
    load(R"html(
      <style>
        @keyframes fade { from { background: #ff0000 } to { background: #0000ff } }
        #box { position: absolute; left: 0; top: 0; width: 100px; height: 100px;
               background: #00ff00; animation: fade 1s linear forwards; }
      </style>
      <div id="box"></div>)html");

    tick(0.0);
    tick(2.0);
    EXPECT_GT(at(50, 50).b, 240) << "held at the end";
    EXPECT_LT(at(50, 50).g, 16);
}

TEST_F(AnimationTest, PercentageKeyframesLandWhereTheySay) {
    load(R"html(
      <style>
        @keyframes three {
          0% { background: #ff0000 }
          50% { background: #00ff00 }
          100% { background: #0000ff }
        }
        #box { position: absolute; left: 0; top: 0; width: 100px; height: 100px;
               animation: three 1s linear forwards; }
      </style>
      <div id="box"></div>)html");

    tick(0.0);
    EXPECT_GT(at(50, 50).r, 240);
    tick(0.5);
    EXPECT_GT(at(50, 50).g, 240) << "the middle keyframe";
    tick(1.0);
    EXPECT_GT(at(50, 50).b, 240);
}

TEST_F(AnimationTest, AnInfiniteAnimationKeepsGoing) {
    load(R"html(
      <style>
        @keyframes blink { from { background: #ff0000 } to { background: #0000ff } }
        #box { position: absolute; left: 0; top: 0; width: 100px; height: 100px;
               animation: blink 1s linear infinite; }
      </style>
      <div id="box"></div>)html");

    tick(0.0);
    tick(1.5);
    EXPECT_NEAR(at(50, 50).r, 128, 24) << "half way through the second run";
    tick(3.0);
    EXPECT_GT(at(50, 50).r, 240) << "back at the start of the fourth run";
}

TEST_F(AnimationTest, AlternateRunsBackwardsEveryOtherTime) {
    load(R"html(
      <style>
        @keyframes fade { from { background: #ff0000 } to { background: #0000ff } }
        #box { position: absolute; left: 0; top: 0; width: 100px; height: 100px;
               animation: fade 1s linear infinite alternate; }
      </style>
      <div id="box"></div>)html");

    tick(0.0);
    tick(0.25);
    const Pixel forward = at(50, 50);
    EXPECT_NEAR(forward.r, 191, 24) << "a quarter of the way from red to blue";

    // The second run plays backwards, so a quarter into it the colour is three
    // quarters of the way towards blue instead.
    tick(1.25);
    EXPECT_NEAR(at(50, 50).r, 64, 24);
    // And three quarters into it, it is back where the forward run was.
    tick(1.75);
    EXPECT_NEAR(at(50, 50).r, forward.r, 24);
}

TEST_F(AnimationTest, MillisecondsAndSecondsAgree) {
    load(R"html(
      <style>
        #box { position: absolute; left: 0; top: 0; width: 100px; height: 100px;
               background: #ff0000; transition: background-color 500ms linear; }
        #box.blue { background: #0000ff }
      </style>
      <div id="box"></div>)html");

    tick(0.0);
    run("document.getElementById('box').classList.add('blue');");
    tick(0.0);
    tick(0.25);
    EXPECT_NEAR(at(50, 50).r, 128, 24) << "500ms is half a second";
}

TEST_F(AnimationTest, AnUnknownAnimationNameChangesNothing) {
    load(R"html(
      <style>
        #box { position: absolute; left: 0; top: 0; width: 100px; height: 100px;
               background: #ff0000; animation: missing 1s linear forwards; }
      </style>
      <div id="box"></div>)html");

    tick(0.0);
    tick(2.0);
    EXPECT_GT(at(50, 50).r, 240) << "the element keeps its own style";
}

TEST_F(AnimationTest, TransitionsSurviveARetargetMidFlight) {
    load(R"html(
      <style>
        #box { position: absolute; left: 0; top: 0; width: 100px; height: 100px;
               background: #ff0000; transition: background-color 1s linear; }
        #box.blue { background: #0000ff }
        #box.green { background: #00ff00 }
      </style>
      <div id="box"></div>)html");

    tick(0.0);
    run("document.getElementById('box').classList.add('blue');");
    tick(0.0);
    tick(0.5);
    const Pixel midway = at(50, 50);
    EXPECT_NEAR(midway.r, 128, 24);

    // Change the target while the first move is still running.
    run("document.getElementById('box').classList.remove('blue');"
        "document.getElementById('box').classList.add('green');");
    tick(0.5);
    const Pixel afterRetarget = at(50, 50);
    EXPECT_NEAR(afterRetarget.r, midway.r, 24) << "it continues from where it was, not from the start";

    tick(1.5);
    EXPECT_GT(at(50, 50).g, 240) << "and reaches the new target";
}
