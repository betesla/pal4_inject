#include "input_hooks.h"

#include <cstdint>
#include <sstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "battle_ui_layout_hooks.h"
#include "camera_hooks.h"
#include "cegui_bindings.h"
#include "cegui_font_hooks.h"
#include "cegui_renderer_hooks.h"
#include "d3d9_quality_hooks.h"
#include "gamepad_runtime.h"
#include "hud_layout_fixups.h"
#include "hook_logging.h"
#include "minimap_hooks.h"
#include "pal4inject/cegui_widescreen.h"
#include "pal4inject/ida_addresses.h"
#include "pal4inject/input_logic.h"
#include "window_focus_hooks.h"
#include "runtime_state.h"

namespace pal4::inject {
namespace {

using ProcessUiEventFn = bool (__thiscall*)(void*, HWND, UINT, WPARAM, LPARAM);
using HandleUiMessageAndProcessFn = char (__thiscall*)(void*, HWND, UINT, WPARAM, LPARAM);
using SimulateKeyPressAndReleaseFn = bool (__thiscall*)(void*, WPARAM);
using ProcessInputsFn = int (__cdecl*)();
using GiTalkFn = char (__cdecl*)(void*, void*);
using MapVirtualKeyToUiKeyFn = int (__thiscall*)(void*, unsigned int);
using EnableMouseCaptureFn = void (__thiscall*)(unsigned char*);
using DisableMouseCaptureFn = void (__thiscall*)(unsigned char*);
using TransformMouseCoordinatesFn = float* (__thiscall*)(float*, float*, float*);
using UiFrameManagerGetInstanceFn = void* (__cdecl*)();
using PalGameIvGetInstanceFn = void* (__cdecl*)();

using RendererQueryDisplaySizeFn = void (__thiscall*)(void*, void*);
using RendererFireEventFn = void (__thiscall*)(void*, const void*, void*, const void*);

struct EventArgsMirror {
    const void* vftable = nullptr;
    unsigned char handled = 0;
    unsigned char padding[3]{};
};

enum class ProcessUiDispatchPath : std::uint8_t {
    replacement = 0,
    fallback_original,
    rejected,
};

struct ProcessUiDispatchResult {
    bool handled = false;
    ProcessUiDispatchPath path = ProcessUiDispatchPath::replacement;
    std::string error;
};

ProcessUiEventFn g_original_process_ui_event = nullptr;
HandleUiMessageAndProcessFn g_original_handle_ui_message = nullptr;
SimulateKeyPressAndReleaseFn g_original_simulate_key_press_and_release = nullptr;
ProcessInputsFn g_original_process_inputs = nullptr;
GiTalkFn g_original_gi_talk = nullptr;

constexpr unsigned char kInjectedTalkTextGbk[] = {
    0xD2, 0xD1, 0xD7, 0xA2, 0xC8, 0xEB, 0x00,
};

struct GiTalkStringArg {
    std::uint32_t reserved = 0;
    const char* text = nullptr;
};

GiTalkStringArg g_injected_gi_talk_arg = {
    0,
    reinterpret_cast<const char*>(kInjectedTalkTextGbk),
};

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

BOOL CALLBACK FindCurrentProcessWindowProc(HWND hwnd, LPARAM lparam) {
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id != GetCurrentProcessId()) {
        return TRUE;
    }

