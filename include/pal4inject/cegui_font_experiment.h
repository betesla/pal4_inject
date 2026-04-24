#pragma once

#include <cstdint>
#include <string_view>

namespace pal4::inject {

struct DynamicFontOversamplePlan {
    bool apply = false;
    std::uint32_t oversampled_point_size = 0;
    float draw_scale = 1.0F;
    float extent_scale = 1.0F;
    float glyph_offset_y = 0.0F;
    bool observe_glyph_image_offsets = false;
    float line_spacing_scale = 1.0F;
    float baseline_scale = 1.0F;
    bool preserve_original_vertical_metrics = false;
};

DynamicFontOversamplePlan BuildDynamicFontOversamplePlan(
    std::string_view canonical_font_name,
    std::uint32_t current_point_size) noexcept;

float ComputeDialogRichTextGlyphHeight(
    DynamicFontOversamplePlan plan,
    std::uint32_t current_point_size,
    float raw_height) noexcept;

bool ShouldApplyOiramlookOlButtonTextRectYOffset(
    std::string_view canonical_font_name,
    std::string_view caller_module_name,
    std::uintptr_t caller_rva,
    int formatting) noexcept;

bool ShouldApplyOiramlookOlButtonTextRectYOffset(
    std::string_view canonical_font_name,
    std::string_view caller_description,
    int formatting) noexcept;

float GetOiramlookOlButtonTextRectYOffset() noexcept;

}  // namespace pal4::inject
