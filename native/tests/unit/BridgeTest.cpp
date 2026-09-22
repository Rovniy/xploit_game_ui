// Stage 7: the bridge between page script and the host.
//
// The cases drive the public C ABI, which is exactly what the C# layer calls.

#include "bridge/QueueBridge.h"
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

class BridgeTest : public ::testing::Test {
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
        desc.name = "bridge-test";
        const xgu_view_id view = xgu_view_create(&desc);
        EXPECT_NE(view, XGU_INVALID_VIEW);
        EXPECT_EQ(xgu_view_load_html(view, html.c_str(), nullptr), XGU_OK);
        return view;
    }

    // One polled message, copied out so it survives the next poll.
    struct Message {
        bool present = false;
        xgu_message_kind kind = XGU_MSG_EMIT;
        uint64_t id = 0;
        std::string name;
        std::string json;
    };

    static Message poll(xgu_view_id view) {
        xgu_message raw{};
        raw.struct_size = sizeof(raw);
        Message result;
        if (!xgu_view_poll_message(view, &raw)) {
            return result;
        }
        result.present = true;
        result.kind = raw.kind;
        result.id = raw.id;
        result.name = raw.name ? raw.name : "";
        result.json = raw.json ? raw.json : "";
        return result;
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
};

} // namespace

TEST_F(BridgeTest, EmitReachesTheHost) {
    const xgu_view_id view = load("<script>Unity.emit('play');</script>");

    const Message message = poll(view);
    ASSERT_TRUE(message.present) << transcript();
    EXPECT_EQ(message.kind, XGU_MSG_EMIT);
    EXPECT_EQ(message.name, "play");
    EXPECT_EQ(message.id, 0u);
    EXPECT_TRUE(message.json.empty()) << "no arguments means no payload";

    EXPECT_FALSE(poll(view).present) << "the queue is empty again";
}

TEST_F(BridgeTest, EmitCarriesItsArgumentsAsJson) {
    const xgu_view_id view =
        load("<script>Unity.emit('state', 42, 'hi', { hp: 7, tags: ['a', 'b'] }, null, true);</script>");

    const Message message = poll(view);
    ASSERT_TRUE(message.present);
    EXPECT_EQ(message.name, "state");
    EXPECT_EQ(message.json, R"([42,"hi",{"hp":7,"tags":["a","b"]},null,true])");
}

TEST_F(BridgeTest, SendReachesTheUnityOnHandlers) {
    const xgu_view_id view = load(R"html(<script>
      Unity.on('healthChanged', function (value, source) {
        console.log('health=' + value + ' from=' + source);
      });
    </script>)html");

    ASSERT_EQ(xgu_view_send_event(view, "healthChanged", "[75,\"grenade\"]"), XGU_OK);
    EXPECT_TRUE(logged("health=75 from=grenade")) << transcript();
}

TEST_F(BridgeTest, SendWithNoPayloadCallsTheHandlerWithNoArguments) {
    const xgu_view_id view = load(R"html(<script>
      Unity.on('tick', function () { console.log('tick arguments=' + arguments.length); });
    </script>)html");

    ASSERT_EQ(xgu_view_send_event(view, "tick", nullptr), XGU_OK);
    EXPECT_TRUE(logged("tick arguments=0")) << transcript();
}

TEST_F(BridgeTest, SeveralHandlersAllRunAndOffRemovesOne) {
    const xgu_view_id view = load(R"html(<script>
      function first() { console.log('first'); }
      function second() { console.log('second'); }
      Unity.on('ping', first);
      Unity.on('ping', second);
      globalThis.dropFirst = function () { Unity.off('ping', first); };
      globalThis.dropAll = function () { Unity.off('ping'); };
    </script>)html");

    ASSERT_EQ(xgu_view_send_event(view, "ping", nullptr), XGU_OK);
    EXPECT_TRUE(logged("first"));
    EXPECT_TRUE(logged("second"));

    ASSERT_EQ(xgu_view_execute_js(view, "dropFirst();", nullptr), XGU_OK);
    {
        std::lock_guard lock(g_logMutex);
        g_logs.clear();
    }
    ASSERT_EQ(xgu_view_send_event(view, "ping", nullptr), XGU_OK);
    EXPECT_FALSE(logged("first"));
    EXPECT_TRUE(logged("second"));

    ASSERT_EQ(xgu_view_execute_js(view, "dropAll();", nullptr), XGU_OK);
    {
        std::lock_guard lock(g_logMutex);
        g_logs.clear();
    }
    ASSERT_EQ(xgu_view_send_event(view, "ping", nullptr), XGU_OK);
    EXPECT_FALSE(logged("second"));
}

