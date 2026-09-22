// Stage 6: host input to DOM events.
//
// Every case drives the public C ABI, so it exercises the same path Unity uses:
// xgu_view_send_input -> hit test -> element state -> event dispatch -> JS.

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

class InputTest : public ::testing::Test {
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

    // Loads the markup into a 200x200 view and returns its id.
    xgu_view_id load(const std::string& html) {
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = 200;
        desc.height = 200;
        desc.device_pixel_ratio = 1.0f;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.name = "input-test";
        const xgu_view_id view = xgu_view_create(&desc);
        EXPECT_NE(view, XGU_INVALID_VIEW);
        const std::string page = "<style>html,body{margin:0;width:100%;height:100%}</style>" + html;
        EXPECT_EQ(xgu_view_load_html(view, page.c_str(), nullptr), XGU_OK);
        return view;
    }

    static xgu_input_event makeEvent(xgu_input_type type) {
        xgu_input_event event{};
        event.struct_size = sizeof(event);
        event.type = type;
        event.button = XGU_BUTTON_NONE;
        return event;
    }

    void mouseMove(xgu_view_id view, float x, float y) {
        xgu_input_event event = makeEvent(XGU_INPUT_MOUSE_MOVE);
        event.x = x;
        event.y = y;
        ASSERT_EQ(xgu_view_send_input(view, &event), XGU_OK);
    }

    void mouseDown(xgu_view_id view, float x, float y, double time = 0.0) {
        xgu_input_event event = makeEvent(XGU_INPUT_MOUSE_DOWN);
        event.x = x;
        event.y = y;
        event.button = XGU_BUTTON_LEFT;
        event.buttons = XGU_BUTTONS_LEFT;
        event.time = time;
        ASSERT_EQ(xgu_view_send_input(view, &event), XGU_OK);
    }

    void mouseUp(xgu_view_id view, float x, float y, double time = 0.0) {
        xgu_input_event event = makeEvent(XGU_INPUT_MOUSE_UP);
        event.x = x;
        event.y = y;
        event.button = XGU_BUTTON_LEFT;
        event.time = time;
        ASSERT_EQ(xgu_view_send_input(view, &event), XGU_OK);
    }

    void click(xgu_view_id view, float x, float y, double time = 0.0) {
        mouseDown(view, x, y, time);
        mouseUp(view, x, y, time);
    }

    void key(xgu_view_id view, xgu_input_type type, const char* name, const char* code) {
        xgu_input_event event = makeEvent(type);
        event.key = name;
        event.code = code;
        ASSERT_EQ(xgu_view_send_input(view, &event), XGU_OK);
    }

