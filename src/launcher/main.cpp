#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include "update_installer.h"

#include "launcher_ui.h"
#include "pal4inject/bug_report.h"
#include "pal4inject/hook_inventory.h"
#include "pal4inject/inject_feature_catalog.h"
#include "pal4inject/inject_settings.h"
#include "pal4inject/launcher.h"
#include "pal4inject/product_identity.h"
#include "pal4inject/runtime_paths.h"
#include "pal4inject_build_info.h"

namespace {

using Resolution = pal4::inject::launcher::Resolution;
using GameConfig = pal4::inject::launcher::GameDisplayConfig;
using LauncherUiState = pal4::inject::launcher::LauncherUiState;
using MonitorDisplayInfo = pal4::inject::launcher::MonitorDisplayInfo;

constexpr const char kGiteeNewIssueUrl[] =
    "https://gitee.com/betesla/pal4_inject/issues/new";


std::filesystem::path CurrentExecutableDirectory() {
    wchar_t buffer[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
    return length == 0 || length >= 32768
        ? std::filesystem::current_path()
        : std::filesystem::path(std::wstring(buffer, length)).parent_path();
}

std::filesystem::path DefaultRuntimeDllPath() {
    const auto executable_directory = CurrentExecutableDirectory();
    const auto packaged_dll = executable_directory / "pal4_inject" / "runtime.dll";
    return std::filesystem::exists(packaged_dll)
        ? packaged_dll
        : executable_directory / "runtime.dll";
}

void PrintUsage() {
    std::cout
        << "Usage: " << pal4::inject::kLauncherExecutableName
        << " (--game-root <path> | --exe <path>) "
        << "[--dll <path>] [--script-mode cs|csb] [--ready-timeout-ms <ms>] "
           "[--no-resume] [--background]\n";
}

void ShowGuiError(const std::wstring& error) {
    MessageBoxW(
        nullptr,
        error.empty() ? L"启动失败。" : error.c_str(),
        pal4::inject::kLauncherWindowTitle,
        MB_ICONERROR | MB_OK);
}

std::wstring WideFromUtf8(const std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (required <= 0) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        required);
    return result;
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

bool CopyUtf8TextToClipboard(
    const HWND owner,
    const std::string_view text,
    std::wstring* const error) {
    const std::wstring wide_text = WideFromUtf8(text);
    if (!OpenClipboard(owner)) {
        if (error) {
            *error = L"无法打开剪贴板。";
        }
        return false;
    }
    EmptyClipboard();
    const std::size_t bytes = (wide_text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        if (error) {
            *error = L"无法分配剪贴板内存。";
        }
        return false;
    }
    void* const destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        CloseClipboard();
        if (error) {
            *error = L"无法写入剪贴板。";
        }
        return false;
    }
    std::memcpy(destination, wide_text.c_str(), bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        if (error) {
            *error = L"无法保存剪贴板内容。";
        }
        return false;
    }
    CloseClipboard();
    return true;
}

bool OpenBugReport(
    const HWND owner,
    const std::string& title,
    const std::string& body,
    std::wstring* const error) {
    if (!CopyUtf8TextToClipboard(owner, body, error)) {
        return false;
    }
    const std::string issue_url = pal4::inject::BuildGiteeNewIssueUrl(
        kGiteeNewIssueUrl,
        title,
        body);
    const auto shell_result = reinterpret_cast<std::intptr_t>(ShellExecuteW(
        owner,
        L"open",
        WideFromUtf8(issue_url).c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL));
    if (shell_result <= 32) {
        if (error) {
            *error = L"无法打开 Gitee Issue 页面。完整报告仍保留在剪贴板中。";
        }
        return false;
    }
    return true;
}

std::vector<pal4::inject::BugReportRedaction> BuildBugReportRedactions(
    const std::filesystem::path& install_directory) {
    std::vector<pal4::inject::BugReportRedaction> redactions;
    if (!install_directory.empty()) {
        redactions.emplace_back(install_directory.string(), "<游戏目录>");
    }
    std::wstring user_profile(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        L"USERPROFILE",
        user_profile.data(),
        static_cast<DWORD>(user_profile.size()));
    if (length != 0 && length < user_profile.size()) {
        user_profile.resize(length);
        const auto utf8_profile = Utf8FromWide(user_profile);
        if (!utf8_profile.empty()) {
            redactions.emplace_back(utf8_profile, "<用户目录>");
        }
        const auto native_profile = std::filesystem::path(user_profile).string();
        if (!native_profile.empty() && native_profile != utf8_profile) {
            redactions.emplace_back(native_profile, "<用户目录>");
        }
    }
    return redactions;
}

std::vector<Resolution> BuildCommonResolutions() {
    return {
        {800, 600},
        {1024, 768},
        {1280, 720},
        {1280, 800},
        {1366, 768},
        {1600, 900},
        {1920, 1080},
        {2560, 1440},
        {3440, 1440},
        {3840, 2160},
        {5120, 2160},
    };
}

std::vector<Resolution> EnumerateDisplayResolutions(const wchar_t* const device_name) {
    std::set<Resolution> unique;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    for (DWORD index = 0; EnumDisplaySettingsW(device_name, index, &mode); ++index) {
        const auto width = static_cast<int>(mode.dmPelsWidth);
        const auto height = static_cast<int>(mode.dmPelsHeight);
        if (width >= 800 && height >= 600) {
            unique.insert({width, height});
        }
    }
    return {unique.begin(), unique.end()};
}

BOOL CALLBACK AppendMonitorDisplayInfo(
    const HMONITOR monitor,
    HDC,
    LPRECT,
    const LPARAM parameter) {
    auto* monitors = reinterpret_cast<std::vector<MonitorDisplayInfo>*>(parameter);
    if (!monitors) {
        return FALSE;
    }
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return TRUE;
    }