TEST_F(BridgeTest, CallResolvesWithTheHostReply) {
    const xgu_view_id view = load(R"html(<script>
      Unity.call('getProfile', 'player-1').then(function (profile) {
        console.log('name=' + profile.name + ' level=' + profile.level);
      });
    </script>)html");

    const Message message = poll(view);
    ASSERT_TRUE(message.present) << transcript();
    EXPECT_EQ(message.kind, XGU_MSG_CALL);
    EXPECT_NE(message.id, 0u);
    EXPECT_EQ(message.name, "getProfile");
    EXPECT_EQ(message.json, R"(["player-1"])");

    ASSERT_EQ(xgu_view_reply(view, message.id, true, R"({"name":"Ada","level":12})"), XGU_OK);
    EXPECT_TRUE(logged("name=Ada level=12")) << transcript();
}

TEST_F(BridgeTest, CallRejectsWhenTheHostReportsAFailure) {
    const xgu_view_id view = load(R"html(<script>
      Unity.call('boom').catch(function (error) {
        console.log('rejected ' + error.name + ': ' + error.message);
      });
    </script>)html");

    const Message message = poll(view);
    ASSERT_TRUE(message.present);
    ASSERT_EQ(xgu_view_reply(view, message.id, false,
                             R"json({"name":"InvalidOperationException","message":"no save slot","stack":"at Load()"})json"),
              XGU_OK);
    EXPECT_TRUE(logged("rejected InvalidOperationException: no save slot")) << transcript();
}

TEST_F(BridgeTest, CallCanBeAwaited) {
    const xgu_view_id view = load(R"html(<script>
      (async function () {
        const total = await Unity.call('add', 2, 3);
        console.log('total=' + total);
      })();
    </script>)html");

    const Message message = poll(view);
    ASSERT_TRUE(message.present);
    EXPECT_EQ(message.json, "[2,3]");
    ASSERT_EQ(xgu_view_reply(view, message.id, true, "5"), XGU_OK);
    EXPECT_TRUE(logged("total=5")) << transcript();
}

TEST_F(BridgeTest, EachCallGetsItsOwnId) {
    const xgu_view_id view = load(R"html(<script>
      Unity.call('a').then(function (v) { console.log('a=' + v); });
      Unity.call('b').then(function (v) { console.log('b=' + v); });
    </script>)html");

    const Message first = poll(view);
    const Message second = poll(view);
    ASSERT_TRUE(first.present);
    ASSERT_TRUE(second.present);
    EXPECT_NE(first.id, second.id);

    // Answer them out of order: each promise must still get its own value.
    ASSERT_EQ(xgu_view_reply(view, second.id, true, "\"second\""), XGU_OK);
    ASSERT_EQ(xgu_view_reply(view, first.id, true, "\"first\""), XGU_OK);
    EXPECT_TRUE(logged("a=first")) << transcript();
    EXPECT_TRUE(logged("b=second"));
}

TEST_F(BridgeTest, ReplyingTwiceIsIgnored) {
    const xgu_view_id view = load("<script>Unity.call('once').then(function (v) { console.log('got=' + v); });</script>");

    const Message message = poll(view);
    ASSERT_TRUE(message.present);
    ASSERT_EQ(xgu_view_reply(view, message.id, true, "1"), XGU_OK);
    ASSERT_EQ(xgu_view_reply(view, message.id, true, "2"), XGU_OK);
    EXPECT_TRUE(logged("got=1"));
    EXPECT_FALSE(logged("got=2")) << "a settled promise does not change";
}

TEST_F(BridgeTest, ACyclicArgumentRejectsTheCallInsteadOfThrowing) {
    // The caller is awaiting a promise, so a payload that cannot be serialised
    // has to come back as a rejection rather than a synchronous throw.
    const xgu_view_id view = load(R"html(<script>
      const cyclic = {};
      cyclic.self = cyclic;
      Unity.call('nope', cyclic).catch(function (error) { console.log('rejected: ' + error.message); });
    </script>)html");

    EXPECT_FALSE(poll(view).present) << "nothing should reach the host";
    EXPECT_TRUE(logged("rejected: arguments are not JSON-serialisable")) << transcript();
}

TEST_F(BridgeTest, ValuesJsonCannotRepresentFollowJsonRules) {
    // Functions and undefined become null inside an array, exactly as
    // JSON.stringify defines it; nothing is dropped or reordered.
    const xgu_view_id view =
        load("<script>Unity.emit('mixed', 1, undefined, function () {}, NaN, Infinity);</script>");

    const Message message = poll(view);
    ASSERT_TRUE(message.present);
    EXPECT_EQ(message.json, "[1,null,null,null,null]");
}

TEST_F(BridgeTest, ACyclicEmitPayloadThrowsInThePage) {
    const xgu_view_id view = load(R"html(<script>
      const cyclic = {};
      cyclic.self = cyclic;
      try {
        Unity.emit('loop', cyclic);
      } catch (error) {
        console.log('emit threw: ' + error.name);
      }
    </script>)html");

    EXPECT_FALSE(poll(view).present);
    EXPECT_TRUE(logged("emit threw: TypeError")) << transcript();
}

