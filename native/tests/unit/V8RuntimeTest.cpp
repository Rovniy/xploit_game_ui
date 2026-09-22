#include "core/Runtime.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <gtest/gtest.h>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

struct LogEntry {
    int level;
    std::string text;
};

std::mutex g_logMutex;
std::vector<LogEntry> g_logs;

void captureLog(void*, int level, const char* message) {
    std::lock_guard lock(g_logMutex);
    g_logs.push_back({level, message ? message : ""});
}

class V8Test : public ::testing::Test {
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
        view_ = createView("v8-test");
        ASSERT_NE(view_, XGU_INVALID_VIEW);
    }

    void TearDown() override {
        xgu_views_destroy_all();
        xgu_set_log_callback(nullptr, nullptr);
    }

    static xgu_view_id createView(const char* name) {
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = 8;
        desc.height = 8;
        desc.device_pixel_ratio = 1.0f;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.name = name;
        return xgu_view_create(&desc);
    }

    void run(const char* source, const char* origin = "test.js") {
        ASSERT_EQ(xgu_view_execute_js(view_, source, origin), XGU_OK);
    }

    static std::vector<LogEntry> logs() {
        std::lock_guard lock(g_logMutex);
        return g_logs;
    }

    static bool anyLogContains(const std::string& needle, int level = -1) {
        for (const LogEntry& entry : logs()) {
            if ((level < 0 || entry.level == level) && entry.text.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    static std::string lastLogWithLevel(int level) {
        const auto all = logs();
        for (auto it = all.rbegin(); it != all.rend(); ++it) {
            if (it->level == level) {
                return it->text;
            }
        }
        return {};
    }

    xgu_view_id view_ = XGU_INVALID_VIEW;
};

} // namespace

TEST_F(V8Test, ConsoleLogFormatsArguments) {
    run("console.log('hi', 1, 2.5, true, null, undefined, {a: 1, b: 'x'}, [1, 2], 'end')");
    const std::string line = lastLogWithLevel(XGU_LOG_INFO);
    EXPECT_NE(line.find("hi 1 2.5 true null undefined {\"a\":1,\"b\":\"x\"} [1,2] end"), std::string::npos) << line;
}

TEST_F(V8Test, ConsoleLevelsMapToLogLevels) {
    run("console.debug('d'); console.info('i'); console.warn('w'); console.error('e');");
    EXPECT_TRUE(anyLogContains("d", XGU_LOG_DEBUG));
    EXPECT_TRUE(anyLogContains("i", XGU_LOG_INFO));
    EXPECT_TRUE(anyLogContains("w", XGU_LOG_WARNING));
    EXPECT_TRUE(anyLogContains("e", XGU_LOG_ERROR));
}

TEST_F(V8Test, UncaughtErrorIsLoggedWithStack) {
    run("function boom() { throw new Error('kaboom'); }\nboom();", "menu/app.js");
    const std::string line = lastLogWithLevel(XGU_LOG_ERROR);
    EXPECT_NE(line.find("Uncaught Error: kaboom"), std::string::npos) << line;
    EXPECT_NE(line.find("at boom"), std::string::npos) << line;
    EXPECT_NE(line.find("menu/app.js:1"), std::string::npos) << line;
}

TEST_F(V8Test, SyntaxErrorIsLogged) {
    run("let = ;", "broken.js");
    const std::string line = lastLogWithLevel(XGU_LOG_ERROR);
    EXPECT_NE(line.find("SyntaxError"), std::string::npos) << line;
    EXPECT_NE(line.find("broken.js"), std::string::npos) << line;
}

TEST_F(V8Test, MicrotasksRunAfterTheScript) {
    run("Promise.resolve().then(() => console.log('micro')); console.log('sync');");
    const auto all = logs();
    int syncIndex = -1;
    int microIndex = -1;
    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        if (all[i].text.find("sync") != std::string::npos) syncIndex = i;
        if (all[i].text.find("micro") != std::string::npos) microIndex = i;
    }
    ASSERT_GE(syncIndex, 0);
    ASSERT_GE(microIndex, 0);
    EXPECT_LT(syncIndex, microIndex);
}

TEST_F(V8Test, UnhandledRejectionIsLogged) {
    run("Promise.reject(new Error('nobody catches me'));");
    EXPECT_TRUE(anyLogContains("Unhandled promise rejection", XGU_LOG_ERROR));
    EXPECT_TRUE(anyLogContains("nobody catches me", XGU_LOG_ERROR));
}

