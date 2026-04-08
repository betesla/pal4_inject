#pragma once

#include <string_view>
#include <vector>

#include "pal4inject/types.h"

namespace pal4::inject {

struct InjectControlPanelRow {
    HookId id = HookId::process_ui_event;
    std::string_view label{};
    bool allow_mode_change = true;
};

std::vector<InjectControlPanelRow> BuildInjectControlPanelRows();
std::vector<HookMode> BuildInjectControlPanelModes();
int FindInjectControlPanelModeIndex(HookMode mode) noexcept;
HookMode InjectControlPanelModeFromIndex(int index) noexcept;

}  // namespace pal4::inject
