#include "pal4inject/cegui_font_experiment.h"

#include <algorithm>
#include <array>

namespace pal4::inject {

namespace {

constexpr std::array<std::uintptr_t, 5> kOiramlookOlButtonDrawTextCallerRvas{
    0x17391,
    0x17602,
    0x17872,
    0x17AE2,
    0x17CA2,
};
constexpr std::array<std::string_view, 5> kOiramlookOlButtonCallerDescriptions{
    "OIRAMLOOK.dll+0x17391",
    "OIRAMLOOK.dll+0x17602",
    "OIRAMLOOK.dll+0x17872",
    "OIRAMLOOK.dll+0x17ae2",
    "OIRAMLOOK.dll+0x17ca2",
};

}  // namespace

DynamicFontOversamplePlan BuildDynamicFontOversamplePlan(
    const std::string_view canonical_font_name,
    const std::uint32_t current_point_size) noexcept {
    DynamicFontOversamplePlan plan{};
    if (current_point_size == 0) {
        return plan;
    }
    if (canonical_font_name == "dialog_simsun") {
        plan.apply = true;
        plan.oversampled_point_size = current_point_size * 2U;
        plan.draw_scale = 0.5F;
        plan.extent_scale = 0.5F;
        plan.glyph_offset_y = 0.0F;
        plan.observe_glyph_image_offsets = false;
        plan.line_spacing_scale = 1.0F;
        plan.baseline_scale = 1.0F;
        return plan;
    }
    if (canonical_font_name == "system") {
        // `system` is used heavily by long-form help text.  A milder oversample
        // keeps punctuation advances closer to the original 13pt layout while
        // still improving glyph sampling quality.  Runtime metric repair keeps
        // the original logical row step so RichTextFrame can add the existing
        // LineSpaceExtent without widening system UI paragraphs.
        plan.apply = true;
        plan.oversampled_point_size = 20;
        plan.draw_scale =
            static_cast<float>(current_point_size) /
            static_cast<float>(plan.oversampled_point_size);
        plan.extent_scale = plan.draw_scale;
        plan.glyph_offset_y = 0.0F;
        plan.observe_glyph_image_offsets = true;
        plan.line_spacing_scale = 1.0F;
        plan.baseline_scale = 1.0F;
        plan.preserve_original_vertical_metrics = true;
        return plan;
    }
    if (canonical_font_name == "systemBold") {
        plan.apply = true;
        plan.oversampled_point_size = current_point_size * 2U;
        plan.draw_scale = 0.5F;
        plan.extent_scale = 0.5F;
        plan.glyph_offset_y = 0.0F;
        plan.observe_glyph_image_offsets = false;
        plan.line_spacing_scale = 1.0F;
        plan.baseline_scale = 1.0F;
        plan.preserve_original_vertical_metrics = true;
        return plan;
    }
    return plan;
}

float ComputeDialogRichTextGlyphHeight(
    const DynamicFontOversamplePlan plan,
    const std::uint32_t current_point_size,
    const float raw_height) noexcept {
    if (!plan.apply || raw_height <= 0.0F) {
        return raw_height;
    }

    // OIRAMLOOK caches DocLine heights from TextGlyph::getHeight().  If that
    // height still looks like the oversampled font's unscaled line spacing,
    // shrink it to the logical dialog line height.  The absolute floor keeps an
    // already-compensated 17-22px line height from being scaled a second time.
    const float point_threshold = current_point_size > 0
        ? static_cast<float>(current_point_size) * 0.75F
        : 0.0F;
    const float unscaled_threshold = std::max(24.0F, point_threshold);
    if (raw_height <= unscaled_threshold) {
        return raw_height;
    }

    return raw_height * plan.draw_scale * plan.line_spacing_scale;
}

bool ShouldApplyOiramlookOlButtonTextRectYOffset(
    const std::string_view canonical_font_name,
    const std::string_view caller_module_name,
    const std::uintptr_t caller_rva,
    const int formatting) noexcept {
    if (canonical_font_name != "system") {
        return false;
    }
    if (caller_module_name != "OIRAMLOOK.dll") {
        return false;
    }
    if (formatting != 2) {
        return false;
    }
    return std::find(
               kOiramlookOlButtonDrawTextCallerRvas.begin(),
               kOiramlookOlButtonDrawTextCallerRvas.end(),
               caller_rva) != kOiramlookOlButtonDrawTextCallerRvas.end();
}

bool ShouldApplyOiramlookOlButtonTextRectYOffset(
    const std::string_view canonical_font_name,
    const std::string_view caller_description,
    const int formatting) noexcept {
    if (canonical_font_name != "system") {
        return false;
    }
    if (formatting != 2) {
        return false;
    }
    return std::find(
               kOiramlookOlButtonCallerDescriptions.begin(),
               kOiramlookOlButtonCallerDescriptions.end(),
               caller_description) != kOiramlookOlButtonCallerDescriptions.end();
}

float GetOiramlookOlButtonTextRectYOffset() noexcept {
    return 2.0F;
}

}  // namespace pal4::inject