    auto* out = reinterpret_cast<HWND*>(lparam);
    if (out) {
        *out = hwnd;
    }
    return FALSE;
}

HWND FindCurrentProcessWindow() {
    HWND hwnd = nullptr;
    EnumWindows(&FindCurrentProcessWindowProc, reinterpret_cast<LPARAM>(&hwnd));
    return hwnd;
}

std::string FormatPointer(const void* value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << reinterpret_cast<std::uintptr_t>(value);
    return out.str();
}

std::uintptr_t MainModuleBase() {
    auto& state = GetRuntimeState();
    std::uintptr_t base = state.MainModuleBase();
    if (base == 0) {
        base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        state.SetMainModuleBase(base);
    }
    return base;
}

template <typename Fn>
Fn ResolveRuntimeFunction(const std::uint32_t ida_ea) {
    const auto base = MainModuleBase();
    if (base == 0) {
        return nullptr;
    }
    return reinterpret_cast<Fn>(ida::ResolveRuntimeAddress(base, ida_ea));
}

void* ResolveRuntimeData(const std::uint32_t ida_ea) {
    const auto base = MainModuleBase();
    if (base == 0) {
        return nullptr;
    }
    return reinterpret_cast<void*>(ida::ResolveRuntimeAddress(base, ida_ea));
}

int* ReadGameConfigPointer() {
    auto* config_ptr_address =
        static_cast<int**>(ResolveRuntimeData(ida::kGameConfigGlobal));
    return config_ptr_address ? *config_ptr_address : nullptr;
}

void LogHookEvent(const HookId id, const std::string_view text) {
    AppendHookEventLog(id, text);
}

ProcessUiDispatchResult CallOriginalProcessUiEvent(
    void* self,
    const HWND hwnd,
    const UINT msg,
    const WPARAM wparam,
    const LPARAM lparam) {
    ProcessUiDispatchResult result{};
    result.path = ProcessUiDispatchPath::fallback_original;
    result.handled = g_original_process_ui_event
        ? g_original_process_ui_event(self, hwnd, msg, wparam, lparam)
        : false;
    if (!g_original_process_ui_event) {
        result.error = "original ProcessUIEvent trampoline is null";
        result.path = ProcessUiDispatchPath::rejected;
    }
    return result;
}

ProcessUiDispatchResult FallbackOrRejectProcessUiEvent(
    const HookMode mode,
    void* self,
    const HWND hwnd,
    const UINT msg,
    const WPARAM wparam,
    const LPARAM lparam,
    const std::string_view reason) {
    auto& state = GetRuntimeState();
    state.SetHookError(HookId::process_ui_event, reason);
    state.SetLastError(reason);
    if (mode == HookMode::replace_with_fallback) {
        auto fallback = CallOriginalProcessUiEvent(self, hwnd, msg, wparam, lparam);
        if (fallback.error.empty()) {
            fallback.error = reason;
        }
        return fallback;
    }

    ProcessUiDispatchResult rejected{};
    rejected.handled = false;
    rejected.path = ProcessUiDispatchPath::rejected;
    rejected.error = std::string(reason);
    return rejected;
}

bool FireRendererDisplaySizeChanged(
    const CeguiBindings& bindings,
    void* system,
    std::string* error) {
    if (!system) {
        if (error) {
            *error = "system pointer is null";
        }
        return false;
    }

    auto* renderer = *reinterpret_cast<void**>(static_cast<unsigned char*>(system) + 0x1C);
    if (!renderer) {
        return true;
    }
    if (!bindings.event_args_vftable ||
        !bindings.renderer_event_namespace ||
        !bindings.renderer_event_display_size_changed) {
        if (error) {
            *error = "renderer event constants are unavailable";
        }
        return false;
    }

    auto** vtable = *reinterpret_cast<void***>(renderer);
    if (!vtable) {
        if (error) {
            *error = "renderer vtable is null";
        }
        return false;
    }

    const auto query_display_size = reinterpret_cast<RendererQueryDisplaySizeFn>(vtable[18]);
    const auto fire_event = reinterpret_cast<RendererFireEventFn>(vtable[5]);
    if (!query_display_size || !fire_event) {
        if (error) {
            *error = "renderer event slots are unavailable";
        }
        return false;
    }

    std::uint64_t renderer_metrics_storage = 0;
    query_display_size(renderer, &renderer_metrics_storage);

    EventArgsMirror args{};
    args.vftable = bindings.event_args_vftable;
    args.handled = 0;
    fire_event(
        renderer,
        bindings.renderer_event_display_size_changed,
        &args,
        bindings.renderer_event_namespace);
    return true;
}

bool RequestGuiSheetRedraw(
    const CeguiBindings& bindings,
    void* system,
    std::string* error) {
    if (!system) {
        if (error) {
            *error = "system pointer is null";
        }
        return false;
    }
    if (!bindings.request_redraw) {
        if (error) {
            *error = "requestRedraw binding is unavailable";
        }
        return false;
    }

    void* gui_sheet = *reinterpret_cast<void**>(static_cast<unsigned char*>(system) + 0x30);
    if (!gui_sheet) {
        return true;
    }
    bindings.request_redraw(gui_sheet);
    return true;
}

ProcessUiDispatchResult DispatchInjectedUiPlan(
    const UiInjectedPlan& plan,
    void* self,
    const HWND hwnd,
    const UINT msg,
    const WPARAM wparam,
    const LPARAM lparam,
    const HookMode mode) {
    CeguiBindings bindings{};
    std::string error;
    if (!TryGetCeguiBindings(&bindings, &error)) {
        return FallbackOrRejectProcessUiEvent(
            mode,
            self,
            hwnd,
            msg,
            wparam,
            lparam,
            error);
    }

    void* system = bindings.get_system_singleton_ptr
        ? bindings.get_system_singleton_ptr()
        : nullptr;
    if (!system) {
        return FallbackOrRejectProcessUiEvent(
            mode,
            self,
            hwnd,
            msg,
            wparam,
            lparam,
            "CEGUI system singleton is null");
    }

    ProcessUiDispatchResult result{};
    result.path = ProcessUiDispatchPath::replacement;
    switch (plan.action) {
    case UiInjectedAction::return_false:
        result.handled = false;
        return result;
    case UiInjectedAction::key_down:
        result.handled = bindings.inject_key_down
            ? bindings.inject_key_down(system, plan.code)
            : false;
        return result;
    case UiInjectedAction::key_up:
        result.handled = bindings.inject_key_up
            ? bindings.inject_key_up(system, plan.code)
            : false;
        return result;
    case UiInjectedAction::char_input:
        result.handled = bindings.inject_char
            ? bindings.inject_char(system, plan.code)
            : false;
        return result;
    case UiInjectedAction::mouse_button_down:
        result.handled = bindings.inject_mouse_button_down
            ? bindings.inject_mouse_button_down(system, plan.code)
            : false;
        return result;
    case UiInjectedAction::mouse_button_up:
        result.handled = bindings.inject_mouse_button_up
            ? bindings.inject_mouse_button_up(system, plan.code)
            : false;
        return result;
    case UiInjectedAction::mouse_wheel:
        result.handled = bindings.inject_mouse_wheel_change
            ? bindings.inject_mouse_wheel_change(system, plan.wheel_delta)
            : false;
        return result;
    case UiInjectedAction::disable_mouse_capture: {
        const auto disable_mouse_capture = ResolveRuntimeFunction<DisableMouseCaptureFn>(
            ida::kDisableMouseCapture);
        if (!disable_mouse_capture) {
            return FallbackOrRejectProcessUiEvent(
                mode,
                self,
                hwnd,
                msg,
                wparam,
                lparam,
                "DisableMouseCapture resolver failed");
        }
        disable_mouse_capture(static_cast<unsigned char*>(self));
        result.handled = false;
        return result;
    }
    case UiInjectedAction::mouse_move: {
        const auto enable_mouse_capture = ResolveRuntimeFunction<EnableMouseCaptureFn>(
            ida::kEnableMouseCapture);
        const auto transform_mouse_coordinates = ResolveRuntimeFunction<TransformMouseCoordinatesFn>(
            ida::kTransformMouseCoordinates);
        if (!enable_mouse_capture || !transform_mouse_coordinates || !bindings.inject_mouse_position) {
            return FallbackOrRejectProcessUiEvent(
                mode,
                self,
                hwnd,
                msg,
                wparam,
                lparam,
                "mouse-move dependencies are unavailable");
        }

        enable_mouse_capture(static_cast<unsigned char*>(self));
        float raw_coords[2]{
            static_cast<float>(LOWORD(lparam)),
            static_cast<float>(HIWORD(lparam)),
        };
        float transformed[2]{};
        transform_mouse_coordinates(static_cast<float*>(self), transformed, raw_coords);
        if (const int* config = ReadGameConfigPointer()) {
            const auto widescreen_plan = BuildCeguiWidescreenPlan(config[0], config[1]);
            if (GetRuntimeState().GetHookMode(HookId::cegui_renderer_constructor_2) != HookMode::observe_only &&
                GetRuntimeState().GetHookMode(HookId::cegui_renderer_constructor_2) != HookMode::mirror_compare) {
                ApplyCeguiWidescreenMouseTransform(
                    widescreen_plan,
                    raw_coords[0],
                    raw_coords[1],
                    &transformed[0],
                    &transformed[1]);
            }
        }
        result.handled = bindings.inject_mouse_position(system, transformed[0], transformed[1]);
        return result;
    }
    case UiInjectedAction::renderer_size_changed:
    case UiInjectedAction::renderer_size_changed_and_redraw: {
        if (!FireRendererDisplaySizeChanged(bindings, system, &error)) {
            return FallbackOrRejectProcessUiEvent(
                mode,
                self,
                hwnd,
                msg,
                wparam,
                lparam,
                error);
        }
        if (plan.action == UiInjectedAction::renderer_size_changed_and_redraw &&
            !RequestGuiSheetRedraw(bindings, system, &error)) {
            return FallbackOrRejectProcessUiEvent(
                mode,
                self,
                hwnd,
                msg,
                wparam,
                lparam,
                error);
        }
        result.handled = false;
        return result;
    }
    case UiInjectedAction::fallback_to_original:
        break;
    }

    return FallbackOrRejectProcessUiEvent(
        mode,
        self,
        hwnd,
        msg,
        wparam,
        lparam,
        "unhandled injected UI plan");
}

ProcessUiDispatchResult DispatchProcessUiEventShared(
    void* self,
    const HWND hwnd,
    const UINT msg,
    const WPARAM wparam,
    const LPARAM lparam) {
    auto& state = GetRuntimeState();
    const HookMode mode = state.GetHookMode(HookId::process_ui_event);
    if (mode == HookMode::observe_only) {
        return CallOriginalProcessUiEvent(self, hwnd, msg, wparam, lparam);
    }
    if (mode == HookMode::mirror_compare) {
        state.SetHookError(
            HookId::process_ui_event,
            "mirror_compare currently falls back to the original path to avoid double side effects");
        return CallOriginalProcessUiEvent(self, hwnd, msg, wparam, lparam);
    }

    std::uint32_t mapped_key = 0;
    if (msg == WM_KEYDOWN || msg == WM_KEYUP) {
        const auto map_virtual_key = ResolveRuntimeFunction<MapVirtualKeyToUiKeyFn>(
            ida::kMapVirtualKeyToUiKey);
        if (!map_virtual_key) {
            return FallbackOrRejectProcessUiEvent(
                mode,
                self,
                hwnd,
                msg,
                wparam,
                lparam,
                "MapVirtualKeyToUiKey resolver failed");
        }
        mapped_key = static_cast<std::uint32_t>(
            map_virtual_key(self, static_cast<unsigned int>(wparam)));
    }

    const auto plan = BuildUiInjectedPlan(msg, mapped_key, static_cast<std::uint32_t>(wparam));
    if (plan.action == UiInjectedAction::fallback_to_original) {
        return FallbackOrRejectProcessUiEvent(
            mode,
            self,
            hwnd,
            msg,
            wparam,
            lparam,
            "message is not handled by the injected Phase 2 ProcessUIEvent path");
    }
    return DispatchInjectedUiPlan(plan, self, hwnd, msg, wparam, lparam, mode);
}

void LogProcessUiDispatch(
    const HookId hook_id,
    const UINT msg,
    const HookMode mode,
    const ProcessUiDispatchResult& result) {
    std::ostringstream out;
    out
        << "hook=" << ToString(hook_id)
        << " msg=" << DescribeWindowsMessage(msg)
        << " mode=" << ToString(mode)
        << " path=";
    switch (result.path) {
    case ProcessUiDispatchPath::replacement:
        out << "replacement";
        break;
    case ProcessUiDispatchPath::fallback_original:
        out << "fallback_original";
        break;
    case ProcessUiDispatchPath::rejected:
        out << "rejected";
        break;
    }
    out << " result=" << (result.handled ? 1 : 0);
    if (!result.error.empty()) {
        out << " error=" << result.error;
    }
    LogHookEvent(hook_id, out.str());
}

bool SimulateKeyPressMirror(
    void* process_context,
    const WPARAM key,
    std::string* error) {
    if (!process_context) {
        if (error) {
            *error = "ProcessUIEvent context is null";
        }
        return false;
    }

    const auto key_down = DispatchProcessUiEventShared(process_context, nullptr, WM_KEYDOWN, key, 0);
    const auto key_up = DispatchProcessUiEventShared(process_context, nullptr, WM_KEYUP, key, 0);
    if (!key_down.error.empty()) {
        if (error) {
            *error = key_down.error;
        }
    }
    if (!key_up.error.empty()) {
        if (error) {
            *error = key_up.error;
        }
    }

    std::ostringstream out;
    out
        << "hook=simulate_key_press_mirror key=" << key
        << " keydown=" << (key_down.handled ? 1 : 0)
        << " keyup=" << (key_up.handled ? 1 : 0);
    if (!key_up.error.empty()) {
        out << " error=" << key_up.error;
    }
    LogHookEvent(HookId::simulate_key_press_and_release, out.str());
    return key_up.handled;
}

void LogLowLevelObserveOnlyHook(
    const HookId hook_id,
    const void* self,
    const std::string_view extra = {}) {
    std::ostringstream out;
    out
        << "hook=" << ToString(hook_id)
        << " mode=observe_only"
        << " tick=" << GetTickCount()
        << " this=" << FormatPointer(self);
    if (!extra.empty()) {
        out << ' ' << extra;
    }
    LogHookEvent(hook_id, out.str());
}

}  // namespace

