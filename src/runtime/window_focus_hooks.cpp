#include "window_focus_hooks.h"

#include <sstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "inject_control_window.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using Pal4MainWndProcFn = LRESULT (__stdcall*)(HWND, UINT, WPARAM, LPARAM);

Pal4MainWndProcFn g_original_pal4_main_wndproc = nullptr;

LRESULT __stdcall Hook_Pal4_Main_WndProc(
    const HWND hwnd,
    const UINT msg,
    const WPARAM wparam,
    const LPARAM lparam) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::pal4_main_wndproc);

    if (!g_original_pal4_main_wndproc) {
        state.SetHookError(
            HookId::pal4_main_wndproc,
            "original PAL4_Main_WndProc trampoline is null");
        state.SetLastError("original PAL4_Main_WndProc trampoline is null");
        return 0;
    }

    const HookMode mode = state.GetHookMode(HookId::pal4_main_wndproc);
    if (mode == HookMode::observe_only || mode == HookMode::mirror_compare) {
        return g_original_pal4_main_wndproc(hwnd, msg, wparam, lparam);
    }

    if (msg == WM_ACTIVATE &&
        LOWORD(wparam) == WA_INACTIVE &&
        IsInjectControlWindowRelated(reinterpret_cast<HWND>(lparam))) {
        std::ostringstream out;
        out
            << "hook=pal4_main_wndproc"
            << " msg=WM_ACTIVATE"
            << " path=ignore_minimize_for_inject_control"
            << " next_hwnd=0x" << std::hex << std::uppercase
            << reinterpret_cast<std::uintptr_t>(reinterpret_cast<HWND>(lparam));
        state.AppendEventLog(out.str());
        state.SetLastUiEvent("PAL4_Main_WndProc:WM_ACTIVATE:inject_control");
        return 0;
    }

    return g_original_pal4_main_wndproc(hwnd, msg, wparam, lparam);
}

}  // namespace

void* GetWindowFocusReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::pal4_main_wndproc:
        return reinterpret_cast<void*>(&Hook_Pal4_Main_WndProc);
    default:
        return nullptr;
    }
}

void SetWindowFocusOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::pal4_main_wndproc:
        g_original_pal4_main_wndproc =
            reinterpret_cast<Pal4MainWndProcFn>(trampoline);
        break;
    default:
        break;
    }
}

}  // namespace pal4::inject
