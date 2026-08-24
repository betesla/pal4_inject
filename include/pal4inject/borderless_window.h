#pragma once

#include <cstdint>

namespace pal4::inject {

struct BorderlessWindowPlan {
    std::uint32_t style = 0;
    std::uint32_t extended_style = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

BorderlessWindowPlan BuildBorderlessWindowPlan(
    std::uint32_t current_style,
    std::uint32_t current_extended_style,
    int monitor_left,
    int monitor_top,
    int monitor_right,
    int monitor_bottom) noexcept;
int ResolveBorderlessPresentFullscreenFlag(
    int requested_fullscreen,
    bool borderless_requested,
    bool replacement_enabled) noexcept;

}  // namespace pal4::inject
