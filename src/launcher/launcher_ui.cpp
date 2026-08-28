#include "launcher_ui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_host.h"
#include "pal4inject/inject_feature_catalog.h"
#include "pal4inject/product_identity.h"
#include "pal4inject/ui_coordinate_space.h"
#include "pal4inject_build_info.h"

namespace pal4::inject::launcher {
namespace {

enum class LauncherPage {
    game = 0,
    video,
    audio,
    controls,
    enhancements,
    bug_report,
    advanced,
};

enum class LauncherNavigationRegion {
    sidebar = 0,
    content,
};

struct LauncherViewState {
    LauncherUiState* launcher = nullptr;
    CheckForUpdatesCallback check_for_updates = nullptr;
    OpenBugReportCallback open_bug_report = nullptr;
    LauncherPage page = LauncherPage::game;
    LauncherNavigationRegion navigation_region =
        LauncherNavigationRegion::sidebar;
    bool focus_sidebar = false;
    bool focus_content = false;
    bool keep_open = true;
    std::array<char, 256> bug_title{};
    std::array<char, 2048> bug_description{};
    bool include_crash_report = false;
    bool include_runtime_log = false;
    bool diagnostics_consent = false;
    std::string bug_report_status;
};

LauncherPage StepLauncherPage(
    const LauncherPage page,
    const int delta) noexcept {
    constexpr int kPageCount =
        static_cast<int>(LauncherPage::advanced) + 1;
    int next = (static_cast<int>(page) + delta) % kPageCount;
    if (next < 0) {
        next += kPageCount;
    }
    return static_cast<LauncherPage>(next);
}

void FocusCurrentWindowFirstItem() {
    auto* const window = ImGui::GetCurrentWindow();
    ImGui::FocusWindow(window);
    ImGui::NavInitWindow(window, true);
    ImGui::SetNavCursorVisibleAfterMove();
}

void FocusLastSubmittedItem() {
    auto& context = *GImGui;
    if (context.LastItemData.ID == 0) {
        return;
    }
    auto* const window = ImGui::GetCurrentWindow();
    ImGui::SetNavWindow(window);
    ImGui::SetNavID(
        context.LastItemData.ID,
        window->DC.NavLayerCurrent,
        context.CurrentFocusScopeId,
        ImGui::WindowRectAbsToRel(window, context.LastItemData.NavRect));
    ImGui::SetNavCursorVisibleAfterMove();
}

void EnterLauncherContent(LauncherViewState* const view) {
    if (!view) {
        return;
    }
    view->navigation_region = LauncherNavigationRegion::content;
    view->focus_content = true;
    view->focus_sidebar = false;
}

void SelectLauncherSidebarPage(
    LauncherViewState* const view,
    const int delta) {
    if (!view) {
        return;
    }
    view->page = StepLauncherPage(view->page, delta);
    view->navigation_region = LauncherNavigationRegion::sidebar;
    view->focus_sidebar = true;
    view->focus_content = false;
}

void UpdateLauncherGlobalGamepadActions(LauncherViewState* const view) {
    if (!view || !view->launcher) {
        return;
    }
    const bool popup_open = ImGui::IsPopupOpen(
        nullptr,
        ImGuiPopupFlags_AnyPopupId |
            ImGuiPopupFlags_AnyPopupLevel);
    if (popup_open) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false)) {
        view->launcher->accepted = true;
        view->keep_open = false;
    } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false)) {
        view->keep_open = false;
    }
}

void UpdateLauncherGamepadHierarchy(LauncherViewState* const view) {
    if (!view) {
        return;
    }
    if (view->navigation_region == LauncherNavigationRegion::content) {
        const bool popup_open = ImGui::IsPopupOpen(
            nullptr,
            ImGuiPopupFlags_AnyPopupId |
                ImGuiPopupFlags_AnyPopupLevel);
        if (!popup_open &&
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
            view->navigation_region = LauncherNavigationRegion::sidebar;
            view->focus_sidebar = true;
            view->focus_content = false;
        }
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, false)) {
        SelectLauncherSidebarPage(view, -1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, false)) {
        SelectLauncherSidebarPage(view, 1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) {
        EnterLauncherContent(view);
    } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, false) ||
               ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false)) {
        view->focus_sidebar = true;
        view->focus_content = false;
    }
}

std::string Utf8FromWide(const std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        required,
        nullptr,
        nullptr);
    return result;
}

std::string PathText(const std::filesystem::path& path) {
    return Utf8FromWide(path.wstring());
}

PersistedHookSetting* FindHookSetting(
    InjectPersistedSettings* const settings,
    const HookId id) {
    if (!settings) {
        return nullptr;
    }
    const auto found = std::find_if(
        settings->hooks.begin(),
        settings->hooks.end(),
        [id](const PersistedHookSetting& setting) { return setting.id == id; });
    return found == settings->hooks.end() ? nullptr : &*found;
}

const char* MsaaLabel(const MsaaLevel level) {
    switch (level) {
    case MsaaLevel::off:
        return "关闭";
    case MsaaLevel::x2:
        return "2x";
    case MsaaLevel::x4:
        return "4x";
    case MsaaLevel::x8:
        return "8x";
    }
    return "关闭";
}

