// xgu_cli — headless tool. Stage 1: renders the built-in test frame to PNG via
// the CPU provider. Later stages add `render <html> <png>`, `layout <html>` and
// `run <html>` subcommands.

#include "core/Runtime.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <include/core/SkImageInfo.h>
#include <include/core/SkPixmap.h>
#include <include/core/SkStream.h>
#include <include/encode/SkPngEncoder.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void usage() {
    std::fprintf(stderr,
                 "xgu_cli %s\n"
                 "usage:\n"
                 "  xgu_cli --test-frame <out.png> [--width W] [--height H] [--dpr F]\n",
                 xgu_version());
}

void logToStderr(void*, int level, const char* message) {
    static const char* names[] = {"debug", "info", "warn", "error"};
    const int idx = level < 0 ? 0 : (level > 3 ? 3 : level);
    std::fprintf(stderr, "[%s] %s\n", names[idx], message);
}

int writePng(const std::string& path, const uint8_t* bottomUp, uint32_t width, uint32_t height) {
    // Convert bottom-up rows (Unity convention) to top-down for the PNG.
    const size_t rowBytes = static_cast<size_t>(width) * 4u;
    std::vector<uint8_t> topDown(rowBytes * height);
    for (uint32_t y = 0; y < height; ++y) {
        std::memcpy(topDown.data() + static_cast<size_t>(y) * rowBytes,
                    bottomUp + static_cast<size_t>(height - 1 - y) * rowBytes, rowBytes);
    }
    const SkImageInfo info =
        SkImageInfo::Make(static_cast<int>(width), static_cast<int>(height), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    SkPixmap pixmap(info, topDown.data(), rowBytes);
    SkFILEWStream stream(path.c_str());
    if (!stream.isValid()) {
        std::fprintf(stderr, "cannot open %s for writing\n", path.c_str());
        return 2;
    }
    if (!SkPngEncoder::Encode(&stream, pixmap, SkPngEncoder::Options{})) {
        std::fprintf(stderr, "PNG encode failed\n");
        return 3;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string output;
    uint32_t width = 640;
    uint32_t height = 360;
    float dpr = 1.0f;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--test-frame" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--width" && i + 1 < argc) {
            width = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            height = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--dpr" && i + 1 < argc) {
            dpr = static_cast<float>(std::atof(argv[++i]));
        } else {
            usage();
            return 1;
        }
    }
    if (output.empty() || width == 0 || height == 0) {
        usage();
        return 1;
    }

    xgu_init_desc init{};
    init.struct_size = sizeof(init);
    init.log_fn = &logToStderr;
    xgu_initialize(&init);
    xgu::Runtime::instance().render().setNoDevice();

    xgu_view_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.width = width;
    desc.height = height;
    desc.device_pixel_ratio = dpr;
    desc.format = XGU_FORMAT_RGBA8;
    desc.provider = XGU_PROVIDER_CPU;
    desc.name = "cli";
    const xgu_view_id view = xgu_view_create(&desc);
    if (view == XGU_INVALID_VIEW) {
        std::fprintf(stderr, "view creation failed\n");
        return 4;
    }
    if (xgu_view_draw_test_frame(view) != XGU_OK) {
        std::fprintf(stderr, "test frame failed\n");
        return 5;
    }

    const void* pixels = nullptr;
    uint32_t size = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    uint64_t frameId = 0;
    if (!xgu_view_acquire_pixels(view, &pixels, &size, &w, &h, &frameId)) {
        std::fprintf(stderr, "no pixels available\n");
        return 6;
    }
    const int rc = writePng(output, static_cast<const uint8_t*>(pixels), w, h);
    xgu_view_release_pixels(view);
    xgu_view_destroy(view);
    xgu_shutdown();
    if (rc == 0) {
        std::printf("wrote %s (%ux%u, frame %llu)\n", output.c_str(), w, h, static_cast<unsigned long long>(frameId));
    }
    return rc;
}