    DISPLAY_DEVICEW display{};
    display.cb = sizeof(display);
    const bool has_display_name =
        EnumDisplayDevicesW(info.szDevice, 0, &display, 0) != FALSE;

    MonitorDisplayInfo entry{};
    entry.device_name = Utf8FromWide(info.szDevice);
    entry.display_name = has_display_name
        ? Utf8FromWide(display.DeviceString)
        : entry.device_name;
    entry.x = info.rcMonitor.left;
    entry.y = info.rcMonitor.top;
    entry.width = info.rcMonitor.right - info.rcMonitor.left;
    entry.height = info.rcMonitor.bottom - info.rcMonitor.top;
    entry.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    entry.resolutions = EnumerateDisplayResolutions(info.szDevice);
    monitors->push_back(std::move(entry));
    return TRUE;
}

std::vector<MonitorDisplayInfo> EnumerateMonitorDisplays() {
    std::vector<MonitorDisplayInfo> monitors;
    EnumDisplayMonitors(
        nullptr,
        nullptr,
        &AppendMonitorDisplayInfo,
        reinterpret_cast<LPARAM>(&monitors));
    std::stable_sort(
        monitors.begin(),
        monitors.end(),
        [](const MonitorDisplayInfo& left, const MonitorDisplayInfo& right) {
            if (left.primary != right.primary) {
                return left.primary;
            }
            return left.x != right.x ? left.x < right.x : left.y < right.y;
        });
    return monitors;
}

const MonitorDisplayInfo* ResolveConfiguredMonitor(const LauncherUiState& state) {
    const auto selected = std::find_if(
        state.monitors.begin(),
        state.monitors.end(),
        [&state](const MonitorDisplayInfo& monitor) {
            return monitor.device_name == state.inject_settings.borderless_monitor;
        });
    if (selected != state.monitors.end()) {
        return &*selected;
    }
    const auto primary = std::find_if(
        state.monitors.begin(),
        state.monitors.end(),
        [](const MonitorDisplayInfo& monitor) { return monitor.primary; });
    return primary != state.monitors.end()
        ? &*primary
        : (state.monitors.empty() ? nullptr : &state.monitors.front());
}

