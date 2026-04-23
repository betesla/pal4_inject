#include "pal4inject/gamepad.h"

#include <array>
#include <string_view>

namespace pal4::inject {
namespace {

template <typename Enum>
struct EnumName {
    Enum value;
    const char* name;
};

constexpr std::array<EnumName<GamepadInputContext>, 3> kContexts{{
    {GamepadInputContext::gameplay, "gameplay"},
    {GamepadInputContext::system_menu, "system_menu"},
    {GamepadInputContext::menu, "menu"},
}};

}  // namespace

const char* ToString(const GamepadInputContext context) noexcept {
    for (const auto& entry : kContexts) {
        if (entry.value == context) {
            return entry.name;
        }
    }
    return "gameplay";
}

bool TryParseGamepadInputContext(const char* text, GamepadInputContext* out) noexcept {
    if (!text || !out) {
        return false;
    }
    for (const auto& entry : kContexts) {
        if (std::string_view(text) == entry.name) {
            *out = entry.value;
            return true;
        }
    }
    return false;
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

int WrapGamepadCycleIndex(const int current, const int delta, const int item_count) noexcept {
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
    GamepadRepeatState* state) noexcept {
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
    if (now_ms < state->next_repeat_ms) {
        return false;
    }
    state->next_repeat_ms = now_ms + repeat_interval_ms;
    return true;
}

}  // namespace pal4::inject
