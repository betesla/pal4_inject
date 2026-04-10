#include "pal4inject/cegui_font_resync.h"

#include <cctype>

namespace pal4::inject {
namespace {

constexpr std::string_view kSystemFont = "system";
constexpr std::string_view kSystemBoldFont = "systemBold";
constexpr std::string_view kDialogSimsunFont = "dialog_simsun";

bool EqualsIgnoreCaseAscii(
    const std::string_view lhs,
    const std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const auto lhs_char = static_cast<unsigned char>(lhs[i]);
        const auto rhs_char = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(lhs_char) != std::tolower(rhs_char)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string_view CanonicalKnownDynamicUiFontName(
    const std::string_view short_name) noexcept {
    if (EqualsIgnoreCaseAscii(short_name, kSystemFont)) {
        return kSystemFont;
    }
    if (EqualsIgnoreCaseAscii(short_name, kSystemBoldFont)) {
        return kSystemBoldFont;
    }
    if (EqualsIgnoreCaseAscii(short_name, kDialogSimsunFont)) {
        return kDialogSimsunFont;
    }
    return {};
}

bool IsKnownDynamicUiFont(const std::string_view short_name) noexcept {
    return !CanonicalKnownDynamicUiFontName(short_name).empty();
}

CeguiDynamicFontResyncTarget BuildKnownDynamicFontResyncTarget(
    const std::string_view short_name,
    const CeguiWidescreenPlan& plan) noexcept {
    (void)short_name;
    CeguiDynamicFontResyncTarget target{};
    target.native_width = plan.logical_width;
    target.native_height = plan.logical_height;
    if (!plan.apply ||
        plan.uniform_scale <= 0.0F ||
        plan.logical_width <= 0.0F ||
        plan.logical_height <= 0.0F) {
        return target;
    }

    target.apply = true;
    target.notify_width = plan.logical_width * plan.uniform_scale;
    target.notify_height = plan.logical_height * plan.uniform_scale;
    return target;
}

}  // namespace pal4::inject
