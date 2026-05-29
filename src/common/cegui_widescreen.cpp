#include "pal4inject/cegui_widescreen.h"

#include <cmath>

namespace pal4::inject {
namespace {

constexpr float kMinimapLeftMargin = 2.0F;
constexpr float kMinimapBottomMargin = 28.0F;
constexpr float kMinimapSize = 173.0F;
constexpr float kPillarboxEpsilonPixels = 1.0F;

}  // namespace

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
    return BuildUiViewportPlan(width, height, UiProfile::centered_800x600);
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
    return PhysicalToUiLogical(plan, raw_x, raw_y, out_x, out_y);
}

bool ShouldDrawOriginalUiPillarboxMask(const CeguiWidescreenPlan& plan) noexcept {
    return plan.apply &&
        !plan.use_original_variant &&
        plan.uniform_scale > 0.0F &&
        plan.draw_pillarbox &&
        plan.horizontal_bias_pixels > kPillarboxEpsilonPixels;
}

}  // namespace pal4::inject
