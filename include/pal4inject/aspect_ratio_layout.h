#pragma once

namespace pal4::inject {

struct AspectRatioRect {
    bool valid = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

AspectRatioRect ComputeAspectFitRect(
    int container_width,
    int container_height,
    int content_width,
    int content_height) noexcept;

AspectRatioRect ComputeAspectFillWidthRect(
    int container_width,
    int container_height,
    int content_width,
    int content_height) noexcept;

}  // namespace pal4::inject