TEST_F(V8Test, ErrorObjectsPrintTheirStack) {
    run("console.error(new TypeError('typed'))");
    const std::string line = lastLogWithLevel(XGU_LOG_ERROR);
    EXPECT_NE(line.find("TypeError: typed"), std::string::npos) << line;
}

TEST_F(V8Test, GlobalsAreIsolatedPerView) {
    const xgu_view_id other = createView("other");
    ASSERT_NE(other, XGU_INVALID_VIEW);
    run("globalThis.marker = 'first';");
    ASSERT_EQ(xgu_view_execute_js(other, "console.log('marker is ' + typeof marker)", "other.js"), XGU_OK);
    EXPECT_TRUE(anyLogContains("marker is undefined"));
    run("console.log('marker is ' + marker)");
    EXPECT_TRUE(anyLogContains("marker is first"));
}

TEST_F(V8Test, StateStaysAcrossCalls) {
    run("var counter = 41;");
    run("counter += 1; console.log('counter=' + counter);");
    EXPECT_TRUE(anyLogContains("counter=42"));
}

TEST_F(V8Test, WebAssemblyIsNotExposed) {
    run("console.log('wasm:' + typeof WebAssembly)");
    EXPECT_TRUE(anyLogContains("wasm:undefined"));
}

TEST_F(V8Test, StateBecomesJsReady) {
    EXPECT_EQ(xgu_view_get_state(view_), XGU_STATE_CREATED);
    run("1 + 1");
    EXPECT_EQ(xgu_view_get_state(view_), XGU_STATE_JS_READY);
    EXPECT_EQ(xgu_view_set_paused(view_, true), XGU_OK);
    EXPECT_EQ(xgu_view_get_state(view_), XGU_STATE_PAUSED);
    EXPECT_EQ(xgu_view_set_paused(view_, false), XGU_OK);
    EXPECT_EQ(xgu_view_get_state(view_), XGU_STATE_JS_READY);
}

TEST_F(V8Test, InvalidArgumentsAreRejected) {
    EXPECT_EQ(xgu_view_execute_js(view_, nullptr, nullptr), XGU_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(xgu_view_execute_js(0x7777000000000001ull, "1", nullptr), XGU_ERR_INVALID_VIEW);
    EXPECT_EQ(xgu_view_get_state(0x7777000000000001ull), XGU_STATE_DESTROYED);
}

TEST_F(V8Test, ManyCreateDestroyCycles) {
    for (int i = 0; i < 20; ++i) {
        const xgu_view_id v = createView("cycle");
        ASSERT_NE(v, XGU_INVALID_VIEW);
        ASSERT_EQ(xgu_view_execute_js(v, "console.log('cycle ' + (1 + 1))", "cycle.js"), XGU_OK);
        ASSERT_EQ(xgu_view_destroy(v), XGU_OK);
    }
    xgu_tick(1.0);
    EXPECT_TRUE(anyLogContains("cycle 2"));
}

TEST_F(V8Test, TickPumpsWithoutErrors) {
    run("console.log('before tick')");
    xgu_tick(0.016);
    xgu_tick(0.033);
    EXPECT_TRUE(anyLogContains("before tick"));
    EXPECT_FALSE(anyLogContains("Uncaught"));
}

// --- log queue (used by Unity to report runtime-thread logs on the main thread) ---

TEST_F(V8Test, LogQueueBuffersAndDrains) {
    xgu_log_queue_enable(true);
    run("console.log('queued one'); console.warn('queued two');");

    std::vector<std::pair<int, std::string>> drained;
    int level = 0;
    const char* message = nullptr;
    while (xgu_log_poll(&level, &message)) {
        drained.emplace_back(level, message ? message : "");
    }
    xgu_log_queue_enable(false);

    ASSERT_GE(drained.size(), 2u);
    bool sawInfo = false;
    bool sawWarning = false;
    for (const auto& [lvl, text] : drained) {
        if (lvl == XGU_LOG_INFO && text.find("queued one") != std::string::npos) sawInfo = true;
        if (lvl == XGU_LOG_WARNING && text.find("queued two") != std::string::npos) sawWarning = true;
    }
    EXPECT_TRUE(sawInfo);
    EXPECT_TRUE(sawWarning);
    EXPECT_FALSE(xgu_log_poll(&level, &message)) << "queue must be empty after draining";
    EXPECT_EQ(xgu_log_dropped_count(), 0u);
}

TEST_F(V8Test, DisablingTheQueueFlushesToTheCallback) {
    xgu_log_queue_enable(true);
    run("console.log('flushed on disable')");
    xgu_log_queue_enable(false); // flushes through the capture callback
    EXPECT_TRUE(anyLogContains("flushed on disable"));
}
