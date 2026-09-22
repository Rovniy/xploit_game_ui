#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace xgu {

// Resolves and reads the files a document may touch (HTML, CSS, scripts,
// images, fonts). Every path is confined to one UI root directory: JavaScript
// never gets arbitrary filesystem access (see docs/architecture.md).
class IAssetLoader {
public:
    virtual ~IAssetLoader() = default;

    // Resolves `reference` (e.g. "./images/x.png") against `baseRelative` (the
    // document's path relative to the root, e.g. "UI/Menu/index.html").
    // Returns the resolved root-relative path with forward slashes, or nullopt
    // when the reference escapes the root or is not a plain relative path.
    virtual std::optional<std::string> resolve(std::string_view baseRelative, std::string_view reference) const = 0;

    // Reads a root-relative path. Returns nullopt when missing or unreadable.
    virtual std::optional<std::string> read(std::string_view relativePath) const = 0;

    virtual bool exists(std::string_view relativePath) const = 0;
    virtual const std::string& root() const = 0;
};

// Reads from a directory on disk. Rejects absolute paths, drive letters, UNC
// paths, URL schemes and any ".." that would leave the root.
class FileAssetLoader final : public IAssetLoader {
public:
    explicit FileAssetLoader(std::string root);

    std::optional<std::string> resolve(std::string_view baseRelative, std::string_view reference) const override;
    std::optional<std::string> read(std::string_view relativePath) const override;
    bool exists(std::string_view relativePath) const override;
    const std::string& root() const override { return root_; }

    // Root-relative path -> absolute path, without checking existence.
    std::string absolutePath(std::string_view relativePath) const;

    // Normalises "a/./b/../c" to "a/c" and rejects escapes. Exposed for tests.
    static std::optional<std::string> normalize(std::string_view path);
    // True for absolute paths, drive letters, UNC paths and "scheme:" references.
    static bool isNonRelative(std::string_view reference);

private:
    std::string root_;
};

} // namespace xgu