bool __fastcall Hook_ProcessUiEvent(
    void* self,
    void*,
    const HWND hwnd,
    const UINT msg,
    const WPARAM wparam,
    const LPARAM lparam) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::process_ui_event);
    state.SetUiDispatchReady(true);
    state.SetLastUiEvent(DescribeWindowsMessage(msg));
    const auto result = DispatchProcessUiEventShared(self, hwnd, msg, wparam, lparam);
    ReadCurrentPalivEntry();
    LogProcessUiDispatch(HookId::process_ui_event, msg, state.GetHookMode(HookId::process_ui_event), result);
    return result.handled;
}

char __fastcall Hook_HandleUiMessageAndProcess(
    void* self,
    void*,
    const HWND hwnd,
    const UINT msg,
    const WPARAM wparam,
    const LPARAM lparam) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::handle_ui_message);
    state.SetUiDispatchReady(true);
    state.SetLastUiEvent(std::string("HandleUIMessageAndProcess:") + DescribeWindowsMessage(msg));

    const HookMode handle_mode = state.GetHookMode(HookId::handle_ui_message);
    if (handle_mode == HookMode::observe_only || handle_mode == HookMode::mirror_compare) {
        LogHookEvent(
            HookId::handle_ui_message,
            std::string("hook=handle_ui_message mode=") + ToString(handle_mode) +
            " path=fallback_original msg=" + DescribeWindowsMessage(msg));
        return g_original_handle_ui_message
            ? g_original_handle_ui_message(self, hwnd, msg, wparam, lparam)
            : 0;
    }

    auto* raw_self = static_cast<unsigned char*>(self);
    auto* message_handled = raw_self + ida::kUiFrameManagerMessageHandledByteOffset;
    auto* escape_simulation = raw_self + ida::kUiFrameManagerEscapeSimulationByteOffset;
    auto* process_context = *reinterpret_cast<void**>(
        raw_self + ida::kUiFrameManagerProcessUiEventThisOffset);

    *message_handled = 0;
    ProcessUiDispatchResult result{};
    if (process_context) {
        result = DispatchProcessUiEventShared(process_context, hwnd, msg, wparam, lparam);
        *message_handled = result.handled ? 1 : 0;
    }

    if (msg == WM_RBUTTONUP && process_context && *escape_simulation) {
        std::string esc_error;
        const bool esc_result = SimulateKeyPressMirror(process_context, VK_ESCAPE, &esc_error);
        std::ostringstream esc_log;
        esc_log
            << "hook=handle_ui_message esc_simulation=inline_mirror"
            << " result=" << (esc_result ? 1 : 0);
        if (!esc_error.empty()) {
            esc_log << " error=" << esc_error;
            state.SetLastError(esc_error);
        }
        LogHookEvent(HookId::handle_ui_message, esc_log.str());
    }

    ReadCurrentPalivEntry();
    std::ostringstream log_line;
    log_line
        << "hook=handle_ui_message mode=" << ToString(handle_mode)
        << " msg=" << DescribeWindowsMessage(msg)
        << " handled_byte=" << static_cast<int>(*message_handled)
        << " context=" << FormatPointer(process_context);
    if (!result.error.empty()) {
        log_line << " error=" << result.error;
    }
    LogHookEvent(HookId::handle_ui_message, log_line.str());
    return *message_handled;
}