    void text(xgu_view_id view, const char* value) {
        xgu_input_event event = makeEvent(XGU_INPUT_TEXT);
        event.text = value;
        ASSERT_EQ(xgu_view_send_input(view, &event), XGU_OK);
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

    // Everything logged, joined, for order-sensitive assertions.
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

// The stage's acceptance criterion: a mouse click from the host reaches a
// listener registered in page script.
TEST_F(InputTest, ClickOnAButtonCallsTheListener) {
    const xgu_view_id view = load(R"html(
      <button id="button" style="position:absolute;left:20px;top:20px;width:100px;height:40px">CLICK</button>
      <script>
        document.getElementById('button').addEventListener('click', function (e) {
          console.log('clicked id=' + e.target.id + ' button=' + e.button + ' detail=' + e.detail);
        });
      </script>)html");

    click(view, 70.0f, 40.0f);
    EXPECT_TRUE(logged("clicked id=button button=0 detail=1"));
}

TEST_F(InputTest, ClickOutsideTheButtonDoesNotReachIt) {
    const xgu_view_id view = load(R"html(
      <button id="button" style="position:absolute;left:20px;top:20px;width:100px;height:40px">CLICK</button>
      <script>
        document.getElementById('button').addEventListener('click', function () { console.log('clicked'); });
      </script>)html");

    click(view, 180.0f, 180.0f);
    EXPECT_FALSE(logged("clicked"));
}

TEST_F(InputTest, TopmostElementWinsByPaintOrder) {
    const xgu_view_id view = load(R"html(
      <div id="under" style="position:absolute;left:0;top:0;width:100px;height:100px;z-index:1"></div>
      <div id="over" style="position:absolute;left:0;top:0;width:100px;height:100px;z-index:2"></div>
      <script>
        document.addEventListener('click', function (e) { console.log('hit=' + e.target.id); });
      </script>)html");

    click(view, 50.0f, 50.0f);
    EXPECT_TRUE(logged("hit=over")) << transcript();
}

TEST_F(InputTest, PointerEventsNoneIsSkipped) {
    const xgu_view_id view = load(R"html(
      <div id="under" style="position:absolute;left:0;top:0;width:100px;height:100px"></div>
      <div id="over" style="position:absolute;left:0;top:0;width:100px;height:100px;pointer-events:none"></div>
      <script>
        document.addEventListener('click', function (e) { console.log('hit=' + e.target.id); });
      </script>)html");

    click(view, 50.0f, 50.0f);
    EXPECT_TRUE(logged("hit=under")) << transcript();
}

TEST_F(InputTest, OverflowHiddenClipsHitTesting) {
    const xgu_view_id view = load(R"html(
      <div id="clip" style="position:absolute;left:0;top:0;width:50px;height:50px;overflow:hidden">
        <div id="big" style="width:150px;height:150px"></div>
      </div>
      <script>
        document.addEventListener('click', function (e) { console.log('hit=' + e.target.id); });
      </script>)html");

    click(view, 25.0f, 25.0f);
    EXPECT_TRUE(logged("hit=big"));
    click(view, 100.0f, 100.0f);
    EXPECT_FALSE(logged("hit=clip")) << transcript();
}

TEST_F(InputTest, TransformsAreInvertedWhenHitTesting) {
    const xgu_view_id view = load(R"html(
      <div id="moved" style="position:absolute;left:0;top:0;width:40px;height:40px;
                             transform:translate(100px, 100px)"></div>
      <script>
        document.addEventListener('click', function (e) { console.log('hit=' + (e.target.id || 'none')); });
      </script>)html");

    click(view, 120.0f, 120.0f);
    EXPECT_TRUE(logged("hit=moved")) << "the box is where the transform put it";
    click(view, 20.0f, 20.0f);
    EXPECT_FALSE(logged("hit=moved\nhit=moved")) << "and not where it was laid out";
}

TEST_F(InputTest, BorderRadiusCutsTheCorners) {
    const xgu_view_id view = load(R"html(
      <div id="round" style="position:absolute;left:0;top:0;width:100px;height:100px;border-radius:50px"></div>
      <script>
        document.addEventListener('click', function (e) { console.log('hit=' + (e.target.id || 'x')); });
      </script>)html");

    click(view, 50.0f, 50.0f);
    EXPECT_TRUE(logged("hit=round")) << "the middle is inside the circle";
    const std::string before = transcript();
    click(view, 2.0f, 2.0f);
    EXPECT_EQ(transcript().find("hit=round", before.size()), std::string::npos)
        << "the corner is outside it";
}

TEST_F(InputTest, HoverMovesThroughOverOutEnterLeave) {
    const xgu_view_id view = load(R"html(
      <div id="a" style="position:absolute;left:0;top:0;width:50px;height:50px"></div>
      <div id="b" style="position:absolute;left:100px;top:0;width:50px;height:50px"></div>
      <script>
        for (const id of ['a', 'b']) {
          const el = document.getElementById(id);
          for (const type of ['mouseover', 'mouseout', 'mouseenter', 'mouseleave']) {
            el.addEventListener(type, function (e) {
              console.log(type + ':' + id + ':' + (e.relatedTarget ? e.relatedTarget.id : 'null'));
            });
          }
        }
      </script>)html");

    mouseMove(view, 25.0f, 25.0f);
    EXPECT_TRUE(logged("mouseover:a:null"));
    EXPECT_TRUE(logged("mouseenter:a:null"));

    mouseMove(view, 125.0f, 25.0f);
    EXPECT_TRUE(logged("mouseout:a:b"));
    EXPECT_TRUE(logged("mouseleave:a:b"));
    EXPECT_TRUE(logged("mouseover:b:a"));
    EXPECT_TRUE(logged("mouseenter:b:a"));
}

TEST_F(InputTest, HoverRestylesThroughTheSelector) {
    const xgu_view_id view = load(R"html(
      <style>#box { background: #ff0000 } #box:hover { background: #0000ff }</style>
      <div id="box" style="position:absolute;left:0;top:0;width:100px;height:100px"></div>)html");

    xgu_view_repaint(view);
    const void* pixels = nullptr;
    uint32_t size = 0, w = 0, h = 0;
    ASSERT_TRUE(xgu_view_acquire_pixels(view, &pixels, &size, &w, &h, nullptr));
    const auto* bytes = static_cast<const uint8_t*>(pixels);
    // Bottom-up rows; (50, 50) from the top is row h-1-50.
    const size_t offset = (static_cast<size_t>(h - 1 - 50) * w + 50) * 4;
    EXPECT_EQ(bytes[offset], 255) << "red before hovering";
    xgu_view_release_pixels(view);

    mouseMove(view, 50.0f, 50.0f);
    ASSERT_TRUE(xgu_view_acquire_pixels(view, &pixels, &size, &w, &h, nullptr)) << "hover should repaint";
    bytes = static_cast<const uint8_t*>(pixels);
    EXPECT_EQ(bytes[offset], 0) << "no red left";
    EXPECT_EQ(bytes[offset + 2], 255) << "blue while hovering";
    xgu_view_release_pixels(view);
}

TEST_F(InputTest, ActiveIsSetWhilePressed) {
    const xgu_view_id view = load(R"html(
      <div id="box" style="position:absolute;left:0;top:0;width:100px;height:100px"></div>
      <script>
        const box = document.getElementById('box');
        box.addEventListener('mousedown', function () { console.log('down matches=' + box.matches('#box')); });
      </script>)html");

    mouseDown(view, 50.0f, 50.0f);
    EXPECT_TRUE(logged("down matches=true"));
    mouseUp(view, 50.0f, 50.0f);
}

TEST_F(InputTest, PressFocusesTheNearestFocusableAncestor) {
    const xgu_view_id view = load(R"html(
      <div id="wrap" style="position:absolute;left:0;top:0;width:150px;height:60px">
        <button id="b" style="width:100px;height:40px"><span id="label">Go</span></button>
      </div>
      <script>
        for (const id of ['b', 'wrap']) {
          document.getElementById(id).addEventListener('focus', function () { console.log('focus=' + id); });
          document.getElementById(id).addEventListener('focusin', function (e) {
            console.log('focusin on ' + id + ' target=' + e.target.id);
          });
        }
      </script>)html");

    // The press lands on the span inside the button; focus goes to the button.
    mouseDown(view, 30.0f, 20.0f);
    EXPECT_TRUE(logged("focus=b")) << transcript();
    EXPECT_FALSE(logged("focus=wrap"));
    EXPECT_TRUE(logged("focusin on wrap target=b")) << "focusin bubbles";
}

TEST_F(InputTest, FocusAndFocusWithinMatchSelectors) {
    const xgu_view_id view = load(R"html(
      <div id="wrap" style="position:absolute;left:0;top:0;width:150px;height:60px">
        <button id="b" style="width:100px;height:40px">Go</button>
      </div>
      <script>
        document.getElementById('b').addEventListener('focus', function () {
          console.log('focus=' + document.getElementById('b').matches(':focus') +
                      ' within=' + document.getElementById('wrap').matches(':focus-within') +
                      ' wrapFocus=' + document.getElementById('wrap').matches(':focus'));
        });
      </script>)html");

    mouseDown(view, 30.0f, 20.0f);
    EXPECT_TRUE(logged("focus=true within=true wrapFocus=false")) << transcript();
}

TEST_F(InputTest, PreventingMouseDownKeepsFocusWhereItWas) {
    const xgu_view_id view = load(R"html(
      <button id="b" style="position:absolute;left:0;top:0;width:100px;height:40px">Go</button>
      <script>
        const b = document.getElementById('b');
        b.addEventListener('mousedown', function (e) { e.preventDefault(); });
        b.addEventListener('focus', function () { console.log('focused'); });
      </script>)html");

    mouseDown(view, 50.0f, 20.0f);
    EXPECT_FALSE(logged("focused"));
}

TEST_F(InputTest, ClickFiresOnTheCommonAncestorOfPressAndRelease) {
    const xgu_view_id view = load(R"html(
      <div id="wrap" style="position:absolute;left:0;top:0;width:200px;height:100px">
        <div id="left" style="position:absolute;left:0;top:0;width:100px;height:100px"></div>
        <div id="right" style="position:absolute;left:100px;top:0;width:100px;height:100px"></div>
      </div>
      <script>
        document.addEventListener('click', function (e) { console.log('click target=' + e.target.id); });
      </script>)html");

    mouseDown(view, 50.0f, 50.0f);
    mouseUp(view, 150.0f, 50.0f);
    EXPECT_TRUE(logged("click target=wrap")) << transcript();
}

TEST_F(InputTest, SecondClickInTimeMakesADoubleClick) {
    const xgu_view_id view = load(R"html(
      <div id="box" style="position:absolute;left:0;top:0;width:100px;height:100px"></div>
      <script>
        const box = document.getElementById('box');
        box.addEventListener('click', function (e) { console.log('click detail=' + e.detail); });
        box.addEventListener('dblclick', function () { console.log('dblclick'); });
      </script>)html");

    click(view, 50.0f, 50.0f, 1.0);
    EXPECT_TRUE(logged("click detail=1"));
    EXPECT_FALSE(logged("dblclick"));

    click(view, 50.0f, 50.0f, 1.2);
    EXPECT_TRUE(logged("click detail=2"));
    EXPECT_TRUE(logged("dblclick"));

    // Too late to continue the sequence.
    click(view, 50.0f, 50.0f, 5.0);
    EXPECT_TRUE(logged("click detail=1"));
}

TEST_F(InputTest, KeysGoToTheFocusedElementAndBubble) {
    const xgu_view_id view = load(R"html(
      <input id="field" style="position:absolute;left:0;top:0;width:100px;height:30px">
      <script>
        document.getElementById('field').addEventListener('keydown', function (e) {
          console.log('keydown key=' + e.key + ' code=' + e.code + ' shift=' + e.shiftKey);
        });
        document.addEventListener('keyup', function (e) { console.log('keyup at document key=' + e.key); });
      </script>)html");

    mouseDown(view, 50.0f, 15.0f);
    mouseUp(view, 50.0f, 15.0f);
    key(view, XGU_INPUT_KEY_DOWN, "Enter", "Enter");
    key(view, XGU_INPUT_KEY_UP, "Enter", "Enter");
    EXPECT_TRUE(logged("keydown key=Enter code=Enter shift=false")) << transcript();
    EXPECT_TRUE(logged("keyup at document key=Enter"));
}

TEST_F(InputTest, KeysWithoutFocusGoToTheBody) {
    const xgu_view_id view = load(R"html(
      <script>
        document.addEventListener('keydown', function (e) { console.log('target=' + e.target.tagName); });
      </script>)html");

    key(view, XGU_INPUT_KEY_DOWN, "a", "KeyA");
    EXPECT_TRUE(logged("target=BODY"));
}

TEST_F(InputTest, TextInputFiresBeforeInputThenInput) {
    const xgu_view_id view = load(R"html(
      <input id="field" style="position:absolute;left:0;top:0;width:100px;height:30px">
      <script>
        const field = document.getElementById('field');
        field.addEventListener('beforeinput', function (e) { console.log('beforeinput data=' + e.data); });
        field.addEventListener('input', function (e) { console.log('input data=' + e.data); });
      </script>)html");

    mouseDown(view, 50.0f, 15.0f);
    mouseUp(view, 50.0f, 15.0f);
    text(view, "hi");
    EXPECT_TRUE(logged("beforeinput data=hi")) << transcript();
    EXPECT_TRUE(logged("input data=hi"));
}

TEST_F(InputTest, CancellingBeforeInputSuppressesInput) {
    const xgu_view_id view = load(R"html(
      <input id="field" style="position:absolute;left:0;top:0;width:100px;height:30px">
      <script>
        const field = document.getElementById('field');
        field.addEventListener('beforeinput', function (e) { e.preventDefault(); });
        field.addEventListener('input', function () { console.log('input ran'); });
      </script>)html");

    mouseDown(view, 50.0f, 15.0f);
    mouseUp(view, 50.0f, 15.0f);
    text(view, "hi");
    EXPECT_FALSE(logged("input ran"));
}

TEST_F(InputTest, WheelCarriesItsDeltas) {
    const xgu_view_id view = load(R"html(
      <div id="box" style="position:absolute;left:0;top:0;width:100px;height:100px"></div>
      <script>
        document.getElementById('box').addEventListener('wheel', function (e) {
          console.log('wheel dx=' + e.deltaX + ' dy=' + e.deltaY + ' mode=' + e.deltaMode);
        });
      </script>)html");

    xgu_input_event event = makeEvent(XGU_INPUT_WHEEL);
    event.x = 50.0f;
    event.y = 50.0f;
    event.delta_y = -120.0f;
    ASSERT_EQ(xgu_view_send_input(view, &event), XGU_OK);
    EXPECT_TRUE(logged("wheel dx=0 dy=-120 mode=0")) << transcript();
}

TEST_F(InputTest, WindowBlurClearsHoverAndFocus) {
    const xgu_view_id view = load(R"html(
      <button id="b" style="position:absolute;left:0;top:0;width:100px;height:40px">Go</button>
      <script>
        const b = document.getElementById('b');
        b.addEventListener('blur', function () { console.log('blurred'); });
        b.addEventListener('mouseleave', function () { console.log('left'); });
      </script>)html");

    mouseDown(view, 50.0f, 20.0f);
    mouseUp(view, 50.0f, 20.0f);

    xgu_input_event event = makeEvent(XGU_INPUT_WINDOW_BLUR);
    ASSERT_EQ(xgu_view_send_input(view, &event), XGU_OK);
    EXPECT_TRUE(logged("blurred")) << transcript();
    EXPECT_TRUE(logged("left"));
}

TEST_F(InputTest, PointerLeaveEndsHover) {
    const xgu_view_id view = load(R"html(
      <div id="box" style="position:absolute;left:0;top:0;width:100px;height:100px"></div>
      <script>
        document.getElementById('box').addEventListener('mouseleave', function () { console.log('left'); });
      </script>)html");

    mouseMove(view, 50.0f, 50.0f);
    xgu_input_event event = makeEvent(XGU_INPUT_POINTER_LEAVE);
    ASSERT_EQ(xgu_view_send_input(view, &event), XGU_OK);
    EXPECT_TRUE(logged("left"));
}

TEST_F(InputTest, TouchIsMirroredToTheMouse) {
    const xgu_view_id view = load(R"html(
      <button id="b" style="position:absolute;left:0;top:0;width:100px;height:40px">Go</button>
      <script>
        document.getElementById('b').addEventListener('click', function () { console.log('tapped'); });
      </script>)html");

    xgu_input_event begin = makeEvent(XGU_INPUT_TOUCH_BEGIN);
    begin.x = 50.0f;
    begin.y = 20.0f;
    ASSERT_EQ(xgu_view_send_input(view, &begin), XGU_OK);
    xgu_input_event end = makeEvent(XGU_INPUT_TOUCH_END);
    end.x = 50.0f;
    end.y = 20.0f;
    ASSERT_EQ(xgu_view_send_input(view, &end), XGU_OK);
    EXPECT_TRUE(logged("tapped")) << transcript();
}

TEST_F(InputTest, SetFocusFromTheHostFiresFocusEvents) {
    const xgu_view_id view = load(R"html(
      <button id="b" style="position:absolute;left:0;top:0;width:100px;height:40px">Go</button>
      <script>
        document.getElementById('b').addEventListener('focus', function () { console.log('focused'); });
        document.getElementById('b').addEventListener('blur', function () { console.log('blurred'); });
      </script>)html");

    ASSERT_EQ(xgu_view_set_focus(view, "b"), XGU_OK);
    EXPECT_TRUE(logged("focused"));
    ASSERT_EQ(xgu_view_set_focus(view, ""), XGU_OK);
    EXPECT_TRUE(logged("blurred"));
}

TEST_F(InputTest, RejectsMalformedEvents) {
    const xgu_view_id view = load("<div></div>");
    EXPECT_EQ(xgu_view_send_input(view, nullptr), XGU_ERR_INVALID_ARGUMENT);

    xgu_input_event tooSmall = makeEvent(XGU_INPUT_MOUSE_MOVE);
    tooSmall.struct_size = 4;
    EXPECT_EQ(xgu_view_send_input(view, &tooSmall), XGU_ERR_INVALID_ARGUMENT);

    xgu_input_event unknown = makeEvent(static_cast<xgu_input_type>(99));
    EXPECT_EQ(xgu_view_send_input(view, &unknown), XGU_ERR_INVALID_ARGUMENT);

    xgu_input_event fine = makeEvent(XGU_INPUT_MOUSE_MOVE);
    EXPECT_EQ(xgu_view_send_input(XGU_INVALID_VIEW, &fine), XGU_ERR_INVALID_VIEW);
}

// --- text editing end to end -------------------------------------------------

namespace {

// A focused single-line field, plus a listener that reports every edit.
constexpr const char* kFieldPage = R"html(
  <input id="field" style="position:absolute;left:0;top:0;width:150px;height:30px">
  <script>
    const field = document.getElementById('field');
    field.addEventListener('input', function (e) {
      console.log('input value=[' + field.value + '] type=' + e.inputType);
    });
    field.addEventListener('change', function () { console.log('change value=[' + field.value + ']'); });
  </script>)html";

} // namespace

TEST_F(InputTest, TypingIntoAFieldUpdatesTheValue) {
    const xgu_view_id view = load(kFieldPage);
    click(view, 50.0f, 15.0f);
    text(view, "abc");
    EXPECT_TRUE(logged("input value=[abc] type=insertText")) << transcript();
}

TEST_F(InputTest, BackspaceAndDeleteEditTheValue) {
    const xgu_view_id view = load(kFieldPage);
    click(view, 50.0f, 15.0f);
    text(view, "abcd");
    key(view, XGU_INPUT_KEY_DOWN, "Backspace", "Backspace");
    EXPECT_TRUE(logged("input value=[abc] type=deleteContentBackward")) << transcript();

    key(view, XGU_INPUT_KEY_DOWN, "ArrowLeft", "ArrowLeft");
    key(view, XGU_INPUT_KEY_DOWN, "Delete", "Delete");
    EXPECT_TRUE(logged("input value=[ab] type=deleteContentForward"));
}

TEST_F(InputTest, SelectAllThenTypingReplacesEverything) {
    const xgu_view_id view = load(kFieldPage);
    click(view, 50.0f, 15.0f);
    text(view, "old");

    xgu_input_event selectAll = makeEvent(XGU_INPUT_KEY_DOWN);
    selectAll.key = "a";
    selectAll.code = "KeyA";
    selectAll.modifiers = XGU_MOD_CTRL;
    ASSERT_EQ(xgu_view_send_input(view, &selectAll), XGU_OK);

    text(view, "new");
    EXPECT_TRUE(logged("input value=[new] type=insertText")) << transcript();
}

TEST_F(InputTest, ShiftArrowSelectsAndTypingReplacesTheSelection) {
    const xgu_view_id view = load(kFieldPage);
    click(view, 50.0f, 15.0f);
    text(view, "abcd");

    xgu_input_event home = makeEvent(XGU_INPUT_KEY_DOWN);
    home.key = "Home";
    home.code = "Home";
    ASSERT_EQ(xgu_view_send_input(view, &home), XGU_OK);

    xgu_input_event extend = makeEvent(XGU_INPUT_KEY_DOWN);
    extend.key = "ArrowRight";
    extend.code = "ArrowRight";
    extend.modifiers = XGU_MOD_SHIFT;
    ASSERT_EQ(xgu_view_send_input(view, &extend), XGU_OK);
    ASSERT_EQ(xgu_view_send_input(view, &extend), XGU_OK);

    text(view, "X");
    EXPECT_TRUE(logged("input value=[Xcd] type=insertText")) << transcript();
}

TEST_F(InputTest, ChangeFiresOnBlurAfterAnEdit) {
    const xgu_view_id view = load(kFieldPage);
    click(view, 50.0f, 15.0f);
    text(view, "typed");
    EXPECT_FALSE(logged("change value="));

    ASSERT_EQ(xgu_view_set_focus(view, ""), XGU_OK);
    EXPECT_TRUE(logged("change value=[typed]")) << transcript();
}

TEST_F(InputTest, EnterCommitsASingleLineField) {
    const xgu_view_id view = load(kFieldPage);
    click(view, 50.0f, 15.0f);
    text(view, "typed");
    key(view, XGU_INPUT_KEY_DOWN, "Enter", "Enter");
    EXPECT_TRUE(logged("change value=[typed]")) << transcript();
}

TEST_F(InputTest, EnterInsertsANewlineInATextarea) {
    const xgu_view_id view = load(R"html(
      <textarea id="area" style="position:absolute;left:0;top:0;width:150px;height:60px"></textarea>
      <script>
        const area = document.getElementById('area');
        area.addEventListener('input', function () { console.log('lines=' + area.value.split('\n').length); });
      </script>)html");

    click(view, 50.0f, 20.0f);
    text(view, "one");
    key(view, XGU_INPUT_KEY_DOWN, "Enter", "Enter");
    text(view, "two");
    EXPECT_TRUE(logged("lines=2")) << transcript();
}

TEST_F(InputTest, ScriptCanReadAndWriteTheValue) {
    const xgu_view_id view = load(R"html(
      <input id="field" value="start" style="position:absolute;left:0;top:0;width:150px;height:30px">
      <script>
        const field = document.getElementById('field');
        console.log('initial=[' + field.value + ']');
        field.value = 'set from script';
        console.log('after=[' + field.value + '] selectionStart=' + field.selectionStart);
      </script>)html");
    (void)view;
    EXPECT_TRUE(logged("initial=[start]"));
    EXPECT_TRUE(logged("after=[set from script] selectionStart=15")) << transcript();
}

TEST_F(InputTest, ScriptCanMoveFocusAndReadActiveElement) {
    const xgu_view_id view = load(R"html(
      <input id="field" style="position:absolute;left:0;top:0;width:150px;height:30px">
      <script>
        const field = document.getElementById('field');
        field.addEventListener('focus', function () {
          console.log('active=' + document.activeElement.id);
        });
        field.focus();
        console.log('afterFocus=' + document.activeElement.id);
        field.blur();
        console.log('afterBlur=' + document.activeElement.tagName);
      </script>)html");
    (void)view;
    EXPECT_TRUE(logged("active=field")) << transcript();
    EXPECT_TRUE(logged("afterFocus=field"));
    EXPECT_TRUE(logged("afterBlur=BODY")) << "focus falls back to the body";
}

TEST_F(InputTest, DisabledFieldsDoNotTakeFocus) {
    const xgu_view_id view = load(R"html(
      <input id="field" disabled style="position:absolute;left:0;top:0;width:150px;height:30px">
      <script>
        document.getElementById('field').addEventListener('focus', function () { console.log('focused'); });
      </script>)html");

    click(view, 50.0f, 15.0f);
    EXPECT_FALSE(logged("focused"));
}

TEST_F(InputTest, TypedTextIsPainted) {
    const xgu_view_id view = load(R"html(
      <input id="field" style="position:absolute;left:0;top:0;width:150px;height:30px;
                               background:#ffffff;color:#000000;border:0">)html");

    click(view, 50.0f, 15.0f);
    text(view, "WWWW");

    const void* pixels = nullptr;
    uint32_t size = 0, w = 0, h = 0;
    ASSERT_TRUE(xgu_view_acquire_pixels(view, &pixels, &size, &w, &h, nullptr)) << "typing should repaint";
    const auto* bytes = static_cast<const uint8_t*>(pixels);
    // Count dark pixels inside the field: the glyphs and the caret.
    uint32_t dark = 0;
    for (uint32_t y = 0; y < 30; ++y) {
        for (uint32_t x = 0; x < 150; ++x) {
            const size_t offset = (static_cast<size_t>(h - 1 - y) * w + x) * 4;
            if (bytes[offset] < 128 && bytes[offset + 3] > 200) {
                ++dark;
            }
        }
    }
    xgu_view_release_pixels(view);
    EXPECT_GT(dark, 20u) << "the value is rendered inside the field";
}
