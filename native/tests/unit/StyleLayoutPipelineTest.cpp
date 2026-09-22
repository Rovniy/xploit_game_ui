// Stage 4 end-to-end: a document loaded through the C ABI is styled and laid
// out, and JavaScript can read and change the style.

#include "core/Runtime.h"
#include "css/ComputedStyle.h"
#include "dom/Document.h"
#include "dom/Element.h"
#include "layout/LayoutEngine.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

using namespace xgu;

namespace {

std::mutex g_logMutex;
std::vector<std::pair<int, std::string>> g_logs;

void captureLog(void*, int level, const char* message) {
    std::lock_guard lock(g_logMutex);
    g_logs.emplace_back(level, message ? message : "");
}

class PipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        {
            std::lock_guard lock(g_logMutex);
            g_logs.clear();
        }
        root_ = std::filesystem::temp_directory_path() / "xgu_pipeline_test";
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_ / "UI");

        xgu_init_desc init{};
        init.struct_size = sizeof(init);
        init.log_fn = &captureLog;
        init.flags = XGU_INIT_SINGLE_THREADED;
        ASSERT_EQ(xgu_initialize(&init), XGU_OK);
        xgu_set_log_callback(&captureLog, nullptr);
        xgu::Runtime::instance().render().setNoDevice();
    }

    void TearDown() override {
        xgu_views_destroy_all();
        xgu_set_log_callback(nullptr, nullptr);
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    void write(const std::string& relative, const std::string& contents) {
        const std::filesystem::path path = root_ / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << contents;
    }

    xgu_view_id createView(uint32_t width = 400, uint32_t height = 300, float dpr = 1.0f) {
        rootPath_ = root_.string();
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = width;
        desc.height = height;
        desc.device_pixel_ratio = dpr;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.ui_root = rootPath_.c_str();
        desc.name = "pipeline";
        return xgu_view_create(&desc);
    }

    // Runs `fn` with the native view; the runtime is single-threaded here.
    template <typename Fn>
    void withView(xgu_view_id id, Fn fn) {
        const bool found = xgu::Runtime::instance().views().withView(static_cast<xgu::ViewId>(id),
                                                                    [&](xgu::View& view) { fn(view); });
        ASSERT_TRUE(found);
    }

    xgu::layout::Rect frameOf(xgu_view_id id, const std::string& elementId) {
        xgu::layout::Rect result;
        withView(id, [&](xgu::View& view) {
            dom::Document* document = view.documentOrNull();
            xgu::layout::LayoutEngine* engine = view.layoutEngine();
            if (!document || !engine) {
                return;
            }
            if (dom::Element* element = document->getElementById(elementId)) {
                if (xgu::layout::LayoutBox* box = engine->boxFor(*element)) {
                    result = box->borderBox();
                }
            }
        });
        return result;
    }

    const css::ComputedStyle* styleOf(xgu_view_id id, const std::string& elementId) {
        const css::ComputedStyle* result = nullptr;
        withView(id, [&](xgu::View& view) {
            if (dom::Document* document = view.documentOrNull()) {
                if (dom::Element* element = document->getElementById(elementId)) {
                    result = element->computedStyle();
                }
            }
        });
        return result;
    }

    static bool logContains(const std::string& needle) {
        std::lock_guard lock(g_logMutex);
        for (const auto& [level, text] : g_logs) {
            if (text.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    std::filesystem::path root_;
    std::string rootPath_;
};

} // namespace

TEST_F(PipelineTest, StyleElementStylesTheDocument) {
    write("UI/index.html", R"(<html><head><style>
      html, body { margin: 0 }
      #panel { width: 200px; height: 100px; background-color: #123456 }
    </style></head><body><div id="panel"></div></body></html>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);

    const css::ComputedStyle* style = styleOf(view, "panel");
    ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->backgroundColor, css::Color::rgba(0x12, 0x34, 0x56));

    const layout::Rect frame = frameOf(view, "panel");
    EXPECT_FLOAT_EQ(frame.width, 200.0f);
    EXPECT_FLOAT_EQ(frame.height, 100.0f);
    EXPECT_FLOAT_EQ(frame.x, 0.0f);
}

TEST_F(PipelineTest, LinkedStylesheetIsLoadedFromTheUiRoot) {
    write("UI/index.html", "<html><head><link rel=\"stylesheet\" href=\"./theme.css\"></head>"
                            "<body><div id=\"d\"></div></body></html>");
    write("UI/theme.css", "html, body { margin: 0 } #d { width: 50px; height: 25px }");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);
    const layout::Rect frame = frameOf(view, "d");
    EXPECT_FLOAT_EQ(frame.width, 50.0f);
    EXPECT_FLOAT_EQ(frame.height, 25.0f);
}

