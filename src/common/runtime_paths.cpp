#include "pal4inject/runtime_paths.h"

#include <algorithm>
#include <cctype>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {
namespace {

std::string LowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

HMODULE CurrentModuleHandle() {
    HMODULE module = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&CurrentModuleHandle),
        &module);
    return module;
}

}  // namespace

std::filesystem::path InjectModuleDirectory() {
    char buffer[MAX_PATH]{};
    const DWORD len = GetModuleFileNameA(CurrentModuleHandle(), buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(std::string(buffer, len)).parent_path();
}

std::filesystem::path InjectDataDirectory() {
    const auto module_dir = InjectModuleDirectory();
    char buffer[MAX_PATH]{};
    const DWORD len = GetModuleFileNameA(CurrentModuleHandle(), buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return module_dir;
    }

    const auto filename = LowerAscii(std::filesystem::path(std::string(buffer, len)).filename().string());
    if (filename == "pal4_inject.exe") {
        return module_dir / "pal4_inject";
    }
    return module_dir;
}

std::filesystem::path RuntimeLogPath() {
    return InjectDataDirectory() / "pal4_inject_runtime.log";
}

std::filesystem::path CrashArtifactDirectory() {
    return InjectDataDirectory();
}

}  // namespace pal4::inject
