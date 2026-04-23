#pragma once

#include <cstdint>

namespace pal4::inject {

enum class GamepadInputContext : std::uint8_t {
    gameplay = 0,
    system_menu,
    menu,
};

struct GamepadDigitalAxes {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
};

struct GamepadRepeatState {
    bool was_pressed = false;
    std::uint32_t next_repeat_ms = 0;
};

const char* ToString(GamepadInputContext context) noexcept;
bool TryParseGamepadInputContext(const char* text, GamepadInputContext* out) noexcept;

GamepadDigitalAxes BuildGamepadDigitalAxes(
    int x,
    int y,
    int deadzone) noexcept;
int WrapGamepadCycleIndex(int current, int delta, int item_count) noexcept;
bool ConsumeGamepadRepeat(
    bool pressed,
    std::uint32_t now_ms,
    std::uint32_t initial_delay_ms,
    std::uint32_t repeat_interval_ms,
    GamepadRepeatState* state) noexcept;

}  // namespace pal4::inject
