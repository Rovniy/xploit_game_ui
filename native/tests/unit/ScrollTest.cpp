// Scrolling: overflow: scroll and auto, the wheel, the scroll properties in
// script, and getBoundingClientRect.

#include "core/Runtime.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <gtest/gtest.h>

#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex g_logMutex;
std::vector<std::string> g_logs;

void captureLog(void*, int, const char* message) {
    std::lock_guard lock(g_logMutex);
    g_logs.emplace_back(message ? message : "");
}

// A 100x100 viewport holding a 100x300 column, so it scrolls 200px vertically.
constexpr const char* kScrollPage = R"html(
  <style>
    html, body { margin: 0; width: 100%; height: 100% }
    #list { position: absolute; left: 0; top: 0; width: 100px; height: 100px; overflow-y: scroll; }
    .item { width: 100px; height: 100px; }
    #a { background: #ff0000 }
    #b { background: #00ff00 }
    #c { background: #0000ff }
  </style>
  <div id="list">
    <div id="a" class="item"></div>
    <div id="b" class="item"></div>
    <div id="c" class="item"></div>
  </div>)html";

class ScrollTest : public ::testing::Test {
protected:
    void SetUp() override {
        {
            std::lock_guard lock(g_logMutex);
            g_logs.clear();
        }
        xgu_init_desc init{};
        init.struct_size = sizeof(init);
        init.log_fn = &captureLog;
        init.flags = XGU_INIT_SINGLE_THREADED;
        ASSERT_EQ(xgu_initialize(&init), XGU_OK);
        xgu_set_log_callback(&captureLog, nullptr);
        xgu::Runtime::instance().render().setNoDevice();
    }

    void TearDown() override {
        if (view_ != XGU_INVALID_VIEW) {
            xgu_view_release_pixels(view_);
        }
        xgu_views_destroy_all();
        xgu_set_log_callback(nullptr, nullptr);
    }

    xgu_view_id load(const std::string& html, uint32_t size = 100) {
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = size;
        desc.height = size;
        desc.device_pixel_ratio = 1.0f;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.name = "scroll-test";
        view_ = xgu_view_create(&desc);
        EXPECT_NE(view_, XGU_INVALID_VIEW);
        EXPECT_EQ(xgu_view_load_html(view_, html.c_str(), nullptr), XGU_OK);
        return view_;
    }

    void wheel(float x, float y, float deltaY) {
        xgu_input_event event{};
        event.struct_size = sizeof(event);
        event.type = XGU_INPUT_WHEEL;
        event.button = XGU_BUTTON_NONE;
        event.x = x;
        event.y = y;
        event.delta_y = deltaY;
        ASSERT_EQ(xgu_view_send_input(view_, &event), XGU_OK);
    }

    void click(float x, float y) {
        xgu_input_event down{};
        down.struct_size = sizeof(down);
        down.type = XGU_INPUT_MOUSE_DOWN;
        down.button = XGU_BUTTON_LEFT;
        down.x = x;
        down.y = y;
        ASSERT_EQ(xgu_view_send_input(view_, &down), XGU_OK);
        xgu_input_event up = down;
        up.type = XGU_INPUT_MOUSE_UP;
        ASSERT_EQ(xgu_view_send_input(view_, &up), XGU_OK);
    }

    // Colour of one pixel, in top-down view coordinates.
    struct Pixel {
        uint8_t r = 0, g = 0, b = 0, a = 0;
    };

