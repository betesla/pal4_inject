#pragma once

#include "pal4inject/ui_coordinate_space.h"

namespace pal4::inject {

using CeguiWidescreenPlan = UiViewportPlan;

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
WidescreenMinimapPlacement BuildWidescreenMinimapPlacement(int width, int height) noexcept;
float ComputeWidescreenHudLogicalX(
    const CeguiWidescreenPlan& plan,
    float base_logical_x,
    WidescreenHudAnchor anchor) noexcept;
float ComputeWidescreenEdgeToEdgeLogicalWidth(
    const CeguiWidescreenPlan& plan,
    float base_logical_width) noexcept;
float ComputeCenteredUiLogicalX(
    const CeguiWidescreenPlan& plan,
    float base_logical_x) noexcept;
float ProjectWidescreenLogicalXToPhysicalPixels(
    const CeguiWidescreenPlan& plan,
    float logical_x) noexcept;
bool ApplyCeguiWidescreenMouseTransform(
    const CeguiWidescreenPlan& plan,
    float raw_x,
    float raw_y,
    float* out_x,
    float* out_y) noexcept;
bool ShouldDrawOriginalUiPillarboxMask(const CeguiWidescreenPlan& plan) noexcept;
bool ShouldDrawOriginalUiPillarboxForVisibleRoots(
    bool pillarboxed_main_menu_root_visible,
    bool system_toolbar_root_visible,
    bool frame_toolbar_root_visible,
    bool system_setting_visible,
    bool in_game_system_menu_widescreen_adapted) noexcept;

}  // namespace pal4::inject
