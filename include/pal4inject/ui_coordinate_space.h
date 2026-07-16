#pragma once

#include <utility>

namespace pal4::inject {

enum class UiProfile : unsigned char {
    centered_800x600 = 0,
    widescreen_1067x600,
};

struct UiViewportPlan {
    UiProfile profile = UiProfile::centered_800x600;
    bool apply = false;
    bool use_original_variant = false;
    int width = 0;
    int height = 0;
    float logical_width = 800.0F;
    float logical_height = 600.0F;
    float uniform_scale = 1.0F;
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    float physical_origin_x = 0.0F;
    float physical_origin_y = 0.0F;
    float physical_width = 800.0F;
    float physical_height = 600.0F;
    float horizontal_bias_pixels = 0.0F;
    float vertical_bias_pixels = 0.0F;
    float logical_horizontal_padding = 0.0F;
    float logical_vertical_padding = 0.0F;
    float render_rect_left = 0.0F;
    float render_rect_top = 0.0F;
    float render_rect_right = 800.0F;
    float render_rect_bottom = 600.0F;
    bool draw_pillarbox = false;
    bool draw_letterbox = false;
};

const char* ToString(UiProfile profile) noexcept;
bool IsWideAspectResolution(int width, int height) noexcept;
bool UsesOriginalWideRendererVariant(int width, int height) noexcept;
UiProfile GetDefaultUiProfile() noexcept;
std::pair<float, float> GetUiProfileLogicalSize(UiProfile profile) noexcept;
UiViewportPlan BuildUiViewportPlan(int width, int height, UiProfile profile) noexcept;
UiViewportPlan BuildActiveUiViewportPlan(int width, int height) noexcept;
bool PhysicalToUiLogical(
    const UiViewportPlan& plan,
    float raw_x,
    float raw_y,
    float* out_x,
    float* out_y) noexcept;
bool UiLogicalToPhysical(
    const UiViewportPlan& plan,
    float logical_x,
    float logical_y,
    float* out_x,
    float* out_y) noexcept;
bool FullscreenLogicalToPhysical(
    const UiViewportPlan& plan,
    float logical_x,
    float logical_y,
    float* out_x,
    float* out_y) noexcept;
bool BattleOverlayLogicalToPhysical(
    const UiViewportPlan& plan,
    float logical_x,
    float logical_y,
    float* out_x,
    float* out_y) noexcept;
bool CombatResultOverlayLogicalToUiLogical(
    const UiViewportPlan& plan,
    float logical_x,
    float logical_y,
    float* out_x,
    float* out_y) noexcept;
bool ProjectedScreenToUiLogical(
    const UiViewportPlan& plan,
    float projected_x,
    float projected_y,
    float* out_x,
    float* out_y) noexcept;
bool UiLogicalToProjectedScreen(
    const UiViewportPlan& plan,
    float logical_x,
    float logical_y,
    float* out_x,
    float* out_y) noexcept;

}  // namespace pal4::inject