bool __fastcall Hook_SimulateKeyPressAndRelease(
    void* self,
    void*,
    const WPARAM wide_char) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::simulate_key_press_and_release);
    const HookMode mode = state.GetHookMode(HookId::simulate_key_press_and_release);
    if (mode == HookMode::observe_only || mode == HookMode::mirror_compare) {
        LogHookEvent(
            HookId::simulate_key_press_and_release,
            std::string("hook=simulate_key_press_and_release mode=") + ToString(mode) +
            " path=fallback_original key=" + std::to_string(wide_char));
        return g_original_simulate_key_press_and_release
            ? g_original_simulate_key_press_and_release(self, wide_char)
            : false;
    }

    auto* process_context = reinterpret_cast<void**>(self)[102];
    std::string error;
    const bool result = SimulateKeyPressMirror(process_context, wide_char, &error);
    if (!error.empty()) {
        state.SetLastError(error);
    }
    ReadCurrentPalivEntry();
    return result;
}

int __cdecl Hook_ProcessInputs() {
    GetRuntimeState().IncrementHookCall(HookId::process_inputs);
    RefreshWidescreenHudLayoutFixups();
    TickGamepadInput();
    LogLowLevelObserveOnlyHook(HookId::process_inputs, nullptr);
    return g_original_process_inputs
        ? g_original_process_inputs()
        : 0;
}

