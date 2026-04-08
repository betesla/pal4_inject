#include "bootstrap.h"

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "camera_unlock.h"
#include "crash_handler.h"
#include "hook_manager.h"
#include "inject_control_window.h"
#include "pal4inject/dpi_awareness.h"
#include "ipc_server.h"
#include "pal4inject/launcher.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

void AppendBootstrapLog(const std::string_view line) {
    char temp_path[MAX_PATH];
    const DWORD temp_len = GetTempPathA(MAX_PATH, temp_path);
    if (temp_len == 0 || temp_len >= MAX_PATH) {
        return;
    }

    std::string log_path = std::string(temp_path, temp_len);
    if (!log_path.empty() && log_path.back() != '\\') {
        log_path.push_back('\\');
    }
    log_path += "pal4_inject_runtime.log";

    HANDLE file = CreateFileA(
        log_path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    const std::string payload = std::string(line) + "\r\n";
    DWORD written = 0;
    WriteFile(file, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
    CloseHandle(file);
}

void SignalReadyEvent() {
    const std::string event_name = GetRuntimeState().ReadyEventName();
    if (event_name.empty()) {
        AppendBootstrapLog("signal_ready_event skipped: empty event name");
        return;
    }

    HANDLE event_handle = CreateEventA(nullptr, TRUE, FALSE, event_name.c_str());
    if (!event_handle) {
        AppendBootstrapLog("signal_ready_event failed");
        return;
    }
    SetEvent(event_handle);
    CloseHandle(event_handle);
    AppendBootstrapLog("signal_ready_event ok");
}

}  // namespace

DWORD WINAPI RuntimeBootstrapThread(LPVOID) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    AppendBootstrapLog("bootstrap_thread_enter");

    auto& state = GetRuntimeState();
    state.SetMainModuleBase(reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr)));
    state.ConfigureNames(
        BuildReadyEventName(GetCurrentProcessId()),
        BuildPipeName(GetCurrentProcessId()));
    AppendBootstrapLog("bootstrap_names_configured");

    std::string error;
    DpiAwarenessMode dpi_mode = DpiAwarenessMode::unknown;
    const bool dpi_ok = ApplyProcessDpiAwareness(&dpi_mode, &error);
    if (!dpi_ok) {
        state.SetLastError(error);
        AppendBootstrapLog(std::string("apply_process_dpi_awareness failed: ") + error);
    } else {
        AppendBootstrapLog(std::string("apply_process_dpi_awareness ok mode=") + ToString(dpi_mode));
    }

    const bool crash_capture_ok = InstallCrashCapture(&error);
    if (!crash_capture_ok) {
        state.SetLastError(error);
        AppendBootstrapLog(std::string("install_crash_capture failed: ") + error);
    } else {
        AppendBootstrapLog("install_crash_capture ok");
    }

    const bool init_ok = GetHookManager().Initialize(&error);
    if (!init_ok) {
        state.SetLastError(error);
        AppendBootstrapLog(std::string("hook_manager_initialize failed: ") + error);
    } else {
        AppendBootstrapLog("hook_manager_initialize ok");
    }

    const bool pipe_ok = StartIpcServer(&error);
    if (!pipe_ok) {
        state.SetLastError(error);
        AppendBootstrapLog(std::string("start_ipc_server failed: ") + error);
    } else {
        AppendBootstrapLog("start_ipc_server ok");
    }

    const bool control_window_ok = StartInjectControlWindow(&error);
    if (!control_window_ok) {
        state.SetLastError(error);
        AppendBootstrapLog(std::string("start_inject_control_window failed: ") + error);
    } else {
        AppendBootstrapLog("start_inject_control_window ok");
    }

    bool hooks_ok = false;
    if (init_ok) {
        hooks_ok = GetHookManager().InstallBootstrapHooks(&error);
        if (!hooks_ok) {
            state.SetLastError(error);
            AppendBootstrapLog(std::string("install_bootstrap_hooks failed: ") + error);
        } else {
            AppendBootstrapLog("install_bootstrap_hooks ok");
        }
    }

    bool camera_patch_ok = false;
    if (init_ok && hooks_ok) {
        camera_patch_ok = ApplyCameraPitchUnlockPatch(&error);
        if (!camera_patch_ok) {
            state.SetLastError(error);
            AppendBootstrapLog(std::string("camera_pitch_unlock_patch failed: ") + error);
        } else {
            AppendBootstrapLog("camera_pitch_unlock_patch ok");
        }
    }

    state.SetHooksReady(hooks_ok);
    state.SetBootstrapReady(
        crash_capture_ok &&
        init_ok &&
        pipe_ok &&
        control_window_ok &&
        hooks_ok &&
        camera_patch_ok);
    AppendBootstrapLog(state.BootstrapReady() ? "bootstrap_ready=1" : "bootstrap_ready=0");
    SignalReadyEvent();
    return 0;
}

}  // namespace pal4::inject