const char* BinkScalingModeLabel(const BinkScalingMode mode) {
    switch (mode) {
    case BinkScalingMode::fit:
        return "完整显示（保持全画面）";
    case BinkScalingMode::fill_width_crop:
        return "宽屏铺满（上下裁剪）";
    }
    return "完整显示（保持全画面）";
}

const char* DisplayModeLabel(const LauncherUiState& state) {
    if (state.inject_settings.borderless_window) {
        return "无边框窗口（推荐）";
    }
    return state.display.fullscreen != 0 ? "独占全屏" : "普通窗口";
}

const char* Xbox360ButtonLabel(const Xbox360Button button) {
    switch (button) {
    case Xbox360Button::a:
        return "A";
    case Xbox360Button::b:
        return "B";
    case Xbox360Button::x:
        return "X";
    case Xbox360Button::y:
        return "Y";
    case Xbox360Button::left_shoulder:
        return "LB";
    case Xbox360Button::right_shoulder:
        return "RB";
    case Xbox360Button::left_trigger:
        return "LT";
    case Xbox360Button::right_trigger:
        return "RT";
    case Xbox360Button::back:
        return "Back";
    case Xbox360Button::start:
        return "Start";
    case Xbox360Button::left_thumb:
        return "L3";
    case Xbox360Button::right_thumb:
        return "R3";
    case Xbox360Button::count:
        break;
    }
    return "?";
}

const char* GamepadActionLabel(const GamepadAction action) {
    switch (action) {
    case GamepadAction::none:
        return "无";
    case GamepadAction::confirm:
        return "确认 / 交互";
    case GamepadAction::cancel:
        return "Esc / 返回（UI / 战斗）";
    case GamepadAction::mouse_left:
        return "按住鼠标左键";
    case GamepadAction::run_toggle:
        return "切换走 / 跑";
    case GamepadAction::auto_forward:
        return "自动前进";
    case GamepadAction::map:
        return "小地图";
    case GamepadAction::switch_leader:
        return "切换领队";
    case GamepadAction::camera_distance_cycle:
        return "切换镜头距离";
    case GamepadAction::system_menu:
        return "整层菜单开关（兼容）";
    case GamepadAction::main_page_previous:
        return "3D：M / UI：上一主分页";
    case GamepadAction::main_page_next:
        return "下一主分页";
    case GamepadAction::sub_page_previous:
        return "上一子分页";
    case GamepadAction::sub_page_next:
        return "下一子分页";
    case GamepadAction::place_marker:
        return "3D：V 路标 / 战斗：攻击";
    case GamepadAction::maze_skill:
        return "3D：C 迷宫技能 / 战斗：防御";
    case GamepadAction::role_page:
        return "F1 / 人物状态";
    case GamepadAction::item_page:
        return "F2 / 物品";
    case GamepadAction::equipment_page:
        return "F3 / 装备";
    case GamepadAction::magic_page:
        return "F4 / 仙术特技";
    case GamepadAction::system_page:
        return "3D：F7 / UI：无";
    case GamepadAction::combat_attack:
        return "战斗：攻击";
    case GamepadAction::combat_defend:
        return "战斗：防御";
    }
    return "无";
}

void DrawPathRow(const char* const label, const std::filesystem::path& path) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(105.0F);
    const auto text = PathText(path);
    ImGui::TextUnformatted(text.c_str());
}

void ApplyResolution(GameDisplayConfig* const config, const Resolution& resolution) {
    if (!config) {
        return;
    }
    config->width = resolution.width;
    config->height = resolution.height;
}

