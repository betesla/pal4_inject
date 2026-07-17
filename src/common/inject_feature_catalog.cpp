#include "pal4inject/inject_feature_catalog.h"

namespace pal4::inject {
namespace {

InjectFeatureCategory BuildCategory(const HookId id) {
    switch (id) {
    case HookId::process_ui_event:
    case HookId::handle_ui_message:
    case HookId::simulate_key_press_and_release:
    case HookId::process_inputs:
    case HookId::update_input_device_state:
    case HookId::initialize_direct_input:
    case HookId::handle_player_input_events:
        return InjectFeatureCategory::input_ui;
    case HookId::gi_talk:
    case HookId::load_font_file:
        return InjectFeatureCategory::script_text;
    case HookId::cegui_renderer_constructor_2:
    case HookId::cegui_system_initialize:
    case HookId::setup_minimap_texture:
    case HookId::combat_console_set_image_position:
    case HookId::combat_console_set_image_position_2:
    case HookId::ui_show_combat_result:
    case HookId::render_text_and_image:
    case HookId::bink_player_update_and_render:
    case HookId::d3d9_set_present_parameters:
        return InjectFeatureCategory::render_visual;
    case HookId::camera_update_matrix:
        return InjectFeatureCategory::camera;
    case HookId::loose_file_overlay:
    case HookId::loose_text_script_overlay:
        return InjectFeatureCategory::resource;
    case HookId::pal4_main_wndproc:
        break;
    }
    return InjectFeatureCategory::input_ui;
}

std::string_view BuildGroupLabel(const HookId id) {
    return InjectFeatureCategoryLabel(BuildCategory(id));
}

}  // namespace

std::vector<InjectFeatureDescriptor> BuildInjectFeatureCatalog() {
    return {
        {HookId::process_ui_event, BuildCategory(HookId::process_ui_event), BuildGroupLabel(HookId::process_ui_event), "界面事件替换", "接管主要 CEGUI 事件入口，并在失败时回退到原实现。", true},
        {HookId::handle_ui_message, BuildCategory(HookId::handle_ui_message), BuildGroupLabel(HookId::handle_ui_message), "菜单消息派发", "修正自动化与宽屏环境下的菜单消息投递。", true},
        {HookId::simulate_key_press_and_release, BuildCategory(HookId::simulate_key_press_and_release), BuildGroupLabel(HookId::simulate_key_press_and_release), "键盘兼容层", "观察或替换游戏内部的按键模拟路径。", true},
        {HookId::process_inputs, BuildCategory(HookId::process_inputs), BuildGroupLabel(HookId::process_inputs), "输入帧观察", "记录每帧输入处理状态，主要用于诊断。", true},
        {HookId::update_input_device_state, BuildCategory(HookId::update_input_device_state), BuildGroupLabel(HookId::update_input_device_state), "输入设备观察", "记录 DirectInput 设备状态更新。", true},
        {HookId::initialize_direct_input, BuildCategory(HookId::initialize_direct_input), BuildGroupLabel(HookId::initialize_direct_input), "DirectInput 观察", "诊断 DirectInput 初始化过程。", true},
        {HookId::gi_talk, BuildCategory(HookId::gi_talk), BuildGroupLabel(HookId::gi_talk), "对白文本注入", "接管实际对白回调并支持文本脚本调试。", true},
        {HookId::load_font_file, BuildCategory(HookId::load_font_file), BuildGroupLabel(HookId::load_font_file), "动态字体重同步", "在安全时机重新同步 CEGUI 动态字体。", true},
        {HookId::cegui_renderer_constructor_2, BuildCategory(HookId::cegui_renderer_constructor_2), BuildGroupLabel(HookId::cegui_renderer_constructor_2), "宽屏界面居中", "建立宽屏 CEGUI 逻辑视口并保持界面比例。", true},
        {HookId::cegui_system_initialize, BuildCategory(HookId::cegui_system_initialize), BuildGroupLabel(HookId::cegui_system_initialize), "CEGUI 初始化观察", "记录 CEGUI 系统初始化状态。", true},
        {HookId::setup_minimap_texture, BuildCategory(HookId::setup_minimap_texture), BuildGroupLabel(HookId::setup_minimap_texture), "小地图宽屏修正", "修正宽屏下小地图纹理与布局。", true},
        {HookId::combat_console_set_image_position, BuildCategory(HookId::combat_console_set_image_position), BuildGroupLabel(HookId::combat_console_set_image_position), "战斗浮字与胜利图居中", "修正战斗世界浮层和胜利图片位置。", true},
        {HookId::combat_console_set_image_position_2, BuildCategory(HookId::combat_console_set_image_position_2), BuildGroupLabel(HookId::combat_console_set_image_position_2), "战斗提示窗居中 A", "修正第一条战斗提示窗口路径。", true},
        {HookId::ui_show_combat_result, BuildCategory(HookId::ui_show_combat_result), BuildGroupLabel(HookId::ui_show_combat_result), "战斗提示窗居中 B", "修正战斗结算与失败提示窗口路径。", true},
        {HookId::render_text_and_image, BuildCategory(HookId::render_text_and_image), BuildGroupLabel(HookId::render_text_and_image), "战斗浮字共享投影", "统一宽屏战斗伤害数字与图像的投影空间。", true},
        {HookId::bink_player_update_and_render, BuildCategory(HookId::bink_player_update_and_render), BuildGroupLabel(HookId::bink_player_update_and_render), "Bink 视频比例修正", "按 launcher 选项完整显示视频，或保持比例铺满宽屏并裁剪上下画面。", true},
        {HookId::d3d9_set_present_parameters, BuildCategory(HookId::d3d9_set_present_parameters), BuildGroupLabel(HookId::d3d9_set_present_parameters), "MSAA 覆写", "在 D3D9 呈现参数生效时请求指定抗锯齿等级。", true},
        {HookId::camera_update_matrix, BuildCategory(HookId::camera_update_matrix), BuildGroupLabel(HookId::camera_update_matrix), "相机俯仰保护", "扩大相机俯仰范围并阻止越过翻转边界。", true},
        {HookId::loose_file_overlay, BuildCategory(HookId::loose_file_overlay), BuildGroupLabel(HookId::loose_file_overlay), "松散文件补丁", "优先加载 gamepatch；CS 脚本缺失时不回退，使用独立日志记录。", true, false},
    };
}

bool InjectFeatureFollowsWidescreen(const HookId id) noexcept {
    switch (id) {
    case HookId::cegui_renderer_constructor_2:
    case HookId::load_font_file:
    case HookId::setup_minimap_texture:
    case HookId::combat_console_set_image_position:
    case HookId::combat_console_set_image_position_2:
    case HookId::ui_show_combat_result:
    case HookId::render_text_and_image:
    case HookId::bink_player_update_and_render:
        return true;
    default:
        return false;
    }
}

void ApplyWidescreenFeaturePreset(
    InjectPersistedSettings* const settings,
    const bool enabled) noexcept {
    if (!settings) {
        return;
    }
    for (auto& hook : settings->hooks) {
        if (!InjectFeatureFollowsWidescreen(hook.id)) {
            continue;
        }
        if (enabled) {
            hook.mode = hook.active_mode == HookMode::replace_strict
                ? HookMode::replace_strict
                : HookMode::replace_with_fallback;
            hook.active_mode = hook.mode;
        } else {
            if (hook.mode != HookMode::observe_only) {
                hook.active_mode = hook.mode;
            }
            hook.mode = HookMode::observe_only;
        }
    }
}

std::vector<HookMode> BuildInjectFeatureModes() {
    return {
        HookMode::observe_only,
        HookMode::mirror_compare,
        HookMode::replace_with_fallback,
        HookMode::replace_strict,
    };
}

std::string_view InjectFeatureCategoryLabel(const InjectFeatureCategory category) noexcept {
    switch (category) {
    case InjectFeatureCategory::input_ui:
        return "输入与界面";
    case InjectFeatureCategory::script_text:
        return "脚本与文本";
    case InjectFeatureCategory::render_visual:
        return "渲染与画面";
    case InjectFeatureCategory::camera:
        return "相机";
    case InjectFeatureCategory::resource:
        return "资源与补丁";
    }
    return "未知";
}

std::string_view InjectFeatureModeLabel(const HookMode mode) noexcept {
    switch (mode) {
    case HookMode::observe_only:
        return "仅观察";
    case HookMode::mirror_compare:
        return "镜像比对";
    case HookMode::replace_with_fallback:
        return "替换（可回退）";
    case HookMode::replace_strict:
        return "强制替换";
    }
    return "未知模式";
}

int FindInjectFeatureModeIndex(const HookMode mode) noexcept {
    const auto modes = BuildInjectFeatureModes();
    for (int index = 0; index < static_cast<int>(modes.size()); ++index) {
        if (modes[static_cast<std::size_t>(index)] == mode) {
            return index;
        }
    }
    return 0;
}

HookMode InjectFeatureModeFromIndex(const int index) noexcept {
    const auto modes = BuildInjectFeatureModes();
    if (index < 0 || index >= static_cast<int>(modes.size())) {
        return HookMode::observe_only;
    }
    return modes[static_cast<std::size_t>(index)];
}

}  // namespace pal4::inject
