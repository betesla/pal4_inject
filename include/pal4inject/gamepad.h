#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pal4::inject {

enum class GamepadInputContext : std::uint8_t {
    gameplay = 0,
    system_menu,
    menu,
};

enum class Xbox360Button : std::uint8_t {
    a = 0,
    b,
    x,
    y,
    left_shoulder,
    right_shoulder,
    left_trigger,
    right_trigger,
    back,
    start,
    left_thumb,
    right_thumb,
    count,
};

enum class GamepadAction : std::uint8_t {
    none = 0,
    confirm,
    cancel,
    mouse_left,
    run_toggle,
    auto_forward,
    map,
    switch_leader,
    camera_distance_cycle,
    system_menu,
    main_page_previous,
    main_page_next,
    sub_page_previous,
    sub_page_next,
    place_marker,
    maze_skill,
    role_page,
    item_page,
    equipment_page,
    magic_page,
    system_page,
    combat_attack,
    combat_defend,
};

enum class GamepadDpadDirection : std::uint8_t {
    up = 0,
    down,
    left,
    right,
};

enum class GamepadDpadNavigationMode : std::uint8_t {
    gameplay_shortcuts = 0,
    plain_ui,
    system_menu,
};

enum class GamepadCombatWheelSector : std::uint8_t {
    none = 0,
    magic,
    stunt,
    flee,
    defend,
    article,
};

inline constexpr std::size_t kXbox360ButtonCount =
    static_cast<std::size_t>(Xbox360Button::count);

struct Xbox360GamepadMapping {
    std::array<GamepadAction, kXbox360ButtonCount> bindings{};

    bool operator==(const Xbox360GamepadMapping&) const noexcept = default;
};

struct GamepadDigitalAxes {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
};

struct GamepadAnalogStick {
    float x = 0.0F;
    float y = 0.0F;
    float magnitude = 0.0F;
};

struct GamepadMovementTuning {
    int mode = 0;
    float speed_multiplier = 0.4F;
    float animation_multiplier = 1.0F;
};

struct GamepadTurnTuning {
    float direction_x = 0.0F;
    float direction_z = 1.0F;
    float remaining_angle_degrees = 0.0F;
    bool use_walk_animation = false;
};

struct GamepadRepeatState {
    bool was_pressed = false;
    std::uint32_t next_repeat_ms = 0;
};

struct GamepadKeyMirrorPlan {
    std::uint8_t press_updates = 0;
    std::uint8_t release_updates = 0;
};

struct GamepadCombatWheelNavigationPlan {
    std::array<GamepadDpadDirection, 5> directions{};
    std::size_t count = 0;
};

enum class GamepadCursorPresentation : std::uint8_t {
    hide_all = 0,
    cegui_only,
    native_only,
};

const char* ToString(GamepadInputContext context) noexcept;
const char* ToString(Xbox360Button button) noexcept;
const char* ToString(GamepadAction action) noexcept;
bool TryParseXbox360Button(std::string_view text, Xbox360Button* out) noexcept;
bool TryParseGamepadAction(std::string_view text, GamepadAction* out) noexcept;

Xbox360GamepadMapping DefaultXbox360GamepadMapping() noexcept;
GamepadAction GetGamepadBinding(
    const Xbox360GamepadMapping& mapping,
    Xbox360Button button) noexcept;
GamepadAction ResolveGamepadActionForContext(
    Xbox360Button button,
    GamepadAction configured_action,
    GamepadInputContext context,
    bool combat_navigation_visible = false,
    bool combat_action_wheel_visible = false) noexcept;
bool IsMappedGamepadActionPressed(
    const Xbox360GamepadMapping& mapping,
    GamepadAction action,
    const std::array<bool, kXbox360ButtonCount>& pressed_buttons) noexcept;
GamepadKeyMirrorPlan BuildGamepadKeyMirrorPlan(
    bool pressed,
    bool was_pressed,
    std::uint8_t raw_key_state) noexcept;
void SetGamepadBinding(
    Xbox360GamepadMapping* mapping,
    Xbox360Button button,
    GamepadAction action) noexcept;

GamepadDigitalAxes BuildGamepadDigitalAxes(int x, int y, int deadzone) noexcept;
GamepadAnalogStick BuildGamepadAnalogStick(int x, int y, int deadzone) noexcept;
bool HasGamepadInputActivity(
    std::uint16_t buttons,
    std::uint8_t left_trigger,
    std::uint8_t right_trigger,
    int left_x,
    int left_y,
    int right_x,
    int right_y,
    int left_deadzone,
    int right_deadzone,
    std::uint8_t trigger_threshold) noexcept;
int SelectGamepadMovementMode(
    float magnitude,
    float run_threshold,
    float fast_run_threshold) noexcept;
GamepadMovementTuning BuildGamepadMovementTuning(
    float magnitude,
    float run_threshold,
    float fast_run_threshold) noexcept;
GamepadTurnTuning BuildGamepadTurnTuning(
    float current_yaw_degrees,
    float target_direction_x,
    float target_direction_z,
    float delta_seconds,
    float turn_speed_degrees_per_second,
    float walk_animation_angle_degrees) noexcept;
float SelectNextGamepadCameraDistance(
    float current,
    float mode_default) noexcept;
int WrapGamepadCycleIndex(int current, int delta, int item_count) noexcept;
int FindNextAvailableGamepadPage(
    int current,
    int delta,
    const bool* available,
    int item_count) noexcept;
GamepadAction SelectGameplayDpadAction(
    GamepadDpadDirection direction) noexcept;
bool ShouldDispatchGamepadCancel(GamepadInputContext context) noexcept;
GamepadDpadNavigationMode SelectGamepadDpadNavigationMode(
    GamepadInputContext context,
    bool system_menu_visible,
    bool menu_navigation_visible,
    bool combat_navigation_visible) noexcept;
GamepadCombatWheelSector SelectGamepadCombatWheelSector(
    const GamepadAnalogStick& stick,
    GamepadCombatWheelSector previous) noexcept;
GamepadCombatWheelNavigationPlan BuildGamepadCombatWheelNavigationPlan(
    GamepadCombatWheelSector sector) noexcept;
bool ShouldCenterGamepadCombatWheel(
    bool action_wheel_visible,
    GamepadCombatWheelSector previous,
    float stick_magnitude) noexcept;
bool ConsumeGamepadRepeat(
    bool pressed,
    std::uint32_t now_ms,
    std::uint32_t initial_delay_ms,
    std::uint32_t repeat_interval_ms,
    GamepadRepeatState* state) noexcept;
GamepadCursorPresentation SelectGamepadCursorPresentation(
    bool gamepad_owns_input,
    bool cegui_cursor_visible) noexcept;
}  // namespace pal4::inject
