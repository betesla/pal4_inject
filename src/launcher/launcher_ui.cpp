#include "launcher_ui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>

#include "imgui.h"
#include "imgui_host.h"
#include "pal4inject/inject_feature_catalog.h"
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

struct LauncherViewState {
    LauncherUiState* launcher = nullptr;
    CheckForUpdatesCallback check_for_updates = nullptr;
    OpenBugReportCallback open_bug_report = nullptr;
    LauncherPage page = LauncherPage::game;
    bool keep_open = true;
    std::array<char, 256> bug_title{};
    std::array<char, 2048> bug_description{};
    bool include_crash_report = false;
    bool include_runtime_log = false;
    bool diagnostics_consent = false;
    std::string bug_report_status;
};

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
    bool widescreen = state->display.widescreen != 0;
    bool vsync = state->display.sync != 0;
    ImGui::TextUnformatted("画面选项");
    ImGui::SameLine(135.0F);
    if (ImGui::Checkbox("启用宽屏", &widescreen)) {
        state->display.widescreen = widescreen ? 1 : 0;
        ApplyWidescreenFeaturePreset(&state->inject_settings, widescreen);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("垂直同步", &vsync)) {
        state->display.sync = vsync ? 1 : 0;
    }
}

void DrawDisplaySettings(LauncherUiState* const state) {
    ImGui::BeginChild("display_settings", ImVec2(0.0F, 270.0F), ImGuiChildFlags_Borders);
    DrawDisplayModeSetting(state);
    DrawMonitorSetting(state);
    DrawResolutionCombo(
        "常用分辨率", "##common_resolution",
        &state->display, state->common_resolutions);
    DrawResolutionCombo(
        "显示器支持", "##monitor_resolution",
        &state->display, state->display_resolutions);
    DrawCustomResolution(state);
    DrawVisualOptions(state);
    ImGui::Spacing();
    ImGui::TextDisabled(
        "无边框模式使用所选显示器的完整区域，并同步该显示器的当前分辨率。");
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
    ImGui::TextDisabled(
        "完整显示会保留全部画面；宽屏铺满保持比例并裁掉超出屏幕的上下部分。");
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
    const bool widescreen_enabled = state->display.widescreen != 0;
    ImGui::SameLine(180.0F);
    ImGui::TextColored(
        widescreen_enabled
            ? ImVec4(0.36F, 0.82F, 0.48F, 1.0F)
            : ImVec4(0.66F, 0.66F, 0.66F, 1.0F),
        widescreen_enabled ? "宽屏修正已启用" : "宽屏修正未启用");
    DrawBinkScalingSetting(state);
    DrawMsaaSetting(state);
}

void DrawAudioPage() {
    ImGui::TextUnformatted("音频");
    ImGui::TextDisabled("声音输出与音量控制。");
    ImGui::Separator();
    ImGui::BeginChild("audio_status", ImVec2(0.0F, 120.0F), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("沿用原游戏音频设置");
    ImGui::TextDisabled("当前版本尚未接管音乐、语音和音效音量；此页为后续音频功能保留。");
    ImGui::EndChild();
}

void DrawControlsPage() {
    ImGui::TextUnformatted("控制");
    ImGui::TextDisabled("键盘、鼠标与输入兼容设置。");
    ImGui::Separator();
    ImGui::BeginChild("controls_status", ImVec2(0.0F, 145.0F), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("沿用原游戏键位");
    ImGui::TextDisabled("当前版本保留原版 DirectInput 行为，暂不提供玩家键位重映射。");
    ImGui::Spacing();
    ImGui::TextDisabled("调试用输入注入和自动化仍保留在 CLI 与高级调试能力中。");
    ImGui::EndChild();
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
        ImGuiChildFlags_Borders,
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

void DrawSidebar(LauncherViewState* const view, const HWND owner) {
    ImGui::TextUnformatted("PAL4 Inject");
    ImGui::TextDisabled("%s", kPal4InjectVersion);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Selectable("游戏", view->page == LauncherPage::game, 0, ImVec2(0.0F, 38.0F))) {
        view->page = LauncherPage::game;
    }
    if (ImGui::Selectable("视频", view->page == LauncherPage::video, 0, ImVec2(0.0F, 38.0F))) {
        view->page = LauncherPage::video;
    }
    if (ImGui::Selectable("音频", view->page == LauncherPage::audio, 0, ImVec2(0.0F, 38.0F))) {
        view->page = LauncherPage::audio;
    }
    if (ImGui::Selectable("控制", view->page == LauncherPage::controls, 0, ImVec2(0.0F, 38.0F))) {
        view->page = LauncherPage::controls;
    }
    if (ImGui::Selectable("增强", view->page == LauncherPage::enhancements, 0, ImVec2(0.0F, 38.0F))) {
        view->page = LauncherPage::enhancements;
    }
    const char* const bug_report_label = view->launcher->bug_report.HasCrashReport()
        ? "反馈 Bug（发现崩溃）"
        : "反馈 Bug";
    if (ImGui::Selectable(
            bug_report_label,
            view->page == LauncherPage::bug_report,
            0,
            ImVec2(0.0F, 38.0F))) {
        view->page = LauncherPage::bug_report;
    }
    if (ImGui::Selectable("高级", view->page == LauncherPage::advanced, 0, ImVec2(0.0F, 38.0F))) {
        view->page = LauncherPage::advanced;
    }

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

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(
        "PAL4 Inject Launcher",
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
    switch (view->page) {
    case LauncherPage::game:
        DrawGamePage(view->launcher);
        break;
    case LauncherPage::video:
        DrawVideoPage(view->launcher);
        break;
    case LauncherPage::audio:
        DrawAudioPage();
        break;
    case LauncherPage::controls:
        DrawControlsPage();
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
    ImGui::SameLine(ImGui::GetWindowWidth() - 250.0F);
    if (ImGui::Button("退出", ImVec2(90.0F, 38.0F))) {
        view->keep_open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("启动游戏", ImVec2(130.0F, 38.0F))) {
        view->launcher->accepted = true;
        view->keep_open = false;
    }
    ImGui::End();
    return view->keep_open;
}

}  // namespace

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
        "[Bug] PAL4 Inject %s 崩溃反馈",
        kPal4InjectVersion);
    return RunImGuiHost(
        L"PAL4 注入启动器",
        1060,
        720,
        &DrawLauncherFrame,
        &view,
        error);
}

}  // namespace pal4::inject::launcher
