#include "pal4inject/widescreen_ui_layout.h"

namespace pal4::inject {

WidescreenUiWindowPlan BuildWidescreenUiWindowPlan(
    const CeguiWidescreenPlan& plan,
    const WidescreenUiHorizontalMode mode,
    const float original_x,
    const float original_width,
    const float horizontal_padding_factor,
    const float original_y,
    const float widescreen_y_offset) noexcept {
    WidescreenUiWindowPlan result{};
    result.x = original_x;
    result.y = original_y;
    result.width = original_width;

    if (widescreen_y_offset != 0.0F) {
        result.y += widescreen_y_offset;
        result.set_y = true;
    }

    switch (mode) {
        case WidescreenUiHorizontalMode::left_edge:
            result.x = ComputeWidescreenHudLogicalX(
                plan,
                original_x,
                WidescreenHudAnchor::left_edge);
            result.set_x = true;
            break;
        case WidescreenUiHorizontalMode::right_edge:
            result.x = ComputeWidescreenHudLogicalX(
                plan,
                original_x,
                WidescreenHudAnchor::right_edge);
            result.set_x = true;
            break;
        case WidescreenUiHorizontalMode::stretch_between_edges:
            result.x = ComputeWidescreenHudLogicalX(
                plan,
                original_x,
                WidescreenHudAnchor::left_edge);
            result.width = ComputeWidescreenEdgeToEdgeLogicalWidth(plan, original_width);
            result.set_x = true;
            result.set_width = true;
            break;
        case WidescreenUiHorizontalMode::offset_by_padding_factor:
            result.x += plan.logical_horizontal_padding * horizontal_padding_factor;
            result.set_x = true;
            break;
        case WidescreenUiHorizontalMode::preserve:
        default:
            break;
    }
    return result;
}

}  // namespace pal4::inject