char __cdecl Hook_GiTalk(void* text_arg, void* voice_key_arg) {
    auto& state = GetRuntimeState();
    state.IncrementHookCall(HookId::gi_talk);
    state.SetLastUiEvent("giTalk");

    const HookMode mode = state.GetHookMode(HookId::gi_talk);
    if (mode == HookMode::observe_only || mode == HookMode::mirror_compare) {
    LogHookEvent(
        HookId::gi_talk,
        std::string("hook=gi_talk mode=") + ToString(mode) +
        " path=fallback_original");
        return g_original_gi_talk
            ? g_original_gi_talk(text_arg, voice_key_arg)
            : 0;
    }

    std::ostringstream out;
    out
        << "hook=gi_talk"
        << " original_text_arg=" << FormatPointer(text_arg)
        << " voice_key_arg=" << FormatPointer(voice_key_arg)
        << " rewritten_text=GBK:D2D1D7A2C8EB";
    LogHookEvent(HookId::gi_talk, out.str());

    if (!g_original_gi_talk) {
        state.SetHookError(HookId::gi_talk, "original giTalk trampoline is null");
        state.SetLastError("original giTalk trampoline is null");
        return 0;
    }

    return g_original_gi_talk(
        &g_injected_gi_talk_arg,
        voice_key_arg);
}

