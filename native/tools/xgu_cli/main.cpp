// xgu_cli — headless tool.
//
//   xgu_cli --test-frame <out.png> [--width W] [--height H] [--dpr F]
//       Renders the built-in Stage 1 test frame through the CPU provider.
//   xgu_cli js <script.js> [--origin NAME]
//       Runs a script in a fresh view; console output goes to stdout/stderr.
//       Exit code 1 when the script reports an uncaught error.
//   xgu_cli layout <page.html> [--width W] [--height H] [--dpr F]
//       Loads the page, styles and lays it out, and prints the box tree as JSON
//       (used by the layout golden tests in CI).
//
// Later stages add `render <html> <png>` and `run <html>`.

#include "core/Runtime.h"
#include "css/ComputedStyle.h"
#include "dom/Document.h"
#include "dom/Element.h"
#include "layout/LayoutEngine.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <include/core/SkImageInfo.h>
#include <include/core/SkPixmap.h>
#include <include/core/SkStream.h>
#include <include/encode/SkPngEncoder.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_errorCount = 0;

void usage() {
    std::fprintf(stderr,
                 "xgu_cli %s\n"
                 "usage:\n"
                 "  xgu_cli --test-frame <out.png> [--width W] [--height H] [--dpr F]\n"
                 "  xgu_cli js <script.js> [--origin NAME]\n"
                 "  xgu_cli layout <page.html> [--width W] [--height H] [--dpr F]\n",
                 xgu_version());
}

void logToConsole(void*, int level, const char* message) {
    if (level >= XGU_LOG_WARNING) {
        if (level == XGU_LOG_ERROR) {
            ++g_errorCount;
        }
        std::fprintf(stderr, "%s\n", message);
    } else {
        std::printf("%s\n", message);
    }
}

bool initialize() {
    xgu_init_desc init{};
    init.struct_size = sizeof(init);
    init.log_fn = &logToConsole;
    init.flags = XGU_INIT_SINGLE_THREADED;
    if (xgu_initialize(&init) != XGU_OK) {
        std::fprintf(stderr, "xgu_initialize failed\n");
        return false;
    }
    xgu::Runtime::instance().render().setNoDevice();
    return true;
}

xgu_view_id createCpuView(uint32_t width, uint32_t height, float dpr, const char* name) {
    xgu_view_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.width = width;
    desc.height = height;
    desc.device_pixel_ratio = dpr;
    desc.format = XGU_FORMAT_RGBA8;
    desc.provider = XGU_PROVIDER_CPU;
    desc.name = name;
    return xgu_view_create(&desc);
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

int commandTestFrame(int argc, char** argv) {
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
    if (!initialize()) {
        return 4;
    }
    const xgu_view_id view = createCpuView(width, height, dpr, "cli");
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

int commandJs(int argc, char** argv) {
    std::string path;
    std::string origin;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--origin" && i + 1 < argc) {
            origin = argv[++i];
        } else if (path.empty()) {
            path = arg;
        } else {
            usage();
            return 1;
        }
    }
    if (path.empty()) {
        usage();
        return 1;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        return 2;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();
    if (origin.empty()) {
        origin = path;
    }
    if (!initialize()) {
        return 4;
    }
    const xgu_view_id view = createCpuView(1, 1, 1.0f, "cli-js");
    if (view == XGU_INVALID_VIEW) {
        std::fprintf(stderr, "view creation failed\n");
        return 4;
    }
    const xgu_status status = xgu_view_execute_js(view, source.c_str(), origin.c_str());
    xgu_tick(0.0);
    xgu_view_destroy(view);
    xgu_shutdown();
    if (status != XGU_OK) {
        std::fprintf(stderr, "execute_js failed (%d)\n", static_cast<int>(status));
        return 5;
    }
    return g_errorCount > 0 ? 1 : 0;
}

// Prints one box and its subtree. Keeping this in the CLI (instead of the
// engine) means the dump format can change without touching the runtime.
void dumpBox(const xgu::layout::LayoutBox& box, int depth, bool last, std::string& out) {
    const std::string indent(static_cast<size_t>(depth) * 2, ' ');
    std::string name = "#anonymous";
    if (box.element()) {
        name = box.element()->tagName().string();
        if (!box.element()->id().empty()) {
            name += "#" + box.element()->id().string();
        }
        for (const xgu::Atom& className : box.element()->classList()) {
            name += "." + className.string();
        }
    }
    const xgu::layout::Rect& frame = box.borderBox();
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer),
                  "%s{\"node\": \"%s\", \"x\": %.2f, \"y\": %.2f, \"width\": %.2f, \"height\": %.2f",
                  indent.c_str(), name.c_str(), frame.x, frame.y, frame.width, frame.height);
    out += buffer;

    std::vector<const xgu::layout::LayoutBox*> children;
    for (const auto& child : box.children()) {
        children.push_back(child.get());
    }
    for (const auto& child : box.atomicInlines()) {
        children.push_back(child.get());
    }
    if (children.empty()) {
        out += "}";
    } else {
        out += ", \"children\": [\n";
        for (size_t i = 0; i < children.size(); ++i) {
            dumpBox(*children[i], depth + 1, i + 1 == children.size(), out);
        }
        out += "\n" + indent + "]}";
    }
    if (!last) {
        out += ",";
    }
    out += "\n";
}

int commandLayout(int argc, char** argv) {
    std::string path;
    uint32_t width = 800;
    uint32_t height = 600;
    float dpr = 1.0f;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--width" && i + 1 < argc) {
            width = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            height = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--dpr" && i + 1 < argc) {
            dpr = static_cast<float>(std::atof(argv[++i]));
        } else if (path.empty()) {
            path = arg;
        } else {
            usage();
            return 1;
        }
    }
    if (path.empty()) {
        usage();
        return 1;
    }

    // The UI root is the file's directory, so relative references resolve.
    std::filesystem::path full = std::filesystem::absolute(path);
    const std::string uiRoot = full.parent_path().string();
    const std::string fileName = full.filename().string();

    if (!initialize()) {
        return 4;
    }
    xgu_view_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.width = width;
    desc.height = height;
    desc.device_pixel_ratio = dpr;
    desc.format = XGU_FORMAT_RGBA8;
    desc.provider = XGU_PROVIDER_CPU;
    desc.ui_root = uiRoot.c_str();
    desc.name = "cli-layout";
    const xgu_view_id view = xgu_view_create(&desc);
    if (view == XGU_INVALID_VIEW) {
        std::fprintf(stderr, "view creation failed\n");
        return 4;
    }
    if (xgu_view_load(view, fileName.c_str()) != XGU_OK) {
        std::fprintf(stderr, "load failed\n");
        return 5;
    }

    int result = 0;
    xgu::Runtime::instance().views().withView(static_cast<xgu::ViewId>(view), [&](xgu::View& v) {
        xgu::layout::LayoutEngine* engine = v.layoutEngine();
        if (!engine || !engine->root()) {
            std::fprintf(stderr, "nothing was laid out\n");
            result = 6;
            return;
        }
        std::string out;
        dumpBox(*engine->root(), 0, true, out);
        std::fputs(out.c_str(), stdout);
    });

    xgu_view_destroy(view);
    xgu_shutdown();
    return result == 0 && g_errorCount > 0 ? 1 : result;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "js") == 0) {
        return commandJs(argc, argv);
    }
    if (argc >= 2 && std::strcmp(argv[1], "layout") == 0) {
        return commandLayout(argc, argv);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--test-frame") == 0) {
        return commandTestFrame(argc, argv);
    }
    usage();
    return 1;
}
