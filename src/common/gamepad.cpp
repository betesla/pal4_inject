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
    EnumName{GamepadAction::place_marker, std::string_view{"place_marker"}},
    EnumName{GamepadAction::maze_skill, std::string_view{"maze_skill"}},
    EnumName{GamepadAction::role_page, std::string_view{"role_page"}},
    EnumName{GamepadAction::item_page, std::string_view{"item_page"}},
    EnumName{GamepadAction::equipment_page, std::string_view{"equipment_page"}},
    EnumName{GamepadAction::magic_page, std::string_view{"magic_page"}},
    EnumName{GamepadAction::system_page, std::string_view{"system_page"}},
    EnumName{GamepadAction::combat_attack, std::string_view{"combat_attack"}},
    EnumName{GamepadAction::combat_defend, std::string_view{"combat_defend"}},
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
    SetGamepadBinding(&mapping, Xbox360Button::x, GamepadAction::place_marker);
    SetGamepadBinding(&mapping, Xbox360Button::y, GamepadAction::maze_skill);
    SetGamepadBinding(
        &mapping, Xbox360Button::left_shoulder, GamepadAction::main_page_previous);
    SetGamepadBinding(
        &mapping, Xbox360Button::right_shoulder, GamepadAction::main_page_next);
    SetGamepadBinding(
        &mapping, Xbox360Button::left_trigger, GamepadAction::sub_page_previous);
    SetGamepadBinding(
        &mapping, Xbox360Button::right_trigger, GamepadAction::sub_page_next);
    SetGamepadBinding(&mapping, Xbox360Button::back, GamepadAction::cancel);
    SetGamepadBinding(&mapping, Xbox360Button::start, GamepadAction::system_page);
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

