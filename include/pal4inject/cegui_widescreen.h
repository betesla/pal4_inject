#pragma once

namespace pal4::inject {

struct CeguiWidescreenPlan {
    bool apply = false;
    bool use_original_variant = false;
    int width = 0;
    int height = 0;
    float logical_width = 800.0F;
    float logical_height = 600.0F;
    float uniform_scale = 1.0F;
    float horizontal_bias_pixels = 0.0F;
    float logical_horizontal_padding = 0.0F;
};

struct WidescreenMinimapPlacement {
    bool apply = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

enum class WidescreenHudAnchor {
    none = 0,
    left_edge,
    right_edge,
};

bool IsWideAspectResolution(int width, int height) noexcept;
bool UsesOriginalWideRendererVariant(int width, int height) noexcept;
CeguiWidescreenPlan BuildCeguiWidescreenPlan(int width, int height) noexcept;
CeguiWidescreenPlan BuildCeguiWidescreenPlanForLogicalSize(
    int width,
    int height,
    float logical_width,
    float logical_height) noexcept;
WidescreenMinimapPlacement BuildWidescreenMinimapPlacement(int width, int height) noexcept;
float ComputeWidescreenHudLogicalX(
    const CeguiWidescreenPlan& plan,
    float base_logical_x,
    WidescreenHudAnchor anchor) noexcept;
float ComputeCenteredUiLogicalX(
    const CeguiWidescreenPlan& plan,
    float base_logical_x) noexcept;
float ProjectWidescreenLogicalXToPhysicalPixels(
    const CeguiWidescreenPlan& plan,
    float logical_x) noexcept;
float ProjectPhysicalPixelsToWidescreenLogicalX(
    const CeguiWidescreenPlan& plan,
    float physical_x) noexcept;
float ProjectPhysicalPixelsToWidescreenLogicalY(
    const CeguiWidescreenPlan& plan,
    float physical_y) noexcept;
bool ApplyCeguiWidescreenMouseTransform(
    const CeguiWidescreenPlan& plan,
    float raw_x,
    float raw_y,
    float* out_x,
    float* out_y) noexcept;

}  // namespace pal4::inject
