#pragma once

#include <string_view>
#include <vector>

#include "pal4inject/inject_settings.h"
#include "pal4inject/types.h"

namespace pal4::inject {

enum class InjectFeatureCategory : std::uint8_t {
    input_ui = 0,
    script_text,
    render_visual,
    camera,
};

struct InjectFeatureDescriptor {
    HookId id = HookId::process_ui_event;
    InjectFeatureCategory category = InjectFeatureCategory::input_ui;
    std::string_view group_label{};
    std::string_view label{};
    std::string_view description{};
    bool allow_mode_change = true;
};

std::vector<InjectFeatureDescriptor> BuildInjectFeatureCatalog();
bool InjectFeatureFollowsWidescreen(HookId id) noexcept;
void ApplyWidescreenFeaturePreset(
    InjectPersistedSettings* settings,
    bool enabled) noexcept;
std::vector<HookMode> BuildInjectFeatureModes();
std::string_view InjectFeatureCategoryLabel(InjectFeatureCategory category) noexcept;
std::string_view InjectFeatureModeLabel(HookMode mode) noexcept;
int FindInjectFeatureModeIndex(HookMode mode) noexcept;
HookMode InjectFeatureModeFromIndex(int index) noexcept;

}  // namespace pal4::inject
