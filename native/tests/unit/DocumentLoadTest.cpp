// Stage 3 end-to-end: loading a document through the C ABI, running its scripts
// and driving the DOM from JavaScript.

#include "core/Runtime.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex g_logMutex;
std::vector<std::pair<int, std::string>> g_logs;

void captureLog(void*, int level, const char* message) {
    std::lock_guard lock(g_logMutex);
    g_logs.emplace_back(level, message ? message : "");
}

class DocumentLoadTest : public ::testing::Test {
protected:
    void SetUp() override {
        {
            std::lock_guard lock(g_logMutex);
            g_logs.clear();
        }
        root_ = std::filesystem::temp_directory_path() / "xgu_doc_test";
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_ / "UI" / "Menu");

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

    xgu_view_id createView() {
        const std::string rootPath = root_.string();
        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = 16;
        desc.height = 16;
        desc.device_pixel_ratio = 1.0f;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.ui_root = rootPath.c_str();
        desc.name = "doc-test";
        return xgu_view_create(&desc);
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

    static bool errorContains(const std::string& needle) {
        std::lock_guard lock(g_logMutex);
        for (const auto& [level, text] : g_logs) {
            if (level == XGU_LOG_ERROR && text.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    std::filesystem::path root_;
};

} // namespace

TEST_F(DocumentLoadTest, LoadsHtmlAndRunsInlineScripts) {
    write("UI/Menu/index.html", R"(<!DOCTYPE html>
<html><head><title>Menu</title></head>
<body><div id="greeting">Hello</div>
<script>
  console.log('title=' + document.title);
  console.log('url=' + document.URL);
  console.log('greeting=' + document.getElementById('greeting').textContent);
  console.log('body tag=' + document.body.tagName);
</script>
</body></html>)");

    const xgu_view_id view = createView();
    ASSERT_NE(view, XGU_INVALID_VIEW);
    ASSERT_EQ(xgu_view_load(view, "UI/Menu/index.html"), XGU_OK);

    // Loading paints a frame, which promotes JsReady to Interactive.
    EXPECT_EQ(xgu_view_get_state(view), XGU_STATE_INTERACTIVE);
    EXPECT_TRUE(logContains("title=Menu"));
    EXPECT_TRUE(logContains("url=UI/Menu/index.html"));
    EXPECT_TRUE(logContains("greeting=Hello"));
    EXPECT_TRUE(logContains("body tag=BODY"));
}

TEST_F(DocumentLoadTest, RunsExternalScriptsInDocumentOrder) {
    write("UI/Menu/index.html",
          "<div id=\"x\">1</div><script src=\"./first.js\"></script><script>console.log('inline second');</script>"
          "<script src=\"sub/third.js\"></script>");
    write("UI/Menu/first.js", "console.log('external first');");
    write("UI/Menu/sub/third.js", "console.log('external third');");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/Menu/index.html"), XGU_OK);

    std::lock_guard lock(g_logMutex);
    int first = -1, second = -1, third = -1;
    for (int i = 0; i < static_cast<int>(g_logs.size()); ++i) {
        const std::string& text = g_logs[static_cast<size_t>(i)].second;
        if (text.find("external first") != std::string::npos) first = i;
        if (text.find("inline second") != std::string::npos) second = i;
        if (text.find("external third") != std::string::npos) third = i;
    }
    ASSERT_GE(first, 0);
    ASSERT_GE(second, 0);
    ASSERT_GE(third, 0);
    EXPECT_LT(first, second);
    EXPECT_LT(second, third);
}

TEST_F(DocumentLoadTest, ScriptsCannotEscapeTheUiRoot) {
    // One level above the UI root: must be refused however the path is spelled.
    {
        std::ofstream file(root_.parent_path() / "xgu_outside.js", std::ios::binary);
        file << "console.log('should not run');";
    }
    write("UI/Menu/index.html", "<script src=\"../../../xgu_outside.js\"></script>"
                                "<script src=\"/xgu_outside.js\"></script>");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/Menu/index.html"), XGU_OK);
    EXPECT_FALSE(logContains("should not run"));
    EXPECT_TRUE(errorContains("was rejected"));

    std::error_code ec;
    std::filesystem::remove(root_.parent_path() / "xgu_outside.js", ec);
}

TEST_F(DocumentLoadTest, ScriptsMayWalkUpInsideTheUiRoot) {
    // "../" that stays within the root is legitimate.
    write("shared.js", "console.log('shared ran');");
    write("UI/Menu/index.html", "<script src=\"../../shared.js\"></script>");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/Menu/index.html"), XGU_OK);
    EXPECT_TRUE(logContains("shared ran"));
}

TEST_F(DocumentLoadTest, MissingDocumentIsReported) {
    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/Menu/missing.html"), XGU_OK); // queued successfully
    EXPECT_TRUE(errorContains("cannot read"));
    EXPECT_NE(xgu_view_get_state(view), XGU_STATE_JS_READY);
}

TEST_F(DocumentLoadTest, DomMutationFromJavaScript) {
    write("UI/Menu/index.html", R"(<div id="root"><span class="item">a</span></div>
<script>
  const root = document.getElementById('root');
  const item = document.createElement('span');
  item.className = 'item second';
  item.textContent = 'b';
  root.appendChild(item);
  console.log('children=' + root.children.length);
  console.log('items=' + document.querySelectorAll('.item').length);
  console.log('html=' + root.innerHTML);
  console.log('text=' + root.textContent);
  item.remove();
  console.log('after remove=' + root.children.length);
</script>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/Menu/index.html"), XGU_OK);
    EXPECT_TRUE(logContains("children=2"));
    EXPECT_TRUE(logContains("items=2"));
    EXPECT_TRUE(logContains("html=<span class=\"item\">a</span><span class=\"item second\">b</span>"));
    EXPECT_TRUE(logContains("text=ab"));
    EXPECT_TRUE(logContains("after remove=1"));
}

TEST_F(DocumentLoadTest, QueryApisAndClassList) {
    write("UI/Menu/index.html", R"(<ul id="list"><li class="a">1</li><li class="a b">2</li></ul>
<script>
  const list = document.getElementById('list');
  console.log('q1=' + list.querySelector('li.b').textContent);
  console.log('q2=' + document.querySelectorAll('li').length);
  console.log('byClass=' + document.getElementsByClassName('a').length);
  console.log('byTag=' + document.getElementsByTagName('li').length);
  const first = list.querySelector('li');
  console.log('has-a=' + first.classList.contains('a'));
  first.classList.add('c');
  console.log('after-add=' + first.className);
  console.log('len=' + first.classList.length);
  console.log('toggled=' + first.classList.toggle('a'));
  console.log('final=' + first.className);
  console.log('matches=' + first.matches('li.c'));
</script>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/Menu/index.html"), XGU_OK);
    EXPECT_TRUE(logContains("q1=2"));
    EXPECT_TRUE(logContains("q2=2"));
    EXPECT_TRUE(logContains("byClass=2"));
    EXPECT_TRUE(logContains("byTag=2"));
    EXPECT_TRUE(logContains("has-a=true"));
    EXPECT_TRUE(logContains("after-add=a c"));
    EXPECT_TRUE(logContains("len=2"));
    EXPECT_TRUE(logContains("toggled=false"));
    EXPECT_TRUE(logContains("final=c"));
    EXPECT_TRUE(logContains("matches=true"));
}

TEST_F(DocumentLoadTest, WrapperIdentityIsStable) {
    write("UI/Menu/index.html", R"(<div id="a"></div>
<script>
  const first = document.getElementById('a');
  const second = document.getElementById('a');
  console.log('same=' + (first === second));
  first.expando = 42;
  console.log('expando=' + document.getElementById('a').expando);
  console.log('parent-is-body=' + (first.parentNode === document.body));
</script>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/Menu/index.html"), XGU_OK);
    EXPECT_TRUE(logContains("same=true"));
    EXPECT_TRUE(logContains("expando=42"));
    EXPECT_TRUE(logContains("parent-is-body=true"));
}

TEST_F(DocumentLoadTest, InvalidSelectorThrows) {
    write("UI/Menu/index.html", R"(<script>
  try { document.querySelector('::bogus'); } catch (e) { console.log('caught=' + e.message); }
</script>)");

    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/Menu/index.html"), XGU_OK);
    EXPECT_TRUE(logContains("caught="));
    EXPECT_TRUE(logContains("not a valid selector"));
}

TEST_F(DocumentLoadTest, LoadHtmlFromMemory) {
    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load_html(view, "<p id=\"p\">inline</p><script>console.log('mem=' + "
                                       "document.getElementById('p').textContent);</script>",
                                 "UI/Menu/virtual.html"),
              XGU_OK);
    EXPECT_TRUE(logContains("mem=inline"));
    // Loading paints a frame, which promotes JsReady to Interactive.
    EXPECT_EQ(xgu_view_get_state(view), XGU_STATE_INTERACTIVE);
}

TEST_F(DocumentLoadTest, ReloadStartsFromACleanIsolate) {
    write("UI/Menu/index.html", "<script>globalThis.counter = (globalThis.counter || 0) + 1; "
                                "console.log('counter=' + globalThis.counter);</script>");
    const xgu_view_id view = createView();
    ASSERT_EQ(xgu_view_load(view, "UI/Menu/index.html"), XGU_OK);
    ASSERT_EQ(xgu_view_reload(view), XGU_OK);

    std::lock_guard lock(g_logMutex);
    int seen = 0;
    for (const auto& [level, text] : g_logs) {
        if (text.find("counter=1") != std::string::npos) {
            ++seen;
        }
        EXPECT_EQ(text.find("counter=2"), std::string::npos) << "globals must not survive a reload";
    }
    EXPECT_EQ(seen, 2);
}

TEST_F(DocumentLoadTest, InvalidArgumentsAreRejected) {
    const xgu_view_id view = createView();
    EXPECT_EQ(xgu_view_load(view, nullptr), XGU_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(xgu_view_load(view, ""), XGU_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(xgu_view_load(0x9999000000000001ull, "UI/Menu/index.html"), XGU_ERR_INVALID_VIEW);
    EXPECT_EQ(xgu_view_load_html(view, nullptr, nullptr), XGU_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(xgu_view_reload(0x9999000000000001ull), XGU_ERR_INVALID_VIEW);
}