GamepadAction ResolveGamepadActionForContext(
    const Xbox360Button button,
    const GamepadAction configured_action,
    const GamepadInputContext context,
    const bool combat_navigation_visible,
    const bool combat_action_wheel_visible) noexcept {
    if (context == GamepadInputContext::gameplay) {
        if (combat_action_wheel_visible) {
            if (button == Xbox360Button::x) {
                return GamepadAction::combat_attack;
            }
            if (button == Xbox360Button::y) {
                return GamepadAction::combat_defend;
            }
        }
        if (button == Xbox360Button::left_shoulder &&
            configured_action == GamepadAction::main_page_previous) {
            return GamepadAction::map;
        }
        if (button == Xbox360Button::b) {
            return combat_navigation_visible
                ? GamepadAction::cancel
                : GamepadAction::none;
        }
        if (button == Xbox360Button::back) {
            return GamepadAction::none;
        }
        return configured_action;
    }
    if (button == Xbox360Button::start &&
        configured_action == GamepadAction::system_page) {
        return GamepadAction::none;
    }
    return configured_action;
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

bool TryDeriveGamepadCameraOrbitFocus(
    const GamepadCameraVector3& position,
    const GamepadCameraVector3& forward,
    const float distance,
    GamepadCameraVector3* const out) noexcept {
    if (!out ||
        !std::isfinite(position.x) ||
        !std::isfinite(position.y) ||
        !std::isfinite(position.z) ||
        !std::isfinite(forward.x) ||
        !std::isfinite(forward.y) ||
        !std::isfinite(forward.z) ||
        !std::isfinite(distance) ||
        distance <= 0.0F) {
        return false;
    }

    const float forward_length = std::sqrt(
        forward.x * forward.x +
        forward.y * forward.y +
        forward.z * forward.z);
    if (!std::isfinite(forward_length) || forward_length <= 0.0001F) {
        return false;
    }

    const float scale = distance / forward_length;
    const GamepadCameraVector3 focus{
        position.x + forward.x * scale,
        position.y + forward.y * scale,
        position.z + forward.z * scale,
    };
    if (!std::isfinite(focus.x) ||
        !std::isfinite(focus.y) ||
        !std::isfinite(focus.z)) {
        return false;
    }
    *out = focus;
    return true;
}

bool ShouldApplyGamepadBattleCamera(
    const float right_stick_magnitude) noexcept {
    return std::isfinite(right_stick_magnitude) &&
        right_stick_magnitude > 0.0F;
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

int FindNextAvailableGamepadPage(
    const int current,
    const int delta,
    const bool* const available,
    const int item_count) noexcept {
    if (!available || item_count <= 0 || delta == 0) {
        return -1;
    }
    const int direction = delta < 0 ? -1 : 1;
    int candidate = WrapGamepadCycleIndex(current, 0, item_count);
    for (int checked = 0; checked < item_count; ++checked) {
        candidate = WrapGamepadCycleIndex(candidate, direction, item_count);
        if (available[candidate]) {
            return candidate;
        }
    }
    return -1;
}

GamepadAction SelectGameplayDpadAction(
    const GamepadDpadDirection direction) noexcept {
    switch (direction) {
    case GamepadDpadDirection::up:
        return GamepadAction::role_page;
    case GamepadDpadDirection::down:
        return GamepadAction::magic_page;
    case GamepadDpadDirection::left:
        return GamepadAction::item_page;
    case GamepadDpadDirection::right:
        return GamepadAction::equipment_page;
    }
    return GamepadAction::none;
}

bool ShouldDispatchGamepadCancel(
    const GamepadInputContext context) noexcept {
    return context != GamepadInputContext::gameplay;
}

GamepadDpadNavigationMode SelectGamepadDpadNavigationMode(
    const GamepadInputContext context,
    const bool system_menu_visible,
    const bool menu_navigation_visible,
    const bool combat_navigation_visible) noexcept {
    if (combat_navigation_visible) {
        return GamepadDpadNavigationMode::plain_ui;
    }
    if (context == GamepadInputContext::system_menu ||
        system_menu_visible) {
        return GamepadDpadNavigationMode::system_menu;
    }
    if (context == GamepadInputContext::menu ||
        menu_navigation_visible) {
        return GamepadDpadNavigationMode::plain_ui;
    }
    return GamepadDpadNavigationMode::gameplay_shortcuts;
}

GamepadCombatWheelSector SelectGamepadCombatWheelSector(
    const GamepadAnalogStick& stick,
    const GamepadCombatWheelSector previous) noexcept {
    constexpr float kEngageMagnitude = 0.45F;
    constexpr float kReleaseMagnitude = 0.25F;
    constexpr float kSectorHalfAngleDegrees = 36.0F;
    constexpr float kAngularHysteresisDegrees = 10.0F;
    constexpr float kRadiansToDegrees = 57.29577951308232F;

    if (stick.magnitude <= kReleaseMagnitude) {
        return GamepadCombatWheelSector::none;
    }
    if (previous == GamepadCombatWheelSector::none &&
        stick.magnitude < kEngageMagnitude) {
        return GamepadCombatWheelSector::none;
    }

    const float angle_degrees =
        std::atan2(stick.y, stick.x) * kRadiansToDegrees;
    const auto sector_center = [](const GamepadCombatWheelSector sector) {
        switch (sector) {
        case GamepadCombatWheelSector::magic:
            return 90.0F;
        case GamepadCombatWheelSector::stunt:
            return 18.0F;
        case GamepadCombatWheelSector::flee:
            return -54.0F;
        case GamepadCombatWheelSector::defend:
            return -126.0F;
        case GamepadCombatWheelSector::article:
            return 162.0F;
        case GamepadCombatWheelSector::none:
            return 0.0F;
        }
        return 0.0F;
    };
    if (previous != GamepadCombatWheelSector::none) {
        const float previous_distance = std::fabs(std::remainder(
            angle_degrees - sector_center(previous),
            360.0F));
        if (previous_distance <=
            kSectorHalfAngleDegrees + kAngularHysteresisDegrees) {
            return previous;
        }
    }

    if (angle_degrees >= 54.0F && angle_degrees < 126.0F) {
        return GamepadCombatWheelSector::magic;
    }
    if (angle_degrees >= -18.0F && angle_degrees < 54.0F) {
        return GamepadCombatWheelSector::stunt;
    }
    if (angle_degrees >= -90.0F && angle_degrees < -18.0F) {
        return GamepadCombatWheelSector::flee;
    }
    if (angle_degrees >= -162.0F && angle_degrees < -90.0F) {
        return GamepadCombatWheelSector::defend;
    }
    return GamepadCombatWheelSector::article;
}

GamepadCombatWheelNavigationPlan BuildGamepadCombatWheelNavigationPlan(
    const GamepadCombatWheelSector sector) noexcept {
    using Direction = GamepadDpadDirection;
    switch (sector) {
    case GamepadCombatWheelSector::magic:
        return {{{Direction::up, Direction::up, Direction::up}}, 3};
    case GamepadCombatWheelSector::stunt:
        return {{{Direction::right, Direction::right, Direction::right}}, 3};
    case GamepadCombatWheelSector::flee:
        return {{{Direction::right, Direction::right, Direction::right,
                  Direction::down, Direction::down}}, 5};
    case GamepadCombatWheelSector::defend:
        return {{{Direction::left, Direction::left, Direction::left,
                  Direction::down, Direction::down}}, 5};
    case GamepadCombatWheelSector::article:
        return {{{Direction::left, Direction::left, Direction::left}}, 3};
    case GamepadCombatWheelSector::none:
        // First converge on the top Magic node from any outer node, then move
        // down once along PAL4's original navigation graph to the centre
        // Attack node. This avoids depending on private CEGUI selection bytes.
        return {{{Direction::up, Direction::up, Direction::up,
                  Direction::down}}, 4};
    }
    return {};
}

bool ShouldCenterGamepadCombatWheel(
    const bool action_wheel_visible,
    const GamepadCombatWheelSector previous,
    const float stick_magnitude) noexcept {
    return action_wheel_visible &&
        previous != GamepadCombatWheelSector::none &&
        stick_magnitude <= 0.25F;
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

}  // namespace pal4::inject
