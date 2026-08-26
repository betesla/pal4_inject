#include "gamepad_runtime.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

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
using InputManagerGetKeyStateFn = SHORT (__thiscall*)(void*, int);
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
constexpr std::uint32_t kCombatUiQueryIntervalMs = 100;
constexpr std::uint8_t kSystemMenuOpenValidationDelayFrames = 4;
constexpr std::uint8_t kSystemMenuHiddenConfirmationFrames = 2;
constexpr int kKeyCodeA = 97;
constexpr int kKeyCodeD = 100;
constexpr int kKeyCodeS = 115;
constexpr int kKeyCodeW = 119;
constexpr int kKeyCodeSpace = 32;

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
    bool hold_space = false;
    bool raw_space_held = false;
    GamepadDpadNavigationMode dpad_navigation_mode =
        GamepadDpadNavigationMode::gameplay_shortcuts;
    DirectionRepeatState dpad_repeat{};
    DirectionRepeatState combat_stick_repeat{};
    GamepadCombatWheelSector combat_wheel_sector =
        GamepadCombatWheelSector::none;
    bool combat_analog_navigation_active = false;
    bool combat_action_wheel_visible = false;
    DWORD last_combat_ui_query_tick = 0;
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

void SetInjectedKeyboardKeyHeld(
    const bool pressed,
    const std::uint32_t virtual_key,
    bool* const held_flag) {
    if (!held_flag || *held_flag == pressed) {
        return;
    }
    if (!SendInjectedKeyboardInput(virtual_key, !pressed)) {
        GetRuntimeState().SetLastError(
            "gamepad failed to mirror a keyboard key state");
        return;
    }
    *held_flag = pressed;
    LogGamepadEvent(
        std::string("keyboard_mirror virtual_key=") +
        std::to_string(virtual_key) +
        " pressed=" + (pressed ? "1" : "0"));
}

bool UpdateGameplayKeyMirror(
    const bool pressed,
    const int key_code,
    bool* const was_pressed) {
    if (!was_pressed) {
        return false;
    }
    if (!pressed && !*was_pressed) {
        return true;
    }
    const auto get_input_manager =
        ResolveRuntimeFunction<GetInputManagerFn>(ida::kGetInputManager);
    const auto get_key_state =
        ResolveRuntimeFunction<InputManagerGetKeyStateFn>(ida::kInputManagerGetKeyState);
    const auto update_key_state =
        ResolveRuntimeFunction<UpdateKeyOrButtonStateFn>(ida::kUpdateKeyOrButtonState);
    if (!get_input_manager || !get_key_state || !update_key_state) {
        GetRuntimeState().SetLastError(
            "gamepad gameplay key-mirror helpers are unavailable");
        return false;
    }
    void* const input_manager = get_input_manager();
    if (!input_manager) {
        GetRuntimeState().SetLastError(
            "gamepad GetInputManager returned null for key mirror");
        return false;
    }

    const auto raw_state = static_cast<std::uint8_t>(
        get_key_state(input_manager, key_code));
    const auto plan = BuildGamepadKeyMirrorPlan(
        pressed,
        *was_pressed,
        raw_state);
    for (std::uint8_t index = 0; index < plan.press_updates; ++index) {
        update_key_state(input_manager, key_code, 1);
    }
    for (std::uint8_t index = 0; index < plan.release_updates; ++index) {
        update_key_state(input_manager, key_code, 0);
    }
    if (*was_pressed != pressed) {
        LogGamepadEvent(
            std::string("gameplay_key_mirror key_code=") +
            std::to_string(key_code) +
            " pressed=" +
            (pressed ? "1" : "0") +
            " raw_before=" + std::to_string(raw_state) +
            " press_updates=" + std::to_string(plan.press_updates) +
            " release_updates=" + std::to_string(plan.release_updates));
    }
    *was_pressed = pressed;
    return true;
}

bool TapUiSeamKey(const std::uint32_t virtual_key) {
    return SendUiSeamKey(virtual_key, false) &&
        SendUiSeamKey(virtual_key, true);
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
}

