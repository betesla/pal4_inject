#pragma once

#include <string_view>

#include "pal4inject/cegui_widescreen.h"

namespace pal4::inject {

struct CeguiDynamicFontResyncTarget {
    bool apply = false;
    float native_width = 800.0F;
    float native_height = 600.0F;
    float notify_width = 800.0F;
    float notify_height = 600.0F;
};

std::string_view CanonicalKnownDynamicUiFontName(std::string_view short_name) noexcept;
bool IsKnownDynamicUiFont(std::string_view short_name) noexcept;
CeguiDynamicFontResyncTarget BuildKnownDynamicFontResyncTarget(
    std::string_view short_name,
    const CeguiWidescreenPlan& plan) noexcept;

}  // namespace pal4::inject
