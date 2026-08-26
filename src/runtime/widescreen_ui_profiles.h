#pragma once

#include <cstddef>
#include <string_view>

#include "pal4inject/widescreen_ui_layout.h"

namespace pal4::inject {

enum class WidescreenUiPillarboxPolicy {
    unchanged,
    preserve,
    remove,
};

struct WidescreenUiWindowRule {
    std::string_view debug_name;
    std::string_view window_name;
    std::string_view fallback_window_name;
    WidescreenUiHorizontalMode horizontal_mode = WidescreenUiHorizontalMode::preserve;
    bool allow_outside_parent = false;
    float horizontal_padding_factor = 0.0F;
    float widescreen_y_offset = 0.0F;
};

struct WidescreenUiProfile {
    std::string_view id;
    std::string_view trigger_window_name;
    WidescreenUiPillarboxPolicy pillarbox_policy = WidescreenUiPillarboxPolicy::unchanged;
    const WidescreenUiWindowRule* rules = nullptr;
    std::size_t rule_count = 0;
};

const WidescreenUiProfile* GetWidescreenUiProfiles(std::size_t* count) noexcept;
const WidescreenUiProfile* FindWidescreenUiProfileByRootName(std::string_view name) noexcept;
bool WidescreenUiWindowNameMatches(
    std::string_view actual,
    std::string_view expected_leaf) noexcept;

}  // namespace pal4::inject
