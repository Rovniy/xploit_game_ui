// setTimeout, setInterval, requestAnimationFrame and performance.now.
//
// Time is driven by xgu_tick, so the tests advance the clock themselves and
// nothing depends on wall-clock timing.

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

class TimerTest : public ::testing::Test {
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
        xgu_views_destroy_all();
        xgu_set_log_callback(nullptr, nullptr);
    }

    xgu_view_id load(const std::string& html) {
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = 64;
        desc.height = 64;
        desc.device_pixel_ratio = 1.0f;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.name = "timer-test";
        const xgu_view_id view = xgu_view_create(&desc);
        EXPECT_NE(view, XGU_INVALID_VIEW);
        EXPECT_EQ(xgu_view_load_html(view, html.c_str(), nullptr), XGU_OK);
        // No tick here: the tests drive the clock, and the first one they make
        // is what performance.now() counts from.
        return view;
    }

    static int countLogged(const std::string& needle) {
        std::lock_guard lock(g_logMutex);
        int count = 0;
        for (const std::string& line : g_logs) {
            if (line.find(needle) != std::string::npos) {
                ++count;
            }
        }
        return count;
    }

    static bool logged(const std::string& needle) { return countLogged(needle) > 0; }

    static std::string transcript() {
        std::lock_guard lock(g_logMutex);
        std::string all;
        for (const std::string& line : g_logs) {
            all += line;
            all += "\n";
        }
        return all;
    }
};

} // namespace

TEST_F(TimerTest, SetTimeoutFiresOnceAfterItsDelay) {
    const xgu_view_id view = load("<script>setTimeout(function () { console.log('fired'); }, 100);</script>");
    (void)view;

    xgu_tick(0.05);
    EXPECT_FALSE(logged("fired")) << "too early";

    xgu_tick(0.15);
    EXPECT_EQ(countLogged("fired"), 1) << transcript();

    xgu_tick(1.0);
    EXPECT_EQ(countLogged("fired"), 1) << "a timeout does not repeat";
}

TEST_F(TimerTest, SetTimeoutPassesItsExtraArguments) {
    load("<script>setTimeout(function (a, b) { console.log('got ' + a + ' ' + b); }, 0, 'x', 7);</script>");
    xgu_tick(0.01);
    EXPECT_TRUE(logged("got x 7")) << transcript();
}

TEST_F(TimerTest, SetIntervalRepeats) {
    load("<script>globalThis.n = 0; setInterval(function () { console.log('tick' + (++globalThis.n)); }, 100);</script>");

    xgu_tick(0.15);
    xgu_tick(0.25);
    xgu_tick(0.35);
    EXPECT_EQ(countLogged("tick1"), 1);
    EXPECT_EQ(countLogged("tick2"), 1);
    EXPECT_EQ(countLogged("tick3"), 1) << transcript();
}

TEST_F(TimerTest, ClearTimeoutStopsAPendingTimer) {
    load(R"html(<script>
      const id = setTimeout(function () { console.log('should not run'); }, 100);
      clearTimeout(id);
    </script>)html");

    xgu_tick(1.0);
    EXPECT_FALSE(logged("should not run"));
}

TEST_F(TimerTest, ClearIntervalStopsARepeatingTimerFromInsideItself) {
    load(R"html(<script>
      let count = 0;
      const id = setInterval(function () {
        console.log('run' + (++count));
        if (count === 2) {
          clearInterval(id);
        }
      }, 50);
    </script>)html");

    for (double t = 0.06; t < 0.6; t += 0.06) {
        xgu_tick(t);
    }
    EXPECT_EQ(countLogged("run1"), 1);
    EXPECT_EQ(countLogged("run2"), 1);
    EXPECT_FALSE(logged("run3")) << transcript();
}

TEST_F(TimerTest, ATimeoutScheduledFromATimeoutWaitsForTheNextTick) {
    load(R"html(<script>
      setTimeout(function () {
        console.log('outer');
        setTimeout(function () { console.log('inner'); }, 0);
      }, 0);
    </script>)html");

    xgu_tick(0.01);
    EXPECT_TRUE(logged("outer"));
    EXPECT_FALSE(logged("inner")) << "the nested timer belongs to the next frame";

    xgu_tick(0.02);
    EXPECT_TRUE(logged("inner")) << transcript();
}