TEST_F(PipelineTest, StylesheetsCannotEscapeTheUiRoot) {
    {
        std::ofstream file(root_.parent_path() / "xgu_outside.css", std::ios::binary);
        file << "#d { width: 999px }";
    }
    write("UI/index.html", "<html><head><link rel=\"stylesheet\" href=\"../../../xgu_outside.css\"></head>"
                            "<body><div id=\"d\"></div></body></html>");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);
    EXPECT_TRUE(logContains("was rejected"));
    EXPECT_LT(frameOf(view, "d").width, 999.0f);

    std::error_code ec;
    std::filesystem::remove(root_.parent_path() / "xgu_outside.css", ec);
}

TEST_F(PipelineTest, FlexLayoutFromAStylesheet) {
    write("UI/index.html", R"(<html><head><style>
      html, body { margin: 0 }
      #row { display: flex; width: 300px; height: 40px }
      #a { width: 100px }
      #b { flex: 1 }
    </style></head><body><div id="row"><div id="a"></div><div id="b"></div></div></body></html>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);
    EXPECT_FLOAT_EQ(frameOf(view, "a").width, 100.0f);
    EXPECT_FLOAT_EQ(frameOf(view, "b").x, 100.0f);
    EXPECT_FLOAT_EQ(frameOf(view, "b").width, 200.0f);
}

TEST_F(PipelineTest, JavaScriptReadsAndWritesInlineStyle) {
    write("UI/index.html", R"(<html><head><style>html, body { margin: 0 }</style></head><body>
      <div id="d" style="width: 10px"></div>
      <script>
        const d = document.getElementById('d');
        console.log('initial=' + d.style.width);
        d.style.width = '120px';
        d.style.backgroundColor = '#ff0000';
        d.style.setProperty('height', '30px');
        console.log('after=' + d.style.width);
        console.log('camel=' + d.style.backgroundColor);
        console.log('viaGet=' + d.style.getPropertyValue('height'));
        console.log('cssText=' + d.style.cssText);
        console.log('unknown=' + d.style.notAProperty);
      </script></body></html>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);

    EXPECT_TRUE(logContains("initial=10px"));
    EXPECT_TRUE(logContains("after=120px"));
    EXPECT_TRUE(logContains("camel=#ff0000"));
    EXPECT_TRUE(logContains("viaGet=30px"));
    EXPECT_TRUE(logContains("cssText=width: 120px;"));
    EXPECT_TRUE(logContains("unknown=undefined"));

    // The style set from JavaScript reaches layout.
    const layout::Rect frame = frameOf(view, "d");
    EXPECT_FLOAT_EQ(frame.width, 120.0f);
    EXPECT_FLOAT_EQ(frame.height, 30.0f);
    EXPECT_EQ(styleOf(view, "d")->backgroundColor, css::Color::rgba(255, 0, 0));
}

TEST_F(PipelineTest, AtomicInlinesKeepDocumentOrderInTheText) {
    // Regression: placeholders used to be appended after every text run, so an
    // inline-block that came first in the markup was laid out last.
    write("UI/index.html", R"(<html><head><style>
      html, body { margin: 0; font-size: 16px }
      button { padding: 0; border: 0; width: 40px; height: 20px }
    </style></head><body><button id="b">x</button><span id="s">tail</span></body></html>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);

    const layout::Rect button = frameOf(view, "b");
    EXPECT_FLOAT_EQ(button.x, 0.0f) << "the button comes first in the markup";
    EXPECT_FLOAT_EQ(button.width, 40.0f);
}

TEST_F(PipelineTest, AFlexRowOfInlineChildrenStaysARow) {
    // Regression: a flex container whose children were all inline-level was
    // turned into one inline formatting context, so the row became a line of
    // text and the flex rules were ignored.
    write("UI/index.html", R"(<html><head><style>
      html, body { margin: 0; font-size: 16px }
      .row { display: flex; align-items: center; width: 300px }
      .label { width: 60px }
      input { flex: 1; padding: 0; border: 0 }
    </style></head><body>
      <div class="row"><span class="label">Name</span><input id="field" value="x"></div>
    </body></html>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);

    const layout::Rect field = frameOf(view, "field");
    EXPECT_FLOAT_EQ(field.x, 60.0f) << "the input starts right after the label";
    EXPECT_FLOAT_EQ(field.width, 240.0f) << "and flex: 1 gives it the rest of the row";
}

TEST_F(PipelineTest, TextBeforeAnAtomicInlineStaysBeforeIt) {
    write("UI/index.html", R"(<html><head><style>
      html, body { margin: 0; font-size: 16px }
      button { padding: 0; border: 0; width: 40px; height: 20px }
    </style></head><body><span>lead</span><button id="b">x</button></body></html>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);

    const layout::Rect button = frameOf(view, "b");
    EXPECT_GT(button.x, 0.0f) << "the text pushes the button to the right";
}

