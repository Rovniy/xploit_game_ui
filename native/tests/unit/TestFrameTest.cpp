#include "core/Runtime.h"
#include "render/RenderSystem.h"
#include "render/skia/TestFrame.h"

#include <xploit_game_ui/xgu.h>

#include <gtest/gtest.h>

#include <cstdint>

namespace {

struct Pixel {
    uint8_t r, g, b, a;
};

// Pixel buffers are bottom-up (Unity convention).
Pixel pixelAt(const uint8_t* data, uint32_t width, uint32_t height, uint32_t x, uint32_t y) {
    const size_t row = static_cast<size_t>(height - 1 - y);
    const uint8_t* p = data + (row * width + x) * 4u;
    return Pixel{p[0], p[1], p[2], p[3]};
}

class CpuFrameTest : public ::testing::Test {
protected:
    void SetUp() override {
        xgu_init_desc init{};
        init.struct_size = sizeof(init);
        init.flags = XGU_INIT_SINGLE_THREADED;
        ASSERT_EQ(xgu_initialize(&init), XGU_OK);
        xgu::Runtime::instance().render().setNoDevice();
    }
    void TearDown() override { xgu_views_destroy_all(); }

    xgu_view_id createView(uint32_t w, uint32_t h, float dpr = 1.0f) {
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = w;
        desc.height = h;
        desc.device_pixel_ratio = dpr;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        return xgu_view_create(&desc);
    }
};

} // namespace

TEST(TestFrame, RecordsValidPicture) {
    const xgu::render::DisplayList frame = xgu::render::recordTestFrame(320, 200, 1.0f, 7);
    ASSERT_TRUE(frame.valid());
    EXPECT_EQ(frame.sizePx.width(), 320);
    EXPECT_EQ(frame.sizePx.height(), 200);
    EXPECT_EQ(frame.frameId, 7u);
    EXPECT_EQ(frame.dirtyPx, SkIRect::MakeWH(320, 200));
}

TEST(TestFrame, RejectsEmptySize) {
    EXPECT_FALSE(xgu::render::recordTestFrame(0, 10, 1.0f, 1).valid());
    EXPECT_FALSE(xgu::render::recordTestFrame(10, 0, 1.0f, 1).valid());
}

TEST_F(CpuFrameTest, PaintsThroughTheCpuProvider) {
    const uint32_t width = 320;
    const uint32_t height = 200;
    const xgu_view_id view = createView(width, height);
    ASSERT_NE(view, XGU_INVALID_VIEW);
    EXPECT_EQ(xgu_view_get_native_texture(view, nullptr, nullptr), nullptr) << "CPU provider has no GPU texture";

    ASSERT_EQ(xgu_view_draw_test_frame(view), XGU_OK);
    EXPECT_FALSE(xgu_view_has_pending_frame(view)) << "CPU provider paints synchronously";
    const uint32_t status = xgu_view_status(view);
    EXPECT_TRUE(status & XGU_ST_TEXTURE_READY);
    EXPECT_TRUE(status & XGU_ST_PIXELS_READY);

    const void* data = nullptr;
    uint32_t size = 0, w = 0, h = 0;
    uint64_t frameId = 0;
    ASSERT_TRUE(xgu_view_acquire_pixels(view, &data, &size, &w, &h, &frameId));
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(w, width);
    EXPECT_EQ(h, height);
    EXPECT_EQ(size, width * height * 4u);
    EXPECT_EQ(frameId, 1u);

    const auto* bytes = static_cast<const uint8_t*>(data);
    const Pixel corner = pixelAt(bytes, w, h, 1, 1);
    EXPECT_EQ(corner.a, 0) << "8% margin around the panel stays transparent";
    const Pixel centre = pixelAt(bytes, w, h, width / 2, height / 2);
    EXPECT_GT(centre.a, 200) << "panel is nearly opaque";
    EXPECT_GT(centre.b, centre.r) << "panel colour is a dark blue";

    // A second acquire without a new frame returns nothing.
    xgu_view_release_pixels(view);
    EXPECT_FALSE(xgu_view_acquire_pixels(view, &data, &size, &w, &h, &frameId));

    // A new frame becomes available again after drawing.
    ASSERT_EQ(xgu_view_draw_test_frame(view), XGU_OK);
    ASSERT_TRUE(xgu_view_acquire_pixels(view, &data, &size, &w, &h, &frameId));
    EXPECT_EQ(frameId, 2u);
    xgu_view_release_pixels(view);

    EXPECT_EQ(xgu_view_destroy(view), XGU_OK);
    EXPECT_EQ(xgu_view_destroy(view), XGU_ERR_INVALID_VIEW);
}

TEST_F(CpuFrameTest, ResizeRecreatesTheSurface) {
    const xgu_view_id view = createView(64, 64);
    ASSERT_NE(view, XGU_INVALID_VIEW);
    ASSERT_EQ(xgu_view_draw_test_frame(view), XGU_OK);
    (void)xgu_view_status(view); // clears RECREATED if set by creation

    ASSERT_EQ(xgu_view_resize(view, 128, 96, 1.0f), XGU_OK);
    ASSERT_EQ(xgu_view_draw_test_frame(view), XGU_OK);
    EXPECT_TRUE(xgu_view_status(view) & XGU_ST_TEXTURE_RECREATED);
    EXPECT_FALSE(xgu_view_status(view) & XGU_ST_TEXTURE_RECREATED) << "flag clears on read";

    const void* data = nullptr;
    uint32_t size = 0, w = 0, h = 0;
    ASSERT_TRUE(xgu_view_acquire_pixels(view, &data, &size, &w, &h, nullptr));
    EXPECT_EQ(w, 128u);
    EXPECT_EQ(h, 96u);
    EXPECT_EQ(size, 128u * 96u * 4u);
    xgu_view_release_pixels(view);
}

TEST_F(CpuFrameTest, DevicePixelRatioScalesContent) {
    const xgu_view_id view = createView(200, 100, 2.0f);
    ASSERT_EQ(xgu_view_draw_test_frame(view), XGU_OK);
    const void* data = nullptr;
    uint32_t size = 0, w = 0, h = 0;
    ASSERT_TRUE(xgu_view_acquire_pixels(view, &data, &size, &w, &h, nullptr));
    const auto* bytes = static_cast<const uint8_t*>(data);
    EXPECT_EQ(pixelAt(bytes, w, h, 1, 1).a, 0);
    EXPECT_GT(pixelAt(bytes, w, h, 100, 50).a, 200);
    xgu_view_release_pixels(view);
}