void* GetReplacementForHook(const HookId id) {
    switch (id) {
    case HookId::process_ui_event:
        return reinterpret_cast<void*>(&Hook_ProcessUiEvent);
    case HookId::handle_ui_message:
        return reinterpret_cast<void*>(&Hook_HandleUiMessageAndProcess);
    case HookId::simulate_key_press_and_release:
        return reinterpret_cast<void*>(&Hook_SimulateKeyPressAndRelease);
    case HookId::process_inputs:
        return reinterpret_cast<void*>(&Hook_ProcessInputs);
    case HookId::gi_talk:
        return reinterpret_cast<void*>(&Hook_GiTalk);
    default:
        if (void* replacement = GetCeguiRendererReplacementForHook(id)) {
            return replacement;
        }
        if (void* replacement = GetCeguiFontReplacementForHook(id)) {
            return replacement;
        }
        if (void* replacement = GetMinimapReplacementForHook(id)) {
            return replacement;
        }
        if (void* replacement = GetWindowFocusReplacementForHook(id)) {
            return replacement;
        }
        if (void* replacement = GetD3d9QualityReplacementForHook(id)) {
            return replacement;
        }
        if (void* replacement = GetBattleUiLayoutReplacementForHook(id)) {
            return replacement;
        }
        return GetCameraReplacementForHook(id);
    }
}

