#include "gamepad_runtime.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>

#include "input_hooks.h"
#include "gamepad_control_hooks.h"
#include "cegui_bindings.h"
#include "pal4inject/gamepad.h"
#include "pal4inject/ida_addresses.h"
#include "runtime_state.h"
#include "ui_snapshot_runtime.h"

namespace pal4::inject {
namespace {

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using GetInputManagerFn = void* (__cdecl*)();
using UpdateKeyOrButtonStateFn = char (__thiscall*)(void*, int, int);

constexpr std::array<const char*, 3> kXInputDlls{
    "xinput1_4.dll",
    "xinput9_1_0.dll",
    "xinput1_3.dll",
};
constexpr BYTE kTriggerThreshold = 128;
constexpr int kVerticalPageCount = 6;
constexpr int kMainPageCount = 7;
constexpr std::uint32_t kRepeatInitialDelayMs = 300;
constexpr std::uint32_t kRepeatIntervalMs = 110;
constexpr std::uint32_t kGamepadToMouseDebounceMs = 250;
constexpr std::uint8_t kSystemMenuCloseAllMaxSteps = 8;
constexpr std::uint8_t kSystemMenuOpenValidationDelayFrames = 4;
constexpr std::uint8_t kSystemMenuHiddenConfirmationFrames = 2;
constexpr int kKeyCodeA = 97;
constexpr int kKeyCodeD = 100;
constexpr int kKeyCodeS = 115;
constexpr int kKeyCodeW = 119;

struct DirectionRepeatState {
    GamepadRepeatState up{};
    GamepadRepeatState down{};
    GamepadRepeatState left{};
    GamepadRepeatState right{};
};

struct GamepadRuntime {
    HMODULE xinput_module = nullptr;
    XInputGetStateFn get_state = nullptr;
    bool load_attempted = false;
    bool connected = false;
    bool system_menu_active = false;
    bool system_menu_cancel_pending = false;
    std::uint8_t system_menu_close_all_steps_remaining = 0;
    std::uint8_t system_menu_validation_delay_frames = 0;
    std::uint8_t system_menu_hidden_frames = 0;
    GamepadInputContext context = GamepadInputContext::gameplay;
    std::uint32_t current_main_page = 0;
    std::uint32_t current_vertical_page = 0;
    XINPUT_STATE previous_state{};
    bool hold_w = false;
    bool hold_a = false;
    bool hold_s = false;
    bool hold_d = false;
    bool hold_mouse_left = false;
    DirectionRepeatState dpad_repeat{};
    std::array<GamepadRepeatState, kXbox360ButtonCount> button_repeat{};
    GamepadModernControlState modern_controls{};
    bool cursor_hide_requested = false;
    bool cursor_hidden_applied = false;
    bool cegui_cursor_hidden = false;
    bool win32_cursor_hidden = false;
    bool native_cursor_suppressed = false;
    HCURSOR saved_win32_cursor = nullptr;
    DWORD last_gamepad_activity_tick = 0;
};

GamepadRuntime& GetGamepadRuntime() {
    static GamepadRuntime runtime;
    return runtime;
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
    return base == 0
        ? nullptr
        : reinterpret_cast<Fn>(ida::ResolveRuntimeAddress(base, ida_ea));
}

void LogGamepadEvent(const std::string_view text) {
    auto& state = GetRuntimeState();
    if (state.GamepadLogEnabled()) {
        state.AppendEventLog(std::string("gamepad:") + std::string(text));
    }
}

bool ApplyGamepadCursorVisibility(GamepadRuntime* const runtime) {
    if (!runtime) {
        return false;
    }

    CeguiBindings bindings{};
    std::string error;
    const bool bindings_ready =
        TryGetCeguiBindings(&bindings, &error) &&
        bindings.get_mouse_cursor_singleton_ptr;
    void* const cursor = bindings_ready
        ? bindings.get_mouse_cursor_singleton_ptr()
        : nullptr;
    bool cegui_cursor_visible = cursor && bindings.mouse_cursor_is_visible
        ? bindings.mouse_cursor_is_visible(cursor)
        : false;
    if (cursor && runtime->cursor_hide_requested) {
        if (bindings.mouse_cursor_hide) {
            bindings.mouse_cursor_hide(cursor);
            runtime->cegui_cursor_hidden = true;
            cegui_cursor_visible = false;
        }
    } else if (cursor && runtime->cegui_cursor_hidden) {
        if (bindings.mouse_cursor_show) {
            bindings.mouse_cursor_show(cursor);
            runtime->cegui_cursor_hidden = false;
            cegui_cursor_visible = true;
        }
    }

    const auto presentation = SelectGamepadCursorPresentation(
        runtime->cursor_hide_requested,
        cegui_cursor_visible);
    const bool suppress_native =
        presentation != GamepadCursorPresentation::native_only;
    runtime->native_cursor_suppressed = suppress_native;
    if (suppress_native) {
        if (!runtime->win32_cursor_hidden) {
            runtime->saved_win32_cursor = GetCursor();
            runtime->win32_cursor_hidden = true;
        }
        SetCursor(nullptr);
    } else if (runtime->win32_cursor_hidden) {
        SetCursor(runtime->saved_win32_cursor);
        runtime->saved_win32_cursor = nullptr;
        runtime->win32_cursor_hidden = false;
    }

    const bool visibility_applied = runtime->cursor_hide_requested
        ? runtime->win32_cursor_hidden || runtime->cegui_cursor_hidden
        : presentation == GamepadCursorPresentation::cegui_only
            ? runtime->win32_cursor_hidden && !runtime->cegui_cursor_hidden
            : !runtime->win32_cursor_hidden;
    if (visibility_applied &&
        runtime->cursor_hidden_applied != runtime->cursor_hide_requested) {
        runtime->cursor_hidden_applied = runtime->cursor_hide_requested;
        LogGamepadEvent(runtime->cursor_hidden_applied
            ? "cursor_hidden=1 input=gamepad"
            : "cursor_hidden=0 input=mouse");
    }
    return visibility_applied;
}

void RequestGamepadCursorHidden(
    GamepadRuntime* const runtime,
    const bool hidden) {
    if (!runtime) {
        return;
    }
    runtime->cursor_hide_requested = hidden;
    ApplyGamepadCursorVisibility(runtime);
}

bool EnsureXInputLoaded(GamepadRuntime* const runtime) {
    if (!runtime) {
        return false;
    }
    if (runtime->get_state) {
        return true;
    }
    if (runtime->load_attempted) {
        return false;
    }
    runtime->load_attempted = true;
    for (const auto* const dll_name : kXInputDlls) {
        HMODULE module = LoadLibraryA(dll_name);
        if (!module) {
            continue;
        }
        const auto get_state = reinterpret_cast<XInputGetStateFn>(
            GetProcAddress(module, "XInputGetState"));
        if (!get_state) {
            FreeLibrary(module);
            continue;
        }
        runtime->xinput_module = module;
        runtime->get_state = get_state;
        LogGamepadEvent(std::string("xinput_loaded=") + dll_name);
        return true;
    }
    GetRuntimeState().SetLastError("gamepad: unable to load an XInput runtime");
    return false;
}

bool SendUiSeamKey(const std::uint32_t virtual_key, const bool key_up) {
    std::string error;
    const bool ok = DispatchSimulatedKey(virtual_key, key_up, true, &error);
    if (!ok && !error.empty()) {
        GetRuntimeState().SetLastError(error);
    }
    return ok;
}

bool SendInjectedKeyboardInput(
    const std::uint32_t virtual_key,
    const bool key_up) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(virtual_key);
    input.ki.dwFlags = key_up ? KEYEVENTF_KEYUP : 0;
    return SendInput(1, &input, sizeof(input)) == 1;
}

bool UpdateGameplayKeyRawState(const int key_code, const bool pressed) {
    const auto get_input_manager =
        ResolveRuntimeFunction<GetInputManagerFn>(ida::kGetInputManager);
    const auto update_key_or_button_state =
        ResolveRuntimeFunction<UpdateKeyOrButtonStateFn>(ida::kUpdateKeyOrButtonState);
    if (!get_input_manager || !update_key_or_button_state) {
        GetRuntimeState().SetLastError("gamepad gameplay input helpers are unavailable");
        return false;
    }
    void* const input_manager = get_input_manager();
    if (!input_manager) {
        GetRuntimeState().SetLastError("gamepad GetInputManager returned null");
        return false;
    }
    update_key_or_button_state(input_manager, key_code, pressed ? 1 : 0);
    return true;
}

bool TapInjectedKey(const std::uint32_t virtual_key) {
    return SendInjectedKeyboardInput(virtual_key, false) &&
        SendInjectedKeyboardInput(virtual_key, true);
}

bool TapUiSeamKey(const std::uint32_t virtual_key) {
    return SendUiSeamKey(virtual_key, false) &&
        SendUiSeamKey(virtual_key, true);
}

LPARAM BuildMouseClientLParam() {
    POINT point{};
    if (!GetCursorPos(&point)) {
        return 0;
    }
    HWND hwnd = GetForegroundWindow();
    if (hwnd) {
        ScreenToClient(hwnd, &point);
    }
    return MAKELPARAM(point.x, point.y);
}

void SetMouseLeftHeld(const bool pressed, GamepadRuntime* const runtime) {
    if (!runtime || runtime->hold_mouse_left == pressed) {
        return;
    }
    UiMessageCommand command{};
    command.msg = pressed ? WM_LBUTTONDOWN : WM_LBUTTONUP;
    command.wparam = pressed ? MK_LBUTTON : 0;
    command.lparam = static_cast<std::uint32_t>(BuildMouseClientLParam());
    command.bypass_os_queue = false;
    std::string error;
    const bool ok = DispatchUiMessageCommand(command, &error);
    if (!ok && !error.empty()) {
        GetRuntimeState().SetLastError(error);
    }
    runtime->hold_mouse_left = pressed && ok;
}

void SetHeldKey(
    const bool pressed,
    const int key_code,
    bool* const held_flag) {
    if (!held_flag) {
        return;
    }
    if (!pressed) {
        *held_flag = false;
        return;
    }
    // PAL4 clears non-physical keys at the beginning of every input frame.
    // Applying the state twice advances it from "just pressed" to "held".
    if (UpdateGameplayKeyRawState(key_code, true) &&
        UpdateGameplayKeyRawState(key_code, true)) {
        *held_flag = true;
    }
}

void ReleaseGameplayHolds(GamepadRuntime* const runtime) {
    if (!runtime) {
        return;
    }
    runtime->hold_w = false;
    runtime->hold_a = false;
    runtime->hold_s = false;
    runtime->hold_d = false;
    runtime->modern_controls = {};
    SetMouseLeftHeld(false, runtime);
}

GamepadInputContext DetermineContext(const GamepadRuntime& runtime) {
    if (runtime.system_menu_active) {
        return GamepadInputContext::system_menu;
    }
    if (GetRuntimeState().LastPalivEntryObserved() == 0) {
        return GamepadInputContext::menu;
    }
    return GamepadInputContext::gameplay;
}

bool IsButtonPressed(
    const XINPUT_STATE& state,
    const Xbox360Button button) {
    WORD mask = 0;
    switch (button) {
    case Xbox360Button::a:
        mask = XINPUT_GAMEPAD_A;
        break;
    case Xbox360Button::b:
        mask = XINPUT_GAMEPAD_B;
        break;
    case Xbox360Button::x:
        mask = XINPUT_GAMEPAD_X;
        break;
    case Xbox360Button::y:
        mask = XINPUT_GAMEPAD_Y;
        break;
    case Xbox360Button::left_shoulder:
        mask = XINPUT_GAMEPAD_LEFT_SHOULDER;
        break;
    case Xbox360Button::right_shoulder:
        mask = XINPUT_GAMEPAD_RIGHT_SHOULDER;
        break;
    case Xbox360Button::back:
        mask = XINPUT_GAMEPAD_BACK;
        break;
    case Xbox360Button::start:
        mask = XINPUT_GAMEPAD_START;
        break;
    case Xbox360Button::left_thumb:
        mask = XINPUT_GAMEPAD_LEFT_THUMB;
        break;
    case Xbox360Button::right_thumb:
        mask = XINPUT_GAMEPAD_RIGHT_THUMB;
        break;
    case Xbox360Button::left_trigger:
        return state.Gamepad.bLeftTrigger >= kTriggerThreshold;
    case Xbox360Button::right_trigger:
        return state.Gamepad.bRightTrigger >= kTriggerThreshold;
    case Xbox360Button::count:
        return false;
    }
    return (state.Gamepad.wButtons & mask) != 0;
}

bool IsButtonJustPressed(
    const XINPUT_STATE& current,
    const XINPUT_STATE& previous,
    const Xbox360Button button) {
    return IsButtonPressed(current, button) && !IsButtonPressed(previous, button);
}

bool ClickSystemMenuClose() {
    std::string error;
    const bool clicked = ClickLikelySystemMenuCloseButton(&error);
    if (!clicked && !error.empty()) {
        GetRuntimeState().SetLastError(error);
    }
    return clicked;
}

void ResetSystemMenuState(GamepadRuntime* const runtime) {
    if (!runtime) {
        return;
    }
    runtime->system_menu_active = false;
    runtime->system_menu_cancel_pending = false;
    runtime->system_menu_close_all_steps_remaining = 0;
    runtime->system_menu_validation_delay_frames = 0;
    runtime->system_menu_hidden_frames = 0;
    runtime->current_main_page = 0;
    runtime->current_vertical_page = 0;
}

void SynchronizeSystemMenuShellVisibility(
    GamepadRuntime* const runtime) {
    if (!runtime || !runtime->system_menu_active) {
        return;
    }
    if (runtime->system_menu_validation_delay_frames != 0) {
        --runtime->system_menu_validation_delay_frames;
        return;
    }

    bool shell_visible = true;
    std::string error;
    if (!QuerySystemMenuShellVisible(&shell_visible, &error)) {
        if (!error.empty()) {
            GetRuntimeState().SetLastError(error);
        }
        return;
    }
    if (shell_visible) {
        runtime->system_menu_hidden_frames = 0;
        return;
    }
    if (++runtime->system_menu_hidden_frames >=
        kSystemMenuHiddenConfirmationFrames) {
        ResetSystemMenuState(runtime);
        ReleaseGameplayHolds(runtime);
        LogGamepadEvent(
            "system_menu_invalidated reason=shell_missing_confirmed");
    }
}

void ToggleSystemMenu(GamepadRuntime* const runtime) {
    if (!runtime) {
        return;
    }

    bool menu_visible = runtime->system_menu_active;
    bool page_visible = false;
    std::string query_error;
    if (!QuerySystemMenuState(
            &menu_visible,
            &page_visible,
            &query_error) &&
        !query_error.empty()) {
        GetRuntimeState().SetLastError(query_error);
    }
    if (menu_visible) {
        if (ClickSystemMenuClose()) {
            runtime->system_menu_close_all_steps_remaining =
                kSystemMenuCloseAllMaxSteps;
        }
        runtime->system_menu_cancel_pending = false;
    } else {
        runtime->system_menu_active = true;
        runtime->current_main_page = 0;
        runtime->current_vertical_page = 0;
        TapUiSeamKey(VK_F1);
        runtime->system_menu_cancel_pending = false;
        runtime->system_menu_close_all_steps_remaining = 0;
        runtime->system_menu_validation_delay_frames =
            kSystemMenuOpenValidationDelayFrames;
        runtime->system_menu_hidden_frames = 0;
    }
    ReleaseGameplayHolds(runtime);
}

void ResolvePendingSystemMenuTransition(GamepadRuntime* const runtime) {
    if (!runtime ||
        (!runtime->system_menu_cancel_pending &&
         runtime->system_menu_close_all_steps_remaining == 0)) {
        return;
    }

    bool visible = true;
    bool page_visible = false;
    std::string error;
    if (!QuerySystemMenuState(&visible, &page_visible, &error)) {
        if (!error.empty()) {
            GetRuntimeState().SetLastError(error);
        }
        runtime->system_menu_cancel_pending = false;
        runtime->system_menu_close_all_steps_remaining = 0;
        return;
    }

    runtime->system_menu_active = visible;
    if (runtime->system_menu_close_all_steps_remaining != 0) {
        if (!visible) {
            runtime->system_menu_close_all_steps_remaining = 0;
        } else if (ClickSystemMenuClose()) {
            --runtime->system_menu_close_all_steps_remaining;
        } else {
            runtime->system_menu_close_all_steps_remaining = 0;
        }
    } else {
        runtime->system_menu_cancel_pending = false;
    }
    if (!visible) {
        runtime->current_main_page = 0;
        runtime->current_vertical_page = 0;
    }
    LogGamepadEvent(std::string("menu_transition_resolved menu_visible=") +
        (visible ? "1" : "0"));
}

void ExecuteImmediateAction(
    GamepadRuntime* const runtime,
    const GamepadAction action) {
    if (!runtime) {
        return;
    }
    switch (action) {
    case GamepadAction::confirm:
        if (runtime->system_menu_active) {
            TapUiSeamKey(VK_RETURN);
        } else {
            TapInjectedKey(VK_SPACE);
        }
        break;
    case GamepadAction::cancel:
        {
            bool menu_visible = runtime->system_menu_active;
            bool page_visible = false;
            std::string error;
            if (!QuerySystemMenuState(
                    &menu_visible,
                    &page_visible,
                    &error) &&
                !error.empty()) {
                GetRuntimeState().SetLastError(error);
            }
            runtime->system_menu_active = menu_visible;
            const auto cancel_action = SelectSystemMenuCancelAction(
                menu_visible,
                page_visible);
            if (cancel_action == SystemMenuCancelAction::close_root) {
                if (ClickSystemMenuClose()) {
                    runtime->system_menu_cancel_pending = true;
                }
            } else if (cancel_action == SystemMenuCancelAction::escape_nested) {
                TapUiSeamKey(VK_ESCAPE);
                runtime->system_menu_cancel_pending = true;
            } else if (DetermineContext(*runtime) == GamepadInputContext::menu) {
                TapUiSeamKey(VK_ESCAPE);
            }
        }
        ReleaseGameplayHolds(runtime);
        break;
    case GamepadAction::run_toggle:
        TapInjectedKey('R');
        break;
    case GamepadAction::auto_forward:
        TapInjectedKey('F');
        break;
    case GamepadAction::map:
        if (runtime->system_menu_active) {
            TapUiSeamKey('M');
        } else {
            TapInjectedKey('M');
        }
        break;
    case GamepadAction::switch_leader:
        TapInjectedKey(VK_TAB);
        break;
    case GamepadAction::camera_distance_cycle:
        if (DetermineContext(*runtime) == GamepadInputContext::gameplay &&
            !CycleGamepadCameraDistance()) {
            GetRuntimeState().SetLastError(
                "gamepad camera-distance control is unavailable");
        }
        break;
    case GamepadAction::system_menu:
        ToggleSystemMenu(runtime);
        break;
    case GamepadAction::none:
    case GamepadAction::mouse_left:
    case GamepadAction::main_page_previous:
    case GamepadAction::main_page_next:
    case GamepadAction::sub_page_previous:
    case GamepadAction::sub_page_next:
        break;
    }
}

bool IsRepeatingPageAction(const GamepadAction action) {
    return action == GamepadAction::main_page_previous ||
        action == GamepadAction::main_page_next ||
        action == GamepadAction::sub_page_previous ||
        action == GamepadAction::sub_page_next;
}

void ExecutePageAction(
    GamepadRuntime* const runtime,
    const GamepadAction action) {
    if (!runtime || !runtime->system_menu_active) {
        return;
    }
    if (action == GamepadAction::main_page_previous ||
        action == GamepadAction::main_page_next) {
        const int delta = action == GamepadAction::main_page_previous ? -1 : 1;
        runtime->current_main_page = static_cast<std::uint32_t>(
            WrapGamepadCycleIndex(
                static_cast<int>(runtime->current_main_page), delta, kMainPageCount));
        const bool dispatched =
            TapUiSeamKey(VK_F1 + runtime->current_main_page);
        LogGamepadEvent(
            std::string("page_action action=") + ToString(action) +
            " main_page=" + std::to_string(runtime->current_main_page) +
            " dispatched=" + (dispatched ? "1" : "0"));
        return;
    }
    const int delta = action == GamepadAction::sub_page_previous ? -1 : 1;
    runtime->current_vertical_page = static_cast<std::uint32_t>(
        WrapGamepadCycleIndex(
            static_cast<int>(runtime->current_vertical_page),
            delta,
            kVerticalPageCount));
    const bool dispatched =
        TapUiSeamKey('1' + runtime->current_vertical_page);
    LogGamepadEvent(
        std::string("page_action action=") + ToString(action) +
        " sub_page=" + std::to_string(runtime->current_vertical_page) +
        " dispatched=" + (dispatched ? "1" : "0"));
}

void UpdateMappedButtons(
    GamepadRuntime* const runtime,
    const XINPUT_STATE& current,
    const XINPUT_STATE& previous,
    const Xbox360GamepadMapping& mapping,
    const DWORD now_ms) {
    if (!runtime) {
        return;
    }
    bool mouse_left_held = false;
    for (std::size_t index = 0; index < kXbox360ButtonCount; ++index) {
        const auto button = static_cast<Xbox360Button>(index);
        const auto action = GetGamepadBinding(mapping, button);
        const bool pressed = IsButtonPressed(current, button);
        if (action == GamepadAction::mouse_left) {
            mouse_left_held = mouse_left_held || pressed;
            runtime->button_repeat[index] = {};
            continue;
        }
        if (IsRepeatingPageAction(action)) {
            const bool repeat = ConsumeGamepadRepeat(
                pressed && runtime->system_menu_active,
                now_ms,
                kRepeatInitialDelayMs,
                kRepeatIntervalMs,
                &runtime->button_repeat[index]);
            if (repeat) {
                ExecutePageAction(runtime, action);
            }
            continue;
        }
        runtime->button_repeat[index] = {};
        if (IsButtonJustPressed(current, previous, button)) {
            ExecuteImmediateAction(runtime, action);
        }
    }
    SetMouseLeftHeld(mouse_left_held && !runtime->system_menu_active, runtime);
}

void DispatchRepeatedDpad(
    const bool pressed,
    GamepadRepeatState* const repeat,
    const std::uint32_t virtual_key,
    const DWORD now_ms) {
    if (ConsumeGamepadRepeat(
            pressed,
            now_ms,
            kRepeatInitialDelayMs,
            kRepeatIntervalMs,
            repeat)) {
        TapUiSeamKey(virtual_key);
    }
}

void UpdateDpadNavigation(
    GamepadRuntime* const runtime,
    const XINPUT_STATE& current,
    const DWORD now_ms) {
    if (!runtime) {
        return;
    }
    DispatchRepeatedDpad(
        (current.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0,
        &runtime->dpad_repeat.up,
        VK_UP,
        now_ms);
    DispatchRepeatedDpad(
        (current.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0,
        &runtime->dpad_repeat.down,
        VK_DOWN,
        now_ms);
    DispatchRepeatedDpad(
        (current.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0,
        &runtime->dpad_repeat.left,
        VK_LEFT,
        now_ms);
    DispatchRepeatedDpad(
        (current.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0,
        &runtime->dpad_repeat.right,
        VK_RIGHT,
        now_ms);
}

void UpdateGameplayStick(
    GamepadRuntime* const runtime,
    const XINPUT_STATE& current,
    const GamepadInputContext context) {
    if (!runtime || context != GamepadInputContext::gameplay) {
        ReleaseGameplayHolds(runtime);
        return;
    }
    if (GetRuntimeState().GamepadModernControls()) {
        runtime->hold_w = false;
        runtime->hold_a = false;
        runtime->hold_s = false;
        runtime->hold_d = false;
        runtime->modern_controls.movement = BuildGamepadAnalogStick(
            current.Gamepad.sThumbLX,
            current.Gamepad.sThumbLY,
            XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        runtime->modern_controls.camera = BuildGamepadAnalogStick(
            current.Gamepad.sThumbRX,
            current.Gamepad.sThumbRY,
            XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        runtime->modern_controls.active = true;
        return;
    }
    runtime->modern_controls = {};
    const auto axes = BuildGamepadDigitalAxes(
        current.Gamepad.sThumbLX,
        current.Gamepad.sThumbLY,
        XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    SetHeldKey(axes.up, kKeyCodeW, &runtime->hold_w);
    SetHeldKey(axes.left, kKeyCodeA, &runtime->hold_a);
    SetHeldKey(axes.down, kKeyCodeS, &runtime->hold_s);
    SetHeldKey(axes.right, kKeyCodeD, &runtime->hold_d);
}

void UpdateConnectionState(
    GamepadRuntime* const runtime,
    const bool connected) {
    if (!runtime || runtime->connected == connected) {
        return;
    }
    runtime->connected = connected;
    GetRuntimeState().SetGamepadConnected(connected);
    LogGamepadEvent(connected ? "connected=1" : "connected=0");
    if (connected) {
        return;
    }
    RequestGamepadCursorHidden(runtime, false);
    ResetSystemMenuState(runtime);
    runtime->context = GamepadInputContext::gameplay;
    runtime->previous_state = {};
    runtime->dpad_repeat = {};
    runtime->button_repeat = {};
    ReleaseGameplayHolds(runtime);
}

}  // namespace

namespace {

void TickGamepadInputCore() {
    auto& state = GetRuntimeState();
    auto& runtime = GetGamepadRuntime();

    if (!state.GamepadEnabled()) {
        RequestGamepadCursorHidden(&runtime, false);
        ReleaseGameplayHolds(&runtime);
        UpdateConnectionState(&runtime, false);
        state.SetGamepadContext(GamepadInputContext::gameplay);
        return;
    }
    if (!EnsureXInputLoaded(&runtime)) {
        UpdateConnectionState(&runtime, false);
        state.SetGamepadContext(GamepadInputContext::gameplay);
        return;
    }

    XINPUT_STATE current{};
    if (runtime.get_state(0, &current) != ERROR_SUCCESS) {
        UpdateConnectionState(&runtime, false);
        state.SetGamepadContext(GamepadInputContext::gameplay);
        return;
    }

    UpdateConnectionState(&runtime, true);
    if (HasGamepadInputActivity(
            current.Gamepad.wButtons,
            current.Gamepad.bLeftTrigger,
            current.Gamepad.bRightTrigger,
            current.Gamepad.sThumbLX,
            current.Gamepad.sThumbLY,
            current.Gamepad.sThumbRX,
            current.Gamepad.sThumbRY,
            XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE,
            XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE,
            kTriggerThreshold)) {
        runtime.last_gamepad_activity_tick = GetTickCount();
        RequestGamepadCursorHidden(&runtime, true);
    } else {
        ApplyGamepadCursorVisibility(&runtime);
    }
    const auto mapping = state.GetGamepadMapping();
    const DWORD now_ms = GetTickCount();
    const XINPUT_STATE previous = runtime.previous_state;

    ResolvePendingSystemMenuTransition(&runtime);
    SynchronizeSystemMenuShellVisibility(&runtime);
    UpdateMappedButtons(&runtime, current, previous, mapping, now_ms);
    const auto context = DetermineContext(runtime);
    UpdateDpadNavigation(&runtime, current, now_ms);
    UpdateGameplayStick(&runtime, current, context);

    if (context != runtime.context) {
        LogGamepadEvent(std::string("context=") + ToString(context));
        runtime.context = context;
    }
    state.SetGamepadContext(context);
    runtime.previous_state = current;
}

}  // namespace

void TickGamepadInput() {
    TickGamepadInputCore();
}

GamepadModernControlState GetGamepadModernControlState() noexcept {
    return GetGamepadRuntime().modern_controls;
}

void NotifyMouseInputActivity() noexcept {
    auto& runtime = GetGamepadRuntime();
    if (runtime.cursor_hide_requested &&
        GetTickCount() - runtime.last_gamepad_activity_tick <
            kGamepadToMouseDebounceMs) {
        return;
    }
    RequestGamepadCursorHidden(&runtime, false);
}

void ReassertGamepadCursorHidden() noexcept {
    auto& runtime = GetGamepadRuntime();
    if (runtime.cursor_hide_requested) {
        ApplyGamepadCursorVisibility(&runtime);
    }
}

bool ShouldSuppressNativeCursor() noexcept {
    const auto& runtime = GetGamepadRuntime();
    return runtime.cursor_hide_requested || runtime.native_cursor_suppressed;
}

}  // namespace pal4::inject
