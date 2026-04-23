#include "pal4inject/cegui_font_experiment.h"

#include <algorithm>

namespace pal4::inject {

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
        plan.line_spacing_scale = 1.0F;
        plan.baseline_scale = 1.0F;
        return plan;
    }
    if (canonical_font_name == "system") {
        // `system` is used heavily by long-form help text.  A milder oversample
        // keeps punctuation advances closer to the original 13pt layout while
        // still improving glyph sampling quality.
        plan.apply = true;
        plan.oversampled_point_size = 20;
        plan.draw_scale =
            static_cast<float>(current_point_size) /
            static_cast<float>(plan.oversampled_point_size);
        plan.extent_scale = plan.draw_scale;
        plan.glyph_offset_y = 0.0F;
        plan.line_spacing_scale = 0.92F;
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

}  // namespace pal4::inject
