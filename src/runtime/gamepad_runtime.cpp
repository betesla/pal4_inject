#include "gamepad_runtime.h"

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>

#include "input_hooks.h"
#include "pal4inject/gamepad.h"
#include "runtime_state.h"
#include "ui_snapshot_runtime.h"

namespace pal4::inject {
namespace {

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

constexpr std::array<const char*, 3> kXInputDlls{
    "xinput1_4.dll",
    "xinput9_1_0.dll",
    "xinput1_3.dll",
};
constexpr std::uint16_t kTriggerThreshold = 128;
constexpr int kVerticalPageCount = 6;
constexpr std::uint32_t kRepeatInitialDelayMs = 300;
constexpr std::uint32_t kRepeatIntervalMs = 110;

struct RuntimeButtonState {
    GamepadRepeatState up{};
    GamepadRepeatState down{};
    GamepadRepeatState left{};
    GamepadRepeatState right{};
    GamepadRepeatState lb{};
    GamepadRepeatState rb{};
    GamepadRepeatState lt{};
    GamepadRepeatState rt{};
};

struct GamepadRuntime {
    HMODULE xinput_module = nullptr;
    XInputGetStateFn get_state = nullptr;
    bool load_attempted = false;
    bool connected = false;
    bool system_menu_active = false;
    std::uint32_t current_main_page = 0;
    std::uint32_t current_vertical_page = 0;
    DWORD last_packet_number = 0;
    XINPUT_STATE previous_state{};
    bool hold_w = false;
    bool hold_a = false;
    bool hold_s = false;
    bool hold_d = false;
    bool hold_mouse_left = false;
    bool x_button_holding_mouse_left = false;
    RuntimeButtonState repeat{};
};

GamepadRuntime& GetGamepadRuntime() {
    static GamepadRuntime runtime;
    return runtime;
}

void LogGamepadEvent(const std::string_view text) {
    auto& state = GetRuntimeState();
    if (!state.GamepadLogEnabled()) {
        return;
    }
    state.AppendEventLog(std::string("gamepad:") + std::string(text));
}

bool EnsureXInputLoaded(GamepadRuntime* runtime) {
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
    for (const auto* dll_name : kXInputDlls) {
        HMODULE module = LoadLibraryA(dll_name);
        if (!module) {
            continue;
        }
        auto* get_state = reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState"));
        if (!get_state) {
            FreeLibrary(module);
            continue;
        }
        runtime->xinput_module = module;
        runtime->get_state = get_state;
        LogGamepadEvent(std::string("xinput_loaded=") + dll_name);
        return true;
    }
    return false;
}

bool SendPostedKey(
    const std::uint32_t virtual_key,
    const bool key_up) {
    std::string error;
    const bool ok = DispatchSimulatedKey(virtual_key, key_up, false, &error);
    if (!ok && !error.empty()) {
        GetRuntimeState().SetLastError(error);
    }
    return ok;
}

bool SendUiSeamKey(
    const std::uint32_t virtual_key,
    const bool key_up) {
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

bool TapInjectedKey(const std::uint32_t virtual_key) {
    return SendInjectedKeyboardInput(virtual_key, false) &&
        SendInjectedKeyboardInput(virtual_key, true);
}

bool TapPostedKey(const std::uint32_t virtual_key) {
    return SendPostedKey(virtual_key, false) &&
        SendPostedKey(virtual_key, true);
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

void SetMouseLeftHeld(const bool pressed, GamepadRuntime* runtime) {
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
    const std::uint32_t virtual_key,
    bool* held_flag) {
    if (!held_flag || *held_flag == pressed) {
        return;
    }
    if (SendInjectedKeyboardInput(virtual_key, !pressed)) {
        *held_flag = pressed;
    }
}

void ReleaseGameplayHolds(GamepadRuntime* runtime) {
    if (!runtime) {
        return;
    }
    SetHeldKey(false, 'W', &runtime->hold_w);
    SetHeldKey(false, 'A', &runtime->hold_a);
    SetHeldKey(false, 'S', &runtime->hold_s);
    SetHeldKey(false, 'D', &runtime->hold_d);
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

bool ButtonPressed(const XINPUT_STATE& state, const WORD mask) {
    return (state.Gamepad.wButtons & mask) != 0;
}

bool ButtonJustPressed(const XINPUT_STATE& current, const XINPUT_STATE& previous, const WORD mask) {
    return ButtonPressed(current, mask) && !ButtonPressed(previous, mask);
}

bool TriggerPressed(const BYTE value) {
    return value >= kTriggerThreshold;
}

void DispatchRepeatedDigitalButton(
    const bool pressed,
    GamepadRepeatState* repeat,
    const std::uint32_t virtual_key,
    const DWORD now_ms) {
    if (ConsumeGamepadRepeat(pressed, now_ms, kRepeatInitialDelayMs, kRepeatIntervalMs, repeat)) {
        TapUiSeamKey(virtual_key);
    }
}

void UpdateMenuStateFromShoulders(
    GamepadRuntime* runtime,
    const XINPUT_STATE& current,
    const DWORD now_ms) {
    if (!runtime || !runtime->system_menu_active) {
        return;
    }
    if (ConsumeGamepadRepeat(
            ButtonPressed(current, XINPUT_GAMEPAD_LEFT_SHOULDER),
            now_ms,
            kRepeatInitialDelayMs,
            kRepeatIntervalMs,
            &runtime->repeat.lb)) {
        runtime->current_main_page = static_cast<std::uint32_t>(
            WrapGamepadCycleIndex(static_cast<int>(runtime->current_main_page), -1, 7));
        TapUiSeamKey(VK_F1 + runtime->current_main_page);
    }
    if (ConsumeGamepadRepeat(
            ButtonPressed(current, XINPUT_GAMEPAD_RIGHT_SHOULDER),
            now_ms,
            kRepeatInitialDelayMs,
            kRepeatIntervalMs,
            &runtime->repeat.rb)) {
        runtime->current_main_page = static_cast<std::uint32_t>(
            WrapGamepadCycleIndex(static_cast<int>(runtime->current_main_page), 1, 7));
        TapUiSeamKey(VK_F1 + runtime->current_main_page);
    }
    if (ConsumeGamepadRepeat(
            TriggerPressed(current.Gamepad.bLeftTrigger),
            now_ms,
            kRepeatInitialDelayMs,
            kRepeatIntervalMs,
            &runtime->repeat.lt)) {
        runtime->current_vertical_page = static_cast<std::uint32_t>(
            WrapGamepadCycleIndex(static_cast<int>(runtime->current_vertical_page), -1, kVerticalPageCount));
        TapUiSeamKey('1' + runtime->current_vertical_page);
    }
    if (ConsumeGamepadRepeat(
            TriggerPressed(current.Gamepad.bRightTrigger),
            now_ms,
            kRepeatInitialDelayMs,
            kRepeatIntervalMs,
            &runtime->repeat.rt)) {
        runtime->current_vertical_page = static_cast<std::uint32_t>(
            WrapGamepadCycleIndex(static_cast<int>(runtime->current_vertical_page), 1, kVerticalPageCount));
        TapUiSeamKey('1' + runtime->current_vertical_page);
    }
}

void UpdateDpadNavigation(
    GamepadRuntime* runtime,
    const XINPUT_STATE& current,
    const DWORD now_ms) {
    if (!runtime) {
        return;
    }
    DispatchRepeatedDigitalButton(
        ButtonPressed(current, XINPUT_GAMEPAD_DPAD_UP),
        &runtime->repeat.up,
        VK_UP,
        now_ms);
    DispatchRepeatedDigitalButton(
        ButtonPressed(current, XINPUT_GAMEPAD_DPAD_DOWN),
        &runtime->repeat.down,
        VK_DOWN,
        now_ms);
    DispatchRepeatedDigitalButton(
        ButtonPressed(current, XINPUT_GAMEPAD_DPAD_LEFT),
        &runtime->repeat.left,
        VK_LEFT,
        now_ms);
    DispatchRepeatedDigitalButton(
        ButtonPressed(current, XINPUT_GAMEPAD_DPAD_RIGHT),
        &runtime->repeat.right,
        VK_RIGHT,
        now_ms);
}

void UpdateGameplayStick(
    GamepadRuntime* runtime,
    const XINPUT_STATE& current) {
    if (!runtime || runtime->system_menu_active) {
        ReleaseGameplayHolds(runtime);
        return;
    }
    const auto axes = BuildGamepadDigitalAxes(
        current.Gamepad.sThumbLX,
        current.Gamepad.sThumbLY,
        XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    SetHeldKey(false, 'W', &runtime->hold_w);
    SetHeldKey(axes.left, 'A', &runtime->hold_a);
    SetHeldKey(axes.down, 'S', &runtime->hold_s);
    SetHeldKey(axes.right, 'D', &runtime->hold_d);
    SetMouseLeftHeld(axes.up || runtime->x_button_holding_mouse_left, runtime);
}

void UpdateFaceButtons(
    GamepadRuntime* runtime,
    const XINPUT_STATE& current,
    const XINPUT_STATE& previous) {
    if (!runtime) {
        return;
    }
    auto click_menu_close = [&]() {
        std::string error;
        if (!ClickLikelySystemMenuCloseButton(&error) && !error.empty()) {
            GetRuntimeState().SetLastError(error);
        }
    };
    if (ButtonJustPressed(current, previous, XINPUT_GAMEPAD_START)) {
        if (runtime->system_menu_active) {
            click_menu_close();
            runtime->system_menu_active = false;
            ReleaseGameplayHolds(runtime);
        } else {
            runtime->system_menu_active = true;
            runtime->current_main_page = 0;
            runtime->current_vertical_page = 0;
            ReleaseGameplayHolds(runtime);
            TapUiSeamKey(VK_F1);
        }
    }

    if (ButtonJustPressed(current, previous, XINPUT_GAMEPAD_BACK)) {
        if (runtime->system_menu_active) {
            TapUiSeamKey('M');
        } else {
            TapInjectedKey('M');
        }
    }
    if (ButtonJustPressed(current, previous, XINPUT_GAMEPAD_Y)) {
        TapInjectedKey('R');
    }
    if (ButtonJustPressed(current, previous, XINPUT_GAMEPAD_LEFT_THUMB)) {
        TapInjectedKey('F');
    }
    if (ButtonJustPressed(current, previous, XINPUT_GAMEPAD_A)) {
        if (runtime->system_menu_active) {
            TapUiSeamKey(VK_RETURN);
        } else {
            TapInjectedKey(VK_SPACE);
        }
    }
    if (ButtonJustPressed(current, previous, XINPUT_GAMEPAD_B)) {
        if (runtime->system_menu_active) {
            click_menu_close();
            runtime->system_menu_active = false;
        } else {
            TapInjectedKey(VK_ESCAPE);
        }
        ReleaseGameplayHolds(runtime);
    }

    const bool x_pressed = ButtonPressed(current, XINPUT_GAMEPAD_X) && !runtime->system_menu_active;
    runtime->x_button_holding_mouse_left = x_pressed;
    if (runtime->system_menu_active) {
        SetMouseLeftHeld(false, runtime);
    } else {
        const auto axes = BuildGamepadDigitalAxes(
            current.Gamepad.sThumbLX,
            current.Gamepad.sThumbLY,
            XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        SetMouseLeftHeld(axes.up || x_pressed, runtime);
    }
}

void UpdateConnectionState(GamepadRuntime* runtime, const bool connected) {
    if (!runtime) {
        return;
    }
    if (runtime->connected == connected) {
        return;
    }
    runtime->connected = connected;
    GetRuntimeState().SetGamepadConnected(connected);
    LogGamepadEvent(connected ? "connected=1" : "connected=0");
    if (!connected) {
        runtime->system_menu_active = false;
        runtime->current_main_page = 0;
        runtime->current_vertical_page = 0;
        runtime->previous_state = {};
        runtime->last_packet_number = 0;
        runtime->repeat = {};
        runtime->x_button_holding_mouse_left = false;
        ReleaseGameplayHolds(runtime);
    }
}

}  // namespace

void TickGamepadInput() {
    auto& state = GetRuntimeState();
    auto& runtime = GetGamepadRuntime();

    if (!state.GamepadEnabled()) {
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
    const DWORD result = runtime.get_state(0, &current);
    if (result != ERROR_SUCCESS) {
        UpdateConnectionState(&runtime, false);
        state.SetGamepadContext(GamepadInputContext::gameplay);
        return;
    }

    UpdateConnectionState(&runtime, true);
    const DWORD now_ms = GetTickCount();
    const XINPUT_STATE previous = runtime.previous_state;

    UpdateFaceButtons(&runtime, current, previous);
    UpdateDpadNavigation(&runtime, current, now_ms);
    UpdateMenuStateFromShoulders(&runtime, current, now_ms);
    UpdateGameplayStick(&runtime, current);

    const auto context = DetermineContext(runtime);
    state.SetGamepadContext(context);
    runtime.previous_state = current;
    runtime.last_packet_number = current.dwPacketNumber;
}

}  // namespace pal4::inject
