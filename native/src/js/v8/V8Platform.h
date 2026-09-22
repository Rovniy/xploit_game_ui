#pragma once

#include <memory>
#include <mutex>
#include <string>

namespace v8 {
class Platform;
}

namespace xgu::js {

// Process-wide V8 platform. Created on first use and intentionally never torn
// down: Unity keeps native plugins loaded across domain reloads and V8 cannot
// be re-initialised in the same process.
class V8Platform {
public:
    static V8Platform& instance();

    // Pre-loads the V8 DLLs from this module's directory, locates icudtl.dat and
    // initialises V8. `dataDirHint` is searched first for icudtl.dat. Returns false
    // (and logs why) when V8 cannot be used; safe to call repeatedly.
    bool ensureInitialized(const std::string& dataDirHint = {});

    bool initialized() const { return initialized_; }
    v8::Platform* platform() const { return platform_.get(); }
    const std::string& version() const { return version_; }

    // Directory containing the module this code lives in (xploit_game_ui.dll or the exe).
    static std::string moduleDirectory();
    // Directory of the running executable.
    static std::string executableDirectory();

private:
    V8Platform() = default;
    bool preloadDlls(const std::string& directory);

    std::mutex mutex_;
    std::unique_ptr<v8::Platform> platform_;
    std::string version_;
    bool initialized_ = false;
    bool failed_ = false;
};

} // namespace xgu::js
