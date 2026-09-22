#include "core/AssetLoader.h"

#include "core/Log.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace xgu {
namespace {

std::string toForwardSlashes(std::string_view path) {
    std::string out(path);
    for (char& c : out) {
        if (c == '\\') {
            c = '/';
        }
    }
    return out;
}

bool isAsciiAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// std::filesystem::path from a UTF-8 string (a plain std::string would be read
// in the native narrow encoding on Windows).
std::filesystem::path toPath(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

} // namespace

FileAssetLoader::FileAssetLoader(std::string root) : root_(toForwardSlashes(root)) {
    while (root_.size() > 1 && root_.back() == '/') {
        root_.pop_back();
    }
}

bool FileAssetLoader::isNonRelative(std::string_view reference) {
    if (reference.empty()) {
        return true;
    }
    const std::string path = toForwardSlashes(reference);
    if (path[0] == '/') {
        return true; // absolute or UNC ("//server/share")
    }
    if (path.size() >= 2 && isAsciiAlpha(path[0]) && path[1] == ':') {
        return true; // drive letter
    }
    // A "scheme:" prefix (http:, file:, data:, javascript: ...). A colon may not
    // appear in a plain relative path on Windows anyway.
    const size_t colon = path.find(':');
    if (colon != std::string::npos) {
        const size_t slash = path.find('/');
        if (slash == std::string::npos || colon < slash) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> FileAssetLoader::normalize(std::string_view path) {
    const std::string forward = toForwardSlashes(path);
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start <= forward.size()) {
        const size_t end = forward.find('/', start);
        const std::string_view part =
            std::string_view(forward).substr(start, (end == std::string::npos ? forward.size() : end) - start);
        if (part == "..") {
            if (parts.empty()) {
                return std::nullopt; // escapes the root
            }
            parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            result.push_back('/');
        }
        result.append(parts[i]);
    }
    return result;
}

std::optional<std::string> FileAssetLoader::resolve(std::string_view baseRelative, std::string_view reference) const {
    if (reference.empty()) {
        return std::nullopt;
    }
    if (isNonRelative(reference)) {
        XGU_LOG_WARNING("asset loader: rejected non-relative reference \"%.*s\"", static_cast<int>(reference.size()),
                        reference.data());
        return std::nullopt;
    }
    // Directory of the base document, if any.
    std::string base = toForwardSlashes(baseRelative);
    const size_t lastSlash = base.rfind('/');
    base = lastSlash == std::string::npos ? std::string() : base.substr(0, lastSlash);

    std::string combined = base.empty() ? std::string(reference) : base + "/" + std::string(reference);
    std::optional<std::string> normalized = normalize(combined);
    if (!normalized) {
        XGU_LOG_WARNING("asset loader: reference \"%.*s\" escapes the UI root", static_cast<int>(reference.size()),
                        reference.data());
        return std::nullopt;
    }
    if (normalized->empty()) {
        return std::nullopt;
    }
    return normalized;
}

std::string FileAssetLoader::absolutePath(std::string_view relativePath) const {
    std::string result = root_;
    if (!result.empty() && !relativePath.empty()) {
        result.push_back('/');
    }
    result.append(relativePath);
    return result;
}

bool FileAssetLoader::exists(std::string_view relativePath) const {
    std::optional<std::string> normalized = normalize(relativePath);
    if (!normalized || normalized->empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(toPath(absolutePath(*normalized)), ec);
}

std::optional<std::string> FileAssetLoader::read(std::string_view relativePath) const {
    std::optional<std::string> normalized = normalize(relativePath);
    if (!normalized || normalized->empty()) {
        return std::nullopt;
    }
    const std::string full = absolutePath(*normalized);
    std::ifstream file(toPath(full), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace xgu
