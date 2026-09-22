// Stage 10: a frame is only produced when something changed, and only the part
// that changed is redrawn.

#include "core/Runtime.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

class DamageTest : public ::testing::Test {
protected:
    void SetUp() override {
        xgu_init_desc init{};
        init.struct_size = sizeof(init);
        init.flags = XGU_INIT_SINGLE_THREADED;
        ASSERT_EQ(xgu_initialize(&init), XGU_OK);
        xgu::Runtime::instance().render().setNoDevice();
    }

    void TearDown() override { xgu_views_destroy_all(); }

    void load(const std::string& body, uint32_t size = 200) {
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = size;
        desc.height = size;
        desc.device_pixel_ratio = 1.0f;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.name = "damage-test";
        view_ = xgu_view_create(&desc);
        ASSERT_NE(view_, XGU_INVALID_VIEW);
        const std::string page =
            "<style>html,body{margin:0;width:100%;height:100%;background:transparent}</style>" + body;
        ASSERT_EQ(xgu_view_load_html(view_, page.c_str(), nullptr), XGU_OK);
    }

    xgu_frame_stats stats() {
        xgu_frame_stats result{};
        result.struct_size = sizeof(result);
        EXPECT_TRUE(xgu_view_get_stats(view_, &result));
        return result;
    }

    void run(const std::string& javascript) {
        ASSERT_EQ(xgu_view_execute_js(view_, javascript.c_str(), nullptr), XGU_OK);
    }

    xgu_view_id view_ = XGU_INVALID_VIEW;
};

} // namespace

