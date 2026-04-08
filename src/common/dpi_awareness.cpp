#include "pal4inject/dpi_awareness.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pal4::inject {
namespace {

using SetProcessDpiAwarenessContextFn = BOOL (WINAPI*)(HANDLE);
using SetProcessDpiAwarenessFn = HRESULT (WINAPI*)(int);
using SetProcessDPIAwareFn = BOOL (WINAPI*)();

constexpr int kProcessPerMonitorDpiAware = 2;

std::string FormatWindowsError(const DWORD code) {
    char* buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);
    std::string text;
    if (size && buffer) {
        text.assign(buffer, size);
        LocalFree(buffer);
    } else {
        text = "Windows error " + std::to_string(code);
    }
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

HMODULE GetModuleHandleOrLoad(const char* module_name) {
    HMODULE module = GetModuleHandleA(module_name);
    if (!module) {
        module = LoadLibraryA(module_name);
    }
    return module;
}

}  // namespace

const char* ToString(const DpiAwarenessMode mode) noexcept {
    switch (mode) {
    case DpiAwarenessMode::unknown:
        return "unknown";
    case DpiAwarenessMode::per_monitor_aware_v2:
        return "per_monitor_aware_v2";
    case DpiAwarenessMode::per_monitor_aware:
        return "per_monitor_aware";
    case DpiAwarenessMode::system_aware:
        return "system_aware";
    case DpiAwarenessMode::already_set:
        return "already_set";
    }
    return "unknown";
}

bool ApplyProcessDpiAwareness(DpiAwarenessMode* out_mode, std::string* error) {
    if (out_mode) {
        *out_mode = DpiAwarenessMode::unknown;
    }
    if (error) {
        error->clear();
    }

    if (HMODULE user32 = GetModuleHandleOrLoad("user32.dll")) {
        const auto set_process_dpi_awareness_context =
            reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_process_dpi_awareness_context) {
            const HANDLE per_monitor_aware_v2_context =
                reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4));
            SetLastError(ERROR_SUCCESS);
            if (set_process_dpi_awareness_context(per_monitor_aware_v2_context)) {
                if (out_mode) {
                    *out_mode = DpiAwarenessMode::per_monitor_aware_v2;
                }
                return true;
            }
            const DWORD last_error = GetLastError();
            if (last_error == ERROR_ACCESS_DENIED) {
                if (out_mode) {
                    *out_mode = DpiAwarenessMode::already_set;
                }
                return true;
            }
        }
    }

    if (HMODULE shcore = GetModuleHandleOrLoad("shcore.dll")) {
        const auto set_process_dpi_awareness =
            reinterpret_cast<SetProcessDpiAwarenessFn>(
                GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (set_process_dpi_awareness) {
            const HRESULT hr = set_process_dpi_awareness(kProcessPerMonitorDpiAware);
            if (SUCCEEDED(hr)) {
                if (out_mode) {
                    *out_mode = DpiAwarenessMode::per_monitor_aware;
                }
                return true;
            }
            if (hr == E_ACCESSDENIED) {
                if (out_mode) {
                    *out_mode = DpiAwarenessMode::already_set;
                }
                return true;
            }
        }
    }

    if (HMODULE user32 = GetModuleHandleOrLoad("user32.dll")) {
        const auto set_process_dpi_aware =
            reinterpret_cast<SetProcessDPIAwareFn>(
                GetProcAddress(user32, "SetProcessDPIAware"));
        if (set_process_dpi_aware) {
            SetLastError(ERROR_SUCCESS);
            if (set_process_dpi_aware()) {
                if (out_mode) {
                    *out_mode = DpiAwarenessMode::system_aware;
                }
                return true;
            }
            const DWORD last_error = GetLastError();
            if (last_error == ERROR_ACCESS_DENIED) {
                if (out_mode) {
                    *out_mode = DpiAwarenessMode::already_set;
                }
                return true;
            }
            if (error) {
                *error = "SetProcessDPIAware failed: " + FormatWindowsError(last_error);
            }
            return false;
        }
    }

    if (error) {
        *error = "no supported DPI awareness API is available";
    }
    return false;
}

}  // namespace pal4::inject
