// Stage 5: whole-image comparison against checked-in PNGs.
//
// The fixtures in tests/testdata/golden contain no text, so the expected images
// do not depend on the fonts a machine happens to have. Text is covered by the
// pixel probes in PaintTest.cpp.
//
// Set XGU_UPDATE_GOLDEN=1 to rewrite the expected images from the current build.
// When a comparison fails the actual and diff images are written next to the
// working directory so the difference can be looked at.

#include "core/Runtime.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <include/codec/SkCodec.h>
#include <include/core/SkData.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkPixmap.h>
#include <include/core/SkStream.h>
#include <include/encode/SkPngEncoder.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// A pixel counts as different when any channel is off by more than this.
constexpr int kChannelTolerance = 8;
// And the comparison fails when more than this share of pixels differ.
constexpr double kMaxDifferingShare = 0.001;

std::filesystem::path goldenDir() { return std::filesystem::path(XGU_TESTDATA_DIR) / "golden"; }

bool updatingGoldens() {
    const char* value = std::getenv("XGU_UPDATE_GOLDEN");
    return value && *value && std::string(value) != "0";
}

// Top-down RGBA8 premultiplied pixels.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    bool empty() const { return pixels.empty(); }
    size_t rowBytes() const { return static_cast<size_t>(width) * 4u; }
};

Image flipToTopDown(const uint8_t* bottomUp, int width, int height) {
    Image image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<size_t>(width) * height * 4u);
    const size_t rowBytes = image.rowBytes();
    for (int y = 0; y < height; ++y) {
        std::memcpy(image.pixels.data() + static_cast<size_t>(y) * rowBytes,
                    bottomUp + static_cast<size_t>(height - 1 - y) * rowBytes, rowBytes);
    }
    return image;
}

bool writePng(const std::filesystem::path& path, const Image& image) {
    const SkImageInfo info = SkImageInfo::Make(image.width, image.height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    const SkPixmap pixmap(info, image.pixels.data(), image.rowBytes());
    SkFILEWStream stream(path.string().c_str());
    return stream.isValid() && SkPngEncoder::Encode(&stream, pixmap, SkPngEncoder::Options{});
}

Image readPng(const std::filesystem::path& path) {
    sk_sp<SkData> data = SkData::MakeFromFileName(path.string().c_str());
    if (!data) {
        return Image{};
    }
    std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(std::move(data));
    if (!codec) {
        return Image{};
    }
    const SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType).makeAlphaType(kPremul_SkAlphaType);
    Image image;
    image.width = info.width();
    image.height = info.height();
    image.pixels.resize(static_cast<size_t>(image.width) * image.height * 4u);
    if (codec->getPixels(info, image.pixels.data(), image.rowBytes()) != SkCodec::kSuccess) {
        return Image{};
    }
    return image;
}

class GoldenImageTest : public ::testing::Test {
protected:
    void SetUp() override {
        xgu_init_desc init{};
        init.struct_size = sizeof(init);
        init.flags = XGU_INIT_SINGLE_THREADED;
        ASSERT_EQ(xgu_initialize(&init), XGU_OK);
        xgu::Runtime::instance().render().setNoDevice();
    }

    void TearDown() override { xgu_views_destroy_all(); }

    void check(const std::string& name, uint32_t width, uint32_t height) {
        const std::string root = goldenDir().string();
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = width;
        desc.height = height;
        desc.device_pixel_ratio = 1.0f;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.ui_root = root.c_str();
        desc.name = "golden";
        const xgu_view_id view = xgu_view_create(&desc);
        ASSERT_NE(view, XGU_INVALID_VIEW);
        ASSERT_EQ(xgu_view_load(view, (name + ".html").c_str()), XGU_OK);

        const void* data = nullptr;
        uint32_t size = 0, w = 0, h = 0;
        ASSERT_TRUE(xgu_view_acquire_pixels(view, &data, &size, &w, &h, nullptr)) << "nothing was painted";
        const Image actual = flipToTopDown(static_cast<const uint8_t*>(data), static_cast<int>(w),
                                           static_cast<int>(h));
        xgu_view_release_pixels(view);
        xgu_view_destroy(view);

        const std::filesystem::path expectedPath = goldenDir() / (name + ".png");
        if (updatingGoldens()) {
            ASSERT_TRUE(writePng(expectedPath, actual)) << "cannot write " << expectedPath.string();
            GTEST_SKIP() << "rewrote " << expectedPath.string();
        }

        const Image expected = readPng(expectedPath);
        ASSERT_FALSE(expected.empty()) << "missing golden " << expectedPath.string()
                                       << " (run the tests with XGU_UPDATE_GOLDEN=1 to create it)";
        ASSERT_EQ(expected.width, actual.width);
        ASSERT_EQ(expected.height, actual.height);

        Image diff;
        diff.width = actual.width;
        diff.height = actual.height;
        diff.pixels.assign(actual.pixels.size(), 0);
        size_t differing = 0;
        for (size_t i = 0; i < actual.pixels.size(); i += 4) {
            int worst = 0;
            for (size_t channel = 0; channel < 4; ++channel) {
                worst = std::max(worst, std::abs(static_cast<int>(actual.pixels[i + channel]) -
                                                 static_cast<int>(expected.pixels[i + channel])));
            }
            if (worst > kChannelTolerance) {
                ++differing;
                diff.pixels[i] = 255;
                diff.pixels[i + 3] = 255;
            }
        }

        const size_t total = actual.pixels.size() / 4;
        const double share = static_cast<double>(differing) / static_cast<double>(total);
        if (share > kMaxDifferingShare) {
            const std::filesystem::path actualPath = std::filesystem::current_path() / (name + ".actual.png");
            const std::filesystem::path diffPath = std::filesystem::current_path() / (name + ".diff.png");
            writePng(actualPath, actual);
            writePng(diffPath, diff);
            FAIL() << differing << " of " << total << " pixels differ by more than " << kChannelTolerance
                   << "/255 (" << share * 100.0 << "%); wrote " << actualPath.string() << " and "
                   << diffPath.string();
        }
    }
};

} // namespace

TEST_F(GoldenImageTest, Shapes) { check("shapes", 320, 200); }

TEST_F(GoldenImageTest, Stacking) { check("stacking", 240, 160); }

TEST_F(GoldenImageTest, ClipAndTransform) { check("clip", 240, 160); }

TEST_F(GoldenImageTest, Shadows) { check("shadows", 240, 200); }

TEST_F(GoldenImageTest, FlexHud) { check("flex-hud", 320, 120); }
