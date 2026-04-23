#pragma once

#include <string>
#include <string_view>

namespace pal4::inject {

void RememberKnownDynamicFontTexture(
    std::string_view font_name,
    const void* texture) noexcept;
bool IsKnownDynamicFontTexture(const void* texture) noexcept;
bool RefreshKnownDynamicFontTextures(std::string* error);

}  // namespace pal4::inject
