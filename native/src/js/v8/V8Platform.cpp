#include "js/v8/V8Platform.h"

#include "core/Log.h"

#include <libplatform/libplatform.h>
#include <v8.h>

#include <windows.h>

#include <filesystem>
#include <vector>

namespace xgu::js {
namespace {

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                         nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), size);
    return out;
}

std::string directoryOfModule(HMODULE module) {
    wchar_t path[MAX_PATH * 2];
    const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    if (length == 0) {
        return {};
    }
    std::filesystem::path p(std::wstring(path, length));
    return toUtf8(p.parent_path().wstring());
}

// Load order matters only for readability; LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR makes
// the loader resolve each DLL's own imports from the same directory.
constexpr const wchar_t* kV8Dlls[] = {
    L"third_party_zlib.dll",
    L"third_party_abseil-cpp_absl.dll",
    L"icuuc.dll",
    L"third_party_icu_icui18n.dll",
    L"v8_libbase.dll",
    L"v8.dll",
    L"v8_libplatform.dll",
};

} // namespace

V8Platform& V8Platform::instance() {
    static V8Platform* platform = new V8Platform(); // intentionally leaked
    return *platform;
}

std::string V8Platform::moduleDirectory() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&V8Platform::moduleDirectory), &module);
    return directoryOfModule(module);
}

std::string V8Platform::executableDirectory() { return directoryOfModule(nullptr); }

bool V8Platform::preloadDlls(const std::string& directory) {
    const std::filesystem::path dir = toWide(directory);
    bool allLoaded = true;
    for (const wchar_t* name : kV8Dlls) {
        const std::filesystem::path full = dir / name;
        if (GetModuleHandleW(name) != nullptr) {
            continue; // already loaded (e.g. from the executable directory)
        }
        if (!std::filesystem::exists(full)) {
            continue; // let the delay-load helper try the default search path
        }
        HMODULE handle = LoadLibraryExW(full.c_str(), nullptr,
                                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!handle) {
            XGU_LOG_ERROR("V8: failed to load %s (error %lu)", toUtf8(full.wstring()).c_str(),
                          static_cast<unsigned long>(GetLastError()));
            allLoaded = false;
        }
    }
    return allLoaded;
}

bool V8Platform::ensureInitialized(const std::string& dataDirHint) {
    std::lock_guard lock(mutex_);
    if (initialized_) {
        return true;
    }
    if (failed_) {
        return false;
    }

    const std::string moduleDir = moduleDirectory();
    const std::string exeDir = executableDirectory();
    preloadDlls(moduleDir);
    if (GetModuleHandleW(L"v8.dll") == nullptr && exeDir != moduleDir) {
        preloadDlls(exeDir);
    }
    if (GetModuleHandleW(L"v8.dll") == nullptr) {
        XGU_LOG_ERROR("V8: v8.dll not found next to the module (%s) or the executable (%s); JavaScript disabled",
                      moduleDir.c_str(), exeDir.c_str());
        failed_ = true;
        return false;
    }

    // ICU data is external in the NuGet build (icudtl.dat).
    std::vector<std::string> candidates;
    if (!dataDirHint.empty()) {
        candidates.push_back(dataDirHint + "/icudtl.dat");
    }
    candidates.push_back(moduleDir + "/icudtl.dat");
    candidates.push_back(exeDir + "/icudtl.dat");
    std::string icuData;
    for (const std::string& candidate : candidates) {
        if (std::filesystem::exists(toWide(candidate))) {
            icuData = candidate;
            break;
        }
    }
    if (icuData.empty()) {
        XGU_LOG_ERROR("V8: icudtl.dat not found (looked next to the module and the executable); JavaScript disabled");
        failed_ = true;
        return false;
    }

    // Security defaults for a local game UI.
    v8::V8::SetFlagsFromString("--no-expose-wasm");

    if (!v8::V8::InitializeICU(icuData.c_str())) {
        XGU_LOG_ERROR("V8: InitializeICU(%s) failed; JavaScript disabled", icuData.c_str());
        failed_ = true;
        return false;
    }
    platform_ = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform_.get());
    if (!v8::V8::Initialize()) {
        XGU_LOG_ERROR("V8: V8::Initialize failed; JavaScript disabled");
        failed_ = true;
        return false;
    }
    version_ = v8::V8::GetVersion();
    initialized_ = true;
    XGU_LOG_INFO("V8 %s initialized (ICU data: %s)", version_.c_str(), icuData.c_str());
    return true;
}

} // namespace xgu::js