std::optional<std::size_t> FindConfigValueOffset(
    const std::vector<std::uint8_t>& data,
    const char* const key) {
    const auto key_length = std::strlen(key);
    if (key_length == 0 || data.size() < key_length + 4) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index + key_length + 4 <= data.size(); ++index) {
        if (std::memcmp(data.data() + index, key, key_length) == 0) {
            return index + key_length;
        }
    }
    return std::nullopt;
}

int ReadConfigInt(
    const std::vector<std::uint8_t>& data,
    const char* const key,
    const int fallback) {
    const auto offset = FindConfigValueOffset(data, key);
    if (!offset || *offset + 4 > data.size()) {
        return fallback;
    }
    const auto base = *offset;
    return static_cast<int>(
        static_cast<std::uint32_t>(data[base]) |
        (static_cast<std::uint32_t>(data[base + 1]) << 8U) |
        (static_cast<std::uint32_t>(data[base + 2]) << 16U) |
        (static_cast<std::uint32_t>(data[base + 3]) << 24U));
}

void WriteConfigInt(
    std::vector<std::uint8_t>* const data,
    const char* const key,
    const int value) {
    if (!data) {
        return;
    }
    const auto offset = FindConfigValueOffset(*data, key);
    if (!offset || *offset + 4 > data->size()) {
        return;
    }
    const auto encoded = static_cast<std::uint32_t>(value);
    const auto base = *offset;
    (*data)[base] = static_cast<std::uint8_t>(encoded & 0xFFU);
    (*data)[base + 1] = static_cast<std::uint8_t>((encoded >> 8U) & 0xFFU);
    (*data)[base + 2] = static_cast<std::uint8_t>((encoded >> 16U) & 0xFFU);
    (*data)[base + 3] = static_cast<std::uint8_t>((encoded >> 24U) & 0xFFU);
}

std::vector<std::uint8_t> BuildDefaultConfigData() {
    const std::uint8_t bytes[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        'f', 'u', 'l', 'l', 's', 'c', 'r', 'e', 'e', 'n', 0, 0, 0, 0,
        'h', 'e', 'i', 'g', 'h', 't', 0x58, 0x02, 0, 0,
        's', 'y', 'n', 'c', 1, 0, 0, 0,
        'w', 'i', 'd', 'e', 's', 'c', 'r', 'e', 'e', 'n', 0, 0, 0, 0,
        'w', 'i', 'd', 't', 'h', 0x20, 0x03, 0, 0,
    };
    return {std::begin(bytes), std::end(bytes)};
}

GameConfig LoadGameConfig(const std::filesystem::path& path) {
    GameConfig config{};
    std::ifstream input(path, std::ios::binary);
    if (input) {
        config.original_data.assign(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }
    if (config.original_data.empty()) {
        config.original_data = BuildDefaultConfigData();
    }
    config.fullscreen = ReadConfigInt(config.original_data, "fullscreen", config.fullscreen);
    config.height = ReadConfigInt(config.original_data, "height", config.height);
    config.sync = ReadConfigInt(config.original_data, "sync", config.sync);
    config.widescreen = ReadConfigInt(config.original_data, "widescreen", config.widescreen);
    config.width = ReadConfigInt(config.original_data, "width", config.width);
    return config;
}

bool SaveGameConfig(
    const std::filesystem::path& path,
    const GameConfig& config,
    std::wstring* const error) {
    auto data = config.original_data.empty() ? BuildDefaultConfigData() : config.original_data;
    WriteConfigInt(&data, "fullscreen", config.fullscreen ? 1 : 0);
    WriteConfigInt(&data, "height", config.height);
    WriteConfigInt(&data, "sync", config.sync ? 1 : 0);
    WriteConfigInt(&data, "widescreen", config.widescreen ? 1 : 0);
    WriteConfigInt(&data, "width", config.width);

    if (std::filesystem::exists(path)) {
        std::error_code copy_error;
        std::filesystem::copy_file(
            path,
            path.wstring() + L".bak",
            std::filesystem::copy_options::overwrite_existing,
            copy_error);
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) {
            *error = L"无法写入 config.cfg。";
        }
        return false;
    }
    output.write(
        reinterpret_cast<const char*>(data.data()),
        static_cast<std::streamsize>(data.size()));
    if (!output) {
        if (error) {
            *error = L"保存 config.cfg 时发生错误。";
        }
        return false;
    }
    return true;
}