void ReleaseDirectKeyboardHolds(GamepadRuntime* const runtime) {
    if (!runtime) {
        return;
    }
    SetInjectedKeyboardKeyHeld(false, VK_SPACE, &runtime->hold_space);
    if (runtime->raw_space_held) {
        UpdateGameplayKeyMirror(
            false,
            kKeyCodeSpace,
            &runtime->raw_space_held);
    }
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

bool OpenSystemMenuPage(
    GamepadRuntime* const runtime,
    const std::uint32_t virtual_key) {
    if (!runtime) {
        return false;
    }
    const bool dispatched = TapInjectedKey(virtual_key);
    if (dispatched) {
        runtime->system_menu_active = true;
        if (virtual_key >= VK_F1 && virtual_key <= VK_F7) {
            runtime->current_main_page = virtual_key - VK_F1;
        }
        runtime->current_vertical_page = 0;
        runtime->system_menu_validation_delay_frames =
            kSystemMenuOpenValidationDelayFrames;
        runtime->system_menu_hidden_frames = 0;
    }
    ReleaseGameplayHolds(runtime);
    LogGamepadEvent(
        std::string("system_menu_page_key virtual_key=") +
        std::to_string(virtual_key) +
        " dispatched=" + (dispatched ? "1" : "0"));
    return dispatched;
}

void ResetSystemMenuState(GamepadRuntime* const runtime) {
    if (!runtime) {
        return;
    }
    runtime->system_menu_active = false;
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

void ExecuteImmediateAction(
    GamepadRuntime* const runtime,
    const GamepadAction action) {
    if (!runtime) {
        return;
    }
    switch (action) {
    case GamepadAction::confirm:
        // Confirm is a direct held Space-key mapping handled in
        // UpdateMappedButtons, so every original input path sees the same key.
        break;
    case GamepadAction::cancel:
        {
            GamepadNavigationUiState navigation_state{};
            std::string error;
            bool menu_navigation_visible = false;
            bool combat_navigation_visible = false;
            if (QueryGamepadNavigationUiState(&navigation_state, &error)) {
                runtime->system_menu_active = navigation_state.menu.visible;
                menu_navigation_visible =
                    navigation_state.menu.menu_navigation_visible;
                combat_navigation_visible =
                    navigation_state.combat_navigation_visible;
            } else if (!error.empty()) {
                GetRuntimeState().SetLastError(error);
            }
            const auto context = DetermineContext(*runtime);
            if (ShouldDispatchGamepadCancel(context) ||
                menu_navigation_visible ||
                combat_navigation_visible) {
                const bool dispatched = TapUiSeamKey(VK_ESCAPE);
                LogGamepadEvent(
                    std::string("cancel context=") + ToString(context) +
                    " combat=" +
                    (combat_navigation_visible ? "1" : "0") +
                    " dispatched=" + (dispatched ? "1" : "0"));
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
        TapInjectedKey('M');
        break;
    case GamepadAction::switch_leader:
        if (DetermineContext(*runtime) == GamepadInputContext::gameplay) {
            TapInjectedKey(VK_TAB);
        }
        break;
    case GamepadAction::camera_distance_cycle:
        if (DetermineContext(*runtime) == GamepadInputContext::gameplay &&
            !CycleGamepadCameraDistance()) {
            GetRuntimeState().SetLastError(
                "gamepad camera-distance control is unavailable");
        }
        break;
    case GamepadAction::system_menu:
        OpenSystemMenuPage(runtime, VK_F7);
        break;
    case GamepadAction::place_marker:
        if (DetermineContext(*runtime) == GamepadInputContext::gameplay) {
            TapInjectedKey('V');
        }
        break;
    case GamepadAction::maze_skill:
        if (DetermineContext(*runtime) == GamepadInputContext::gameplay) {
            TapInjectedKey('C');
        }
        break;
    case GamepadAction::role_page:
        OpenSystemMenuPage(runtime, VK_F1);
        break;
    case GamepadAction::item_page:
        OpenSystemMenuPage(runtime, VK_F2);
        break;
    case GamepadAction::equipment_page:
        OpenSystemMenuPage(runtime, VK_F3);
        break;
    case GamepadAction::magic_page:
        OpenSystemMenuPage(runtime, VK_F4);
        break;
    case GamepadAction::system_page:
        OpenSystemMenuPage(runtime, VK_F7);
        break;
    case GamepadAction::combat_attack:
    case GamepadAction::combat_defend:
        {
            std::string error;
            const bool attack = action == GamepadAction::combat_attack;
            const bool dispatched = TryActivateCombatActionButton(
                attack
                    ? CombatActionButton::attack
                    : CombatActionButton::defend,
                &error);
            if (!error.empty()) {
                GetRuntimeState().SetLastError(error);
            }
            LogGamepadEvent(
                std::string("combat_shortcut action=") +
                (attack ? "attack" : "defend") +
                " dispatched=" + (dispatched ? "1" : "0"));
        }
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

    SystemMenuNavigationState navigation{};
    std::string error;
    if (!QuerySystemMenuNavigationState(&navigation, &error)) {
        if (!error.empty()) {
            GetRuntimeState().SetLastError(error);
        }
        LogGamepadEvent(
            std::string("page_action action=") + ToString(action) +
            " skipped=navigation_snapshot_unavailable");
        return;
    }
    if (navigation.has_current_main_page &&
        runtime->current_main_page != navigation.current_main_page) {
        runtime->current_main_page = navigation.current_main_page;
        runtime->current_vertical_page = 0;
    }

    if (action == GamepadAction::main_page_previous ||
        action == GamepadAction::main_page_next) {
        const int delta = action == GamepadAction::main_page_previous ? -1 : 1;
        const int next_page = FindNextAvailableGamepadPage(
            static_cast<int>(runtime->current_main_page),
            delta,
            navigation.main_pages.data(),
            kMainPageCount);
        if (next_page < 0 ||
            next_page == static_cast<int>(runtime->current_main_page)) {
            LogGamepadEvent(
                std::string("page_action action=") + ToString(action) +
                " skipped=no_other_available_main_page");
            return;
        }
        const bool dispatched = TapUiSeamKey(VK_F1 + next_page);
        if (dispatched) {
            runtime->current_main_page =
                static_cast<std::uint32_t>(next_page);
            runtime->current_vertical_page = 0;
        }
        LogGamepadEvent(
            std::string("page_action action=") + ToString(action) +
            " main_page=" + std::to_string(next_page) +
            " dispatched=" + (dispatched ? "1" : "0"));
        return;
    }

    if (!navigation.has_current_main_page) {
        LogGamepadEvent(
            std::string("page_action action=") + ToString(action) +
            " skipped=no_active_main_page");
        return;
    }
    const int delta = action == GamepadAction::sub_page_previous ? -1 : 1;
    const int next_page = FindNextAvailableGamepadPage(
        static_cast<int>(runtime->current_vertical_page),
        delta,
        navigation.sub_pages.data(),
        kVerticalPageCount);
    if (next_page < 0 ||
        next_page == static_cast<int>(runtime->current_vertical_page)) {
        LogGamepadEvent(
            std::string("page_action action=") + ToString(action) +
            " skipped=no_other_available_sub_page");
        return;
    }
    const bool dispatched = TapUiSeamKey('1' + next_page);
    if (dispatched) {
        runtime->current_vertical_page =
            static_cast<std::uint32_t>(next_page);
    }
    LogGamepadEvent(
        std::string("page_action action=") + ToString(action) +
        " sub_page=" + std::to_string(next_page) +
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
    std::array<bool, kXbox360ButtonCount> pressed_buttons{};
    for (std::size_t index = 0; index < kXbox360ButtonCount; ++index) {
        pressed_buttons[index] = IsButtonPressed(
            current,
            static_cast<Xbox360Button>(index));
    }
    const bool space_held = IsMappedGamepadActionPressed(
        mapping,
        GamepadAction::confirm,
        pressed_buttons);
    GamepadInputContext input_context = DetermineContext(*runtime);
    constexpr std::array context_sensitive_buttons{
        Xbox360Button::b,
        Xbox360Button::x,
        Xbox360Button::y,
        Xbox360Button::back,
        Xbox360Button::start,
        Xbox360Button::left_shoulder,
        Xbox360Button::right_shoulder,
        Xbox360Button::left_trigger,
        Xbox360Button::right_trigger,
    };
    bool context_sensitive_button_just_pressed = false;
    for (const auto button : context_sensitive_buttons) {
        if (IsButtonJustPressed(current, previous, button)) {
            context_sensitive_button_just_pressed = true;
            break;
        }
    }
    bool combat_navigation_visible = false;
    bool combat_action_wheel_visible = false;
    if (context_sensitive_button_just_pressed) {
        GamepadNavigationUiState navigation_state{};
        std::string error;
        if (QueryGamepadNavigationUiState(&navigation_state, &error)) {
            combat_navigation_visible =
                navigation_state.combat_navigation_visible;
            combat_action_wheel_visible =
                navigation_state.combat_action_wheel_visible;
            if (navigation_state.menu.visible) {
                runtime->system_menu_active = true;
                input_context = GamepadInputContext::system_menu;
            } else if (navigation_state.menu.menu_navigation_visible) {
                input_context = GamepadInputContext::menu;
            }
        } else if (!error.empty()) {
            GetRuntimeState().SetLastError(error);
        }
    }
    for (std::size_t index = 0; index < kXbox360ButtonCount; ++index) {
        const auto button = static_cast<Xbox360Button>(index);
        const auto action = ResolveGamepadActionForContext(
            button,
            GetGamepadBinding(mapping, button),
            input_context,
            combat_navigation_visible,
            combat_action_wheel_visible);
        const bool pressed = pressed_buttons[index];
        if (action == GamepadAction::confirm) {
            runtime->button_repeat[index] = {};
            continue;
        }
        if (action == GamepadAction::mouse_left) {
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
    SetInjectedKeyboardKeyHeld(space_held, VK_SPACE, &runtime->hold_space);
    UpdateGameplayKeyMirror(
        space_held,
        kKeyCodeSpace,
        &runtime->raw_space_held);
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

void DispatchRepeatedRoleSwitchOrDpad(
    const bool pressed,
    GamepadRepeatState* const repeat,
    const bool next,
    const std::uint32_t fallback_virtual_key,
    const DWORD now_ms) {
    if (!ConsumeGamepadRepeat(
            pressed,
            now_ms,
            kRepeatInitialDelayMs,
            kRepeatIntervalMs,
            repeat)) {
        return;
    }

    std::string error;
    if (TryActivateSystemMenuRoleSwitch(next, &error)) {
        LogGamepadEvent(std::string("role_switch direction=") +
            (next ? "next" : "previous") + " dispatched=1");
        return;
    }
    if (!error.empty()) {
        GetRuntimeState().SetLastError(error);
    }
    TapUiSeamKey(fallback_virtual_key);
}

std::uint32_t VirtualKeyForDpadDirection(
    const GamepadDpadDirection direction) noexcept {
    switch (direction) {
    case GamepadDpadDirection::up:
        return VK_UP;
    case GamepadDpadDirection::down:
        return VK_DOWN;
    case GamepadDpadDirection::left:
        return VK_LEFT;
    case GamepadDpadDirection::right:
        return VK_RIGHT;
    }
    return 0;
}

void DispatchCombatWheelSelection(
    GamepadRuntime* const runtime,
    const GamepadAnalogStick& stick) {
    if (!runtime) {
        return;
    }
    const auto sector = SelectGamepadCombatWheelSector(
        stick,
        runtime->combat_wheel_sector);
    if (sector == runtime->combat_wheel_sector) {
        return;
    }
    runtime->combat_wheel_sector = sector;
    const auto plan = BuildGamepadCombatWheelNavigationPlan(sector);
    for (std::size_t index = 0; index < plan.count; ++index) {
        const auto virtual_key = VirtualKeyForDpadDirection(
            plan.directions[index]);
        if (virtual_key != 0) {
            TapUiSeamKey(virtual_key);
        }
    }
    LogGamepadEvent(
        std::string("combat_wheel sector=") +
        std::to_string(static_cast<unsigned int>(sector)) +
        " steps=" + std::to_string(plan.count));
}

void DispatchCombatStickNavigation(
    GamepadRuntime* const runtime,
    const GamepadAnalogStick& stick,
    const DWORD now_ms) {
    if (!runtime) {
        return;
    }
    constexpr float kNavigationMagnitude = 0.35F;
    const bool active = stick.magnitude >= kNavigationMagnitude;
    const bool horizontal = std::fabs(stick.x) > std::fabs(stick.y);
    const bool up = active && !horizontal && stick.y > 0.0F;
    const bool down = active && !horizontal && stick.y < 0.0F;
    const bool left = active && horizontal && stick.x < 0.0F;
    const bool right = active && horizontal && stick.x > 0.0F;
    DispatchRepeatedDpad(
        up, &runtime->combat_stick_repeat.up, VK_UP, now_ms);
    DispatchRepeatedDpad(
        down, &runtime->combat_stick_repeat.down, VK_DOWN, now_ms);
    DispatchRepeatedDpad(
        left, &runtime->combat_stick_repeat.left, VK_LEFT, now_ms);
    DispatchRepeatedDpad(
        right, &runtime->combat_stick_repeat.right, VK_RIGHT, now_ms);
}

void RefreshCombatAnalogUiState(
    GamepadRuntime* const runtime,
    const DWORD now_ms) {
    if (!runtime) {
        return;
    }
    CombatNavigationUiState ui_state{};
    std::string error;
    if (QueryCombatNavigationUiState(&ui_state, &error)) {
        const bool wheel_changed =
            runtime->combat_action_wheel_visible !=
            ui_state.action_wheel_visible;
        runtime->combat_analog_navigation_active = ui_state.visible;
        runtime->combat_action_wheel_visible =
            ui_state.action_wheel_visible;
        if (wheel_changed || !ui_state.visible) {
            runtime->combat_wheel_sector =
                GamepadCombatWheelSector::none;
            runtime->combat_stick_repeat = {};
        }
    } else if (!error.empty()) {
        GetRuntimeState().SetLastError(error);
    }
    runtime->last_combat_ui_query_tick = now_ms;
}

void UpdateDpadNavigation(
    GamepadRuntime* const runtime,
    const XINPUT_STATE& current,
    const XINPUT_STATE& previous,
    const GamepadInputContext context,
    const DWORD now_ms) {
    if (!runtime) {
        return;
    }
    constexpr WORD kDpadMask =
        XINPUT_GAMEPAD_DPAD_UP |
        XINPUT_GAMEPAD_DPAD_DOWN |
        XINPUT_GAMEPAD_DPAD_LEFT |
        XINPUT_GAMEPAD_DPAD_RIGHT;
    const bool any_pressed =
        (current.Gamepad.wButtons & kDpadMask) != 0;
    const bool any_was_pressed =
        (previous.Gamepad.wButtons & kDpadMask) != 0;
    if (!any_pressed) {
        runtime->dpad_navigation_mode =
            GamepadDpadNavigationMode::gameplay_shortcuts;
    } else if (!any_was_pressed) {
        bool system_menu_visible = false;
        bool menu_navigation_visible = false;
        bool combat_navigation_visible = false;
        if (context == GamepadInputContext::gameplay) {
            GamepadNavigationUiState ui_state{};
            std::string error;
            if (QueryGamepadNavigationUiState(&ui_state, &error)) {
                system_menu_visible = ui_state.menu.visible;
                menu_navigation_visible =
                    ui_state.menu.menu_navigation_visible;
                combat_navigation_visible =
                    ui_state.combat_navigation_visible;
                if (system_menu_visible) {
                    runtime->system_menu_active = true;
                }
            } else if (!error.empty()) {
                GetRuntimeState().SetLastError(error);
            }
        }
        runtime->dpad_navigation_mode =
            SelectGamepadDpadNavigationMode(
                context,
                system_menu_visible,
                menu_navigation_visible,
                combat_navigation_visible);
        const char* mode_name = "gameplay";
        if (runtime->dpad_navigation_mode ==
            GamepadDpadNavigationMode::plain_ui) {
            mode_name = "ui";
        } else if (runtime->dpad_navigation_mode ==
                   GamepadDpadNavigationMode::system_menu) {
            mode_name = "system_menu";
        }
        LogGamepadEvent(
            std::string("dpad_navigation mode=") +
            mode_name +
            " context=" + ToString(context) +
            " visible_menu=" +
            ((system_menu_visible || menu_navigation_visible) ? "1" : "0") +
            " visible_combat=" +
            (combat_navigation_visible ? "1" : "0"));
    }
    if (runtime->dpad_navigation_mode ==
        GamepadDpadNavigationMode::gameplay_shortcuts) {
        constexpr std::array shortcuts{
            std::pair{XINPUT_GAMEPAD_DPAD_UP, GamepadDpadDirection::up},
            std::pair{XINPUT_GAMEPAD_DPAD_DOWN, GamepadDpadDirection::down},
            std::pair{XINPUT_GAMEPAD_DPAD_LEFT, GamepadDpadDirection::left},
            std::pair{XINPUT_GAMEPAD_DPAD_RIGHT, GamepadDpadDirection::right},
        };
        std::array<GamepadRepeatState*, 4> repeat_states{
            &runtime->dpad_repeat.up,
            &runtime->dpad_repeat.down,
            &runtime->dpad_repeat.left,
            &runtime->dpad_repeat.right,
        };
        for (std::size_t index = 0; index < shortcuts.size(); ++index) {
            const auto [mask, direction] = shortcuts[index];
            const bool pressed = (current.Gamepad.wButtons & mask) != 0;
            const bool was_pressed = (previous.Gamepad.wButtons & mask) != 0;
            auto* const repeat = repeat_states[index];
            if (!pressed) {
                *repeat = {};
                continue;
            }
            repeat->was_pressed = true;
            repeat->next_repeat_ms = now_ms + kRepeatInitialDelayMs;
            if (!was_pressed) {
                ExecuteImmediateAction(
                    runtime,
                    SelectGameplayDpadAction(direction));
            }
        }
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
    if (runtime->dpad_navigation_mode ==
        GamepadDpadNavigationMode::system_menu) {
        DispatchRepeatedRoleSwitchOrDpad(
            (current.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0,
            &runtime->dpad_repeat.left,
            false,
            VK_LEFT,
            now_ms);
        DispatchRepeatedRoleSwitchOrDpad(
            (current.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0,
            &runtime->dpad_repeat.right,
            true,
            VK_RIGHT,
            now_ms);
        return;
    }
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

void UpdateAnalogSticks(
    GamepadRuntime* const runtime,
    const XINPUT_STATE& current,
    const GamepadInputContext context,
    const DWORD now_ms) {
    if (!runtime || context != GamepadInputContext::gameplay) {
        ReleaseGameplayHolds(runtime);
        return;
    }

    const auto left_stick = BuildGamepadAnalogStick(
        current.Gamepad.sThumbLX,
        current.Gamepad.sThumbLY,
        XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    const auto right_stick = BuildGamepadAnalogStick(
        current.Gamepad.sThumbRX,
        current.Gamepad.sThumbRY,
        XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    const bool analog_active =
        left_stick.magnitude > 0.0F || right_stick.magnitude > 0.0F;
    if ((analog_active || runtime->combat_analog_navigation_active) &&
        (runtime->last_combat_ui_query_tick == 0 ||
         now_ms - runtime->last_combat_ui_query_tick >=
             kCombatUiQueryIntervalMs)) {
        RefreshCombatAnalogUiState(runtime, now_ms);
    }
    if (!analog_active) {
        if (ShouldCenterGamepadCombatWheel(
                runtime->combat_action_wheel_visible,
                runtime->combat_wheel_sector,
                left_stick.magnitude)) {
            DispatchCombatWheelSelection(runtime, left_stick);
        }
        if (runtime->combat_analog_navigation_active) {
            ReleaseGameplayHolds(runtime);
            runtime->modern_controls = {};
            runtime->combat_stick_repeat = {};
            return;
        }
        runtime->combat_action_wheel_visible = false;
        runtime->combat_wheel_sector = GamepadCombatWheelSector::none;
        runtime->combat_stick_repeat = {};
        runtime->last_combat_ui_query_tick = 0;
    } else if (runtime->combat_analog_navigation_active) {
        ReleaseGameplayHolds(runtime);
        runtime->modern_controls = {};
        if (runtime->combat_action_wheel_visible) {
            runtime->combat_stick_repeat = {};
            DispatchCombatWheelSelection(runtime, left_stick);
        } else {
            runtime->combat_wheel_sector =
                GamepadCombatWheelSector::none;
            DispatchCombatStickNavigation(runtime, left_stick, now_ms);
        }
        return;
    }

    if (GetRuntimeState().GamepadModernControls()) {
        runtime->hold_w = false;
        runtime->hold_a = false;
        runtime->hold_s = false;
        runtime->hold_d = false;
        runtime->modern_controls.movement = left_stick;
        runtime->modern_controls.camera = right_stick;
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
    runtime->dpad_navigation_mode =
        GamepadDpadNavigationMode::gameplay_shortcuts;
    runtime->dpad_repeat = {};
    runtime->combat_stick_repeat = {};
    runtime->combat_wheel_sector = GamepadCombatWheelSector::none;
    runtime->combat_analog_navigation_active = false;
    runtime->combat_action_wheel_visible = false;
    runtime->last_combat_ui_query_tick = 0;
    runtime->button_repeat = {};
    ReleaseDirectKeyboardHolds(runtime);
    ReleaseGameplayHolds(runtime);
}

}  // namespace

namespace {

void TickGamepadInputCore() {
    auto& state = GetRuntimeState();
    auto& runtime = GetGamepadRuntime();

    if (!state.GamepadEnabled()) {
        RequestGamepadCursorHidden(&runtime, false);
        ReleaseDirectKeyboardHolds(&runtime);
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

    SynchronizeSystemMenuShellVisibility(&runtime);
    UpdateMappedButtons(&runtime, current, previous, mapping, now_ms);
    auto context = DetermineContext(runtime);
    UpdateDpadNavigation(&runtime, current, previous, context, now_ms);
    context = DetermineContext(runtime);
    UpdateAnalogSticks(&runtime, current, context, now_ms);

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
