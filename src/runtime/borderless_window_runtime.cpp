#include "borderless_window_runtime.h"

#include <cstdint>
#include <cstring>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "pal4inject/borderless_window.h"

namespace pal4::inject {
namespace {

struct WindowCandidate {
    HWND window = nullptr;
    long long client_area = -1;
};

struct MonitorCandidate {
    std::string requested_device;
    HMONITOR monitor = nullptr;
};

BOOL CALLBACK FindLargestProcessWindow(const HWND window, const LPARAM parameter) {
    auto* candidate = reinterpret_cast<WindowCandidate*>(parameter);
    if (!candidate || !window || GetAncestor(window, GA_ROOT) != window) {
        return TRUE;
    }
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id != GetCurrentProcessId()) {
        return TRUE;
    }
    RECT client{};
    if (!GetClientRect(window, &client)) {
        return TRUE;
    }
    const long long width = client.right - client.left;
    const long long height = client.bottom - client.top;
    const long long area = width > 0 && height > 0 ? width * height : 0;
    if (area > candidate->client_area) {
        candidate->window = window;
        candidate->client_area = area;
    }
    return TRUE;
}

BOOL CALLBACK FindRequestedMonitor(
    const HMONITOR monitor,
    HDC,
    LPRECT,
    const LPARAM parameter) {
    auto* candidate = reinterpret_cast<MonitorCandidate*>(parameter);
    if (!candidate) {
        return FALSE;
    }
    MONITORINFOEXA info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoA(monitor, &info) &&
        _stricmp(info.szDevice, candidate->requested_device.c_str()) == 0) {
        candidate->monitor = monitor;
        return FALSE;
    }
    return TRUE;
}

bool SetWindowStyleChecked(
    const HWND window,
    const int index,
    const LONG_PTR value,
    const char* const label,
    std::string* error) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrA(window, index, value);
    const DWORD result_error = GetLastError();
    if (previous == 0 && result_error != ERROR_SUCCESS) {
        if (error) {
            *error = std::string(label) + " failed with error " +
                std::to_string(result_error);
        }
        return false;
    }
    return true;
}

}  // namespace

bool ApplyBorderlessWindowToCurrentProcess(
    const std::string_view requested_monitor,
    std::string* const summary) {
    WindowCandidate candidate{};
    EnumWindows(&FindLargestProcessWindow, reinterpret_cast<LPARAM>(&candidate));
    if (!candidate.window) {
        if (summary) {
            *summary = "game window not found";
        }
        return false;
    }

    MonitorCandidate monitor_candidate{};
    monitor_candidate.requested_device = requested_monitor;
    if (!monitor_candidate.requested_device.empty()) {
        EnumDisplayMonitors(
            nullptr,
            nullptr,
            &FindRequestedMonitor,
            reinterpret_cast<LPARAM>(&monitor_candidate));
    }
    const bool used_fallback = monitor_candidate.monitor == nullptr;
    const HMONITOR monitor = monitor_candidate.monitor
        ? monitor_candidate.monitor
        : MonitorFromWindow(candidate.window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXA monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (!monitor || !GetMonitorInfoA(monitor, &monitor_info)) {
        if (summary) {
            *summary = "GetMonitorInfoA failed with error " +
                std::to_string(GetLastError());
        }
        return false;
    }

    const auto plan = BuildBorderlessWindowPlan(
        static_cast<std::uint32_t>(GetWindowLongPtrA(candidate.window, GWL_STYLE)),
        static_cast<std::uint32_t>(GetWindowLongPtrA(candidate.window, GWL_EXSTYLE)),
        monitor_info.rcMonitor.left,
        monitor_info.rcMonitor.top,
        monitor_info.rcMonitor.right,
        monitor_info.rcMonitor.bottom);
    if (plan.width <= 0 || plan.height <= 0) {
        if (summary) {
            *summary = "monitor rectangle is empty";
        }
        return false;
    }

    std::string error;
    if (!SetWindowStyleChecked(
            candidate.window,
            GWL_STYLE,
            static_cast<LONG_PTR>(plan.style),
            "SetWindowLongPtrA(GWL_STYLE)",
            &error) ||
        !SetWindowStyleChecked(
            candidate.window,
            GWL_EXSTYLE,
            static_cast<LONG_PTR>(plan.extended_style),
            "SetWindowLongPtrA(GWL_EXSTYLE)",
            &error)) {
        if (summary) {
            *summary = error;
        }
        return false;
    }

    if (!SetWindowPos(
            candidate.window,
            HWND_TOP,
            plan.x,
            plan.y,
            plan.width,
            plan.height,
            SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        if (summary) {
            *summary = "SetWindowPos failed with error " +
                std::to_string(GetLastError());
        }
        return false;
    }

    RECT applied{};
    GetWindowRect(candidate.window, &applied);
    std::ostringstream out;
    out << "hwnd=0x" << std::hex << std::uppercase
        << reinterpret_cast<std::uintptr_t>(candidate.window)
        << std::dec
        << " requested_monitor="
        << (requested_monitor.empty() ? "auto" : std::string(requested_monitor))
        << " resolved_monitor=" << monitor_info.szDevice
        << " fallback=" << (used_fallback ? 1 : 0)
        << " monitor=" << plan.x << ',' << plan.y
        << ' ' << plan.width << 'x' << plan.height
        << " applied=" << applied.left << ',' << applied.top
        << ' ' << (applied.right - applied.left) << 'x'
        << (applied.bottom - applied.top);
    if (summary) {
        *summary = out.str();
    }
    return true;
}

}  // namespace pal4::inject