void SetOriginalTrampoline(const HookId id, void* trampoline) {
    switch (id) {
    case HookId::process_ui_event:
        g_original_process_ui_event = reinterpret_cast<ProcessUiEventFn>(trampoline);
        break;
    case HookId::handle_ui_message:
        g_original_handle_ui_message = reinterpret_cast<HandleUiMessageAndProcessFn>(trampoline);
        break;
    case HookId::simulate_key_press_and_release:
        g_original_simulate_key_press_and_release =
            reinterpret_cast<SimulateKeyPressAndReleaseFn>(trampoline);
        break;
    case HookId::process_inputs:
        g_original_process_inputs = reinterpret_cast<ProcessInputsFn>(trampoline);
        break;
    case HookId::gi_talk:
        g_original_gi_talk = reinterpret_cast<GiTalkFn>(trampoline);
        break;
    default:
        SetCeguiRendererOriginalTrampoline(id, trampoline);
        SetCeguiFontOriginalTrampoline(id, trampoline);
        SetMinimapOriginalTrampoline(id, trampoline);
        SetWindowFocusOriginalTrampoline(id, trampoline);
        SetD3d9QualityOriginalTrampoline(id, trampoline);
        SetBattleUiLayoutOriginalTrampoline(id, trampoline);
        SetCameraOriginalTrampoline(id, trampoline);
        break;
    }
}

bool DispatchUiMessageCommand(const UiMessageCommand& command, std::string* error) {
    if (!command.bypass_os_queue) {
        HWND hwnd = FindCurrentProcessWindow();
        if (!hwnd) {
            hwnd = GetForegroundWindow();
        }
        if (!hwnd) {
            if (error) {
                *error = "failed to resolve a target window for OS queue dispatch";
            }
            return false;
        }
        if (!PostMessageA(
                hwnd,
                command.msg,
                static_cast<WPARAM>(command.wparam),
                static_cast<LPARAM>(command.lparam))) {
            if (error) {
                *error = "PostMessageA failed: " + FormatWindowsError(GetLastError());
            }
            return false;
        }
        LogHookEvent(
            HookId::process_ui_event,
            std::string("dispatch_ui_message path=os_queue msg=") +
            DescribeWindowsMessage(command.msg));
        return true;
    }

    auto& state = GetRuntimeState();
    if (!state.UiDispatchReady() && !RefreshUiDispatchReady(error)) {
        if (error) {
            if (error->empty()) {
                *error = "ui dispatch seam is not ready yet";
            }
        }
        return false;
    }

    const auto get_ui_frame_manager = ResolveRuntimeFunction<UiFrameManagerGetInstanceFn>(
        ida::kUiFrameManagerGetInstance);
    const auto handle_ui_message = ResolveRuntimeFunction<HandleUiMessageAndProcessFn>(
        ida::kHandleUiMessageAndProcess);
    if (!get_ui_frame_manager || !handle_ui_message) {
        if (error) {
            *error = "UI dispatch helpers are unavailable";
        }
        return false;
    }

    void* ui_frame_manager = get_ui_frame_manager();
    if (!ui_frame_manager) {
        if (error) {
            *error = "UIFrameManager_GetInstance returned null";
        }
        return false;
    }

    const char handled = handle_ui_message(
        ui_frame_manager,
        nullptr,
        static_cast<UINT>(command.msg),
        static_cast<WPARAM>(command.wparam),
        static_cast<LPARAM>(command.lparam));
    ReadCurrentPalivEntry();
    GetRuntimeState().SetLastUiEvent(std::string("dispatch:") + DescribeWindowsMessage(command.msg));
    LogHookEvent(
        HookId::handle_ui_message,
        std::string("dispatch_ui_message path=seam msg=") +
        DescribeWindowsMessage(command.msg) +
        " handled=" + std::to_string(handled != 0));
    return handled != 0;
}

