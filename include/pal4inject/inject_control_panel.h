#pragma once

#include <string_view>
#include <vector>

#include "pal4inject/types.h"

namespace pal4::inject {

enum class InjectControlPanelPage : std::uint8_t {
    overview = 0,
    input_ui,
    script_text,
    render_visual,
    camera,
};

struct InjectControlPanelRow {
    HookId id = HookId::process_ui_event;
    InjectControlPanelPage page = InjectControlPanelPage::overview;
    std::wstring_view group_label{};
    std::wstring_view label{};
    bool allow_mode_change = true;
};

std::vector<InjectControlPanelRow> BuildInjectControlPanelRows();
std::vector<HookMode> BuildInjectControlPanelModes();
std::wstring_view BuildInjectControlPanelPageLabel(InjectControlPanelPage page) noexcept;
std::wstring_view BuildInjectControlPanelModeLabel(HookMode mode) noexcept;
int FindInjectControlPanelModeIndex(HookMode mode) noexcept;
HookMode InjectControlPanelModeFromIndex(int index) noexcept;

}  // namespace pal4::inject