TEST_F(BridgeTest, HostEventsQueuedBeforeAnyScriptStillArrive) {
    // The bridge belongs to the view, not to the document, so the host can send
    // before anything is loaded.
    xgu_view_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.width = 64;
    desc.height = 64;
    desc.device_pixel_ratio = 1.0f;
    desc.format = XGU_FORMAT_RGBA8;
    desc.provider = XGU_PROVIDER_CPU;
    const xgu_view_id view = xgu_view_create(&desc);
    ASSERT_NE(view, XGU_INVALID_VIEW);

    ASSERT_EQ(xgu_view_send_event(view, "early", "[1]"), XGU_OK);
    ASSERT_EQ(xgu_view_load_html(view,
                                 "<script>Unity.on('early', function (v) { console.log('early=' + v); });</script>",
                                 nullptr),
              XGU_OK);
    // The queued message is delivered on the next pump.
    ASSERT_EQ(xgu_view_send_event(view, "late", nullptr), XGU_OK);
    EXPECT_TRUE(logged("early=1")) << transcript();
}

TEST_F(BridgeTest, RejectsMalformedCalls) {
    const xgu_view_id view = load("<script></script>");
    EXPECT_EQ(xgu_view_send_event(view, nullptr, nullptr), XGU_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(xgu_view_send_event(view, "", nullptr), XGU_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(xgu_view_reply(view, 0, true, nullptr), XGU_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(xgu_view_send_event(XGU_INVALID_VIEW, "x", nullptr), XGU_ERR_INVALID_VIEW);

    xgu_message raw{};
    raw.struct_size = 4;
    EXPECT_FALSE(xgu_view_poll_message(view, &raw));
    EXPECT_FALSE(xgu_view_poll_message(view, nullptr));
}

// --- the queue itself ---------------------------------------------------------

TEST(QueueBridgeTest, KeepsOrderInBothDirections) {
    xgu::bridge::QueueBridge bridge;
    for (int i = 0; i < 3; ++i) {
        xgu::BridgeMessage message;
        message.name = std::to_string(i);
        EXPECT_TRUE(bridge.postToHost(std::move(message)));
    }
    for (int i = 0; i < 3; ++i) {
        xgu::BridgeMessage out;
        ASSERT_TRUE(bridge.pollFromPage(out));
        EXPECT_EQ(out.name, std::to_string(i));
    }
    xgu::BridgeMessage empty;
    EXPECT_FALSE(bridge.pollFromPage(empty));
}

TEST(QueueBridgeTest, RefusesPayloadsOverTheLimit) {
    xgu::bridge::QueueBridge bridge;
    xgu::BridgeMessage message;
    message.name = "big";
    message.json.assign(xgu::bridge::QueueBridge::kMaxPayloadBytes + 1, 'x');
    EXPECT_FALSE(bridge.postToHost(std::move(message)));
    EXPECT_EQ(bridge.pendingForHost(), 0u);
}

TEST(QueueBridgeTest, DropsTheOldestEmitButNeverACall) {
    xgu::bridge::QueueBridge bridge;
    // Fill the queue with emits, then one call at the end.
    for (size_t i = 0; i < xgu::bridge::QueueBridge::kMaxQueued; ++i) {
        xgu::BridgeMessage message;
        message.kind = xgu::BridgeMessageKind::Emit;
        message.name = std::to_string(i);
        ASSERT_TRUE(bridge.postToHost(std::move(message)));
    }
    EXPECT_EQ(bridge.droppedCount(), 0u);

    xgu::BridgeMessage call;
    call.kind = xgu::BridgeMessageKind::Call;
    call.id = 1;
    call.name = "call";
    EXPECT_TRUE(bridge.postToHost(std::move(call))) << "a call makes room for itself";
    EXPECT_EQ(bridge.droppedCount(), 1u);

    xgu::BridgeMessage first;
    ASSERT_TRUE(bridge.pollFromPage(first));
    EXPECT_EQ(first.name, "1") << "the oldest emit was the one dropped";
}

TEST(QueueBridgeTest, RefusesWhenOnlyCallsAreQueued) {
    xgu::bridge::QueueBridge bridge;
    for (size_t i = 0; i < xgu::bridge::QueueBridge::kMaxQueued; ++i) {
        xgu::BridgeMessage message;
        message.kind = xgu::BridgeMessageKind::Call;
        message.id = i + 1;
        ASSERT_TRUE(bridge.postToHost(std::move(message)));
    }
    xgu::BridgeMessage another;
    another.kind = xgu::BridgeMessageKind::Call;
    another.id = 99999;
    EXPECT_FALSE(bridge.postToHost(std::move(another))) << "a call is never silently lost";
}