    Pixel pixelAt(uint32_t x, uint32_t y) {
        const void* data = nullptr;
        uint32_t size = 0, w = 0, h = 0;
        if (!xgu_view_acquire_pixels(view_, &data, &size, &w, &h, nullptr)) {
            ADD_FAILURE() << "no frame was painted";
            return Pixel{};
        }
        const auto* bytes = static_cast<const uint8_t*>(data);
        const size_t offset = (static_cast<size_t>(h - 1 - y) * w + x) * 4;
        const Pixel pixel{bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]};
        xgu_view_release_pixels(view_);
        return pixel;
    }

    static bool logged(const std::string& needle) {
        std::lock_guard lock(g_logMutex);
        for (const std::string& line : g_logs) {
            if (line.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    static std::string transcript() {
        std::lock_guard lock(g_logMutex);
        std::string all;
        for (const std::string& line : g_logs) {
            all += line;
            all += "\n";
        }
        return all;
    }

    xgu_view_id view_ = XGU_INVALID_VIEW;
};

} // namespace

TEST_F(ScrollTest, ScrollableExtentComesFromTheContent) {
    load(std::string(kScrollPage) + R"html(<script>
      const list = document.getElementById('list');
      console.log('client=' + list.clientHeight + ' scroll=' + list.scrollHeight + ' top=' + list.scrollTop);
    </script>)html");

    EXPECT_TRUE(logged("client=100 scroll=300 top=0")) << transcript();
}

TEST_F(ScrollTest, AnElementThatFitsDoesNotScroll) {
    load(R"html(
      <style>html,body{margin:0}
             #box{width:100px;height:100px;overflow:scroll}
             #inner{width:50px;height:50px}</style>
      <div id="box"><div id="inner"></div></div>
      <script>
        const box = document.getElementById('box');
        console.log('scroll=' + box.scrollHeight + ' client=' + box.clientHeight);
        box.scrollTop = 500;
        console.log('after=' + box.scrollTop);
      </script>)html");

    EXPECT_TRUE(logged("scroll=100 client=100"));
    EXPECT_TRUE(logged("after=0")) << "nothing to scroll, so the offset stays at zero";
}

TEST_F(ScrollTest, TheWheelScrollsTheContent) {
    load(kScrollPage);

    EXPECT_EQ(pixelAt(50, 50).r, 255) << "the first item is red";
    wheel(50.0f, 50.0f, 100.0f);
    const Pixel after = pixelAt(50, 50);
    EXPECT_EQ(after.g, 255) << "scrolled down by one item, so green is showing";
    EXPECT_EQ(after.r, 0);
}

TEST_F(ScrollTest, ScrollingStopsAtTheEnds) {
    load(std::string(kScrollPage) + R"html(<script>
      globalThis.report = function () { console.log('top=' + document.getElementById('list').scrollTop); };
    </script>)html");

    wheel(50.0f, 50.0f, -100.0f);
    ASSERT_EQ(xgu_view_execute_js(view_, "report();", nullptr), XGU_OK);
    EXPECT_TRUE(logged("top=0")) << "cannot scroll above the start";

    wheel(50.0f, 50.0f, 10000.0f);
    ASSERT_EQ(xgu_view_execute_js(view_, "report();", nullptr), XGU_OK);
    EXPECT_TRUE(logged("top=200")) << "clamped to the scrollable height: " << transcript();
}

TEST_F(ScrollTest, HitTestingFollowsTheScrolledContent) {
    load(std::string(kScrollPage) + R"html(<script>
      document.addEventListener('click', function (e) { console.log('hit=' + e.target.id); });
    </script>)html");

    click(50.0f, 50.0f);
    EXPECT_TRUE(logged("hit=a"));

    wheel(50.0f, 50.0f, 100.0f);
    click(50.0f, 50.0f);
    EXPECT_TRUE(logged("hit=b")) << "after scrolling, the same point is over the second item: " << transcript();
}

TEST_F(ScrollTest, AClickOutsideTheScrollPortStillMisses) {
    load(std::string(kScrollPage) + R"html(<script>
      document.addEventListener('click', function (e) { console.log('hit=' + (e.target.id || 'none')); });
    </script>)html", 300);

    // The list is 100x100; a point below it is outside, even though the content
    // is 300 tall.
    click(50.0f, 250.0f);
    EXPECT_FALSE(logged("hit=c")) << transcript();
}

TEST_F(ScrollTest, ScriptCanSetAndReadTheOffset) {
    load(std::string(kScrollPage) + R"html(<script>
      const list = document.getElementById('list');
      list.scrollTop = 150;
      console.log('set=' + list.scrollTop);
      list.scrollBy(0, 100);
      console.log('clamped=' + list.scrollTop);
      list.scrollTo({ top: 0 });
      console.log('back=' + list.scrollTop);
    </script>)html");

    EXPECT_TRUE(logged("set=150")) << transcript();
    EXPECT_TRUE(logged("clamped=200"));
    EXPECT_TRUE(logged("back=0"));
}

TEST_F(ScrollTest, ScrollIntoViewBringsAChildInto) {
    load(std::string(kScrollPage) + R"html(<script>
      document.getElementById('c').scrollIntoView();
      console.log('top=' + document.getElementById('list').scrollTop);
    </script>)html");

    EXPECT_TRUE(logged("top=200")) << transcript();
}

TEST_F(ScrollTest, TheScrollEventFiresOnTheBoxThatMoved) {
    load(std::string(kScrollPage) + R"html(<script>
      const list = document.getElementById('list');
      list.addEventListener('scroll', function (e) {
        console.log('scrolled ' + e.target.id + ' to ' + list.scrollTop);
      });
      document.addEventListener('scroll', function () { console.log('document heard it'); });
    </script>)html");

    wheel(50.0f, 50.0f, 50.0f);
    EXPECT_TRUE(logged("scrolled list to 50")) << transcript();
    EXPECT_FALSE(logged("document heard it")) << "scroll does not bubble";
}

TEST_F(ScrollTest, PreventingTheWheelStopsTheScroll) {
    load(std::string(kScrollPage) + R"html(<script>
      document.getElementById('list').addEventListener('wheel', function (e) { e.preventDefault(); });
      globalThis.report = function () { console.log('top=' + document.getElementById('list').scrollTop); };
    </script>)html");

    wheel(50.0f, 50.0f, 100.0f);
    ASSERT_EQ(xgu_view_execute_js(view_, "report();", nullptr), XGU_OK);
    EXPECT_TRUE(logged("top=0")) << transcript();
}

TEST_F(ScrollTest, ScrollingChainsToTheAncestorThatCanStillMove) {
    load(R"html(
      <style>
        html, body { margin: 0; width: 100%; height: 100% }
        #outer { position: absolute; left: 0; top: 0; width: 100px; height: 100px; overflow-y: scroll }
        #inner { width: 100px; height: 60px; overflow-y: scroll }
        .tall { width: 100px; height: 400px }
      </style>
      <div id="outer">
        <div id="inner"><div class="tall"></div></div>
        <div class="tall"></div>
      </div>
      <script>
        globalThis.report = function () {
          console.log('inner=' + document.getElementById('inner').scrollTop +
                      ' outer=' + document.getElementById('outer').scrollTop);
        };
      </script>)html");

    // The pointer is over the inner box, so it scrolls first.
    wheel(50.0f, 30.0f, 100.0f);
    ASSERT_EQ(xgu_view_execute_js(view_, "report();", nullptr), XGU_OK);
    EXPECT_TRUE(logged("inner=100 outer=0")) << transcript();

    // Once the inner one is at its end, the wheel moves the outer one.
    wheel(50.0f, 30.0f, 10000.0f);
    wheel(50.0f, 30.0f, 50.0f);
    ASSERT_EQ(xgu_view_execute_js(view_, "report();", nullptr), XGU_OK);
    EXPECT_TRUE(logged("inner=340 outer=50")) << transcript();
}

TEST_F(ScrollTest, OverflowHiddenIsNotScrollableByTheWheel) {
    load(R"html(
      <style>html,body{margin:0;width:100%;height:100%}
             #box{position:absolute;left:0;top:0;width:100px;height:100px;overflow:hidden}
             .tall{width:100px;height:400px}</style>
      <div id="box"><div class="tall"></div></div>
      <script>globalThis.report = function () { console.log('top=' + document.getElementById('box').scrollTop); };</script>)html");

    wheel(50.0f, 50.0f, 100.0f);
    ASSERT_EQ(xgu_view_execute_js(view_, "report();", nullptr), XGU_OK);
    EXPECT_TRUE(logged("top=0")) << "hidden clips but does not scroll";
}

TEST_F(ScrollTest, GetBoundingClientRectReportsThePaintedPosition) {
    load(std::string(kScrollPage) + R"html(<script>
      globalThis.report = function (id) {
        const r = document.getElementById(id).getBoundingClientRect();
        console.log(id + ' at ' + Math.round(r.left) + ',' + Math.round(r.top) +
                    ' size ' + Math.round(r.width) + 'x' + Math.round(r.height) +
                    ' bottom ' + Math.round(r.bottom));
      };
      report('b');
    </script>)html");

    EXPECT_TRUE(logged("b at 0,100 size 100x100 bottom 200")) << transcript();

    // After scrolling, the rectangle follows what is on screen.
    wheel(50.0f, 50.0f, 100.0f);
    ASSERT_EQ(xgu_view_execute_js(view_, "report('b');", nullptr), XGU_OK);
    EXPECT_TRUE(logged("b at 0,0 size 100x100 bottom 100")) << transcript();
}