const pal4::inject::PersistedHookSetting* FindPersistedHook(
    const pal4::inject::InjectPersistedSettings& settings,
    const pal4::inject::HookId id) {
    const auto found = std::find_if(
        settings.hooks.begin(),
        settings.hooks.end(),
        [id](const pal4::inject::PersistedHookSetting& setting) { return setting.id == id; });
    return found == settings.hooks.end() ? nullptr : &*found;
}

pal4::inject::HookMode DefaultModeForHook(const pal4::inject::HookId id) {
    const auto inventory = pal4::inject::BuildHookInventorySkeleton();
    const auto found = std::find_if(
        inventory.begin(),
        inventory.end(),
        [id](const pal4::inject::HookDescriptor& descriptor) { return descriptor.id == id; });
    return found == inventory.end()
        ? pal4::inject::HookMode::observe_only
        : found->mode;
}

pal4::inject::InjectPersistedSettings NormalizeInjectSettings(
    const pal4::inject::InjectPersistedSettings& loaded) {
    pal4::inject::InjectPersistedSettings normalized{};
    normalized.script_mode = loaded.script_mode;
    normalized.msaa_level = loaded.msaa_level;
    normalized.bink_scaling_mode = loaded.bink_scaling_mode;
    normalized.gi_talk_volume = loaded.gi_talk_volume;
    normalized.gamepad_enabled = loaded.gamepad_enabled;
    normalized.gamepad_log_enabled = loaded.gamepad_log_enabled;
    normalized.gamepad_modern_controls = loaded.gamepad_modern_controls;
    normalized.gamepad_invert_camera_x = loaded.gamepad_invert_camera_x;
    normalized.gamepad_invert_camera_y = loaded.gamepad_invert_camera_y;
    normalized.gamepad_preserve_free_camera = loaded.gamepad_preserve_free_camera;
    normalized.gamepad_run_threshold = loaded.gamepad_run_threshold;
    normalized.gamepad_fast_run_threshold = loaded.gamepad_fast_run_threshold;
    normalized.gamepad_camera_sensitivity = loaded.gamepad_camera_sensitivity;
    normalized.gamepad_mapping = loaded.gamepad_mapping;
    normalized.borderless_window = loaded.borderless_window;
    normalized.borderless_monitor = loaded.borderless_monitor;
    for (const auto& feature : pal4::inject::BuildInjectFeatureCatalog()) {
        if (const auto* persisted = FindPersistedHook(loaded, feature.id)) {
            normalized.hooks.push_back(*persisted);
            continue;
        }
        const auto mode = DefaultModeForHook(feature.id);
        normalized.hooks.push_back({
            feature.id,
            mode,
            mode == pal4::inject::HookMode::observe_only
                ? pal4::inject::HookMode::replace_with_fallback
                : mode,
            false,
        });
    }
    if (normalized.borderless_window) {
        const auto present_hook = std::find_if(
            normalized.hooks.begin(),
            normalized.hooks.end(),
            [](const pal4::inject::PersistedHookSetting& hook) {
                return hook.id == pal4::inject::HookId::d3d9_set_present_parameters;
            });
        if (present_hook != normalized.hooks.end() &&
            (present_hook->mode == pal4::inject::HookMode::observe_only ||
             present_hook->mode == pal4::inject::HookMode::mirror_compare)) {
            present_hook->mode = pal4::inject::HookMode::replace_with_fallback;
            present_hook->active_mode = present_hook->mode;
        }
    }
    const auto process_inputs_hook = std::find_if(
        normalized.hooks.begin(),
        normalized.hooks.end(),
        [](const pal4::inject::PersistedHookSetting& hook) {
            return hook.id == pal4::inject::HookId::process_inputs;
        });
    if (process_inputs_hook != normalized.hooks.end()) {
        if (normalized.gamepad_enabled) {
            process_inputs_hook->mode = pal4::inject::HookMode::replace_with_fallback;
            process_inputs_hook->active_mode = process_inputs_hook->mode;
        } else if (process_inputs_hook->mode != pal4::inject::HookMode::observe_only) {
            process_inputs_hook->active_mode = process_inputs_hook->mode;
            process_inputs_hook->mode = pal4::inject::HookMode::observe_only;
        }
    }
    return normalized;
}