TEST_F(DamageTest, AnIdlePageProducesNoFrames) {
    load(R"html(<div style="position:absolute;left:0;top:0;width:50px;height:50px;background:#ff0000"></div>)html");

    const uint64_t afterLoad = stats().frames_published;
    for (int i = 0; i < 30; ++i) {
        xgu_tick(static_cast<double>(i) / 60.0);
    }
    const xgu_frame_stats after = stats();
    EXPECT_EQ(after.frames_published, afterLoad) << "nothing changed, so nothing was drawn";
    EXPECT_GE(after.frames_skipped, 30u);
}

TEST_F(DamageTest, AChangeProducesExactlyOneFrame) {
    load(R"html(
      <div id="box" style="position:absolute;left:0;top:0;width:50px;height:50px;background:#ff0000"></div>)html");

    for (int i = 0; i < 5; ++i) {
        xgu_tick(static_cast<double>(i) / 60.0);
    }
    const uint64_t before = stats().frames_published;

    run("document.getElementById('box').style.backgroundColor = '#0000ff';");
    xgu_tick(1.0);
    EXPECT_EQ(stats().frames_published, before + 1);

    // And the page goes quiet again.
    for (int i = 0; i < 10; ++i) {
        xgu_tick(2.0 + static_cast<double>(i) / 60.0);
    }
    EXPECT_EQ(stats().frames_published, before + 1);
}

TEST_F(DamageTest, OnlyTheChangedBoxIsRedrawn) {
    load(R"html(
      <div id="a" style="position:absolute;left:10px;top:10px;width:20px;height:20px;background:#ff0000"></div>
      <div id="b" style="position:absolute;left:150px;top:150px;width:20px;height:20px;background:#00ff00"></div>)html");

    xgu_tick(0.0);
    run("document.getElementById('b').style.backgroundColor = '#0000ff';");
    xgu_tick(1.0);

    const xgu_frame_stats after = stats();
    // The damage covers b (150..170) and nothing near a.
    EXPECT_GE(after.damage_x, 140);
    EXPECT_GE(after.damage_y, 140);
    EXPECT_LE(after.damage_width, 40);
    EXPECT_LE(after.damage_height, 40);
}

TEST_F(DamageTest, AMovedBoxDamagesBothPlaces) {
    load(R"html(
      <div id="box" style="position:absolute;left:10px;top:10px;width:20px;height:20px;background:#ff0000"></div>)html");

    xgu_tick(0.0);
    run("document.getElementById('box').style.left = '150px';");
    xgu_tick(1.0);

    const xgu_frame_stats after = stats();
    // Where it was and where it went are both covered.
    EXPECT_LE(after.damage_x, 10);
    EXPECT_GE(after.damage_x + after.damage_width, 170);
}

TEST_F(DamageTest, AShadowIsInsideTheDamage) {
    load(R"html(
      <div id="box" style="position:absolute;left:80px;top:80px;width:20px;height:20px;background:#ff0000;
                           box-shadow: 0 0 20px 10px rgba(0,0,0,0.9)"></div>)html");

    xgu_tick(0.0);
    run("document.getElementById('box').style.backgroundColor = '#0000ff';");
    xgu_tick(1.0);

    const xgu_frame_stats after = stats();
    // The shadow reaches 30px past the box on every side.
    EXPECT_LE(after.damage_x, 50);
    EXPECT_LE(after.damage_y, 50);
    EXPECT_GE(after.damage_x + after.damage_width, 130);
    EXPECT_GE(after.damage_y + after.damage_height, 130);
}

TEST_F(DamageTest, AddingAnElementRedrawsEverything) {
    // A new box tree loses the boxes that went away, so their pixels can only be
    // cleaned up by redrawing the surface.
    load(R"html(<div id="host" style="position:absolute;left:0;top:0;width:100px;height:100px"></div>)html");

    xgu_tick(0.0);
    run("document.getElementById('host').appendChild(document.createElement('div'));");
    xgu_tick(1.0);

    const xgu_frame_stats after = stats();
    EXPECT_EQ(after.damage_width, 200);
    EXPECT_EQ(after.damage_height, 200);
}

TEST_F(DamageTest, AResizeRedrawsEverything) {
    load(R"html(<div style="position:absolute;left:0;top:0;width:50px;height:50px;background:#ff0000"></div>)html");

    xgu_tick(0.0);
    ASSERT_EQ(xgu_view_resize(view_, 120, 90, 1.0f), XGU_OK);
    xgu_tick(1.0);

    const xgu_frame_stats after = stats();
    EXPECT_EQ(after.damage_width, 120);
    EXPECT_EQ(after.damage_height, 90);
}

TEST_F(DamageTest, WhatWasNotRedrawnIsStillOnScreen) {
    // The surface persists between frames, so the part outside the damage has to
    // keep the pixels it already had.
    load(R"html(
      <div id="a" style="position:absolute;left:0;top:0;width:40px;height:40px;background:#ff0000"></div>
      <div id="b" style="position:absolute;left:150px;top:150px;width:40px;height:40px;background:#00ff00"></div>)html");

    xgu_tick(0.0);
    run("document.getElementById('b').style.backgroundColor = '#0000ff';");
    xgu_tick(1.0);

    const void* data = nullptr;
    uint32_t size = 0, w = 0, h = 0;
    ASSERT_TRUE(xgu_view_acquire_pixels(view_, &data, &size, &w, &h, nullptr));
    const auto* bytes = static_cast<const uint8_t*>(data);
    const auto at = [&](uint32_t x, uint32_t y) {
        return bytes + (static_cast<size_t>(h - 1 - y) * w + x) * 4;
    };
    EXPECT_EQ(at(20, 20)[0], 255) << "the untouched box is still red";
    EXPECT_EQ(at(170, 170)[2], 255) << "and the changed one is blue";
    xgu_view_release_pixels(view_);
}

TEST_F(DamageTest, AnAnimationKeepsProducingFrames) {
    load(R"html(
      <style>
        @keyframes fade { from { background: #ff0000 } to { background: #0000ff } }
        #box { position: absolute; left: 0; top: 0; width: 50px; height: 50px;
               animation: fade 1s linear infinite; }
      </style>
      <div id="box"></div>)html");

    const uint64_t before = stats().frames_published;
    for (int i = 0; i < 20; ++i) {
        xgu_tick(static_cast<double>(i) / 60.0);
    }
    EXPECT_GE(stats().frames_published, before + 20) << "an animation asks for a frame every tick";
}
