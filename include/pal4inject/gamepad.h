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

struct GamepadRepeatState {
    bool was_pressed = false;
    std::uint32_t next_repeat_ms = 0;
};

enum class GamepadCursorPresentation : std::uint8_t {
    hide_all = 0,
    cegui_only,
    native_only,
};

enum class SystemMenuCancelAction : std::uint8_t {
    none = 0,
    close_root,
    escape_nested,
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
float SelectNextGamepadCameraDistance(
    float current,
    float mode_default) noexcept;
int WrapGamepadCycleIndex(int current, int delta, int item_count) noexcept;
bool ConsumeGamepadRepeat(
    bool pressed,
    std::uint32_t now_ms,
    std::uint32_t initial_delay_ms,
    std::uint32_t repeat_interval_ms,
    GamepadRepeatState* state) noexcept;
GamepadCursorPresentation SelectGamepadCursorPresentation(
    bool gamepad_owns_input,
    bool cegui_cursor_visible) noexcept;
SystemMenuCancelAction SelectSystemMenuCancelAction(
    bool menu_visible,
    bool page_visible) noexcept;
}  // namespace pal4::inject
