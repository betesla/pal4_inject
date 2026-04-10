#include "pal4inject/cegui_widescreen.h"

#include <cmath>

namespace pal4::inject {
namespace {

constexpr float kLogicalUiWidth = 800.0F;
constexpr float kLogicalUiHeight = 600.0F;
constexpr float kAspect43 = 4.0F / 3.0F;
constexpr float kMinimapLeftMargin = 2.0F;
constexpr float kMinimapBottomMargin = 28.0F;
constexpr float kMinimapSize = 173.0F;

}  // namespace

bool IsWideAspectResolution(const int width, const int height) noexcept {
    if (width <= 0 || height <= 0) {
        return false;
    }
    return static_cast<float>(width) / static_cast<float>(height) > kAspect43;
}

bool UsesOriginalWideRendererVariant(const int width, const int height) noexcept {
    return width == 1280 && height == 800;
}

float ComputeWidescreenHudLogicalX(
    const CeguiWidescreenPlan& plan,
    const float base_logical_x,
    const WidescreenHudAnchor anchor) noexcept {
    if (!plan.apply || plan.use_original_variant) {
        return base_logical_x;
    }

    switch (anchor) {
    case WidescreenHudAnchor::left_edge:
        return base_logical_x - plan.logical_horizontal_padding;
    case WidescreenHudAnchor::right_edge:
        return base_logical_x + plan.logical_horizontal_padding;
    case WidescreenHudAnchor::none:
    default:
        return base_logical_x;
    }
}

float ComputeCenteredUiLogicalX(
    const CeguiWidescreenPlan& plan,
    const float base_logical_x) noexcept {
    if (!plan.apply || plan.use_original_variant) {
        return base_logical_x;
    }
    return base_logical_x + plan.logical_horizontal_padding;
}

float ProjectWidescreenLogicalXToPhysicalPixels(
    const CeguiWidescreenPlan& plan,
    const float logical_x) noexcept {
    if (plan.uniform_scale <= 0.0F) {
        return logical_x;
    }

    const float horizontal_bias =
        (plan.apply && !plan.use_original_variant) ? plan.horizontal_bias_pixels : 0.0F;
    return logical_x * plan.uniform_scale + horizontal_bias;
}

CeguiWidescreenPlan BuildCeguiWidescreenPlan(const int width, const int height) noexcept {
    CeguiWidescreenPlan plan{};
    plan.width = width;
    plan.height = height;
    plan.logical_width = kLogicalUiWidth;
    plan.logical_height = kLogicalUiHeight;
    plan.use_original_variant = UsesOriginalWideRendererVariant(width, height);

    if (!IsWideAspectResolution(width, height)) {
        return plan;
    }

    plan.apply = true;
    plan.uniform_scale = static_cast<float>(height) / kLogicalUiHeight;
    const float scaled_width = kLogicalUiWidth * plan.uniform_scale;
    if (static_cast<float>(width) > scaled_width) {
        plan.horizontal_bias_pixels =
            (static_cast<float>(width) - scaled_width) * 0.5F;
        if (plan.uniform_scale > 0.0F) {
            plan.logical_horizontal_padding =
                plan.horizontal_bias_pixels / plan.uniform_scale;
        }
    }
    return plan;
}

WidescreenMinimapPlacement BuildWidescreenMinimapPlacement(
    const int width,
    const int height) noexcept {
    WidescreenMinimapPlacement placement{};
    const auto plan = BuildCeguiWidescreenPlan(width, height);
    if (!plan.apply || plan.use_original_variant || plan.uniform_scale <= 0.0F) {
        return placement;
    }

    placement.apply = true;
    placement.width = static_cast<int>(std::lround(kMinimapSize * plan.uniform_scale));
    placement.height = placement.width;
    const float logical_x = ComputeWidescreenHudLogicalX(
        plan,
        kMinimapLeftMargin,
        WidescreenHudAnchor::left_edge);
    placement.x = static_cast<int>(std::lround(
        ProjectWidescreenLogicalXToPhysicalPixels(plan, logical_x)));
    placement.y = static_cast<int>(
        std::lround(
            static_cast<float>(height) -
            kMinimapBottomMargin * plan.uniform_scale -
            static_cast<float>(placement.height)));
    return placement;
}

bool ApplyCeguiWidescreenMouseTransform(
    const CeguiWidescreenPlan& plan,
    const float raw_x,
    const float raw_y,
    float* const out_x,
    float* const out_y) noexcept {
    if (!out_x || !out_y) {
        return false;
    }
    if (!plan.apply || plan.use_original_variant || plan.uniform_scale <= 0.0F) {
        return false;
    }

    *out_x = (raw_x - plan.horizontal_bias_pixels) / plan.uniform_scale;
    *out_y = raw_y / plan.uniform_scale;
    return true;
}

}  // namespace pal4::inject
