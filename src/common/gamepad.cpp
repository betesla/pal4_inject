#include "pal4inject/gamepad.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pal4::inject {
namespace {

template <typename Enum>
struct EnumName {
    Enum value;
    std::string_view name;
};

constexpr std::array kContexts{
    EnumName{GamepadInputContext::gameplay, std::string_view{"gameplay"}},
    EnumName{GamepadInputContext::system_menu, std::string_view{"system_menu"}},
    EnumName{GamepadInputContext::menu, std::string_view{"menu"}},
};

constexpr std::array kButtons{
    EnumName{Xbox360Button::a, std::string_view{"a"}},
    EnumName{Xbox360Button::b, std::string_view{"b"}},
    EnumName{Xbox360Button::x, std::string_view{"x"}},
    EnumName{Xbox360Button::y, std::string_view{"y"}},
    EnumName{Xbox360Button::left_shoulder, std::string_view{"left_shoulder"}},
    EnumName{Xbox360Button::right_shoulder, std::string_view{"right_shoulder"}},
    EnumName{Xbox360Button::left_trigger, std::string_view{"left_trigger"}},
    EnumName{Xbox360Button::right_trigger, std::string_view{"right_trigger"}},
    EnumName{Xbox360Button::back, std::string_view{"back"}},
    EnumName{Xbox360Button::start, std::string_view{"start"}},
    EnumName{Xbox360Button::left_thumb, std::string_view{"left_thumb"}},
    EnumName{Xbox360Button::right_thumb, std::string_view{"right_thumb"}},
};

constexpr std::array kActions{
    EnumName{GamepadAction::none, std::string_view{"none"}},
    EnumName{GamepadAction::confirm, std::string_view{"confirm"}},
    EnumName{GamepadAction::cancel, std::string_view{"cancel"}},
    EnumName{GamepadAction::mouse_left, std::string_view{"mouse_left"}},
    EnumName{GamepadAction::run_toggle, std::string_view{"run_toggle"}},
    EnumName{GamepadAction::auto_forward, std::string_view{"auto_forward"}},
    EnumName{GamepadAction::map, std::string_view{"map"}},
    EnumName{GamepadAction::switch_leader, std::string_view{"switch_leader"}},
    EnumName{GamepadAction::camera_distance_cycle, std::string_view{"camera_distance_cycle"}},
    EnumName{GamepadAction::system_menu, std::string_view{"system_menu"}},
    EnumName{GamepadAction::main_page_previous, std::string_view{"main_page_previous"}},
    EnumName{GamepadAction::main_page_next, std::string_view{"main_page_next"}},
    EnumName{GamepadAction::sub_page_previous, std::string_view{"sub_page_previous"}},
    EnumName{GamepadAction::sub_page_next, std::string_view{"sub_page_next"}},
};

template <typename Enum, std::size_t N>
const char* EnumToString(
    const Enum value,
    const std::array<EnumName<Enum>, N>& names,
    const char* const fallback) noexcept {
    for (const auto& entry : names) {
        if (entry.value == value) {
            return entry.name.data();
        }
    }
    return fallback;
}

template <typename Enum, std::size_t N>
bool TryParseEnum(
    const std::string_view text,
    const std::array<EnumName<Enum>, N>& names,
    Enum* const out) noexcept {
    if (!out) {
        return false;
    }
    for (const auto& entry : names) {
        if (entry.name == text) {
            *out = entry.value;
            return true;
        }
    }
    return false;
}

std::size_t ButtonIndex(const Xbox360Button button) noexcept {
    return static_cast<std::size_t>(button);
}

}  // namespace

const char* ToString(const GamepadInputContext context) noexcept {
    return EnumToString(context, kContexts, "gameplay");
}

const char* ToString(const Xbox360Button button) noexcept {
    return EnumToString(button, kButtons, "unknown");
}

const char* ToString(const GamepadAction action) noexcept {
    return EnumToString(action, kActions, "none");
}

