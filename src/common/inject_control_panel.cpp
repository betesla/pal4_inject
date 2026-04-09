#include "pal4inject/inject_control_panel.h"

#include "pal4inject/hook_inventory.h"

namespace pal4::inject {
namespace {

std::string_view BuildDisplayLabel(const HookId id) {
    switch (id) {
    case HookId::process_ui_event:
        return "UI event replacement";
    case HookId::handle_ui_message:
        return "Menu message dispatch";
    case HookId::simulate_key_press_and_release:
        return "Keyboard compatibility";
    case HookId::process_inputs:
        return "Input frame observe";
    case HookId::update_input_device_state:
        return "Input device observe";
    case HookId::initialize_direct_input:
        return "DirectInput observe";
    case HookId::gi_talk:
        return "Dialogue text injection";
    case HookId::cegui_renderer_constructor_2:
        return "Wide UI centering";
    case HookId::cegui_system_initialize:
        return "CEGUI init observe";
    case HookId::setup_minimap_texture:
        return "Minimap widescreen fix";
    case HookId::camera_update_matrix:
        return "Camera pitch clamp";
    case HookId::d3d9_set_present_parameters:
        return "MSAA override";
    case HookId::pal4_main_wndproc:
        return "Panel focus protection";
    case HookId::handle_player_input_events:
        return "Player input observe";
    }
    return "Unknown Hook";
}

}  // namespace

std::vector<InjectControlPanelRow> BuildInjectControlPanelRows() {
    const auto inventory = BuildHookInventorySkeleton();
    std::vector<InjectControlPanelRow> rows;
    rows.reserve(inventory.size());
    for (const auto& descriptor : inventory) {
        rows.push_back({
            descriptor.id,
            BuildDisplayLabel(descriptor.id),
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