bool DispatchSimulatedKey(
    const std::uint32_t virtual_key,
    const bool key_up,
    const bool bypass_os_queue,
    std::string* error) {
    UiMessageCommand command{};
    command.msg = key_up ? WM_KEYUP : WM_KEYDOWN;
    command.wparam = virtual_key;
    command.lparam = 0;
    command.bypass_os_queue = bypass_os_queue;
    return DispatchUiMessageCommand(command, error);
}

bool RefreshUiDispatchReady(std::string* reason) {
    auto& state = GetRuntimeState();
    const bool previous = state.UiDispatchReady();
    if (previous) {
        if (reason) {
            *reason = {};
        }
        return true;
    }

    bool ready = false;
    std::string local_reason;

    const auto get_ui_frame_manager = ResolveRuntimeFunction<UiFrameManagerGetInstanceFn>(
        ida::kUiFrameManagerGetInstance);
    if (!get_ui_frame_manager) {
        local_reason = "UIFrameManager_GetInstance resolver is unavailable";
        ready = false;
    } else {
        void* ui_frame_manager = get_ui_frame_manager();
        if (!ui_frame_manager) {
            local_reason = "UIFrameManager_GetInstance returned null";
            ready = false;
        } else if (!state.HooksReady()) {
            local_reason = "bootstrap hooks are not ready";
            ready = false;
        } else {
            void* process_context = *reinterpret_cast<void**>(
                static_cast<unsigned char*>(ui_frame_manager) +
                ida::kUiFrameManagerProcessUiEventThisOffset);
            if (process_context) {
                ready = true;
            } else {
                local_reason = "uiFrameManager process context is null";
                ready = false;
            }
        }
    }

    if (ready) {
        state.SetUiDispatchReady(true);
    }
    if (reason) {
        *reason = ready ? std::string() : local_reason;
    }
    if (!previous && ready) {
        LogHookEvent(HookId::process_ui_event, std::string("ui_dispatch_ready=") + (ready ? "1" : "0"));
    }
    return ready;
}

std::uint32_t ReadCurrentPalivEntry() noexcept {
    const auto get_pal_game_iv = ResolveRuntimeFunction<PalGameIvGetInstanceFn>(
        ida::kPalGameIvGetInstance);
    if (!get_pal_game_iv) {
        return 0;
    }
    __try {
        auto* instance = static_cast<std::uint32_t*>(get_pal_game_iv());
        if (!instance) {
            return 0;
        }
        const std::uint32_t entry = instance[ida::kPalGameIvCurrentStateEntryIndex];
        GetRuntimeState().ObservePalivEntry(entry);
        return entry;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        GetRuntimeState().SetLastError("ReadCurrentPalivEntry trapped an SEH exception");
        return 0;
    }
}

}  // namespace pal4::inject
