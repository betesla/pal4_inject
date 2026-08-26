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

bool IsMappedGamepadActionPressed(
    const Xbox360GamepadMapping& mapping,
    const GamepadAction action,
    const std::array<bool, kXbox360ButtonCount>& pressed_buttons) noexcept {
    if (action == GamepadAction::none) {
        return false;
    }
    for (std::size_t index = 0; index < kXbox360ButtonCount; ++index) {
        if (pressed_buttons[index] && mapping.bindings[index] == action) {
            return true;
        }
    }
    return false;
}

GamepadKeyMirrorPlan BuildGamepadKeyMirrorPlan(
    const bool pressed,
    const bool was_pressed,
    const std::uint8_t raw_key_state) noexcept {
    GamepadKeyMirrorPlan plan{};
    if (!pressed) {
        if (was_pressed && (raw_key_state == 2 || raw_key_state == 3)) {
            plan.release_updates = 1;
        }
        return plan;
    }

    if (!was_pressed) {
        // Preserve PAL4's one-frame "just pressed" state (2). Advancing it
        // twice here would skip directly to the held state (3), which breaks
        // one-shot gameplay interaction checks.
        if (raw_key_state < 2) {
            plan.press_updates = 1;
        }
        return plan;
    }

    // The native DirectInput poll runs before the gamepad mirror and normally
    // advances a synthetic held key to released (1). Restore held (3) without
    // losing the original transition semantics.
    if (raw_key_state == 0 || raw_key_state == 1) {
        plan.press_updates = 2;
    } else if (raw_key_state == 2) {
        plan.press_updates = 1;
    }
    return plan;
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
    return BuildGamepadMovementTuning(
        magnitude,
        run_threshold,
        fast_run_threshold).mode;
}

GamepadMovementTuning BuildGamepadMovementTuning(
    const float magnitude,
    const float run_threshold,
    const float fast_run_threshold) noexcept {
    const float normalized_run = std::clamp(run_threshold, 0.05F, 0.95F);
    const float normalized_fast = std::clamp(
        fast_run_threshold,
        normalized_run + 0.01F,
        1.0F);
    if (magnitude >= normalized_fast) {
        return {2, 1.5F, 1.4F};
    }
    if (magnitude < normalized_run) {
        return {0, 0.4F, 1.0F};
    }

    // PAL4's stock modes jump directly from run (1.0x movement/animation)
    // to fast run (1.5x movement, 1.4x animation). Preserve both endpoints,
    // but let an analog stick blend continuously between the configured
    // run and fast-run thresholds.
    const float blend = std::clamp(
        (magnitude - normalized_run) / (normalized_fast - normalized_run),
        0.0F,
        1.0F);
    return {
        1,
        1.0F + 0.5F * blend,
        1.0F + 0.4F * blend,
    };
}

GamepadTurnTuning BuildGamepadTurnTuning(
    const float current_yaw_degrees,
    const float target_direction_x,
    const float target_direction_z,
    const float delta_seconds,
    const float turn_speed_degrees_per_second,
    const float walk_animation_angle_degrees) noexcept {
    const float target_length = std::sqrt(
        target_direction_x * target_direction_x +
        target_direction_z * target_direction_z);
    if (!std::isfinite(current_yaw_degrees) ||
        !std::isfinite(target_length) || target_length <= 0.0001F) {
        return {target_direction_x, target_direction_z, 0.0F, false};
    }

    constexpr float kRadiansToDegrees = 57.29577951308232F;
    constexpr float kDegreesToRadians = 0.017453292519943295F;
    const float normalized_target_x = target_direction_x / target_length;
    const float normalized_target_z = target_direction_z / target_length;
    const float target_yaw =
        std::atan2(normalized_target_x, normalized_target_z) * kRadiansToDegrees;
    float angle_delta = std::fmod(
        target_yaw - current_yaw_degrees + 540.0F,
        360.0F) - 180.0F;
    if (!std::isfinite(angle_delta)) {
        return {normalized_target_x, normalized_target_z, 0.0F, false};
    }

    const float safe_delta_seconds = std::clamp(delta_seconds, 0.0F, 0.1F);
    const float safe_turn_speed = std::max(turn_speed_degrees_per_second, 0.0F);
    const float maximum_turn = safe_turn_speed * safe_delta_seconds;
    const float applied_turn = std::clamp(angle_delta, -maximum_turn, maximum_turn);
    const float smoothed_yaw = current_yaw_degrees + applied_turn;
    const float smoothed_yaw_radians = smoothed_yaw * kDegreesToRadians;
    const float walking_threshold = std::max(walk_animation_angle_degrees, 0.0F);
    return {
        std::sin(smoothed_yaw_radians),
        std::cos(smoothed_yaw_radians),
        std::fabs(angle_delta),
        std::fabs(angle_delta) > walking_threshold,
    };
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
