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

bool IsWideAspectResolution(int width, int height) noexcept;
bool UsesOriginalWideRendererVariant(int width, int height) noexcept;
CeguiWidescreenPlan BuildCeguiWidescreenPlan(int width, int height) noexcept;
WidescreenMinimapPlacement BuildWidescreenMinimapPlacement(int width, int height) noexcept;
bool ApplyCeguiWidescreenMouseTransform(
    const CeguiWidescreenPlan& plan,
    float raw_x,
    float raw_y,
    float* out_x,
    float* out_y) noexcept;

}  // namespace pal4::inject
