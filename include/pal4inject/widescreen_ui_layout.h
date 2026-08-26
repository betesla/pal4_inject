#pragma once

#include "pal4inject/cegui_widescreen.h"

namespace pal4::inject {

enum class WidescreenUiHorizontalMode {
    preserve,
    left_edge,
    right_edge,
    stretch_between_edges,
    offset_by_padding_factor,
};

struct WidescreenUiWindowPlan {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    bool set_x = false;
    bool set_y = false;
    bool set_width = false;
};

WidescreenUiWindowPlan BuildWidescreenUiWindowPlan(
    const CeguiWidescreenPlan& plan,
    WidescreenUiHorizontalMode mode,
    float original_x,
    float original_width,
    float horizontal_padding_factor = 0.0F,
    float original_y = 0.0F,
    float widescreen_y_offset = 0.0F) noexcept;

}  // namespace pal4::inject