bool TryParseXbox360Button(
    const std::string_view text,
    Xbox360Button* const out) noexcept {
    return TryParseEnum(text, kButtons, out);
}

bool TryParseGamepadAction(
    const std::string_view text,
    GamepadAction* const out) noexcept {
    return TryParseEnum(text, kActions, out);
}

Xbox360GamepadMapping DefaultXbox360GamepadMapping() noexcept {
    Xbox360GamepadMapping mapping{};
    SetGamepadBinding(&mapping, Xbox360Button::a, GamepadAction::confirm);
    SetGamepadBinding(&mapping, Xbox360Button::b, GamepadAction::cancel);
    SetGamepadBinding(&mapping, Xbox360Button::x, GamepadAction::mouse_left);
    SetGamepadBinding(&mapping, Xbox360Button::y, GamepadAction::run_toggle);
    SetGamepadBinding(
        &mapping, Xbox360Button::left_shoulder, GamepadAction::main_page_previous);
    SetGamepadBinding(
        &mapping, Xbox360Button::right_shoulder, GamepadAction::main_page_next);
    SetGamepadBinding(
        &mapping, Xbox360Button::left_trigger, GamepadAction::sub_page_previous);
    SetGamepadBinding(
        &mapping, Xbox360Button::right_trigger, GamepadAction::sub_page_next);
    SetGamepadBinding(&mapping, Xbox360Button::back, GamepadAction::map);
    SetGamepadBinding(&mapping, Xbox360Button::start, GamepadAction::system_menu);
    SetGamepadBinding(&mapping, Xbox360Button::left_thumb, GamepadAction::switch_leader);
    SetGamepadBinding(
        &mapping,
        Xbox360Button::right_thumb,
        GamepadAction::camera_distance_cycle);
    return mapping;
}

GamepadAction GetGamepadBinding(
    const Xbox360GamepadMapping& mapping,
    const Xbox360Button button) noexcept {
    const auto index = ButtonIndex(button);
    return index < mapping.bindings.size()
        ? mapping.bindings[index]
        : GamepadAction::none;
}

void SetGamepadBinding(
    Xbox360GamepadMapping* const mapping,
    const Xbox360Button button,
    const GamepadAction action) noexcept {
    if (!mapping) {
        return;
    }
    const auto index = ButtonIndex(button);
    if (index < mapping->bindings.size()) {
        mapping->bindings[index] = action;
    }
}

GamepadDigitalAxes BuildGamepadDigitalAxes(
    const int x,
    const int y,
    const int deadzone) noexcept {
    GamepadDigitalAxes axes{};
    if (y >= deadzone) {
        axes.up = true;
    } else if (y <= -deadzone) {
        axes.down = true;
    }
    if (x >= deadzone) {
        axes.right = true;
    } else if (x <= -deadzone) {
        axes.left = true;
    }
    return axes;
}

GamepadAnalogStick BuildGamepadAnalogStick(
    const int x,
    const int y,
    const int deadzone) noexcept {
    constexpr float kStickMaximum = 32767.0F;
    const float normalized_x = std::clamp(
        static_cast<float>(x) / kStickMaximum, -1.0F, 1.0F);
    const float normalized_y = std::clamp(
        static_cast<float>(y) / kStickMaximum, -1.0F, 1.0F);
    const float raw_length =
        std::sqrt(normalized_x * normalized_x + normalized_y * normalized_y);
    const float raw_magnitude = std::min(raw_length, 1.0F);
    const float normalized_deadzone = std::clamp(
        static_cast<float>(std::max(deadzone, 0)) / kStickMaximum,
        0.0F,
        0.99F);
    if (raw_magnitude <= normalized_deadzone || raw_magnitude <= 0.0F) {
        return {};
    }

    const float direction_x = normalized_x / raw_length;
    const float direction_y = normalized_y / raw_length;
    const float magnitude = std::clamp(
        (raw_magnitude - normalized_deadzone) / (1.0F - normalized_deadzone),
        0.0F,
        1.0F);
    return {direction_x * magnitude, direction_y * magnitude, magnitude};
}

