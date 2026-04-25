#include "pal4inject/inject_control_panel.h"

namespace pal4::inject {
namespace {

InjectControlPanelPage BuildPage(const HookId id) {
    switch (id) {
    case HookId::cegui_renderer_constructor_2:
    case HookId::load_font_file:
    case HookId::setup_minimap_texture:
    case HookId::combat_console_set_image_position:
    case HookId::combat_console_set_image_position_2:
    case HookId::ui_show_combat_result:
        return InjectControlPanelPage::ui_resolution;
    case HookId::process_ui_event:
    case HookId::handle_ui_message:
    case HookId::simulate_key_press_and_release:
    case HookId::process_inputs:
    case HookId::pal4_main_wndproc:
        return InjectControlPanelPage::input_interaction;
    case HookId::gi_talk:
        return InjectControlPanelPage::script_runtime;
    case HookId::d3d9_set_present_parameters:
        return InjectControlPanelPage::render_visual;
    case HookId::camera_update_matrix:
    case HookId::game_render_frame:
        return InjectControlPanelPage::camera;
    }
    return InjectControlPanelPage::overview;
}

std::wstring_view BuildGroupLabel(const HookId id) {
    switch (id) {
    case HookId::process_ui_event:
    case HookId::handle_ui_message:
    case HookId::simulate_key_press_and_release:
    case HookId::process_inputs:
    case HookId::pal4_main_wndproc:
        return L"\u8f93\u5165\u4e0e\u4ea4\u4e92";
    case HookId::cegui_renderer_constructor_2:
    case HookId::load_font_file:
    case HookId::setup_minimap_texture:
    case HookId::combat_console_set_image_position:
    case HookId::combat_console_set_image_position_2:
    case HookId::ui_show_combat_result:
        return L"UI \u5206\u8fa8\u7387\u81ea\u9002\u5e94";
    case HookId::gi_talk:
        return L"\u811a\u672c\u4e0e\u5267\u60c5";
    case HookId::d3d9_set_present_parameters:
        return L"\u6e32\u67d3\u4e0e\u753b\u9762";
    case HookId::camera_update_matrix:
    case HookId::game_render_frame:
        return L"\u76f8\u673a";
    }
    return L"\u672a\u77e5\u5206\u7ec4";
}

std::wstring_view BuildDisplayLabel(const HookId id) {
    switch (id) {
    case HookId::process_ui_event:
        return L"\u83dc\u5355\u70b9\u51fb\u4e0e\u6309\u952e\u63a5\u7ba1";
    case HookId::handle_ui_message:
        return L"\u83dc\u5355\u6d88\u606f\u8865\u6295\u9012";
    case HookId::simulate_key_press_and_release:
        return L"\u517c\u5bb9\u6309\u952e\u6a21\u62df";
    case HookId::process_inputs:
        return L"\u624b\u67c4\u8f6e\u8be2\u4e0e HUD \u5237\u65b0";
    case HookId::gi_talk:
        return L"\u5bf9\u767d\u6587\u672c\u6ce8\u5165";
    case HookId::cegui_renderer_constructor_2:
        return L"\u5bbd\u5c4f\u754c\u9762\u5c45\u4e2d";
    case HookId::load_font_file:
        return L"\u9ad8\u6e05\u5b57\u4f53\u4e0e\u7f29\u653e\u91cd\u540c\u6b65";
    case HookId::setup_minimap_texture:
        return L"\u5c0f\u5730\u56fe\u5bbd\u5c4f\u4fee\u6b63";
    case HookId::combat_console_set_image_position:
        return L"\u6218\u6597\u6d6e\u5b57\u4e0e\u80dc\u5229\u56fe\u5c45\u4e2d";
    case HookId::combat_console_set_image_position_2:
        return L"\u6218\u6597\u63d0\u793a\u7a97\u5c45\u4e2d A";
    case HookId::ui_show_combat_result:
        return L"\u6218\u6597\u63d0\u793a\u7a97\u5c45\u4e2d B";
    case HookId::camera_update_matrix:
        return L"\u76f8\u673a\u4fef\u4ef0\u4fdd\u62a4";
    case HookId::game_render_frame:
        return L"VR \u6e32\u67d3\u5e27\u4e0e\u6d3b\u8dc3\u76f8\u673a\u91c7\u6837";
    case HookId::d3d9_set_present_parameters:
        return L"\u6297\u952f\u9f7f\u8bbe\u5907\u53c2\u6570\u8986\u5199";
    case HookId::pal4_main_wndproc:
        return L"\u9762\u677f\u4e0e\u6e38\u620f\u7126\u70b9\u4fdd\u62a4";
    }
    return L"\u672a\u77e5\u529f\u80fd";
}

std::string_view BuildHookName(const HookId id) {
    return ToString(id);
}

}  // namespace

std::vector<InjectControlPanelRow> BuildInjectControlPanelRows() {
    return {
        {HookId::process_ui_event, BuildPage(HookId::process_ui_event), BuildGroupLabel(HookId::process_ui_event), BuildDisplayLabel(HookId::process_ui_event), BuildHookName(HookId::process_ui_event), true},
        {HookId::handle_ui_message, BuildPage(HookId::handle_ui_message), BuildGroupLabel(HookId::handle_ui_message), BuildDisplayLabel(HookId::handle_ui_message), BuildHookName(HookId::handle_ui_message), true},
        {HookId::simulate_key_press_and_release, BuildPage(HookId::simulate_key_press_and_release), BuildGroupLabel(HookId::simulate_key_press_and_release), BuildDisplayLabel(HookId::simulate_key_press_and_release), BuildHookName(HookId::simulate_key_press_and_release), true},
        {HookId::process_inputs, BuildPage(HookId::process_inputs), BuildGroupLabel(HookId::process_inputs), BuildDisplayLabel(HookId::process_inputs), BuildHookName(HookId::process_inputs), true},
        {HookId::pal4_main_wndproc, BuildPage(HookId::pal4_main_wndproc), BuildGroupLabel(HookId::pal4_main_wndproc), BuildDisplayLabel(HookId::pal4_main_wndproc), BuildHookName(HookId::pal4_main_wndproc), true},
        {HookId::gi_talk, BuildPage(HookId::gi_talk), BuildGroupLabel(HookId::gi_talk), BuildDisplayLabel(HookId::gi_talk), BuildHookName(HookId::gi_talk), true},
        {HookId::load_font_file, BuildPage(HookId::load_font_file), BuildGroupLabel(HookId::load_font_file), BuildDisplayLabel(HookId::load_font_file), BuildHookName(HookId::load_font_file), true},
        {HookId::cegui_renderer_constructor_2, BuildPage(HookId::cegui_renderer_constructor_2), BuildGroupLabel(HookId::cegui_renderer_constructor_2), BuildDisplayLabel(HookId::cegui_renderer_constructor_2), BuildHookName(HookId::cegui_renderer_constructor_2), true},
        {HookId::setup_minimap_texture, BuildPage(HookId::setup_minimap_texture), BuildGroupLabel(HookId::setup_minimap_texture), BuildDisplayLabel(HookId::setup_minimap_texture), BuildHookName(HookId::setup_minimap_texture), true},
        {HookId::combat_console_set_image_position, BuildPage(HookId::combat_console_set_image_position), BuildGroupLabel(HookId::combat_console_set_image_position), BuildDisplayLabel(HookId::combat_console_set_image_position), BuildHookName(HookId::combat_console_set_image_position), true},
        {HookId::combat_console_set_image_position_2, BuildPage(HookId::combat_console_set_image_position_2), BuildGroupLabel(HookId::combat_console_set_image_position_2), BuildDisplayLabel(HookId::combat_console_set_image_position_2), BuildHookName(HookId::combat_console_set_image_position_2), true},
        {HookId::ui_show_combat_result, BuildPage(HookId::ui_show_combat_result), BuildGroupLabel(HookId::ui_show_combat_result), BuildDisplayLabel(HookId::ui_show_combat_result), BuildHookName(HookId::ui_show_combat_result), true},
        {HookId::d3d9_set_present_parameters, BuildPage(HookId::d3d9_set_present_parameters), BuildGroupLabel(HookId::d3d9_set_present_parameters), BuildDisplayLabel(HookId::d3d9_set_present_parameters), BuildHookName(HookId::d3d9_set_present_parameters), true},
        {HookId::camera_update_matrix, BuildPage(HookId::camera_update_matrix), BuildGroupLabel(HookId::camera_update_matrix), BuildDisplayLabel(HookId::camera_update_matrix), BuildHookName(HookId::camera_update_matrix), true},
        {HookId::game_render_frame, BuildPage(HookId::game_render_frame), BuildGroupLabel(HookId::game_render_frame), BuildDisplayLabel(HookId::game_render_frame), BuildHookName(HookId::game_render_frame), true},
    };
}

std::vector<HookMode> BuildInjectControlPanelModes() {
    return {
        HookMode::observe_only,
        HookMode::replace_with_fallback,
    };
}

std::wstring_view BuildInjectControlPanelPageLabel(const InjectControlPanelPage page) noexcept {
    switch (page) {
    case InjectControlPanelPage::overview:
        return L"\u6982\u89c8";
    case InjectControlPanelPage::ui_resolution:
        return L"UI \u5206\u8fa8\u7387\u81ea\u9002\u5e94";
    case InjectControlPanelPage::input_interaction:
        return L"\u8f93\u5165\u4e0e\u4ea4\u4e92";
    case InjectControlPanelPage::script_runtime:
        return L"\u811a\u672c\u4e0e\u5267\u60c5";
    case InjectControlPanelPage::render_visual:
        return L"\u6e32\u67d3\u4e0e\u753b\u9762";
    case InjectControlPanelPage::camera:
        return L"\u76f8\u673a";
    }
    return L"\u672a\u77e5";
}

std::wstring_view BuildInjectControlPanelModeLabel(const HookMode mode) noexcept {
    switch (mode) {
    case HookMode::observe_only:
        return L"\u4ec5\u89c2\u5bdf";
    case HookMode::replace_with_fallback:
        return L"\u66ff\u6362\uff08\u53ef\u56de\u9000\uff09";
    case HookMode::mirror_compare:
    case HookMode::replace_strict:
        return L"\u66ff\u6362\uff08\u53ef\u56de\u9000\uff09";
    }
    return L"\u672a\u77e5\u6a21\u5f0f";
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
