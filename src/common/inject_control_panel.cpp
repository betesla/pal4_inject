#include "pal4inject/inject_control_panel.h"

#include "pal4inject/hook_inventory.h"

namespace pal4::inject {

std::vector<InjectControlPanelRow> BuildInjectControlPanelRows() {
    const auto inventory = BuildHookInventorySkeleton();
    std::vector<InjectControlPanelRow> rows;
    rows.reserve(inventory.size());
    for (const auto& descriptor : inventory) {
        rows.push_back({
            descriptor.id,
            ToString(descriptor.id),
            descriptor.id != HookId::handle_player_input_events,
        });
    }
    return rows;
}

std::vector<HookMode> BuildInjectControlPanelModes() {
    return {
        HookMode::observe_only,
        HookMode::mirror_compare,
        HookMode::replace_with_fallback,
        HookMode::replace_strict,
    };
}

int FindInjectControlPanelModeIndex(const HookMode mode) noexcept {
    const auto modes = BuildInjectControlPanelModes();
    for (int i = 0; i < static_cast<int>(modes.size()); ++i) {
        if (modes[static_cast<std::size_t>(i)] == mode) {
            return i;
        }
    }
    return 0;
}

HookMode InjectControlPanelModeFromIndex(const int index) noexcept {
    const auto modes = BuildInjectControlPanelModes();
    if (index < 0 || index >= static_cast<int>(modes.size())) {
        return HookMode::observe_only;
    }
    return modes[static_cast<std::size_t>(index)];
}

}  // namespace pal4::inject
