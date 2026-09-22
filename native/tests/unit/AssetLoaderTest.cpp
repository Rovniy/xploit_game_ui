#include "core/AssetLoader.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace xgu;

namespace {

class AssetLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() / "xgu_assets_test";
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_ / "UI" / "Menu" / "images");
        write("UI/Menu/index.html", "<div>hi</div>");
        write("UI/Menu/app.js", "console.log('x')");
        write("UI/Menu/images/icon.txt", "icon");
        write("secret.txt", "top secret");
        loader_ = std::make_unique<FileAssetLoader>(root_.string());
    }

    void TearDown() override {
        loader_.reset();
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    void write(const std::string& relative, const std::string& contents) {
        const std::filesystem::path path = root_ / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << contents;
    }

    std::filesystem::path root_;
    std::unique_ptr<FileAssetLoader> loader_;
};

} // namespace

TEST_F(AssetLoaderTest, ResolvesRelativeReferences) {
    EXPECT_EQ(loader_->resolve("UI/Menu/index.html", "./app.js"), "UI/Menu/app.js");
    EXPECT_EQ(loader_->resolve("UI/Menu/index.html", "app.js"), "UI/Menu/app.js");
    EXPECT_EQ(loader_->resolve("UI/Menu/index.html", "images/icon.txt"), "UI/Menu/images/icon.txt");
    EXPECT_EQ(loader_->resolve("UI/Menu/index.html", "../Menu/app.js"), "UI/Menu/app.js");
    EXPECT_EQ(loader_->resolve("", "UI/Menu/app.js"), "UI/Menu/app.js");
}

TEST_F(AssetLoaderTest, RejectsEscapesAndAbsolutePaths) {
    EXPECT_FALSE(loader_->resolve("UI/Menu/index.html", "../../../secret.txt").has_value());
    EXPECT_FALSE(loader_->resolve("UI/Menu/index.html", "/etc/passwd").has_value());
    EXPECT_FALSE(loader_->resolve("UI/Menu/index.html", "C:/Windows/system.ini").has_value());
    EXPECT_FALSE(loader_->resolve("UI/Menu/index.html", "\\\\server\\share\\file").has_value());
    EXPECT_FALSE(loader_->resolve("UI/Menu/index.html", "file:///etc/passwd").has_value());
    EXPECT_FALSE(loader_->resolve("UI/Menu/index.html", "https://example.com/x.js").has_value());
    EXPECT_FALSE(loader_->resolve("UI/Menu/index.html", "javascript:alert(1)").has_value());
    EXPECT_FALSE(loader_->resolve("UI/Menu/index.html", "").has_value());
}

TEST_F(AssetLoaderTest, NormalizeHandlesDotSegments) {
    EXPECT_EQ(FileAssetLoader::normalize("a/./b/../c"), "a/c");
    EXPECT_EQ(FileAssetLoader::normalize("a//b"), "a/b");
    EXPECT_EQ(FileAssetLoader::normalize("a\\b"), "a/b");
    EXPECT_FALSE(FileAssetLoader::normalize("../a").has_value());
    EXPECT_FALSE(FileAssetLoader::normalize("a/../../b").has_value());
}

TEST_F(AssetLoaderTest, ReadsFilesInsideTheRoot) {
    const auto contents = loader_->read("UI/Menu/index.html");
    ASSERT_TRUE(contents.has_value());
    EXPECT_EQ(*contents, "<div>hi</div>");
    EXPECT_TRUE(loader_->exists("UI/Menu/app.js"));
    EXPECT_FALSE(loader_->exists("UI/Menu/missing.js"));
    EXPECT_FALSE(loader_->read("UI/Menu/missing.js").has_value());
}

TEST_F(AssetLoaderTest, ReadRejectsEscapingPaths) {
    EXPECT_FALSE(loader_->read("../secret.txt").has_value());
    EXPECT_FALSE(loader_->exists("../secret.txt"));
    // The file exists, but only through a path inside the root.
    EXPECT_TRUE(loader_->exists("secret.txt"));
}
