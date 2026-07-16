#include "pal4inject/aspect_ratio_layout.h"

#include <algorithm>
#include <cmath>

namespace pal4::inject {

AspectRatioRect ComputeAspectFitRect(
    const int container_width,
    const int container_height,
    const int content_width,
    const int content_height) noexcept {
    AspectRatioRect rect{};
    if (container_width <= 0 || container_height <= 0 ||
        content_width <= 0 || content_height <= 0) {
        return rect;
    }

    const double width_scale =
        static_cast<double>(container_width) / static_cast<double>(content_width);
    const double height_scale =
        static_cast<double>(container_height) / static_cast<double>(content_height);
    const double scale = std::min(width_scale, height_scale);
    if (!(scale > 0.0)) {
        return rect;
    }

    rect.width = std::max(
        1,
        static_cast<int>(std::lround(static_cast<double>(content_width) * scale)));
    rect.height = std::max(
        1,
        static_cast<int>(std::lround(static_cast<double>(content_height) * scale)));
    rect.width = std::min(rect.width, container_width);
    rect.height = std::min(rect.height, container_height);
    rect.x = (container_width - rect.width) / 2;
    rect.y = (container_height - rect.height) / 2;
    rect.valid = true;
    return rect;
}

AspectRatioRect ComputeAspectFillWidthRect(
    const int container_width,
    const int container_height,
    const int content_width,
    const int content_height) noexcept {
    AspectRatioRect rect{};
    if (container_width <= 0 || container_height <= 0 ||
        content_width <= 0 || content_height <= 0) {
        return rect;
    }

    const double scale =
        static_cast<double>(container_width) / static_cast<double>(content_width);
    if (!(scale > 0.0)) {
        return rect;
    }

    rect.width = container_width;
    rect.height = std::max(
        1,
        static_cast<int>(std::lround(static_cast<double>(content_height) * scale)));
    rect.x = 0;
    rect.y = (container_height - rect.height) / 2;
    rect.valid = true;
    return rect;
}

}  // namespace pal4::inject
