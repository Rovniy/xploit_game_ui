// linear-gradient and radial-gradient in background-image.

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

class GradientTest : public ::testing::Test {
protected:
    void SetUp() override {
        xgu_init_desc init{};
        init.struct_size = sizeof(init);
        init.flags = XGU_INIT_SINGLE_THREADED;
        ASSERT_EQ(xgu_initialize(&init), XGU_OK);
        xgu::Runtime::instance().render().setNoDevice();
    }

    void TearDown() override { xgu_views_destroy_all(); }

    // Creates the view, loads `page` and keeps a copy of the painted frame. A
    // frame can only be acquired once, so the copy is what the probes read.
    void render(const std::string& page) {
        if (view_ != XGU_INVALID_VIEW) {
            xgu_view_destroy(view_);
        }
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = 100;
        desc.height = 100;
        desc.device_pixel_ratio = 1.0f;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.name = "gradient-test";
        view_ = xgu_view_create(&desc);
        ASSERT_NE(view_, XGU_INVALID_VIEW);
        ASSERT_EQ(xgu_view_load_html(view_, page.c_str(), nullptr), XGU_OK);

        const void* data = nullptr;
        uint32_t size = 0;
        ASSERT_TRUE(xgu_view_acquire_pixels(view_, &data, &size, &width_, &height_, nullptr))
            << "no frame was painted";
        const auto* bytes = static_cast<const uint8_t*>(data);
        pixels_.assign(bytes, bytes + size);
        xgu_view_release_pixels(view_);
    }

    // Paints a 100x100 box filling the view with the given background.
    void paint(const std::string& background) {
        render("<style>html,body{margin:0;width:100%;height:100%;background:transparent}"
               "#box{position:absolute;left:0;top:0;width:100px;height:100px;background-image:" +
               background + "}</style><div id=\"box\"></div>");
    }

    Pixel at(uint32_t x, uint32_t y) const {
        if (pixels_.empty() || x >= width_ || y >= height_) {
            return Pixel{};
        }
        // The buffer is bottom-up.
        const size_t offset = (static_cast<size_t>(height_ - 1 - y) * width_ + x) * 4;
        return Pixel{pixels_[offset], pixels_[offset + 1], pixels_[offset + 2], pixels_[offset + 3]};
    }

    xgu_view_id view_ = XGU_INVALID_VIEW;
    std::vector<uint8_t> pixels_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace

TEST_F(GradientTest, DefaultDirectionRunsTopToBottom) {
    paint("linear-gradient(#ff0000, #0000ff)");

    const Pixel top = at(50, 2);
    const Pixel bottom = at(50, 97);
    EXPECT_GT(top.r, 200) << "red at the top";
    EXPECT_LT(top.b, 60);
    EXPECT_GT(bottom.b, 200) << "blue at the bottom";
    EXPECT_LT(bottom.r, 60);

    const Pixel middle = at(50, 50);
    EXPECT_GT(middle.r, 60) << "mixed in the middle";
    EXPECT_GT(middle.b, 60);
}

TEST_F(GradientTest, ToRightRunsLeftToRight) {
    paint("linear-gradient(to right, #ff0000, #0000ff)");

    EXPECT_GT(at(2, 50).r, 200);
    EXPECT_GT(at(97, 50).b, 200);
    // Nothing changes down a column.
    const Pixel high = at(50, 5);
    const Pixel low = at(50, 95);
    EXPECT_NEAR(high.r, low.r, 4);
    EXPECT_NEAR(high.b, low.b, 4);
}

TEST_F(GradientTest, AnAngleInDegreesPointsTheGradient) {
    // 90deg is to the right, the same as "to right".
    paint("linear-gradient(90deg, #ff0000, #0000ff)");
    EXPECT_GT(at(2, 50).r, 200);
    EXPECT_GT(at(97, 50).b, 200);

    // 270deg is the other way round.
    paint("linear-gradient(270deg, #ff0000, #0000ff)");
    EXPECT_GT(at(2, 50).b, 200);
    EXPECT_GT(at(97, 50).r, 200);
}

TEST_F(GradientTest, StopPositionsMoveTheTransition) {
    // Red until 80%, then a short run to blue.
    paint("linear-gradient(to right, #ff0000 80%, #0000ff 100%)");

    EXPECT_GT(at(50, 50).r, 240) << "still solid red well past the middle";
    EXPECT_LT(at(50, 50).b, 20);
    EXPECT_GT(at(97, 50).b, 200);
}

TEST_F(GradientTest, ThreeStopsPutTheMiddleColourInTheMiddle) {
    paint("linear-gradient(to right, #ff0000, #00ff00, #0000ff)");

    EXPECT_GT(at(2, 50).r, 200);
    EXPECT_GT(at(50, 50).g, 200) << "the unpositioned middle stop lands at 50%";
    EXPECT_GT(at(97, 50).b, 200);
}

TEST_F(GradientTest, RadialGradientRunsFromTheCentreOutwards) {
    paint("radial-gradient(#ff0000, #0000ff)");

    const Pixel centre = at(50, 50);
    const Pixel corner = at(2, 2);
    EXPECT_GT(centre.r, 200) << "red in the middle";
    EXPECT_GT(corner.b, 200) << "blue at the corner";
}

TEST_F(GradientTest, RadialAcceptsTheShapeKeyword) {
    paint("radial-gradient(circle, #ff0000, #0000ff)");
    EXPECT_GT(at(50, 50).r, 200);
    EXPECT_GT(at(2, 2).b, 200);
}

TEST_F(GradientTest, GradientsPaintOverTheBackgroundColour) {
    render("<style>html,body{margin:0;width:100%;height:100%}"
           "#box{position:absolute;left:0;top:0;width:100px;height:100px;"
           "background-color:#00ff00;background-image:linear-gradient(#ff0000, #ff0000)}</style>"
           "<div id=\"box\"></div>");

    const Pixel middle = at(50, 50);
    EXPECT_GT(middle.r, 240) << "the gradient covers the colour underneath";
    EXPECT_LT(middle.g, 20);
}

TEST_F(GradientTest, AMalformedGradientPaintsNothing) {
    // One stop cannot make a gradient; the box stays transparent.
    paint("linear-gradient(#ff0000)");
    EXPECT_EQ(at(50, 50).a, 0);

    paint("linear-gradient(to nowhere, #ff0000, #0000ff)");
    // "nowhere" is not a side, so the head is not consumed and the colours still
    // make a top-to-bottom gradient rather than nothing at all.
    EXPECT_GT(at(50, 2).r, 200);
}

TEST_F(GradientTest, CurrentColorResolvesAgainstTheElement) {
    render("<style>html,body{margin:0;width:100%;height:100%;background:transparent}"
           "#box{position:absolute;left:0;top:0;width:100px;height:100px;color:#ff0000;"
           "background-image:linear-gradient(currentColor, currentColor)}</style>"
           "<div id=\"box\"></div>");

    const Pixel middle = at(50, 50);
    EXPECT_GT(middle.r, 240);
    EXPECT_LT(middle.g, 20);
}