TEST_F(PipelineTest, JavaScriptCanSetShorthandProperties) {
    // Shorthands have no PropertyId, so the style interceptor used to drop them
    // and "el.style.background = ..." silently did nothing.
    write("UI/index.html", R"(<html><head><style>html, body { margin: 0 }</style></head><body>
      <div id="d"></div>
      <script>
        const d = document.getElementById('d');
        d.style.background = '#0000ff';
        d.style.padding = '4px 8px';
        d.style.border = '2px solid #00ff00';
        console.log('cssText=' + d.style.cssText);
        console.log('shorthandReadsBack=[' + d.style.background + ']');
      </script></body></html>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);

    EXPECT_TRUE(logContains("background-color: #0000ff"));
    EXPECT_TRUE(logContains("padding-top: 4px"));
    EXPECT_TRUE(logContains("border-left-style: solid"));
    // A shorthand is not re-serialised from its longhands (documented deviation).
    EXPECT_TRUE(logContains("shorthandReadsBack=[]"));

    const css::ComputedStyle* style = styleOf(view, "d");
    ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->backgroundColor, css::Color::rgba(0, 0, 255));
    EXPECT_EQ(style->borderColor[css::kTop], css::Color::rgba(0, 255, 0));
}

TEST_F(PipelineTest, ClassChangeFromJavaScriptRestyles) {
    write("UI/index.html", R"(<html><head><style>
      html, body { margin: 0 }
      #d { width: 10px; height: 10px }
      #d.big { width: 200px; height: 80px }
    </style></head><body><div id="d"></div>
      <script>document.getElementById('d').classList.add('big');</script>
    </body></html>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);
    const layout::Rect frame = frameOf(view, "d");
    EXPECT_FLOAT_EQ(frame.width, 200.0f);
    EXPECT_FLOAT_EQ(frame.height, 80.0f);
}

TEST_F(PipelineTest, ElementsAddedFromJavaScriptAreLaidOut) {
    write("UI/index.html", R"(<html><head><style>
      html, body { margin: 0 }
      .item { height: 20px }
    </style></head><body><div id="list"></div>
      <script>
        const list = document.getElementById('list');
        for (let i = 0; i < 3; i++) {
          const item = document.createElement('div');
          item.className = 'item';
          item.id = 'item' + i;
          list.appendChild(item);
        }
      </script></body></html>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);
    EXPECT_FLOAT_EQ(frameOf(view, "item0").y, 0.0f);
    EXPECT_FLOAT_EQ(frameOf(view, "item1").y, 20.0f);
    EXPECT_FLOAT_EQ(frameOf(view, "item2").y, 40.0f);
    EXPECT_FLOAT_EQ(frameOf(view, "list").height, 60.0f);
}

TEST_F(PipelineTest, ResizeRelaysOutViewportUnits) {
    write("UI/index.html", "<html><head><style>html, body { margin: 0 } "
                            "#d { width: 50vw; height: 10vh }</style></head><body><div id=\"d\"></div></body></html>");

    const xgu_view_id view = createView(400, 300);
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);
    EXPECT_FLOAT_EQ(frameOf(view, "d").width, 200.0f);
    EXPECT_FLOAT_EQ(frameOf(view, "d").height, 30.0f);

    ASSERT_EQ(xgu_view_resize(view, 800, 600, 1.0f), XGU_OK);
    withView(view, [](xgu::View& v) { EXPECT_TRUE(v.updateStyleAndLayout()); });
    EXPECT_FLOAT_EQ(frameOf(view, "d").width, 400.0f);
    EXPECT_FLOAT_EQ(frameOf(view, "d").height, 60.0f);
}

TEST_F(PipelineTest, DevicePixelRatioKeepsLayoutInCssPixels) {
    write("UI/index.html", "<html><head><style>html, body { margin: 0 } "
                            "#d { width: 100px; height: 50px }</style></head><body><div id=\"d\"></div></body></html>");

    // 800x600 device pixels at dpr 2 is a 400x300 CSS-pixel viewport.
    const xgu_view_id view = createView(800, 600, 2.0f);
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);
    const layout::Rect frame = frameOf(view, "d");
    EXPECT_FLOAT_EQ(frame.width, 100.0f) << "boxes stay in CSS pixels";
    EXPECT_FLOAT_EQ(frame.height, 50.0f);
}

TEST_F(PipelineTest, ReloadRebuildsStyleAndLayout) {
    write("UI/index.html", "<html><head><style>html, body { margin: 0 } "
                            "#d { width: 60px; height: 20px }</style></head><body><div id=\"d\"></div></body></html>");
    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/index.html"), XGU_OK);
    EXPECT_FLOAT_EQ(frameOf(view, "d").width, 60.0f);

    write("UI/index.html", "<html><head><style>html, body { margin: 0 } "
                            "#d { width: 250px; height: 20px }</style></head><body><div id=\"d\"></div></body></html>");
    ASSERT_EQ(xgu_view_reload(view), XGU_OK);
    EXPECT_FLOAT_EQ(frameOf(view, "d").width, 250.0f);
}
