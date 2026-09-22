// Stage 5: painting the laid-out box tree.
//
// The assertions probe pixels instead of comparing whole images, so they do not
// depend on which fonts the machine has installed. Whole-image comparisons live
// in GoldenImageTest.cpp, whose fixtures deliberately contain no text.

#include "core/Runtime.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string>

namespace {

struct Pixel {
    uint8_t r = 0, g = 0, b = 0, a = 0;

    bool operator==(const Pixel& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
};

std::ostream& operator<<(std::ostream& stream, const Pixel& pixel) {
    return stream << "rgba(" << +pixel.r << ", " << +pixel.g << ", " << +pixel.b << ", " << +pixel.a << ")";
}

// One painted frame, addressable in top-down pixel coordinates.
class Frame {
public:
    Frame() = default;
    Frame(const uint8_t* data, uint32_t width, uint32_t height) : data_(data), width_(width), height_(height) {}

    Pixel at(uint32_t x, uint32_t y) const {
        if (!data_ || x >= width_ || y >= height_) {
            return Pixel{};
        }
        // The pixel buffer is bottom-up (Unity convention).
        const uint8_t* p = data_ + (static_cast<size_t>(height_ - 1 - y) * width_ + x) * 4u;
        return Pixel{p[0], p[1], p[2], p[3]};
    }

    // Counts pixels differing from `background` inside the rectangle. That is how
    // the tests detect "something was drawn here" without knowing the font.
    uint32_t inkIn(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const Pixel& background) const {
        uint32_t count = 0;
        for (uint32_t row = y; row < y + height; ++row) {
            for (uint32_t column = x; column < x + width; ++column) {
                if (!(at(column, row) == background)) {
                    ++count;
                }
            }
        }
        return count;
    }

private:
    const uint8_t* data_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

constexpr Pixel kTransparent{0, 0, 0, 0};
constexpr Pixel kRed{255, 0, 0, 255};
constexpr Pixel kBlue{0, 0, 255, 255};
constexpr Pixel kGreen{0, 128, 0, 255};

class PaintTest : public ::testing::Test {
protected:
    void SetUp() override {
        xgu_init_desc init{};
        init.struct_size = sizeof(init);
        init.flags = XGU_INIT_SINGLE_THREADED;
        ASSERT_EQ(xgu_initialize(&init), XGU_OK);
        xgu::Runtime::instance().render().setNoDevice();
    }

    void TearDown() override {
        if (view_ != XGU_INVALID_VIEW) {
            xgu_view_release_pixels(view_);
        }
        xgu_views_destroy_all();
    }

    // Loads the markup, paints it and returns the frame. The pixels stay valid
    // until the next call or until the fixture is torn down.
    Frame paint(const std::string& html, uint32_t width = 200, uint32_t height = 120) {
        if (view_ != XGU_INVALID_VIEW) {
            xgu_view_release_pixels(view_);
            xgu_view_destroy(view_);
            view_ = XGU_INVALID_VIEW;
        }
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = width;
        desc.height = height;
        desc.device_pixel_ratio = 1.0f;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.name = "paint-test";
        view_ = xgu_view_create(&desc);
        EXPECT_NE(view_, XGU_INVALID_VIEW);
        // Every fixture starts from a transparent page with no default margin, so
        // a probe outside the painted boxes is a clean zero.
        const std::string page =
            "<style>html,body{margin:0;width:100%;height:100%;background:transparent}</style>" + html;
        EXPECT_EQ(xgu_view_load_html(view_, page.c_str(), nullptr), XGU_OK);

        const void* data = nullptr;
        uint32_t size = 0, w = 0, h = 0;
        if (!xgu_view_acquire_pixels(view_, &data, &size, &w, &h, nullptr)) {
            ADD_FAILURE() << "no frame was painted";
            return Frame();
        }
        return Frame(static_cast<const uint8_t*>(data), w, h);
    }

