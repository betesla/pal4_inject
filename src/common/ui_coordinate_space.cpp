#include "pal4inject/ui_coordinate_space.h"

#include <algorithm>

namespace pal4::inject {
namespace {

constexpr float kAspect43 = 4.0F / 3.0F;
constexpr float kBattleOverlayMaxScale = 1080.0F / 600.0F;

}  // namespace

const char* ToString(const UiProfile profile) noexcept {
    switch (profile) {
    case UiProfile::centered_800x600:
        return "centered_800x600";
    case UiProfile::widescreen_1067x600:
        return "widescreen_1067x600";
    }
    return "unknown";
}

bool IsWideAspectResolution(const int width, const int height) noexcept {
    if (width <= 0 || height <= 0) {
        return false;
    }
    return static_cast<float>(width) / static_cast<float>(height) > kAspect43;
}

bool UsesOriginalWideRendererVariant(const int width, const int height) noexcept {
    return width == 1280 && height == 800;
}

UiProfile GetDefaultUiProfile() noexcept {
    return UiProfile::centered_800x600;
}

std::pair<float, float> GetUiProfileLogicalSize(const UiProfile profile) noexcept {
    switch (profile) {
    case UiProfile::centered_800x600:
        return {800.0F, 600.0F};
    case UiProfile::widescreen_1067x600:
        return {1067.0F, 600.0F};
    }
    return {800.0F, 600.0F};
}

UiViewportPlan BuildUiViewportPlan(
    const int width,
    const int height,
    const UiProfile profile) noexcept {
    UiViewportPlan plan{};
    plan.profile = profile;
    plan.width = width;
    plan.height = height;
    const auto logical_size = GetUiProfileLogicalSize(profile);
    plan.logical_width = logical_size.first;
    plan.logical_height = logical_size.second;
    plan.physical_width = logical_size.first;
    plan.physical_height = logical_size.second;
    plan.render_rect_right = logical_size.first;
    plan.render_rect_bottom = logical_size.second;
    plan.use_original_variant =
        profile == UiProfile::centered_800x600 &&
        UsesOriginalWideRendererVariant(width, height);

    if (width <= 0 || height <= 0 || plan.logical_width <= 0.0F || plan.logical_height <= 0.0F) {
        return plan;
    }

    const float width_scale = static_cast<float>(width) / plan.logical_width;
    const float height_scale = static_cast<float>(height) / plan.logical_height;
    plan.uniform_scale = std::min(width_scale, height_scale);
    if (plan.uniform_scale <= 0.0F) {
        return plan;
    }

    plan.apply = IsWideAspectResolution(width, height);
    plan.scale_x = plan.uniform_scale;
    plan.scale_y = plan.uniform_scale;
    plan.physical_width = plan.logical_width * plan.uniform_scale;
    plan.physical_height = plan.logical_height * plan.uniform_scale;
    plan.physical_origin_x =
        (static_cast<float>(width) - plan.physical_width) * 0.5F;
    plan.physical_origin_y =
        (static_cast<float>(height) - plan.physical_height) * 0.5F;
    plan.horizontal_bias_pixels = plan.physical_origin_x;
    plan.vertical_bias_pixels = plan.physical_origin_y;
    plan.draw_pillarbox = plan.horizontal_bias_pixels > 1.0F;
    plan.draw_letterbox = plan.vertical_bias_pixels > 1.0F;
    if (plan.uniform_scale > 0.0F) {
        plan.logical_horizontal_padding = plan.horizontal_bias_pixels / plan.uniform_scale;
        plan.logical_vertical_padding = plan.vertical_bias_pixels / plan.uniform_scale;
    }
    if (plan.apply && !plan.use_original_variant) {
        plan.render_rect_left = -plan.logical_horizontal_padding;
        plan.render_rect_right = plan.logical_width + plan.logical_horizontal_padding;
        plan.render_rect_top = -plan.logical_vertical_padding;
        plan.render_rect_bottom = plan.logical_height + plan.logical_vertical_padding;
    }
    return plan;
}

UiViewportPlan BuildActiveUiViewportPlan(const int width, const int height) noexcept {
    return BuildUiViewportPlan(width, height, GetDefaultUiProfile());
}

bool PhysicalToUiLogical(
    const UiViewportPlan& plan,
    const float raw_x,
    const float raw_y,
    float* const out_x,
    float* const out_y) noexcept {
    if (!out_x || !out_y || plan.scale_x <= 0.0F || plan.scale_y <= 0.0F) {
        return false;
    }
    *out_x = (raw_x - plan.physical_origin_x) / plan.scale_x;
    *out_y = (raw_y - plan.physical_origin_y) / plan.scale_y;
    return true;
}

bool UiLogicalToPhysical(
    const UiViewportPlan& plan,
    const float logical_x,
    const float logical_y,
    float* const out_x,
    float* const out_y) noexcept {
    if (!out_x || !out_y || plan.scale_x <= 0.0F || plan.scale_y <= 0.0F) {
        return false;
    }
    *out_x = logical_x * plan.scale_x + plan.physical_origin_x;
    *out_y = logical_y * plan.scale_y + plan.physical_origin_y;
    return true;
}

bool FullscreenLogicalToPhysical(
    const UiViewportPlan& plan,
    const float logical_x,
    const float logical_y,
    float* const out_x,
    float* const out_y) noexcept {
    if (!out_x || !out_y || plan.scale_x <= 0.0F || plan.scale_y <= 0.0F) {
        return false;
    }
    *out_x = logical_x * plan.scale_x;
    *out_y = logical_y * plan.scale_y;
    return true;
}

bool BattleOverlayLogicalToPhysical(
    const UiViewportPlan& plan,
    const float logical_x,
    const float logical_y,
    float* const out_x,
    float* const out_y) noexcept {
    if (!out_x || !out_y || plan.scale_x <= 0.0F || plan.scale_y <= 0.0F) {
        return false;
    }

    // This legacy battle path renders into an overlay that tracks the old
    // fullscreen logical origin, then targets a 1080p-sized intermediate
    // surface regardless of the final client resolution.
    *out_x = logical_x * kBattleOverlayMaxScale;
    *out_y = logical_y * kBattleOverlayMaxScale;
    return true;
}

bool CombatResultOverlayLogicalToUiLogical(
    const UiViewportPlan& plan,
    const float logical_x,
    const float logical_y,
    float* const out_x,
    float* const out_y) noexcept {
    if (!out_x || !out_y) {
        return false;
    }

    // These self-rendered combat result images are still drawn by the CEGUI
    // renderer, so keep them in active UI logical space. They only need the
    // legacy 800x600 coordinate to be recentered into the active logical rect.
    *out_x = logical_x + plan.logical_horizontal_padding;
    *out_y = logical_y + plan.logical_vertical_padding;
    return true;
}

bool ProjectedScreenToUiLogical(
    const UiViewportPlan& plan,
    const float projected_x,
    const float projected_y,
    float* const out_x,
    float* const out_y) noexcept {
    return PhysicalToUiLogical(plan, projected_x, projected_y, out_x, out_y);
}

bool UiLogicalToProjectedScreen(
    const UiViewportPlan& plan,
    const float logical_x,
    const float logical_y,
    float* const out_x,
    float* const out_y) noexcept {
    return UiLogicalToPhysical(plan, logical_x, logical_y, out_x, out_y);
}

}  // namespace pal4::inject