bool HasGamepadInputActivity(
    const std::uint16_t buttons,
    const std::uint8_t left_trigger,
    const std::uint8_t right_trigger,
    const int left_x,
    const int left_y,
    const int right_x,
    const int right_y,
    const int left_deadzone,
    const int right_deadzone,
    const std::uint8_t trigger_threshold) noexcept {
    if (buttons != 0 || left_trigger >= trigger_threshold ||
        right_trigger >= trigger_threshold) {
        return true;
    }
    return BuildGamepadAnalogStick(left_x, left_y, left_deadzone).magnitude > 0.0F ||
        BuildGamepadAnalogStick(right_x, right_y, right_deadzone).magnitude > 0.0F;
}

int SelectGamepadMovementMode(
    const float magnitude,
    const float run_threshold,
    const float fast_run_threshold) noexcept {
    const float normalized_run = std::clamp(run_threshold, 0.05F, 0.95F);
    const float normalized_fast = std::clamp(
        fast_run_threshold,
        normalized_run + 0.01F,
        1.0F);
    if (magnitude >= normalized_fast) {
        return 2;
    }
    return magnitude >= normalized_run ? 1 : 0;
}

float SelectNextGamepadCameraDistance(
    const float current,
    const float mode_default) noexcept {
    if (!std::isfinite(current) || !std::isfinite(mode_default) ||
        mode_default <= 0.0F) {
        return current;
    }
    const float closest_distance = mode_default * 0.2F;
    const float close_distance = mode_default * 0.5F;
    const float far_distance = mode_default * 1.5F;
    const float tolerance = mode_default * 0.05F;
    if (current < close_distance - tolerance) {
        return close_distance;
    }
    if (current < mode_default - tolerance) {
        return mode_default;
    }
    if (current < far_distance - tolerance) {
        return far_distance;
    }
    return closest_distance;
}

int WrapGamepadCycleIndex(
    const int current,
    const int delta,
    const int item_count) noexcept {
    if (item_count <= 0) {
        return 0;
    }
    int next = (current + delta) % item_count;
    if (next < 0) {
        next += item_count;
    }
    return next;
}

bool ConsumeGamepadRepeat(
    const bool pressed,
    const std::uint32_t now_ms,
    const std::uint32_t initial_delay_ms,
    const std::uint32_t repeat_interval_ms,
    GamepadRepeatState* const state) noexcept {
    if (!state) {
        return false;
    }
    if (!pressed) {
        state->was_pressed = false;
        state->next_repeat_ms = 0;
        return false;
    }
    if (!state->was_pressed) {
        state->was_pressed = true;
        state->next_repeat_ms = now_ms + initial_delay_ms;
        return true;
    }
    if (static_cast<std::int32_t>(now_ms - state->next_repeat_ms) < 0) {
        return false;
    }
    state->next_repeat_ms = now_ms + repeat_interval_ms;
    return true;
}

GamepadCursorPresentation SelectGamepadCursorPresentation(
    const bool gamepad_owns_input,
    const bool cegui_cursor_visible) noexcept {
    if (gamepad_owns_input) {
        return GamepadCursorPresentation::hide_all;
    }
    return cegui_cursor_visible
        ? GamepadCursorPresentation::cegui_only
        : GamepadCursorPresentation::native_only;
}

SystemMenuCancelAction SelectSystemMenuCancelAction(
    const bool menu_visible,
    const bool page_visible) noexcept {
    if (!menu_visible) {
        return SystemMenuCancelAction::none;
    }
    return page_visible
        ? SystemMenuCancelAction::escape_nested
        : SystemMenuCancelAction::close_root;
}

}  // namespace pal4::inject