    xgu_view_id view_ = XGU_INVALID_VIEW;
};

} // namespace

TEST_F(PaintTest, BackgroundFillsTheBorderBox) {
    const Frame frame = paint(R"html(<div style="position:absolute;left:20px;top:10px;width:60px;height:30px;
                                             background:#ff0000"></div>)html");
    EXPECT_EQ(frame.at(50, 25), kRed) << "inside the box";
    EXPECT_EQ(frame.at(20, 10), kRed) << "top-left corner is inclusive";
    EXPECT_EQ(frame.at(79, 39), kRed) << "bottom-right corner is inclusive";
    EXPECT_EQ(frame.at(19, 25), kTransparent) << "one pixel left of the box";
    EXPECT_EQ(frame.at(80, 25), kTransparent) << "one pixel right of the box";
    EXPECT_EQ(frame.at(50, 40), kTransparent) << "one pixel below the box";
}

TEST_F(PaintTest, BorderRadiusRoundsTheCorners) {
    const Frame frame = paint(R"html(<div style="position:absolute;left:0;top:0;width:60px;height:60px;
                                             background:#ff0000;border-radius:20px"></div>)html");
    EXPECT_EQ(frame.at(30, 30), kRed) << "the middle is still filled";
    EXPECT_EQ(frame.at(1, 1), kTransparent) << "the corner is cut away";
    EXPECT_EQ(frame.at(58, 1), kTransparent);
    EXPECT_EQ(frame.at(1, 58), kTransparent);
    EXPECT_EQ(frame.at(58, 58), kTransparent);
    EXPECT_EQ(frame.at(30, 1), kRed) << "the middle of an edge is not cut";
}

TEST_F(PaintTest, BorderIsPaintedInsideTheBorderBox) {
    const Frame frame = paint(R"html(<div style="position:absolute;left:10px;top:10px;width:60px;height:40px;
                                             background:#0000ff;border:5px solid #ff0000;
                                             box-sizing:border-box"></div>)html");
    EXPECT_EQ(frame.at(40, 12), kRed) << "top border";
    EXPECT_EQ(frame.at(40, 47), kRed) << "bottom border";
    EXPECT_EQ(frame.at(12, 30), kRed) << "left border";
    EXPECT_EQ(frame.at(67, 30), kRed) << "right border";
    EXPECT_EQ(frame.at(40, 30), kBlue) << "the background shows inside the border";
    EXPECT_EQ(frame.at(40, 9), kTransparent) << "the border does not spill outside";
}

TEST_F(PaintTest, OpacityBlendsTheWholeSubtree) {
    const Frame frame = paint(R"html(<div style="position:absolute;left:0;top:0;width:60px;height:60px;
                                             background:#ff0000;opacity:0.5">
                                   <div style="width:20px;height:20px;background:#ff0000"></div>
                                 </div>)html");
    const Pixel outer = frame.at(50, 50);
    const Pixel overlapped = frame.at(10, 10);
    EXPECT_NEAR(outer.a, 128, 2) << "half transparent";
    EXPECT_EQ(overlapped, outer) << "opacity applies to the group, not to each box in it";
}

TEST_F(PaintTest, VisibilityHiddenPaintsNothing) {
    const Frame frame = paint(R"html(<div style="position:absolute;left:0;top:0;width:60px;height:60px;
                                             background:#ff0000;visibility:hidden"></div>)html");
    EXPECT_EQ(frame.at(30, 30), kTransparent);
}

TEST_F(PaintTest, TransformMovesTheBox) {
    const Frame frame = paint(R"html(<div style="position:absolute;left:0;top:0;width:20px;height:20px;
                                             background:#ff0000;transform:translate(40px, 30px)"></div>)html");
    EXPECT_EQ(frame.at(50, 40), kRed) << "drawn at the translated position";
    EXPECT_EQ(frame.at(10, 10), kTransparent) << "nothing left at the untransformed position";
}

TEST_F(PaintTest, OverflowHiddenClipsTheChild) {
    const Frame frame = paint(R"html(<div style="position:absolute;left:0;top:0;width:40px;height:40px;
                                             overflow:hidden">
                                   <div style="width:100px;height:100px;background:#ff0000"></div>
                                 </div>)html");
    EXPECT_EQ(frame.at(20, 20), kRed) << "inside the clip";
    EXPECT_EQ(frame.at(45, 20), kTransparent) << "clipped horizontally";
    EXPECT_EQ(frame.at(20, 45), kTransparent) << "clipped vertically";
}

TEST_F(PaintTest, PositiveZIndexPaintsOverTreeOrder) {
    const Frame frame = paint(R"html(<div style="position:absolute;left:0;top:0;width:60px;height:60px;
                                             background:#ff0000;z-index:2"></div>
                                 <div style="position:absolute;left:0;top:0;width:60px;height:60px;
                                             background:#0000ff;z-index:1"></div>)html");
    EXPECT_EQ(frame.at(30, 30), kRed) << "the higher z-index wins over the later sibling";
}

TEST_F(PaintTest, NegativeZIndexPaintsBetweenBackgroundAndContent) {
    // CSS 2.1 Appendix E: a stacking context paints its own background first,
    // then its negative z-index descendants, then its in-flow content.
    const Frame frame = paint(R"html(<div style="position:absolute;left:0;top:0;width:100px;height:60px;
                                             background:#008000;z-index:0">
                                   <div style="width:100px;height:30px;background:#0000ff"></div>
                                   <div style="position:absolute;left:0;top:0;width:100px;height:60px;
                                               background:#ff0000;z-index:-1"></div>
                                 </div>)html");
    EXPECT_EQ(frame.at(50, 15), kBlue) << "in-flow content stays above a negative z-index";
    EXPECT_EQ(frame.at(50, 45), kRed) << "but the negative z-index covers the parent's own background";
}

TEST_F(PaintTest, TextIsPaintedInsideItsBlock) {
    const Frame frame = paint(R"html(<div style="position:absolute;left:10px;top:10px;width:120px;height:40px;
                                             background:#0000ff;color:#ff0000;font-size:20px">Hi</div>)html");
    ASSERT_EQ(frame.at(10, 10), kBlue) << "the block background is there to draw on";
    EXPECT_GT(frame.inkIn(10, 10, 120, 40, kBlue), 20u) << "glyphs were rasterised over the background";
    EXPECT_EQ(frame.inkIn(0, 0, 200, 10, kTransparent), 0u) << "no ink above the block";
    EXPECT_EQ(frame.inkIn(0, 50, 200, 70, kTransparent), 0u) << "no ink below the block";
}

TEST_F(PaintTest, TextAlignCentresInsideTheFinalBoxWidth) {
    // Regression: the paragraph used to keep the width of a Yoga trial measure
    // pass, so centred text was drawn far to the right of its box.
    const Frame frame = paint(R"html(<div style="position:absolute;left:0;top:0;width:200px;height:40px;
                                             background:#0000ff;color:#ff0000;font-size:20px;
                                             text-align:center">Hi</div>)html");
    EXPECT_GT(frame.inkIn(60, 0, 80, 40, kBlue), 20u) << "centred text lands in the middle third";
    EXPECT_EQ(frame.inkIn(0, 0, 60, 40, kBlue), 0u) << "nothing in the left third";
    EXPECT_EQ(frame.inkIn(140, 0, 60, 40, kBlue), 0u) << "nothing in the right third";
}

TEST_F(PaintTest, AtomicInlineIsPaintedWhereItWasLaidOut) {
    // Regression: a button inside a centred block is an atomic inline. Its own
    // background and its label have to coincide.
    const Frame frame = paint(R"html(<div style="position:absolute;left:0;top:0;width:200px;height:60px;
                                             text-align:center">
                                   <button style="padding:8px 16px;border:0;background:#0000ff;
                                                  color:#ff0000;font-size:20px">Go</button>
                                 </div>)html");
    ASSERT_GT(frame.inkIn(0, 0, 200, 60, kTransparent), 100u) << "the button background was painted";

    // Everything drawn sits in one band: the button. Find it, then check the
    // label is inside it rather than beside it.
    uint32_t firstRow = 60, lastRow = 0, firstColumn = 200, lastColumn = 0;
    for (uint32_t y = 0; y < 60; ++y) {
        for (uint32_t x = 0; x < 200; ++x) {
            if (frame.at(x, y).a != 0) {
                firstRow = std::min(firstRow, y);
                lastRow = std::max(lastRow, y);
                firstColumn = std::min(firstColumn, x);
                lastColumn = std::max(lastColumn, x);
            }
        }
    }
    ASSERT_LT(firstColumn, lastColumn);
    EXPECT_GT(frame.inkIn(firstColumn, firstRow, lastColumn - firstColumn + 1, lastRow - firstRow + 1, kBlue), 20u)
        << "the label is drawn over the button background, not beside it";
    EXPECT_NEAR(static_cast<int>((firstColumn + lastColumn) / 2), 100, 3) << "the button is centred in its block";
}

TEST_F(PaintTest, StyleChangeFromJavaScriptRepaints) {
    Frame frame = paint(R"html(<div id="box" style="position:absolute;left:0;top:0;width:60px;height:60px;
                                             background:#ff0000"></div>)html");
    ASSERT_EQ(frame.at(30, 30), kRed);

    ASSERT_EQ(xgu_view_execute_js(view_, "document.getElementById('box').style.background = '#0000ff';", nullptr),
              XGU_OK);
    xgu_view_release_pixels(view_);
    ASSERT_EQ(xgu_view_repaint(view_), XGU_OK);

    const void* data = nullptr;
    uint32_t size = 0, w = 0, h = 0;
    ASSERT_TRUE(xgu_view_acquire_pixels(view_, &data, &size, &w, &h, nullptr)) << "no frame after the style change";
    frame = Frame(static_cast<const uint8_t*>(data), w, h);
    EXPECT_EQ(frame.at(30, 30), kBlue);
}

TEST_F(PaintTest, DevicePixelRatioScalesTheRecording) {
    xgu_view_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.width = 120;
    desc.height = 80;
    desc.device_pixel_ratio = 2.0f;
    desc.format = XGU_FORMAT_RGBA8;
    desc.provider = XGU_PROVIDER_CPU;
    const xgu_view_id view = xgu_view_create(&desc);
    ASSERT_NE(view, XGU_INVALID_VIEW);
    ASSERT_EQ(xgu_view_load_html(view,
                                 "<style>html,body{margin:0;background:transparent}</style>"
                                 "<div style=\"position:absolute;left:0;top:0;width:20px;height:20px;"
                                 "background:#ff0000\"></div>",
                                 nullptr),
              XGU_OK);
    const void* data = nullptr;
    uint32_t size = 0, w = 0, h = 0;
    ASSERT_TRUE(xgu_view_acquire_pixels(view, &data, &size, &w, &h, nullptr));
    const Frame frame(static_cast<const uint8_t*>(data), w, h);
    // 20 CSS px at a device pixel ratio of 2 covers 40 device pixels.
    EXPECT_EQ(frame.at(39, 39), kRed);
    EXPECT_EQ(frame.at(41, 39), kTransparent);
    xgu_view_release_pixels(view);
    xgu_view_destroy(view);
}