void AddCurrentResolution(std::vector<Resolution>* const resolutions, const Resolution current) {
    if (!resolutions) {
        return;
    }
    if (std::find(resolutions->begin(), resolutions->end(), current) == resolutions->end()) {
        resolutions->push_back(current);
        std::sort(resolutions->begin(), resolutions->end());
    }
}

bool ConfigureGuiLaunch(pal4::inject::LaunchOptions* const options) {
    if (!options) {
        return false;
    }

    LauncherUiState state{};
    const auto install_directory = CurrentExecutableDirectory();
    state.game_exe = install_directory / "PAL4.exe";
    state.runtime_dll = install_directory / "pal4_inject" / "runtime.dll";
    state.config_path = install_directory / "config.cfg";
    state.inject_settings_path = pal4::inject::DefaultInjectSettingsPath();
    state.bug_report = pal4::inject::LoadLatestBugReportData(
        pal4::inject::PackagedPayloadDirectory(install_directory),
        BuildBugReportRedactions(install_directory));
    state.display = LoadGameConfig(state.config_path);
    state.common_resolutions = BuildCommonResolutions();
    state.monitors = EnumerateMonitorDisplays();
    const Resolution current_resolution{state.display.width, state.display.height};
    AddCurrentResolution(&state.common_resolutions, current_resolution);

    pal4::inject::InjectPersistedSettings loaded_settings{};
    std::string settings_error;
    const auto settings_load_path =
        !std::filesystem::exists(state.inject_settings_path) &&
            std::filesystem::exists(pal4::inject::LegacyInjectPanelSettingsPath())
        ? pal4::inject::LegacyInjectPanelSettingsPath()
        : state.inject_settings_path;
    if (!pal4::inject::LoadInjectPersistedSettings(
            settings_load_path,
            &loaded_settings,
            &settings_error)) {
        ShowGuiError(L"无法读取注入配置：\n\n" + WideFromUtf8(settings_error));
        return false;
    }
    state.inject_settings = NormalizeInjectSettings(loaded_settings);
    if (const auto* monitor = ResolveConfiguredMonitor(state)) {
        state.inject_settings.borderless_monitor = monitor->device_name;
        state.display_resolutions = monitor->resolutions;
    }
    AddCurrentResolution(&state.display_resolutions, current_resolution);
    pal4::inject::launcher::SynchronizeAutomaticWidescreen(&state);
    state.script_mode = state.inject_settings.script_mode;

    std::wstring ui_error;
    if (!pal4::inject::launcher::RunLauncherUi(
            &state,
            &OpenBugReport,
            &ui_error)) {
        ShowGuiError(ui_error);
        return false;
    }
    if (!state.accepted && state.update_stage.empty()) {
        return false;
    }
    if (!state.update_stage.empty()) {
        pal4::inject::launcher::SynchronizeAutomaticWidescreen(&state);
        std::wstring config_error;
        state.inject_settings.script_mode = state.script_mode;
        if (!SaveGameConfig(state.config_path, state.display, &config_error) ||
            !pal4::inject::SaveInjectPersistedSettings(
                state.inject_settings_path, state.inject_settings, &settings_error)) {
            ShowGuiError(L"更新前保存设置失败：\n" + config_error + WideFromUtf8(settings_error));
            return false;
        }
        try {
            pal4::inject::update::LaunchInstaller(install_directory, state.update_stage);
        } catch (const std::exception& error) {
            ShowGuiError(WideFromUtf8(error.what()));
        }
        return false;
    }
    if (!std::filesystem::exists(state.game_exe)) {
        ShowGuiError(L"没有找到 PAL4.exe。\n\n请把 PAL4Plus.exe 放到 PAL4.exe 所在目录。");
        return false;
    }
    if (!std::filesystem::exists(state.runtime_dll)) {
        ShowGuiError(L"没有找到 pal4_inject\\runtime.dll。\n\n请确认发布文件已复制完整。");
        return false;
    }
    pal4::inject::launcher::SynchronizeAutomaticWidescreen(&state);
    std::wstring config_error;
    if (!SaveGameConfig(state.config_path, state.display, &config_error)) {
        ShowGuiError(config_error);
        return false;
    }
    state.inject_settings.script_mode = state.script_mode;
    if (!pal4::inject::SaveInjectPersistedSettings(
            state.inject_settings_path,
            state.inject_settings,
            &settings_error)) {
        ShowGuiError(L"无法保存注入配置：\n\n" + WideFromUtf8(settings_error));
        return false;
    }

    options->executable_path = state.game_exe;
    options->dll_path = state.runtime_dll;
    options->script_mode = state.script_mode;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    pal4::inject::LaunchOptions options;
    options.dll_path = DefaultRuntimeDllPath();
    int wide_count = 0;
    auto* wide_args = CommandLineToArgvW(GetCommandLineW(), &wide_count);
    const bool update_start = wide_count == 3 && std::wstring_view(wide_args[1]) == L"--update-ready";
    if (wide_count == 3 && std::wstring_view(wide_args[1]) == L"--apply-update") {
        const std::filesystem::path job = wide_args[2];
        LocalFree(wide_args);
        return pal4::inject::update::RunInstaller(job);
    }
    if (update_start) pal4::inject::update::SetReadyEvent(wide_args[2]);
    if (wide_args) LocalFree(wide_args);
    const bool gui_mode = argc == 1 || update_start;
    if (gui_mode && !update_start) {
        try {
            if (pal4::inject::update::RecoverInterruptedUpdate(CurrentExecutableDirectory())) return 0;
        } catch (const std::exception& error) {
            ShowGuiError(WideFromUtf8(error.what()));
            return 1;
        }
    }
    if (gui_mode && !ConfigureGuiLaunch(&options)) {
        return 0;
    }
    if (!gui_mode) {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--game-root" && index + 1 < argc) {
                options.game_root = argv[++index];
            } else if (argument == "--exe" && index + 1 < argc) {
                options.executable_path = argv[++index];
            } else if (argument == "--dll" && index + 1 < argc) {
                options.dll_path = argv[++index];
            } else if (argument == "--script-mode" && index + 1 < argc) {
                if (!pal4::inject::TryParseScriptMode(argv[++index], &options.script_mode) ||
                    options.script_mode == pal4::inject::ScriptMode::inherit) {
                    std::cerr << "invalid --script-mode, expected cs or csb\n";
                    return 1;
                }
            } else if (argument == "--ready-timeout-ms" && index + 1 < argc) {
                options.ready_timeout_ms = static_cast<DWORD>(std::stoul(argv[++index]));
            } else if (argument == "--no-resume") {
                options.resume_after_ready = false;
            } else if (argument == "--background" || argument == "--minimized") {
                options.background_window = true;
            } else if (argument == "--arg" && index + 1 < argc) {
                options.child_args.push_back(argv[++index]);
            } else {
                PrintUsage();
                return 1;
            }
        }
        if (options.game_root.empty() && options.executable_path.empty()) {
            PrintUsage();
            return 1;
        }
    }

    pal4::inject::InjectedProcess process{};
    const auto result = pal4::inject::LaunchInjectedProcess(options, &process);
    if (!result.ok) {
        if (gui_mode) {
            ShowGuiError(WideFromUtf8(result.error));
        }
        std::cerr << result.error << '\n';
        return 1;
    }

    std::cout
        << "ok pid=" << result.process_id
        << " pipe=" << result.pipe_name
        << " ready_event=" << result.ready_event_name
        << " script_mode=" << pal4::inject::ToString(result.script_mode)
        << " resumed=" << (options.resume_after_ready ? 1 : 0)
        << " background=" << (options.background_window ? 1 : 0)
        << '\n';
    process.Close();
    return 0;
}