TEST_F(TimerTest, RequestAnimationFrameRunsOncePerTick) {
    load(R"html(<script>
      let frames = 0;
      function step() {
        console.log('frame' + (++frames));
        requestAnimationFrame(step);
      }
      requestAnimationFrame(step);
    </script>)html");

    xgu_tick(0.016);
    xgu_tick(0.032);
    xgu_tick(0.048);
    EXPECT_EQ(countLogged("frame1"), 1);
    EXPECT_EQ(countLogged("frame2"), 1);
    EXPECT_EQ(countLogged("frame3"), 1) << transcript();
}

TEST_F(TimerTest, CancelAnimationFrameStopsTheCallback) {
    load(R"html(<script>
      const id = requestAnimationFrame(function () { console.log('should not run'); });
      cancelAnimationFrame(id);
    </script>)html");

    xgu_tick(0.016);
    EXPECT_FALSE(logged("should not run"));
}

TEST_F(TimerTest, AnimationFrameCallbacksGetTheTimestamp) {
    // The first tick starts the clock, so the timestamp is measured from it.
    load(R"html(<script>
      requestAnimationFrame(function () {
        requestAnimationFrame(function (t) { console.log('t=' + Math.round(t)); });
      });
    </script>)html");

    xgu_tick(0.0);
    xgu_tick(0.25);
    EXPECT_TRUE(logged("t=250")) << transcript();
}

TEST_F(TimerTest, PerformanceNowAdvancesWithTheClock) {
    const xgu_view_id view =
        load("<script>globalThis.report = function () { console.log('now=' + Math.round(performance.now())); };</script>");

    xgu_tick(0.0);
    xgu_tick(0.5);
    ASSERT_EQ(xgu_view_execute_js(view, "report();", nullptr), XGU_OK);
    EXPECT_TRUE(logged("now=500")) << transcript();
}

TEST_F(TimerTest, ATimerThatThrowsDoesNotStopTheOthers) {
    load(R"html(<script>
      setTimeout(function () { throw new Error('boom'); }, 0);
      setTimeout(function () { console.log('still ran'); }, 0);
    </script>)html");

    xgu_tick(0.01);
    EXPECT_TRUE(logged("Uncaught Error: boom")) << transcript();
    EXPECT_TRUE(logged("still ran"));
}

TEST_F(TimerTest, TimersFromAPreviousDocumentDoNotSurviveAReload) {
    const xgu_view_id view = load("<script>setInterval(function () { console.log('old'); }, 50);</script>");

    xgu_tick(0.06);
    ASSERT_TRUE(logged("old"));

    ASSERT_EQ(xgu_view_load_html(view, "<script>console.log('new document');</script>", nullptr), XGU_OK);
    {
        std::lock_guard lock(g_logMutex);
        g_logs.clear();
    }
    xgu_tick(0.2);
    xgu_tick(0.3);
    EXPECT_FALSE(logged("old")) << "the old isolate and its timers are gone";
}

TEST_F(TimerTest, ATimerThatChangesTheDomRepaints) {
    const xgu_view_id view = load(R"html(
      <style>html,body{margin:0;width:100%;height:100%}
             #box{position:absolute;left:0;top:0;width:64px;height:64px;background:#ff0000}</style>
      <div id="box"></div>
      <script>
        setTimeout(function () { document.getElementById('box').style.backgroundColor = '#0000ff'; }, 50);
      </script>)html");

    const void* pixels = nullptr;
    uint32_t size = 0, w = 0, h = 0;
    ASSERT_TRUE(xgu_view_acquire_pixels(view, &pixels, &size, &w, &h, nullptr));
    const auto* bytes = static_cast<const uint8_t*>(pixels);
    const size_t centre = (static_cast<size_t>(h - 1 - 32) * w + 32) * 4;
    EXPECT_EQ(bytes[centre], 255) << "red to start with";
    xgu_view_release_pixels(view);

    xgu_tick(0.1);
    ASSERT_TRUE(xgu_view_acquire_pixels(view, &pixels, &size, &w, &h, nullptr)) << "the timer should repaint";
    bytes = static_cast<const uint8_t*>(pixels);
    EXPECT_EQ(bytes[centre + 2], 255) << "blue after the timer ran";
    xgu_view_release_pixels(view);
}
