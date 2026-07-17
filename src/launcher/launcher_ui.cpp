#include "launcher_ui.h"

#include <algorithm>
#include <array>
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
    inject_features,
    advanced,
};

struct LauncherViewState {
    LauncherUiState* launcher = nullptr;
    CheckForUpdatesCallback check_for_updates = nullptr;
    LauncherPage page = LauncherPage::game;
    bool keep_open = true;
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
    GameDisplayConfig* const config,
    const std::vector<Resolution>& resolutions) {
    const std::string preview =
        std::to_string(config->width) + " × " + std::to_string(config->height);
    ImGui::SetNextItemWidth(240.0F);
    if (ImGui::BeginCombo(label, preview.c_str())) {
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

void DrawGamePage(LauncherUiState* const state) {
    ImGui::TextUnformatted("游戏设置");
    ImGui::TextDisabled("这些选项写入游戏 config.cfg，并在启动前生效。");
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
    ImGui::TextUnformatted("分辨率");
    DrawResolutionCombo("常用分辨率", &state->display, state->common_resolutions);
    DrawResolutionCombo("显示器支持", &state->display, state->display_resolutions);
    ImGui::SetNextItemWidth(140.0F);
    ImGui::InputInt("宽度", &state->display.width, 0, 0);
    ImGui::SetNextItemWidth(140.0F);
    ImGui::InputInt("高度", &state->display.height, 0, 0);
    state->display.width = std::clamp(state->display.width, 320, 20000);
    state->display.height = std::clamp(state->display.height, 240, 20000);

    bool fullscreen = state->display.fullscreen != 0;
    bool widescreen = state->display.widescreen != 0;
    bool vsync = state->display.sync != 0;
    if (ImGui::Checkbox("全屏运行", &fullscreen)) {
        state->display.fullscreen = fullscreen ? 1 : 0;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("启用宽屏", &widescreen)) {
        state->display.widescreen = widescreen ? 1 : 0;
        ApplyWidescreenFeaturePreset(&state->inject_settings, widescreen);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("垂直同步", &vsync)) {
        state->display.sync = vsync ? 1 : 0;
    }

    ImGui::Spacing();
    ImGui::BeginChild("game_paths", ImVec2(0.0F, 105.0F), ImGuiChildFlags_Borders);
    DrawPathRow("游戏程序", state->game_exe);
    DrawPathRow("游戏配置", state->config_path);
    DrawPathRow("运行库", state->runtime_dll);
    ImGui::EndChild();
}

void ToggleFeature(PersistedHookSetting* setting, bool enabled);

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

void DrawPlayerEnhancements(LauncherUiState* const state) {
    const bool widescreen_enabled = state->display.widescreen != 0;
    ImGui::TextUnformatted("宽屏修正");
    ImGui::SameLine(180.0F);
    ImGui::TextColored(
        widescreen_enabled
            ? ImVec4(0.36F, 0.82F, 0.48F, 1.0F)
            : ImVec4(0.66F, 0.66F, 0.66F, 1.0F),
        widescreen_enabled ? "已跟随游戏设置启用" : "已跟随游戏设置关闭");
    ImGui::TextDisabled(
        "统一控制宽屏 UI、字体、小地图、战斗界面和 Bink 视频修正。");

    ImGui::Spacing();
    DrawBinkScalingSetting(state);
    DrawMsaaSetting(state);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("其他增强");
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

void DrawSidebar(LauncherViewState* const view, const HWND owner) {
    ImGui::TextUnformatted("PAL4 Inject");
    ImGui::TextDisabled("%s", kPal4InjectVersion);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Selectable("游戏设置", view->page == LauncherPage::game, 0, ImVec2(0.0F, 42.0F))) {
        view->page = LauncherPage::game;
    }
    if (ImGui::Selectable("增强功能", view->page == LauncherPage::inject_features, 0, ImVec2(0.0F, 42.0F))) {
        view->page = LauncherPage::inject_features;
    }
    if (ImGui::Selectable("高级调试", view->page == LauncherPage::advanced, 0, ImVec2(0.0F, 42.0F))) {
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
    case LauncherPage::inject_features:
        ImGui::TextUnformatted("增强功能");
        ImGui::TextDisabled("成熟宽屏修正统一跟随“启用宽屏”；逐项调试仍保留在高级页。");
        ImGui::Separator();
        DrawPlayerEnhancements(view->launcher);
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
    return RunImGuiHost(
        L"PAL4 注入启动器",
        1060,
        720,
        &DrawLauncherFrame,
        &view,
        error);
}

}  // namespace pal4::inject::launcher