void DrawResolutionCombo(
    const char* const label,
    const char* const id,
    GameDisplayConfig* const config,
    const std::vector<Resolution>& resolutions) {
    const std::string preview =
        std::to_string(config->width) + " × " + std::to_string(config->height);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(135.0F);
    ImGui::SetNextItemWidth(520.0F);
    if (ImGui::BeginCombo(id, preview.c_str())) {
        for (const auto& resolution : resolutions) {
            const std::string item =
                std::to_string(resolution.width) + " × " + std::to_string(resolution.height);
            const bool selected =
                resolution.width == config->width && resolution.height == config->height;
            if (ImGui::Selectable(item.c_str(), selected)) {
                ApplyResolution(config, resolution);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void ToggleFeature(PersistedHookSetting* setting, bool enabled);

std::size_t SelectedMonitorIndex(const LauncherUiState& state) {
    const auto selected = std::find_if(
        state.monitors.begin(),
        state.monitors.end(),
        [&state](const MonitorDisplayInfo& monitor) {
            return monitor.device_name == state.inject_settings.borderless_monitor;
        });
    return selected == state.monitors.end()
        ? 0
        : static_cast<std::size_t>(selected - state.monitors.begin());
}

std::string MonitorLabel(const MonitorDisplayInfo& monitor, const std::size_t index) {
    std::string label = std::to_string(index + 1) + " · ";
    label += monitor.display_name.empty() ? monitor.device_name : monitor.display_name;
    label += " · " + std::to_string(monitor.width) + " × " +
        std::to_string(monitor.height);
    if (monitor.primary) {
        label += "（主显示器）";
    }
    return label;
}

void SelectMonitor(LauncherUiState* const state, const std::size_t index) {
    if (!state || index >= state->monitors.size()) {
        return;
    }
    const auto& monitor = state->monitors[index];
    state->inject_settings.borderless_monitor = monitor.device_name;
    state->display_resolutions = monitor.resolutions;
    if (monitor.width > 0 && monitor.height > 0) {
        state->display.width = monitor.width;
        state->display.height = monitor.height;
    }
}

void DrawHelpMarker(const char* const id, const char* const text) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.21F, 0.27F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28F, 0.34F, 0.44F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.34F, 0.42F, 0.54F, 1.0F));
    ImGui::SmallButton(id);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);

    if (hovered) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0F);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void DrawMonitorSetting(LauncherUiState* const state) {
    ImGui::TextUnformatted("目标显示器");
    ImGui::SameLine(135.0F);
    const bool enabled = state->inject_settings.borderless_window;
    if (!enabled) {
        ImGui::BeginDisabled();
    }
    const std::size_t selected_index = SelectedMonitorIndex(*state);
    const std::string preview = state->monitors.empty()
        ? "未检测到显示器"
        : MonitorLabel(state->monitors[selected_index], selected_index);
    ImGui::SetNextItemWidth(520.0F);
    if (ImGui::BeginCombo("##target_monitor", preview.c_str())) {
        for (std::size_t index = 0; index < state->monitors.size(); ++index) {
            const bool selected = index == selected_index;
            const auto label = MonitorLabel(state->monitors[index], index);
            if (ImGui::Selectable(label.c_str(), selected)) {
                SelectMonitor(state, index);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (!enabled) {
        ImGui::EndDisabled();
    }
}

void DrawDisplayModeSetting(LauncherUiState* const state) {
    ImGui::TextUnformatted("显示模式");
    ImGui::SameLine(0.0F, 6.0F);
    DrawHelpMarker(
        "?##display_mode_help",
        "普通窗口\n"
        "保留系统窗口边框，不切换桌面显示模式；可以手动拖到其他显示器，适合需要同时操作桌面程序的场景。\n\n"
        "无边框窗口（推荐）\n"
        "以窗口模式渲染并覆盖所选显示器，外观接近全屏。Alt+Tab 更快，也能减少老游戏切换显示模式时的黑屏和闪屏。\n\n"
        "独占全屏\n"
        "由原游戏独占显示设备并切换显示模式。Alt+Tab 可能较慢或出现闪屏；当前版本沿用游戏默认适配器，因此不能选择目标显示器。");
    ImGui::SameLine(135.0F);
    ImGui::SetNextItemWidth(520.0F);
    if (ImGui::BeginCombo("##display_mode", DisplayModeLabel(*state))) {
        const bool windowed =
            !state->inject_settings.borderless_window && state->display.fullscreen == 0;
        if (ImGui::Selectable("普通窗口", windowed)) {
            state->inject_settings.borderless_window = false;
            state->display.fullscreen = 0;
        }

        const bool borderless = state->inject_settings.borderless_window;
        if (ImGui::Selectable("无边框窗口（推荐）", borderless)) {
            state->inject_settings.borderless_window = true;
            state->display.fullscreen = 0;
            if (!state->monitors.empty()) {
                SelectMonitor(state, SelectedMonitorIndex(*state));
            }
            ToggleFeature(
                FindHookSetting(
                    &state->inject_settings,
                    HookId::d3d9_set_present_parameters),
                true);
        }

        const bool exclusive =
            !state->inject_settings.borderless_window && state->display.fullscreen != 0;
        if (ImGui::Selectable("独占全屏", exclusive)) {
            state->inject_settings.borderless_window = false;
            state->display.fullscreen = 1;
        }
        ImGui::EndCombo();
    }
}

void DrawCustomResolution(LauncherUiState* const state) {
    ImGui::TextUnformatted("自定义分辨率");
    ImGui::SameLine(135.0F);
    ImGui::SetNextItemWidth(140.0F);
    ImGui::InputInt("##custom_width", &state->display.width, 0, 0);
    ImGui::SameLine();
    ImGui::TextUnformatted("×");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0F);
    ImGui::InputInt("##custom_height", &state->display.height, 0, 0);
    state->display.width = std::clamp(state->display.width, 320, 20000);
    state->display.height = std::clamp(state->display.height, 240, 20000);
}

void DrawVisualOptions(LauncherUiState* const state) {
    bool vsync = state->display.sync != 0;
    ImGui::TextUnformatted("宽屏修正");
    ImGui::SameLine(135.0F);
    const bool widescreen = state->display.widescreen != 0;
    ImGui::TextColored(
        widescreen
            ? ImVec4(0.36F, 0.82F, 0.48F, 1.0F)
            : ImVec4(0.66F, 0.66F, 0.66F, 1.0F),
        widescreen ? "自动开启（宽于 4:3）" : "自动关闭（4:3 或更窄）");

    ImGui::TextUnformatted("画面选项");
    ImGui::SameLine(135.0F);
    if (ImGui::Checkbox("垂直同步", &vsync)) {
        state->display.sync = vsync ? 1 : 0;
    }
}

void DrawDisplaySettings(LauncherUiState* const state) {
    ImGui::BeginChild(
        "display_settings",
        ImVec2(0.0F, 300.0F),
        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    DrawDisplayModeSetting(state);
    DrawMonitorSetting(state);
    DrawResolutionCombo(
        "常用分辨率", "##common_resolution",
        &state->display, state->common_resolutions);
    DrawResolutionCombo(
        "显示器支持", "##monitor_resolution",
        &state->display, state->display_resolutions);
    DrawCustomResolution(state);
    SynchronizeAutomaticWidescreen(state);
    DrawVisualOptions(state);
    ImGui::Spacing();
    ImGui::TextDisabled(
        "宽屏修正随分辨率比例自动切换；无边框模式使用所选显示器的完整区域。");
    ImGui::EndChild();
}

void DrawGamePage(LauncherUiState* const state) {
    ImGui::TextUnformatted("游戏");
    ImGui::TextDisabled("游戏运行方式与本地文件位置。");
    ImGui::Separator();

    ImGui::TextUnformatted("脚本模式");
    int script_mode = state->script_mode == ScriptMode::cs ? 0 : 1;
    if (ImGui::RadioButton("CS 文本脚本", script_mode == 0)) {
        state->script_mode = ScriptMode::cs;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("CSB 原始脚本", script_mode == 1)) {
        state->script_mode = ScriptMode::csb;
    }
    ImGui::TextDisabled("CS 适合调试和快速迭代；普通游玩建议使用 CSB。");

    ImGui::Spacing();
    ImGui::TextUnformatted("运行文件");
    ImGui::BeginChild("game_paths", ImVec2(0.0F, 105.0F), ImGuiChildFlags_Borders);
    DrawPathRow("游戏程序", state->game_exe);
    DrawPathRow("游戏配置", state->config_path);
    DrawPathRow("运行库", state->runtime_dll);
    ImGui::EndChild();
}

void DrawBinkScalingSetting(LauncherUiState* const state) {
    ImGui::TextUnformatted("过场视频显示");
    ImGui::SameLine(0.0F, 6.0F);
    DrawHelpMarker(
        "?##bink_scaling_help",
        "完整显示（保持全画面）\n"
        "保持视频原始比例，完整显示全部画面，不拉伸也不裁剪。视频比例为 4:3 而屏幕为宽屏时，左右会保留黑边。\n\n"
        "宽屏铺满（上下裁剪）\n"
        "保持视频原始比例并放大到铺满屏幕宽度，不会横向拉伸；超出屏幕的上下画面会被裁剪。");
    ImGui::SameLine(180.0F);
    ImGui::SetNextItemWidth(240.0F);
    if (ImGui::BeginCombo(
            "##bink_scaling_mode",
            BinkScalingModeLabel(state->inject_settings.bink_scaling_mode))) {
        constexpr std::array modes{
            BinkScalingMode::fit,
            BinkScalingMode::fill_width_crop,
        };
        for (const auto mode : modes) {
            const bool selected = mode == state->inject_settings.bink_scaling_mode;
            if (ImGui::Selectable(BinkScalingModeLabel(mode), selected)) {
                state->inject_settings.bink_scaling_mode = mode;
                ToggleFeature(
                    FindHookSetting(
                        &state->inject_settings,
                        HookId::bink_player_update_and_render),
                    state->display.widescreen != 0);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void DrawMsaaSetting(LauncherUiState* const state) {
    ImGui::TextUnformatted("抗锯齿 MSAA");
    ImGui::SameLine(180.0F);
    ImGui::SetNextItemWidth(150.0F);
    if (ImGui::BeginCombo("##msaa", MsaaLabel(state->inject_settings.msaa_level))) {
        constexpr std::array levels{
            MsaaLevel::off,
            MsaaLevel::x2,
            MsaaLevel::x4,
            MsaaLevel::x8,
        };
        for (const auto level : levels) {
            const bool selected = level == state->inject_settings.msaa_level;
            if (ImGui::Selectable(MsaaLabel(level), selected)) {
                state->inject_settings.msaa_level = level;
                if (level != MsaaLevel::off) {
                    ToggleFeature(
                        FindHookSetting(
                            &state->inject_settings,
                            HookId::d3d9_set_present_parameters),
                        true);
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("这里保存的是期望等级；实际应用结果以 runtime 日志或 CLI 状态为准。");
}

void DrawVideoPage(LauncherUiState* const state) {
    ImGui::TextUnformatted("视频");
    ImGui::TextDisabled("显示设备、窗口模式、分辨率与渲染质量。");
    ImGui::Separator();
    DrawDisplaySettings(state);

    ImGui::Spacing();
    ImGui::TextUnformatted("渲染与过场");
    DrawBinkScalingSetting(state);
    DrawMsaaSetting(state);
}

void DrawAudioPage(LauncherUiState* const state) {
    ImGui::TextUnformatted("音频");
    ImGui::TextDisabled("声音输出与音量控制。");
    ImGui::Separator();
    ImGui::BeginChild(
        "audio_settings",
        ImVec2(0.0F, 155.0F),
        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    ImGui::TextUnformatted("对白语音音量");
    ImGui::SameLine(180.0F);
    ImGui::SetNextItemWidth(260.0F);
    float volume_percent = state->inject_settings.gi_talk_volume * 100.0F;
    if (ImGui::SliderFloat(
            "##gi_talk_volume", &volume_percent, 0.0F, 300.0F, "%.0f%%")) {
        state->inject_settings.gi_talk_volume = volume_percent / 100.0F;
    }
    ImGui::TextDisabled("只调整 giTalk 播放的 PALSOUND 对白配音。背景音乐和普通音效仍沿用原游戏设置。");
    ImGui::TextDisabled("游戏内快捷键：[ 降低 5%，] 提高 5%。");
    ImGui::TextDisabled("超过 100% 会放大原始配音，部分音频可能出现削波或失真。");
    ImGui::Spacing();
    ImGui::TextUnformatted("其他音频");
    ImGui::TextDisabled("音乐、普通音效及输出设备仍由原游戏配置控制。");
    ImGui::EndChild();
}

void DrawControlsPage(LauncherUiState* const state) {
    ImGui::TextUnformatted("控制");
    ImGui::TextDisabled("Xbox 360 手柄与输入兼容设置。");
    ImGui::Separator();

    bool enabled = state->inject_settings.gamepad_enabled;
    if (ImGui::Checkbox("启用 Xbox 360 / XInput 手柄", &enabled)) {
        state->inject_settings.gamepad_enabled = enabled;
        ToggleFeature(
            FindHookSetting(&state->inject_settings, HookId::process_inputs),
            enabled);
    }
    ImGui::TextDisabled("按键映射暂时固定；下表仅展示当前默认布局，不提供修改入口。");

    ImGui::BeginDisabled(!state->inject_settings.gamepad_enabled);
    ImGui::Checkbox(
        "现代摇杆控制",
        &state->inject_settings.gamepad_modern_controls);
    ImGui::TextDisabled("左摇杆切换走/跑，并在跑到快跑之间连续调节速度与动作；右摇杆旋转自由视角。关闭后左摇杆退回 W/A/S/D。 ");
    ImGui::BeginDisabled(!state->inject_settings.gamepad_modern_controls);
    float run_threshold_percent =
        state->inject_settings.gamepad_run_threshold * 100.0F;
    ImGui::SetNextItemWidth(260.0F);
    if (ImGui::SliderFloat(
            "跑步触发幅度",
            &run_threshold_percent,
            20.0F,
            90.0F,
            "%.0f%%")) {
        state->inject_settings.gamepad_run_threshold =
            run_threshold_percent / 100.0F;
        state->inject_settings.gamepad_fast_run_threshold = std::max(
            state->inject_settings.gamepad_fast_run_threshold,
            state->inject_settings.gamepad_run_threshold + 0.01F);
    }
    float fast_run_threshold_percent =
        state->inject_settings.gamepad_fast_run_threshold * 100.0F;
    ImGui::SetNextItemWidth(260.0F);
    if (ImGui::SliderFloat(
            "快跑触发幅度",
            &fast_run_threshold_percent,
            run_threshold_percent + 1.0F,
            100.0F,
            "%.0f%%")) {
        state->inject_settings.gamepad_fast_run_threshold =
            fast_run_threshold_percent / 100.0F;
    }
    ImGui::SetNextItemWidth(260.0F);
    ImGui::SliderFloat(
        "右摇杆灵敏度",
        &state->inject_settings.gamepad_camera_sensitivity,
        20.0F,
        360.0F,
        "%.0f 度/秒");
    ImGui::Checkbox(
        "反转右摇杆纵向",
        &state->inject_settings.gamepad_invert_camera_y);
    ImGui::Checkbox(
        "保持当前自由镜头（忽略脚本模式切换）",
        &state->inject_settings.gamepad_preserve_free_camera);
    ImGui::TextDisabled("实验选项：进入小场景时恢复切换前的镜头模式；可能影响剧情演出镜头。遇到异常请关闭。");
    ImGui::EndDisabled();
    ImGui::SeparatorText("Xbox 360 按钮映射（只读）");
    constexpr std::array buttons{
        Xbox360Button::a,
        Xbox360Button::b,
        Xbox360Button::x,
        Xbox360Button::y,
        Xbox360Button::left_shoulder,
        Xbox360Button::right_shoulder,
        Xbox360Button::left_trigger,
        Xbox360Button::right_trigger,
        Xbox360Button::back,
        Xbox360Button::start,
        Xbox360Button::left_thumb,
        Xbox360Button::right_thumb,
    };
    const auto mapping = DefaultXbox360GamepadMapping();
    if (ImGui::BeginTable(
            "gamepad_bindings",
            4,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("按键", ImGuiTableColumnFlags_WidthFixed, 52.0F);
        ImGui::TableSetupColumn("动作", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("按键", ImGuiTableColumnFlags_WidthFixed, 52.0F);
        ImGui::TableSetupColumn("动作", ImGuiTableColumnFlags_WidthStretch);
        for (std::size_t row = 0; row < buttons.size() / 2; ++row) {
            ImGui::TableNextRow();
            for (std::size_t pair = 0; pair < 2; ++pair) {
                const auto button = buttons[row + pair * (buttons.size() / 2)];
                ImGui::TableSetColumnIndex(static_cast<int>(pair * 2));
                ImGui::TextUnformatted(Xbox360ButtonLabel(button));
                ImGui::TableSetColumnIndex(static_cast<int>(pair * 2 + 1));
                ImGui::TextUnformatted(GamepadActionLabel(
                    GetGamepadBinding(mapping, button)));
            }
        }
        ImGui::EndTable();
    }
    ImGui::SeparatorText("摇杆与十字键（只读）");
    constexpr std::array directional_controls{
        std::pair{"左摇杆", "3D：移动 / 战斗：行动轮，回中选攻击"},
        std::pair{"右摇杆", "旋转镜头"},
        std::pair{"十字键 上", "3D：F1 人物状态 / UI：向上"},
        std::pair{"十字键 下", "3D：F4 仙术特技 / UI：向下"},
        std::pair{"十字键 左", "3D：F2 物品 / UI：向左"},
        std::pair{"十字键 右", "3D：F3 装备 / UI：向右"},
    };
    if (ImGui::BeginTable(
            "gamepad_directional_controls",
            2,
            ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("输入", ImGuiTableColumnFlags_WidthFixed, 90.0F);
        ImGui::TableSetupColumn("动作", ImGuiTableColumnFlags_WidthStretch);
        for (const auto& [input, action] : directional_controls) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(input);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(action);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::Checkbox(
        "记录手柄连接与上下文事件",
        &state->inject_settings.gamepad_log_enabled);
    ImGui::EndDisabled();
    ImGui::TextDisabled("当前仅轮询 0 号 XInput 设备；暂不支持震动和其他手柄布局。");
}

void ToggleFeature(PersistedHookSetting* const setting, const bool enabled) {
    if (!setting) {
        return;
    }
    if (enabled) {
        setting->mode = setting->active_mode == HookMode::observe_only
            ? HookMode::replace_with_fallback
            : setting->active_mode;
    } else {
        if (setting->mode != HookMode::observe_only) {
            setting->active_mode = setting->mode;
        }
        setting->mode = HookMode::observe_only;
    }
}

void DrawFeatureCard(
    LauncherUiState* const state,
    const InjectFeatureDescriptor& feature) {
    auto* setting = FindHookSetting(&state->inject_settings, feature.id);
    if (!setting) {
        return;
    }

    ImGui::PushID(static_cast<int>(feature.id));
    ImGui::BeginChild(
        "feature",
        ImVec2(0.0F, 86.0F),
        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    bool enabled = setting->mode != HookMode::observe_only;
    if (ImGui::Checkbox("##enabled", &enabled)) {
        ToggleFeature(setting, enabled);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(feature.label.data(), feature.label.data() + feature.label.size());
    ImGui::TextDisabled("%.*s", static_cast<int>(feature.description.size()), feature.description.data());
    ImGui::EndChild();
    ImGui::PopID();
}

void DrawEnhancementsPage(LauncherUiState* const state) {
    ImGui::TextUnformatted("增强");
    ImGui::TextDisabled("不属于原游戏设置的兼容修正与资源覆盖功能。");
    ImGui::Separator();
    const auto features = BuildInjectFeatureCatalog();
    for (const auto& feature : features) {
        if (feature.id == HookId::camera_update_matrix ||
            feature.id == HookId::loose_file_overlay) {
            DrawFeatureCard(state, feature);
        }
    }
    const auto gamepatch_path = PathText(state->game_exe.parent_path() / "gamepatch");
    ImGui::TextDisabled(
        "松散文件目录：%s（CPK 资源与 CS 脚本共用）",
        gamepatch_path.c_str());
    ImGui::TextDisabled("CS 模式只读取 gamepatch；缺少脚本时不会回退 editData。");
    ImGui::TextDisabled("独立日志：gamepatch\\loose_file_load.log（JSON Lines）");
}

void DrawAdvancedPage(LauncherUiState* const state) {
    ImGui::TextUnformatted("高级调试");
    ImGui::TextDisabled("Hook 模式和逐项日志主要面向逆向开发；普通游玩无需修改。");
    ImGui::Separator();

    if (ImGui::BeginTable(
            "advanced_hooks",
            4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0.0F, -1.0F))) {
        ImGui::TableSetupColumn("功能", ImGuiTableColumnFlags_WidthStretch, 0.34F);
        ImGui::TableSetupColumn("内部 ID", ImGuiTableColumnFlags_WidthStretch, 0.25F);
        ImGui::TableSetupColumn("模式", ImGuiTableColumnFlags_WidthStretch, 0.28F);
        ImGui::TableSetupColumn("日志", ImGuiTableColumnFlags_WidthFixed, 64.0F);
        ImGui::TableHeadersRow();

        const auto features = BuildInjectFeatureCatalog();
        const auto modes = BuildInjectFeatureModes();
        for (const auto& feature : features) {
            auto* setting = FindHookSetting(&state->inject_settings, feature.id);
            if (!setting) {
                continue;
            }
            ImGui::PushID(static_cast<int>(feature.id));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(feature.label.data(), feature.label.data() + feature.label.size());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", ToString(feature.id));
            ImGui::TableSetColumnIndex(2);
            const auto current_label = InjectFeatureModeLabel(setting->mode);
            if (feature.allow_mode_change) {
                ImGui::SetNextItemWidth(-1.0F);
                if (ImGui::BeginCombo("##mode", current_label.data())) {
                    for (const auto mode : modes) {
                        const auto mode_label = InjectFeatureModeLabel(mode);
                        const bool selected = mode == setting->mode;
                        if (ImGui::Selectable(mode_label.data(), selected)) {
                            setting->mode = mode;
                            if (mode != HookMode::observe_only) {
                                setting->active_mode = mode;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextDisabled("%.*s", static_cast<int>(current_label.size()), current_label.data());
            }
            ImGui::TableSetColumnIndex(3);
            if (feature.allow_log_change) {
                ImGui::Checkbox("##log", &setting->log_enabled);
            } else {
                ImGui::TextDisabled("独立文件");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

std::string BuildVisibleBugReportBody(const LauncherViewState& view) {
    BugReportBodyOptions options{};
    options.description = view.bug_description.data();
    options.version = kPal4InjectVersion;
    options.build_id = kPal4InjectBuildId;
    options.include_crash_report = view.include_crash_report;
    options.include_runtime_log = view.include_runtime_log;
    return BuildBugReportBody(view.launcher->bug_report, options);
}

void DrawBugReportPage(LauncherViewState* const view, const HWND owner) {
    auto& report = view->launcher->bug_report;
    ImGui::TextUnformatted("反馈 Bug");
    ImGui::TextDisabled("整理诊断信息并打开预填的 Gitee Issue；最终提交仍由你在网页中确认。");
    ImGui::Separator();

    ImGui::TextUnformatted("Issue 标题");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputText("##bug_title", view->bug_title.data(), view->bug_title.size());
    ImGui::TextUnformatted("问题现象与复现步骤");
    ImGui::InputTextMultiline(
        "##bug_description",
        view->bug_description.data(),
        view->bug_description.size(),
        ImVec2(-1.0F, 65.0F));

    ImGui::Spacing();
    ImGui::TextUnformatted("诊断信息（提交前可预览）");
    if (report.HasCrashReport()) {
        ImGui::Checkbox("附带最新崩溃文本（已脱敏）", &view->include_crash_report);
        ImGui::TextDisabled("文件：%s", PathText(report.crash_report_path.filename()).c_str());
    } else {
        ImGui::TextDisabled("未检测到崩溃文本报告。");
    }
    if (report.HasRuntimeLog()) {
        ImGui::Checkbox("附带运行日志末尾（已脱敏）", &view->include_runtime_log);
    }
    if (!report.crash_dump_path.empty()) {
        ImGui::TextDisabled("检测到 minidump，但第一版不会读取或上传该文件。");
    }

    const bool includes_diagnostics = view->include_crash_report || view->include_runtime_log;
    if (includes_diagnostics) {
        ImGui::Checkbox(
            "我已检查下方预览，并同意将勾选的诊断信息发送给 Gitee",
            &view->diagnostics_consent);
    } else {
        view->diagnostics_consent = false;
    }

    const std::string body = BuildVisibleBugReportBody(*view);
    ImGui::TextUnformatted("提交内容预览");
    ImGui::BeginChild("bug_report_preview", ImVec2(0.0F, 60.0F), ImGuiChildFlags_Borders);
    ImGui::TextWrapped("%s", body.c_str());
    ImGui::EndChild();

    const bool can_open = view->bug_title[0] != '\0' &&
        (!includes_diagnostics || view->diagnostics_consent);
    ImGui::BeginDisabled(!can_open);
    if (ImGui::Button("复制内容并打开 Gitee", ImVec2(220.0F, 38.0F)) &&
        view->open_bug_report) {
        std::wstring error;
        if (view->open_bug_report(owner, view->bug_title.data(), body, &error)) {
            view->bug_report_status =
                "已打开 Gitee，并把完整内容复制到剪贴板。请检查网页内容后手动提交。";
        } else {
            view->bug_report_status = "打开失败：" + Utf8FromWide(error);
        }
    }
    ImGui::EndDisabled();
    if (!can_open && includes_diagnostics) {
        ImGui::SameLine();
        ImGui::TextDisabled("请先确认诊断信息授权");
    }
    if (!view->bug_report_status.empty()) {
        ImGui::TextWrapped("%s", view->bug_report_status.c_str());
    }
}

void DrawSidebarPageItem(
    LauncherViewState* const view,
    const char* const label,
    const LauncherPage page) {
    if (ImGui::Selectable(
            label,
            view->page == page,
            0,
            ImVec2(0.0F, 38.0F))) {
        view->page = page;
        EnterLauncherContent(view);
    }
    if (view->focus_sidebar && view->page == page) {
        FocusLastSubmittedItem();
        view->focus_sidebar = false;
    }
}

void DrawSidebar(LauncherViewState* const view, const HWND owner) {
    ImGui::TextUnformatted("PAL4Plus");
    ImGui::SameLine();
    ImGui::TextDisabled("P4P");
    ImGui::TextDisabled("%s", kPal4InjectVersion);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    DrawSidebarPageItem(view, "游戏", LauncherPage::game);
    DrawSidebarPageItem(view, "视频", LauncherPage::video);
    DrawSidebarPageItem(view, "音频", LauncherPage::audio);
    DrawSidebarPageItem(view, "控制", LauncherPage::controls);
    DrawSidebarPageItem(view, "增强", LauncherPage::enhancements);
    const char* const bug_report_label = view->launcher->bug_report.HasCrashReport()
        ? "反馈 Bug（发现崩溃）"
        : "反馈 Bug";
    DrawSidebarPageItem(view, bug_report_label, LauncherPage::bug_report);
    DrawSidebarPageItem(view, "高级", LauncherPage::advanced);

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 86.0F);
    if (ImGui::Button("检查更新", ImVec2(-1.0F, 34.0F)) && view->check_for_updates) {
        view->check_for_updates(owner, false);
    }
    ImGui::TextDisabled("build %s", kPal4InjectBuildId);
}

bool DrawLauncherFrame(const HWND hwnd, void* const context) {
    auto* const view = static_cast<LauncherViewState*>(context);
    if (!view || !view->launcher) {
        return false;
    }
    UpdateLauncherGlobalGamepadActions(view);
    UpdateLauncherGamepadHierarchy(view);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(
        "PAL4Plus (P4P) Launcher",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings);

    ImGui::BeginChild("sidebar", ImVec2(190.0F, -58.0F), ImGuiChildFlags_Borders);
    DrawSidebar(view, hwnd);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("content", ImVec2(0.0F, -58.0F), ImGuiChildFlags_Borders);
    if (view->focus_content) {
        FocusCurrentWindowFirstItem();
        view->focus_content = false;
    }
    switch (view->page) {
    case LauncherPage::game:
        DrawGamePage(view->launcher);
        break;
    case LauncherPage::video:
        DrawVideoPage(view->launcher);
        break;
    case LauncherPage::audio:
        DrawAudioPage(view->launcher);
        break;
    case LauncherPage::controls:
        DrawControlsPage(view->launcher);
        break;
    case LauncherPage::enhancements:
        DrawEnhancementsPage(view->launcher);
        break;
    case LauncherPage::bug_report:
        DrawBugReportPage(view, hwnd);
        break;
    case LauncherPage::advanced:
        DrawAdvancedPage(view->launcher);
        break;
    }
    ImGui::EndChild();

    ImGui::Separator();
    const auto settings_path = PathText(view->launcher->inject_settings_path);
    ImGui::TextDisabled("配置：%s", settings_path.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 310.0F);
    if (ImGui::Button("退出 [Back]", ImVec2(128.0F, 38.0F))) {
        view->keep_open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("启动游戏 [Start]", ImVec2(164.0F, 38.0F))) {
        view->launcher->accepted = true;
        view->keep_open = false;
    }
    ImGui::End();
    return view->keep_open;
}

}  // namespace

void SynchronizeAutomaticWidescreen(LauncherUiState* const state) noexcept {
    if (!state) {
        return;
    }
    const bool enabled = IsWideAspectResolution(
        state->display.width,
        state->display.height);
    state->display.widescreen = enabled ? 1 : 0;
    ApplyWidescreenFeaturePreset(&state->inject_settings, enabled);
}

bool RunLauncherUi(
    LauncherUiState* const state,
    const CheckForUpdatesCallback check_for_updates,
    const OpenBugReportCallback open_bug_report,
    std::wstring* const error) {
    if (!state) {
        if (error) {
            *error = L"Launcher UI state is null.";
        }
        return false;
    }
    LauncherViewState view{};
    view.launcher = state;
    view.check_for_updates = check_for_updates;
    view.open_bug_report = open_bug_report;
    view.page = LauncherPage::game;
    view.include_crash_report = state->bug_report.HasCrashReport();
    std::snprintf(
        view.bug_title.data(),
        view.bug_title.size(),
        "[Bug] PAL4Plus (P4P) %s 崩溃反馈",
        kPal4InjectVersion);
    return RunImGuiHost(
        kLauncherWindowTitle,
        1060,
        720,
        &DrawLauncherFrame,
        &view,
        error);
}

}  // namespace pal4::inject::launcher
