#pragma once

#include <cstdint>

namespace pal4::inject {

enum class UiInjectedAction : std::uint8_t {
    fallback_to_original = 0,
    return_false,
    key_down,
    key_up,
    char_input,
    mouse_move,
    mouse_button_down,
    mouse_button_up,
    mouse_wheel,
    renderer_size_changed,
    renderer_size_changed_and_redraw,
    disable_mouse_capture,
};

struct UiInjectedPlan {
    UiInjectedAction action = UiInjectedAction::fallback_to_original;
    std::uint32_t code = 0;
    float wheel_delta = 0.0F;
};

std::uint32_t NormalizeProcessUiEventKeyDown(std::uint32_t mapped_key) noexcept;
std::uint32_t NormalizeProcessUiEventKeyUp(std::uint32_t mapped_key) noexcept;
bool ShouldSuppressMappedUiKey(std::uint32_t mapped_key) noexcept;
UiInjectedPlan BuildUiInjectedPlan(
    std::uint32_t message,
    std::uint32_t mapped_key,
    std::uint32_t wparam) noexcept;
const char* ToString(UiInjectedAction action) noexcept;
const char* DescribeWindowsMessage(std::uint32_t message) noexcept;

}  // namespace pal4::inject
