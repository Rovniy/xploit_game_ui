#include <xploit_game_ui/xgu.h>

#include <gtest/gtest.h>

#include <cstring>

TEST(CApi, PingReturns42) { EXPECT_EQ(xgu_ping(), 42); }

TEST(CApi, VersionIsNonEmpty) {
    const char* version = xgu_version();
    ASSERT_NE(version, nullptr);
    EXPECT_GT(std::strlen(version), 0u);
}

TEST(CApi, InitializeIsIdempotent) {
    xgu_init_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.flags = XGU_INIT_SINGLE_THREADED;
    EXPECT_EQ(xgu_initialize(&desc), XGU_OK);
    EXPECT_TRUE(xgu_is_initialized());
    EXPECT_EQ(xgu_initialize(&desc), XGU_OK);
    EXPECT_EQ(xgu_initialize(nullptr), XGU_OK);
    xgu_shutdown();
    EXPECT_FALSE(xgu_is_initialized());
    xgu_shutdown(); // second shutdown is a no-op
    // Re-initialise so later tests in this process keep the single-threaded runtime.
    EXPECT_EQ(xgu_initialize(&desc), XGU_OK);
}

TEST(CApi, InvalidViewIdsAreRejected) {
    EXPECT_EQ(xgu_view_destroy(XGU_INVALID_VIEW), XGU_ERR_INVALID_VIEW);
    EXPECT_EQ(xgu_view_resize(XGU_INVALID_VIEW, 10, 10, 1.0f), XGU_ERR_INVALID_VIEW);
    EXPECT_EQ(xgu_view_resize(0x1234567800000001ull, 0, 10, 1.0f), XGU_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(xgu_view_get_native_texture(0x1234567800000001ull, nullptr, nullptr), nullptr);
    EXPECT_FALSE(xgu_view_has_pending_frame(0x1234567800000001ull));
    EXPECT_EQ(xgu_view_status(0x1234567800000001ull), 0u);
    EXPECT_EQ(xgu_view_draw_test_frame(0x1234567800000001ull), XGU_ERR_INVALID_VIEW);
}

TEST(CApi, CreateRejectsBadDescriptors) {
    EXPECT_EQ(xgu_view_create(nullptr), XGU_INVALID_VIEW);
    xgu_view_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.width = 0;
    desc.height = 10;
    EXPECT_EQ(xgu_view_create(&desc), XGU_INVALID_VIEW);
}

TEST(CApi, RenderEventFuncIsStable) {
    EXPECT_NE(xgu_get_render_event_func(), nullptr);
    EXPECT_EQ(xgu_get_render_event_func(), xgu_get_render_event_func());
}
